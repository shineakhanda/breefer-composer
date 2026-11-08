#include "media/Exporter.h"

#include "engine/Compositor.h"
#include "media/AudioMix.h"
#include "media/Decoder.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QThread>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace bc {
namespace {

QString ffError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

QStringList encoderCandidates(const QString& codec, bool hardware)
{
    QStringList list;
    if (codec == QLatin1String("h264")) {
        if (hardware) list << "h264_nvenc" << "h264_qsv" << "h264_amf" << "h264_mf";
        list << "libopenh264" << "h264_mf";
    } else if (codec == QLatin1String("hevc")) {
        if (hardware) list << "hevc_nvenc" << "hevc_qsv" << "hevc_amf";
        list << "hevc_mf" << "libkvazaar";
    } else if (codec == QLatin1String("prores")) {
        list << "prores_ks" << "prores";
    } else if (codec == QLatin1String("dnxhr")) {
        list << "dnxhd";
    } else if (codec == QLatin1String("vp9")) {
        list << "libvpx-vp9";
    } else if (codec == QLatin1String("av1")) {
        if (hardware) list << "av1_nvenc" << "av1_qsv" << "av1_amf";
        list << "libsvtav1" << "libaom-av1";
    } else if (codec == QLatin1String("mpeg4")) {
        list << "mpeg4";
    }
    list.removeDuplicates();
    return list;
}

AVPixelFormat preferredPixFmt(const QString& codec)
{
    if (codec == QLatin1String("prores")) return AV_PIX_FMT_YUV422P10LE;
    if (codec == QLatin1String("dnxhr")) return AV_PIX_FMT_YUV422P;
    return AV_PIX_FMT_YUV420P;
}

AVPixelFormat choosePixFmt(AVCodecContext* ctx, const AVCodec* codec, AVPixelFormat preferred)
{
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) < 0
        || !configs || count <= 0)
        return preferred;
    const auto* fmts = static_cast<const AVPixelFormat*>(configs);
    for (int i = 0; i < count; ++i)
        if (fmts[i] == preferred) return preferred;
    // Prefer a plain system-memory format over hardware surfaces.
    for (int i = 0; i < count; ++i) {
        const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(fmts[i]);
        if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return fmts[i];
    }
    return fmts[0];
}

AVSampleFormat chooseSampleFmt(AVCodecContext* ctx, const AVCodec* codec, AVSampleFormat preferred)
{
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs, &count) < 0
        || !configs || count <= 0)
        return preferred;
    const auto* fmts = static_cast<const AVSampleFormat*>(configs);
    for (int i = 0; i < count; ++i)
        if (fmts[i] == preferred) return preferred;
    return fmts[0];
}

} // namespace

// ------------------------------------------------------------------ presets

