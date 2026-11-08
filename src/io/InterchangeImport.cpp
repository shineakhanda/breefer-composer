// Timeline importers: OpenTimelineIO, FCP 7 XML (xmeml) and CMX 3600 EDL.
// Each adapter maps its format onto the canonical Sequence; nothing format
// specific leaks into the model.

#include "io/Interchange.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QUrl>
#include <QXmlStreamReader>
#include <cmath>

namespace bc::io {
namespace {

Tick secondsToTicks(double s) { return secToTicks(s); }

QString pathFromUrl(const QString& url)
{
    if (url.isEmpty()) return {};
    const QUrl u(url);
    if (u.isLocalFile()) return QDir::toNativeSeparators(u.toLocalFile());
    if (url.startsWith(QStringLiteral("file:"))) {
        // Premiere writes file://localhost/C%3a/...
        QString p = QUrl::fromPercentEncoding(url.toUtf8());
        p.remove(QRegularExpression(QStringLiteral("^file:/*(localhost/)?")));
        if (p.size() > 1 && p[1] == QLatin1Char(':')) return QDir::toNativeSeparators(p);
        return QDir::toNativeSeparators(QLatin1Char('/') + p);
    }
    return QDir::toNativeSeparators(url);
}

// Shared media table keyed by path (or name when no path).
struct MediaTable {
    QVector<MediaRef>& media;
    QHash<QString, QString> byKey;

