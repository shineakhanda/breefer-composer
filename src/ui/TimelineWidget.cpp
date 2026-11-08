#include "ui/TimelineWidget.h"

#include "core/Document.h"
#include "core/Evaluate.h"
#include "media/MediaProbe.h"
#include "media/ThumbnailCache.h"
#include "ui/Icons.h"
#include "ui/Widgets.h"
#include "media/WaveformCache.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QToolButton>
#include <QFontMetrics>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLinearGradient>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace bc {
namespace {

constexpr int kEdgeZone = 7;
constexpr double kGainTopDb = 15.0;
constexpr double kGainBottomDb = -30.0;

QString mediaMime() { return QStringLiteral("application/x-breefer-media"); }

QColor mix(const QColor& a, const QColor& b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t,
                            a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t,
                            a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

void drawLock(QPainter& p, const QRect& r, bool on)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = on ? QColor(0xe0, 0xe0, 0xe6) : QColor(0x70, 0x70, 0x78);
    p.setPen(QPen(c, 1.4));
    p.setBrush(Qt::NoBrush);
    const QRectF body(r.center().x() - 5, r.center().y() - 1, 10, 7);
    p.drawArc(QRectF(body.left() + 2, body.top() - 6, 6, 9), 0, 180 * 16);
    p.setBrush(on ? c : Qt::NoBrush);
    p.drawRoundedRect(body, 1.5, 1.5);
    p.restore();
}

void drawSyncLock(QPainter& p, const QRect& r, bool on)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor c = on ? QColor(0xe0, 0xe0, 0xe6) : QColor(0x70, 0x70, 0x78);
    p.setPen(QPen(c, 1.4));
    p.setBrush(Qt::NoBrush);
    const QPointF ctr = r.center();
    p.drawRoundedRect(QRectF(ctr.x() - 7, ctr.y() - 3, 8, 6), 3, 3);
    p.drawRoundedRect(QRectF(ctr.x() - 1, ctr.y() - 3, 8, 6), 3, 3);
    if (!on) {
        p.setPen(QPen(QColor(0xc0, 0x54, 0x4e), 1.4));
        p.drawLine(QPointF(ctr.x() - 6, ctr.y() + 5), QPointF(ctr.x() + 6, ctr.y() - 5));
    }
    p.restore();
}

} // namespace

TimelineWidget::TimelineWidget(Document* document, QWidget* parent)
    : QAbstractScrollArea(parent)
    , m_document(document)
{
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);

    horizontalScrollBar()->setSingleStep(24);
    verticalScrollBar()->setSingleStep(24);

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { viewport()->update(); });

    connect(m_document, &Document::structureChanged, this, [this] {
        updateScrollbars();
        viewport()->update();
    });
    connect(m_document, &Document::contentsChanged, this, [this] { viewport()->update(); });
    connect(m_document, &Document::selectionChanged, this, [this] { viewport()->update(); });
    connect(m_document, &Document::playheadChanged, this, [this] {
        const int scrollBefore = horizontalScrollBar()->value();
        ensurePlayheadVisible();
        const double x = xForTick(m_document->playhead());
        if (horizontalScrollBar()->value() != scrollBefore || m_lastPlayheadX < 0 || m_drag != Drag::None) {
            viewport()->update();
        } else {
            // Only the old and new playhead columns plus the timecode change.
            const int lo = static_cast<int>(std::min(x, m_lastPlayheadX)) - 8;
            const int hi = static_cast<int>(std::max(x, m_lastPlayheadX)) + 8;
            viewport()->update(QRect(lo, 0, hi - lo, viewport()->height()));
            viewport()->update(QRect(0, 0, headerWidth(), 26));
        }
        m_lastPlayheadX = x;
    });

    m_waveforms = new WaveformCache(this);
    connect(m_waveforms, &WaveformCache::peaksReady, this, [this](const QString&) { viewport()->update(); });
    connect(&ThumbnailCache::instance(), &ThumbnailCache::ready, this, [this](const QString&) { viewport()->update(); });

    auto refreshMedia = [this] { m_waveforms->ensure(m_document->project().media); };
    connect(m_document, &Document::mediaChanged, this, refreshMedia);
    connect(m_document, &Document::documentLoaded, this, refreshMedia);
    refreshMedia();

    // Premiere's timeline header row: snap, linked selection, add marker, settings.
    m_headerTools = new QWidget(viewport());
    m_headerTools->setGeometry(4, 25, headerWidth() - 8, 18);
    auto* row = new QHBoxLayout(m_headerTools);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(1);
    m_snapButton = iconButton(icons::Id::Snap, tr("Snap in Timeline (S)"), m_headerTools, true, 13);
    m_snapButton->setChecked(true);
    connect(m_snapButton, &QToolButton::toggled, this, [this](bool on) {
        m_snapping = on;
        emit snappingToggled(on);
    });
    m_linkButton = iconButton(icons::Id::LinkedSelection, tr("Linked Selection (Ctrl+Shift+L)"), m_headerTools, true, 13);
    m_linkButton->setChecked(true);
    connect(m_linkButton, &QToolButton::toggled, this, [this](bool on) {
        m_linkedSelection = on;
        emit linkedSelectionToggled(on);
    });
    auto* marker = iconButton(icons::Id::AddMarker, tr("Add Marker (M)"), m_headerTools, false, 13);
    connect(marker, &QToolButton::clicked, this, &TimelineWidget::addMarker);
    auto* settings = iconButton(icons::Id::Wrench, tr("Timeline Display Settings"), m_headerTools, false, 13);
    settings->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(settings);
    QAction* thumbs = menu->addAction(tr("Show Video Thumbnails"));
    thumbs->setCheckable(true);
    thumbs->setChecked(true);
    connect(thumbs, &QAction::toggled, this, [this](bool on) { m_showThumbs = on; viewport()->update(); });
    QAction* waves = menu->addAction(tr("Show Audio Waveform"));
    waves->setCheckable(true);
    waves->setChecked(true);
    connect(waves, &QAction::toggled, this, [this](bool on) { m_showWaves = on; viewport()->update(); });
    menu->addSeparator();
    menu->addAction(tr("Minimize All Tracks"), this, [this] {
        if (Sequence* s = m_document->sequence()) for (auto& t : s->tracks) t.height = 28;
        updateScrollbars(); viewport()->update();
    });
    menu->addAction(tr("Expand All Tracks"), this, [this] {
        if (Sequence* s = m_document->sequence()) for (auto& t : s->tracks) t.height = 90;
        updateScrollbars(); viewport()->update();
    });
    menu->addAction(tr("Reset Track Heights"), this, [this] {
        if (Sequence* s = m_document->sequence()) for (auto& t : s->tracks) t.height = 0;
        updateScrollbars(); viewport()->update();
    });
    settings->setMenu(menu);
    for (QToolButton* b : {m_snapButton, m_linkButton, marker, settings}) row->addWidget(b);
    row->addStretch(1);

    updateScrollbars();
}

void TimelineWidget::setSnapping(bool on)
{
    m_snapping = on;
    if (m_snapButton && m_snapButton->isChecked() != on) m_snapButton->setChecked(on);
}

void TimelineWidget::setLinkedSelection(bool on)
{
    m_linkedSelection = on;
    if (m_linkButton && m_linkButton->isChecked() != on) m_linkButton->setChecked(on);
}

QString TimelineWidget::toolName(Tool tool)
{
    switch (tool) {
    case Tool::Select: return tr("Selection Tool (V)");
    case Tool::TrackSelect: return tr("Track Select Forward Tool (A)");
    case Tool::Ripple: return tr("Ripple Edit Tool (B)");
    case Tool::Roll: return tr("Rolling Edit Tool (N)");
    case Tool::RateStretch: return tr("Rate Stretch Tool (R)");
    case Tool::Razor: return tr("Razor Tool (C)");
    case Tool::Slip: return tr("Slip Tool (Y)");
    case Tool::Slide: return tr("Slide Tool (U)");
    case Tool::Hand: return tr("Hand Tool (H)");
    case Tool::Zoom: return tr("Zoom Tool (Z)");
    }
    return {};
}

Tick TimelineWidget::defaultTransitionTicks() const
{
    return frameTicks() * m_defaultTransitionFrames;
}

// ------------------------------------------------------------------- geometry

int TimelineWidget::trackHeight(int index) const
{
    const Sequence* seq = m_document->sequence();
    const Track* t = seq ? seq->track(index) : nullptr;
    if (!t) return 60;
    if (t->height > 0) return std::clamp(t->height, 24, 240);
    return t->kind == TrackKind::Audio ? 46 : 46;
}

int TimelineWidget::trackTop(int index) const
{
    int y = rulerHeight();
    for (int i = 0; i < index; ++i) y += trackHeight(i);
    return y - verticalScrollBar()->value();
}

int TimelineWidget::totalTracksHeight() const
{
    const Sequence* seq = m_document->sequence();
    int h = 0;
    if (seq)
        for (int i = 0; i < seq->tracks.size(); ++i) h += trackHeight(i);
    return h;
}

int TimelineWidget::trackAtY(int y) const
{
    const Sequence* seq = m_document->sequence();
    if (!seq || y < rulerHeight()) return -1;
    for (int i = 0; i < seq->tracks.size(); ++i) {
        const int top = trackTop(i);
        if (y >= top && y < top + trackHeight(i)) return i;
    }
    return -1;
}

double TimelineWidget::xForTick(Tick tick) const
{
    return headerWidth() + ticksToSec(tick) * m_pps - horizontalScrollBar()->value();
}

Tick TimelineWidget::tickAtX(double x) const
{
    const double seconds = (x - headerWidth() + horizontalScrollBar()->value()) / m_pps;
    return secToTicks(std::max(0.0, seconds));
}

Tick TimelineWidget::frameTicks() const
{
    const Sequence* seq = m_document->sequence();
    return seq ? seq->rate.ticksPerFrame() : FrameRate{25, 1}.ticksPerFrame();
}

QVector<Tick> TimelineWidget::snapPoints(const QSet<QString>& ignore) const
{
    QVector<Tick> points{0, m_document->playhead()};
    const Sequence* seq = m_document->sequence();
    if (!seq) return points;
    for (const auto& track : seq->tracks)
        for (const auto& clip : track.clips) {
            if (ignore.contains(clip.id)) continue;
            points << clip.start << clip.end();
        }
    for (const auto& m : seq->markers) points << m.time;
    if (seq->inPoint >= 0) points << seq->inPoint;
    if (seq->outPoint >= 0) points << seq->outPoint;
    return points;
}

Tick TimelineWidget::snapTick(Tick tick, const QSet<QString>& ignore, double* snappedX) const
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return tick;
    const Tick framed = seq->rate.snap(tick);
    if (snappedX) *snappedX = -1;
    if (!m_snapping) return framed;

    const Tick threshold = secToTicks(9.0 / m_pps);
    Tick best = framed;
    Tick bestDistance = threshold + 1;
    for (Tick candidate : snapPoints(ignore)) {
        const Tick distance = std::llabs(candidate - tick);
        if (distance <= threshold && distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    if (bestDistance <= threshold && snappedX) *snappedX = xForTick(best);
    return best;
}

QRectF TimelineWidget::clipRect(int trackIndex, const Clip& clip) const
{
    const double x1 = xForTick(clip.start);
    const double x2 = xForTick(clip.end());
    return QRectF(x1, trackTop(trackIndex) + 2.0, std::max(2.0, x2 - x1), trackHeight(trackIndex) - 4.0);
}

double TimelineWidget::gainLineY(const QRectF& rect, double gainDb) const
{
    const double top = rect.top() + 16;
    const double bottom = rect.bottom() - 4;
    const double t = (kGainTopDb - std::clamp(gainDb, kGainBottomDb, kGainTopDb)) / (kGainTopDb - kGainBottomDb);
    return top + (bottom - top) * std::sqrt(t);
}

double TimelineWidget::gainForY(const QRectF& rect, double y) const
{
    const double top = rect.top() + 16;
    const double bottom = rect.bottom() - 4;
    const double n = std::clamp((y - top) / std::max(1.0, bottom - top), 0.0, 1.0);
    const double db = kGainTopDb - n * n * (kGainTopDb - kGainBottomDb);
    return std::round(db * 10.0) / 10.0;
}

void TimelineWidget::updateScrollbars()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;

    Tick end = std::max(seq->duration, m_document->playhead());
    end += secToTicks(std::max(10.0, ticksToSec(end) * 0.25));

    const int viewWidth = std::max(50, viewport()->width() - headerWidth());
    const int contentWidth = static_cast<int>(ticksToSec(end) * m_pps);
    horizontalScrollBar()->setRange(0, std::max(0, contentWidth - viewWidth));
    horizontalScrollBar()->setPageStep(viewWidth);
    horizontalScrollBar()->setSingleStep(std::max(8, static_cast<int>(m_pps)));

    const int viewHeight = std::max(40, viewport()->height() - rulerHeight());
    verticalScrollBar()->setRange(0, std::max(0, totalTracksHeight() + 40 - viewHeight));
    verticalScrollBar()->setPageStep(viewHeight);
}

