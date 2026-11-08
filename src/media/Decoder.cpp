#include "media/Decoder.h"

#include <QThread>
#include <QtGlobal>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace bc {
namespace {

QString ffError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

std::atomic<bool> g_hwEnabled{true};

#ifdef Q_OS_WIN
constexpr AVHWDeviceType kHwType = AV_HWDEVICE_TYPE_D3D11VA;
constexpr AVPixelFormat kHwFormat = AV_PIX_FMT_D3D11;
#elif defined(Q_OS_MACOS)
constexpr AVHWDeviceType kHwType = AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
constexpr AVPixelFormat kHwFormat = AV_PIX_FMT_VIDEOTOOLBOX;
#else
constexpr AVHWDeviceType kHwType = AV_HWDEVICE_TYPE_VAAPI;
constexpr AVPixelFormat kHwFormat = AV_PIX_FMT_VAAPI;
#endif

// One GPU device shared by every decoder (creating one per file is slow and
// wastes VRAM on integrated graphics).
AVBufferRef* sharedHwDevice()
{
    static std::once_flag once;
    static AVBufferRef* device = nullptr;
    std::call_once(once, [] {
        if (av_hwdevice_ctx_create(&device, kHwType, nullptr, nullptr, 0) < 0) device = nullptr;
    });
    return device;
}

AVPixelFormat pickHwFormat(AVCodecContext* ctx, const AVPixelFormat* formats)
{
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f)
        if (*f == kHwFormat) return *f;
    // GPU cannot do this stream: take the first software format.
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*f);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *f;
    }
    Q_UNUSED(ctx);
    return formats[0];
}

bool wantsHardware(AVCodecID id, int width)
{
    // Intra-frame proxies (MJPEG/ProRes/DNx) are cheap on the CPU already.
    if (width < 1280) return false;
    return id == AV_CODEC_ID_H264 || id == AV_CODEC_ID_HEVC || id == AV_CODEC_ID_VP9 || id == AV_CODEC_ID_AV1
           || id == AV_CODEC_ID_MPEG2VIDEO;
}

Tick toTicks(int64_t pts, AVRational tb)
{
    if (pts == AV_NOPTS_VALUE) return -1;
    return secToTicks(static_cast<double>(pts) * av_q2d(tb));
}

} // namespace

// --------------------------------------------------------------- VideoDecoder

void VideoDecoder::setHardwareDecodeEnabled(bool enabled) { g_hwEnabled = enabled; }
bool VideoDecoder::hardwareDecodeEnabled() { return g_hwEnabled; }

VideoDecoder::VideoDecoder()
{
    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
    m_swFrame = av_frame_alloc();
}

VideoDecoder::~VideoDecoder()
{
    close();
    av_packet_free(&m_packet);
    av_frame_free(&m_frame);
    av_frame_free(&m_swFrame);
}

bool VideoDecoder::open(const QString& path, QString* error)
{
    close();

    const QByteArray utf8 = path.toUtf8();
    int ret = avformat_open_input(&m_fmt, utf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(path, ffError(ret));
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        if (error) *error = QStringLiteral("No stream info for %1").arg(path);
        close();
        return false;
    }

    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        if (error) *error = QStringLiteral("No video stream in %1").arg(path);
        close();
        return false;
    }

    AVStream* st = m_fmt->streams[m_stream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        if (error) *error = QStringLiteral("Unsupported codec in %1").arg(path);
        close();
        return false;
    }

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx || avcodec_parameters_to_context(m_ctx, st->codecpar) < 0) {
        if (error) *error = QStringLiteral("Cannot init decoder for %1").arg(path);
        close();
        return false;
    }

    m_hw = false;
    if (g_hwEnabled && wantsHardware(st->codecpar->codec_id, st->codecpar->width)) {
        if (AVBufferRef* device = sharedHwDevice()) {
            m_ctx->hw_device_ctx = av_buffer_ref(device);
            m_ctx->get_format = pickHwFormat;
            m_hw = true;
        }
    }
    // Software threads: leave a core for UI + audio on dual-core machines.
    const int cores = std::max(1, QThread::idealThreadCount());
    m_ctx->thread_count = m_hw ? 1 : std::clamp(cores - 1, 1, 4);
    m_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    ret = avcodec_open2(m_ctx, codec, nullptr);
    if (ret < 0 && m_hw) {
        // Retry in software.
        avcodec_free_context(&m_ctx);
        m_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_ctx, st->codecpar);
        m_ctx->thread_count = std::clamp(cores - 1, 1, 4);
        m_ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        m_hw = false;
        ret = avcodec_open2(m_ctx, codec, nullptr);
    }
    if (ret < 0) {
        if (error) *error = QStringLiteral("Cannot open codec: %1").arg(ffError(ret));
        close();
        return false;
    }

    m_width = st->codecpar->width;
    m_height = st->codecpar->height;

    AVRational fr = av_guess_frame_rate(m_fmt, st, nullptr);
    if (fr.num <= 0 || fr.den <= 0) fr = AVRational{25, 1};
    m_rate = FrameRate{fr.num, fr.den};

    if (m_fmt->duration != AV_NOPTS_VALUE && m_fmt->duration > 0)
        m_duration = static_cast<Tick>(m_fmt->duration) * kTicksPerSecond / AV_TIME_BASE;
    else if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
        m_duration = toTicks(st->duration, st->time_base);

    m_lastDecoded = -1;
    return true;
}

