#pragma once

#include "core/Project.h"

namespace bc {

// What a single track contributes at a point in time, including transitions.
// Shared by the compositor, the real-time audio mixer and the exporter so that
// preview and export can never disagree.
struct TrackEval {
    // Normal case: one clip, possibly fading in/out on its own.
    const Clip* clip = nullptr;
    double fade = 1.0;                    // 0..1 multiplier for one-sided fades
    TransitionType fadeType = TransitionType::None;

    // Two-sided transition across a cut.
    const Clip* outgoing = nullptr;
    const Clip* incoming = nullptr;
    double progress = 0.0;                // 0 = all outgoing, 1 = all incoming
    TransitionType type = TransitionType::None;

    bool empty() const { return !clip && !outgoing && !incoming; }
    bool isTransition() const { return outgoing && incoming; }
};

// True if a clip at `index` has a two-sided head transition (previous clip abuts).
bool hasCrossHead(const Track& track, int index);

// Half the head transition's length when it is two-sided, otherwise 0. The
// previous clip keeps playing for this long past its end.
Tick crossHalf(const Track& track, int index);

// The full interval during which a clip contributes (includes transition handles).
void audibleRange(const Track& track, int index, Tick& from, Tick& to);

TrackEval evaluateTrack(const Track& track, Tick time);

// Linear gain of a clip on an audio track at `time`, folding in clip gain,
// one-sided fades and constant-power crossfades. Track fader/pan not included.
double clipAudioGain(const Track& track, int index, Tick time);

// Whether any track of this kind is soloed, and whether `track` is audible/visible.
bool trackActive(const Sequence& seq, const Track& track);

} // namespace bc
