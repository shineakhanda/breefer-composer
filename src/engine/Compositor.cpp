#include "engine/Compositor.h"

#include "core/Evaluate.h"
#include "engine/Effects.h"
#include "engine/Model3D.h"
#include "engine/PluginHost.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace bc {

Compositor::Compositor() = default;

Compositor::~Compositor()
{
    qDeleteAll(m_decoders);
}

void Compositor::setPreviewSize(int width, int height)
{
    m_width = std::max(2, width & ~1);
    m_height = std::max(2, height & ~1);
}

void Compositor::invalidate()
{
    qDeleteAll(m_decoders);
    m_decoders.clear();
    m_decoderOrder.clear();
    m_lastGood.clear();
    m_cache.clear();
}

VideoDecoder* Compositor::decoderFor(const MediaRef& ref)
{
    auto it = m_decoders.find(ref.id);
    if (it != m_decoders.end() && it.value()->hardwareFailed()) {
        delete it.value();
        m_decoders.erase(it);
        m_decoderOrder.removeOne(ref.id);
        it = m_decoders.end();
        m_cache.clear();   // frames decoded around the failure may be bad
    }
    if (it != m_decoders.end()) {
        m_decoderOrder.removeOne(ref.id);
        m_decoderOrder.push_back(ref.id);
        return it.value();
    }

    // A proxy, when present, is what actually gets decoded.
    const QString path = ref.proxyPath.isEmpty() ? ref.path : ref.proxyPath;

    auto* decoder = new VideoDecoder;
    if (!decoder->open(path)) {
        delete decoder;
        return nullptr;
    }
    m_decoders.insert(ref.id, decoder);
    m_decoderOrder.push_back(ref.id);
    trimDecoders();
    return decoder;
}

void Compositor::trimDecoders()
{
    // Keep the open-decoder set small; each holds reference frames and buffers.
    // Least recently used goes first.
    while (m_decoders.size() > kMaxDecoders && !m_decoderOrder.isEmpty()) {
        const QString victim = m_decoderOrder.takeFirst();
        delete m_decoders.take(victim);
    }
}

QImage Compositor::sourceImage(const Sequence& seq, const QVector<MediaRef>& media,
                               const Clip& clip, Tick time, QSize& drawSize)
{
    Q_UNUSED(seq);
    const MediaRef* ref = nullptr;
    for (const auto& m : media) {
        if (m.id == clip.mediaId) {
            ref = &m;
            break;
        }
    }
    if (!ref || !ref->hasVideo || ref->offline) return {};

    const int srcW = ref->width > 0 ? ref->width : m_width;
    const int srcH = ref->height > 0 ? ref->height : m_height;

    // Fit inside the frame (Premiere's "Set to frame size" default).
    const double scale = std::min(static_cast<double>(m_width) / srcW,
                                  static_cast<double>(m_height) / srcH);
    const int drawW = std::max(2, static_cast<int>(std::lround(srcW * scale)) & ~1);
    const int drawH = std::max(2, static_cast<int>(std::lround(srcH * scale)) & ~1);
    drawSize = QSize(drawW, drawH);

    Tick sourceTime = std::max<Tick>(0, clip.sourceTime(time));
    if (!ref->isStillImage() && ref->duration > 0)
        sourceTime = std::min(sourceTime, ref->duration - ref->rate.ticksPerFrame());
    const qint64 frameIndex = ref->isStillImage() ? 0 : ref->rate.frameOf(sourceTime);
    const QString key = frameKey(ref->id, frameIndex, drawW, drawH);

    QImage frame = m_cache.get(key);
    if (frame.isNull()) {
        if (VideoDecoder* decoder = decoderFor(*ref)) {
            frame = decoder->frameAt(std::max<Tick>(0, sourceTime), drawW, drawH);
            if (!frame.isNull()) m_cache.put(key, frame);
        }
    }
    // Decoders can fail on the last frames of a file; hold the last good one.
    const QString lastKey = ref->id + QLatin1Char('@') + QString::number(drawW);
    if (frame.isNull()) return m_lastGood.value(lastKey);
    m_lastGood.insert(lastKey, frame);
    return frame;
}

