#pragma once

#include "core/Project.h"
#include "engine/Compositor.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QThread>
#include <QWaitCondition>
#include <atomic>
#include <functional>
#include <optional>

class QTimer;

namespace bc {

// Owns the compositor and every decoder. Renders the newest requested time;
// stale requests are coalesced rather than queued, which is how frames drop.
class RenderWorker : public QThread {
    Q_OBJECT

public:
    explicit RenderWorker(QObject* parent = nullptr);
    ~RenderWorker() override;

    void setSource(const Sequence& seq, const QVector<MediaRef>& media, const QVector<Sequence>& library = {});
    void setPreviewHeight(int height);
    void request(Tick time, bool highQuality = false);
    void invalidate();   // drop decoders + cache on the render thread
    void setCacheBudgetKB(int kb);
    void trimCaches();   // free memory now (low-RAM situations)
    void stop();

    int renderedCount() const { return m_rendered.load(); }

signals:
    void frameReady(const QImage& image, bc::Tick time);

protected:
    void run() override;

private:
    QMutex m_mutex;
    QWaitCondition m_wake;
    Sequence m_sequence;
    QVector<MediaRef> m_media;
    QVector<Sequence> m_library;
    std::optional<Tick> m_pending;
    bool m_pendingHq = false;
    bool m_invalidate = false;
    bool m_trim = false;
    int m_cacheBudgetKB = 0;   // 0 = leave the default
    int m_previewHeight = 540;
    bool m_quit = false;
    std::atomic<int> m_rendered{0};
    Compositor m_compositor;
};

class PlaybackEngine : public QObject {
    Q_OBJECT

public:
    explicit PlaybackEngine(QObject* parent = nullptr);
    ~PlaybackEngine() override;

    void setSource(const Sequence& seq, const QVector<MediaRef>& media, const QVector<Sequence>& library = {});
    void setPreviewHeight(int height);
    int previewHeight() const { return m_previewHeight; }

    void play(Tick from);
    void pause();
    void seek(Tick time);
    void invalidate();
    void setCacheBudgetKB(int kb) { m_worker->setCacheBudgetKB(kb); }
    void trimCaches() { m_worker->trimCaches(); }

    // Shuttle speed (J/K/L): negative plays backwards. 1.0 = normal.
    void setSpeed(double speed);
    double speed() const { return m_speed; }

    // Loop between in/out (out <= in disables).
    void setLoop(bool enabled, Tick in, Tick out);

    // When set and returning a value, playback at 1x follows this clock
    // (the audio device), so audio is the master clock.
    void setExternalClock(std::function<std::optional<Tick>()> clock) { m_externalClock = std::move(clock); }

    // Renders the paused frame at full quality (smooth scaling).
    void setHighQualityPause(bool on) { m_hqPause = on; }

    bool isPlaying() const { return m_playing; }
    FrameRate rate() const { return m_rate; }
    Tick position() const { return m_position; }

signals:
    void frameReady(const QImage& image, bc::Tick time);
    void positionChanged(bc::Tick time);
    void playbackStateChanged(bool playing);
    void qualityChanged(int previewHeight);
    void droppedFramesChanged(int total);
    void looped(bc::Tick to);

private slots:
    void onTick();

private:
    void requestAt(Tick time);
    void adaptQuality(bool dropped);

    RenderWorker* m_worker = nullptr;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
    Tick m_anchor = 0;
    Tick m_position = 0;
    bool m_playing = false;
    int m_previewHeight = 540;
    int m_ceiling = 540;
    FrameRate m_rate{25, 1};
    Tick m_duration = 0;
    Sequence m_sequence;
    QVector<MediaRef> m_media;
    QVector<Sequence> m_library;
    double m_speed = 1.0;
    bool m_loop = false;
    Tick m_loopIn = 0;
    Tick m_loopOut = 0;
    bool m_hqPause = true;
    std::function<std::optional<Tick>()> m_externalClock;

    int m_lastRendered = 0;
    int m_consecutiveDrops = 0;
    int m_totalDropped = 0;
    QElapsedTimer m_stableClock;
};

} // namespace bc