void VideoDecoder::close()
{
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_ctx) avcodec_free_context(&m_ctx);
    if (m_fmt) avformat_close_input(&m_fmt);
    m_stream = -1;
    m_width = m_height = 0;
    m_lastDecoded = -1;
    m_flushed = false;
}

bool VideoDecoder::seekTo(Tick sourceTime)
{
    if (!m_fmt || m_stream < 0) return false;

    const int64_t usec = static_cast<int64_t>(ticksToSec(sourceTime) * 1'000'000.0);
    const int64_t ts = av_rescale_q(usec, AV_TIME_BASE_Q, m_fmt->streams[m_stream]->time_base);

    if (av_seek_frame(m_fmt, m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0)
        return false;

    avcodec_flush_buffers(m_ctx);
    m_lastDecoded = -1;
    m_flushed = false;
    return true;
}

QImage VideoDecoder::toImage(AVFrame* frame, int outWidth, int outHeight)
{
    if (!frame || outWidth <= 0 || outHeight <= 0) return {};

    // GPU surface: copy back only the frame we actually show.
    if (frame->format == kHwFormat) {
        av_frame_unref(m_swFrame);
        if (av_hwframe_transfer_data(m_swFrame, frame, 0) < 0) {
            // Driver hiccup, device reset or an unsupported surface: stop trusting the GPU path.
            // Every decoder opened from now on is software, and this one gets reopened.
            g_hwEnabled = false;
            m_hwFailed = true;
            return {};
        }
        frame = m_swFrame;
    }

    // Big downscales (4K -> preview) use the fast path; quality is set by the
    // preview size anyway.
    const bool bigDownscale = frame->width >= outWidth * 2;
    m_sws = sws_getCachedContext(
        m_sws,
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        outWidth, outHeight, AV_PIX_FMT_BGRA,
        bigDownscale ? SWS_FAST_BILINEAR : SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws) return {};

    QImage image(outWidth, outHeight, QImage::Format_ARGB32);
    uint8_t* dstData[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstLines[4] = {static_cast<int>(image.bytesPerLine()), 0, 0, 0};

    sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height, dstData, dstLines);
    return image;
}

QImage VideoDecoder::frameAt(Tick sourceTime, int outWidth, int outHeight)
{
    if (!m_ctx) return {};

    const Tick frameSpan = m_rate.ticksPerFrame();
    const bool needSeek = m_lastDecoded < 0
                          || sourceTime < m_lastDecoded - frameSpan
                          || sourceTime - m_lastDecoded > 2 * kTicksPerSecond;

    if (needSeek && !seekTo(sourceTime)) return {};

    while (true) {
        int ret = avcodec_receive_frame(m_ctx, m_frame);
        if (ret == AVERROR(EAGAIN)) {
            // Feed another packet.
            bool sentSomething = false;
            while (av_read_frame(m_fmt, m_packet) >= 0) {
                const int pktStream = m_packet->stream_index;
                if (pktStream == m_stream) {
                    int sendRet = avcodec_send_packet(m_ctx, m_packet);
                    av_packet_unref(m_packet);
                    sentSomething = true;
                    if (sendRet < 0 && sendRet != AVERROR(EAGAIN)) break;
                    break;
                }
                av_packet_unref(m_packet);
            }

            if (!sentSomething && !m_flushed) {
                avcodec_send_packet(m_ctx, nullptr);   // drain
                m_flushed = true;
                continue;
            }
            if (!sentSomething && m_flushed) {
                // Nothing left; return the closest frame we already have.
                return {};
            }
            continue;
        }
        if (ret == AVERROR_EOF) return {};
        if (ret < 0) return {};

        const Tick ft = toTicks(m_frame->best_effort_timestamp, m_fmt->streams[m_stream]->time_base);
        if (ft >= 0) m_lastDecoded = ft;

        const bool reachedTarget = (ft < 0) || (ft + frameSpan > sourceTime);
        QImage result;
        if (reachedTarget)
            result = toImage(m_frame, outWidth, outHeight);

        av_frame_unref(m_frame);
        if (reachedTarget) return result;
    }
}

// --------------------------------------------------------------- AudioDecoder

AudioDecoder::AudioDecoder()
{
    m_packet = av_packet_alloc();
    m_frame = av_frame_alloc();
}

AudioDecoder::~AudioDecoder()
{
    close();
    av_packet_free(&m_packet);
    av_frame_free(&m_frame);
}

bool AudioDecoder::open(const QString& path, QString* error)
{
    close();

    const QByteArray utf8 = path.toUtf8();
    int ret = avformat_open_input(&m_fmt, utf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        if (error) *error = ffError(ret);
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        if (error) *error = QStringLiteral("No stream info");
        close();
        return false;
    }

    m_stream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        if (error) *error = QStringLiteral("No audio stream");
        close();
        return false;
    }

    AVStream* st = m_fmt->streams[m_stream];
    const AVCodec* codec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!codec) {
        close();
        return false;
    }

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx || avcodec_parameters_to_context(m_ctx, st->codecpar) < 0) {
        close();
        return false;
    }
    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        close();
        return false;
    }

    AVChannelLayout inLayout = st->codecpar->ch_layout;
    if (inLayout.nb_channels <= 0) av_channel_layout_default(&inLayout, 2);

    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, kChannels);

    ret = swr_alloc_set_opts2(&m_swr,
                              &outLayout, AV_SAMPLE_FMT_FLT, kSampleRate,
                              &inLayout, static_cast<AVSampleFormat>(st->codecpar->format),
                              st->codecpar->sample_rate, 0, nullptr);
    if (ret < 0 || swr_init(m_swr) < 0) {
        if (error) *error = QStringLiteral("Cannot init resampler");
        close();
        return false;
    }

    if (m_fmt->duration != AV_NOPTS_VALUE && m_fmt->duration > 0)
        m_duration = static_cast<Tick>(m_fmt->duration) * kTicksPerSecond / AV_TIME_BASE;
    else if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
        m_duration = toTicks(st->duration, st->time_base);

    return true;
}