QVector<Exporter::Preset> Exporter::presets(int seqWidth, int seqHeight)
{
    QVector<Preset> list;
    auto add = [&](const QString& cat, const QString& name, auto configure) {
        Preset p;
        p.category = cat;
        p.name = name;
        p.settings.width = seqWidth;
        p.settings.height = seqHeight;
        configure(p.settings);
        list.push_back(p);
    };

    add(QStringLiteral("Match Source"), QStringLiteral("Match Sequence - H.264 High Bitrate"), [](Settings& s) {
        s.bitrateKbps = 20000;
    });
    add(QStringLiteral("Web"), QStringLiteral("YouTube 1080p HD"), [](Settings& s) {
        s.width = 1920; s.height = 1080; s.bitrateKbps = 16000;
    });
    add(QStringLiteral("Web"), QStringLiteral("YouTube 2160p 4K UHD (HEVC)"), [](Settings& s) {
        s.width = 3840; s.height = 2160; s.videoCodec = QStringLiteral("hevc"); s.bitrateKbps = 45000;
    });
    add(QStringLiteral("Web"), QStringLiteral("YouTube 720p HD"), [](Settings& s) {
        s.width = 1280; s.height = 720; s.bitrateKbps = 7500;
    });
    add(QStringLiteral("Social"), QStringLiteral("Vertical 9:16 (Shorts / Reels / TikTok)"), [](Settings& s) {
        s.width = 1080; s.height = 1920; s.bitrateKbps = 14000;
    });
    add(QStringLiteral("Social"), QStringLiteral("Square 1:1 (Instagram feed)"), [](Settings& s) {
        s.width = 1080; s.height = 1080; s.bitrateKbps = 10000;
    });
    add(QStringLiteral("Social"), QStringLiteral("Portrait 4:5 (Instagram feed)"), [](Settings& s) {
        s.width = 1080; s.height = 1350; s.bitrateKbps = 10000;
    });
    add(QStringLiteral("Master"), QStringLiteral("Apple ProRes 422 (.mov)"), [](Settings& s) {
        s.container = QStringLiteral("mov"); s.videoCodec = QStringLiteral("prores");
        s.audioCodec = QStringLiteral("pcm");
    });
    add(QStringLiteral("Master"), QStringLiteral("Avid DNxHR HQ (.mov)"), [](Settings& s) {
        s.container = QStringLiteral("mov"); s.videoCodec = QStringLiteral("dnxhr");
        s.audioCodec = QStringLiteral("pcm");
    });
    add(QStringLiteral("Web"), QStringLiteral("WebM VP9"), [](Settings& s) {
        s.container = QStringLiteral("webm"); s.videoCodec = QStringLiteral("vp9");
        s.audioCodec = QStringLiteral("opus"); s.bitrateKbps = 8000;
    });
    add(QStringLiteral("Web"), QStringLiteral("AV1 (.mkv)"), [](Settings& s) {
        s.container = QStringLiteral("mkv"); s.videoCodec = QStringLiteral("av1"); s.bitrateKbps = 8000;
    });
    add(QStringLiteral("Audio"), QStringLiteral("WAV 48 kHz 16-bit"), [](Settings& s) {
        s.container = QStringLiteral("wav"); s.videoCodec = QStringLiteral("none");
        s.audioCodec = QStringLiteral("pcm");
    });
    add(QStringLiteral("Audio"), QStringLiteral("AAC audio only (.m4a)"), [](Settings& s) {
        s.container = QStringLiteral("m4a"); s.videoCodec = QStringLiteral("none");
        s.audioBitrateKbps = 256;
    });
    return list;
}

QString Exporter::extensionFor(const Settings& s)
{
    return s.container;
}

QStringList Exporter::availableVideoCodecs()
{
    QStringList out;
    for (const QString& codec : {QStringLiteral("h264"), QStringLiteral("hevc"), QStringLiteral("prores"),
                                 QStringLiteral("dnxhr"), QStringLiteral("vp9"), QStringLiteral("av1")}) {
        for (const QString& name : encoderCandidates(codec, true)) {
            if (avcodec_find_encoder_by_name(name.toLatin1().constData())) {
                out << codec;
                break;
            }
        }
    }
    return out;
}

// ------------------------------------------------------------------- worker

class ExportWorker : public QThread {
    Q_OBJECT

public:
    struct Job {
        Sequence sequence;
        QVector<MediaRef> media;
        Exporter::Settings settings;
        QVector<Sequence> library;
    };

    void enqueue(const Job& job)
    {
        {
            QMutexLocker lock(&m_mutex);
            m_queue.enqueue(job);
        }
        if (!isRunning()) start();
    }

    void cancel()
    {
        QMutexLocker lock(&m_mutex);
        m_cancelled = true;
    }

    void cancelAll()
    {
        QMutexLocker lock(&m_mutex);
        m_queue.clear();
        m_cancelled = true;
    }

    int pending() const
    {
        QMutexLocker lock(const_cast<QMutex*>(&m_mutex));
        return m_queue.size();
    }

signals:
    void progressed(int percent);
    void started(const QString& label, int remaining);
    void encoder(const QString& description);
    void finished(const QString& path);
    void failed(const QString& error);
    void empty();

protected:
    void run() override
    {
        for (;;) {
            Job job;
            int remaining = 0;
            {
                QMutexLocker lock(&m_mutex);
                if (m_queue.isEmpty()) break;
                job = m_queue.dequeue();
                remaining = m_queue.size();
                m_cancelled = false;
            }
            emit started(job.settings.label.isEmpty() ? QFileInfo(job.settings.outputPath).fileName()
                                                      : job.settings.label,
                         remaining);
            QString error;
            if (exportSequence(job, &error))
                emit finished(job.settings.outputPath);
            else
                emit failed(error);
        }
        emit empty();
    }

private:
    bool cancelled() const
    {
        QMutexLocker lock(const_cast<QMutex*>(&m_mutex));
        return m_cancelled;
    }

