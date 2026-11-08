#include "core/Document.h"

#include "io/ProjectIO.h"

#include <QDebug>
#include <QFileInfo>
#include <algorithm>

namespace bc {

Document::Document(QObject* parent)
    : QObject(parent)
{
    m_project = Project::createDefault();
    if (!m_project.sequences.isEmpty())
        m_sequenceId = m_project.sequences.first().id;
}

Sequence* Document::sequence()
{
    return m_project.sequenceById(m_sequenceId);
}

const Sequence* Document::sequence() const
{
    return m_project.sequenceById(m_sequenceId);
}

Sequence Document::snapshotSequence() const
{
    if (const Sequence* s = m_project.sequenceById(m_sequenceId))
        return *s;
    return Sequence{};
}

edit::Context Document::editContext() const
{
    edit::Context ctx;
    ctx.media = &m_project.media;
    ctx.frame = sequence() ? sequence()->rate.ticksPerFrame() : FrameRate{25, 1}.ticksPerFrame();
    return ctx;
}

bool Document::applyEdit(const QString& text, const std::function<bool(Sequence&)>& fn)
{
    Sequence* seq = sequence();
    if (!seq) return false;

    Sequence working = *seq;
    if (!fn(working)) return false;
    working.recomputeDuration();

    QString problem;
    if (!edit::validate(working, &problem)) {
        qWarning() << "Edit rejected:" << text << problem;
        return false;
    }

    m_undo.push(std::make_unique<SequenceSnapshotCommand>(*seq, working, text), m_project);
    pruneSelection();
    emit structureChanged();
    emit contentsChanged();
    return true;
}

void Document::setLiveSequence(const Sequence& s)
{
    Sequence* seq = sequence();
    if (!seq) return;
    *seq = s;
    seq->recomputeDuration();
    emit structureChanged();
    emit contentsChanged();
}

void Document::commitSequence(const Sequence& before, const QString& text)
{
    Sequence* seq = sequence();
    if (!seq) return;
    Sequence after = *seq;
    after.recomputeDuration();
    *seq = before;
    m_undo.push(std::make_unique<SequenceSnapshotCommand>(before, after, text), m_project);
    pruneSelection();
    emit structureChanged();
    emit contentsChanged();
}

void Document::editTrack(int trackIndex, const QString& text,
                         const std::function<void(QVector<Clip>&)>& fn)
{
    Sequence* seq = sequence();
    if (!seq) return;
    Track* t = seq->track(trackIndex);
    if (!t) return;

    QVector<Clip> before = t->clips;
    QVector<Clip> after = before;
    fn(after);

    std::stable_sort(after.begin(), after.end(),
                     [](const Clip& a, const Clip& b) { return a.start < b.start; });

    m_undo.push(std::make_unique<TrackSnapshotCommand>(seq->id, trackIndex, before, after, text),
                m_project);

    emit structureChanged();
    emit contentsChanged();
}

void Document::commitTrackEdit(int trackIndex, const QVector<Clip>& before,
                               const QVector<Clip>& after, const QString& text)
{
    Sequence* seq = sequence();
    if (!seq || !seq->track(trackIndex)) return;

    m_undo.push(std::make_unique<TrackSnapshotCommand>(seq->id, trackIndex, before, after, text),
                m_project);

    emit structureChanged();
    emit contentsChanged();
}

void Document::pushCommand(std::unique_ptr<Command> command)
{
    if (!command) return;
    m_undo.push(std::move(command), m_project);
    if (Sequence* s = sequence()) s->recomputeDuration();
    emit structureChanged();
    emit contentsChanged();
}

void Document::notifyChanged()
{
    m_metaDirty = true;
    emit structureChanged();
    emit contentsChanged();
}

void Document::setPlayhead(Tick tick)
{
    tick = std::max<Tick>(0, tick);
    if (m_playhead == tick) return;
    m_playhead = tick;
    emit playheadChanged(m_playhead);
}

void Document::setSelection(const QString& clipId, int trackIndex)
{
    QStringList list;
    if (!clipId.isEmpty()) list << clipId;
    if (m_selectedClipId == clipId && m_selectedTrack == trackIndex && m_selection == list) return;
    m_selectedClipId = clipId;
    m_selectedTrack = trackIndex;
    m_selection = list;
    emit selectionChanged();
}

void Document::setSelectionList(const QStringList& clipIds, const QString& primary)
{
    QString main = primary;
    if (main.isEmpty() || !clipIds.contains(main)) main = clipIds.isEmpty() ? QString() : clipIds.first();

    // Prefer a video clip as primary so Effect Controls shows the picture clip.
    if (primary.isEmpty() && sequence()) {
        for (const QString& id : clipIds) {
            int ti = -1;
            if (sequence()->findClip(id, &ti) && sequence()->tracks[ti].kind == TrackKind::Video) {
                main = id;
                break;
            }
        }
    }

    int trackIndex = -1;
    if (!main.isEmpty() && sequence()) sequence()->findClip(main, &trackIndex);

    if (m_selection == clipIds && m_selectedClipId == main) return;
    m_selection = clipIds;
    m_selectedClipId = main;
    m_selectedTrack = trackIndex;
    emit selectionChanged();
}

void Document::clearSelection()
{
    if (m_selectedClipId.isEmpty() && m_selectedTrack < 0 && m_selection.isEmpty()) return;
    m_selectedClipId.clear();
    m_selectedTrack = -1;
    m_selection.clear();
    emit selectionChanged();
}

void Document::pruneSelection()
{
    const Sequence* seq = sequence();
    if (!seq) return;
    QStringList kept;
    for (const QString& id : m_selection)
        if (seq->findClip(id)) kept << id;
    int ti = -1;
    const bool primaryAlive = !m_selectedClipId.isEmpty() && seq->findClip(m_selectedClipId, &ti);
    if (kept == m_selection && (m_selectedClipId.isEmpty() || primaryAlive)) {
        if (primaryAlive && ti != m_selectedTrack) {
            m_selectedTrack = ti;
            emit selectionChanged();
        }
        return;
    }
    m_selection = kept;
    if (!primaryAlive) {
        m_selectedClipId = kept.isEmpty() ? QString() : kept.first();
        ti = -1;
        if (!m_selectedClipId.isEmpty()) seq->findClip(m_selectedClipId, &ti);
    }
    m_selectedTrack = ti;
    emit selectionChanged();
}

void Document::undo()
{
    m_undo.undo(m_project);
    if (Sequence* s = sequence()) s->recomputeDuration();
    pruneSelection();
    emit structureChanged();
    emit contentsChanged();
}

void Document::redo()
{
    m_undo.redo(m_project);
    if (Sequence* s = sequence()) s->recomputeDuration();
    pruneSelection();
    emit structureChanged();
    emit contentsChanged();
}

void Document::setSequence(const QString& sequenceId)
{
    if (m_sequenceId == sequenceId || !m_project.sequenceById(sequenceId)) return;
    m_sequenceId = sequenceId;
    m_playhead = 0;
    clearSelection();
    emit sequencesChanged();
    emit structureChanged();
    emit contentsChanged();
    emit playheadChanged(0);
}

void Document::insertSequence(const Sequence& seq)
{
    m_project.sequences.push_back(seq);
    m_metaDirty = true;
    emit sequencesChanged();
}

QString Document::addSequence(const Sequence& seq)
{
    m_project.sequences.push_back(seq);
    m_metaDirty = true;
    emit sequencesChanged();
    setSequence(seq.id);
    return seq.id;
}

void Document::addMedia(const MediaRef& ref)
{
    if (m_project.mediaById(ref.id)) return;
    m_project.media.push_back(ref);
    m_metaDirty = true;
    emit mediaChanged();
}

void Document::removeMedia(const QString& mediaId)
{
    for (int i = 0; i < m_project.media.size(); ++i) {
        if (m_project.media[i].id != mediaId) continue;
        m_project.media.removeAt(i);
        // Clips referencing it are removed too (Premiere asks first; the UI does).
        for (auto& seq : m_project.sequences)
            for (auto& t : seq.tracks)
                t.clips.erase(std::remove_if(t.clips.begin(), t.clips.end(),
                                             [&](const Clip& c) { return c.mediaId == mediaId; }),
                              t.clips.end());
        for (auto& seq : m_project.sequences) seq.recomputeDuration();
        m_undo.clear();
        m_metaDirty = true;
        pruneSelection();
        emit mediaChanged();
        emit structureChanged();
        emit contentsChanged();
        return;
    }
}

void Document::replaceMediaPath(const QString& mediaId, const MediaRef& replacement)
{
    MediaRef* media = m_project.mediaById(mediaId);
    if (!media) return;
    const QString id = media->id;
    *media = replacement;
    media->id = id;
    media->offline = false;
    m_metaDirty = true;
    emit mediaChanged();
    emit contentsChanged();
}

void Document::setMediaProxy(const QString& mediaId, const QString& proxyPath)
{
    MediaRef* media = m_project.mediaById(mediaId);
    if (!media || media->proxyPath == proxyPath) return;
    media->proxyPath = proxyPath;
    m_metaDirty = true;
    emit mediaChanged();
    emit contentsChanged();
}

bool Document::load(const QString& path, QString* error)
{
    Project loaded;
    if (!io::loadProject(path, loaded, error)) return false;

    for (auto& m : loaded.media)
        m.offline = !m.path.isEmpty() && !QFileInfo::exists(m.path);

    // Older builds allowed trims past the end of the media; clamp them so the
    // timeline never shows frames that do not exist.
    for (auto& seq : loaded.sequences) {
        for (auto& track : seq.tracks) {
            for (int i = track.clips.size() - 1; i >= 0; --i) {
                Clip& c = track.clips[i];
                const MediaRef* media = loaded.mediaById(c.mediaId);
                if (!media || media->duration <= 0 || c.isGenerator()) continue;
                const Tick limit = seq.rate.snap(maxClipDuration(c, media));
                if (c.duration > limit) {
                    if (limit <= 0) track.clips.removeAt(i);
                    else c.duration = limit;
                }
            }
        }
        seq.recomputeDuration();
    }

    m_project = loaded;
    m_undo.clear();
    m_savedRevision = m_undo.revision();
    m_metaDirty = false;
    m_playhead = 0;
    m_selectedClipId.clear();
    m_selectedTrack = -1;
    m_selection.clear();
    m_sequenceId = m_project.sequences.isEmpty() ? QString() : m_project.sequences.first().id;

    emit mediaChanged();
    emit sequencesChanged();
    emit structureChanged();
    emit contentsChanged();
    emit playheadChanged(0);
    emit selectionChanged();
    emit documentLoaded();
    return true;
}

bool Document::save(const QString& path, QString* error)
{
    if (!io::saveProject(m_project, path, error)) return false;
    if (!path.endsWith(QStringLiteral(".autosave"))) {
        m_project.filePath = path;
        m_savedRevision = m_undo.revision();
        m_metaDirty = false;
    }
    return true;
}

void Document::newProject()
{
    m_project = Project::createDefault();
    m_undo.clear();
    m_savedRevision = m_undo.revision();
    m_metaDirty = false;
    m_playhead = 0;
    m_selectedClipId.clear();
    m_selectedTrack = -1;
    m_selection.clear();
    m_sequenceId = m_project.sequences.first().id;

    emit mediaChanged();
    emit sequencesChanged();
    emit structureChanged();
    emit contentsChanged();
    emit playheadChanged(0);
    emit selectionChanged();
    emit documentLoaded();
}

} // namespace bc