void TimelineWidget::ensurePlayheadVisible()
{
    if (m_drag == Drag::Scrub) return;
    const double x = xForTick(m_document->playhead());
    const int left = headerWidth();
    const int right = viewport()->width();
    if (x < left + 20)
        horizontalScrollBar()->setValue(static_cast<int>(horizontalScrollBar()->value() - (left + 20 - x)));
    else if (x > right - 40)
        horizontalScrollBar()->setValue(static_cast<int>(
            horizontalScrollBar()->value() + (x - (right - 40)) + (right - left) * 0.3));
}

QString TimelineWidget::mediaLabel(const QString& mediaId) const
{
    if (const MediaRef* ref = m_document->project().mediaById(mediaId)) return ref->name;
    return tr("Missing media");
}

QImage TimelineWidget::thumbnailFor(const QString& mediaId)
{
    const MediaRef* ref = m_document->project().mediaById(mediaId);
    if (!ref || !ref->hasVideo || ref->offline) return {};
    const QImage img = ThumbnailCache::instance().get(ref->proxyPath.isEmpty() ? ref->path : ref->proxyPath, 112, 63);
    return img.width() > 1 ? img : QImage();
}

// ------------------------------------------------------------------ hit tests

TimelineWidget::Hit TimelineWidget::hitTest(const QPoint& pos) const
{
    Hit hit;
    if (pos.x() < headerWidth() || pos.y() < rulerHeight()) return hit;
    hit.track = trackAtY(pos.y());
    const Sequence* seq = m_document->sequence();
    if (!seq || hit.track < 0) return hit;
    const Track& track = seq->tracks[hit.track];

    // Transition boxes first (top band of the track).
    const int top = trackTop(hit.track);
    const bool inBand = pos.y() < top + 2 + (trackHeight(hit.track) - 4) * 0.45;
    if (inBand) {
        for (int ci = 0; ci < track.clips.size(); ++ci) {
            const Clip& c = track.clips[ci];
            auto test = [&](Tick a, Tick b, bool head) {
                if (pos.x() >= xForTick(a) && pos.x() <= xForTick(b)) {
                    hit.onTransition = true;
                    hit.transitionHead = head;
                    hit.clipIndex = ci;
                    hit.clipId = c.id;
                    return true;
                }
                return false;
            };
            if (c.inTransition.active()) {
                const Tick half = crossHalf(track, ci);
                if (half > 0 ? test(c.start - half, c.start + half, true)
                             : test(c.start, c.start + std::min(c.inTransition.duration, c.duration), true))
                    return hit;
            }
            const bool nextCross = ci + 1 < track.clips.size() && hasCrossHead(track, ci + 1);
            if (c.outTransition.active() && !nextCross) {
                if (test(c.end() - std::min(c.outTransition.duration, c.duration), c.end(), false)) return hit;
            }
        }
    }

    const Tick t = tickAtX(pos.x());
    int ci = track.indexAt(t);

    // Edges: also catch an edge when the cursor sits just outside the clip.
    auto edgeOf = [&](int index) {
        const Clip& c = track.clips[index];
        const double x1 = xForTick(c.start);
        const double x2 = xForTick(c.end());
        const bool narrow = x2 - x1 < kEdgeZone * 3;
        if (std::abs(pos.x() - x1) <= kEdgeZone && (!narrow || pos.x() <= (x1 + x2) / 2)) return Edge::Head;
        if (std::abs(pos.x() - x2) <= kEdgeZone && (!narrow || pos.x() > (x1 + x2) / 2)) return Edge::Tail;
        return Edge::None;
    };

    if (ci < 0) {
        for (int i = 0; i < track.clips.size(); ++i) {
            const Edge e = edgeOf(i);
            if (e != Edge::None) {
                hit.clipIndex = i;
                hit.clipId = track.clips[i].id;
                hit.edge = e;
                return hit;
            }
        }
        return hit;
    }

    hit.clipIndex = ci;
    hit.clipId = track.clips[ci].id;
    hit.edge = edgeOf(ci);

    if (hit.edge == Edge::None && track.kind == TrackKind::Audio) {
        const QRectF r = clipRect(hit.track, track.clips[ci]);
        if (std::abs(pos.y() - gainLineY(r, track.clips[ci].gainDb)) <= 4) hit.onGainLine = true;
    }
    return hit;
}

QRect TimelineWidget::headerButtonRect(int trackIndex, HeaderButton button) const
{
    const int top = trackTop(trackIndex);
    const int h = trackHeight(trackIndex);
    const int cy = top + std::min(h / 2, 15);
    switch (button) {
    case HeaderButton::Patch: return QRect(4, cy - 9, 22, 18);
    case HeaderButton::Lock: return QRect(29, cy - 8, 16, 16);
    case HeaderButton::Target: return QRect(47, cy - 9, 24, 18);
    case HeaderButton::Sync: return QRect(74, cy - 8, 16, 16);
    case HeaderButton::Mute: return QRect(93, cy - 8, 16, 16);
    case HeaderButton::Solo: return QRect(111, cy - 8, 16, 16);
    default: return {};
    }
}

TimelineWidget::HeaderButton TimelineWidget::headerButtonAt(const QPoint& pos, int trackIndex) const
{
    const Sequence* seq = m_document->sequence();
    const bool audio = seq && seq->track(trackIndex) && seq->tracks[trackIndex].kind == TrackKind::Audio;
    for (HeaderButton b : {HeaderButton::Patch, HeaderButton::Lock, HeaderButton::Target, HeaderButton::Sync,
                           HeaderButton::Mute, HeaderButton::Solo}) {
        if (b == HeaderButton::Solo && !audio) continue;
        if (headerButtonRect(trackIndex, b).adjusted(-1, -2, 1, 2).contains(pos)) return b;
    }
    return HeaderButton::None;
}

// ----------------------------------------------------------------- zoom/tools

void TimelineWidget::zoomIn()
{
    const Tick anchor = m_document->playhead();
    const double ax = xForTick(anchor);
    m_pps = std::min(6000.0, m_pps * 1.4);
    updateScrollbars();
    horizontalScrollBar()->setValue(static_cast<int>(ticksToSec(anchor) * m_pps - (ax - headerWidth())));
    viewport()->update();
}

void TimelineWidget::zoomOut()
{
    const Tick anchor = m_document->playhead();
    const double ax = xForTick(anchor);
    m_pps = std::max(0.5, m_pps / 1.4);
    updateScrollbars();
    horizontalScrollBar()->setValue(static_cast<int>(ticksToSec(anchor) * m_pps - (ax - headerWidth())));
    viewport()->update();
}

void TimelineWidget::zoomToFit()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const int viewWidth = std::max(50, viewport()->width() - headerWidth() - 30);
    const double seconds = std::max(2.0, ticksToSec(seq->duration));
    m_pps = std::clamp(viewWidth / seconds, 0.5, 6000.0);
    horizontalScrollBar()->setValue(0);
    updateScrollbars();
    viewport()->update();
}

void TimelineWidget::setTool(Tool tool)
{
    m_tool = tool;
    Qt::CursorShape shape = Qt::ArrowCursor;
    switch (tool) {
    case Tool::Razor: shape = Qt::CrossCursor; break;
    case Tool::Hand: shape = Qt::OpenHandCursor; break;
    case Tool::Slip:
    case Tool::Slide: shape = Qt::SizeHorCursor; break;
    case Tool::Zoom: shape = Qt::PointingHandCursor; break;
    default: break;
    }
    viewport()->setCursor(shape);
    emit toolChanged(tool);
    emit statusMessage(toolName(tool));
    viewport()->update();
}

// -------------------------------------------------------------------- painting

void TimelineWidget::paintEvent(QPaintEvent* event)
{
    m_paintRect = event->rect();
    QPainter painter(viewport());
    painter.setClipRect(m_paintRect);
    painter.fillRect(viewport()->rect(), theme::windowBg());

    paintTracks(painter);
    paintOverlays(painter);
    paintTrackHeaders(painter);
    paintRuler(painter);
    paintPlayhead(painter);
}

void TimelineWidget::paintRuler(QPainter& painter)
{
    const QRect ruler(0, 0, viewport()->width(), rulerHeight());
    painter.fillRect(ruler, theme::rulerBg());
    painter.fillRect(QRect(0, 0, headerWidth(), rulerHeight()), theme::panelBg());
    painter.fillRect(QRect(0, rulerHeight() - 1, viewport()->width(), 1), QColor(0x0e, 0x0e, 0x0e));

    const Sequence* seq = m_document->sequence();
    if (!seq) return;

    // Playhead timecode in the corner, Premiere-style.
    painter.setFont(theme::monoFont(12));
    painter.setPen(theme::playhead());
    painter.drawText(QRect(8, 1, headerWidth() - 12, 24), Qt::AlignVCenter | Qt::AlignLeft,
                     formatTimecode(m_document->playhead(), seq->rate));

    painter.save();
    painter.setClipRect(QRect(headerWidth(), 0, viewport()->width() - headerWidth(), rulerHeight()));

    // In/out range.
    if (seq->inPoint >= 0 || seq->outPoint >= 0) {
        const Tick in = seq->inPoint >= 0 ? seq->inPoint : 0;
        const Tick out = seq->outPoint >= 0 ? seq->outPoint : seq->duration;
        const QRectF r(xForTick(in), rulerHeight() - 9, xForTick(out) - xForTick(in), 8);
        painter.fillRect(r, QColor(0x5c, 0xad, 0xff, 90));
        painter.setPen(QPen(theme::playhead(), 2));
        if (seq->inPoint >= 0) painter.drawLine(QPointF(r.left(), r.top()), QPointF(r.left(), r.bottom()));
        if (seq->outPoint >= 0) painter.drawLine(QPointF(r.right(), r.top()), QPointF(r.right(), r.bottom()));
    }

    static const double steps[] = {0.04, 0.08, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60,
                                   120, 300, 600, 900, 1800, 3600, 7200};
    double stepSeconds = steps[sizeof(steps) / sizeof(steps[0]) - 1];
    for (double candidate : steps) {
        if (candidate * m_pps >= 92.0) {
            stepSeconds = candidate;
            break;
        }
    }
    // Frame-accurate steps when zoomed far in.
    const double frameSec = ticksToSec(seq->rate.ticksPerFrame());
    if (frameSec * m_pps >= 92.0) stepSeconds = frameSec;
    else if (stepSeconds < frameSec * 2) stepSeconds = std::ceil(stepSeconds / frameSec) * frameSec;

    const double startSeconds = horizontalScrollBar()->value() / m_pps;
    const double endSeconds = startSeconds + (viewport()->width() - headerWidth()) / m_pps;
    const double firstTick = std::floor(startSeconds / stepSeconds) * stepSeconds;

    painter.setFont(theme::monoFont(7));
    double lastLabelX = -1e9;
    for (double s = firstTick; s <= endSeconds + stepSeconds; s += stepSeconds) {
        if (s < 0) continue;
        const double x = headerWidth() + s * m_pps - horizontalScrollBar()->value();
        painter.setPen(QColor(0x55, 0x55, 0x5e));
        painter.drawLine(QPointF(x, rulerHeight() - 10), QPointF(x, rulerHeight() - 1));
        if (x - lastLabelX >= 80.0) {
            painter.setPen(theme::textDim());
            painter.drawText(QRectF(x + 3, 2, 90, 12), Qt::AlignLeft | Qt::AlignVCenter,
                             formatTimecode(secToTicks(s), seq->rate));
            lastLabelX = x;
        }
        painter.setPen(QColor(0x3c, 0x3c, 0x44));
        for (int m = 1; m < 5; ++m) {
            const double mx = x + (stepSeconds * m_pps) / 5.0;
            painter.drawLine(QPointF(mx, rulerHeight() - 5), QPointF(mx, rulerHeight() - 1));
        }
    }

    // Markers.
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const auto& m : seq->markers) {
        const double x = xForTick(m.time);
        if (x < headerWidth() - 10 || x > viewport()->width() + 10) continue;
        const QColor c = theme::labelColor(m.color);
        if (m.duration > 0) {
            painter.fillRect(QRectF(x, 15, xForTick(m.time + m.duration) - x, 6), QColor(c.red(), c.green(), c.blue(), 120));
        }
        QPolygonF shape;
        shape << QPointF(x - 5, 14) << QPointF(x + 5, 14) << QPointF(x + 5, 21) << QPointF(x, 26)
              << QPointF(x - 5, 21);
        painter.setPen(Qt::NoPen);
        painter.setBrush(c);
        painter.drawPolygon(shape);
        if (!m.name.isEmpty()) {
            painter.setPen(theme::text());
            painter.setFont(theme::uiFont(7));
            painter.drawText(QPointF(x + 7, 22), m.name);
        }
    }
    painter.restore();
}

