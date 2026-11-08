#pragma once

#include "core/Commands.h"
#include "core/EditOps.h"
#include "core/Project.h"

#include <QObject>
#include <QStringList>
#include <functional>

namespace bc {

class Document : public QObject {
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);

    Project& project() { return m_project; }
    const Project& project() const { return m_project; }

    Sequence* sequence();
    const Sequence* sequence() const;
    QString sequenceId() const { return m_sequenceId; }

    UndoStack& undoStack() { return m_undo; }

    Tick playhead() const { return m_playhead; }

    // Primary selection (drives Effect Controls / Lumetri) plus the full set.
    QString selectedClipId() const { return m_selectedClipId; }
    int selectedTrackIndex() const { return m_selectedTrack; }
    const QStringList& selectedClips() const { return m_selection; }
    bool isSelected(const QString& clipId) const { return m_selection.contains(clipId); }

    // Copies the live sequence into a value the render thread can read without
    // touching the document. Cheap enough at this scale (hundreds of clips).
    Sequence snapshotSequence() const;

    edit::Context editContext() const;

    // Runs fn on a copy of the active sequence; if it returns true the change
    // is committed as one undoable step.
    bool applyEdit(const QString& text, const std::function<bool(Sequence&)>& fn);

    // Replace the active sequence without an undo step (live gesture preview).
    void setLiveSequence(const Sequence& seq);

    // Commit an edit whose live preview already mutated the sequence.
    void commitSequence(const Sequence& before, const QString& text);

    void editTrack(int trackIndex, const QString& text,
                   const std::function<void(QVector<Clip>&)>& fn);
    void commitTrackEdit(int trackIndex, const QVector<Clip>& before,
                         const QVector<Clip>& after, const QString& text);
    void pushCommand(std::unique_ptr<Command> command);
    void notifyChanged();

    bool load(const QString& path, QString* error);
    bool save(const QString& path, QString* error);
    void newProject();

    bool isDirty() const { return m_undo.revision() != m_savedRevision || m_metaDirty; }
    void markMetaDirty() { m_metaDirty = true; }

    // Clipboard (process-wide within the document).
    const QVector<edit::ClipboardItem>& clipboard() const { return m_clipboard; }
    void setClipboard(const QVector<edit::ClipboardItem>& items) { m_clipboard = items; }

    // Media management.
    void removeMedia(const QString& mediaId);
    void replaceMediaPath(const QString& mediaId, const MediaRef& replacement);

    // Sequences.
    QString addSequence(const Sequence& seq);

signals:
    void structureChanged();     // clips added / removed / moved
    void contentsChanged();      // anything that affects rendering
    void playheadChanged(bc::Tick tick);
    void selectionChanged();
    void mediaChanged();
    void documentLoaded();
    void sequencesChanged();

public slots:
    void setPlayhead(Tick tick);
    void setSelection(const QString& clipId, int trackIndex);
    void setSelectionList(const QStringList& clipIds, const QString& primary = QString());
    void clearSelection();
    void undo();
    void redo();
    void setSequence(const QString& sequenceId);
    void addMedia(const bc::MediaRef& ref);
    void setMediaProxy(const QString& mediaId, const QString& proxyPath);

private:
    void pruneSelection();

    Project m_project;
    QString m_sequenceId;
    UndoStack m_undo;
    Tick m_playhead = 0;
    QString m_selectedClipId;
    int m_selectedTrack = -1;
    QStringList m_selection;
    QVector<edit::ClipboardItem> m_clipboard;
    quint64 m_savedRevision = 0;
    bool m_metaDirty = false;
};

} // namespace bc
