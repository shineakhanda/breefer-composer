#include "io/ProjectIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace bc::io {
namespace {

QJsonObject keyframeToJson(const Keyframe& k)
{
    QJsonObject o;
    o["time"] = static_cast<double>(k.time);
    o["value"] = k.value;
    if (k.interp != KeyInterp::Linear) o["interp"] = static_cast<int>(k.interp);
    return o;
}

Keyframe keyframeFromJson(const QJsonObject& o)
{
    Keyframe k;
    k.time = static_cast<Tick>(o["time"].toDouble());
    k.value = o["value"].toDouble();
    k.interp = static_cast<KeyInterp>(o["interp"].toInt(0));
    return k;
}

QJsonObject paramToJson(const Param& p)
{
    QJsonObject o;
    o["id"] = p.id;
    o["name"] = p.name;
    o["value"] = p.value;
    o["min"] = p.minValue;
    o["max"] = p.maxValue;
    QJsonArray keys;
    for (const auto& k : p.keys) keys.append(keyframeToJson(k));
    o["keys"] = keys;
    return o;
}

Param paramFromJson(const QJsonObject& o)
{
    Param p;
    p.id = o["id"].toString();
    p.name = o["name"].toString();
    p.value = o["value"].toDouble(1.0);
    p.minValue = o["min"].toDouble(0.0);
    p.maxValue = o["max"].toDouble(1.0);
    for (const auto& v : o["keys"].toArray()) p.keys.push_back(keyframeFromJson(v.toObject()));
    return p;
}

QJsonObject effectToJson(const EffectInstance& e)
{
    QJsonObject o;
    o["id"] = e.id;
    o["type"] = e.type;
    o["name"] = e.name;
    o["enabled"] = e.enabled;
    QJsonArray params;
    for (const auto& p : e.params) params.append(paramToJson(p));
    o["params"] = params;
    return o;
}

EffectInstance effectFromJson(const QJsonObject& o)
{
    EffectInstance e;
    e.id = o["id"].toString();
    e.type = o["type"].toString();
    e.name = o["name"].toString();
    e.enabled = o["enabled"].toBool(true);
    for (const auto& v : o["params"].toArray()) e.params.push_back(paramFromJson(v.toObject()));
    return e;
}

QJsonObject colorToJson(const QColor& c) { return QJsonObject{{"argb", c.name(QColor::HexArgb)}}; }
QColor colorFromJson(const QJsonValue& v, const QColor& fallback)
{
    const QString s = v.toObject()["argb"].toString();
    return s.isEmpty() ? fallback : QColor(s);
}

QJsonObject transitionToJson(const Transition& t)
{
    return QJsonObject{{"type", static_cast<int>(t.type)}, {"duration", static_cast<double>(t.duration)}};
}
Transition transitionFromJson(const QJsonValue& v)
{
    Transition t;
    const QJsonObject o = v.toObject();
    t.type = static_cast<TransitionType>(o["type"].toInt(0));
    t.duration = static_cast<Tick>(o["duration"].toDouble());
    return t;
}

QJsonObject titleToJson(const TitleSpec& t)
{
    QJsonObject o;
    o["text"] = t.text;
    o["font"] = t.fontFamily;
    o["size"] = t.fontSize;
    o["bold"] = t.bold;
    o["italic"] = t.italic;
    o["color"] = colorToJson(t.color);
    o["align"] = t.align;
    o["x"] = t.posX;
    o["y"] = t.posY;
    o["background"] = t.background;
    o["backgroundColor"] = colorToJson(t.backgroundColor);
    o["shadow"] = t.shadow;
    o["outline"] = t.outline;
    o["outlineColor"] = colorToJson(t.outlineColor);
    return o;
}
TitleSpec titleFromJson(const QJsonObject& o)
{
    TitleSpec t;
    t.text = o["text"].toString(t.text);
    t.fontFamily = o["font"].toString(t.fontFamily);
    t.fontSize = o["size"].toDouble(t.fontSize);
    t.bold = o["bold"].toBool(t.bold);
    t.italic = o["italic"].toBool(t.italic);
    t.color = colorFromJson(o["color"], t.color);
    t.align = o["align"].toInt(t.align);
    t.posX = o["x"].toDouble(t.posX);
    t.posY = o["y"].toDouble(t.posY);
    t.background = o["background"].toBool(t.background);
    t.backgroundColor = colorFromJson(o["backgroundColor"], t.backgroundColor);
    t.shadow = o["shadow"].toBool(t.shadow);
    t.outline = o["outline"].toDouble(t.outline);
    t.outlineColor = colorFromJson(o["outlineColor"], t.outlineColor);
    return t;
}

QJsonObject clipToJson(const Clip& c)
{
    QJsonObject o;
    o["id"] = c.id;
    o["mediaId"] = c.mediaId;
    o["start"] = static_cast<double>(c.start);
    o["duration"] = static_cast<double>(c.duration);
    o["sourceIn"] = static_cast<double>(c.sourceIn);
    o["speed"] = c.speed;
    o["muted"] = c.muted;
    o["label"] = c.label;
    o["reverse"] = c.reverse;
    o["enabled"] = c.enabled;
    o["labelColor"] = c.labelColor;
    o["linkId"] = c.linkId;
    o["gainDb"] = c.gainDb;
    o["generator"] = static_cast<int>(c.generator);
    if (c.generator == GeneratorKind::Title) o["title"] = titleToJson(c.title);
    if (c.generator == GeneratorKind::ColorMatte) o["matte"] = colorToJson(c.matteColor);
    if (c.generator == GeneratorKind::Nest) o["nested"] = c.nestedSequenceId;
    if (c.generator == GeneratorKind::Model3D) o["model"] = c.modelPath;
    if (c.inTransition.active()) o["in"] = transitionToJson(c.inTransition);
    if (c.outTransition.active()) o["out"] = transitionToJson(c.outTransition);
    QJsonArray effects;
    for (const auto& e : c.effects) effects.append(effectToJson(e));
    o["effects"] = effects;
    return o;
}

Clip clipFromJson(const QJsonObject& o)
{
    Clip c;
    c.id = o["id"].toString();
    c.mediaId = o["mediaId"].toString();
    c.start = static_cast<Tick>(o["start"].toDouble());
    c.duration = static_cast<Tick>(o["duration"].toDouble());
    c.sourceIn = static_cast<Tick>(o["sourceIn"].toDouble());
    c.speed = o["speed"].toDouble(1.0);
    c.muted = o["muted"].toBool(false);
    c.label = o["label"].toString();
    c.reverse = o["reverse"].toBool(false);
    c.enabled = o["enabled"].toBool(true);
    c.labelColor = o["labelColor"].toInt(-1);
    c.linkId = o["linkId"].toString();
    c.gainDb = o["gainDb"].toDouble(0.0);
    c.generator = static_cast<GeneratorKind>(o["generator"].toInt(0));
    if (c.generator == GeneratorKind::Title) c.title = titleFromJson(o["title"].toObject());
    if (c.generator == GeneratorKind::ColorMatte) c.matteColor = colorFromJson(o["matte"], c.matteColor);
    c.nestedSequenceId = o["nested"].toString();
    c.modelPath = o["model"].toString();
    c.inTransition = transitionFromJson(o["in"]);
    c.outTransition = transitionFromJson(o["out"]);
    c.effects.clear();
    for (const auto& v : o["effects"].toArray()) c.effects.push_back(effectFromJson(v.toObject()));

    // Migration: early builds stored opacity as 0..1 instead of 0..100.
    for (auto& effect : c.effects) {
        if (effect.type != QLatin1String("opacity")) continue;
        for (auto& p : effect.params)
            if (p.id == QLatin1String("amount")) {
                if (p.value <= 1.0) p.value *= 100.0;
                if (p.maxValue <= 1.0) p.maxValue = 100.0;
                for (auto& k : p.keys)
                    if (k.value <= 1.0) k.value *= 100.0;
            }
    }
    return c;
}

QJsonObject trackToJson(const Track& t)
{
    QJsonObject o;
    o["id"] = t.id;
    o["name"] = t.name;
    o["kind"] = t.kind == TrackKind::Video ? "video" : "audio";
    o["muted"] = t.muted;
    o["solo"] = t.solo;
    o["locked"] = t.locked;
    o["syncLock"] = t.syncLock;
    o["targeted"] = t.targeted;
    o["volumeDb"] = t.volumeDb;
    o["pan"] = t.pan;
    o["height"] = t.height;
    QJsonArray clips;
    for (const auto& c : t.clips) clips.append(clipToJson(c));
    o["clips"] = clips;
    return o;
}

Track trackFromJson(const QJsonObject& o)
{
    Track t;
    t.id = o["id"].toString();
    t.name = o["name"].toString();
    t.kind = o["kind"].toString() == "audio" ? TrackKind::Audio : TrackKind::Video;
    t.muted = o["muted"].toBool(false);
    t.solo = o["solo"].toBool(false);
    t.locked = o["locked"].toBool(false);
    t.syncLock = o["syncLock"].toBool(true);
    t.targeted = o["targeted"].toBool(true);
    t.volumeDb = o["volumeDb"].toDouble(0.0);
    t.pan = o["pan"].toDouble(0.0);
    t.height = o["height"].toInt(0);
    for (const auto& v : o["clips"].toArray()) t.clips.push_back(clipFromJson(v.toObject()));
    t.sortClips();
    return t;
}

QJsonObject sequenceToJson(const Sequence& s)
{
    QJsonObject o;
    o["id"] = s.id;
    o["name"] = s.name;
    o["width"] = s.width;
    o["height"] = s.height;
    o["rateNum"] = s.rate.num;
    o["rateDen"] = s.rate.den;
    o["patchVideo"] = s.patchVideo;
    o["patchAudio"] = s.patchAudio;
    o["in"] = static_cast<double>(s.inPoint);
    o["out"] = static_cast<double>(s.outPoint);
    QJsonArray markers;
    for (const auto& m : s.markers) {
        markers.append(QJsonObject{{"id", m.id}, {"time", static_cast<double>(m.time)},
                                   {"duration", static_cast<double>(m.duration)},
                                   {"name", m.name}, {"comment", m.comment}, {"color", m.color}});
    }
    o["markers"] = markers;
    QJsonArray tracks;
    for (const auto& t : s.tracks) tracks.append(trackToJson(t));
    o["tracks"] = tracks;
    return o;
}

Sequence sequenceFromJson(const QJsonObject& o)
{
    Sequence s;
    s.id = o["id"].toString();
    s.name = o["name"].toString();
    s.width = o["width"].toInt(1920);
    s.height = o["height"].toInt(1080);
    s.rate = FrameRate{o["rateNum"].toInt(25), o["rateDen"].toInt(1)};
    s.patchVideo = o["patchVideo"].toInt(1);
    s.patchAudio = o["patchAudio"].toInt(1);
    s.inPoint = static_cast<Tick>(o["in"].toDouble(-1));
    s.outPoint = static_cast<Tick>(o["out"].toDouble(-1));
    for (const auto& v : o["markers"].toArray()) {
        const QJsonObject mo = v.toObject();
        Marker m;
        m.id = mo["id"].toString();
        m.time = static_cast<Tick>(mo["time"].toDouble());
        m.duration = static_cast<Tick>(mo["duration"].toDouble());
        m.name = mo["name"].toString();
        m.comment = mo["comment"].toString();
        m.color = mo["color"].toInt(3);
        s.markers.push_back(m);
    }
    for (const auto& v : o["tracks"].toArray()) s.tracks.push_back(trackFromJson(v.toObject()));
    s.recomputeDuration();
    return s;
}

QJsonObject mediaToJson(const MediaRef& m)
{
    QJsonObject o;
    o["id"] = m.id;
    o["path"] = m.path;
    o["name"] = m.name;
    o["duration"] = static_cast<double>(m.duration);
    o["width"] = m.width;
    o["height"] = m.height;
    o["rateNum"] = m.rate.num;
    o["rateDen"] = m.rate.den;
    o["sampleRate"] = m.sampleRate;
    o["channels"] = m.channels;
    o["hasVideo"] = m.hasVideo;
    o["hasAudio"] = m.hasAudio;
    o["codec"] = m.codec;
    o["proxyPath"] = m.proxyPath;
    if (!m.bin.isEmpty()) o["bin"] = m.bin;
    if (!m.remotionEntry.isEmpty()) {
        o["remotionEntry"] = m.remotionEntry;
        o["remotionComposition"] = m.remotionComposition;
        o["remotionProps"] = m.remotionProps;
    }
    return o;
}

MediaRef mediaFromJson(const QJsonObject& o)
{
    MediaRef m;
    m.id = o["id"].toString();
    m.path = o["path"].toString();
    m.name = o["name"].toString();
    m.duration = static_cast<Tick>(o["duration"].toDouble());
    m.width = o["width"].toInt();
    m.height = o["height"].toInt();
    m.rate = FrameRate{o["rateNum"].toInt(25), o["rateDen"].toInt(1)};
    m.sampleRate = o["sampleRate"].toInt(48000);
    m.channels = o["channels"].toInt(2);
    m.hasVideo = o["hasVideo"].toBool();
    m.hasAudio = o["hasAudio"].toBool();
    m.codec = o["codec"].toString();
    m.proxyPath = o["proxyPath"].toString();
    m.bin = o["bin"].toString();
    m.remotionEntry = o["remotionEntry"].toString();
    m.remotionComposition = o["remotionComposition"].toString();
    m.remotionProps = o["remotionProps"].toString();
    return m;
}

} // namespace