void TimelineWidget::paintTrackHeaders(QPainter& painter)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;

    painter.save();
    painter.setClipRect(QRect(0, rulerHeight(), headerWidth(), viewport()->height() - rulerHeight()));
    painter.fillRect(QRect(0, rulerHeight(), headerWidth(), viewport()->height()), theme::panelBg());

    const QColor iconOff(0x8a, 0x8a, 0x8a);
    const QColor iconOn(0xe6, 0xe6, 0xe6);

    for (int i = 0; i < seq->tracks.size(); ++i) {
        const Track& track = seq->tracks[i];
        const int top = trackTop(i);
        const int height = trackHeight(i);
        if (top > viewport()->height() || top + height < rulerHeight()) continue;

        const bool audio = track.kind == TrackKind::Audio;
        const QRect header(0, top, headerWidth() - 1, height);
        painter.fillRect(header.adjusted(0, 0, 0, -1), QColor(0x22, 0x22, 0x22));
        painter.fillRect(QRect(0, top + height - 1, headerWidth(), 1), QColor(0x14, 0x14, 0x14));
        const QString lane = (audio ? QStringLiteral("A") : QStringLiteral("V")) + QString::number(seq->laneOf(i));

        painter.setRenderHint(QPainter::Antialiasing, true);
        // Source patch indicator (blue box on the patched track, outline elsewhere).
        const QRect patch = headerButtonRect(i, HeaderButton::Patch);
        const bool patched = seq->laneOf(i) == (audio ? seq->patchAudio : seq->patchVideo);
        painter.setPen(Qt::NoPen);
        painter.setBrush(patched ? theme::accent() : QColor(0x2a, 0x2a, 0x2a));
        painter.drawRoundedRect(QRectF(patch).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
        if (patched) {
            painter.setPen(Qt::white);
            painter.setFont(theme::uiFont(7, true));
            painter.drawText(patch, Qt::AlignCenter, lane);
        }

        icons::paint(painter, track.locked ? icons::Id::Lock : icons::Id::Unlock,
                     headerButtonRect(i, HeaderButton::Lock), track.locked ? iconOn : iconOff);

        // Track target (the lane label), blue when targeted.
        const QRect target = headerButtonRect(i, HeaderButton::Target);
        painter.setPen(Qt::NoPen);
        painter.setBrush(track.targeted ? theme::accent() : QColor(0x33, 0x33, 0x33));
        painter.drawRoundedRect(QRectF(target), 3, 3);
        painter.setPen(track.targeted ? Qt::white : theme::textDim());
        painter.setFont(theme::uiFont(7, true));
        painter.drawText(target, Qt::AlignCenter, lane);

        icons::paint(painter, track.syncLock ? icons::Id::SyncLock : icons::Id::SyncLockOff,
                     headerButtonRect(i, HeaderButton::Sync), track.syncLock ? iconOn : iconOff);

        if (!audio) {
            icons::paint(painter, track.muted ? icons::Id::EyeOff : icons::Id::Eye,
                         headerButtonRect(i, HeaderButton::Mute), track.muted ? iconOff : iconOn);
        } else {
            auto letter = [&](HeaderButton b, const QString& text, bool on, const QColor& onColor) {
                const QRect r = headerButtonRect(i, b);
                painter.setPen(on ? Qt::NoPen : QPen(QColor(0x55, 0x55, 0x55), 1));
                painter.setBrush(on ? onColor : Qt::NoBrush);
                painter.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
                painter.setPen(on ? QColor(0x10, 0x10, 0x10) : theme::textDim());
                painter.setFont(theme::uiFont(7, true));
                painter.drawText(r, Qt::AlignCenter, text);
            };
            letter(HeaderButton::Mute, QStringLiteral("M"), track.muted, QColor(0x4c, 0xae, 0x55));
            letter(HeaderButton::Solo, QStringLiteral("S"), track.solo, QColor(0xf2, 0xc9, 0x38));
        }
        painter.setRenderHint(QPainter::Antialiasing, false);

        // Name when the track is tall enough (Premiere shows "Video 1"/"Audio 1").
        if (height >= 36) {
            painter.setPen(track.locked ? theme::textFaint() : theme::textDim());
            painter.setFont(theme::uiFont(7));
            QString name = track.name;
            if (name == lane) name = (audio ? tr("Audio %1") : tr("Video %1")).arg(seq->laneOf(i));
            painter.drawText(QRect(30, top + height - 15, headerWidth() - 34, 13), Qt::AlignLeft | Qt::AlignVCenter,
                             QFontMetrics(painter.font()).elidedText(name, Qt::ElideRight, headerWidth() - 34));
        } else if (headerWidth() > 140) {
            painter.setPen(theme::textDim());
            painter.setFont(theme::uiFont(7));
            painter.drawText(QRect(131, top, headerWidth() - 133, height), Qt::AlignLeft | Qt::AlignVCenter,
                             QFontMetrics(painter.font()).elidedText(track.name, Qt::ElideRight, headerWidth() - 135));
        }
        if (track.locked) painter.fillRect(header, QColor(0, 0, 0, 40));
    }

    // V/A divider.
    for (int i = 1; i < seq->tracks.size(); ++i) {
        if (seq->tracks[i - 1].kind != seq->tracks[i].kind) {
            painter.fillRect(QRect(0, trackTop(i) - 2, viewport()->width(), 3), QColor(0x0e, 0x0e, 0x0e));
        }
    }
    painter.fillRect(QRect(headerWidth() - 1, rulerHeight(), 1, viewport()->height()), QColor(0x0e, 0x0e, 0x0e));
    painter.restore();
}

void TimelineWidget::paintTracks(QPainter& painter)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;

    const int left = headerWidth();
    const int width = viewport()->width() - left;

    painter.save();
    painter.setClipRect(QRect(left, rulerHeight(), width, viewport()->height() - rulerHeight()));

    for (int i = 0; i < seq->tracks.size(); ++i) {
        const Track& track = seq->tracks[i];
        const int top = trackTop(i);
        const int height = trackHeight(i);
        if (top > viewport()->height() || top + height < rulerHeight()) continue;

        const bool audio = track.kind == TrackKind::Audio;
        const QRect area(left, top, width, height);
        painter.fillRect(area, audio ? theme::trackAudioBg(i % 2 == 1) : theme::trackVideoBg(i % 2 == 1));

        painter.setPen(theme::borderSoft());
        painter.drawLine(left, top + height - 1, viewport()->width(), top + height - 1);

        for (int ci = 0; ci < track.clips.size(); ++ci) {
            const Clip& clip = track.clips[ci];
            const QRectF rect = clipRect(i, clip);
            if (rect.right() < left || rect.left() > viewport()->width()) continue;
            paintClip(painter, i, ci, clip, m_document->isSelected(clip.id));
        }
        paintTransitions(painter, i);

        if (!trackActive(*seq, track)) painter.fillRect(area, QColor(0, 0, 0, 110));
        if (track.locked) {
            painter.save();
            painter.setBrush(QBrush(QColor(255, 255, 255, 14), Qt::BDiagPattern));
            painter.setPen(Qt::NoPen);
            painter.drawRect(area);
            painter.restore();
        }
    }

    const int lastTop = trackTop(seq->tracks.size());
    if (lastTop < viewport()->height())
        painter.fillRect(QRect(left, lastTop, width, viewport()->height() - lastTop), theme::windowBg());

    painter.restore();
}

