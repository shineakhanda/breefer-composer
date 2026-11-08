#include "media/AudioMix.h"

#include "core/Evaluate.h"
#include "media/Decoder.h"

#include <algorithm>
#include <cmath>

namespace bc {
namespace {

constexpr int kRate = AudioDecoder::kSampleRate;
constexpr Tick kTicksPerSample = kTicksPerSecond / kRate;
constexpr Tick kSeekTolerance = kTicksPerSecond / 20;   // 50 ms of drift before re-seeking

} // namespace

void panGains(double pan, double& left, double& right)
{
    const double p = std::clamp(pan, -1.0, 1.0);
    const double angle = (p + 1.0) * 0.25 * 3.14159265358979323846;   // 0..pi/2
    left = std::cos(angle) * 1.41421356237;
    right = std::sin(angle) * 1.41421356237;
    left = std::min(left, 1.0);
    right = std::min(right, 1.0);
}

void mixSequenceBlock(const Sequence& seq, Tick blockStart, int frames, float* out,
                      std::vector<float>& scratch, const AudioDecoderLookup& decoders,
                      std::unordered_map<std::string, Tick>& decoderPos,
                      const QVector<Sequence>* library, int depth)
{
    const Tick blockEnd = blockStart + static_cast<Tick>(frames) * kTicksPerSample;

    for (const auto& track : seq.tracks) {
        if (track.kind != TrackKind::Audio || !trackActive(seq, track)) continue;

        const double fader = dbToGain(track.volumeDb);
        double panL = 1.0;
        double panR = 1.0;
        panGains(track.pan, panL, panR);

        for (int ci = 0; ci < track.clips.size(); ++ci) {
            const Clip& clip = track.clips[ci];
            if (!clip.enabled || clip.muted) continue;

            if (clip.generator == GeneratorKind::Nest && library && depth < 4) {
                // Nested sequence audio: mix it into a temporary buffer, then
                // apply this clip's gain/fades and the track fader.
                const Sequence* nested = nullptr;
                for (const auto& candidate : *library)
                    if (candidate.id == clip.nestedSequenceId) nested = &candidate;
                if (!nested) continue;
                const Tick from = std::max(clip.start, blockStart);
                const Tick to = std::min(clip.end(), blockEnd);
                if (to <= from) continue;
                const int offset = static_cast<int>((from - blockStart) / kTicksPerSample);
                const int count = std::clamp(static_cast<int>((to - from) / kTicksPerSample), 0, frames - offset);
                if (count <= 0) continue;
                std::vector<float> nestedMix(static_cast<size_t>(count) * 2, 0.0f);
                mixSequenceBlock(*nested, clip.sourceTime(from), count, nestedMix.data(), scratch, decoders,
                                 decoderPos, library, depth + 1);
                const double g0 = clipAudioGain(track, ci, from) * fader;
                const double g1 = clipAudioGain(track, ci, from + static_cast<Tick>(count - 1) * kTicksPerSample) * fader;
                for (int i = 0; i < count; ++i) {
                    const double g = g0 + (g1 - g0) * (count > 1 ? static_cast<double>(i) / (count - 1) : 0.0);
                    out[static_cast<size_t>(offset + i) * 2] += static_cast<float>(nestedMix[static_cast<size_t>(i) * 2] * g * panL);
                    out[static_cast<size_t>(offset + i) * 2 + 1] += static_cast<float>(nestedMix[static_cast<size_t>(i) * 2 + 1] * g * panR);
                }
                continue;
            }
            if (clip.reverse || clip.isGenerator()) continue;

            Tick from = 0;
            Tick to = 0;
            audibleRange(track, ci, from, to);
            if (to <= blockStart || from >= blockEnd) continue;

            Tick overlapStart = std::max(from, blockStart);
            const Tick overlapEnd = std::min(to, blockEnd);

            // Source before the media start (crossfade pre-roll on a clip with no
            // head handle) is silence.
            const double speed = std::max(0.05, clip.speed);
            Tick desired = clip.sourceTime(overlapStart);
            if (desired < 0) {
                const Tick skip = static_cast<Tick>(std::ceil(static_cast<double>(-desired) / speed));
                overlapStart += skip;
                desired = clip.sourceTime(overlapStart);
                if (overlapStart >= overlapEnd) continue;
            }

            int frameOffset = static_cast<int>((overlapStart - blockStart) / kTicksPerSample);
            int count = static_cast<int>((overlapEnd - overlapStart) / kTicksPerSample);
            frameOffset = std::clamp(frameOffset, 0, frames);
            count = std::clamp(count, 0, frames - frameOffset);
            if (count <= 0) continue;

            AudioDecoder* decoder = decoders(clip.id, clip.mediaId);
            if (!decoder) continue;

            const std::string key = clip.id.toStdString();
            auto posIt = decoderPos.find(key);
            if (posIt == decoderPos.end() || std::llabs(posIt->second - desired) > kSeekTolerance) {
                decoder->seekTo(desired);
                decoderPos[key] = desired;
            }

            const int sourceFrames = std::max(1, static_cast<int>(std::lround(count * speed)));
            scratch.resize(static_cast<size_t>(sourceFrames) * 2 + 4);
            const int got = decoder->read(scratch.data(), sourceFrames);
            decoderPos[key] += static_cast<Tick>(got) * kTicksPerSample;
            if (got <= 0) continue;

            const double g0 = clipAudioGain(track, ci, overlapStart) * fader;
            const double g1 = clipAudioGain(track, ci, overlapStart + static_cast<Tick>(count - 1) * kTicksPerSample) * fader;

            for (int i = 0; i < count; ++i) {
                float l;
                float r;
                if (speed == 1.0) {
                    if (i >= got) break;
                    l = scratch[static_cast<size_t>(i) * 2];
                    r = scratch[static_cast<size_t>(i) * 2 + 1];
                } else {
                    const double pos = i * speed;
                    const int idx = std::min(static_cast<int>(pos), got - 1);
                    const int nxt = std::min(idx + 1, got - 1);
                    const float f = static_cast<float>(pos - idx);
                    l = scratch[static_cast<size_t>(idx) * 2] * (1.0f - f) + scratch[static_cast<size_t>(nxt) * 2] * f;
                    r = scratch[static_cast<size_t>(idx) * 2 + 1] * (1.0f - f) + scratch[static_cast<size_t>(nxt) * 2 + 1] * f;
                }
                const double t = count > 1 ? static_cast<double>(i) / (count - 1) : 0.0;
                const double g = g0 + (g1 - g0) * t;
                out[static_cast<size_t>(frameOffset + i) * 2] += static_cast<float>(l * g * panL);
                out[static_cast<size_t>(frameOffset + i) * 2 + 1] += static_cast<float>(r * g * panR);
            }
        }
    }
}

} // namespace bc
