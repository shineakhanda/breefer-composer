#include <exception>
#include <new>
#include "engine/PlaybackEngine.h"

#include <QTimer>
#include <algorithm>

namespace bc {

// ----------------------------------------------------------------- RenderWorker

RenderWorker::RenderWorker(QObject* parent)
    : QThread(parent)
{
}

RenderWorker::~RenderWorker()
{
    stop();
}

void RenderWorker::setSource(const Sequence& seq, const QVector<MediaRef>& media, const QVector<Sequence>& library)
{
    QMutexLocker lock(&m_mutex);
    m_sequence = seq;
    m_media = media;
    m_library = library;
}

void RenderWorker::setPreviewHeight(int height)
{
    QMutexLocker lock(&m_mutex);
    m_previewHeight = height;
}

void RenderWorker::request(Tick time, bool highQuality)
{
    QMutexLocker lock(&m_mutex);
    m_pending = time;
    m_pendingHq = highQuality;
    m_wake.wakeOne();
}

void RenderWorker::invalidate()
{
    QMutexLocker lock(&m_mutex);
    m_invalidate = true;
    m_wake.wakeOne();
}

void RenderWorker::setCacheBudgetKB(int kb)
{
    QMutexLocker lock(&m_mutex);
    m_cacheBudgetKB = kb;
    m_wake.wakeOne();
}

void RenderWorker::trimCaches()
{
    QMutexLocker lock(&m_mutex);
    m_trim = true;
    m_wake.wakeOne();
}

void RenderWorker::stop()
{
    QMutexLocker lock(&m_mutex);
    m_quit = true;
    m_wake.wakeAll();
    lock.unlock();
    if (isRunning()) wait(2000);
}

void RenderWorker::run()
{
    QMutexLocker lock(&m_mutex);
    m_quit = false;

    while (!m_quit) {
        while (!m_quit && !m_pending && !m_invalidate && !m_trim && m_cacheBudgetKB == 0)
            m_wake.wait(&m_mutex);
        if (m_quit) break;

        if (m_cacheBudgetKB > 0 || m_trim) {
            const int budget = m_cacheBudgetKB;
            const bool trim = m_trim;
            m_cacheBudgetKB = 0;
            m_trim = false;
            lock.unlock();
            if (budget > 0) m_compositor.cache().setBudgetKB(budget);
            if (trim) m_compositor.cache().clear();
            lock.relock();
            if (!m_pending && !m_invalidate) continue;
        }

        if (m_invalidate) {
            m_invalidate = false;
            lock.unlock();
            m_compositor.invalidate();
            lock.relock();
            if (!m_pending) continue;
        }

        const Tick time = *m_pending;
        const bool hq = m_pendingHq;
        m_pending.reset();

        const Sequence seq = m_sequence;
        const QVector<MediaRef> media = m_media;
        const QVector<Sequence> library = m_library;
        const int height = std::max(90, m_previewHeight);

        lock.unlock();

        const double aspect = (seq.height > 0) ? static_cast<double>(seq.width) / seq.height : 16.0 / 9.0;
        const int width = std::max(2, static_cast<int>(std::lround(height * aspect)) & ~1);

        m_compositor.setPreviewSize(width, height);
        m_compositor.setHighQuality(hq);
        m_compositor.setLibrary(library);
        QImage image;
        try {
            image = m_compositor.render(seq, media, time);
        } catch (const std::exception&) {
            // A bad frame (corrupt media, out of memory, plugin fault) must not take the editor down:
            // show a black frame and keep going. Drop the caches in case they were the cause.
            m_compositor.invalidate();
            image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
        } catch (...) {
            image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
        }
        if (image.isNull()) {
            image = QImage(width, height, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::black);
        }
        ++m_rendered;

        emit frameReady(image, time);

        lock.relock();
    }
}

// ---------------------------------------------------------------- PlaybackEngine

PlaybackEngine::PlaybackEngine(QObject* parent)
    : QObject(parent)
{
    m_worker = new RenderWorker(this);
    connect(m_worker, &RenderWorker::frameReady, this,
            [this](const QImage& image, Tick time) { emit frameReady(image, time); },
            Qt::QueuedConnection);

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &PlaybackEngine::onTick);

    m_worker->start();
}

PlaybackEngine::~PlaybackEngine()
{
    m_timer->stop();
    m_worker->stop();
}