void Compositor::drawTitle(QPainter& painter, const Sequence& seq, const Clip& clip)
{
    const TitleSpec& t = clip.title;
    const double s = static_cast<double>(m_width) / std::max(1, seq.width);

    QFont font(t.fontFamily);
    font.setPixelSize(std::max(4, static_cast<int>(std::lround(t.fontSize * s))));
    font.setBold(t.bold);
    font.setItalic(t.italic);
    const QFontMetricsF metrics(font);

    const QStringList lines = t.text.split(QLatin1Char('\n'));
    double blockW = 0;
    for (const QString& line : lines) blockW = std::max(blockW, metrics.horizontalAdvance(line));
    const double lineH = metrics.lineSpacing();
    const double blockH = lineH * lines.size();

    const double ax = t.posX * m_width;
    const double ay = t.posY * m_height;
    double left = ax - blockW / 2.0;
    if (t.align == 0) left = ax;
    if (t.align == 2) left = ax - blockW;
    const double top = ay - blockH / 2.0;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (t.background) {
        const double pad = lineH * 0.25;
        painter.setPen(Qt::NoPen);
        painter.setBrush(t.backgroundColor);
        painter.drawRoundedRect(QRectF(left - pad * 1.6, top - pad, blockW + pad * 3.2, blockH + pad * 2),
                                pad * 0.8, pad * 0.8);
    }

    QPainterPath path;
    for (int i = 0; i < lines.size(); ++i) {
        const double w = metrics.horizontalAdvance(lines[i]);
        double x = left;
        if (t.align == 1) x = left + (blockW - w) / 2.0;
        if (t.align == 2) x = left + (blockW - w);
        path.addText(QPointF(x, top + metrics.ascent() + lineH * i), font, lines[i]);
    }

    if (t.shadow) {
        painter.save();
        painter.translate(std::max(1.0, 3.0 * s), std::max(1.0, 3.0 * s));
        painter.fillPath(path, QColor(0, 0, 0, 150));
        painter.restore();
    }
    if (t.outline > 0.0) {
        QPen pen(t.outlineColor, t.outline * s * 2.0);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.strokePath(path, pen);
    }
    painter.fillPath(path, t.color);
    painter.restore();
}

