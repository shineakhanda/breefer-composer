#pragma once

#include "core/Types.h"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QVector>
#include <cmath>
#include <algorithm>
#include <optional>

namespace bc {

enum class TrackKind { Video, Audio };

enum class KeyInterp { Linear = 0, Hold = 1, Ease = 2 };

struct Keyframe {
    Tick time = 0;      // relative to clip start
    double value = 0.0;
    KeyInterp interp = KeyInterp::Linear;   // interpolation towards the next key
};

struct Param {
    QString id;
    QString name;
    double value = 1.0;
    double minValue = 0.0;
    double maxValue = 1.0;
    QVector<Keyframe> keys;   // sorted by time

    double evaluate(Tick clipLocalTime) const;
    void setKey(Tick local, double v);
    bool removeKey(Tick local);
};

struct EffectInstance {
    QString id;
    QString type;        // "motion", "opacity", "lumetri", "blur", "sharpen", "mask", ...
    QString name;
    bool enabled = true;
    QVector<Param> params;

    Param* param(const QString& pid);
    const Param* param(const QString& pid) const;
};

struct MediaRef {
    QString id;
    QString path;
    QString name;
    Tick duration = 0;
    int width = 0;
    int height = 0;
    FrameRate rate{25, 1};
    int sampleRate = 48000;
    int channels = 2;
    bool hasVideo = false;
    bool hasAudio = false;
    QString codec;
    QString proxyPath;   // empty until a proxy exists
    QString bin;         // Project panel folder ("" = root, "A/B" nested)
    // Remotion / React compositions rendered to this file (re-renderable).
    QString remotionEntry;        // project dir or bundle
    QString remotionComposition;
    QString remotionProps;        // JSON
    bool offline = false;  // file missing on disk at load time

    bool isStillImage() const { return duration <= 0; }
};

// Transitions sit on clip edges.
//  - A clip's head transition whose previous neighbour ends exactly at this clip's
//    start is a two-sided transition centred on the cut (cross dissolve, wipe, ...).
//  - Otherwise it is a one-sided fade from/to nothing.
enum class TransitionType {
    None = 0,
    CrossDissolve,
    DipToBlack,
    DipToWhite,
    WipeLeft,
    WipeRight,
    PushLeft,
    Iris,
};

struct Transition {
    TransitionType type = TransitionType::None;
    Tick duration = 0;
    bool active() const { return type != TransitionType::None && duration > 0; }
};

QString transitionName(TransitionType type);

// Synthetic media produced by the clip itself instead of a file.
enum class GeneratorKind {
    None = 0,
    Title,
    ColorMatte,
    Nest,             // a whole sequence used as a clip (nestedSequenceId)
    Adjustment,       // applies its effects to everything below it
    Model3D,          // OBJ model rendered with the "model3d" effect parameters
};

struct TitleSpec {
    QString text = QStringLiteral("Title");
    QString fontFamily = QStringLiteral("Segoe UI");
    double fontSize = 96.0;        // in sequence pixels
    bool bold = true;
    bool italic = false;
    QColor color = Qt::white;
    int align = 1;                 // 0 left, 1 centre, 2 right
    double posX = 0.5;             // 0..1 of frame width (anchor)
    double posY = 0.82;            // 0..1 of frame height (anchor)
    bool background = false;       // box behind the text
    QColor backgroundColor = QColor(0, 0, 0, 160);
    bool shadow = true;
    double outline = 0.0;          // stroke width in sequence pixels
    QColor outlineColor = Qt::black;
};

struct Clip {
    QString id;
    QString mediaId;       // empty for generators
    Tick start = 0;        // timeline position
    Tick duration = 0;     // timeline duration
    Tick sourceIn = 0;     // position inside the media
    double speed = 1.0;    // > 0; 2.0 = twice as fast
    bool reverse = false;
    bool muted = false;    // legacy audio mute flag
    bool enabled = true;   // disabled clips are neither seen nor heard
    QString label;
    int labelColor = -1;   // -1 = automatic
    QString linkId;        // clips sharing a non-empty linkId move/trim together
    double gainDb = 0.0;   // audio clip gain

    GeneratorKind generator = GeneratorKind::None;
    TitleSpec title;
    QColor matteColor = QColor(0x20, 0x40, 0x80);
    QString nestedSequenceId;   // GeneratorKind::Nest
    QString modelPath;          // GeneratorKind::Model3D (.obj)

