#include "media/AudioEngine.h"

#include "media/AudioMix.h"
#include "media/Decoder.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace bc {
namespace {

constexpr int kRate = 48000;
constexpr int kChannels = 2;
constexpr int kBlockFrames = 1024;
constexpr Tick kTicksPerFrame = kTicksPerSecond / kRate;   // exactly 5,292,000

} // namespace

struct AudioEngine::Impl {
    ma_device device{};
    bool deviceReady = false;

    std::thread feeder;
    std::atomic<bool> quit{false};
    std::atomic<bool> playing{false};
    std::atomic<uint64_t> generation{0};

    mutable std::mutex sourceMutex;
    Sequence sequence;
    QVector<MediaRef> media;

    std::vector<float> ring;
    uint64_t ringFrames = static_cast<uint64_t>(kRate) * 2;   // 2 s of headroom
    std::atomic<uint64_t> readPos{0};
    std::atomic<uint64_t> writePos{0};

    std::atomic<uint64_t> playedFrames{0};
    std::atomic<int64_t> startTick{0};
    std::atomic<int64_t> feedTick{0};

    std::atomic<float> peakLeft{0.0f};
    std::atomic<float> peakRight{0.0f};

    // Feeder-thread state only.
    std::unordered_map<std::string, std::unique_ptr<AudioDecoder>> decoders;
    std::unordered_map<std::string, Tick> decoderPos;   // next source tick each decoder will return
    std::vector<float> mix;
    std::vector<float> scratch;

    const MediaRef* findMedia(const QString& id) const
    {
        for (const auto& m : media)
            if (m.id == id) return &m;
        return nullptr;
    }

    AudioDecoder* decoderFor(const QString& clipId, const QString& mediaId)
    {
        const std::string key = clipId.toStdString();
        auto it = decoders.find(key);
        if (it != decoders.end()) return it->second.get();

        const MediaRef* ref = findMedia(mediaId);
        if (!ref || !ref->hasAudio) {
            decoders[key] = nullptr;
            return nullptr;
        }

        auto decoder = std::make_unique<AudioDecoder>();
        if (!decoder->open(ref->path)) {
            decoders[key] = nullptr;
            return nullptr;
        }
        AudioDecoder* raw = decoder.get();
        decoders[key] = std::move(decoder);
        return raw;
    }

    void mixBlock(Tick blockStart)
    {
        std::fill(mix.begin(), mix.end(), 0.0f);

        Sequence seq;
        {
            std::lock_guard<std::mutex> lock(sourceMutex);
            seq = sequence;
        }
        if (seq.tracks.isEmpty()) return;
        mixSequenceBlock(seq, blockStart, kBlockFrames, mix.data(), scratch,
                         [this](const QString& clipId, const QString& mediaId) {
                             return decoderFor(clipId, mediaId);
                         },
                         decoderPos);

        // Soft clip so a dense mix never slams the device with > 0 dBFS.
        for (auto& sample : mix) sample = std::tanh(sample);
    }

    void writeBlock(uint32_t frames)
    {
        const uint64_t w = writePos.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < frames; ++i) {
            const size_t idx = static_cast<size_t>((w + i) % ringFrames) * kChannels;
            ring[idx + 0] = mix[static_cast<size_t>(i) * kChannels + 0];
            ring[idx + 1] = mix[static_cast<size_t>(i) * kChannels + 1];
        }
        writePos.store(w + frames, std::memory_order_release);
    }

    void runFeeder()
    {
        uint64_t localGeneration = generation.load();

        while (!quit.load(std::memory_order_relaxed)) {
            if (!playing.load(std::memory_order_relaxed)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }

            const uint64_t gen = generation.load();
            if (gen != localGeneration) {
                localGeneration = gen;
                decoderPos.clear();
            }

            const uint64_t written = writePos.load(std::memory_order_acquire);
            const uint64_t read = readPos.load(std::memory_order_acquire);
            const uint64_t available = written - read;
            if (available + kBlockFrames > ringFrames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }

            const Tick blockStart = feedTick.load();
            mixBlock(blockStart);
            writeBlock(kBlockFrames);
            feedTick.store(blockStart + static_cast<Tick>(kBlockFrames) * kTicksPerFrame);
        }
    }

    void flushRing()
    {
        readPos.store(writePos.load(std::memory_order_acquire));
        peakLeft.store(0.0f);
        peakRight.store(0.0f);
    }

    void resetTransport(Tick from)
    {
        startTick.store(from);
        feedTick.store(from);
        playedFrames.store(0);
        flushRing();
        generation.fetch_add(1);
    }
};

