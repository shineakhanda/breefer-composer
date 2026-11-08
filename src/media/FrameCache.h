#pragma once

#include <QCache>
#include <QImage>
#include <QString>

namespace bc {

inline QString frameKey(const QString& mediaId, qint64 frame, int w, int h)
{
    return mediaId + QLatin1Char('|') + QString::number(frame)
           + QLatin1Char('|') + QString::number(w) + QLatin1Char('x') + QString::number(h);
}

// Bounded LRU of decoded frames. Cost is the image's byte size in KB.
// Owned by the render thread: no locking, no sharing with the UI thread.
class FrameCache {
public:
    explicit FrameCache(int budgetKB = 512 * 1024)
        : m_cache(budgetKB)
    {
    }

    bool contains(const QString& key) { return m_cache.contains(key); }

    QImage get(const QString& key)
    {
        QImage* img = m_cache.object(key);
        if (!img) {
            ++m_misses;
            return {};
        }
        ++m_hits;
        // QCache does not refresh recency on read; re-inserting makes it LRU-correct.
        QImage copy = *img;
        m_cache.insert(key, new QImage(copy), costKB(copy));
        return copy;
    }

    void put(const QString& key, const QImage& image)
    {
        if (image.isNull()) return;
        m_cache.insert(key, new QImage(image), costKB(image));
    }

    void clear() { m_cache.clear(); }
    void setBudgetKB(int kb) { m_cache.setMaxCost(kb); }
    int budgetKB() const { return m_cache.maxCost(); }
    int usedKB() const { return m_cache.totalCost(); }
    int hits() const { return m_hits; }
    int misses() const { return m_misses; }

private:
    static int costKB(const QImage& image)
    {
        return static_cast<int>(image.sizeInBytes() / 1024) + 1;
    }

    QCache<QString, QImage> m_cache;
    int m_hits = 0;
    int m_misses = 0;
};

} // namespace bc
