#include "core/Commands.h"

#include <utility>

namespace bc {

TrackSnapshotCommand::TrackSnapshotCommand(QString sequenceId, int trackIndex,
                                           QVector<Clip> before, QVector<Clip> after,
                                           QString text)
    : m_sequenceId(std::move(sequenceId))
    , m_trackIndex(trackIndex)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_text(std::move(text))
{
}

void TrackSnapshotCommand::apply(Project& project, const QVector<Clip>& clips)
{
    Sequence* seq = project.sequenceById(m_sequenceId);
    if (!seq) return;
    Track* t = seq->track(m_trackIndex);
    if (!t) return;
    t->clips = clips;
    seq->recomputeDuration();
}

void TrackSnapshotCommand::undo(Project& project) { apply(project, m_before); }
void TrackSnapshotCommand::redo(Project& project) { apply(project, m_after); }

// ------------------------------------------------------ SequenceSnapshotCommand

SequenceSnapshotCommand::SequenceSnapshotCommand(Sequence before, Sequence after, QString text)
    : m_before(std::move(before))
    , m_after(std::move(after))
    , m_text(std::move(text))
{
}

void SequenceSnapshotCommand::apply(Project& project, const Sequence& s)
{
    Sequence* seq = project.sequenceById(s.id);
    if (!seq) return;
    *seq = s;
    seq->recomputeDuration();
}

void SequenceSnapshotCommand::undo(Project& project) { apply(project, m_before); }
void SequenceSnapshotCommand::redo(Project& project) { apply(project, m_after); }

// ------------------------------------------------------------- CompositeCommand

void CompositeCommand::undo(Project& project)
{
    for (auto it = m_children.rbegin(); it != m_children.rend(); ++it)
        (*it)->undo(project);
}

void CompositeCommand::redo(Project& project)
{
    for (auto& child : m_children)
        child->redo(project);
}

// ------------------------------------------------------------------ UndoStack

void UndoStack::push(std::unique_ptr<Command> cmd, Project& project)
{
    if (!cmd) return;

    // Drop the redo tail, then run the command.
    while (static_cast<int>(m_commands.size()) > m_index)
        m_commands.pop_back();

    cmd->redo(project);
    ++m_revision;
    m_commands.push_back(std::move(cmd));
    m_index = static_cast<int>(m_commands.size());

    if (static_cast<int>(m_commands.size()) > kMaxDepth) {
        const int excess = static_cast<int>(m_commands.size()) - kMaxDepth;
        m_commands.erase(m_commands.begin(), m_commands.begin() + excess);
        m_index -= excess;
    }
}

QString UndoStack::undoText() const
{
    return canUndo() ? m_commands[m_index - 1]->text() : QString();
}

QString UndoStack::redoText() const
{
    return canRedo() ? m_commands[m_index]->text() : QString();
}

void UndoStack::undo(Project& project)
{
    if (!canUndo()) return;
    --m_index;
    m_commands[m_index]->undo(project);
    ++m_revision;
}

void UndoStack::redo(Project& project)
{
    if (!canRedo()) return;
    m_commands[m_index]->redo(project);
    ++m_index;
    ++m_revision;
}

void UndoStack::clear()
{
    m_commands.clear();
    m_index = 0;
}

} // namespace bc
