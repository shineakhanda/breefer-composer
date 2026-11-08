#include "core/Evaluate.h"

#include <cmath>

namespace bc {

namespace { constexpr double kHalfPi = 1.57079632679489661923; }

bool hasCrossHead(const Track& track, int index)
{
    if (index <= 0 || index >= track.clips.size()) return false;
    const Clip& clip = track.clips[index];
    if (!clip.inTransition.active()) return false;
    const Clip& prev = track.clips[index - 1];
    return prev.end() == clip.start && prev.enabled && clip.enabled;
}

Tick crossHalf(const Track& track, int index)
{
    if (!hasCrossHead(track, index)) return 0;
    const Clip& clip = track.clips[index];
    // A transition cannot be longer than either side of the cut.
    const Tick limit = std::min(track.clips[index - 1].duration, clip.duration);
    return std::min(clip.inTransition.duration, limit) / 2;
}

void audibleRange(const Track& track, int index, Tick& from, Tick& to)
{
    const Clip& clip = track.clips[index];
    from = clip.start - crossHalf(track, index);
    to = clip.end();
    if (index + 1 < track.clips.size()) to += crossHalf(track, index + 1);
}

namespace {

double fadeIn(const Clip& clip, Tick time)
{
    if (!clip.inTransition.active()) return 1.0;
    const Tick d = std::min(clip.inTransition.duration, clip.duration);
    if (d <= 0) return 1.0;
    const Tick local = time - clip.start;
    if (local >= d) return 1.0;
    return std::clamp(static_cast<double>(local) / static_cast<double>(d), 0.0, 1.0);
}

double fadeOut(const Clip& clip, Tick time)
{
    if (!clip.outTransition.active()) return 1.0;
    const Tick d = std::min(clip.outTransition.duration, clip.duration);
    if (d <= 0) return 1.0;
    const Tick remaining = clip.end() - time;
    if (remaining >= d) return 1.0;
    return std::clamp(static_cast<double>(remaining) / static_cast<double>(d), 0.0, 1.0);
}

} // namespace

TrackEval evaluateTrack(const Track& track, Tick time)
{
    TrackEval result;
    const int n = track.clips.size();
    if (n == 0) return result;

    // Locate the clip under the time (or the nearest clip after it).
    auto it = std::upper_bound(track.clips.begin(), track.clips.end(), time,
                               [](Tick v, const Clip& c) { return v < c.start; });
    const int after = static_cast<int>(it - track.clips.begin());   // first clip with start > time

    // A two-sided transition can be centred on the cut before `after` (the clip
    // under the playhead started at or before time) or on the start of `after`.
    for (int candidate : {after, after - 1}) {
        if (candidate <= 0 || candidate >= n) continue;
        const Tick half = crossHalf(track, candidate);
        if (half <= 0) continue;
        const Clip& in = track.clips[candidate];
        const Tick from = in.start - half;
        const Tick to = in.start + half;
        if (time >= from && time < to) {
            result.outgoing = &track.clips[candidate - 1];
            result.incoming = &in;
            result.progress = static_cast<double>(time - from) / static_cast<double>(to - from);
            result.type = in.inTransition.type;
            return result;
        }
    }

    const int index = after - 1;
    if (index < 0) return result;
    const Clip& clip = track.clips[index];
    if (!clip.contains(time) || !clip.enabled) return result;

    result.clip = &clip;
    double fade = 1.0;
    if (!hasCrossHead(track, index)) {
        const double f = fadeIn(clip, time);
        if (f < 1.0) result.fadeType = clip.inTransition.type;
        fade = std::min(fade, f);
    }
    const bool nextCross = index + 1 < n && hasCrossHead(track, index + 1);
    if (!nextCross) {
        const double f = fadeOut(clip, time);
        if (f < 1.0) result.fadeType = clip.outTransition.type;
        fade = std::min(fade, f);
    }
    result.fade = fade;
    return result;
}

double clipAudioGain(const Track& track, int index, Tick time)
{
    const Clip& clip = track.clips[index];
    if (!clip.enabled || clip.muted) return 0.0;

    double gain = dbToGain(clip.gainDb);

    // Volume keyframes ("volume" effect, level in dB).
    for (const auto& e : clip.effects) {
        if (!e.enabled || e.type != QLatin1String("volume")) continue;
        if (const Param* p = e.param(QStringLiteral("level")))
            gain *= dbToGain(p->evaluate(time - clip.start));
    }

    // Constant-power crossfade with the previous clip.
    const Tick headHalf = crossHalf(track, index);
    if (headHalf > 0 && time < clip.start + headHalf) {
        const double p = static_cast<double>(time - (clip.start - headHalf))
                         / static_cast<double>(2 * headHalf);
        return gain * std::sin(std::clamp(p, 0.0, 1.0) * kHalfPi);
    }
    if (index + 1 < track.clips.size()) {
        const Tick tailHalf = crossHalf(track, index + 1);
        const Tick cut = track.clips[index + 1].start;
        if (tailHalf > 0 && time >= cut - tailHalf) {
            const double p = static_cast<double>(time - (cut - tailHalf))
                             / static_cast<double>(2 * tailHalf);
            return gain * std::cos(std::clamp(p, 0.0, 1.0) * kHalfPi);
        }
    }

    if (time < clip.start || time >= clip.end()) return 0.0;

    if (headHalf <= 0) gain *= fadeIn(clip, time);
    const bool nextCross = index + 1 < track.clips.size() && hasCrossHead(track, index + 1);
    if (!nextCross) gain *= fadeOut(clip, time);
    return gain;
}

bool trackActive(const Sequence& seq, const Track& track)
{
    if (track.muted) return false;
    bool anySolo = false;
    for (const auto& t : seq.tracks)
        if (t.kind == track.kind && t.solo) anySolo = true;
    return !anySolo || track.solo;
}

} // namespace bc
