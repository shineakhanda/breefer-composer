// Best-effort importer for Adobe Premiere Pro .prproj files.
//
// A .prproj is gzip-compressed XML. Top-level elements are objects addressed by
// ObjectID (referenced with ObjectRef) or ObjectUID (referenced with ObjectURef).
// The chain we follow, as written by Premiere Pro CC 2017 - 2025:
//
//   Sequence -> TrackGroups/TrackGroup/Second@ObjectRef -> VideoTrackGroup | AudioTrackGroup
//     -> TrackGroup/Tracks/Track@ObjectURef -> VideoClipTrack | AudioClipTrack
//     -> ClipTrack/ClipItems/TrackItems/TrackItem@ObjectRef -> VideoClipTrackItem | AudioClipTrackItem
//     -> ClipTrackItem/TrackItem/{Start,End} and ClipTrackItem/SubClip@ObjectRef -> SubClip
//     -> Clip@ObjectRef -> VideoClip | AudioClip -> Clip/{InPoint,PlaybackSpeed,Source@ObjectRef}
//     -> VideoMediaSource | AudioMediaSource -> MediaSource/Media@ObjectURef -> Media
//     -> ActualMediaFilePath | FilePath
//
// Times are in Premiere ticks, 254016000000 per second -- the same timebase
// Breefer uses internally, so positions map 1:1. The format is undocumented and
// changes between versions; anything we cannot map is reported as a warning.

#include "io/Interchange.h"
#include "io/Inflate.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>
#include <memory>
#include <vector>

namespace bc::io {
namespace {

struct Node {
    QString tag;
    QString text;
    QHash<QString, QString> attrs;
    std::vector<std::unique_ptr<Node>> children;

    const Node* child(const QString& name) const
    {
        for (const auto& c : children)
            if (c->tag == name) return c.get();
        return nullptr;
    }
    // Slash path: "ClipTrackItem/TrackItem/Start".
    const Node* path(const QString& p) const
    {
        const Node* n = this;
        for (const QString& part : p.split(QLatin1Char('/'))) {
            if (!n) return nullptr;
            n = n->child(part);
        }
        return n;
    }
    QString value(const QString& p) const
    {
        const Node* n = path(p);
        return n ? n->text.trimmed() : QString();
    }
    QList<const Node*> all(const QString& name) const
    {
        QList<const Node*> out;
        for (const auto& c : children)
            if (c->tag == name) out << c.get();
        return out;
    }
};

struct Graph {
    std::unique_ptr<Node> root;
    QHash<QString, const Node*> byId;
    QHash<QString, const Node*> byUid;

    const Node* resolve(const Node* reference) const
    {
        if (!reference) return nullptr;
        const QString ref = reference->attrs.value(QStringLiteral("ObjectRef"));
        if (!ref.isEmpty()) return byId.value(ref);
        const QString uref = reference->attrs.value(QStringLiteral("ObjectURef"));
        if (!uref.isEmpty()) return byUid.value(uref);
        return nullptr;
    }
};

bool parse(const QByteArray& xml, Graph& g, QString* error)
{
    QXmlStreamReader reader(xml);
    std::vector<Node*> stack;
    g.root = std::make_unique<Node>();
    g.root->tag = QStringLiteral("#document");
    stack.push_back(g.root.get());

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            auto node = std::make_unique<Node>();
            node->tag = reader.name().toString();
            for (const auto& a : reader.attributes()) {
                const QString name = a.name().toString();
                if (name.startsWith(QLatin1String("Object")) || name == QLatin1String("Index"))
                    node->attrs.insert(name, a.value().toString());
            }
            Node* raw = node.get();
            stack.back()->children.push_back(std::move(node));
            stack.push_back(raw);
            // Only top-level objects are addressable.
            if (stack.size() == 3) {
                const QString id = raw->attrs.value(QStringLiteral("ObjectID"));
                const QString uid = raw->attrs.value(QStringLiteral("ObjectUID"));
                if (!id.isEmpty()) g.byId.insert(id, raw);
                if (!uid.isEmpty()) g.byUid.insert(uid, raw);
            }
        } else if (reader.isCharacters() && !reader.isWhitespace()) {
            stack.back()->text += reader.text();
        } else if (reader.isEndElement()) {
            if (stack.size() > 1) stack.pop_back();
        }
    }
    if (reader.hasError()) {
        if (error) *error = QStringLiteral("XML error at line %1: %2").arg(reader.lineNumber()).arg(reader.errorString());
        return false;
    }
    return true;
}

