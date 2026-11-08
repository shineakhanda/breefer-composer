#include "media/WaveformCache.h"

#include "media/Decoder.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>
#include <QWaitCondition>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace bc {

namespace {
constexpr int kBucketSamples = AudioDecoder::kSampleRate / 20;   // 50 ms
}

class WaveformWorker : public QThread {
    Q_OBJECT

public:
    void enqueue(const MediaRef& ref)
    {
        QMutexLocker lock(&m_mutex);
        if (m_seen.contains(ref.id)) return;
        m_seen.insert(ref.id);
        m_queue.append(ref);
        m_wake.wakeOne();
    }

    void stop()
    {
        QMutexLocker lock(&m_mutex);
        m_quit = true;
        m_wake.wakeAll();
        lock.unlock();
        if (isRunning()) wait(4000);
    }

signals:
    void scanned(const QString& mediaId, const QVector<float>& peaks);

protected:
    void run() override
    {
        QMutexLocker lock(&m_mutex);
        while (!m_quit) {
            while (!m_quit && m_queue.isEmpty())
                m_wake.wait(&m_mutex);
            if (m_quit) break;

            const MediaRef ref = m_queue.takeFirst();
            lock.unlock();

            QVector<float> peaks;
            if (ref.hasAudio) peaks = scan(ref);

            lock.relock();
            emit scanned(ref.id, peaks);
        }
    }

private:
    static QVector<float> scan(const MediaRef& ref)
    {
        QVector<float> peaks;
        AudioDecoder decoder;
        if (!decoder.open(ref.path)) return peaks;

        constexpr int kBlockFrames = 4096;
        std::vector<float> buffer(static_cast<size_t>(kBlockFrames) * AudioDecoder::kChannels);

        // Cap the scan at two hours of source so a pathological file cannot
        // occupy the worker forever.
        const qint64 maxFrames = static_cast<qint64>(AudioDecoder::kSampleRate) * 60 * 60 * 2;

        int bucketCount = 0;
        float bucketMax = 0.0f;
        qint64 total = 0;

        while (total < maxFrames) {
            const int got = decoder.read(buffer.data(), kBlockFrames);
            if (got <= 0) break;

            for (int i = 0; i < got; ++i) {
                const float l = std::abs(buffer[static_cast<size_t>(i) * 2 + 0]);
                const float r = std::abs(buffer[static_cast<size_t>(i) * 2 + 1]);
                bucketMax = std::max(bucketMax, std::max(l, r));
                if (++bucketCount >= kBucketSamples) {
                    peaks.push_back(bucketMax);
                    bucketMax = 0.0f;
                    bucketCount = 0;
                }
            }
            total += got;
        }
        if (bucketCount > 0) peaks.push_back(bucketMax);
        return peaks;
    }

    QMutex m_mutex;
    QWaitCondition m_wake;
    QList<MediaRef> m_queue;
    QSet<QString> m_seen;
    bool m_quit = false;
};

WaveformCache::WaveformCache(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<QVector<float>>("QVector<float>");

    m_worker = new WaveformWorker;
    connect(m_worker, &WaveformWorker::scanned, this,
            [this](const QString& mediaId, const QVector<float>& peaks) {
                m_peaks.insert(mediaId, peaks);
                emit peaksReady(mediaId);
            });
    m_worker->start();
}

WaveformCache::~WaveformCache()
{
    m_worker->stop();
    delete m_worker;
}

void WaveformCache::ensure(const QVector<MediaRef>& media)
{
    for (const auto& ref : media) {
        if (!ref.hasAudio || m_peaks.contains(ref.id)) continue;
        m_worker->enqueue(ref);
    }
}

QVector<float> WaveformCache::peaks(const QString& mediaId) const
{
    return m_peaks.value(mediaId);
}

bool WaveformCache::hasPeaks(const QString& mediaId) const
{
    return m_peaks.contains(mediaId);
}

} // namespace bc

#include "WaveformCache.moc"
