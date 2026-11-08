#pragma once

#include "core/EditOps.h"
#include "core/Project.h"

#include <QAbstractScrollArea>
#include <QHash>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QVector>

class QToolButton;

namespace bc {

class Document;
class WaveformCache;

class TimelineWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    enum class Tool { Select, TrackSelect, Ripple, Roll, RateStretch, Razor, Slip, Slide, Hand, Zoom };

    explicit TimelineWidget(Document* document, QWidget* parent = nullptr);

    void setTool(Tool tool);
    Tool tool() const { return m_tool; }
    static QString toolName(Tool tool);

    bool snapping() const { return m_snapping; }
    void setSnapping(bool on);
    bool linkedSelection() const { return m_linkedSelection; }
    void setLinkedSelection(bool on);
    void setShowThumbnails(bool on) { m_showThumbs = on; viewport()->update(); }

    Tick defaultTransitionTicks() const;
    void setDefaultTransitionFrames(int frames) { m_defaultTransitionFrames = frames; }

public slots:
    void zoomIn();
    void zoomOut();
    void zoomToFit();

    void splitAtPlayhead();          // selected clips, else targeted tracks
    void splitAllAtPlayhead();       // Add Edit to All Tracks
    void deleteSelected(bool ripple);
    void selectAll();
    void deselectAll();
    void copySelection();
    void cutSelection();
    void paste(bool insert);
    void duplicateSelection();
    void nudge(int frames);
    void nudgeLane(int lanes);
    void addDefaultTransition(bool audio);
    void goToEdit(bool next);
    void rippleTrimToPlayhead(bool headSide);   // Q / W
    void toggleEnableSelection();
    void linkSelection();
    void unlinkSelection();
    void speedDialog();
    void reverseSelection();
    void addMarker();
    void goToMarker(bool next);
    void clearMarkerAtPlayhead();
    void markIn();
    void markOut();
    void markClip();
    void clearInOut();
    void liftInOut();
    void extractInOut();
    void closeGapAtPlayhead();
    void closeAllGaps();
    void matchFrame();
    void selectClipAtPlayhead();
    void addTrack(bool audio);
    void toggleTrackMute(int trackIndex);
    void setSelectedLabelColor(int color);
    void renameSelected();
    void editTransitionDuration();
    void nestSelection();

signals:
    void scrubStarted();
    void statusMessage(const QString& message);
    void toolChanged(bc::TimelineWidget::Tool tool);
    void matchFrameRequested(const QString& mediaId, bc::Tick sourceTime);
    void mediaDropped(const QStringList& mediaIds, bc::Tick at, int trackIndex, bool insert);
    void filesDropped(const QStringList& paths, bc::Tick at, int trackIndex, bool insert);
    void clipActivated(const QString& clipId);   // double click
    void titleEditRequested(const QString& clipId);
    void openSequenceRequested(const QString& sequenceId);
    void snappingToggled(bool on);
    void linkedSelectionToggled(bool on);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    enum class Drag {
        None, Scrub, Marquee, Move, Trim, Slip, Slide, Stretch, Hand, Gain, TrackResize, TransitionEdge
    };
    enum class HeaderButton { None, Patch, Target, Sync, Mute, Solo, Lock };
    enum class Edge { None, Head, Tail };

    struct Hit {
        int track = -1;
        int clipIndex = -1;
        QString clipId;
        Edge edge = Edge::None;
        bool onGainLine = false;
        bool onTransition = false;
        bool transitionHead = true;   // the transition belongs to this clip's head
    };

    int headerWidth() const { return 168; }
    int rulerHeight() const { return 44; }
    int trackHeight(int index) const;
    int trackTop(int index) const;
    int trackAtY(int y) const;
    int totalTracksHeight() const;