    struct Output {
        AVFormatContext* fmt = nullptr;
        AVCodecContext* video = nullptr;
        AVStream* videoStream = nullptr;
        AVCodecContext* audio = nullptr;
        AVStream* audioStream = nullptr;
        AVPacket* packet = nullptr;

        ~Output()
        {
            if (packet) av_packet_free(&packet);
            if (video) avcodec_free_context(&video);
            if (audio) avcodec_free_context(&audio);
            if (fmt) {
                if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE)) avio_closep(&fmt->pb);
                avformat_free_context(fmt);
            }
        }
    };

    bool openVideo(Output& out, const Exporter::Settings& s, const Sequence& seq, int width, int height,
                   QString* chosen, QString* error)
    {
        const QStringList candidates = encoderCandidates(s.videoCodec, s.preferHardware);
        QString lastError = tr("No encoder available for %1").arg(s.videoCodec);

        for (const QString& name : candidates) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name.toLatin1().constData());
            if (!codec) continue;

            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            ctx->width = width;
            ctx->height = height;
            ctx->time_base = AVRational{seq.rate.den, seq.rate.num};
            ctx->framerate = AVRational{seq.rate.num, seq.rate.den};
            ctx->sample_aspect_ratio = AVRational{1, 1};
            ctx->pix_fmt = choosePixFmt(ctx, codec, preferredPixFmt(s.videoCodec));
            ctx->gop_size = std::max(1, static_cast<int>(std::lround(seq.rate.value())) * 2);
            ctx->max_b_frames = 0;
            ctx->color_primaries = AVCOL_PRI_BT709;
            ctx->color_trc = AVCOL_TRC_BT709;
            ctx->colorspace = AVCOL_SPC_BT709;
            ctx->color_range = AVCOL_RANGE_MPEG;
            if (s.videoCodec != QLatin1String("prores") && s.videoCodec != QLatin1String("dnxhr")) {
                ctx->bit_rate = static_cast<int64_t>(s.bitrateKbps) * 1000;
                ctx->rc_max_rate = ctx->bit_rate * 3 / 2;
                ctx->rc_buffer_size = static_cast<int>(ctx->bit_rate * 2);
            }
            if (out.fmt->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            AVDictionary* opts = nullptr;
            if (name == QLatin1String("prores_ks")) av_dict_set(&opts, "profile", "2", 0);
            if (name == QLatin1String("dnxhd")) av_dict_set(&opts, "profile", "dnxhr_hq", 0);
            if (name == QLatin1String("libsvtav1")) av_dict_set(&opts, "preset", "10", 0);
            if (name == QLatin1String("libaom-av1")) av_dict_set(&opts, "cpu-used", "8", 0);
            if (name == QLatin1String("libvpx-vp9")) {
                av_dict_set(&opts, "deadline", "realtime", 0);
                av_dict_set(&opts, "cpu-used", "6", 0);
                av_dict_set(&opts, "row-mt", "1", 0);
            }
            if (name.endsWith(QLatin1String("_nvenc"))) av_dict_set(&opts, "preset", "p5", 0);
            if (name == QLatin1String("h264_mf") || name == QLatin1String("hevc_mf"))
                av_dict_set(&opts, "hw_encoding", "0", 0);

            const int rc = avcodec_open2(ctx, codec, &opts);
            av_dict_free(&opts);
            if (rc < 0) {
                lastError = tr("%1: %2").arg(name, ffError(rc));
                avcodec_free_context(&ctx);
                continue;
            }

            out.video = ctx;
            out.videoStream = avformat_new_stream(out.fmt, nullptr);
            avcodec_parameters_from_context(out.videoStream->codecpar, ctx);
            out.videoStream->time_base = ctx->time_base;
            out.videoStream->avg_frame_rate = ctx->framerate;
            if (chosen) *chosen = name;
            return true;
        }
        if (error) *error = lastError;
        return false;
    }

    bool openAudio(Output& out, const Exporter::Settings& s)
    {
        QStringList names;
        if (s.audioCodec == QLatin1String("pcm")) names << "pcm_s16le";
        else if (s.audioCodec == QLatin1String("opus")) names << "libopus" << "opus";
        else names << "aac" << "aac_mf";

        for (const QString& name : names) {
            const AVCodec* codec = avcodec_find_encoder_by_name(name.toLatin1().constData());
            if (!codec) continue;
            AVCodecContext* ctx = avcodec_alloc_context3(codec);
            ctx->sample_rate = AudioDecoder::kSampleRate;
            av_channel_layout_default(&ctx->ch_layout, 2);
            ctx->sample_fmt = chooseSampleFmt(ctx, codec,
                                              name == QLatin1String("pcm_s16le") ? AV_SAMPLE_FMT_S16
                                                                                 : AV_SAMPLE_FMT_FLTP);
            ctx->bit_rate = static_cast<int64_t>(s.audioBitrateKbps) * 1000;
            ctx->time_base = AVRational{1, AudioDecoder::kSampleRate};
            ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
            if (out.fmt->oformat->flags & AVFMT_GLOBALHEADER) ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if (avcodec_open2(ctx, codec, nullptr) < 0) {
                avcodec_free_context(&ctx);
                continue;
            }
            out.audio = ctx;
            out.audioStream = avformat_new_stream(out.fmt, nullptr);
            avcodec_parameters_from_context(out.audioStream->codecpar, ctx);
            out.audioStream->time_base = ctx->time_base;
            return true;
        }
        return false;
    }

    static void drain(Output& out, AVCodecContext* ctx, AVStream* stream)
    {
        while (avcodec_receive_packet(ctx, out.packet) == 0) {
            out.packet->stream_index = stream->index;
            av_packet_rescale_ts(out.packet, ctx->time_base, stream->time_base);
            av_interleaved_write_frame(out.fmt, out.packet);
            av_packet_unref(out.packet);
        }
    }

    bool exportSequence(const Job& job, QString* error)
    {
        const Sequence& seq = job.sequence;
        const Exporter::Settings& s = job.settings;

        const bool wantVideo = s.videoCodec != QLatin1String("none");
        const int width = std::max(16, s.width & ~1);
        const int height = std::max(16, s.height & ~1);

        const Tick rangeStart = std::clamp<Tick>(s.rangeStart, 0, seq.duration);
        const Tick rangeEnd = (s.rangeEnd > rangeStart) ? std::min(s.rangeEnd, seq.duration) : seq.duration;
        if (rangeEnd <= rangeStart) {
            if (error) *error = tr("Nothing to export in the selected range");
            return false;
        }

        QDir().mkpath(QFileInfo(s.outputPath).absolutePath());

        Output out;
        const QByteArray path = s.outputPath.toUtf8();
        const QByteArray fmtName = s.container == QLatin1String("mkv") ? QByteArray("matroska")
                                 : s.container == QLatin1String("m4a") ? QByteArray("ipod")
                                 : s.container.toLatin1();
        if (avformat_alloc_output_context2(&out.fmt, nullptr, fmtName.constData(), path.constData()) < 0
            || !out.fmt) {
            if (error) *error = tr("Cannot create a %1 file").arg(s.container);
            return false;
        }

        QString chosen;
        if (wantVideo && !openVideo(out, s, seq, width, height, &chosen, error)) return false;
        if (wantVideo) emit encoder(tr("Video encoder: %1 (%2x%3)").arg(chosen).arg(width).arg(height));

        if (s.includeAudio || !wantVideo) {
            if (!openAudio(out, s) && !wantVideo) {
                if (error) *error = tr("No audio encoder available");
                return false;
            }
        }

        if (!(out.fmt->oformat->flags & AVFMT_NOFILE)) {
            const int rc = avio_open(&out.fmt->pb, path.constData(), AVIO_FLAG_WRITE);
            if (rc < 0) {
                if (error) *error = tr("Cannot open output for writing: %1").arg(ffError(rc));
                return false;
            }
        }
        if (const int rc = avformat_write_header(out.fmt, nullptr); rc < 0) {
            if (error) *error = tr("Cannot write container header: %1").arg(ffError(rc));
            return false;
        }
        out.packet = av_packet_alloc();

        Compositor compositor;
        compositor.setPreviewSize(width, height);
        compositor.setHighQuality(s.highQualityScaling);
        compositor.setLibrary(job.library);

        const Tick tpf = seq.rate.ticksPerFrame();
        const qint64 firstFrame = rangeStart / tpf;
        const qint64 lastFrame = std::max<qint64>(firstFrame + 1, (rangeEnd + tpf - 1) / tpf);
        const qint64 totalFrames = lastFrame - firstFrame;

        SwsContext* sws = nullptr;
        AVFrame* yuv = nullptr;
        if (out.video) {
            yuv = av_frame_alloc();
            yuv->format = out.video->pix_fmt;
            yuv->width = width;
            yuv->height = height;
            av_frame_get_buffer(yuv, 32);
        }

        // Audio state. Decoders always read originals, never proxies.
        QVector<MediaRef> audioMedia = job.media;
        for (auto& m : audioMedia) m.proxyPath.clear();
        std::unordered_map<std::string, std::unique_ptr<AudioDecoder>> decoders;
        std::unordered_map<std::string, Tick> decoderPos;
        std::vector<float> scratch;
        auto lookup = [&](const QString& clipId, const QString& mediaId) -> AudioDecoder* {
            const std::string key = clipId.toStdString();
            auto it = decoders.find(key);
            if (it != decoders.end()) return it->second.get();
            const MediaRef* ref = nullptr;
            for (const auto& m : audioMedia)
                if (m.id == mediaId) ref = &m;
            std::unique_ptr<AudioDecoder> dec;
            if (ref && ref->hasAudio && !ref->offline) {
                dec = std::make_unique<AudioDecoder>();
                if (!dec->open(ref->path)) dec.reset();
            }
            AudioDecoder* raw = dec.get();
            decoders[key] = std::move(dec);
            return raw;
        };

        std::vector<float> pending;
        int64_t samplesWritten = 0;
        const int audioFrameSize = out.audio ? (out.audio->frame_size > 0 ? out.audio->frame_size : 1024) : 0;
        constexpr Tick kTps = kTicksPerSecond / AudioDecoder::kSampleRate;
        Tick audioCursor = rangeStart;

        auto encodeAudio = [&](const float* interleaved, int frames) {
            AVFrame* frame = av_frame_alloc();
            frame->format = out.audio->sample_fmt;
            frame->sample_rate = AudioDecoder::kSampleRate;
            av_channel_layout_copy(&frame->ch_layout, &out.audio->ch_layout);
            frame->nb_samples = frames;
            av_frame_get_buffer(frame, 0);
            const auto fmt = static_cast<AVSampleFormat>(frame->format);
            for (int i = 0; i < frames; ++i) {
                const float l = std::clamp(interleaved[i * 2], -1.0f, 1.0f);
                const float r = std::clamp(interleaved[i * 2 + 1], -1.0f, 1.0f);
                if (fmt == AV_SAMPLE_FMT_FLTP) {
                    reinterpret_cast<float*>(frame->data[0])[i] = l;
                    reinterpret_cast<float*>(frame->data[1])[i] = r;
                } else if (fmt == AV_SAMPLE_FMT_FLT) {
                    reinterpret_cast<float*>(frame->data[0])[i * 2] = l;
                    reinterpret_cast<float*>(frame->data[0])[i * 2 + 1] = r;
                } else if (fmt == AV_SAMPLE_FMT_S16) {
                    reinterpret_cast<int16_t*>(frame->data[0])[i * 2] = static_cast<int16_t>(l * 32767.0f);
                    reinterpret_cast<int16_t*>(frame->data[0])[i * 2 + 1] = static_cast<int16_t>(r * 32767.0f);
                } else if (fmt == AV_SAMPLE_FMT_S16P) {
                    reinterpret_cast<int16_t*>(frame->data[0])[i] = static_cast<int16_t>(l * 32767.0f);
                    reinterpret_cast<int16_t*>(frame->data[1])[i] = static_cast<int16_t>(r * 32767.0f);
                }
            }
            frame->pts = samplesWritten;
            samplesWritten += frames;
            if (avcodec_send_frame(out.audio, frame) >= 0) drain(out, out.audio, out.audioStream);
            av_frame_free(&frame);
        };

        auto pumpAudioUntil = [&](Tick until) {
            if (!out.audio) return;
            while (audioCursor < until) {
                const int frames = static_cast<int>(std::min<Tick>(1024, (until - audioCursor + kTps - 1) / kTps));
                if (frames <= 0) break;
                std::vector<float> block(static_cast<size_t>(frames) * 2, 0.0f);
                mixSequenceBlock(seq, audioCursor, frames, block.data(), scratch, lookup, decoderPos, &job.library);
                pending.insert(pending.end(), block.begin(), block.end());
                audioCursor += static_cast<Tick>(frames) * kTps;
                while (static_cast<int>(pending.size()) >= audioFrameSize * 2) {
                    encodeAudio(pending.data(), audioFrameSize);
                    pending.erase(pending.begin(), pending.begin() + audioFrameSize * 2);
                }
            }
        };

        for (qint64 i = 0; i < totalFrames; ++i) {
            if (cancelled()) break;
            const Tick time = (firstFrame + i) * tpf;

            if (out.video) {
                const QImage frame = compositor.render(seq, job.media, time);
                const AVPixelFormat srcFmt = AV_PIX_FMT_BGRA;
                sws = sws_getCachedContext(sws, frame.width(), frame.height(), srcFmt, width, height,
                                           out.video->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr);
                av_frame_make_writable(yuv);
                const uint8_t* srcData[4] = {frame.constBits(), nullptr, nullptr, nullptr};
                const int srcLines[4] = {static_cast<int>(frame.bytesPerLine()), 0, 0, 0};
                sws_scale(sws, srcData, srcLines, 0, frame.height(), yuv->data, yuv->linesize);
                yuv->pts = i;
                if (avcodec_send_frame(out.video, yuv) >= 0) drain(out, out.video, out.videoStream);
            }

            pumpAudioUntil(time + tpf);

            if ((i % 5) == 0) emit progressed(static_cast<int>(std::min<qint64>(99, (i * 100) / totalFrames)));
        }

        const bool wasCancelled = cancelled();

        if (out.video) {
            avcodec_send_frame(out.video, nullptr);
            drain(out, out.video, out.videoStream);
        }
        if (out.audio) {
            if (!pending.empty()) {
                const int frames = static_cast<int>(pending.size() / 2);
                std::vector<float> last(static_cast<size_t>(audioFrameSize) * 2, 0.0f);
                std::copy(pending.begin(), pending.end(), last.begin());
                // Variable-frame-size encoders (PCM) accept the short tail as is.
                encodeAudio(last.data(), (out.audio->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
                                             ? frames : audioFrameSize);
            }
            avcodec_send_frame(out.audio, nullptr);
            drain(out, out.audio, out.audioStream);
        }

        av_write_trailer(out.fmt);
        if (sws) sws_freeContext(sws);
        if (yuv) av_frame_free(&yuv);

        if (wasCancelled) {
            if (error) *error = tr("Export cancelled");
            return false;
        }
        emit progressed(100);
        return true;
    }

    QMutex m_mutex;
    QQueue<Job> m_queue;
    bool m_cancelled = false;
};

