#pragma once

#include <QString>
#include <QtGlobal>
#include <cmath>
#include <cstdint>

namespace bc {

// All timeline positions are integer ticks at a base rate that divides evenly
// by every common frame rate, including 1001-based NTSC rates.
//   254016000000 = 2^8 * 3^4 * 5^3 * 7^2 * 11 ? -> verified divisible by
//   24, 25, 30, 48, 50, 60, 100, 120, 24000/1001, 30000/1001, 60000/1001
using Tick = qint64;
inline constexpr Tick kTicksPerSecond = 254016000000LL;

inline Tick secToTicks(double s)
{
    return static_cast<Tick>(s * static_cast<double>(kTicksPerSecond) + (s >= 0 ? 0.5 : -0.5));
}
inline double ticksToSec(Tick t)
{
    return static_cast<double>(t) / static_cast<double>(kTicksPerSecond);
}
inline Tick msToTicks(double ms) { return secToTicks(ms / 1000.0); }

struct FrameRate {
    int num = 25;
    int den = 1;

    double value() const { return static_cast<double>(num) / static_cast<double>(den); }
    bool operator==(const FrameRate& o) const { return num == o.num && den == o.den; }
    bool operator!=(const FrameRate& o) const { return !(*this == o); }

    QString label() const
    {
        if (den == 1) return QString::number(num);
        // NTSC rates are conventionally written as 23.976 / 29.97 / 59.94
        return QString::number(value(), 'f', 2);
    }

    static FrameRate fromDouble(double fps)
    {
        static const FrameRate known[] = {
            {24000, 1001}, {30000, 1001}, {60000, 1001}, {120000, 1001},
            {24, 1}, {25, 1}, {30, 1}, {48, 1}, {50, 1}, {60, 1}, {120, 1}, {240, 1},
        };
        for (const auto& k : known)
            if (std::abs(k.value() - fps) < 0.02) return k;
        return FrameRate{static_cast<int>(std::lround(fps)), 1};
    }

    // Duration of a single frame in ticks. Exact for every rate above.
    Tick ticksPerFrame() const
    {
        return (kTicksPerSecond * static_cast<Tick>(den)) / static_cast<Tick>(num);
    }
    Tick ticksForFrames(qint64 frames) const { return frames * ticksPerFrame(); }
    qint64 frameOf(Tick t) const
    {
        const Tick tpf = ticksPerFrame();
        return tpf > 0 ? t / tpf : 0;
    }
    Tick snap(Tick t) const
    {
        const Tick tpf = ticksPerFrame();
        if (tpf <= 0) return t;
        return ((t + tpf / 2) / tpf) * tpf;
    }
};

// SMPTE timecode. Drop-frame only for 29.97/59.94 as is conventional.
inline QString formatTimecode(Tick t, FrameRate fr, bool dropFrame = false)
{
    const qint64 tpf = fr.ticksPerFrame();
    if (tpf <= 0) return QStringLiteral("00:00:00:00");

    qint64 totalFrames = t / tpf;
    if (totalFrames < 0) totalFrames = 0;

    const int fpsInt = static_cast<int>(std::lround(fr.value()));
    const bool df = dropFrame && (std::abs(fr.value() - 29.97) < 0.01 || std::abs(fr.value() - 59.94) < 0.01);

    qint64 fps = fpsInt;
    if (df) {
        // Convert real frame count to drop-frame timecode frame count.
        const qint64 dropPerMinute = (fpsInt == 59) ? 4 : 2;
        const qint64 framesPer10Min = fpsInt * 600 - dropPerMinute * 9;
        const qint64 framesPerMin = fpsInt * 60 - dropPerMinute;
        qint64 d = totalFrames / framesPer10Min;
        qint64 m = totalFrames % framesPer10Min;
        totalFrames += dropPerMinute * 9 * d;
        if (m > dropPerMinute)
            totalFrames += dropPerMinute * ((m - dropPerMinute) / framesPerMin);
    }

    qint64 ff = totalFrames % fps;
    qint64 totalSeconds = totalFrames / fps;
    qint64 ss = totalSeconds % 60;
    qint64 mm = (totalSeconds / 60) % 60;
    qint64 hh = totalSeconds / 3600;

    return QStringLiteral("%1:%2:%3:%4")
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

} // namespace bc
