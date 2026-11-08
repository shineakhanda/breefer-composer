#pragma once

#include "core/Types.h"

#include <QImage>
#include <QString>
#include <QVector>
#include <memory>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

namespace bc {

class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();
    bool isOpen() const { return m_ctx != nullptr; }
    bool isHardware() const { return m_hw; }

    // Global switch (Preferences). Hardware decode uses D3D11VA on Windows and
    // falls back to software per file when the GPU cannot handle the codec.
    static void setHardwareDecodeEnabled(bool enabled);
    static bool hardwareDecodeEnabled();

    int sourceWidth() const { return m_width; }
    int sourceHeight() const { return m_height; }
    FrameRate rate() const { return m_rate; }
    Tick duration() const { return m_duration; }

    // Decodes the frame at `sourceTime`, scaled into outWidth x outHeight.
    // Returns a null image if the position cannot be satisfied.
    QImage frameAt(Tick sourceTime, int outWidth, int outHeight);

private:
    bool seekTo(Tick sourceTime);
    QImage toImage(AVFrame* frame, int outWidth, int outHeight);

    AVFormatContext* m_fmt = nullptr;
    AVCodecContext* m_ctx = nullptr;
    AVPacket* m_packet = nullptr;
    AVFrame* m_frame = nullptr;
    AVFrame* m_swFrame = nullptr;
    SwsContext* m_sws = nullptr;
    bool m_hw = false;
    int m_stream = -1;
    int m_width = 0;
    int m_height = 0;
    FrameRate m_rate{25, 1};
    Tick m_duration = 0;
    Tick m_lastDecoded = -1;
    bool m_flushed = false;
};

// Pull-based PCM decoder: always emits 48 kHz stereo float, so the mixer never
// has to care about the source format.
class AudioDecoder {
public:
    static constexpr int kSampleRate = 48000;
    static constexpr int kChannels = 2;

    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();
    bool isOpen() const { return m_ctx != nullptr; }
    Tick duration() const { return m_duration; }

    // Fills `out` with interleaved stereo float. Returns frames written.
    int read(float* out, int maxFrames);

    // Places the read head at the given source position.
    bool seekTo(Tick sourceTime);

private:
    bool receiveToBuffer();

    AVFormatContext* m_fmt = nullptr;
    AVCodecContext* m_ctx = nullptr;
    AVPacket* m_packet = nullptr;
    AVFrame* m_frame = nullptr;
    SwrContext* m_swr = nullptr;
    int m_stream = -1;
    Tick m_duration = 0;
    bool m_eof = false;

    QVector<float> m_buf;   // decoded stereo float staging
    int m_bufRead = 0;
};

} // namespace bc
