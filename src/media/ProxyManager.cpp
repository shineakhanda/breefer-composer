#include "media/ProxyManager.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
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

} // namespace

class ProxyWorker : public QThread {
    Q_OBJECT

public:
    struct Job {
        MediaRef ref;
        int height;
    };

    void enqueue(const MediaRef& ref, int height)
    {
        QMutexLocker lock(&m_mutex);
        const QString key = ref.id + QLatin1Char('|') + QString::number(height);
        if (m_seen.contains(key)) return;
        m_seen.insert(key);
        m_queue.append(Job{ref, height});
        m_wake.wakeOne();
    }

    int pending() { return m_queue.size(); }

    void stop()
    {
        QMutexLocker lock(&m_mutex);
        m_quit = true;
        m_wake.wakeAll();
        lock.unlock();
        if (isRunning()) wait(10000);
    }

signals:
    void progressed(const QString& mediaId, int percent);
    void finished(const QString& mediaId, const QString& proxyPath);
    void failed(const QString& mediaId, const QString& error);

protected:
    void run() override
    {
        QMutexLocker lock(&m_mutex);
        while (!m_quit) {
            while (!m_quit && m_queue.isEmpty())
                m_wake.wait(&m_mutex);
            if (m_quit) break;

            const Job job = m_queue.takeFirst();
            lock.unlock();

            QString error;
            const QString output = ProxyManager::proxyPathFor(job.ref, job.height);
            if (encode(job.ref, job.height, output, &error))
                emit finished(job.ref.id, output);
            else
                emit failed(job.ref.id, error);

            lock.relock();
        }
    }

private:
    bool encode(const MediaRef& ref, int targetHeight, const QString& outputPath, QString* error)
    {
        AVFormatContext* input = nullptr;
        if (avformat_open_input(&input, ref.path.toUtf8().constData(), nullptr, nullptr) < 0) {
            if (error) *error = tr("Cannot open source");
            return false;
        }
        avformat_find_stream_info(input, nullptr);

        const int videoStream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (videoStream < 0) {
            if (error) *error = tr("No video stream");
            avformat_close_input(&input);
            return false;
        }

        AVStream* inStream = input->streams[videoStream];
        const AVCodec* decoderCodec = avcodec_find_decoder(inStream->codecpar->codec_id);
        AVCodecContext* decoder = avcodec_alloc_context3(decoderCodec);
        avcodec_parameters_to_context(decoder, inStream->codecpar);
        decoder->thread_count = 2;
        if (avcodec_open2(decoder, decoderCodec, nullptr) < 0) {
            if (error) *error = tr("Cannot open decoder");
            avcodec_free_context(&decoder);
            avformat_close_input(&input);
            return false;
        }

        AVRational frameRate = av_guess_frame_rate(input, inStream, nullptr);
        if (frameRate.num <= 0 || frameRate.den <= 0) frameRate = AVRational{25, 1};

        const int srcW = inStream->codecpar->width;
        const int srcH = inStream->codecpar->height;
        const double aspect = static_cast<double>(srcW) / std::max(1, srcH);

        const int height = std::max(120, std::min(targetHeight, srcH));
        const int width = std::max(2, static_cast<int>(std::lround(height * aspect)) & ~1);

        // ---- output container
        AVFormatContext* output = nullptr;
        if (avformat_alloc_output_context2(&output, nullptr, "avi",
                                           outputPath.toUtf8().constData()) < 0 || !output) {
            if (error) *error = tr("Cannot create proxy container");
            avcodec_free_context(&decoder);
            avformat_close_input(&input);
            return false;
        }

        const AVCodec* encoderCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (!encoderCodec) {
            if (error) *error = tr("MJPEG encoder unavailable in this FFmpeg build");
            avformat_free_context(output);
            avcodec_free_context(&decoder);
            avformat_close_input(&input);
            return false;
        }

        AVStream* outStream = avformat_new_stream(output, nullptr);
        AVCodecContext* encoder = avcodec_alloc_context3(encoderCodec);
        encoder->width = width;
        encoder->height = height;
        encoder->pix_fmt = AV_PIX_FMT_YUVJ420P;
        encoder->time_base = AVRational{1, 1000000};
        encoder->framerate = frameRate;
        encoder->gop_size = 1;          // intra-only: seek anywhere, decode cheaply
        encoder->max_b_frames = 0;
        encoder->qmin = 2;
        encoder->qmax = 7;
        encoder->color_range = AVCOL_RANGE_JPEG;

        if (avcodec_open2(encoder, encoderCodec, nullptr) < 0) {
            if (error) *error = tr("Cannot open MJPEG encoder");
            avcodec_free_context(&encoder);
            avformat_free_context(output);
            avcodec_free_context(&decoder);
            avformat_close_input(&input);
            return false;
        }

        avcodec_parameters_from_context(outStream->codecpar, encoder);
        outStream->time_base = encoder->time_base;

        if (!(output->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&output->pb, outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
                if (error) *error = tr("Cannot write proxy file");
                avcodec_free_context(&encoder);
                avformat_free_context(output);
                avcodec_free_context(&decoder);
                avformat_close_input(&input);
                return false;
            }
        }

        if (avformat_write_header(output, nullptr) < 0) {
            if (error) *error = tr("Cannot write proxy header");
            avio_closep(&output->pb);
            avformat_free_context(output);
            avcodec_free_context(&encoder);
            avcodec_free_context(&decoder);
            avformat_close_input(&input);
            return false;
        }

        SwsContext* sws = sws_getContext(srcW, srcH, static_cast<AVPixelFormat>(inStream->codecpar->format),
                                         width, height, AV_PIX_FMT_YUVJ420P,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);

        AVFrame* frame = av_frame_alloc();
        AVFrame* scaled = av_frame_alloc();
        scaled->format = AV_PIX_FMT_YUVJ420P;
        scaled->width = width;
        scaled->height = height;
        av_frame_get_buffer(scaled, 32);

        AVPacket* packet = av_packet_alloc();
        int64_t written = 0;
        const int64_t totalFrames = std::max<int64_t>(1, ref.rate.frameOf(ref.duration));
        int64_t lastPercent = -1;

        auto drainEncoder = [&]() {
            while (avcodec_receive_packet(encoder, packet) == 0) {
                packet->stream_index = outStream->index;
                av_packet_rescale_ts(packet, encoder->time_base, outStream->time_base);
                av_interleaved_write_frame(output, packet);
                av_packet_unref(packet);
            }
        };

        while (av_read_frame(input, packet) >= 0) {
            if (packet->stream_index != videoStream) {
                av_packet_unref(packet);
                continue;
            }

            if (avcodec_send_packet(decoder, packet) >= 0) {
                while (avcodec_receive_frame(decoder, frame) == 0) {
                    if (!sws) {
                        av_frame_unref(frame);
                        continue;
                    }
                    sws_scale(sws, frame->data, frame->linesize, 0, srcH,
                              scaled->data, scaled->linesize);

                    int64_t pts = frame->best_effort_timestamp;
                    if (pts == AV_NOPTS_VALUE) pts = written;
                    scaled->pts = av_rescale_q(pts, inStream->time_base, encoder->time_base);

                    avcodec_send_frame(encoder, scaled);
                    drainEncoder();

                    ++written;
                    const int64_t percent = std::min<int64_t>(99, (written * 100) / totalFrames);
                    if (percent != lastPercent) {
                        lastPercent = percent;
                        emit progressed(ref.id, static_cast<int>(percent));
                    }
                    av_frame_unref(frame);
                }
            }
            av_packet_unref(packet);
        }

        avcodec_send_frame(encoder, nullptr);
        drainEncoder();

        av_write_trailer(output);

        if (sws) sws_freeContext(sws);
        av_frame_free(&frame);
        av_frame_free(&scaled);
        av_packet_free(&packet);
        avcodec_free_context(&encoder);
        if (output->pb) avio_closep(&output->pb);
        avformat_free_context(output);
        avcodec_free_context(&decoder);
        avformat_close_input(&input);

        return written > 0;
    }