void Compositor::drawClip(QPainter& painter, const Sequence& seq, const QVector<MediaRef>& media,
                          const Clip& clip, Tick time, double opacityMul)
{
    const Tick local = time - clip.start;
    const double s = static_cast<double>(m_width) / std::max(1, seq.width);
    fx::Transform transform = fx::transformFor(clip, local);
    transform.x *= s;
    transform.y *= s;
    const double opacity = std::clamp(
        fx::paramValue(clip, fx::kOpacity, "amount", local, 100.0) / 100.0 * opacityMul, 0.0, 1.0);
    if (opacity <= 0.0) return;

    // Build the clip's own image at canvas size (generators) or fitted size (media).
    QImage frame;
    QSize drawSize;
    if (clip.generator == GeneratorKind::ColorMatte) {
        frame = QImage(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
        frame.fill(clip.matteColor);
        drawSize = frame.size();
    } else if (clip.generator == GeneratorKind::Title) {
        frame = QImage(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
        frame.fill(Qt::transparent);
        QPainter tp(&frame);
        drawTitle(tp, seq, clip);
        drawSize = frame.size();
    } else if (clip.generator == GeneratorKind::Nest) {
        // Nested sequence: render it (transparent background) at our canvas size.
        const Sequence* nested = nullptr;
        for (const auto& candidate : m_library)
            if (candidate.id == clip.nestedSequenceId) nested = &candidate;
        if (!nested || m_nestDepth >= 4) return;
        ++m_nestDepth;
        frame = renderCanvas(*nested, media, std::max<Tick>(0, clip.sourceTime(time)), false);
        --m_nestDepth;
        drawSize = frame.size();
    } else if (clip.generator == GeneratorKind::Model3D) {
        const auto mesh = Mesh::load(clip.modelPath);
        RenderParams3D p;
        auto v = [&](const char* id, double fallback) { return fx::paramValue(clip, fx::kModel3D, id, local, fallback); };
        p.rotateX = v("rotateX", p.rotateX);
        p.rotateY = v("rotateY", p.rotateY);
        p.rotateZ = v("rotateZ", p.rotateZ);
        p.scale = v("scale", p.scale);
        p.distance = v("distance", p.distance);
        p.fov = v("fov", p.fov);
        p.lightAzimuth = v("lightAzimuth", p.lightAzimuth);
        p.lightElevation = v("lightElevation", p.lightElevation);
        p.ambient = v("ambient", p.ambient);
        p.color = QColor(static_cast<int>(v("red", 200)), static_cast<int>(v("green", 200)), static_cast<int>(v("blue", 210)));
        p.wireframe = v("wireframe", 0.0) >= 0.5;
        frame = renderMesh(*mesh, p, m_width, m_height);
        drawSize = frame.size();
    } else if (clip.generator == GeneratorKind::Adjustment) {
        return;   // handled in renderCanvas (needs the layers beneath)
    } else {
        frame = sourceImage(seq, media, clip, time, drawSize);
        if (frame.isNull()) return;
        // QImage copy-on-write detaches on the first pixel write; untouched frames stay shared.
    }

    applyPixelEffects(frame, clip, local, s);

    const QPointF topLeft(-drawSize.width() * 0.5, -drawSize.height() * 0.5);
    const bool smooth = m_highQuality || std::abs(transform.scale - 1.0) > 1e-3
                        || std::abs(transform.rotation) > 1e-3;

    if (!fx::hasMask(clip)) {
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        painter.setOpacity(opacity);
        painter.translate(m_width * 0.5 + transform.x, m_height * 0.5 + transform.y);
        // Basic 3D: true perspective swivel/tilt around the layer centre.
        const double swivel = fx::paramValue(clip, fx::kBasic3D, "swivel", local, 0.0);
        const double tilt = fx::paramValue(clip, fx::kBasic3D, "tilt", local, 0.0);
        const double distance = fx::paramValue(clip, fx::kBasic3D, "distance", local, 0.0);
        if (std::abs(swivel) > 1e-3 || std::abs(tilt) > 1e-3 || std::abs(distance) > 1e-3) {
            const double plane = m_width * 1.6;
            QTransform t3;
            t3.rotate(tilt, Qt::XAxis, plane);
            t3.rotate(swivel, Qt::YAxis, plane);
            const double depthScale = plane / std::max(1.0, plane + distance / 100.0 * plane);
            t3.scale(depthScale, depthScale);
            painter.setTransform(t3, true);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        }
        painter.rotate(transform.rotation);
        painter.scale(transform.scale, transform.scale);
        painter.drawImage(topLeft, frame);
        painter.restore();
        return;
    }

    // Shape mask: composite the transformed frame through a feathered matte.
    QImage layer(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    {
        QPainter lp(&layer);
        lp.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
        lp.translate(m_width * 0.5 + transform.x, m_height * 0.5 + transform.y);
        lp.rotate(transform.rotation);
        lp.scale(transform.scale, transform.scale);
        lp.drawImage(topLeft, frame);
    }

    const double cx = m_width * 0.5 + fx::paramValue(clip, fx::kMask, "position.x", local, 0.0) * s;
    const double cy = m_height * 0.5 + fx::paramValue(clip, fx::kMask, "position.y", local, 0.0) * s;
    const double maskW = m_width * fx::paramValue(clip, fx::kMask, "width", local, 55.0) / 100.0;
    const double maskH = m_height * fx::paramValue(clip, fx::kMask, "height", local, 55.0) / 100.0;
    const double featherPx = fx::paramValue(clip, fx::kMask, "feather", local, 20.0) * s;
    const double roundness = fx::paramValue(clip, fx::kMask, "roundness", local, 100.0);
    const double maskOpacity = fx::paramValue(clip, fx::kMask, "opacity", local, 100.0) / 100.0;

    QImage matte(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    matte.fill(Qt::transparent);
    {
        QPainter mp(&matte);
        mp.setRenderHint(QPainter::Antialiasing, true);
        mp.setPen(Qt::NoPen);
        const int steps = std::clamp(static_cast<int>(featherPx / 2.0), 1, 40);
        const int alpha = std::max(8, 255 / (steps + 1));
        const double radius = std::min(maskW, maskH) * 0.5 * (roundness / 100.0);
        for (int st = steps; st >= 0; --st) {
            const double grow = st * (featherPx / steps) / 2.0;
            mp.setBrush(QColor(255, 255, 255, alpha));
            const QRectF r(cx - maskW / 2 - grow, cy - maskH / 2 - grow, maskW + grow * 2, maskH + grow * 2);
            if (roundness >= 99.0) mp.drawEllipse(r);
            else mp.drawRoundedRect(r, radius, radius);
        }
        if (maskOpacity < 1.0) {
            mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            mp.fillRect(matte.rect(), QColor(0, 0, 0, static_cast<int>(maskOpacity * 255)));
        }
    }
    {
        QPainter mp(&layer);
        mp.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        mp.drawImage(0, 0, matte);
    }

    painter.save();
    painter.setOpacity(opacity);
    painter.drawImage(0, 0, layer);
    painter.restore();
}

QImage Compositor::renderLayer(const Sequence& seq, const QVector<MediaRef>& media,
                               const Clip& clip, Tick time)
{
    QImage layer(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    layer.fill(Qt::transparent);
    QPainter p(&layer);
    drawClip(p, seq, media, clip, time, 1.0);
    p.end();
    return layer;
}

void Compositor::applyPixelEffects(QImage& frame, const Clip& clip, Tick local, double s)
{
    fx::applyColor(frame, clip, local);

    const double blurRadius = fx::paramValue(clip, fx::kBlur, "radius", local, 0.0) * s * 1.8;
    if (blurRadius > 0.5) fx::applyBlur(frame, blurRadius);

    const double sharpen = fx::paramValue(clip, fx::kSharpen, "amount", local, 0.0);
    if (sharpen > 0.0) fx::applySharpen(frame, sharpen);

    const double vignette = fx::paramValue(clip, fx::kVignette, "amount", local, 0.0);
    if (vignette > 0.0)
        fx::applyVignette(frame, vignette, fx::paramValue(clip, fx::kVignette, "size", local, 60.0));

    // Native plugin effects, in stack order.
    for (const auto& e : clip.effects)
        if (e.enabled && e.type.startsWith(QLatin1String("plugin:"))) PluginHost::instance().render(e, frame, local);

    fx::applyCropFlip(frame, clip, local);
}

QImage Compositor::render(const Sequence& seq, const QVector<MediaRef>& media, Tick time)
{
    return renderCanvas(seq, media, time, true);
}

QImage Compositor::renderCanvas(const Sequence& seq, const QVector<MediaRef>& media, Tick time, bool opaque)
{
    QImage canvas(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(opaque ? Qt::black : Qt::transparent);

    QPainter painter(&canvas);

    // Tracks are stored top-first (V3, V2, V1, ...); composite bottom-up.
    for (int ti = seq.tracks.size() - 1; ti >= 0; --ti) {
        const Track& track = seq.tracks[ti];
        if (track.kind != TrackKind::Video || !trackActive(seq, track)) continue;

        const TrackEval ev = evaluateTrack(track, time);
        if (ev.empty()) continue;

        if (!ev.isTransition()) {
            const Clip& clip = *ev.clip;
            if (!clip.enabled) continue;
            if (clip.generator == GeneratorKind::Adjustment) {
                // Adjustment layer: re-process everything composited so far,
                // blended over the original by the layer's opacity.
                const Tick local = time - clip.start;
                QImage processed = canvas.copy();
                applyPixelEffects(processed, clip, local, static_cast<double>(m_width) / std::max(1, seq.width));
                const double opacity = std::clamp(
                    fx::paramValue(clip, fx::kOpacity, "amount", local, 100.0) / 100.0 * ev.fade, 0.0, 1.0);
                painter.save();
                painter.setOpacity(opacity);
                painter.drawImage(0, 0, processed);
                painter.restore();
                continue;
            }
            const bool dipWhite = ev.fadeType == TransitionType::DipToWhite && ev.fade < 1.0;
            drawClip(painter, seq, media, clip, time, dipWhite ? 1.0 : ev.fade);
            if (dipWhite) {
                painter.save();
                painter.setOpacity(1.0 - ev.fade);
                painter.fillRect(canvas.rect(), Qt::white);
                painter.restore();
            }
            continue;
        }

        // Two-sided transition.
        const double p = std::clamp(ev.progress, 0.0, 1.0);
        const QImage a = renderLayer(seq, media, *ev.outgoing, time);
        const QImage b = renderLayer(seq, media, *ev.incoming, time);
        const QRectF full(0, 0, m_width, m_height);

        painter.save();
        switch (ev.type) {
        case TransitionType::DipToBlack:
        case TransitionType::DipToWhite: {
            const QColor dip = ev.type == TransitionType::DipToBlack ? Qt::black : Qt::white;
            const QImage& side = p < 0.5 ? a : b;
            const double amount = p < 0.5 ? p * 2.0 : (1.0 - p) * 2.0;
            painter.drawImage(0, 0, side);
            painter.setOpacity(amount);
            painter.fillRect(full, dip);
            break;
        }
        case TransitionType::WipeLeft:
        case TransitionType::WipeRight: {
            painter.drawImage(0, 0, a);
            const double w = m_width * p;
            const QRectF r = ev.type == TransitionType::WipeRight ? QRectF(0, 0, w, m_height)
                                                                  : QRectF(m_width - w, 0, w, m_height);
            painter.setClipRect(r);
            painter.drawImage(0, 0, b);
            break;
        }
        case TransitionType::PushLeft: {
            const double offset = m_width * p;
            painter.drawImage(QPointF(-offset, 0), a);
            painter.drawImage(QPointF(m_width - offset, 0), b);
            break;
        }
        case TransitionType::Iris: {
            painter.drawImage(0, 0, a);
            const double radius = std::hypot(m_width, m_height) * 0.5 * p;
            QPainterPath circle;
            circle.addEllipse(QPointF(m_width * 0.5, m_height * 0.5), radius, radius);
            painter.setRenderHint(QPainter::Antialiasing, true);
            painter.setClipPath(circle);
            painter.drawImage(0, 0, b);
            break;
        }
        case TransitionType::CrossDissolve:
        default: {
            // Blend the two layers first so a dissolve over lower tracks stays correct.
            QImage mix(m_width, m_height, QImage::Format_ARGB32_Premultiplied);
            mix.fill(Qt::transparent);
            QPainter mp(&mix);
            mp.setOpacity(1.0 - p);
            mp.drawImage(0, 0, a);
            mp.setCompositionMode(QPainter::CompositionMode_Plus);
            mp.setOpacity(p);
            mp.drawImage(0, 0, b);
            mp.end();
            painter.drawImage(0, 0, mix);
            break;
        }
        }
        painter.restore();
    }

    painter.end();
    return canvas;
}

} // namespace bc
