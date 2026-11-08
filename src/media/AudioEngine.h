#pragma once

#include "core/Project.h"

#include <memory>

namespace bc {

// Real-time audio output. The device callback only copies from a lock-free
// ring buffer; all decoding and mixing happens on a feeder thread.
class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool initialize();
    void shutdown();

    void setSource(const Sequence& sequence, const QVector<MediaRef>& media);
    void play(Tick from);
    void pause();
    void seek(Tick from);

    bool isPlaying() const;
    Tick position() const;
    void peaks(float& left, float& right) const;

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace bc