namespace {

void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount)
{
    Q_UNUSED(input);
    auto* impl = static_cast<AudioEngine::Impl*>(device->pUserData);
    auto* out = static_cast<float*>(output);

    std::memset(out, 0, static_cast<size_t>(frameCount) * kChannels * sizeof(float));

    const uint64_t read = impl->readPos.load(std::memory_order_acquire);
    const uint64_t written = impl->writePos.load(std::memory_order_acquire);
    const uint64_t available = written - read;
    const uint32_t copy = static_cast<uint32_t>(
        std::min<uint64_t>(available, frameCount));

    float peakL = 0.0f;
    float peakR = 0.0f;

    for (uint32_t i = 0; i < copy; ++i) {
        const size_t idx = static_cast<size_t>((read + i) % impl->ringFrames) * kChannels;
        const float l = impl->ring[idx + 0];
        const float r = impl->ring[idx + 1];
        out[static_cast<size_t>(i) * kChannels + 0] = l;
        out[static_cast<size_t>(i) * kChannels + 1] = r;
        peakL = std::max(peakL, std::abs(l));
        peakR = std::max(peakR, std::abs(r));
    }

    impl->readPos.store(read + copy, std::memory_order_release);
    impl->playedFrames.fetch_add(copy);

    if (copy > 0) {
        impl->peakLeft.store(peakL);
        impl->peakRight.store(peakR);
    }
}

} // namespace

AudioEngine::AudioEngine()
    : m_impl(std::make_unique<Impl>())
{
    m_impl->ring.assign(static_cast<size_t>(m_impl->ringFrames) * kChannels, 0.0f);
    m_impl->mix.assign(static_cast<size_t>(kBlockFrames) * kChannels, 0.0f);
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

bool AudioEngine::initialize()
{
    if (m_impl->deviceReady) return true;

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = kChannels;
    config.sampleRate = kRate;
    config.dataCallback = audioCallback;
    config.pUserData = m_impl.get();

    if (ma_device_init(nullptr, &config, &m_impl->device) != MA_SUCCESS) {
        return false;
    }

    if (ma_device_start(&m_impl->device) != MA_SUCCESS) {
        ma_device_uninit(&m_impl->device);
        return false;
    }

    m_impl->deviceReady = true;
    m_impl->feeder = std::thread([this] { m_impl->runFeeder(); });
    return true;
}

void AudioEngine::shutdown()
{
    if (!m_impl) return;

    m_impl->quit.store(true);
    if (m_impl->feeder.joinable()) m_impl->feeder.join();

    if (m_impl->deviceReady) {
        ma_device_uninit(&m_impl->device);
        m_impl->deviceReady = false;
    }
}

void AudioEngine::setSource(const Sequence& sequence, const QVector<MediaRef>& media)
{
    std::lock_guard<std::mutex> lock(m_impl->sourceMutex);
    m_impl->sequence = sequence;
    m_impl->media = media;
}

void AudioEngine::play(Tick from)
{
    m_impl->resetTransport(from);
    m_impl->playing.store(true);
}

void AudioEngine::pause()
{
    m_impl->playing.store(false);
    m_impl->flushRing();
}

void AudioEngine::seek(Tick from)
{
    const bool wasPlaying = m_impl->playing.load();
    m_impl->resetTransport(from);
    m_impl->playing.store(wasPlaying);
}

bool AudioEngine::isPlaying() const
{
    return m_impl->playing.load();
}

Tick AudioEngine::position() const
{
    return m_impl->startTick.load()
           + static_cast<Tick>(m_impl->playedFrames.load()) * kTicksPerFrame;
}

void AudioEngine::peaks(float& left, float& right) const
{
    left = m_impl->peakLeft.load();
    right = m_impl->peakRight.load();
}

} // namespace bc