bool saveProject(const Project& project, const QString& path, QString* error)
{
    QJsonObject root;
    root["format"] = "breefer-project";
    root["version"] = 2;
    root["id"] = project.id;
    root["name"] = project.name;

    root["bins"] = QJsonArray::fromStringList(project.bins);
    QJsonArray media;
    for (const auto& m : project.media) media.append(mediaToJson(m));
    root["media"] = media;

    QJsonArray sequences;
    for (const auto& s : project.sequences) sequences.append(sequenceToJson(s));
    root["sequences"] = sequences;

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool loadProject(const QString& path, Project& out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (doc.isNull() || !doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid project file: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    if (root["format"].toString() != QLatin1String("breefer-project")) {
        if (error) *error = QStringLiteral("Not a Breefer project file");
        return false;
    }

    Project p;
    p.id = root["id"].toString();
    p.name = root["name"].toString(QStringLiteral("Untitled Project"));
    p.filePath = path;

    for (const auto& v : root["bins"].toArray()) p.bins << v.toString();
    for (const auto& v : root["media"].toArray()) p.media.push_back(mediaFromJson(v.toObject()));

    // Make projects portable: relative media paths are relative to the project
    // file, and a missing absolute path is retried next to the project (the usual
    // result of copying a project folder to another machine).
    const QDir projectDir = QFileInfo(path).absoluteDir();
    for (auto& m : p.media) {
        if (m.path.isEmpty()) continue;
        if (QFileInfo(m.path).isRelative()) {
            m.path = QDir::cleanPath(projectDir.absoluteFilePath(m.path));
        } else if (!QFileInfo::exists(m.path)) {
            const QString beside = projectDir.filePath(QFileInfo(m.path).fileName());
            if (QFileInfo::exists(beside)) m.path = beside;
        }
        // Proxies are per-machine cache files: drop stale ones, they are regenerated.
        if (!m.proxyPath.isEmpty() && !QFileInfo::exists(m.proxyPath)) m.proxyPath.clear();
    }
    for (const auto& v : root["sequences"].toArray()) p.sequences.push_back(sequenceFromJson(v.toObject()));

    // v1 projects had no A/V links: link video/audio clips cut from the same
    // media at the same place, which is what the v1 insert produced.
    if (root["version"].toInt(1) < 2) {
        for (auto& seq : p.sequences) {
            for (auto& vt : seq.tracks) {
                if (vt.kind != TrackKind::Video) continue;
                for (auto& vc : vt.clips) {
                    if (!vc.linkId.isEmpty() || vc.mediaId.isEmpty()) continue;
                    for (auto& at : seq.tracks) {
                        if (at.kind != TrackKind::Audio) continue;
                        for (auto& ac : at.clips) {
                            if (!ac.linkId.isEmpty() || ac.mediaId != vc.mediaId) continue;
                            if (ac.start != vc.start || ac.duration != vc.duration || ac.sourceIn != vc.sourceIn) continue;
                            vc.linkId = ac.linkId = makeId(QStringLiteral("link"));
                        }
                    }
                }
            }
        }
    }

    if (p.sequences.isEmpty()) p.sequences.push_back(makeDefaultSequence());
    out = p;
    return true;
}

} // namespace bc::io
