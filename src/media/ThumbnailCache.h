#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>

class QThreadPool;

namespace bc {

// Decodes poster frames on one low-priority background thread. The UI asks
// for a thumbnail, gets a null image the first time, and repaints when
// `ready` fires. Never blocks painting.
class ThumbnailCache : public QObject {
    Q_OBJECT

public:
    static ThumbnailCache& instance();

    // Returns the cached thumbnail or a null image (and starts decoding it).
    QImage get(const QString& path, int maxWidth, int maxHeight);
    void forget(const QString& path);

signals:
    void ready(const QString& path);

private:
    explicit ThumbnailCache(QObject* parent = nullptr);
    QString key(const QString& path, int w, int h) const;

    QThreadPool* m_pool = nullptr;
    QHash<QString, QImage> m_images;
    QSet<QString> m_pending;
};

} // namespace bc