void TimelineWidget::paintClip(QPainter& painter, int trackIndex, int clipIndex, const Clip& clip, bool selected)
{
    Q_UNUSED(clipIndex);
    const Sequence* seq = m_document->sequence();
    const Track& track = seq->tracks[trackIndex];
    const bool audio = track.kind == TrackKind::Audio;
    QRectF rect = clipRect(trackIndex, clip);
    rect.adjust(0, 0, -1, 0);
    if (!rect.intersects(QRectF(m_paintRect))) return;

    // Premiere defaults: video = Iris, audio = Forest, graphics = Lavender, mattes = Mango.
    QColor label;
    if (clip.labelColor >= 0) label = theme::labelColor(clip.labelColor);
    else if (clip.generator == GeneratorKind::Title) label = theme::labelColor(3);
    else if (clip.generator == GeneratorKind::ColorMatte) label = theme::labelColor(7);
    else label = audio ? theme::labelColor(5) : theme::labelColor(1);

    QColor body = selected ? label.lighter(125) : label;
    if (!clip.enabled) body = mix(body, QColor(0x40, 0x40, 0x40), 0.75);
    const QColor textColor = clip.enabled ? QColor(0x0e, 0x0e, 0x12) : QColor(0x9a, 0x9a, 0x9a);

    painter.save();
    painter.fillRect(rect, body);

    const double nameH = std::min(15.0, rect.height());
    // Video: thumbnail at the head, under the name strip.
    if (!audio && m_showThumbs && clip.generator == GeneratorKind::None && rect.height() >= 30 && rect.width() > 50) {
        const QImage thumb = thumbnailFor(clip.mediaId);
        if (!thumb.isNull()) {
            const double th = rect.height() - nameH - 1;
            const double tw = th * thumb.width() / std::max(1, thumb.height());
            const QRectF tr(rect.left() + 1, rect.top() + nameH, std::min(tw, rect.width() - 2), th);
            painter.setOpacity(clip.enabled ? 1.0 : 0.4);
            painter.drawImage(tr, thumb, QRectF(0, 0, thumb.width() * tr.width() / tw, thumb.height()));
            painter.setOpacity(1.0);
        }
    }
    if (clip.generator == GeneratorKind::ColorMatte && rect.height() > 26) {
        painter.fillRect(QRectF(rect.left() + 3, rect.top() + nameH + 2, std::min(22.0, rect.width() - 6),
                                rect.height() - nameH - 4), clip.matteColor);
    }

    // Audio: waveform drawn in a darker shade of the label, clipped to the repaint area.
    if (audio && m_waveforms && m_showWaves) {
        const QRectF wave(rect.left() + 1, rect.top() + nameH, rect.width() - 2, rect.height() - nameH - 2);
        const QVector<float> peaks = m_waveforms->peaks(clip.mediaId);
        const double gain = dbToGain(clip.gainDb);
        if (!peaks.isEmpty() && wave.height() > 6) {
            painter.setPen(QPen(label.darker(240), 1));
            const double mid = wave.center().y();
            const int px0 = static_cast<int>(std::max<double>({wave.left(), static_cast<double>(headerWidth()), static_cast<double>(m_paintRect.left())}));
            const int px1 = static_cast<int>(std::min<double>({wave.right(), static_cast<double>(viewport()->width()), static_cast<double>(m_paintRect.right())}));
            for (int x = px0; x <= px1; ++x) {
                const Tick src = clip.sourceTime(tickAtX(x));
                const int bucket = static_cast<int>(ticksToSec(src) * 20.0);
                if (bucket < 0 || bucket >= peaks.size()) continue;
                const double amp = std::min(1.0, static_cast<double>(peaks[bucket]) * gain);
                const double half = std::max(0.5, amp * (wave.height() * 0.48));
                painter.drawLine(QPointF(x, mid - half), QPointF(x, mid + half));
            }
        }
        if (rect.height() >= 30) {
            const double y = gainLineY(rect, clip.gainDb);
            const bool hot = m_hover.onGainLine && m_hover.clipId == clip.id;
            painter.setPen(QPen(hot ? QColor(0xff, 0xff, 0xff) : QColor(0xf5, 0xe6, 0x6e), hot ? 1.8 : 1.0));
            painter.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
        }
    }

    // Name strip: fx badge + name in dark text, as in Premiere.
    if (rect.width() > 14) {
        painter.setClipRect(rect.adjusted(2, 0, -2, 0).intersected(QRectF(m_paintRect)));
        double x = rect.left() + 3;
        bool hasFx = false;
        for (const auto& e : clip.effects) {
            if (e.type == QLatin1String("motion") || e.type == QLatin1String("opacity") || e.type == QLatin1String("volume")) {
                for (const auto& p : e.params) {
                    EffectInstance fresh;
                    if (!p.keys.isEmpty()) hasFx = true;
                }
                continue;
            }
            if (e.type == QLatin1String("lumetri")) {
                for (const auto& p : e.params)
                    if (!p.keys.isEmpty() || (p.id == QLatin1String("saturation") ? p.value != 100.0 : p.value != 0.0)) hasFx = true;
                continue;
            }
            hasFx = true;
        }
        painter.setRenderHint(QPainter::Antialiasing, true);
        if (rect.width() > 40) {
            const QRectF badge(x, rect.top() + 2, 14, 10);
            painter.setPen(Qt::NoPen);
            painter.setBrush(hasFx ? QColor(0xf2, 0xc9, 0x38) : mix(body, Qt::black, 0.25));
            painter.drawRoundedRect(badge, 2, 2);
            painter.setPen(hasFx ? QColor(0x10, 0x10, 0x10) : mix(body, Qt::black, 0.55));
            QFont f = theme::uiFont(6, true);
            f.setItalic(true);
            painter.setFont(f);
            painter.drawText(badge, Qt::AlignCenter, QStringLiteral("fx"));
            x += 18;
        }
        QString text = clip.label;
        if (text.isEmpty()) {
            if (clip.generator == GeneratorKind::Title) text = clip.title.text.section(QLatin1Char('\n'), 0, 0);
            else if (clip.generator == GeneratorKind::ColorMatte) text = tr("Color Matte");
            else text = mediaLabel(clip.mediaId);
        }
        if (std::abs(clip.speed - 1.0) > 1e-6 || clip.reverse)
            text += QStringLiteral(" [%1%2%]").arg(clip.reverse ? QStringLiteral("-") : QString()).arg(clip.speed * 100.0, 0, 'f', 0);
        painter.setPen(textColor);
        painter.setFont(theme::uiFont(7, false));
        painter.drawText(QRectF(x, rect.top(), rect.right() - x - 2, nameH), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(painter.font()).elidedText(text, Qt::ElideRight, static_cast<int>(rect.right() - x - 2)));
        painter.setClipping(false);
    }

    if (const MediaRef* ref = m_document->project().mediaById(clip.mediaId); ref && ref->offline) {
        painter.fillRect(rect, QColor(0xc0, 0x30, 0x30, 170));
        painter.setPen(Qt::white);
        painter.setFont(theme::uiFont(7, true));
        painter.drawText(rect, Qt::AlignCenter, tr("Media Offline"));
    }

    // Outline: thin dark edge, white when selected.
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(selected ? QPen(Qt::white, 1) : QPen(mix(body, Qt::black, 0.45), 1));
    painter.drawRect(rect.adjusted(0, 0, -1, -1));

    // Edge hover highlight (trim cursor zone).
    if (m_hover.clipId == clip.id && m_hover.edge != Edge::None && m_drag == Drag::None) {
        const double ex = m_hover.edge == Edge::Head ? rect.left() + 1 : rect.right() - 1;
        QColor c = Qt::white;
        if (m_tool == Tool::Ripple) c = QColor(0xf2, 0xc9, 0x38);
        if (m_tool == Tool::Roll) c = QColor(0xe0, 0x5a, 0x5a);
        painter.setPen(QPen(c, 2));
        painter.drawLine(QPointF(ex, rect.top()), QPointF(ex, rect.bottom()));
    }
    painter.restore();
}

void TimelineWidget::paintTransitions(QPainter& painter, int trackIndex)
{
    const Sequence* seq = m_document->sequence();
    const Track& track = seq->tracks[trackIndex];
    const int top = trackTop(trackIndex);
    const double bandH = (trackHeight(trackIndex) - 4) * 0.45;

    auto drawBox = [&](Tick a, Tick b, const Transition& tr, bool selected) {
        const QRectF r(xForTick(a), top + 2, std::max(4.0, xForTick(b) - xForTick(a)), bandH);
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        QLinearGradient g(r.topLeft(), r.topRight());
        g.setColorAt(0, QColor(0x9a, 0x8c, 0xd8, 220));
        g.setColorAt(1, QColor(0x5e, 0x50, 0xa8, 220));
        painter.setBrush(g);
        painter.setPen(QPen(selected ? Qt::white : QColor(0x2a, 0x22, 0x50), selected ? 1.6 : 1));
        painter.drawRoundedRect(r, 2, 2);
        painter.setPen(QPen(QColor(255, 255, 255, 120), 1));
        painter.drawLine(r.bottomLeft(), r.topRight());
        if (r.width() > 60) {
            painter.setPen(Qt::white);
            painter.setFont(theme::uiFont(7, true));
            painter.drawText(r.adjusted(4, 0, -4, 0), Qt::AlignVCenter | Qt::AlignLeft,
                             QFontMetrics(painter.font()).elidedText(transitionName(tr.type), Qt::ElideRight,
                                                                     static_cast<int>(r.width()) - 8));
        }
        painter.restore();
    };

    for (int ci = 0; ci < track.clips.size(); ++ci) {
        const Clip& c = track.clips[ci];
        if (c.inTransition.active()) {
            const bool sel = m_selectedTransitionClip == c.id && m_selectedTransitionHead;
            const Tick half = crossHalf(track, ci);
            if (half > 0) drawBox(c.start - half, c.start + half, c.inTransition, sel);
            else drawBox(c.start, c.start + std::min(c.inTransition.duration, c.duration), c.inTransition, sel);
        }
        const bool nextCross = ci + 1 < track.clips.size() && hasCrossHead(track, ci + 1);
        if (c.outTransition.active() && !nextCross) {
            const bool sel = m_selectedTransitionClip == c.id && !m_selectedTransitionHead;
            drawBox(c.end() - std::min(c.outTransition.duration, c.duration), c.end(), c.outTransition, sel);
        }
    }
}

void TimelineWidget::paintOverlays(QPainter& painter)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const int left = headerWidth();
    painter.save();
    painter.setClipRect(QRect(left, rulerHeight(), viewport()->width() - left, viewport()->height() - rulerHeight()));

    // In/out shading across tracks.
    if (seq->inPoint >= 0 || seq->outPoint >= 0) {
        const Tick in = seq->inPoint >= 0 ? seq->inPoint : 0;
        const Tick out = seq->outPoint >= 0 ? seq->outPoint : seq->duration;
        painter.fillRect(QRectF(xForTick(in), rulerHeight(), xForTick(out) - xForTick(in),
                                viewport()->height() - rulerHeight()),
                         QColor(0x5c, 0xad, 0xff, 22));
    }

    // Marker lines.
    for (const auto& m : seq->markers) {
        const double x = xForTick(m.time);
        QColor c = theme::labelColor(m.color);
        c.setAlpha(90);
        painter.setPen(QPen(c, 1, Qt::DashLine));
        painter.drawLine(QPointF(x, rulerHeight()), QPointF(x, viewport()->height()));
    }

    // Snap indicator.
    if (m_snapX >= 0 && m_drag != Drag::None) {
        painter.setPen(QPen(QColor(0xff, 0xff, 0xff, 200), 1));
        painter.drawLine(QPointF(m_snapX, rulerHeight()), QPointF(m_snapX, viewport()->height()));
    }

    // Marquee.
    if (m_drag == Drag::Marquee && !m_marquee.isNull()) {
        painter.setPen(QPen(theme::accent(), 1, Qt::DashLine));
        painter.setBrush(QColor(0x2d, 0x8c, 0xf0, 40));
        painter.drawRect(m_marquee.normalized());
    }

    // Drop ghost.
    if (m_dropActive && m_dropTrack >= 0) {
        const QRectF r(xForTick(m_dropTick), trackTop(m_dropTrack) + 2,
                       std::max(6.0, xForTick(m_dropTick + m_dropDuration) - xForTick(m_dropTick)),
                       trackHeight(m_dropTrack) - 4);
        painter.setPen(QPen(Qt::white, 1.5, Qt::DashLine));
        painter.setBrush(QColor(255, 255, 255, 40));
        painter.drawRoundedRect(r, 3, 3);
    }
    painter.restore();
}

void TimelineWidget::paintPlayhead(QPainter& painter)
{
    const double x = xForTick(m_document->playhead());
    if (x < headerWidth() - 1 || x > viewport()->width()) return;

    painter.save();
    painter.setPen(QPen(theme::playhead(), 1.2));
    painter.drawLine(QPointF(x, rulerHeight() - 6), QPointF(x, viewport()->height()));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme::playhead());
    QPolygonF head;
    head << QPointF(x - 6, 2) << QPointF(x + 6, 2) << QPointF(x + 6, 10) << QPointF(x, rulerHeight() - 6)
         << QPointF(x - 6, 10);
    painter.drawPolygon(head);
    painter.restore();
}

// ------------------------------------------------------------------ selection

QStringList TimelineWidget::selectionWithLinks(const QString& clipId, bool ignoreLinks) const
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return {};
    if (ignoreLinks || !m_linkedSelection) return {clipId};
    return seq->linkedClips(clipId);
}

void TimelineWidget::selectFromClick(const Hit& hit, Qt::KeyboardModifiers mods)
{
    const bool alt = mods & Qt::AltModifier;
    const QStringList group = selectionWithLinks(hit.clipId, alt);
    QStringList sel = m_document->selectedClips();

    if (mods & (Qt::ShiftModifier | Qt::ControlModifier)) {
        const bool already = sel.contains(hit.clipId);
        for (const QString& id : group) {
            if (already) sel.removeAll(id);
            else if (!sel.contains(id)) sel << id;
        }
        m_document->setSelectionList(sel, already ? QString() : hit.clipId);
        return;
    }
    if (sel.contains(hit.clipId)) {
        // Keep the group (so it can be dragged) but make this the primary clip.
        m_document->setSelectionList(sel, hit.clipId);
        return;
    }
    m_document->setSelectionList(group, hit.clipId);
}

QStringList TimelineWidget::targetedSelection() const
{
    const Sequence* seq = m_document->sequence();
    QStringList out;
    if (!seq) return out;
    for (const QString& id : m_document->selectedClips()) {
        int ti = -1;
        if (seq->findClip(id, &ti) && !seq->tracks[ti].locked) out << id;
    }
    return out;
}

// -------------------------------------------------------------------- gestures

void TimelineWidget::beginGesture(Drag drag, const QString& text)
{
    m_drag = drag;
    m_dragText = text;
    m_dragBase = m_document->snapshotSequence();
    m_dragChanged = false;
}

void TimelineWidget::previewGesture(const std::function<bool(Sequence&)>& fn)
{
    Sequence preview = m_dragBase;
    const bool changed = fn(preview);
    if (!changed && !m_dragChanged) return;
    if (!changed) preview = m_dragBase;
    if (!edit::validate(preview)) return;
    m_dragChanged = changed;
    m_document->setLiveSequence(preview);
}

void TimelineWidget::endGesture(bool commit)
{
    if (commit && m_dragChanged) {
        m_document->commitSequence(m_dragBase, m_dragText);
    } else if (m_dragChanged) {
        m_document->setLiveSequence(m_dragBase);
    }
    m_drag = Drag::None;
    m_dragChanged = false;
    m_snapX = -1;
    m_dragBase = Sequence{};
}

