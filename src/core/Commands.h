#pragma once

#include "core/Project.h"

#include <QString>
#include <memory>
#include <utility>
#include <vector>

namespace bc {

class Command {
public:
    virtual ~Command() = default;
    virtual void undo(Project& project) = 0;
    virtual void redo(Project& project) = 0;
    virtual QString text() const = 0;
};

// Coarse-grained but simple and exact: a track's clip list is snapshotted
// before and after an edit. Clip records are small (a few hundred bytes), and
// this makes every edit — move, trim, split, ripple delete — trivially correct
// without bespoke inverse logic.
class TrackSnapshotCommand : public Command {
public:
    TrackSnapshotCommand(QString sequenceId, int trackIndex,
                         QVector<Clip> before, QVector<Clip> after, QString text);

    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override { return m_text; }

private:
    void apply(Project& project, const QVector<Clip>& clips);

    QString m_sequenceId;
    int m_trackIndex;
    QVector<Clip> m_before;
    QVector<Clip> m_after;
    QString m_text;
};

// Whole-sequence snapshot. Used for multi-track edits (ripple, insert, linked
// trims, ...). A Sequence copy is a few KB for typical timelines.
class SequenceSnapshotCommand : public Command {
public:
    SequenceSnapshotCommand(Sequence before, Sequence after, QString text);
    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override { return m_text; }

private:
    void apply(Project& project, const Sequence& s);
    Sequence m_before;
    Sequence m_after;
    QString m_text;
};

// Groups several commands so a cross-track drag undoes as a single step.
class CompositeCommand : public Command {
public:
    explicit CompositeCommand(QString text) : m_text(std::move(text)) {}
    void add(std::unique_ptr<Command> cmd) { m_children.push_back(std::move(cmd)); }
    void undo(Project& project) override;
    void redo(Project& project) override;
    QString text() const override { return m_text; }
    bool empty() const { return m_children.empty(); }

private:
    QString m_text;
    std::vector<std::unique_ptr<Command>> m_children;
};

class UndoStack {
public:
    static constexpr int kMaxDepth = 200;

    void push(std::unique_ptr<Command> cmd, Project& project);
    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < static_cast<int>(m_commands.size()); }
    QString undoText() const;
    QString redoText() const;
    void undo(Project& project);
    void redo(Project& project);
    void clear();
    int depth() const { return static_cast<int>(m_commands.size()); }
    int index() const { return m_index; }
    QString textAt(int i) const { return (i >= 0 && i < depth()) ? m_commands[i]->text() : QString(); }
    // Monotonic counter bumped on every push/undo/redo; used for dirty tracking.
    quint64 revision() const { return m_revision; }

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    int m_index = 0;
    quint64 m_revision = 0;
};

} // namespace bc
