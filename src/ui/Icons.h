#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

// Vector icon set drawn with QPainter (no fonts, no emoji, no image files).
// Modelled on Premiere Pro's thin line icons: grey at rest, white on hover,
// blue when a toggle is on.
namespace bc::icons {

enum class Id {
    // Tools
    Selection, TrackSelectForward, RippleEdit, RollingEdit, RateStretch, Razor, Slip, Slide,
    Hand, Zoom, Type,
    // Transport / monitor
    AddMarker, MarkIn, MarkOut, GoToIn, GoToOut, StepBack, StepForward, Play, Stop,
    Insert, Overwrite, Lift, Extract, ExportFrame, Loop, SafeMargins, Wrench, Plus, ShuttleBack,
    // Timeline
    Snap, LinkedSelection, Nest, Lock, Unlock, SyncLock, SyncLockOff, Eye, EyeOff, Keyframe,
    PrevKeyframe, NextKeyframe, Stopwatch, StopwatchOn, Reset, FxOn, FxOff, TwirlOpen, TwirlClosed,
    // Panels / app
    Hamburger, ListView, IconView, NewItem, NewBin, Search, Trash, Folder, Home, Import, Export,
    Undo, Redo, Proxy, Close, Maximize, Sequence, Film, Audio, Still, Title, Matte,
};

// Monochrome icon with Normal (grey), Active (white), Selected/On (blue), Disabled states.
QIcon icon(Id id);

// Single-colour pixmap for custom painting (timeline headers etc.).
QPixmap pixmap(Id id, int size, const QColor& color, qreal dpr = 2.0);

// Paint directly into a rect (crisp at any scale).
void paint(QPainter& painter, Id id, const QRectF& rect, const QColor& color);

// Legacy helpers used by older code paths.
QIcon select();
QIcon razor();
QIcon split();
QIcon remove();
QIcon undo();
QIcon redo();
QIcon zoomIn();
QIcon zoomOut();
QIcon fit();
QIcon play();
QIcon pause();
QIcon previous();
QIcon next();
QIcon import();
QIcon proxy();

} // namespace bc::icons