    double xForTick(Tick tick) const;
    Tick tickAtX(double x) const;
    Tick frameTicks() const;
    Tick snapTick(Tick tick, const QSet<QString>& ignore = {}, double* snappedX = nullptr) const;
    QVector<Tick> snapPoints(const QSet<QString>& ignore) const;

    Hit hitTest(const QPoint& pos) const;
    QRectF clipRect(int trackIndex, const Clip& clip) const;
    double gainLineY(const QRectF& rect, double gainDb) const;
    double gainForY(const QRectF& rect, double y) const;

    void updateScrollbars();
    void paintRuler(QPainter& painter);
    void paintTrackHeaders(QPainter& painter);
    void paintTracks(QPainter& painter);
    void paintClip(QPainter& painter, int trackIndex, int clipIndex, const Clip& clip, bool selected);
    void paintTransitions(QPainter& painter, int trackIndex);
    void paintPlayhead(QPainter& painter);
    void paintOverlays(QPainter& painter);

    QString mediaLabel(const QString& mediaId) const;
    QImage thumbnailFor(const QString& mediaId);

    QRect headerButtonRect(int trackIndex, HeaderButton button) const;
    HeaderButton headerButtonAt(const QPoint& pos, int trackIndex) const;
    void toggleTrackFlag(int trackIndex, HeaderButton button);
    void showHeaderMenu(int trackIndex, const QPoint& globalPos);

    void ensurePlayheadVisible();
    void selectFromClick(const Hit& hit, Qt::KeyboardModifiers mods);
    QStringList selectionWithLinks(const QString& clipId, bool ignoreLinks) const;
    QStringList targetedSelection() const;

    // Live gestures: the document's sequence is replaced with a preview built
    // from the gesture's starting state; release commits it as one undo step.
    void beginGesture(Drag drag, const QString& text);
    void previewGesture(const std::function<bool(Sequence&)>& fn);
    void endGesture(bool commit);

    Tick dropTime(const QPoint& pos) const;

    Document* m_document;
    WaveformCache* m_waveforms = nullptr;
    Tool m_tool = Tool::Select;
    bool m_snapping = true;
    bool m_linkedSelection = true;
    int m_defaultTransitionFrames = 25;

    double m_pps = 48.0;          // pixels per second at current zoom

    // Gesture state.
    Drag m_drag = Drag::None;
    QString m_dragText;
    Sequence m_dragBase;
    bool m_dragChanged = false;
    QPoint m_pressPos;
    Tick m_pressTick = 0;
    bool m_pressMoved = false;
    QString m_dragClipId;
    int m_dragTrack = -1;
    Edge m_dragEdge = Edge::None;
    edit::TrimMode m_trimMode = edit::TrimMode::Normal;
    Tick m_dragOrigin = 0;         // grabbed edge / clip start at press
    Tick m_dragDuration = 0;
    QStringList m_dragIds;
    double m_snapX = -1;           // snap indicator
    QRect m_marquee;
    QStringList m_marqueeBase;
    int m_resizeTrack = -1;
    int m_resizeStartHeight = 0;
    double m_gainStart = 0;
    QPoint m_handStart;
    int m_handScrollX = 0;
    int m_handScrollY = 0;

    // Hover / misc.
    Hit m_hover;
    QString m_selectedTransitionClip;   // clip whose head transition is selected
    bool m_selectedTransitionHead = true;

    // Drag & drop preview.
    bool m_dropActive = false;
    Tick m_dropTick = 0;
    int m_dropTrack = -1;
    Tick m_dropDuration = 0;

    bool m_showThumbs = true;
    bool m_showWaves = true;
    QString m_numBuffer;            // Premiere numeric entry: +12 Enter nudges
    double m_lastPlayheadX = -1;
    QRect m_paintRect;
    QToolButton* m_snapButton = nullptr;
    QToolButton* m_linkButton = nullptr;
    QWidget* m_headerTools = nullptr;
};

} // namespace bc

Q_DECLARE_METATYPE(bc::TimelineWidget::Tool)