void TimelineWidget::mousePressEvent(QMouseEvent* event)
{
    setFocus();
    const QPoint pos = event->position().toPoint();
    m_pressPos = pos;
    m_pressMoved = false;
    m_pressTick = tickAtX(pos.x());

    if (event->button() == Qt::MiddleButton || (m_tool == Tool::Hand && event->button() == Qt::LeftButton)) {
        m_drag = Drag::Hand;
        m_handStart = pos;
        m_handScrollX = horizontalScrollBar()->value();
        m_handScrollY = verticalScrollBar()->value();
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    Sequence* seq = m_document->sequence();
    if (!seq) return;

    // Ruler: scrub.
    if (pos.y() < rulerHeight()) {
        if (pos.x() < headerWidth()) return;
        emit scrubStarted();
        m_drag = Drag::Scrub;
        m_document->setPlayhead((event->modifiers() & Qt::ShiftModifier) ? snapTick(m_pressTick)
                                                                          : seq->rate.snap(m_pressTick));
        return;
    }

    // Track headers.
    if (pos.x() < headerWidth()) {
        const int trackIndex = trackAtY(pos.y());
        if (trackIndex < 0) return;
        // Bottom edge resizes the track.
        if (std::abs(pos.y() - (trackTop(trackIndex) + trackHeight(trackIndex))) <= 3) {
            m_drag = Drag::TrackResize;
            m_resizeTrack = trackIndex;
            m_resizeStartHeight = trackHeight(trackIndex);
            return;
        }
        const HeaderButton button = headerButtonAt(pos, trackIndex);
        if (button != HeaderButton::None) toggleTrackFlag(trackIndex, button);
        return;
    }

    // Zoom tool.
    if (m_tool == Tool::Zoom) {
        const Tick anchor = m_pressTick;
        const double ax = pos.x();
        m_pps = (event->modifiers() & Qt::AltModifier) ? std::max(0.5, m_pps / 1.6) : std::min(6000.0, m_pps * 1.6);
        updateScrollbars();
        horizontalScrollBar()->setValue(static_cast<int>(ticksToSec(anchor) * m_pps - (ax - headerWidth())));
        viewport()->update();
        return;
    }

    const Hit hit = hitTest(pos);
    const edit::Context ctx = m_document->editContext();
    const Qt::KeyboardModifiers mods = event->modifiers();

    // Transitions.
    if (hit.onTransition && m_tool == Tool::Select) {
        m_selectedTransitionClip = hit.clipId;
        m_selectedTransitionHead = hit.transitionHead;
        m_document->clearSelection();
        m_dragClipId = hit.clipId;
        m_dragTrack = hit.track;
        beginGesture(Drag::TransitionEdge, tr("Transition Duration"));
        viewport()->update();
        return;
    }
    m_selectedTransitionClip.clear();

    const Track* track = hit.track >= 0 ? seq->track(hit.track) : nullptr;

    if (hit.clipId.isEmpty() || !track) {
        if (m_tool == Tool::Select || m_tool == Tool::TrackSelect) {
            if (m_tool == Tool::TrackSelect) {
                // Select everything from here onward and allow dragging it.
                QStringList ids;
                for (int ti = 0; ti < seq->tracks.size(); ++ti) {
                    const Track& t = seq->tracks[ti];
                    if (t.locked || ((mods & Qt::ShiftModifier) && ti != hit.track)) continue;
                    for (const auto& c : t.clips)
                        if (c.end() > m_pressTick) ids << c.id;
                }
                m_document->setSelectionList(ids);
                return;
            }
            if (!(mods & (Qt::ShiftModifier | Qt::ControlModifier))) m_document->clearSelection();
            m_marqueeBase = m_document->selectedClips();
            m_marquee = QRect(pos, pos);
            m_drag = Drag::Marquee;
        }
        viewport()->update();
        return;
    }
    if (track->locked) return;

    const Clip& clip = track->clips[hit.clipIndex];
    m_dragClipId = clip.id;
    m_dragTrack = hit.track;

    switch (m_tool) {
    case Tool::Razor: {
        const Tick at = (mods & Qt::ShiftModifier) ? seq->rate.snap(m_pressTick) : snapTick(m_pressTick);
        const QStringList scope = (mods & Qt::ShiftModifier) ? QStringList() : selectionWithLinks(clip.id, mods & Qt::AltModifier);
        m_document->applyEdit(tr("Razor"), [&](Sequence& s) { return !edit::splitAt(s, at, scope, ctx).isEmpty(); });
        return;
    }
    case Tool::TrackSelect: {
        QStringList ids;
        for (int ti = 0; ti < seq->tracks.size(); ++ti) {
            const Track& t = seq->tracks[ti];
            if (t.locked || ((mods & Qt::ShiftModifier) && ti != hit.track)) continue;
            for (const auto& c : t.clips)
                if (c.end() > m_pressTick) ids << c.id;
        }
        m_document->setSelectionList(ids, clip.id);
        m_dragIds = ids;
        m_dragOrigin = clip.start;
        m_dragDuration = clip.duration;
        beginGesture(Drag::Move, tr("Move Clips"));
        return;
    }
    case Tool::Slip:
    case Tool::Slide:
        selectFromClick(hit, mods);
        m_dragIds = selectionWithLinks(clip.id, mods & Qt::AltModifier);
        beginGesture(m_tool == Tool::Slip ? Drag::Slip : Drag::Slide,
                     m_tool == Tool::Slip ? tr("Slip") : tr("Slide"));
        return;
    default:
        break;
    }

    if (hit.edge != Edge::None) {
        m_dragEdge = hit.edge;
        m_dragOrigin = hit.edge == Edge::Head ? clip.start : clip.end();
        if (m_tool == Tool::RateStretch) {
            beginGesture(Drag::Stretch, tr("Rate Stretch"));
        } else {
            m_trimMode = edit::TrimMode::Normal;
            if (m_tool == Tool::Ripple || (m_tool == Tool::Select && (mods & Qt::ControlModifier)))
                m_trimMode = edit::TrimMode::Ripple;
            if (m_tool == Tool::Roll) m_trimMode = edit::TrimMode::Roll;
            beginGesture(Drag::Trim, m_trimMode == edit::TrimMode::Ripple ? tr("Ripple Trim")
                                     : m_trimMode == edit::TrimMode::Roll ? tr("Roll Edit")
                                                                          : tr("Trim Clip"));
        }
        if (!m_document->isSelected(clip.id)) m_document->setSelectionList(selectionWithLinks(clip.id, true), clip.id);
        return;
    }

    if (hit.onGainLine && m_tool == Tool::Select) {
        m_gainStart = clip.gainDb;
        m_document->setSelectionList(selectionWithLinks(clip.id, true), clip.id);
        beginGesture(Drag::Gain, tr("Clip Gain"));
        return;
    }

    if (m_tool == Tool::Select || m_tool == Tool::Ripple || m_tool == Tool::Roll || m_tool == Tool::RateStretch) {
        selectFromClick(hit, mods);
        if (!m_document->isSelected(clip.id)) return;   // toggled off
        m_dragIds = targetedSelection();
        m_dragOrigin = clip.start;
        m_dragDuration = clip.duration;
        beginGesture(Drag::Move, tr("Move Clips"));
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    if ((pos - m_pressPos).manhattanLength() > 3) m_pressMoved = true;
    Sequence* seq = m_document->sequence();
    if (!seq) return;
    const edit::Context ctx = m_document->editContext();

    if (m_drag == Drag::None) {
        const Hit hit = hitTest(pos);
        const bool changed = hit.clipId != m_hover.clipId || hit.edge != m_hover.edge || hit.onGainLine != m_hover.onGainLine;
        m_hover = hit;
        Qt::CursorShape shape = Qt::ArrowCursor;
        if (pos.x() < headerWidth()) {
            const int ti = trackAtY(pos.y());
            if (ti >= 0 && std::abs(pos.y() - (trackTop(ti) + trackHeight(ti))) <= 3) shape = Qt::SizeVerCursor;
        } else if (m_tool == Tool::Razor) shape = Qt::CrossCursor;
        else if (m_tool == Tool::Hand) shape = Qt::OpenHandCursor;
        else if (m_tool == Tool::Zoom) shape = Qt::PointingHandCursor;
        else if (m_tool == Tool::Slip || m_tool == Tool::Slide) shape = hit.clipId.isEmpty() ? Qt::ArrowCursor : Qt::SizeHorCursor;
        else if (hit.edge != Edge::None) shape = Qt::SplitHCursor;
        else if (hit.onGainLine) shape = Qt::SizeVerCursor;
        viewport()->setCursor(shape);
        if (changed) viewport()->update();
        return;
    }

    switch (m_drag) {
    case Drag::Scrub:
        m_document->setPlayhead((event->modifiers() & Qt::ShiftModifier) ? snapTick(tickAtX(pos.x()))
                                                                          : seq->rate.snap(tickAtX(pos.x())));
        return;

    case Drag::Hand:
        horizontalScrollBar()->setValue(m_handScrollX - (pos.x() - m_handStart.x()));
        verticalScrollBar()->setValue(m_handScrollY - (pos.y() - m_handStart.y()));
        return;

    case Drag::TrackResize: {
        Track* t = seq->track(m_resizeTrack);
        if (!t) return;
        t->height = std::clamp(m_resizeStartHeight + (pos.y() - m_pressPos.y()), 26, 240);
        updateScrollbars();
        viewport()->update();
        return;
    }

    case Drag::Marquee: {
        m_marquee.setBottomRight(pos);
        const QRect r = m_marquee.normalized();
        QStringList ids = m_marqueeBase;
        for (int ti = 0; ti < seq->tracks.size(); ++ti) {
            const Track& t = seq->tracks[ti];
            if (t.locked) continue;
            for (const auto& c : t.clips) {
                if (clipRect(ti, c).intersects(r)) {
                    for (const QString& id : selectionWithLinks(c.id, event->modifiers() & Qt::AltModifier))
                        if (!ids.contains(id)) ids << id;
                }
            }
        }
        m_document->setSelectionList(ids);
        viewport()->update();
        return;
    }

    case Drag::Move: {
        if (!m_pressMoved) return;
        const QSet<QString> ignore(m_dragIds.begin(), m_dragIds.end());
        const Tick rawStart = std::max<Tick>(0, m_dragOrigin + (tickAtX(pos.x()) - m_pressTick));
        double sx1 = -1;
        double sx2 = -1;
        const Tick s1 = snapTick(rawStart, ignore, &sx1);
        const Tick s2 = snapTick(rawStart + m_dragDuration, ignore, &sx2) - m_dragDuration;
        Tick start = seq->rate.snap(rawStart);
        m_snapX = -1;
        if (sx1 >= 0 && (sx2 < 0 || std::llabs(s1 - rawStart) <= std::llabs(s2 - rawStart))) {
            start = s1;
            m_snapX = sx1;
        } else if (sx2 >= 0) {
            start = s2;
            m_snapX = sx2;
        }
        const Tick delta = start - m_dragOrigin;

        int laneDelta = 0;
        const int hovered = trackAtY(std::clamp(pos.y(), rulerHeight() + 1, viewport()->height() - 1));
        const Track* grabbed = m_dragBase.track(m_dragTrack);
        if (hovered >= 0 && grabbed && m_dragBase.track(hovered) && m_dragBase.tracks[hovered].kind == grabbed->kind)
            laneDelta = m_dragBase.laneOf(hovered) - m_dragBase.laneOf(m_dragTrack);

        const edit::PlaceMode mode = (event->modifiers() & Qt::ControlModifier) ? edit::PlaceMode::Insert
                                                                                 : edit::PlaceMode::Overwrite;
        const QStringList ids = m_dragIds;
        previewGesture([&](Sequence& s) {
            if (edit::moveClips(s, ids, delta, laneDelta, mode, ctx)) return true;
            return laneDelta != 0 && edit::moveClips(s, ids, delta, 0, mode, ctx);
        });
        emit statusMessage(tr("%1 %2%3")
                               .arg(mode == edit::PlaceMode::Insert ? tr("Insert move") : tr("Move"))
                               .arg(delta >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                               .arg(formatTimecode(std::llabs(delta), seq->rate)));
        return;
    }

    case Drag::Trim:
    case Drag::Stretch: {
        Tick edge = snapTick(tickAtX(pos.x()), {m_dragClipId}, &m_snapX);
        const QString id = m_dragClipId;
        const bool head = m_dragEdge == Edge::Head;
        if (m_drag == Drag::Stretch) {
            previewGesture([&](Sequence& s) { return edit::rateStretch(s, id, head, edge, ctx); });
            if (const Clip* c = seq->findClip(id))
                emit statusMessage(tr("Speed %1%").arg(c->speed * 100.0, 0, 'f', 1));
            return;
        }
        const Tick delta = edge - m_dragOrigin;
        const edit::TrimMode mode = m_trimMode;
        const bool linked = !(event->modifiers() & Qt::AltModifier);
        previewGesture([&](Sequence& s) { return edit::trim(s, id, head, delta, mode, ctx, linked); });
        if (const Clip* c = seq->findClip(id))
            emit statusMessage(tr("%1  %2  duration %3")
                                   .arg(m_dragText, (delta >= 0 ? QStringLiteral("+") : QStringLiteral("-"))
                                                        + formatTimecode(std::llabs(delta), seq->rate),
                                        formatTimecode(c->duration, seq->rate)));
        return;
    }

    case Drag::Slip:
    case Drag::Slide: {
        const Tick delta = seq->rate.snap(tickAtX(pos.x()) - m_pressTick);
        const QStringList ids = m_dragIds;
        const bool slipping = m_drag == Drag::Slip;
        previewGesture([&](Sequence& s) {
            return slipping ? edit::slip(s, ids, delta, ctx) : edit::slide(s, ids, delta, ctx);
        });
        if (const Clip* c = seq->findClip(m_dragClipId))
            emit statusMessage(tr("%1  source in %2").arg(m_dragText, formatTimecode(c->sourceIn, seq->rate)));
        return;
    }

    case Drag::Gain: {
        const Clip* base = m_dragBase.findClip(m_dragClipId);
        if (!base) return;
        const QRectF r = clipRect(m_dragTrack, *base);
        const double db = gainForY(r, pos.y());
        const QStringList ids = m_document->selectedClips();
        previewGesture([&](Sequence& s) {
            for (const QString& id : ids) {
                int ti = -1;
                if (Clip* c = s.findClip(id, &ti); c && s.tracks[ti].kind == TrackKind::Audio) c->gainDb = db;
            }
            return true;
        });
        emit statusMessage(tr("Clip gain %1 dB").arg(db, 0, 'f', 1));
        return;
    }

    case Drag::TransitionEdge: {
        if (!m_pressMoved) return;
        int ti = -1;
        const Clip* c = m_dragBase.findClip(m_dragClipId, &ti);
        if (!c) return;
        const Track& t = m_dragBase.tracks[ti];
        const int ci = t.indexOf(c->id);
        const Tick at = seq->rate.snap(tickAtX(pos.x()));
        Tick duration;
        if (m_selectedTransitionHead) {
            duration = crossHalf(t, ci) > 0 ? 2 * std::llabs(at - c->start) : at - c->start;
        } else {
            duration = c->end() - at;
        }
        duration = std::max(frameTicks(), duration);
        const QString id = c->id;
        const bool head = m_selectedTransitionHead;
        const TransitionType type = head ? c->inTransition.type : c->outTransition.type;
        previewGesture([&](Sequence& s) { return edit::setTransition(s, id, head, type, duration); });
        emit statusMessage(tr("Transition %1").arg(formatTimecode(duration, seq->rate)));
        return;
    }
    default:
        return;
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_drag == Drag::Hand) {
        m_drag = Drag::None;
        setTool(m_tool);
        return;
    }
    if (m_drag == Drag::Scrub || m_drag == Drag::Marquee || m_drag == Drag::TrackResize) {
        if (m_drag == Drag::TrackResize) m_document->markMetaDirty();
        m_drag = Drag::None;
        m_marquee = QRect();
        updateScrollbars();
        viewport()->update();
        return;
    }
    if (m_drag == Drag::None) return;

    const bool clickOnly = !m_pressMoved;
    endGesture(true);

    // A plain click on a clip inside a multi-selection selects just that clip.
    if (clickOnly && m_tool == Tool::Select && !(event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier))
        && !m_dragClipId.isEmpty() && m_document->selectedClips().size() > 1) {
        const QStringList group = selectionWithLinks(m_dragClipId, event->modifiers() & Qt::AltModifier);
        if (group.size() != m_document->selectedClips().size())
            m_document->setSelectionList(group, m_dragClipId);
    }

    m_dragIds.clear();
    updateScrollbars();
    viewport()->update();
}

void TimelineWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    const QPoint pos = event->position().toPoint();
    const Sequence* seq = m_document->sequence();
    if (!seq) return;

    if (pos.y() < rulerHeight() && pos.x() >= headerWidth()) {
        // Edit the marker under the cursor.
        for (int i = 0; i < seq->markers.size(); ++i) {
            if (std::abs(xForTick(seq->markers[i].time) - pos.x()) <= 6) {
                bool ok = false;
                const QString name = QInputDialog::getText(this, tr("Marker"), tr("Name:"), QLineEdit::Normal,
                                                           seq->markers[i].name, &ok);
                if (ok)
                    m_document->applyEdit(tr("Edit Marker"), [&](Sequence& s) {
                        s.markers[i].name = name;
                        return true;
                    });
                return;
            }
        }
        return;
    }

    const Hit hit = hitTest(pos);
    if (hit.onTransition) {
        editTransitionDuration();
        return;
    }
    if (hit.clipId.isEmpty()) return;
    const Clip* clip = seq->findClip(hit.clipId);
    if (clip && clip->generator == GeneratorKind::Title) emit titleEditRequested(hit.clipId);
    else emit clipActivated(hit.clipId);
}

void TimelineWidget::leaveEvent(QEvent* event)
{
    m_hover = Hit{};
    viewport()->update();
    QAbstractScrollArea::leaveEvent(event);
}

void TimelineWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y() != 0 ? event->angleDelta().y() : event->angleDelta().x();
    if (event->modifiers() & Qt::ControlModifier || event->modifiers() & Qt::AltModifier) {
        const Tick anchorTick = tickAtX(event->position().x());
        m_pps = std::clamp(m_pps * std::pow(1.0018, delta), 0.5, 6000.0);
        updateScrollbars();
        const int newValue = static_cast<int>(ticksToSec(anchorTick) * m_pps - (event->position().x() - headerWidth()));
        horizontalScrollBar()->setValue(std::max(0, newValue));
        viewport()->update();
        return;
    }
    if (event->modifiers() & Qt::ShiftModifier || event->angleDelta().x() != 0) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta);
        return;
    }
    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta);
}

