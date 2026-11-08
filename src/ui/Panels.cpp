#include "ui/Panels.h"

#include "core/Document.h"
#include "media/Exporter.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QDial>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

namespace bc {

// ------------------------------------------------------------- AudioMixer

AudioMixerPanel::AudioMixerPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* host = new QWidget(scroll);
    m_strips = new QHBoxLayout(host);
    m_strips->setContentsMargins(0, 0, 0, 0);
    m_strips->setSpacing(6);
    scroll->setWidget(host);
    root->addWidget(scroll);

    connect(m_document, &Document::structureChanged, this, &AudioMixerPanel::rebuild);
    connect(m_document, &Document::documentLoaded, this, &AudioMixerPanel::rebuild);
    rebuild();
}

void AudioMixerPanel::rebuild()
{
    if (m_updating) return;
    const Sequence* seq = m_document->sequence();
    if (!seq) return;

    // Cheap signature check so dragging a fader does not rebuild the strips.
    QString signature = seq->id;
    for (const auto& t : seq->tracks)
        if (t.kind == TrackKind::Audio)
            signature += t.id + QString::number(t.volumeDb) + QString::number(t.pan) + (t.muted ? "m" : "") + (t.solo ? "s" : "") + t.name;
    if (signature == m_builtFor) return;
    m_builtFor = signature;

    while (QLayoutItem* item = m_strips->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    for (int ti = 0; ti < seq->tracks.size(); ++ti) {
        const Track& track = seq->tracks[ti];
        if (track.kind != TrackKind::Audio) continue;
        const QString trackId = track.id;

        auto* strip = new QWidget;
        strip->setFixedWidth(74);
        strip->setStyleSheet(QStringLiteral("QWidget { background: #232327; border-radius: 4px; }"));
        auto* col = new QVBoxLayout(strip);
        col->setContentsMargins(4, 6, 4, 6);
        col->setSpacing(4);

        auto* pan = new QDial(strip);
        pan->setRange(-100, 100);
        pan->setValue(static_cast<int>(std::lround(track.pan * 100)));
        pan->setFixedSize(38, 38);
        pan->setNotchesVisible(true);
        pan->setToolTip(tr("Pan (double-click to center)"));
        col->addWidget(pan, 0, Qt::AlignHCenter);

        auto* buttons = new QHBoxLayout;
        auto* mute = new QToolButton(strip);
        mute->setText(QStringLiteral("M"));
        mute->setCheckable(true);
        mute->setChecked(track.muted);
        auto* solo = new QToolButton(strip);
        solo->setText(QStringLiteral("S"));
        solo->setCheckable(true);
        solo->setChecked(track.solo);
        buttons->addWidget(mute);
        buttons->addWidget(solo);
        col->addLayout(buttons);

        auto* fader = new QSlider(Qt::Vertical, strip);
        fader->setRange(-600, 120);   // tenths of dB
        fader->setValue(static_cast<int>(std::lround(track.volumeDb * 10)));
        fader->setMinimumHeight(110);
        col->addWidget(fader, 1, Qt::AlignHCenter);

        auto* value = new QLabel(QStringLiteral("%1 dB").arg(track.volumeDb, 0, 'f', 1), strip);
        value->setAlignment(Qt::AlignCenter);
        value->setStyleSheet(QStringLiteral("color: #c8c8d0; font-size: 10px;"));
        col->addWidget(value);
        auto* name = new QLabel(track.name, strip);
        name->setAlignment(Qt::AlignCenter);
        name->setStyleSheet(QStringLiteral("color: #ffffff; font-weight: 600;"));
        col->addWidget(name);

        auto apply = [this, trackId](auto fn) {
            Sequence* s = m_document->sequence();
            if (!s) return;
            if (Track* t = s->trackById(trackId)) {
                fn(*t);
                m_updating = true;
                m_document->notifyChanged();
                m_updating = false;
            }
        };
        connect(fader, &QSlider::valueChanged, strip, [=](int v) {
            value->setText(QStringLiteral("%1 dB").arg(v / 10.0, 0, 'f', 1));
            apply([v](Track& t) { t.volumeDb = v / 10.0; });
        });
        connect(pan, &QDial::valueChanged, strip, [=](int v) { apply([v](Track& t) { t.pan = v / 100.0; }); });
        connect(mute, &QToolButton::toggled, strip, [=](bool on) { apply([on](Track& t) { t.muted = on; }); });
        connect(solo, &QToolButton::toggled, strip, [=](bool on) { apply([on](Track& t) { t.solo = on; }); });

        m_strips->addWidget(strip);
    }
    m_strips->addStretch(1);
}

// ----------------------------------------------------------------- History

HistoryPanel::HistoryPanel(Document* document, QWidget* parent)
    : QWidget(parent)
    , m_document(document)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    m_list = new QListWidget(this);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: #1c1c1f; }"));
    root->addWidget(m_list);

    connect(m_document, &Document::structureChanged, this, &HistoryPanel::refresh);
    connect(m_document, &Document::documentLoaded, this, &HistoryPanel::refresh);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (m_updating || row < 0) return;
        // Row 0 is the opened state; row n means n commands applied.
        UndoStack& stack = m_document->undoStack();
        m_updating = true;
        while (stack.index() > row && stack.canUndo()) m_document->undo();
        while (stack.index() < row && stack.canRedo()) m_document->redo();
        m_updating = false;
        refresh();
    });
    refresh();
}

