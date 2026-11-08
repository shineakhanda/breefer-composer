#pragma once

#include "core/Project.h"

#include <QObject>
#include <QString>
#include <QVector>

namespace bc {

class ExportWorker;

// Renders sequences to media files on a worker thread. Jobs queue up and run
// one after another (batch export).
class Exporter : public QObject {
    Q_OBJECT

public:
    struct Settings {
        QString outputPath;
        QString container = QStringLiteral("mp4");   // mp4, mov, mkv, webm, wav
        QString videoCodec = QStringLiteral("h264");  // h264, hevc, prores, dnxhr, vp9, av1, none
        QString audioCodec = QStringLiteral("aac");   // aac, pcm, opus
        int width = 1920;
        int height = 1080;
        int bitrateKbps = 12000;
        int audioBitrateKbps = 192;
        bool includeAudio = true;
        bool preferHardware = true;
        bool highQualityScaling = true;
        Tick rangeStart = 0;
        Tick rangeEnd = -1;            // -1 = sequence end
        QString label;                  // shown in the queue
    };

    struct Preset {
        QString name;
        QString category;
        Settings settings;
    };

    // Built-in presets (YouTube, vertical social, masters, audio).
    static QVector<Preset> presets(int seqWidth, int seqHeight);

    // Probes which encoders this FFmpeg build can actually open.
    static QStringList availableVideoCodecs();

    static QString extensionFor(const Settings& s);

    explicit Exporter(QObject* parent = nullptr);
    ~Exporter() override;

    // Queues a job; it starts immediately when the queue is idle.
    void start(const Sequence& sequence, const QVector<MediaRef>& media, const Settings& settings);
    void cancel();        // cancels the running job
    void cancelAll();     // and drops queued ones
    bool isRunning() const;
    int pendingCount() const;

signals:
    void progress(int percent);
    void jobStarted(const QString& label, int remaining);
    void encoderChosen(const QString& description);
    void finished(const QString& path);
    void failed(const QString& error);
    void queueEmpty();

private:
    ExportWorker* m_worker = nullptr;
};

} // namespace bc