    QMutex m_mutex;
    QWaitCondition m_wake;
    QList<Job> m_queue;
    QSet<QString> m_seen;
    bool m_quit = false;
};

ProxyManager::ProxyManager(QObject* parent)
    : QObject(parent)
{
    m_worker = new ProxyWorker;
    m_worker->setObjectName(QStringLiteral("proxy-worker"));

    connect(m_worker, &ProxyWorker::progressed, this, &ProxyManager::progress);
    connect(m_worker, &ProxyWorker::finished, this, &ProxyManager::finished);
    connect(m_worker, &ProxyWorker::failed, this, &ProxyManager::failed);

    m_worker->start();
}

ProxyManager::~ProxyManager()
{
    m_worker->stop();
    delete m_worker;
}

QString ProxyManager::proxyDirectory()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                        + QStringLiteral("/proxies");
    QDir().mkpath(dir);
    return dir;
}

QString ProxyManager::proxyPathFor(const MediaRef& ref, int targetHeight)
{
    return proxyDirectory() + QLatin1Char('/') + ref.id
           + QStringLiteral("_%1.avi").arg(targetHeight);
}

void ProxyManager::enqueue(const MediaRef& ref, int targetHeight)
{
    if (!ref.hasVideo) return;
    m_worker->enqueue(ref, targetHeight);
}

int ProxyManager::pendingCount() const
{
    return m_worker->pending();
}

} // namespace bc

#include "ProxyManager.moc"
