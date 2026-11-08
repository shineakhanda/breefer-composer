#include "io/Interchange.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamWriter>
#include <QSet>
#include <algorithm>
#include <cmath>

namespace bc::io {
namespace {

QString fileUrl(const QString& path)
{
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

bool writeText(const QString& path, const QString& text, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << text;
    out.flush();
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

const MediaRef* mediaFor(const Project& project, const QString& id)
{
    return project.mediaById(id);
}

// Flat, ordered list of video clips for formats that have no track concept.
QVector<Clip> flatVideoClips(const Sequence& sequence)
{
    QVector<Clip> clips;
    for (const auto& track : sequence.tracks) {
        if (track.kind != TrackKind::Video || track.muted) continue;
        for (const auto& clip : track.clips) clips.push_back(clip);
    }
    std::sort(clips.begin(), clips.end(),
              [](const Clip& a, const Clip& b) { return a.start < b.start; });
    return clips;
}

QString rational(const QString& value, const FrameRate& rate)
{
    return QStringLiteral("%1/%2s").arg(value).arg(rate.num);
}

QString frameCount(const FrameRate& rate, Tick ticks)
{
    return QString::number(rate.frameOf(ticks) * rate.den);
}

} // namespace

// ----------------------------------------------------------------------- EDL

bool exportEdl(const Sequence& sequence, const Project& project,
               const QString& path, QString* error)
{
    const FrameRate rate = sequence.rate;
    const QVector<Clip> clips = flatVideoClips(sequence);

    QString edl;
    edl += QStringLiteral("TITLE: %1\n").arg(sequence.name);
    edl += QStringLiteral("FCM: NON-DROP FRAME\n\n");

    int event = 1;
    for (const auto& clip : clips) {
        const MediaRef* media = mediaFor(project, clip.mediaId);
        const QString reel = QStringLiteral("AX");
        const QString clipName = media ? media->name : clip.mediaId;

        edl += QStringLiteral("%1  %2  V     C        %3 %4 %5 %6\n")
                   .arg(event, 3, 10, QLatin1Char('0'))
                   .arg(reel.leftJustified(8, QLatin1Char(' ')))
                   .arg(formatTimecode(clip.sourceIn, rate))
                   .arg(formatTimecode(clip.sourceIn + clip.duration, rate))
                   .arg(formatTimecode(clip.start, rate))
                   .arg(formatTimecode(clip.end(), rate));
        edl += QStringLiteral("* FROM CLIP NAME: %1\n\n").arg(clipName);
        ++event;
    }

    return writeText(path, edl, error);
}

// --------------------------------------------------------------------- FCP7

bool exportFcp7Xml(const Sequence& sequence, const Project& project,
                   const QString& path, QString* error)
{
    // xmeml v4 as written by Premiere Pro / FCP 7: frame counts at the
    // sequence's nominal timebase with an NTSC flag for 1001 rates.
    const int timebase = static_cast<int>(std::lround(sequence.rate.value()));
    const bool ntsc = sequence.rate.den == 1001;
    auto frames = [&](Tick t) { return QString::number(sequence.rate.frameOf(t)); };

    QString xml;
    QXmlStreamWriter w(&xml);
    auto writeRate = [&w](int tb, bool isNtsc) {
        w.writeStartElement(QStringLiteral("rate"));
        w.writeTextElement(QStringLiteral("timebase"), QString::number(tb));
        w.writeTextElement(QStringLiteral("ntsc"), isNtsc ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
        w.writeEndElement();
    };

    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"), true);
    w.writeDTD(QStringLiteral("<!DOCTYPE xmeml>"));
    w.writeStartElement(QStringLiteral("xmeml"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("4"));
    w.writeStartElement(QStringLiteral("sequence"));
    w.writeAttribute(QStringLiteral("id"), QStringLiteral("sequence-1"));
    w.writeTextElement(QStringLiteral("name"), sequence.name);
    w.writeTextElement(QStringLiteral("duration"), frames(sequence.duration));
    writeRate(timebase, ntsc);

    // Stable ids: one clipitem id per clip, one file id per media.
    QHash<QString, QString> clipItemId;
    int counter = 0;
    for (const auto& t : sequence.tracks)
        for (const auto& c : t.clips) clipItemId.insert(c.id, QStringLiteral("clipitem-%1").arg(++counter));
    QHash<QString, QString> fileId;
    QSet<QString> fileWritten;
    for (const auto& m : project.media) fileId.insert(m.id, QStringLiteral("file-%1").arg(fileId.size() + 1));

    auto writeTrack = [&](const Track& track, bool audio) {
        w.writeStartElement(QStringLiteral("track"));
        for (int ci = 0; ci < track.clips.size(); ++ci) {
            const Clip& clip = track.clips[ci];
            const MediaRef* media = mediaFor(project, clip.mediaId);
            if (!media || clip.isGenerator()) continue;

            // Cross dissolve centred on the cut before this clip.
            if (ci > 0 && clip.inTransition.active() && track.clips[ci - 1].end() == clip.start) {
                const Tick half = std::min(clip.inTransition.duration,
                                           std::min(clip.duration, track.clips[ci - 1].duration)) / 2;
                w.writeStartElement(QStringLiteral("transitionitem"));
                w.writeTextElement(QStringLiteral("start"), frames(clip.start - half));
                w.writeTextElement(QStringLiteral("end"), frames(clip.start + half));
                w.writeTextElement(QStringLiteral("alignment"), QStringLiteral("center"));
                writeRate(timebase, ntsc);
                w.writeStartElement(QStringLiteral("effect"));
                w.writeTextElement(QStringLiteral("name"), audio ? QStringLiteral("Cross Fade (+3dB)") : QStringLiteral("Cross Dissolve"));
                w.writeTextElement(QStringLiteral("effectid"), audio ? QStringLiteral("KGAudioTransCrossFade3dB") : QStringLiteral("Cross Dissolve"));
                w.writeTextElement(QStringLiteral("effecttype"), QStringLiteral("transition"));
                w.writeTextElement(QStringLiteral("mediatype"), audio ? QStringLiteral("audio") : QStringLiteral("video"));
                w.writeEndElement();
                w.writeEndElement();
            }

            w.writeStartElement(QStringLiteral("clipitem"));
            w.writeAttribute(QStringLiteral("id"), clipItemId.value(clip.id));
            w.writeTextElement(QStringLiteral("name"), clip.label.isEmpty() ? media->name : clip.label);
            w.writeTextElement(QStringLiteral("enabled"), clip.enabled ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
            w.writeTextElement(QStringLiteral("duration"), frames(media->duration));
            writeRate(timebase, ntsc);
            w.writeTextElement(QStringLiteral("start"), frames(clip.start));
            w.writeTextElement(QStringLiteral("end"), frames(clip.end()));
            w.writeTextElement(QStringLiteral("in"), frames(clip.sourceIn));
            w.writeTextElement(QStringLiteral("out"), frames(clip.sourceIn + clip.sourceSpan()));

            const QString fid = fileId.value(media->id);
            w.writeStartElement(QStringLiteral("file"));
            w.writeAttribute(QStringLiteral("id"), fid);
            if (!fileWritten.contains(fid)) {
                fileWritten.insert(fid);
                w.writeTextElement(QStringLiteral("name"), media->name);
                w.writeTextElement(QStringLiteral("pathurl"), fileUrl(media->path));
                writeRate(static_cast<int>(std::lround(media->rate.value())), media->rate.den == 1001);
                w.writeTextElement(QStringLiteral("duration"), frames(media->duration));
                w.writeStartElement(QStringLiteral("media"));
                if (media->hasVideo) {
                    w.writeStartElement(QStringLiteral("video"));
                    w.writeStartElement(QStringLiteral("samplecharacteristics"));
                    w.writeTextElement(QStringLiteral("width"), QString::number(media->width));
                    w.writeTextElement(QStringLiteral("height"), QString::number(media->height));
                    w.writeEndElement();
                    w.writeEndElement();
                }
                if (media->hasAudio) {
                    w.writeStartElement(QStringLiteral("audio"));
                    w.writeTextElement(QStringLiteral("channelcount"), QString::number(media->channels));
                    w.writeEndElement();
                }
                w.writeEndElement();   // media
            }
            w.writeEndElement();   // file

            if (audio) {
                w.writeStartElement(QStringLiteral("sourcetrack"));
                w.writeTextElement(QStringLiteral("mediatype"), QStringLiteral("audio"));
                w.writeTextElement(QStringLiteral("trackindex"), QStringLiteral("1"));
                w.writeEndElement();
            }
            if (!clip.linkId.isEmpty()) {
                for (const auto& t : sequence.tracks)
                    for (const auto& other : t.clips)
                        if (other.linkId == clip.linkId) {
                            w.writeStartElement(QStringLiteral("link"));
                            w.writeTextElement(QStringLiteral("linkclipref"), clipItemId.value(other.id));
                            w.writeTextElement(QStringLiteral("mediatype"),
                                               t.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video"));
                            w.writeEndElement();
                        }
            }
            w.writeEndElement();   // clipitem
        }
        w.writeTextElement(QStringLiteral("enabled"), track.muted ? QStringLiteral("FALSE") : QStringLiteral("TRUE"));
        w.writeTextElement(QStringLiteral("locked"), track.locked ? QStringLiteral("TRUE") : QStringLiteral("FALSE"));
        w.writeEndElement();   // track
    };

    w.writeStartElement(QStringLiteral("media"));
    w.writeStartElement(QStringLiteral("video"));
    w.writeStartElement(QStringLiteral("format"));
    w.writeStartElement(QStringLiteral("samplecharacteristics"));
    writeRate(timebase, ntsc);
    w.writeTextElement(QStringLiteral("width"), QString::number(sequence.width));
    w.writeTextElement(QStringLiteral("height"), QString::number(sequence.height));
    w.writeTextElement(QStringLiteral("pixelaspectratio"), QStringLiteral("square"));
    w.writeEndElement();
    w.writeEndElement();
    for (int lane = 1; lane <= sequence.videoTrackCount(); ++lane)
        if (const Track* t = sequence.track(sequence.trackForLane(TrackKind::Video, lane))) writeTrack(*t, false);
    w.writeEndElement();   // video

    w.writeStartElement(QStringLiteral("audio"));
    for (int lane = 1; lane <= sequence.audioTrackCount(); ++lane)
        if (const Track* t = sequence.track(sequence.trackForLane(TrackKind::Audio, lane))) writeTrack(*t, true);
    w.writeEndElement();   // audio

    w.writeEndElement();   // media
    w.writeEndElement();   // sequence
    w.writeEndElement();   // xmeml
    w.writeEndDocument();

    return writeText(path, xml, error);
}

// ------------------------------------------------------------------- FCPXML

bool exportFcpXml(const Sequence& sequence, const Project& project,
                  const QString& path, QString* error)
{
    QString xml;
    QXmlStreamWriter w(&xml);
    w.setAutoFormatting(true);
    w.writeStartDocument(QStringLiteral("1.0"), true);
    w.writeDTD(QStringLiteral("<!DOCTYPE fcpxml>"));
    w.writeStartElement(QStringLiteral("fcpxml"));
    w.writeAttribute(QStringLiteral("version"), QStringLiteral("1.9"));

    w.writeStartElement(QStringLiteral("resources"));

    w.writeStartElement(QStringLiteral("format"));
    w.writeAttribute(QStringLiteral("id"), QStringLiteral("r1"));
    w.writeAttribute(QStringLiteral("name"),
                     QStringLiteral("FFVideoFormat%1p%2")
                         .arg(sequence.height)
                         .arg(sequence.rate.num));
    w.writeAttribute(QStringLiteral("frameDuration"), rational(QStringLiteral("1"), sequence.rate));
    w.writeAttribute(QStringLiteral("width"), QString::number(sequence.width));
    w.writeAttribute(QStringLiteral("height"), QString::number(sequence.height));
    w.writeEndElement();

    int assetIndex = 0;
    QHash<QString, QString> assetIds;
    for (const auto& clip : flatVideoClips(sequence)) {
        const MediaRef* media = mediaFor(project, clip.mediaId);
        if (!media || assetIds.contains(media->id)) continue;

        const QString id = QStringLiteral("r%1").arg(++assetIndex + 1);
        assetIds.insert(media->id, id);

        w.writeStartElement(QStringLiteral("asset"));
        w.writeAttribute(QStringLiteral("id"), id);
        w.writeAttribute(QStringLiteral("name"), media->name);
        w.writeAttribute(QStringLiteral("src"), fileUrl(media->path));
        w.writeAttribute(QStringLiteral("start"), QStringLiteral("0s"));
        w.writeAttribute(QStringLiteral("duration"), rational(frameCount(media->rate, media->duration), media->rate));
        w.writeAttribute(QStringLiteral("hasVideo"), media->hasVideo ? QStringLiteral("1") : QStringLiteral("0"));
        w.writeAttribute(QStringLiteral("hasAudio"), media->hasAudio ? QStringLiteral("1") : QStringLiteral("0"));
        w.writeEndElement();
    }
    w.writeEndElement();   // resources

    w.writeStartElement(QStringLiteral("library"));
    w.writeStartElement(QStringLiteral("event"));
    w.writeAttribute(QStringLiteral("name"), QStringLiteral("Breefer Export"));
    w.writeStartElement(QStringLiteral("project"));
    w.writeAttribute(QStringLiteral("name"), sequence.name);
    w.writeStartElement(QStringLiteral("sequence"));
    w.writeAttribute(QStringLiteral("format"), QStringLiteral("r1"));
    w.writeAttribute(QStringLiteral("duration"), rational(frameCount(sequence.rate, sequence.duration), sequence.rate));
    w.writeAttribute(QStringLiteral("tcStart"), QStringLiteral("0s"));
    w.writeAttribute(QStringLiteral("tcFormat"), QStringLiteral("NDF"));

    w.writeStartElement(QStringLiteral("spine"));
    for (const auto& clip : flatVideoClips(sequence)) {
        const MediaRef* media = mediaFor(project, clip.mediaId);
        if (!media) continue;
        const QString ref = assetIds.value(media->id);
        if (ref.isEmpty()) continue;

        w.writeStartElement(QStringLiteral("asset-clip"));
        w.writeAttribute(QStringLiteral("name"), media->name);
        w.writeAttribute(QStringLiteral("ref"), ref);
        w.writeAttribute(QStringLiteral("offset"), rational(frameCount(sequence.rate, clip.start), sequence.rate));
        w.writeAttribute(QStringLiteral("start"), rational(frameCount(sequence.rate, clip.sourceIn), sequence.rate));
        w.writeAttribute(QStringLiteral("duration"), rational(frameCount(sequence.rate, clip.duration), sequence.rate));
        w.writeEndElement();
    }
    w.writeEndElement();   // spine
    w.writeEndElement();   // sequence
    w.writeEndElement();   // project
    w.writeEndElement();   // event
    w.writeEndElement();   // library
    w.writeEndElement();   // fcpxml
    w.writeEndDocument();

    return writeText(path, xml, error);
}

// --------------------------------------------------------------------- OTIO

bool exportOtio(const Sequence& sequence, const Project& project,
                const QString& path, QString* error)
{
    auto rationalTime = [&](Tick ticks) {
        QJsonObject t;
        t["OTIO_SCHEMA"] = QStringLiteral("RationalTime.1");
        t["rate"] = sequence.rate.value();
        t["value"] = static_cast<double>(sequence.rate.frameOf(ticks));
        return t;
    };

    auto timeRange = [&](Tick start, Tick duration) {
        QJsonObject r;
        r["OTIO_SCHEMA"] = QStringLiteral("TimeRange.1");
        r["start_time"] = rationalTime(start);
        r["duration"] = rationalTime(duration);
        return r;
    };

    // OTIO stacks list tracks bottom-up: V1, V2, ... then A1, A2, ...
    QVector<const Track*> ordered;
    for (int lane = 1; lane <= sequence.videoTrackCount(); ++lane)
        if (const Track* t = sequence.track(sequence.trackForLane(TrackKind::Video, lane))) ordered << t;
    for (int lane = 1; lane <= sequence.audioTrackCount(); ++lane)
        if (const Track* t = sequence.track(sequence.trackForLane(TrackKind::Audio, lane))) ordered << t;

    QJsonArray trackArray;
    for (const Track* trackPtr : ordered) {
        const Track& track = *trackPtr;
        QJsonObject trackObject;
        trackObject["OTIO_SCHEMA"] = QStringLiteral("Track.1");
        trackObject["name"] = track.name;
        trackObject["kind"] = track.kind == TrackKind::Video ? QStringLiteral("Video")
                                                             : QStringLiteral("Audio");
        trackObject["metadata"] = QJsonObject{{"breefer", QJsonObject{{"muted", track.muted}}}};

        QJsonArray children;
        Tick cursor = 0;
        for (const auto& clip : track.clips) {
            // Tracks are sequential in OTIO: empty time must be an explicit Gap.
            if (clip.start > cursor) {
                QJsonObject gap;
                gap["OTIO_SCHEMA"] = QStringLiteral("Gap.1");
                gap["name"] = QString();
                gap["source_range"] = timeRange(0, clip.start - cursor);
                children.append(gap);
            }
            cursor = clip.end();
            if (clip.isGenerator()) {
                QJsonObject gap;
                gap["OTIO_SCHEMA"] = QStringLiteral("Gap.1");
                gap["name"] = clip.generator == GeneratorKind::Title ? clip.title.text : QStringLiteral("Color Matte");
                gap["source_range"] = timeRange(0, clip.duration);
                children.append(gap);
                continue;
            }
            const MediaRef* media = mediaFor(project, clip.mediaId);

            QJsonObject reference;
            reference["OTIO_SCHEMA"] = QStringLiteral("ExternalReference.1");
            reference["name"] = media ? media->name : clip.mediaId;
            reference["target_url"] = media ? fileUrl(media->path) : QString();
            reference["available_range"] = media ? timeRange(0, media->duration) : timeRange(0, 0);

            QJsonObject item;
            item["OTIO_SCHEMA"] = QStringLiteral("Clip.2");
            item["name"] = media ? media->name : clip.mediaId;
            item["source_range"] = timeRange(clip.sourceIn, clip.sourceSpan());
            if (std::abs(clip.speed - 1.0) > 1e-6 || clip.reverse) {
                QJsonObject warp;
                warp["OTIO_SCHEMA"] = QStringLiteral("LinearTimeWarp.1");
                warp["name"] = QStringLiteral("speed");
                warp["time_scalar"] = clip.reverse ? -clip.speed : clip.speed;
                item["effects"] = QJsonArray{warp};
            }
            item["media_reference"] = reference;
            children.append(item);
        }

        trackObject["children"] = children;
        trackArray.append(trackObject);
    }

    QJsonObject stack;
    stack["OTIO_SCHEMA"] = QStringLiteral("Stack.1");
    stack["name"] = QStringLiteral("tracks");
    stack["children"] = trackArray;

    QJsonObject timeline;
    timeline["OTIO_SCHEMA"] = QStringLiteral("Timeline.1");
    timeline["name"] = sequence.name;
    timeline["global_start_time"] = rationalTime(0);
    timeline["metadata"] =
        QJsonObject{{"breefer", QJsonObject{{"width", sequence.width},
                                           {"height", sequence.height}}}};
    timeline["tracks"] = stack;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(timeline).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

} // namespace bc::io
