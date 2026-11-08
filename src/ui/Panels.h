#pragma once

#include <QWidget>

class QListWidget;
class QProgressBar;
class QLabel;
class QVBoxLayout;
class QHBoxLayout;

namespace bc {

class Document;
class Exporter;

// Audio Track Mixer: one strip per audio track (fader, pan, mute, solo).
class AudioMixerPanel : public QWidget {
    Q_OBJECT

public:
    explicit AudioMixerPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void rebuild();

private:
    Document* m_document;
    QHBoxLayout* m_strips = nullptr;
    QString m_builtFor;   // sequence id + track ids the strips were built for
    bool m_updating = false;
};

// History: the undo stack as a list; clicking an entry jumps there.
class HistoryPanel : public QWidget {
    Q_OBJECT

public:
    explicit HistoryPanel(Document* document, QWidget* parent = nullptr);

public slots:
    void refresh();

private:
    Document* m_document;
    QListWidget* m_list = nullptr;
    bool m_updating = false;
};

// Render queue status: current job, progress, finished jobs.
class RenderQueuePanel : public QWidget {
    Q_OBJECT

public:
    explicit RenderQueuePanel(Exporter* exporter, QWidget* parent = nullptr);

private:
    Exporter* m_exporter;
    QLabel* m_current = nullptr;
    QProgressBar* m_progress = nullptr;
    QListWidget* m_log = nullptr;
};

} // namespace bc
