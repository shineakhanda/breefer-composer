#include "media/MediaProbe.h"

#include "media/Decoder.h"

#include <QFileInfo>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace bc {
namespace {

QString ffError(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // namespace

ProbeResult probeMedia(const QString& path)
{
    ProbeResult result;
    result.ref.id = makeId(QStringLiteral("media"));
    result.ref.path = path;
    result.ref.name = QFileInfo(path).fileName();

    AVFormatContext* fmt = nullptr;
    const QByteArray utf8 = path.toUtf8();
    int ret = avformat_open_input(&fmt, utf8.constData(), nullptr, nullptr);
    if (ret < 0) {
        result.error = QStringLiteral("Cannot open file: %1").arg(ffError(ret));
        return result;
    }

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        result.error = QStringLiteral("Not a readable media file");
        avformat_close_input(&fmt);
        return result;
    }

    if (fmt->duration != AV_NOPTS_VALUE && fmt->duration > 0)
        result.ref.duration = static_cast<Tick>(fmt->duration) * kTicksPerSecond / AV_TIME_BASE;

    const int v = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int a = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (v >= 0) {
        AVStream* st = fmt->streams[v];
        AVCodecParameters* par = st->codecpar;
        result.ref.hasVideo = true;
        result.ref.width = par->width;
        result.ref.height = par->height;
        result.ref.codec = QString::fromUtf8(avcodec_get_name(par->codec_id));

        AVRational fr = av_guess_frame_rate(fmt, st, nullptr);
        if (fr.num > 0 && fr.den > 0)
            result.ref.rate = FrameRate{fr.num, fr.den};

        if (result.ref.duration <= 0 && st->duration != AV_NOPTS_VALUE && st->duration > 0)
            result.ref.duration = secToTicks(static_cast<double>(st->duration) * av_q2d(st->time_base));

        // Single-image files report no useful duration; give them a 10 s default.
        if (result.ref.duration <= 0) result.ref.duration = secToTicks(10.0);
    }

    if (a >= 0) {
        AVStream* st = fmt->streams[a];
        result.ref.hasAudio = true;
        result.ref.sampleRate = st->codecpar->sample_rate;
        result.ref.channels = st->codecpar->ch_layout.nb_channels;
    }

    avformat_close_input(&fmt);

    if (!result.ref.hasVideo && !result.ref.hasAudio) {
        result.error = QStringLiteral("No audio or video streams");
        return result;
    }

    if (result.ref.duration <= 0) result.ref.duration = secToTicks(10.0);
    result.ok = true;
    return result;
}

QImage extractThumbnail(const QString& path, int maxWidth, int maxHeight)
{
    VideoDecoder decoder;
    if (!decoder.open(path)) return {};

    const Tick probeTime = decoder.duration() > secToTicks(2.0)
                               ? decoder.duration() / 10
                               : 0;

    QImage image = decoder.frameAt(probeTime, maxWidth, maxHeight);
    if (image.isNull())
        image = decoder.frameAt(0, maxWidth, maxHeight);
    return image;
}

} // namespace bc