void TimelineWidget::keyPressEvent(QKeyEvent* event)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick frame = seq->rate.ticksPerFrame();
    // Premiere numeric entry: type +12 or -5 then Enter to nudge the selected
    // clips (or move the playhead when nothing is selected).
    const QString typed = event->text();
    if (!m_numBuffer.isEmpty() && typed.size() == 1 && typed[0].isDigit()) {
        m_numBuffer += typed;
        emit statusMessage(tr("Move %1 frames - press Enter").arg(m_numBuffer));
        return;
    }
    if ((event->key() == Qt::Key_Plus || (event->key() == Qt::Key_Minus && (event->modifiers() & Qt::KeypadModifier)))
        && m_drag == Drag::None) {
        m_numBuffer = event->key() == Qt::Key_Plus ? QStringLiteral("+") : QStringLiteral("-");
        emit statusMessage(tr("Type frames, then Enter"));
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && m_numBuffer.size() > 1) {
        const int frames = m_numBuffer.toInt();
        m_numBuffer.clear();
        if (!targetedSelection().isEmpty()) nudge(frames);
        else m_document->setPlayhead(std::max<Tick>(0, m_document->playhead() + frame * frames));
        return;
    }
    if (event->key() != Qt::Key_Shift) m_numBuffer.clear();

    switch (event->key()) {
    case Qt::Key_Escape:
        if (m_drag != Drag::None) {
            endGesture(false);
            viewport()->update();
        } else {
            deselectAll();
        }
        return;
    case Qt::Key_Left:
        if (event->modifiers() & Qt::AltModifier) { nudge(event->modifiers() & Qt::ShiftModifier ? -5 : -1); return; }
        m_document->setPlayhead(m_document->playhead() - frame * (event->modifiers() & Qt::ShiftModifier ? 5 : 1));
        return;
    case Qt::Key_Right:
        if (event->modifiers() & Qt::AltModifier) { nudge(event->modifiers() & Qt::ShiftModifier ? 5 : 1); return; }
        m_document->setPlayhead(m_document->playhead() + frame * (event->modifiers() & Qt::ShiftModifier ? 5 : 1));
        return;
    case Qt::Key_Up:
        if (event->modifiers() & Qt::AltModifier) { nudgeLane(1); return; }
        goToEdit(false);
        return;
    case Qt::Key_Down:
        if (event->modifiers() & Qt::AltModifier) { nudgeLane(-1); return; }
        goToEdit(true);
        return;
    case Qt::Key_Home:
        m_document->setPlayhead(0);
        return;
    case Qt::Key_End:
        m_document->setPlayhead(seq->duration);
        return;
    case Qt::Key_Backspace:
        deleteSelected(event->modifiers() & Qt::ShiftModifier);
        return;
    default:
        break;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void TimelineWidget::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollbars();
}

// --------------------------------------------------------------- track header

void TimelineWidget::toggleTrackFlag(int trackIndex, HeaderButton button)
{
    Sequence* seq = m_document->sequence();
    Track* track = seq ? seq->track(trackIndex) : nullptr;
    if (!track) return;

    switch (button) {
    case HeaderButton::Mute: track->muted = !track->muted; break;
    case HeaderButton::Solo: track->solo = !track->solo; break;
    case HeaderButton::Lock: track->locked = !track->locked; break;
    case HeaderButton::Sync: track->syncLock = !track->syncLock; break;
    case HeaderButton::Target: track->targeted = !track->targeted; break;
    case HeaderButton::Patch: {
        int& patch = track->kind == TrackKind::Audio ? seq->patchAudio : seq->patchVideo;
        const int lane = seq->laneOf(trackIndex);
        patch = (patch == lane) ? 0 : lane;
        break;
    }
    default: return;
    }
    m_document->notifyChanged();
}

void TimelineWidget::toggleTrackMute(int trackIndex)
{
    toggleTrackFlag(trackIndex, HeaderButton::Mute);
}

void TimelineWidget::showHeaderMenu(int trackIndex, const QPoint& globalPos)
{
    Sequence* seq = m_document->sequence();
    if (!seq || !seq->track(trackIndex)) return;
    const Track& track = seq->tracks[trackIndex];

    QMenu menu(this);
    QAction* rename = menu.addAction(tr("Rename..."));
    menu.addSeparator();
    QAction* addV = menu.addAction(tr("Add Video Track"));
    QAction* addA = menu.addAction(tr("Add Audio Track"));
    QAction* del = menu.addAction(tr("Delete Track"));
    menu.addSeparator();
    QAction* small = menu.addAction(tr("Track Height: Small"));
    QAction* medium = menu.addAction(tr("Track Height: Medium"));
    menu.addAction(tr("Track Height: Large"));
    QAction* chosen = menu.exec(globalPos);
    if (!chosen) return;

    if (chosen == rename) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename Track"), tr("Name:"), QLineEdit::Normal, track.name, &ok);
        if (ok && !name.isEmpty())
            m_document->applyEdit(tr("Rename Track"), [&](Sequence& s) { s.tracks[trackIndex].name = name; return true; });
    } else if (chosen == addV || chosen == addA) {
        addTrack(chosen == addA);
    } else if (chosen == del) {
        if (!m_document->applyEdit(tr("Delete Track"), [&](Sequence& s) { return edit::removeTrack(s, trackIndex); }))
            emit statusMessage(tr("A sequence needs at least one video and one audio track"));
    } else {
        const int h = chosen == small ? 34 : chosen == medium ? 0 : 110;   // else: large
        seq->tracks[trackIndex].height = h;
        m_document->notifyChanged();
    }
}

// ------------------------------------------------------------------ context menu