    QString idFor(const QString& path, const QString& name, bool video, bool audio)
    {
        const QString key = path.isEmpty() ? name : QFileInfo(path).absoluteFilePath().toLower();
        auto it = byKey.find(key);
        if (it != byKey.end()) {
            for (auto& m : media)
                if (m.id == it.value()) {
                    m.hasVideo = m.hasVideo || video;
                    m.hasAudio = m.hasAudio || audio;
                }
            return it.value();
        }
        MediaRef m;
        m.id = makeId(QStringLiteral("media"));
        m.path = path;
        m.name = name.isEmpty() ? QFileInfo(path).fileName() : name;
        m.hasVideo = video;
        m.hasAudio = audio;
        media.push_back(m);
        byKey.insert(key, m.id);
        return m.id;
    }
};

void finish(Sequence& seq)
{
    for (auto& t : seq.tracks) t.sortClips();
    // Drop overlaps an importer could not represent (keeps the invariant).
    for (auto& t : seq.tracks) {
        for (int i = 1; i < t.clips.size(); ++i) {
            if (t.clips[i].start < t.clips[i - 1].end()) {
                const Tick overlap = t.clips[i - 1].end() - t.clips[i].start;
                t.clips[i - 1].duration -= overlap;
                if (t.clips[i - 1].duration <= 0) {
                    t.clips.removeAt(i - 1);
                    --i;
                }
            }
        }
    }
    seq.renameTracks();
    seq.recomputeDuration();
}

Sequence emptySequence(const QString& name, int width, int height, FrameRate rate)
{
    Sequence s;
    s.id = makeId(QStringLiteral("seq"));
    s.name = name;
    s.width = width;
    s.height = height;
    s.rate = rate;
    return s;
}

int ensureLane(Sequence& seq, TrackKind kind, int lane)
{
    int ti = seq.trackForLane(kind, lane);
    while (ti < 0) {
        Track t;
        t.kind = kind;
        t.id = makeId(kind == TrackKind::Video ? QStringLiteral("vt") : QStringLiteral("at"));
        if (kind == TrackKind::Video) seq.tracks.insert(0, t);
        else seq.tracks.push_back(t);
        ti = seq.trackForLane(kind, lane);
    }
    return ti;
}

void ensureMinimumTracks(Sequence& seq)
{
    ensureLane(seq, TrackKind::Video, 1);
    ensureLane(seq, TrackKind::Audio, 1);
}

// ------------------------------------------------------------------- OTIO

double rtSeconds(const QJsonObject& rt)
{
    const double rate = rt["rate"].toDouble(24.0);
    return rate > 0 ? rt["value"].toDouble() / rate : 0.0;
}

bool importOtio(const QString& path, Sequence& out, QVector<MediaRef>& media, QStringList* warnings, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject timeline = doc.object();
    if (timeline["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("SerializableCollection"))) {
        for (const auto& v : timeline["children"].toArray())
            if (v.toObject()["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("Timeline"))) {
                timeline = v.toObject();
                break;
            }
    }
    if (!timeline["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("Timeline"))) {
        if (error) *error = QStringLiteral("Not an OpenTimelineIO timeline");
        return false;
    }

    // Rate from the first clip's source range.
    double rateValue = 0;
    const QJsonArray tracks = timeline["tracks"].toObject()["children"].toArray();
    for (const auto& tv : tracks) {
        for (const auto& cv : tv.toObject()["children"].toArray()) {
            const QJsonObject sr = cv.toObject()["source_range"].toObject();
            if (!sr.isEmpty()) { rateValue = sr["duration"].toObject()["rate"].toDouble(); break; }
        }
        if (rateValue > 0) break;
    }
    const FrameRate rate = FrameRate::fromDouble(rateValue > 0 ? rateValue : 25.0);
    const QJsonObject meta = timeline["metadata"].toObject()["breefer"].toObject();
    out = emptySequence(timeline["name"].toString(QFileInfo(path).completeBaseName()),
                        meta["width"].toInt(1920), meta["height"].toInt(1080), rate);

    MediaTable table{media, {}};
    int videoLane = 0;
    int audioLane = 0;
    const QFileInfo base(path);

    for (const auto& tv : tracks) {
        const QJsonObject track = tv.toObject();
        const bool audio = track["kind"].toString() == QLatin1String("Audio");
        const int lane = audio ? ++audioLane : ++videoLane;
        const int ti = ensureLane(out, audio ? TrackKind::Audio : TrackKind::Video, lane);
        if (!track["name"].toString().isEmpty()) out.tracks[ti].name = track["name"].toString();

        Tick cursor = 0;
        TransitionType pendingType = TransitionType::None;
        Tick pendingDuration = 0;
        for (const auto& cv : track["children"].toArray()) {
            const QJsonObject item = cv.toObject();
            const QString schema = item["OTIO_SCHEMA"].toString();
            if (schema.startsWith(QStringLiteral("Transition"))) {
                const double in = rtSeconds(item["in_offset"].toObject());
                const double outOff = rtSeconds(item["out_offset"].toObject());
                pendingType = TransitionType::CrossDissolve;
                pendingDuration = out.rate.snap(secondsToTicks(in + outOff));
                continue;
            }
            const QJsonObject sr = item["source_range"].toObject();
            const Tick duration = out.rate.snap(secondsToTicks(rtSeconds(sr["duration"].toObject())));
            if (schema.startsWith(QStringLiteral("Gap"))) {
                cursor += duration;
                continue;
            }
            if (!schema.startsWith(QStringLiteral("Clip"))) {
                if (warnings) *warnings << QStringLiteral("Skipped unsupported item %1").arg(schema);
                cursor += duration;
                continue;
            }
            QJsonObject ref = item["media_reference"].toObject();
            if (ref.isEmpty()) {
                // Clip.2 media_references map.
                const QJsonObject refs = item["media_references"].toObject();
                ref = refs[item["active_media_reference_key"].toString(QStringLiteral("DEFAULT_MEDIA"))].toObject();
            }
            QString mediaPath = pathFromUrl(ref["target_url"].toString());
            if (!mediaPath.isEmpty() && QFileInfo(mediaPath).isRelative())
                mediaPath = base.absoluteDir().filePath(mediaPath);

            Clip c;
            c.id = makeId(QStringLiteral("clip"));
            c.mediaId = table.idFor(mediaPath, ref["name"].toString(item["name"].toString()), !audio, audio);
            c.label = item["name"].toString();
            c.start = cursor;
            c.sourceIn = std::max<Tick>(0, secondsToTicks(rtSeconds(sr["start_time"].toObject())));
            double speed = 1.0;
            for (const auto& ev : item["effects"].toArray()) {
                const QJsonObject e = ev.toObject();
                if (e["OTIO_SCHEMA"].toString().startsWith(QStringLiteral("LinearTimeWarp")))
                    speed = e["time_scalar"].toDouble(1.0);
            }
            c.speed = std::max(0.05, std::abs(speed));
            c.reverse = speed < 0;
            c.duration = out.rate.snap(static_cast<Tick>(duration / c.speed));
            if (pendingType != TransitionType::None) {
                c.inTransition = {pendingType, pendingDuration};
                pendingType = TransitionType::None;
            }
            if (c.duration > 0) out.tracks[ti].clips.push_back(c);
            cursor += c.duration;
        }
    }

    ensureMinimumTracks(out);
    finish(out);
    return true;
}

// ---------------------------------------------------------------- FCP7 XML

struct XmlClip {
    QString name;
    QString fileId;
    Tick start = -1;
    Tick end = -1;
    Tick in = 0;
    Tick out = 0;
    bool enabled = true;
    double speed = 1.0;
};

bool importFcp7(const QString& path, Sequence& out, QVector<MediaRef>& media, QStringList* warnings, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    QXmlStreamReader xml(&file);

    MediaTable table{media, {}};
    QHash<QString, QString> fileIdToMedia;     // xml file id -> media id
    QHash<QString, QPair<bool, bool>> fileStreams;

    int timebase = 25;
    bool ntsc = false;
    int width = 1920;
    int height = 1080;
    QString seqName = QFileInfo(path).completeBaseName();
    bool inSequence = false;
    bool seenSequence = false;
    int depthSequence = 0;

    // First pass collects everything inside the first <sequence>.
    struct TrackData { bool audio; QVector<XmlClip> clips; QVector<QPair<Tick, Tick>> transitions; };
    QVector<TrackData> trackData;
    bool inVideo = false;
    bool inAudio = false;

    QStringList stack;
    XmlClip current;
    bool inClip = false;
    bool inFile = false;
    QString currentFileId;
    QString filePath;
    QString fileName;
    bool fileHasVideo = false;
    bool fileHasAudio = false;
    bool inTransition = false;
    Tick transStart = -1;
    Tick transEnd = -1;

    auto frames = [&](const QString& text) -> Tick {
        const FrameRate r = ntsc ? FrameRate{timebase * 1000, 1001} : FrameRate{timebase, 1};
        return r.ticksForFrames(text.toLongLong());
    };

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString n = xml.name().toString();
            stack << n;
            if (n == QLatin1String("sequence") && !seenSequence) {
                inSequence = true;
                seenSequence = true;
                depthSequence = stack.size();
            }
            if (!inSequence) continue;
            const QString parent = stack.size() >= 2 ? stack[stack.size() - 2] : QString();

            if (n == QLatin1String("video") && parent == QLatin1String("media")) inVideo = true;
            else if (n == QLatin1String("audio") && parent == QLatin1String("media")) inAudio = true;
            else if (n == QLatin1String("track") && (inVideo || inAudio) && !inClip) trackData.push_back({inAudio, {}, {}});
            else if (n == QLatin1String("clipitem") && !trackData.isEmpty()) {
                inClip = true;
                current = XmlClip{};
                const QString enabled = xml.attributes().value(QStringLiteral("enabled")).toString();
                Q_UNUSED(enabled);
            } else if (n == QLatin1String("transitionitem")) {
                inTransition = true;
                transStart = transEnd = -1;
            } else if (n == QLatin1String("file") && inClip) {
                currentFileId = xml.attributes().value(QStringLiteral("id")).toString();
                current.fileId = currentFileId;
                inFile = true;
                filePath.clear();
                fileName.clear();
                fileHasVideo = fileHasAudio = false;
            } else if (inFile && n == QLatin1String("video") && parent == QLatin1String("media")) {
                fileHasVideo = true;
            } else if (inFile && n == QLatin1String("audio") && parent == QLatin1String("media")) {
                fileHasAudio = true;
            }
        } else if (xml.isCharacters() && !xml.isWhitespace() && inSequence) {
            const QString n = stack.isEmpty() ? QString() : stack.last();
            const QString parent = stack.size() >= 2 ? stack[stack.size() - 2] : QString();
            const QString text = xml.text().toString().trimmed();

            if (n == QLatin1String("timebase") && parent == QLatin1String("rate") && stack.size() == depthSequence + 2) timebase = text.toInt();
            else if (n == QLatin1String("ntsc") && parent == QLatin1String("rate") && stack.size() == depthSequence + 2) ntsc = text.compare(QLatin1String("TRUE"), Qt::CaseInsensitive) == 0;
            else if (n == QLatin1String("name") && stack.size() == depthSequence + 1) seqName = text;
            else if (n == QLatin1String("width") && parent == QLatin1String("samplecharacteristics") && !inClip) width = text.toInt();
            else if (n == QLatin1String("height") && parent == QLatin1String("samplecharacteristics") && !inClip) height = text.toInt();
            else if (inFile) {
                if (n == QLatin1String("pathurl")) filePath = pathFromUrl(text);
                else if (n == QLatin1String("name") && parent == QLatin1String("file")) fileName = text;
            } else if (inClip && parent == QLatin1String("clipitem")) {
                if (n == QLatin1String("name")) current.name = text;
                else if (n == QLatin1String("start")) current.start = text.toLongLong() < 0 ? -1 : frames(text);
                else if (n == QLatin1String("end")) current.end = text.toLongLong() < 0 ? -1 : frames(text);
                else if (n == QLatin1String("in")) current.in = frames(text);
                else if (n == QLatin1String("out")) current.out = frames(text);
                else if (n == QLatin1String("enabled")) current.enabled = text.compare(QLatin1String("FALSE"), Qt::CaseInsensitive) != 0;
            } else if (inClip && n == QLatin1String("value") && stack.contains(QStringLiteral("effect"))) {
                // Time Remap / speed parameter (percent).
                if (stack.contains(QStringLiteral("parameter"))) {
                    bool ok = false;
                    const double v = text.toDouble(&ok);
                    if (ok && v > 0 && v != 100.0 && stack.contains(QStringLiteral("filter"))) current.speed = v / 100.0;
                }
            } else if (inTransition && parent == QLatin1String("transitionitem")) {
                if (n == QLatin1String("start")) transStart = frames(text);
                else if (n == QLatin1String("end")) transEnd = frames(text);
            }
        } else if (xml.isEndElement()) {
            const QString n = xml.name().toString();
            if (inSequence) {
                if (n == QLatin1String("file") && inFile) {
                    inFile = false;
                    if (!currentFileId.isEmpty() && (!filePath.isEmpty() || !fileName.isEmpty())) {
                        fileStreams[currentFileId] = {fileHasVideo, fileHasAudio};
                        fileIdToMedia[currentFileId] = table.idFor(filePath, fileName, fileHasVideo, fileHasAudio);
                    }
                } else if (n == QLatin1String("clipitem") && inClip) {
                    inClip = false;
                    if (!trackData.isEmpty()) trackData.last().clips.push_back(current);
                } else if (n == QLatin1String("transitionitem")) {
                    inTransition = false;
                    if (!trackData.isEmpty() && transStart >= 0 && transEnd > transStart)
                        trackData.last().transitions.push_back({transStart, transEnd});
                } else if (n == QLatin1String("video") && inVideo && stack.size() >= 2 && stack[stack.size() - 2] == QLatin1String("media") && !inFile) {
                    inVideo = false;
                } else if (n == QLatin1String("audio") && inAudio && stack.size() >= 2 && stack[stack.size() - 2] == QLatin1String("media") && !inFile) {
                    inAudio = false;
                } else if (n == QLatin1String("sequence") && stack.size() == depthSequence) {
                    inSequence = false;
                }
            }
            if (!stack.isEmpty()) stack.removeLast();
        }
    }
    if (xml.hasError()) {
        if (error) *error = QStringLiteral("XML error: %1 (line %2)").arg(xml.errorString()).arg(xml.lineNumber());
        return false;
    }
    if (!seenSequence) {
        if (error) *error = QStringLiteral("No <sequence> found - is this a Final Cut Pro 7 XML file?");
        return false;
    }

    const FrameRate rate = ntsc ? FrameRate{timebase * 1000, 1001} : FrameRate{timebase, 1};
    out = emptySequence(seqName, width, height, rate);

    int videoLane = 0;
    int audioLane = 0;
    QHash<QString, QString> linkByKey;   // fileId + start -> linkId
    for (const auto& td : trackData) {
        const int lane = td.audio ? ++audioLane : ++videoLane;
        const int ti = ensureLane(out, td.audio ? TrackKind::Audio : TrackKind::Video, lane);
        Tick previousEnd = 0;
        for (const auto& xc : td.clips) {
            Tick start = xc.start;
            Tick end = xc.end;
            // -1 means the edge sits inside a transition; use the transition's centre.
            if (start < 0) {
                start = previousEnd;
                for (const auto& tr : td.transitions)
                    if (tr.second >= previousEnd && tr.first <= previousEnd + rate.ticksPerFrame()) start = (tr.first + tr.second) / 2;
            }
            if (end < 0) {
                end = start + std::max<Tick>(rate.ticksPerFrame(), xc.out - xc.in);
                for (const auto& tr : td.transitions)
                    if (tr.first <= end && tr.second >= end - rate.ticksPerFrame()) end = (tr.first + tr.second) / 2;
            }
            if (end <= start) continue;
            Clip c;
            c.id = makeId(QStringLiteral("clip"));
            c.mediaId = fileIdToMedia.value(xc.fileId);
            if (c.mediaId.isEmpty()) {
                if (warnings) *warnings << QStringLiteral("Clip \"%1\" references unknown media; skipped").arg(xc.name);
                continue;
            }
            c.label = xc.name;
            c.start = start;
            c.duration = end - start;
            c.sourceIn = xc.in;
            c.enabled = xc.enabled;
            c.speed = xc.speed;
            const QString key = xc.fileId + QLatin1Char('@') + QString::number(start);
            if (!linkByKey.contains(key)) linkByKey.insert(key, makeId(QStringLiteral("link")));
            c.linkId = linkByKey.value(key);
            for (const auto& tr : td.transitions)
                if (tr.first <= start && tr.second >= start && tr.first < start) c.inTransition = {TransitionType::CrossDissolve, tr.second - tr.first};
            out.tracks[ti].clips.push_back(c);
            previousEnd = end;
        }
    }

    // A link group with a single member is not a link.
    QHash<QString, int> linkCount;
    for (const auto& t : out.tracks)
        for (const auto& c : t.clips) linkCount[c.linkId]++;
    for (auto& t : out.tracks)
        for (auto& c : t.clips)
            if (linkCount.value(c.linkId) < 2) c.linkId.clear();

    ensureMinimumTracks(out);
    finish(out);
    return true;
}

// --------------------------------------------------------------------- EDL

bool importEdl(const QString& path, Sequence& out, QVector<MediaRef>& media, QStringList* warnings, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream in(&file);
    const QStringList lines = in.readAll().split(QLatin1Char('\n'));

    bool dropFrame = false;
    QString title = QFileInfo(path).completeBaseName();
    for (const QString& l : lines) {
        if (l.startsWith(QStringLiteral("TITLE:"))) title = l.mid(6).trimmed();
        if (l.startsWith(QStringLiteral("FCM:")) && l.contains(QStringLiteral("DROP")) && !l.contains(QStringLiteral("NON")))
            dropFrame = true;
    }
    const FrameRate rate = dropFrame ? FrameRate{30000, 1001} : FrameRate{25, 1};
    if (warnings) *warnings << QStringLiteral("EDLs carry no frame rate; assumed %1 fps. Change it in Sequence Settings if needed.").arg(rate.label());
    out = emptySequence(title, 1920, 1080, rate);

    const int fps = static_cast<int>(std::lround(rate.value()));
    auto tc = [&](const QString& s) -> Tick {
        const QStringList p = s.split(QRegularExpression(QStringLiteral("[:;.]")));
        if (p.size() != 4) return 0;
        const qint64 f = ((p[0].toLongLong() * 60 + p[1].toLongLong()) * 60 + p[2].toLongLong()) * fps + p[3].toLongLong();
        return rate.ticksForFrames(f);
    };

    static const QRegularExpression eventRe(
        QStringLiteral("^(\\d{3,6})\\s+(\\S+)\\s+(\\S+)\\s+(C|D|W\\d+)\\s*(\\d+)?\\s+(\\d\\d:\\d\\d:\\d\\d[:;]\\d\\d)\\s+(\\d\\d:\\d\\d:\\d\\d[:;]\\d\\d)\\s+(\\d\\d:\\d\\d:\\d\\d[:;]\\d\\d)\\s+(\\d\\d:\\d\\d:\\d\\d[:;]\\d\\d)"));

    MediaTable table{media, {}};
    struct Event { QString reel; QString channels; Tick srcIn, srcOut, recIn, recOut; QString clipName; Tick dissolve = 0; };
    QVector<Event> events;
    Tick firstRec = -1;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        const auto m = eventRe.match(line);
        if (m.hasMatch()) {
            Event e;
            e.reel = m.captured(2);
            e.channels = m.captured(3);
            if (m.captured(4) == QLatin1String("D")) e.dissolve = rate.ticksForFrames(m.captured(5).toInt());
            e.srcIn = tc(m.captured(6));
            e.srcOut = tc(m.captured(7));
            e.recIn = tc(m.captured(8));
            e.recOut = tc(m.captured(9));
            if (firstRec < 0 || e.recIn < firstRec) firstRec = e.recIn;
            events.push_back(e);
        } else if (line.startsWith(QStringLiteral("* FROM CLIP NAME:")) && !events.isEmpty()) {
            events.last().clipName = line.mid(17).trimmed();
        }
    }
    if (events.isEmpty()) {
        if (error) *error = QStringLiteral("No edit events found in the EDL");
        return false;
    }

    // Record timecodes usually start at 01:00:00:00; rebase to zero.
    const QDir dir = QFileInfo(path).absoluteDir();
    for (const auto& e : events) {
        if (e.reel == QLatin1String("BL") || e.reel == QLatin1String("BLACK")) continue;
        const QString name = e.clipName.isEmpty() ? e.reel : e.clipName;
        const QString candidate = dir.filePath(name);
        const bool video = e.channels.contains(QLatin1Char('V'));
        const bool audio = e.channels.contains(QLatin1Char('A'));
        const QString mediaId = table.idFor(QFileInfo::exists(candidate) ? candidate : QString(), name, video, audio);
        const QString link = makeId(QStringLiteral("link"));
        auto add = [&](TrackKind kind, int lane) {
            const int ti = ensureLane(out, kind, lane);
            Clip c;
            c.id = makeId(QStringLiteral("clip"));
            c.mediaId = mediaId;
            c.label = name;
            c.start = e.recIn - firstRec;
            c.duration = e.recOut - e.recIn;
            c.sourceIn = e.srcIn;
            c.linkId = (video && audio) ? link : QString();
            if (e.dissolve > 0) c.inTransition = {TransitionType::CrossDissolve, e.dissolve};
            if (c.duration > 0) out.tracks[ti].clips.push_back(c);
        };
        if (video) add(TrackKind::Video, 1);
        if (audio) {
            add(TrackKind::Audio, 1);
            if (e.channels.contains(QStringLiteral("A2")) || e.channels.contains(QStringLiteral("AA"))) add(TrackKind::Audio, 2);
        }
    }
    for (auto& m : media)
        if (m.path.isEmpty() && warnings) *warnings << QStringLiteral("Media for \"%1\" not found next to the EDL; relink it in the Project panel").arg(m.name);

    ensureMinimumTracks(out);
    finish(out);
    return true;
}

} // namespace

bool importTimeline(const QString& path, Sequence& out, QVector<MediaRef>& media, QStringList* warnings, QString* error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("otio")) return importOtio(path, out, media, warnings, error);
    if (suffix == QLatin1String("edl")) return importEdl(path, out, media, warnings, error);
    if (suffix == QLatin1String("xml")) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            const QByteArray head = f.read(4096);
            if (head.contains("<fcpxml")) {
                if (error) *error = QStringLiteral("FCPXML (Final Cut Pro X) import is not supported yet; export FCP 7 XML or OTIO instead.");
                return false;
            }
        }
        return importFcp7(path, out, media, warnings, error);
    }
    if (error) *error = QStringLiteral("Unsupported timeline format: .%1").arg(suffix);
    return false;
}

} // namespace bc::io
