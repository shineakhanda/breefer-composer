#pragma once

#include "core/Project.h"
#include "media/Decoder.h"
#include "media/FrameCache.h"

#include <QHash>
#include <QImage>

class QPainter;

namespace bc {

// Composites a sequence at a point in time. Lives on the render thread and owns
// every decoder it touches, so no media state is ever shared with the UI.
class Compositor {
public:
    Compositor();
    ~Compositor();

    void setPreviewSize(int width, int height);
    int previewWidth() const { return m_width; }
    int previewHeight() const { return m_height; }

    // High quality = smooth scaling of transformed layers (export, paused frames).
    void setHighQuality(bool hq) { m_highQuality = hq; }

    QImage render(const Sequence& seq, const QVector<MediaRef>& media, Tick time);

    // Drops every cached decoder and frame (media replaced, proxy switched, ...).
    void invalidate();

    FrameCache& cache() { return m_cache; }

private:
    VideoDecoder* decoderFor(const MediaRef& ref);
    void trimDecoders();

    // Draws one clip, fully processed (effects, transform, mask, opacity).
    void drawClip(QPainter& painter, const Sequence& seq, const QVector<MediaRef>& media,
                  const Clip& clip, Tick time, double opacityMul);
    QImage renderLayer(const Sequence& seq, const QVector<MediaRef>& media,
                       const Clip& clip, Tick time);
    QImage sourceImage(const Sequence& seq, const QVector<MediaRef>& media,
                       const Clip& clip, Tick time, QSize& drawSize);
    void drawTitle(QPainter& painter, const Sequence& seq, const Clip& clip);

    FrameCache m_cache;
    QHash<QString, VideoDecoder*> m_decoders;   // owned
    QVector<QString> m_decoderOrder;            // LRU, most recent last
    QHash<QString, QImage> m_lastGood;          // per media: last decoded frame (EOF fallback)
    int m_width = 960;
    int m_height = 540;
    bool m_highQuality = false;
    static constexpr int kMaxDecoders = 8;
};

} // namespace bc