void TimelineWidget::contextMenuEvent(QContextMenuEvent* event)
{
    const QPoint pos = event->pos();
    if (pos.x() < headerWidth() && pos.y() >= rulerHeight()) {
        const int ti = trackAtY(pos.y());
        if (ti >= 0) showHeaderMenu(ti, event->globalPos());
        return;
    }
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Hit hit = hitTest(pos);

    QMenu menu(this);
    if (hit.onTransition) {
        m_selectedTransitionClip = hit.clipId;
        m_selectedTransitionHead = hit.transitionHead;
        viewport()->update();
        QAction* duration = menu.addAction(tr("Set Transition Duration..."));
        QMenu* type = menu.addMenu(tr("Change Transition"));
        for (int t = 1; t <= static_cast<int>(TransitionType::Iris); ++t) {
            QAction* a = type->addAction(transitionName(static_cast<TransitionType>(t)));
            a->setData(t);
        }
        QAction* clear = menu.addAction(tr("Clear Transition"));
        QAction* chosen = menu.exec(event->globalPos());
        if (!chosen) return;
        const QString id = hit.clipId;
        const bool head = hit.transitionHead;
        if (chosen == duration) editTransitionDuration();
        else if (chosen == clear)
            m_document->applyEdit(tr("Clear Transition"), [&](Sequence& s) { return edit::setTransition(s, id, head, TransitionType::None, 0); });
        else if (chosen->data().isValid()) {
            const Clip* c = seq->findClip(id);
            const Tick d = c ? (head ? c->inTransition.duration : c->outTransition.duration) : defaultTransitionTicks();
            const auto t = static_cast<TransitionType>(chosen->data().toInt());
            m_document->applyEdit(tr("Change Transition"), [&](Sequence& s) { return edit::setTransition(s, id, head, t, d); });
        }
        return;
    }

    if (!hit.clipId.isEmpty() && !m_document->isSelected(hit.clipId))
        m_document->setSelectionList(selectionWithLinks(hit.clipId, false), hit.clipId);

    const bool haveSelection = !m_document->selectedClips().isEmpty();
    if (haveSelection) {
        menu.addAction(tr("Cut"), this, &TimelineWidget::cutSelection);
        menu.addAction(tr("Copy"), this, &TimelineWidget::copySelection);
    }
    QAction* pasteAction = menu.addAction(tr("Paste"), this, [this] { paste(false); });
    pasteAction->setEnabled(!m_document->clipboard().isEmpty());
    QAction* pasteInsert = menu.addAction(tr("Paste Insert"), this, [this] { paste(true); });
    pasteInsert->setEnabled(!m_document->clipboard().isEmpty());

    if (haveSelection) {
        menu.addSeparator();
        menu.addAction(tr("Clear"), this, [this] { deleteSelected(false); });
        menu.addAction(tr("Ripple Delete"), this, [this] { deleteSelected(true); });
        menu.addSeparator();
        const Clip* primary = seq->findClip(m_document->selectedClipId());
        QAction* enable = menu.addAction(tr("Enable"), this, &TimelineWidget::toggleEnableSelection);
        enable->setCheckable(true);
        enable->setChecked(primary && primary->enabled);
        menu.addAction(tr("Unlink"), this, &TimelineWidget::unlinkSelection);
        menu.addAction(tr("Link"), this, &TimelineWidget::linkSelection);
        menu.addAction(tr("Duplicate"), this, &TimelineWidget::duplicateSelection);
        menu.addSeparator();
        menu.addAction(tr("Speed/Duration..."), this, &TimelineWidget::speedDialog);
        menu.addAction(tr("Reverse"), this, &TimelineWidget::reverseSelection);
        menu.addAction(tr("Apply Default Transitions"), this, [this] {
            const QStringList sel = m_document->selectedClips();
            const Tick d = defaultTransitionTicks();
            m_document->applyEdit(tr("Apply Transitions"), [&](Sequence& s) {
                bool a = edit::applyDefaultTransition(s, 0, sel, TrackKind::Video, TransitionType::CrossDissolve, d);
                bool b = edit::applyDefaultTransition(s, 0, sel, TrackKind::Audio, TransitionType::CrossDissolve, d);
                return a || b;
            });
        });
        menu.addSeparator();
        menu.addAction(tr("Rename..."), this, &TimelineWidget::renameSelected);
        QMenu* labels = menu.addMenu(tr("Label"));
        static const char* names[] = {"Violet", "Iris", "Caribbean", "Forest", "Mango", "Tan", "Rose", "Magenta"};
        for (int i = 0; i < 8; ++i) {
            QPixmap px(12, 12);
            px.fill(theme::labelColor(i));
            labels->addAction(QIcon(px), tr(names[i]), this, [this, i] { setSelectedLabelColor(i); });
        }
        labels->addAction(tr("Automatic"), this, [this] { setSelectedLabelColor(-1); });
        if (primary && primary->generator == GeneratorKind::None) {
            menu.addSeparator();
            menu.addAction(tr("Reveal in Source Monitor (Match Frame)"), this, &TimelineWidget::matchFrame);
        }
        if (primary && primary->generator == GeneratorKind::Title) {
            menu.addAction(tr("Edit Title..."), this, [this, id = primary->id] { emit titleEditRequested(id); });
        }
    } else {
        const int ti = hit.track;
        if (ti >= 0) {
            menu.addSeparator();
            menu.addAction(tr("Ripple Delete Gap"), this, [this, ti, t = tickAtX(pos.x())] {
                m_document->applyEdit(tr("Ripple Delete Gap"), [&](Sequence& s) { return edit::closeGapAt(s, ti, t); });
            });
        }
    }
    menu.exec(event->globalPos());
}

// --------------------------------------------------------------- drag & drop

Tick TimelineWidget::dropTime(const QPoint& pos) const
{
    const Tick t = tickAtX(pos.x());
    return snapTick(t);
}

void TimelineWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(mediaMime()) || event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        m_dropActive = true;
        m_dropDuration = secToTicks(5.0);
        if (event->mimeData()->hasFormat(mediaMime())) {
            const QStringList ids = QString::fromUtf8(event->mimeData()->data(mediaMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            Tick total = 0;
            for (const QString& id : ids)
                if (const MediaRef* m = m_document->project().mediaById(id))
                    total += m->isStillImage() ? secToTicks(5.0) : m->duration;
            if (total > 0) m_dropDuration = total;
        }
    }
}

void TimelineWidget::dragMoveEvent(QDragMoveEvent* event)
{
    const QPoint pos = event->position().toPoint();
    m_dropTick = dropTime(pos);
    m_dropTrack = trackAtY(pos.y());
    event->acceptProposedAction();
    viewport()->update();
}

void TimelineWidget::dragLeaveEvent(QDragLeaveEvent* event)
{
    Q_UNUSED(event);
    m_dropActive = false;
    viewport()->update();
}

void TimelineWidget::dropEvent(QDropEvent* event)
{
    m_dropActive = false;
    const QPoint pos = event->position().toPoint();
    const Tick at = dropTime(pos);
    const int track = trackAtY(pos.y());
    const bool insert = event->modifiers() & Qt::ControlModifier;

    if (event->mimeData()->hasFormat(mediaMime())) {
        const QStringList ids = QString::fromUtf8(event->mimeData()->data(mediaMime())).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        emit mediaDropped(ids, at, track, insert);
    } else if (event->mimeData()->hasUrls()) {
        QStringList paths;
        for (const QUrl& url : event->mimeData()->urls())
            if (url.isLocalFile()) paths << url.toLocalFile();
        if (!paths.isEmpty()) emit filesDropped(paths, at, track, insert);
    }
    event->acceptProposedAction();
    viewport()->update();
}

// ------------------------------------------------------------------- commands

void TimelineWidget::splitAtPlayhead()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick at = m_document->playhead();
    const edit::Context ctx = m_document->editContext();
    QStringList scope = targetedSelection();

    // Without a selection, cut the targeted tracks (Premiere's Add Edit).
    if (scope.isEmpty()) {
        for (const auto& t : seq->tracks) {
            if (!t.targeted || t.locked) continue;
            if (const Clip* c = t.clipAt(at)) scope << c->id;
        }
    }
    if (scope.isEmpty()) return;
    m_document->applyEdit(tr("Add Edit"), [&](Sequence& s) { return !edit::splitAt(s, at, scope, ctx).isEmpty(); });
}

void TimelineWidget::splitAllAtPlayhead()
{
    const Tick at = m_document->playhead();
    const edit::Context ctx = m_document->editContext();
    m_document->applyEdit(tr("Add Edit to All Tracks"), [&](Sequence& s) { return !edit::splitAt(s, at, {}, ctx).isEmpty(); });
}

void TimelineWidget::deleteSelected(bool ripple)
{
    if (!m_selectedTransitionClip.isEmpty()) {
        const QString id = m_selectedTransitionClip;
        const bool head = m_selectedTransitionHead;
        m_selectedTransitionClip.clear();
        m_document->applyEdit(tr("Clear Transition"), [&](Sequence& s) { return edit::setTransition(s, id, head, TransitionType::None, 0); });
        return;
    }
    const QStringList ids = targetedSelection();
    if (ids.isEmpty()) {
        // Ripple delete with nothing selected closes the gap under the playhead on targeted tracks.
        if (ripple) closeGapAtPlayhead();
        return;
    }
    const Tick before = m_document->sequence() ? m_document->sequence()->duration : 0;
    Tick removed = 0;
    if (ripple && m_document->sequence())
        for (const QString& id : ids)
            if (const Clip* c = m_document->sequence()->findClip(id)) removed = std::max(removed, c->duration);
    m_document->applyEdit(ripple ? tr("Ripple Delete") : tr("Clear"),
                          [&](Sequence& s) { return edit::removeClips(s, ids, ripple); });
    m_document->clearSelection();
    if (ripple && m_document->sequence() && before - m_document->sequence()->duration < removed)
        emit statusMessage(tr("Ripple limited: clips on sync-locked tracks span the gap. "
                              "Turn off Sync Lock (chain icon) on those tracks to ripple only this track."));
}

void TimelineWidget::selectAll()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    QStringList ids;
    for (const auto& t : seq->tracks)
        if (!t.locked)
            for (const auto& c : t.clips) ids << c.id;
    m_document->setSelectionList(ids);
}

void TimelineWidget::deselectAll()
{
    m_selectedTransitionClip.clear();
    m_document->clearSelection();
    viewport()->update();
}

void TimelineWidget::copySelection()
{
    const Sequence* seq = m_document->sequence();
    if (!seq || m_document->selectedClips().isEmpty()) return;
    m_document->setClipboard(edit::copy(*seq, m_document->selectedClips()));
    emit statusMessage(tr("Copied %n clip(s)", nullptr, m_document->selectedClips().size()));
}

void TimelineWidget::cutSelection()
{
    copySelection();
    deleteSelected(false);
}

void TimelineWidget::paste(bool insert)
{
    const auto items = m_document->clipboard();
    if (items.isEmpty()) return;
    const Tick at = m_document->playhead();
    QStringList created;
    m_document->applyEdit(insert ? tr("Paste Insert") : tr("Paste"), [&](Sequence& s) {
        created = edit::paste(s, items, at, insert ? edit::PlaceMode::Insert : edit::PlaceMode::Overwrite);
        return !created.isEmpty();
    });
    if (!created.isEmpty()) {
        m_document->setSelectionList(created);
        Tick end = at;
        for (const auto& item : items) end = std::max(end, at + item.clip.end());
        m_document->setPlayhead(end);
    }
}

void TimelineWidget::duplicateSelection()
{
    const Sequence* seq = m_document->sequence();
    if (!seq || m_document->selectedClips().isEmpty()) return;
    const auto items = edit::copy(*seq, m_document->selectedClips());
    Tick end = 0;
    for (const QString& id : m_document->selectedClips())
        if (const Clip* c = seq->findClip(id)) end = std::max(end, c->end());
    QStringList created;
    m_document->applyEdit(tr("Duplicate"), [&](Sequence& s) {
        created = edit::paste(s, items, end, edit::PlaceMode::Overwrite);
        return !created.isEmpty();
    });
    if (!created.isEmpty()) m_document->setSelectionList(created);
}

void TimelineWidget::nudge(int frames)
{
    const QStringList ids = targetedSelection();
    if (ids.isEmpty()) return;
    const Tick delta = frameTicks() * frames;
    const edit::Context ctx = m_document->editContext();
    m_document->applyEdit(tr("Nudge"), [&](Sequence& s) {
        return edit::moveClips(s, ids, delta, 0, edit::PlaceMode::Overwrite, ctx);
    });
}

void TimelineWidget::nudgeLane(int lanes)
{
    const QStringList ids = targetedSelection();
    if (ids.isEmpty()) return;
    const edit::Context ctx = m_document->editContext();
    if (!m_document->applyEdit(tr("Nudge Track"), [&](Sequence& s) {
            return edit::moveClips(s, ids, 0, lanes, edit::PlaceMode::Overwrite, ctx);
        }))
        emit statusMessage(tr("No track to move to"));
}