void PlaybackEngine::setSource(const Sequence& seq, const QVector<MediaRef>& media, const QVector<Sequence>& library)
{
    m_sequence = seq;
    m_media = media;
    m_library = library;
    m_duration = seq.duration;
    m_rate = seq.rate;

    const double fps = std::clamp(m_rate.value(), 1.0, 120.0);
    m_timer->setInterval(std::clamp(static_cast<int>(1000.0 / fps), 8, 50));

    m_worker->setSource(seq, media, library);
    requestAt(m_position);
}

void PlaybackEngine::setPreviewHeight(int height)
{
    height = std::clamp(height, 90, 2160);
    m_ceiling = height;   // adaptive quality never recovers above what the user chose
    if (height == m_previewHeight) return;
    m_previewHeight = height;
    m_worker->setPreviewHeight(height);
    emit qualityChanged(height);
    requestAt(m_position);
}

void PlaybackEngine::invalidate()
{
    m_worker->setSource(m_sequence, m_media, m_library);
    m_worker->invalidate();
    requestAt(m_position);
}

void PlaybackEngine::play(Tick from)
{
    if (m_playing) return;
    m_anchor = from;
    m_position = from;
    m_clock.start();
    m_lastRendered = m_worker->renderedCount();
    m_consecutiveDrops = 0;
    m_stableClock.start();
    m_playing = true;
    m_timer->start();
    emit playbackStateChanged(true);
    requestAt(from);
}

void PlaybackEngine::pause()
{
    if (!m_playing) return;
    m_playing = false;
    m_timer->stop();
    m_speed = 1.0;
    emit playbackStateChanged(false);
    requestAt(m_position);
}

void PlaybackEngine::seek(Tick time)
{
    m_position = std::max<Tick>(0, time);
    m_anchor = m_position;
    if (m_playing) m_clock.restart();
    requestAt(m_position);
}

void PlaybackEngine::requestAt(Tick time)
{
    m_worker->request(time, !m_playing && m_hqPause);
}

void PlaybackEngine::setSpeed(double speed)
{
    if (speed == m_speed) return;
    // Re-anchor so the position stays continuous across speed changes.
    if (m_playing) {
        m_anchor = m_position;
        m_clock.restart();
    }
    m_speed = speed;
}

void PlaybackEngine::setLoop(bool enabled, Tick in, Tick out)
{
    m_loop = enabled && out > in;
    m_loopIn = in;
    m_loopOut = out;
}

void PlaybackEngine::onTick()
{
    if (!m_playing) return;

    Tick target;
    std::optional<Tick> external;
    if (m_speed == 1.0 && m_externalClock) external = m_externalClock();
    if (external) {
        target = *external;
    } else {
        const double elapsed = static_cast<double>(m_clock.nsecsElapsed()) / 1e9;
        target = m_anchor + secToTicks(elapsed * m_speed);
    }

    const Tick end = m_loop ? m_loopOut : m_duration;
    if (m_speed > 0 && end > 0 && target >= end) {
        if (m_loop) {
            m_anchor = m_loopIn;
            m_position = m_loopIn;
            m_clock.restart();
            emit positionChanged(m_position);
            emit looped(m_loopIn);
            requestAt(m_position);
            return;
        }
        m_position = m_duration;
        emit positionChanged(m_position);
        requestAt(m_duration);
        pause();
        return;
    }
    if (m_speed < 0 && target <= 0) {
        m_position = 0;
        emit positionChanged(0);
        requestAt(0);
        pause();
        return;
    }

    m_position = target;
    emit positionChanged(target);
    requestAt(target);

    const int rendered = m_worker->renderedCount();
    const bool dropped = (rendered == m_lastRendered);
    m_lastRendered = rendered;
    adaptQuality(dropped);
}

void PlaybackEngine::adaptQuality(bool dropped)
{
    if (dropped) {
        m_stableClock.restart();
        if (++m_consecutiveDrops > 12) {
            m_consecutiveDrops = 0;
            if (m_previewHeight > 180) {
                const int next = m_previewHeight > 540 ? 540 : (m_previewHeight > 360 ? 360 : (m_previewHeight > 270 ? 270 : 180));
                m_previewHeight = next;
                m_worker->setPreviewHeight(next);
                emit qualityChanged(next);
            }
        }
        ++m_totalDropped;
        if (m_totalDropped % 10 == 0)
            emit droppedFramesChanged(m_totalDropped);
    } else {
        m_consecutiveDrops = 0;
        if (m_previewHeight < m_ceiling && m_stableClock.elapsed() > 8000) {
            m_previewHeight = std::min(m_ceiling, m_previewHeight + 90);
            m_worker->setPreviewHeight(m_previewHeight);
            emit qualityChanged(m_previewHeight);
            m_stableClock.restart();
        }
    }
}

} // namespace bc