    Transition inTransition;
    Transition outTransition;

    QVector<EffectInstance> effects;

    Tick end() const { return start + duration; }
    bool contains(Tick t) const { return t >= start && t < end(); }
    bool isGenerator() const { return generator != GeneratorKind::None; }

    // Length of source material consumed by the clip.
    Tick sourceSpan() const { return static_cast<Tick>(static_cast<double>(duration) * speed); }

    // Map a timeline tick to a source tick inside the media.
    Tick sourceTime(Tick timeline) const
    {
        const double local = static_cast<double>(timeline - start);
        if (reverse) {
            const double fromEnd = static_cast<double>(duration) - local;
            return sourceIn + static_cast<Tick>(std::max(0.0, fromEnd - 1.0) * speed);
        }
        return sourceIn + static_cast<Tick>(local * speed);
    }

    double opacityAt(Tick timeline) const;
    Param* effectParam(const QString& effectType, const QString& paramId);
};

struct Track {
    QString id;
    QString name;
    TrackKind kind = TrackKind::Video;
    bool muted = false;
    bool solo = false;
    bool locked = false;
    bool syncLock = true;     // participates in ripple edits made on other tracks
    bool targeted = true;     // receives edits/navigation (Premiere track targeting)
    double volumeDb = 0.0;    // audio track fader
    double pan = 0.0;         // -1 left .. +1 right
    int height = 0;           // 0 = default
    QVector<Clip> clips;      // invariant: sorted by start, non-overlapping

    // Binary search: O(log n). Relies on the non-overlap invariant.
    const Clip* clipAt(Tick t) const;
    Clip* clipAt(Tick t);
    int indexAt(Tick t) const;
    int indexOf(const QString& clipId) const;
    void sortClips();
};

struct Marker {
    QString id;
    Tick time = 0;
    Tick duration = 0;
    QString name;
    QString comment;
    int color = 3;   // index into theme::labelColor
};

struct Sequence {
    QString id;
    QString name = QStringLiteral("Sequence 01");
    int width = 1920;
    int height = 1080;
    FrameRate rate{25, 1};
    Tick duration = 0;
    int patchVideo = 1;    // Source patching: lane receiving source video (0 = off)
    int patchAudio = 1;    // ... and source audio
    Tick inPoint = -1;     // -1 = unset
    Tick outPoint = -1;
    QVector<Track> tracks;
    QVector<Marker> markers;   // sorted by time

    Track* track(int index);
    const Track* track(int index) const;
    Track* trackById(const QString& id);
    int trackIndexOf(const QString& trackId) const;

    // Premiere numbering: V1 is the video track closest to the audio tracks.
    int videoTrackCount() const;
    int audioTrackCount() const;
    int laneOf(int trackIndex) const;                 // 1-based within its kind
    int trackForLane(TrackKind kind, int lane) const; // -1 if none
    void renameTracks();

    void recomputeDuration();
    Clip* findClip(const QString& clipId, int* trackIndexOut = nullptr);
    const Clip* findClip(const QString& clipId, int* trackIndexOut = nullptr) const;
    QStringList linkedClips(const QString& clipId) const;   // includes clipId itself

    bool hasRange() const { return inPoint >= 0 && outPoint > inPoint; }

    QString newClipId(const QString& prefix = QStringLiteral("clip"));
};

struct Project {
    QString id;
    QString name = QStringLiteral("Untitled Project");
    QString filePath;
    QStringList bins;              // folder paths in the Project panel
    QVector<MediaRef> media;
    QVector<Sequence> sequences;

    MediaRef* mediaById(const QString& id);
    const MediaRef* mediaById(const QString& id) const;
    Sequence* sequenceById(const QString& id);
    const Sequence* sequenceById(const QString& id) const;

    static Project createDefault();
};

// Sensible starting timeline: 3 video tracks + 3 audio tracks.
Sequence makeDefaultSequence(const QString& name = QStringLiteral("Sequence 01"),
                             int width = 1920, int height = 1080,
                             FrameRate rate = FrameRate{25, 1});

QString makeId(const QString& prefix);

// Longest timeline duration the clip may reach from its current sourceIn
// (unbounded for stills and generators).
Tick maxClipDuration(const Clip& clip, const MediaRef* media);

inline double dbToGain(double db) { return db <= -96.0 ? 0.0 : std::pow(10.0, db / 20.0); }

} // namespace bc
