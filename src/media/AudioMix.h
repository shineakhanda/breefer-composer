#pragma once

#include "core/Project.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace bc {

class AudioDecoder;

using AudioDecoderLookup = std::function<AudioDecoder*(const QString& clipId, const QString& mediaId)>;

// Mixes `frames` stereo frames of the sequence starting at `blockStart` into
// `out` (interleaved, accumulated). Shared by real-time playback and export so
// both apply clip gain, volume keyframes, fades, crossfades, speed, track
// fader and pan identically.
//
// `decoderPos` tracks the source tick each decoder will return next, so the
// mixer only seeks when playback jumps.
void mixSequenceBlock(const Sequence& seq, Tick blockStart, int frames, float* out,
                      std::vector<float>& scratch, const AudioDecoderLookup& decoders,
                      std::unordered_map<std::string, Tick>& decoderPos);

// Constant-power pan law; centre = unity on both sides.
void panGains(double pan, double& left, double& right);

} // namespace bc