Tick ticks(const QString& text)
{
    bool ok = false;
    const qint64 v = text.toLongLong(&ok);
    return ok ? static_cast<Tick>(v) : -1;
}

} // namespace

bool importPremiereProject(const QString& path, QVector<Sequence>& sequences, QVector<MediaRef>& media,
                           QStringList* warnings, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray raw = file.readAll();
    QByteArray xml;
    if (raw.startsWith("\x1f\x8b")) {
        if (!gunzip(raw, xml, error)) return false;
    } else {
        xml = raw;   // already uncompressed
    }
    if (!xml.contains("<PremiereData")) {
        if (error) *error = QStringLiteral("Not a Premiere Pro project (no <PremiereData> root)");
        return false;
    }

    Graph g;
    if (!parse(xml, g, error)) return false;
    const Node* premiere = g.root->child(QStringLiteral("PremiereData"));
    if (!premiere) {
        if (error) *error = QStringLiteral("Missing <PremiereData>");
        return false;
    }

    // Media, deduplicated by path.
    QHash<const Node*, QString> mediaIdFor;
    QHash<QString, QString> mediaByPath;
    auto mediaFor = [&](const Node* mediaNode, bool video) -> QString {
        if (!mediaNode) return {};
        auto it = mediaIdFor.find(mediaNode);
        if (it != mediaIdFor.end()) {
            for (auto& m : media)
                if (m.id == it.value()) (video ? m.hasVideo : m.hasAudio) = true;
            return it.value();
        }
        QString filePath = mediaNode->value(QStringLiteral("ActualMediaFilePath"));
        if (filePath.isEmpty()) filePath = mediaNode->value(QStringLiteral("FilePath"));
        const QString key = filePath.toLower();
        QString id = mediaByPath.value(key);
        if (id.isEmpty()) {
            MediaRef m;
            m.id = makeId(QStringLiteral("media"));
            m.path = filePath;
            m.name = mediaNode->value(QStringLiteral("Title"));
            if (m.name.isEmpty()) m.name = QFileInfo(filePath).fileName();
            m.hasVideo = video;
            m.hasAudio = !video;
            media.push_back(m);
            id = m.id;
            mediaByPath.insert(key, id);
        }
        mediaIdFor.insert(mediaNode, id);
        return id;
    };

    int skippedTransitions = 0;
    int skippedOther = 0;

    for (const Node* seqNode : premiere->all(QStringLiteral("Sequence"))) {
        if (seqNode->attrs.value(QStringLiteral("ObjectUID")).isEmpty()) continue;
        const Node* groups = seqNode->child(QStringLiteral("TrackGroups"));
        if (!groups) continue;

        Sequence seq;
        seq.id = makeId(QStringLiteral("seq"));
        seq.name = seqNode->value(QStringLiteral("Name"));
        if (seq.name.isEmpty()) seq.name = QStringLiteral("Premiere Sequence %1").arg(sequences.size() + 1);

        int videoLane = 0;
        int audioLane = 0;
        QHash<QString, QString> linkKey;   // media@start/end -> link id

        for (const Node* group : groups->all(QStringLiteral("TrackGroup"))) {
            const Node* trackGroup = g.resolve(group->child(QStringLiteral("Second")));
            if (!trackGroup) continue;
            const bool video = trackGroup->tag == QLatin1String("VideoTrackGroup");
            const bool audio = trackGroup->tag == QLatin1String("AudioTrackGroup");
            if (!video && !audio) continue;

            if (video) {
                const Tick frameTicks = ticks(trackGroup->value(QStringLiteral("TrackGroup/FrameRate")));
                if (frameTicks > 0) seq.rate = FrameRate::fromDouble(static_cast<double>(kTicksPerSecond) / frameTicks);
                const QString rect = trackGroup->value(QStringLiteral("FrameRect"));
                const QStringList parts = rect.split(QLatin1Char(','));
                if (parts.size() == 4 && parts[2].toInt() > 0) {
                    seq.width = parts[2].toInt();
                    seq.height = parts[3].toInt();
                }
            }

            const Node* tracks = trackGroup->path(QStringLiteral("TrackGroup/Tracks"));
            if (!tracks) continue;
            for (const Node* trackRef : tracks->all(QStringLiteral("Track"))) {
                const Node* trackNode = g.resolve(trackRef);
                if (!trackNode) continue;
                Track track;
                track.kind = video ? TrackKind::Video : TrackKind::Audio;
                track.id = makeId(video ? QStringLiteral("vt") : QStringLiteral("at"));
                const int lane = video ? ++videoLane : ++audioLane;
                track.name = (video ? QStringLiteral("V") : QStringLiteral("A")) + QString::number(lane);

                const Node* items = trackNode->path(QStringLiteral("ClipTrack/ClipItems/TrackItems"));
                if (items) {
                    for (const Node* itemRef : items->all(QStringLiteral("TrackItem"))) {
                        const Node* item = g.resolve(itemRef);
                        if (!item) continue;
                        if (item->tag.contains(QLatin1String("Transition"))) {
                            ++skippedTransitions;
                            continue;
                        }
                        const Node* cti = item->child(QStringLiteral("ClipTrackItem"));
                        if (!cti) { ++skippedOther; continue; }
                        const Tick start = ticks(cti->value(QStringLiteral("TrackItem/Start")));
                        const Tick end = ticks(cti->value(QStringLiteral("TrackItem/End")));
                        if (start < 0 || end <= start) continue;

                        const Node* subClip = g.resolve(cti->child(QStringLiteral("SubClip")));
                        const Node* clipNode = subClip ? g.resolve(subClip->child(QStringLiteral("Clip"))) : nullptr;
                        const Node* clipBody = clipNode ? clipNode->child(QStringLiteral("Clip")) : nullptr;
                        const Node* source = clipBody ? g.resolve(clipBody->child(QStringLiteral("Source"))) : nullptr;
                        const Node* mediaNode = source ? g.resolve(source->path(QStringLiteral("MediaSource/Media"))) : nullptr;
                        if (!mediaNode) {
                            // Graphics, nests and synthetic media have no file.
                            ++skippedOther;
                            continue;
                        }

                        Clip c;
                        c.id = makeId(QStringLiteral("clip"));
                        c.mediaId = mediaFor(mediaNode, video);
                        c.start = start;
                        c.duration = end - start;
                        c.sourceIn = std::max<Tick>(0, ticks(clipBody->value(QStringLiteral("InPoint"))));
                        bool ok = false;
                        const double speed = clipBody->value(QStringLiteral("PlaybackSpeed")).toDouble(&ok);
                        if (ok && speed != 0.0) {
                            c.speed = std::abs(speed);
                            c.reverse = speed < 0;
                        }
                        c.label = subClip->value(QStringLiteral("Name"));
                        const QString key = c.mediaId + QLatin1Char('@') + QString::number(start) + QLatin1Char('-') + QString::number(end);
                        if (!linkKey.contains(key)) linkKey.insert(key, makeId(QStringLiteral("link")));
                        c.linkId = linkKey.value(key);
                        track.clips.push_back(c);
                    }
                }
                track.sortClips();
                // Keep the non-overlap invariant.
                for (int i = 1; i < track.clips.size(); ++i)
                    if (track.clips[i].start < track.clips[i - 1].end())
                        track.clips[i - 1].duration = std::max<Tick>(1, track.clips[i].start - track.clips[i - 1].start);
                if (video) seq.tracks.insert(0, track);
                else seq.tracks.push_back(track);
            }
        }

        if (seq.tracks.isEmpty()) continue;
        // Single-member link groups are not links.
        QHash<QString, int> count;
        for (const auto& t : seq.tracks)
            for (const auto& c : t.clips) count[c.linkId]++;
        for (auto& t : seq.tracks)
            for (auto& c : t.clips)
                if (count.value(c.linkId) < 2) c.linkId.clear();
        if (seq.videoTrackCount() == 0) {
            Track t; t.kind = TrackKind::Video; t.id = makeId(QStringLiteral("vt")); t.name = QStringLiteral("V1");
            seq.tracks.insert(0, t);
        }
        if (seq.audioTrackCount() == 0) {
            Track t; t.kind = TrackKind::Audio; t.id = makeId(QStringLiteral("at")); t.name = QStringLiteral("A1");
            seq.tracks.push_back(t);
        }
        seq.recomputeDuration();
        sequences.push_back(seq);
    }

    if (sequences.isEmpty()) {
        if (error) *error = QStringLiteral("No sequences found. This Premiere version may store them differently; "
                                           "in Premiere use File > Export > Final Cut Pro XML and import that instead.");
        return false;
    }
    if (warnings) {
        if (skippedTransitions) *warnings << QStringLiteral("%1 transition(s) were not imported.").arg(skippedTransitions);
        if (skippedOther) *warnings << QStringLiteral("%1 item(s) without media (graphics, nests, adjustment layers) were skipped.").arg(skippedOther);
        *warnings << QStringLiteral("Effects, keyframes and audio levels are not read from .prproj. "
                                    "For a fuller transfer, export Final Cut Pro XML from Premiere.");
    }
    return true;
}

} // namespace bc::io
