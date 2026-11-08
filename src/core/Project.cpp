#include "core/Project.h"

#include <QUuid>

namespace bc {

QString transitionName(TransitionType type)
{
    switch (type) {
    case TransitionType::None: return QStringLiteral("None");
    case TransitionType::CrossDissolve: return QStringLiteral("Cross Dissolve");
    case TransitionType::DipToBlack: return QStringLiteral("Dip to Black");
    case TransitionType::DipToWhite: return QStringLiteral("Dip to White");
    case TransitionType::WipeLeft: return QStringLiteral("Wipe Left");
    case TransitionType::WipeRight: return QStringLiteral("Wipe Right");
    case TransitionType::PushLeft: return QStringLiteral("Push");
    case TransitionType::Iris: return QStringLiteral("Iris Round");
    }
    return QStringLiteral("Transition");
}

// ---------------------------------------------------------------------- Param

double Param::evaluate(Tick clipLocalTime) const
{
    if (keys.isEmpty()) return value;
    if (clipLocalTime <= keys.first().time) return keys.first().value;
    if (clipLocalTime >= keys.last().time) return keys.last().value;

    auto it = std::upper_bound(keys.begin(), keys.end(), clipLocalTime,
                               [](Tick t, const Keyframe& k) { return t < k.time; });
    const Keyframe& b = *it;
    const Keyframe& a = *(it - 1);
    const Tick span = b.time - a.time;
    if (span <= 0) return b.value;
    double f = static_cast<double>(clipLocalTime - a.time) / static_cast<double>(span);
    switch (a.interp) {
    case KeyInterp::Hold: return a.value;
    case KeyInterp::Ease: f = f * f * (3.0 - 2.0 * f); break;
    case KeyInterp::Linear: break;
    }
    return a.value + (b.value - a.value) * f;
}

void Param::setKey(Tick local, double v)
{
    for (auto& k : keys) {
        if (k.time == local) {
            k.value = v;
            return;
        }
    }
    keys.push_back({local, v, KeyInterp::Linear});
    std::sort(keys.begin(), keys.end(),
              [](const Keyframe& a, const Keyframe& b) { return a.time < b.time; });
}

bool Param::removeKey(Tick local)
{
    for (int i = 0; i < keys.size(); ++i) {
        if (keys[i].time == local) {
            keys.removeAt(i);
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------ EffectInstance

Param* EffectInstance::param(const QString& pid)
{
    for (auto& p : params)
        if (p.id == pid) return &p;
    return nullptr;
}

const Param* EffectInstance::param(const QString& pid) const
{
    for (const auto& p : params)
        if (p.id == pid) return &p;
    return nullptr;
}

// ----------------------------------------------------------------------- Clip

double Clip::opacityAt(Tick timeline) const
{
    for (const auto& e : effects) {
        if (!e.enabled || e.type != QLatin1String("opacity")) continue;
        if (const Param* p = e.param(QStringLiteral("amount")))
            return p->evaluate(timeline - start);
    }
    return 1.0;
}

Param* Clip::effectParam(const QString& effectType, const QString& paramId)
{
    for (auto& e : effects) {
        if (e.type != effectType) continue;
        if (Param* p = e.param(paramId)) return p;
    }
    return nullptr;
}

// ---------------------------------------------------------------------- Track

int Track::indexAt(Tick t) const
{
    // Upper bound on start, then step back one: clips are sorted and disjoint.
    auto it = std::upper_bound(clips.begin(), clips.end(), t,
                               [](Tick v, const Clip& c) { return v < c.start; });
    if (it == clips.begin()) return -1;
    --it;
    return it->contains(t) ? static_cast<int>(it - clips.begin()) : -1;
}

const Clip* Track::clipAt(Tick t) const
{
    const int i = indexAt(t);
    return i >= 0 ? &clips[i] : nullptr;
}

Clip* Track::clipAt(Tick t)
{
    const int i = indexAt(t);
    return i >= 0 ? &clips[i] : nullptr;
}

int Track::indexOf(const QString& clipId) const
{
    for (int i = 0; i < clips.size(); ++i)
        if (clips[i].id == clipId) return i;
    return -1;
}

void Track::sortClips()
{
    std::stable_sort(clips.begin(), clips.end(),
                     [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

// ------------------------------------------------------------------- Sequence

Track* Sequence::track(int index)
{
    return (index >= 0 && index < tracks.size()) ? &tracks[index] : nullptr;
}

const Track* Sequence::track(int index) const
{
    return (index >= 0 && index < tracks.size()) ? &tracks[index] : nullptr;
}

Track* Sequence::trackById(const QString& id)
{
    for (auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

int Sequence::trackIndexOf(const QString& trackId) const
{
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks[i].id == trackId) return i;
    return -1;
}

int Sequence::videoTrackCount() const
{
    int n = 0;
    for (const auto& t : tracks)
        if (t.kind == TrackKind::Video) ++n;
    return n;
}

int Sequence::audioTrackCount() const
{
    return tracks.size() - videoTrackCount();
}

int Sequence::laneOf(int trackIndex) const
{
    const Track* t = track(trackIndex);
    if (!t) return 0;
    if (t->kind == TrackKind::Video) {
        // Video tracks are stored top-first: V3, V2, V1.
        int lane = 0;
        for (int i = trackIndex; i < tracks.size(); ++i)
            if (tracks[i].kind == TrackKind::Video) ++lane;
        return lane;
    }
    int lane = 0;
    for (int i = 0; i <= trackIndex; ++i)
        if (tracks[i].kind == TrackKind::Audio) ++lane;
    return lane;
}

int Sequence::trackForLane(TrackKind kind, int lane) const
{
    if (lane <= 0) return -1;
    for (int i = 0; i < tracks.size(); ++i)
        if (tracks[i].kind == kind && laneOf(i) == lane) return i;
    return -1;
}

void Sequence::renameTracks()
{
    for (int i = 0; i < tracks.size(); ++i) {
        Track& t = tracks[i];
        const QString expectedPrefix = t.kind == TrackKind::Video ? QStringLiteral("V") : QStringLiteral("A");
        // Only rename tracks still carrying a generated name.
        bool generated = t.name.isEmpty();
        if (!generated && t.name.startsWith(expectedPrefix)) {
            bool ok = false;
            t.name.mid(1).toInt(&ok);
            generated = ok;
        }
        if (generated) t.name = expectedPrefix + QString::number(laneOf(i));
    }
}

void Sequence::recomputeDuration()
{
    Tick maxEnd = 0;
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            maxEnd = std::max(maxEnd, c.end());
    duration = maxEnd;
}

Clip* Sequence::findClip(const QString& clipId, int* trackIndexOut)
{
    for (int i = 0; i < tracks.size(); ++i) {
        const int ci = tracks[i].indexOf(clipId);
        if (ci >= 0) {
            if (trackIndexOut) *trackIndexOut = i;
            return &tracks[i].clips[ci];
        }
    }
    return nullptr;
}

const Clip* Sequence::findClip(const QString& clipId, int* trackIndexOut) const
{
    return const_cast<Sequence*>(this)->findClip(clipId, trackIndexOut);
}

QStringList Sequence::linkedClips(const QString& clipId) const
{
    QStringList result;
    const Clip* clip = findClip(clipId);
    if (!clip) return result;
    if (clip->linkId.isEmpty()) {
        result << clipId;
        return result;
    }
    for (const auto& t : tracks)
        for (const auto& c : t.clips)
            if (c.linkId == clip->linkId) result << c.id;
    return result;
}

QString Sequence::newClipId(const QString& prefix)
{
    return makeId(prefix);
}

// -------------------------------------------------------------------- Project

MediaRef* Project::mediaById(const QString& id)
{
    for (auto& m : media)
        if (m.id == id) return &m;
    return nullptr;
}

const MediaRef* Project::mediaById(const QString& id) const
{
    return const_cast<Project*>(this)->mediaById(id);
}

Sequence* Project::sequenceById(const QString& id)
{
    for (auto& s : sequences)
        if (s.id == id) return &s;
    return nullptr;
}

const Sequence* Project::sequenceById(const QString& id) const
{
    return const_cast<Project*>(this)->sequenceById(id);
}

Project Project::createDefault()
{
    Project p;
    p.id = makeId(QStringLiteral("proj"));
    p.sequences.push_back(makeDefaultSequence());
    return p;
}

// ------------------------------------------------------------------- builders

Sequence makeDefaultSequence(const QString& name, int width, int height, FrameRate rate)
{
    Sequence s;
    s.id = makeId(QStringLiteral("seq"));
    s.name = name;
    s.width = width;
    s.height = height;
    s.rate = rate;

    auto makeTrack = [](const QString& trackName, TrackKind kind) {
        Track t;
        t.id = makeId(kind == TrackKind::Video ? QStringLiteral("vt") : QStringLiteral("at"));
        t.name = trackName;
        t.kind = kind;
        return t;
    };

    s.tracks.push_back(makeTrack(QStringLiteral("V3"), TrackKind::Video));
    s.tracks.push_back(makeTrack(QStringLiteral("V2"), TrackKind::Video));
    s.tracks.push_back(makeTrack(QStringLiteral("V1"), TrackKind::Video));
    s.tracks.push_back(makeTrack(QStringLiteral("A1"), TrackKind::Audio));
    s.tracks.push_back(makeTrack(QStringLiteral("A2"), TrackKind::Audio));
    s.tracks.push_back(makeTrack(QStringLiteral("A3"), TrackKind::Audio));
    return s;
}

QString makeId(const QString& prefix)
{
    return prefix + QLatin1Char('_') + QUuid::createUuid().toString(QUuid::WithoutBraces).left(13);
}

Tick maxClipDuration(const Clip& clip, const MediaRef* media)
{
    constexpr Tick kUnbounded = kTicksPerSecond * 3600 * 24;
    if (clip.isGenerator() || !media || media->isStillImage()) return kUnbounded;
    const double remaining = static_cast<double>(media->duration - clip.sourceIn);
    if (remaining <= 0) return 0;
    return static_cast<Tick>(remaining / std::max(0.01, clip.speed));
}

} // namespace bc