void TimelineWidget::addDefaultTransition(bool audio)
{
    const Tick at = m_document->playhead();
    const QStringList sel = targetedSelection();
    const Tick d = defaultTransitionTicks();
    if (!m_document->applyEdit(audio ? tr("Apply Audio Transition") : tr("Apply Video Transition"), [&](Sequence& s) {
            return edit::applyDefaultTransition(s, at, sel, audio ? TrackKind::Audio : TrackKind::Video,
                                                TransitionType::CrossDissolve, d);
        }))
        emit statusMessage(tr("Target a track with clips (click V1/A1 in the track header)"));
}

void TimelineWidget::goToEdit(bool next)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const QVector<Tick> points = edit::editPoints(*seq, true);
    const Tick now = m_document->playhead();
    if (next) {
        for (Tick p : points)
            if (p > now) { m_document->setPlayhead(p); return; }
    } else {
        for (int i = points.size() - 1; i >= 0; --i)
            if (points[i] < now) { m_document->setPlayhead(points[i]); return; }
    }
}

void TimelineWidget::rippleTrimToPlayhead(bool headSide)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick now = m_document->playhead();
    Tick prev = 0;
    for (Tick p : edit::editPoints(*seq, true))
        if (p < now) prev = p;
    if (m_document->applyEdit(headSide ? tr("Ripple Trim Previous Edit to Playhead") : tr("Ripple Trim Next Edit to Playhead"),
                              [&](Sequence& s) { return edit::rippleTrimToTime(s, now, headSide); })
        && headSide)
        m_document->setPlayhead(prev);
}

void TimelineWidget::toggleEnableSelection()
{
    const Sequence* seq = m_document->sequence();
    const QStringList ids = targetedSelection();
    if (!seq || ids.isEmpty()) return;
    const Clip* primary = seq->findClip(m_document->selectedClipId());
    const bool enable = primary ? !primary->enabled : true;
    m_document->applyEdit(enable ? tr("Enable") : tr("Disable"), [&](Sequence& s) { return edit::setEnabled(s, ids, enable); });
}

void TimelineWidget::linkSelection()
{
    const QStringList ids = targetedSelection();
    m_document->applyEdit(tr("Link"), [&](Sequence& s) { return edit::link(s, ids); });
}

void TimelineWidget::unlinkSelection()
{
    const QStringList ids = targetedSelection();
    m_document->applyEdit(tr("Unlink"), [&](Sequence& s) { return edit::unlink(s, ids); });
}

void TimelineWidget::speedDialog()
{
    const Sequence* seq = m_document->sequence();
    const QStringList ids = targetedSelection();
    if (!seq || ids.isEmpty()) return;
    const Clip* primary = seq->findClip(m_document->selectedClipId());
    if (!primary) primary = seq->findClip(ids.first());
    if (!primary) return;

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Clip Speed / Duration"));
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* speed = new QDoubleSpinBox(&dialog);
    speed->setRange(5, 2000);
    speed->setDecimals(1);
    speed->setSuffix(QStringLiteral(" %"));
    speed->setValue(primary->speed * 100.0);
    form->addRow(tr("Speed"), speed);
    auto* duration = new QLabel(formatTimecode(primary->duration, seq->rate), &dialog);
    form->addRow(tr("Duration"), duration);
    auto* reverse = new QCheckBox(tr("Reverse Speed"), &dialog);
    reverse->setChecked(primary->reverse);
    form->addRow(QString(), reverse);
    auto* ripple = new QCheckBox(tr("Ripple Edit, Shifting Trailing Clips"), &dialog);
    ripple->setChecked(true);
    form->addRow(QString(), ripple);
    layout->addLayout(form);
    connect(speed, &QDoubleSpinBox::valueChanged, &dialog, [=](double v) {
        duration->setText(formatTimecode(static_cast<Tick>(primary->sourceSpan() / (v / 100.0)), seq->rate));
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    const double s = speed->value() / 100.0;
    const bool rev = reverse->isChecked();
    const bool rip = ripple->isChecked();
    const edit::Context ctx = m_document->editContext();
    m_document->applyEdit(tr("Speed/Duration"), [&](Sequence& sq) { return edit::setSpeed(sq, ids, s, rev, rip, ctx); });
}

void TimelineWidget::reverseSelection()
{
    const Sequence* seq = m_document->sequence();
    const QStringList ids = targetedSelection();
    if (!seq || ids.isEmpty()) return;
    const edit::Context ctx = m_document->editContext();
    m_document->applyEdit(tr("Reverse"), [&](Sequence& s) {
        bool changed = false;
        for (const QString& id : ids) {
            if (Clip* c = s.findClip(id)) {
                c->reverse = !c->reverse;
                changed = true;
            }
        }
        Q_UNUSED(ctx);
        return changed;
    });
}

void TimelineWidget::addMarker()
{
    const Tick at = m_document->playhead();
    m_document->applyEdit(tr("Add Marker"), [&](Sequence& s) {
        for (const auto& m : s.markers)
            if (m.time == at) return false;
        edit::addMarker(s, at);
        return true;
    });
}

void TimelineWidget::goToMarker(bool next)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick now = m_document->playhead();
    if (next) {
        for (const auto& m : seq->markers)
            if (m.time > now) { m_document->setPlayhead(m.time); return; }
    } else {
        for (int i = seq->markers.size() - 1; i >= 0; --i)
            if (seq->markers[i].time < now) { m_document->setPlayhead(seq->markers[i].time); return; }
    }
}

void TimelineWidget::clearMarkerAtPlayhead()
{
    const Tick at = m_document->playhead();
    const Tick tol = frameTicks() / 2;
    m_document->applyEdit(tr("Clear Marker"), [&](Sequence& s) { return edit::removeMarkerAt(s, at, tol); });
}

void TimelineWidget::markIn()
{
    const Tick at = m_document->playhead();
    m_document->applyEdit(tr("Mark In"), [&](Sequence& s) {
        s.inPoint = at;
        if (s.outPoint >= 0 && s.outPoint <= at) s.outPoint = -1;
        return true;
    });
}

void TimelineWidget::markOut()
{
    const Tick at = m_document->playhead() + frameTicks();
    m_document->applyEdit(tr("Mark Out"), [&](Sequence& s) {
        s.outPoint = at;
        if (s.inPoint >= at) s.inPoint = -1;
        return true;
    });
}

void TimelineWidget::markClip()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    Tick from = -1;
    Tick to = -1;
    for (const QString& id : m_document->selectedClips()) {
        if (const Clip* c = seq->findClip(id)) {
            from = from < 0 ? c->start : std::min(from, c->start);
            to = std::max(to, c->end());
        }
    }
    if (from < 0) {
        for (const auto& t : seq->tracks) {
            if (!t.targeted) continue;
            if (const Clip* c = t.clipAt(m_document->playhead())) {
                from = from < 0 ? c->start : std::min(from, c->start);
                to = std::max(to, c->end());
            }
        }
    }
    if (from < 0) return;
    m_document->applyEdit(tr("Mark Clip"), [&](Sequence& s) {
        s.inPoint = from;
        s.outPoint = to;
        return true;
    });
}

void TimelineWidget::clearInOut()
{
    m_document->applyEdit(tr("Clear In and Out"), [&](Sequence& s) {
        if (s.inPoint < 0 && s.outPoint < 0) return false;
        s.inPoint = s.outPoint = -1;
        return true;
    });
}

void TimelineWidget::liftInOut()
{
    const Sequence* seq = m_document->sequence();
    if (!seq || (seq->inPoint < 0 && seq->outPoint < 0)) {
        emit statusMessage(tr("Set In and Out points first (I / O)"));
        return;
    }
    const Tick from = std::max<Tick>(0, seq->inPoint);
    const Tick to = seq->outPoint >= 0 ? seq->outPoint : seq->duration;
    m_document->applyEdit(tr("Lift"), [&](Sequence& s) { return edit::liftRange(s, from, to); });
}

void TimelineWidget::extractInOut()
{
    const Sequence* seq = m_document->sequence();
    if (!seq || (seq->inPoint < 0 && seq->outPoint < 0)) {
        emit statusMessage(tr("Set In and Out points first (I / O)"));
        return;
    }
    const Tick from = std::max<Tick>(0, seq->inPoint);
    const Tick to = seq->outPoint >= 0 ? seq->outPoint : seq->duration;
    m_document->applyEdit(tr("Extract"), [&](Sequence& s) {
        if (!edit::extractRange(s, from, to)) return false;
        s.inPoint = s.outPoint = -1;
        return true;
    });
    m_document->setPlayhead(from);
}

void TimelineWidget::closeGapAtPlayhead()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick at = m_document->playhead();
    for (int ti = 0; ti < seq->tracks.size(); ++ti) {
        if (!seq->tracks[ti].targeted || seq->tracks[ti].locked || seq->tracks[ti].clipAt(at)) continue;
        if (m_document->applyEdit(tr("Ripple Delete Gap"), [&](Sequence& s) { return edit::closeGapAt(s, ti, at); }))
            return;
    }
}

void TimelineWidget::closeAllGaps()
{
    if (!m_document->applyEdit(tr("Close Gaps"), [](Sequence& s) { return edit::closeAllGaps(s); }))
        emit statusMessage(tr("No gaps to close"));
}

void TimelineWidget::matchFrame()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick at = m_document->playhead();
    const Clip* clip = seq->findClip(m_document->selectedClipId());
    if (!clip || !clip->contains(at)) {
        clip = nullptr;
        // Topmost targeted video track wins, as in Premiere.
        for (const auto& t : seq->tracks) {
            if (!t.targeted) continue;
            if (const Clip* c = t.clipAt(at); c && !c->isGenerator()) { clip = c; break; }
        }
    }
    if (!clip || clip->isGenerator()) {
        emit statusMessage(tr("No source clip under the playhead"));
        return;
    }
    emit matchFrameRequested(clip->mediaId, clip->sourceTime(std::clamp(at, clip->start, clip->end() - 1)));
}

void TimelineWidget::selectClipAtPlayhead()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    QStringList ids;
    for (const auto& t : seq->tracks) {
        if (!t.targeted || t.locked) continue;
        if (const Clip* c = t.clipAt(m_document->playhead())) ids << c->id;
    }
    m_document->setSelectionList(ids);
}

void TimelineWidget::addTrack(bool audio)
{
    m_document->applyEdit(audio ? tr("Add Audio Track") : tr("Add Video Track"), [&](Sequence& s) {
        edit::addTrack(s, audio ? TrackKind::Audio : TrackKind::Video);
        return true;
    });
}

void TimelineWidget::setSelectedLabelColor(int color)
{
    const QStringList ids = m_document->selectedClips();
    m_document->applyEdit(tr("Label"), [&](Sequence& s) {
        for (const QString& id : ids)
            if (Clip* c = s.findClip(id)) c->labelColor = color;
        return !ids.isEmpty();
    });
}

void TimelineWidget::renameSelected()
{
    const Sequence* seq = m_document->sequence();
    const QStringList ids = m_document->selectedClips();
    if (!seq || ids.isEmpty()) return;
    const Clip* primary = seq->findClip(m_document->selectedClipId());
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Clip"), tr("Clip name:"), QLineEdit::Normal,
                                               primary ? primary->label : QString(), &ok);
    if (!ok) return;
    m_document->applyEdit(tr("Rename"), [&](Sequence& s) {
        for (const QString& id : ids)
            if (Clip* c = s.findClip(id)) c->label = name;
        return true;
    });
}

void TimelineWidget::editTransitionDuration()
{
    const Sequence* seq = m_document->sequence();
    if (!seq || m_selectedTransitionClip.isEmpty()) return;
    const Clip* c = seq->findClip(m_selectedTransitionClip);
    if (!c) return;
    const bool head = m_selectedTransitionHead;
    const Transition current = head ? c->inTransition : c->outTransition;
    bool ok = false;
    const int frames = QInputDialog::getInt(this, tr("Transition Duration"), tr("Duration (frames):"),
                                            static_cast<int>(current.duration / frameTicks()), 1, 10000, 1, &ok);
    if (!ok) return;
    const QString id = c->id;
    const Tick d = frameTicks() * frames;
    m_document->applyEdit(tr("Transition Duration"), [&](Sequence& s) { return edit::setTransition(s, id, head, current.type, d); });
}

} // namespace bc