void AudioDecoder::close()
{
    if (m_swr) swr_free(&m_swr);
    if (m_ctx) avcodec_free_context(&m_ctx);
    if (m_fmt) avformat_close_input(&m_fmt);
    m_stream = -1;
    m_eof = false;
    m_buf.clear();
    m_bufRead = 0;
}

bool AudioDecoder::seekTo(Tick sourceTime)
{
    if (!m_fmt || m_stream < 0) return false;
    const int64_t usec = static_cast<int64_t>(ticksToSec(sourceTime) * 1'000'000.0);
    const int64_t ts = av_rescale_q(usec, AV_TIME_BASE_Q, m_fmt->streams[m_stream]->time_base);
    if (av_seek_frame(m_fmt, m_stream, ts, AVSEEK_FLAG_BACKWARD) < 0) return false;
    avcodec_flush_buffers(m_ctx);
    m_eof = false;
    m_buf.clear();
    m_bufRead = 0;
    return true;
}

bool AudioDecoder::receiveToBuffer()
{
    while (true) {
        int ret = avcodec_receive_frame(m_ctx, m_frame);
        if (ret == AVERROR(EAGAIN)) {
            bool sent = false;
            while (av_read_frame(m_fmt, m_packet) >= 0) {
                if (m_packet->stream_index == m_stream) {
                    avcodec_send_packet(m_ctx, m_packet);
                    av_packet_unref(m_packet);
                    sent = true;
                    break;
                }
                av_packet_unref(m_packet);
            }
            if (!sent) {
                if (!m_eof) {
                    avcodec_send_packet(m_ctx, nullptr);
                    m_eof = true;
                } else {
                    return false;
                }
            }
            continue;
        }
        if (ret < 0) return false;

        const int outSamples = swr_get_out_samples(m_swr, m_frame->nb_samples);
        if (outSamples <= 0) {
            av_frame_unref(m_frame);
            return false;
        }

        m_buf.resize(outSamples * kChannels);
        uint8_t* dst = reinterpret_cast<uint8_t*>(m_buf.data());
        const int converted = swr_convert(m_swr, &dst, outSamples,
                                          const_cast<const uint8_t**>(m_frame->extended_data),
                                          m_frame->nb_samples);
        av_frame_unref(m_frame);

        if (converted <= 0) return false;
        m_buf.resize(converted * kChannels);
        m_bufRead = 0;
        return true;
    }
}

int AudioDecoder::read(float* out, int maxFrames)
{
    int written = 0;
    while (written < maxFrames) {
        const int available = m_buf.size() / kChannels - m_bufRead;
        if (available > 0) {
            const int n = std::min(available, maxFrames - written);
            std::memcpy(out + written * kChannels,
                        m_buf.constData() + m_bufRead * kChannels,
                        static_cast<size_t>(n) * kChannels * sizeof(float));
            m_bufRead += n;
            written += n;
            continue;
        }
        m_buf.clear();
        m_bufRead = 0;
        if (!receiveToBuffer()) break;
    }

    if (written < maxFrames) {
        std::memset(out + written * kChannels, 0,
                    static_cast<size_t>(maxFrames - written) * kChannels * sizeof(float));
    }
    return written;
}

} // namespace bc
