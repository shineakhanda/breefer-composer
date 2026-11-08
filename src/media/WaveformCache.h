#pragma once

#include "core/Project.h"

#include <QHash>
#include <QObject>
#include <QVector>

namespace bc {

class WaveformWorker;

// Decodes each media file's audio once, off the UI thread, into 50 ms peak
// buckets so the timeline can draw real waveforms instead of a placeholder.
class WaveformCache : public QObject {
    Q_OBJECT

public:
    explicit WaveformCache(QObject* parent = nullptr);
    ~WaveformCache() override;

    void ensure(const QVector<MediaRef>& media);
    QVector<float> peaks(const QString& mediaId) const;
    bool hasPeaks(const QString& mediaId) const;

signals:
    void peaksReady(const QString& mediaId);

private:
    WaveformWorker* m_worker = nullptr;
    QHash<QString, QVector<float>> m_peaks;
};

} // namespace bc
