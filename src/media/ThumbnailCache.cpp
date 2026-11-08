#include "media/ThumbnailCache.h"

#include "media/MediaProbe.h"

#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QThreadPool>

namespace bc {

ThumbnailCache& ThumbnailCache::instance()
{
    static ThumbnailCache* cache = new ThumbnailCache;
    return *cache;
}

ThumbnailCache::ThumbnailCache(QObject* parent)
    : QObject(parent)
{
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(1);   // never compete with playback on a 2-core CPU
}

QString ThumbnailCache::key(const QString& path, int w, int h) const
{
    return path + QLatin1Char('|') + QString::number(w) + QLatin1Char('x') + QString::number(h);
}

QImage ThumbnailCache::get(const QString& path, int maxWidth, int maxHeight)
{
    if (path.isEmpty()) return {};
    const QString k = key(path, maxWidth, maxHeight);
    auto it = m_images.find(k);
    if (it != m_images.end()) return it.value();
    if (m_pending.contains(k)) return {};
    m_pending.insert(k);

    QPointer<ThumbnailCache> self(this);
    m_pool->start([self, path, k, maxWidth, maxHeight] {
        QThread::currentThread()->setPriority(QThread::LowPriority);
        const QImage image = extractThumbnail(path, maxWidth, maxHeight);
        if (!self) return;
        QMetaObject::invokeMethod(self.data(), [self, path, k, image] {
            if (!self) return;
            self->m_pending.remove(k);
            // Cache failures too (as a tiny image) so we never retry in a loop.
            self->m_images.insert(k, image.isNull() ? QImage(1, 1, QImage::Format_ARGB32) : image);
            emit self->ready(path);
        }, Qt::QueuedConnection);
    });
    return {};
}

void ThumbnailCache::forget(const QString& path)
{
    for (auto it = m_images.begin(); it != m_images.end();) {
        if (it.key().startsWith(path + QLatin1Char('|'))) it = m_images.erase(it);
        else ++it;
    }
}

} // namespace bc
