#pragma once

#include "core/Project.h"

#include <QHash>
#include <QSet>
#include <QStringList>

// Headless, UI-free editing operations on a Sequence value. Every operation is
// all-or-nothing on the sequence it is given; the Document wraps each call in a
// single undoable snapshot command. Locked tracks are never modified.
namespace bc::edit {

struct Context {
    const QVector<MediaRef>* media = nullptr;
    Tick frame = 0;   // ticks per sequence frame (minimum clip duration)

    const MediaRef* find(const QString& mediaId) const;
    static Context forSequence(const Sequence& seq, const QVector<MediaRef>& media);
};

enum class TrimMode { Normal, Ripple, Roll };
enum class PlaceMode { Overwrite, Insert };

struct ClipboardItem {
    TrackKind kind = TrackKind::Video;
    int lane = 1;          // V1/A1 = 1
    Clip clip;             // start relative to the earliest copied clip
};

struct Placement {
    int trackIndex = -1;
    Clip clip;
};

// ---- queries --------------------------------------------------------------

// Adds every clip linked to any id in `ids`.
QStringList expandLinks(const Sequence& seq, const QStringList& ids);

// How far right/left a head or tail edge may move. Positive delta moves the
// edge towards later time.
void headLimits(const Sequence& seq, int trackIndex, int clipIndex, const Context& ctx,
                Tick& minDelta, Tick& maxDelta, bool ignoreNeighbours = false);
void tailLimits(const Sequence& seq, int trackIndex, int clipIndex, const Context& ctx,
                Tick& minDelta, Tick& maxDelta, bool ignoreNeighbours = false);

// Sorted, de-duplicated clip boundaries (0 included) on targeted tracks.
QVector<Tick> editPoints(const Sequence& seq, bool targetedOnly);

// Largest amount clips starting at or after `at` can move left on all `tracks`
// without overlapping anything.
Tick maxLeftShift(const Sequence& seq, Tick at, const QSet<int>& tracks);

// Tracks a ripple originating on `origin` tracks drags along (unlocked + sync lock).
QSet<int> rippleTracks(const Sequence& seq, const QSet<int>& origin);

// ---- primitive helpers ----------------------------------------------------

// Removes, trims or splits everything on the track overlapping [from, to).
void clearRange(Sequence& seq, int trackIndex, Tick from, Tick to, QHash<QString, QString>& linkMap);

// Moves every clip on `tracks` whose start >= at by delta (no safety checks).
void shiftFrom(Sequence& seq, Tick at, Tick delta, const QSet<int>& tracks);

void shiftKeyframes(Clip& clip, Tick delta);

// ---- edits (return true when the sequence changed) -------------------------

QStringList splitAt(Sequence& seq, Tick time, const QStringList& onlyClips, const Context& ctx);

bool removeClips(Sequence& seq, const QStringList& ids, bool ripple);

bool closeGapAt(Sequence& seq, int trackIndex, Tick time);
bool closeAllGaps(Sequence& seq);

bool moveClips(Sequence& seq, const QStringList& ids, Tick delta, int laneDelta,
               PlaceMode mode, const Context& ctx);

bool trim(Sequence& seq, const QString& clipId, bool head, Tick delta, TrimMode mode,
          const Context& ctx, bool includeLinked = true);

bool slip(Sequence& seq, const QStringList& ids, Tick delta, const Context& ctx);
bool slide(Sequence& seq, const QStringList& ids, Tick delta, const Context& ctx);

bool rateStretch(Sequence& seq, const QString& clipId, bool head, Tick newEdge, const Context& ctx);

bool setSpeed(Sequence& seq, const QStringList& ids, double speed, bool reverse, bool ripple,
              const Context& ctx);

bool place(Sequence& seq, QVector<Placement> placements, PlaceMode mode);

bool liftRange(Sequence& seq, Tick from, Tick to);
bool extractRange(Sequence& seq, Tick from, Tick to);

bool rippleTrimToTime(Sequence& seq, Tick time, bool trimHeadSide);

bool setTransition(Sequence& seq, const QString& clipId, bool head, TransitionType type, Tick duration);
bool applyDefaultTransition(Sequence& seq, Tick time, const QStringList& selection,
                            TrackKind kind, TransitionType type, Tick duration);

QVector<ClipboardItem> copy(const Sequence& seq, const QStringList& ids);
QStringList paste(Sequence& seq, const QVector<ClipboardItem>& items, Tick at, PlaceMode mode);

bool setEnabled(Sequence& seq, const QStringList& ids, bool enabled);
bool link(Sequence& seq, const QStringList& ids);
bool unlink(Sequence& seq, const QStringList& ids);

int addTrack(Sequence& seq, TrackKind kind);
bool removeTrack(Sequence& seq, int trackIndex);

// Moves the clips into `nestedOut` (a new sequence with the same settings and
// lanes) and replaces them with linked Nest clips. Returns the new clip ids.
QStringList nest(Sequence& seq, const QStringList& ids, Sequence& nestedOut, const QString& name);

QString addMarker(Sequence& seq, Tick time, const QString& name = QString());
bool removeMarkerAt(Sequence& seq, Tick time, Tick tolerance);

// Checks the sorted/non-overlapping invariant on every track (used by tests and
// as a debug guard in the Document).
bool validate(const Sequence& seq, QString* problem = nullptr);

} // namespace bc::edit