// ----------------------------------------------------------------- Exporter

Exporter::Exporter(QObject* parent)
    : QObject(parent)
{
    m_worker = new ExportWorker;
    connect(m_worker, &ExportWorker::progressed, this, &Exporter::progress);
    connect(m_worker, &ExportWorker::started, this, &Exporter::jobStarted);
    connect(m_worker, &ExportWorker::encoder, this, &Exporter::encoderChosen);
    connect(m_worker, &ExportWorker::finished, this, &Exporter::finished);
    connect(m_worker, &ExportWorker::failed, this, &Exporter::failed);
    connect(m_worker, &ExportWorker::empty, this, &Exporter::queueEmpty);
    // A job queued while the worker was winding down would otherwise be stranded.
    connect(m_worker, &QThread::finished, this, [this] {
        if (m_worker->pending() > 0) m_worker->start();
    });
}

Exporter::~Exporter()
{
    if (m_worker->isRunning()) {
        m_worker->cancelAll();
        m_worker->wait(10000);
    }
    delete m_worker;
}

void Exporter::start(const Sequence& sequence, const QVector<MediaRef>& media, const Settings& settings,
                     const QVector<Sequence>& library)
{
    m_worker->enqueue({sequence, media, settings, library});
}

void Exporter::cancel() { m_worker->cancel(); }
void Exporter::cancelAll() { m_worker->cancelAll(); }
bool Exporter::isRunning() const { return m_worker->isRunning(); }
int Exporter::pendingCount() const { return m_worker->pending(); }

} // namespace bc

#include "Exporter.moc"