void HistoryPanel::refresh()
{
    if (m_updating) return;
    m_updating = true;
    UndoStack& stack = m_document->undoStack();
    m_list->clear();
    m_list->addItem(tr("Open"));
    for (int i = 0; i < stack.depth(); ++i) {
        auto* item = new QListWidgetItem(stack.textAt(i), m_list);
        if (i >= stack.index()) item->setForeground(theme::textFaint());
    }
    m_list->setCurrentRow(stack.index());
    m_updating = false;
}

// ------------------------------------------------------------- RenderQueue

RenderQueuePanel::RenderQueuePanel(Exporter* exporter, QWidget* parent)
    : QWidget(parent)
    , m_exporter(exporter)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 6, 6, 6);
    m_current = new QLabel(tr("Idle - use File > Export > Media to add jobs"), this);
    m_current->setWordWrap(true);
    root->addWidget(m_current);
    auto* row = new QHBoxLayout;
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    row->addWidget(m_progress, 1);
    auto* cancel = new QPushButton(tr("Stop"), this);
    auto* cancelAll = new QPushButton(tr("Stop All"), this);
    row->addWidget(cancel);
    row->addWidget(cancelAll);
    root->addLayout(row);
    m_log = new QListWidget(this);
    m_log->setStyleSheet(QStringLiteral("QListWidget { background: #1c1c1f; }"));
    root->addWidget(m_log, 1);

    connect(cancel, &QPushButton::clicked, m_exporter, &Exporter::cancel);
    connect(cancelAll, &QPushButton::clicked, m_exporter, &Exporter::cancelAll);
    connect(m_exporter, &Exporter::progress, m_progress, &QProgressBar::setValue);
    connect(m_exporter, &Exporter::jobStarted, this, [this](const QString& label, int remaining) {
        m_current->setText(remaining > 0 ? tr("Rendering %1 (%2 more queued)").arg(label).arg(remaining)
                                         : tr("Rendering %1").arg(label));
        m_progress->setValue(0);
    });
    connect(m_exporter, &Exporter::encoderChosen, this, [this](const QString& text) {
        m_log->addItem(QStringLiteral("  ") + text);
    });
    connect(m_exporter, &Exporter::finished, this, [this](const QString& path) {
        auto* item = new QListWidgetItem(tr("Done: %1  (%2)").arg(QFileInfo(path).fileName(),
                                                              QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"))), m_log);
        item->setData(Qt::UserRole, path);
        item->setToolTip(tr("Double-click to open the folder"));
    });
    connect(m_exporter, &Exporter::failed, this, [this](const QString& error) {
        m_log->addItem(tr("Failed: %1").arg(error));
    });
    connect(m_exporter, &Exporter::queueEmpty, this, [this] {
        m_current->setText(tr("Queue finished"));
        m_progress->setValue(100);
    });
    connect(m_log, &QListWidget::itemDoubleClicked, this, [](QListWidgetItem* item) {
        const QString path = item->data(Qt::UserRole).toString();
        if (!path.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
}

} // namespace bc
