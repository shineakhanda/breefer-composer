#include "ui/MainWindow.h"

#include "core/Document.h"
#include "engine/Compositor.h"
#include "engine/Effects.h"
#include "engine/PlaybackEngine.h"
#include "io/Interchange.h"
#include "media/AudioEngine.h"
#include "media/Exporter.h"
#include "media/MediaProbe.h"
#include "media/ProxyManager.h"
#include "media/RemotionBridge.h"
#include "engine/PluginHost.h"
#include "ui/AudioMeters.h"
#include "ui/ColorPanels.h"
#include "ui/Dialogs.h"
#include "ui/EffectControls.h"
#include "ui/EffectsPanel.h"
#include "ui/Icons.h"
#include "ui/MediaBin.h"
#include "ui/MonitorWidget.h"
#include "ui/Panels.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "media/Decoder.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QCursor>
#include <QDesktopServices>
#include <QJsonObject>
#include <QListWidget>
#include <QUrl>
#include "engine/Model3D.h"
#include "media/ThumbnailCache.h"
#include <QKeyEvent>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QThread>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <memory>

namespace bc {
namespace {

QWidget* wrapPanel(const QString& title, QWidget* inner)
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* header = new QLabel(title, panel);
    header->setStyleSheet(QStringLiteral(
        "background: #2a2a2f; color: #d8d8dc; padding: 4px 8px;"
        "font-weight: 600; border-bottom: 1px solid #3a3a40;"));
    layout->addWidget(header);
    layout->addWidget(inner, 1);
    return panel;
}

QSettings appSettings() { return QSettings(QStringLiteral("Breefer"), QStringLiteral("Composer")); }

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_document = new Document(this);
    m_programEngine = new PlaybackEngine(this);
    m_sourceEngine = new PlaybackEngine(this);
    m_audio = new AudioEngine();
    m_audio->initialize();
    m_proxies = new ProxyManager(this);
    m_exporter = new Exporter(this);

    // Audio is the master clock at normal speed.
    m_programEngine->setExternalClock([this]() -> std::optional<Tick> {
        if (m_audio && m_audio->isPlaying()) return m_audio->position();
        return std::nullopt;
    });

    {
        QSettings settings = appSettings();
        m_lastExportFolder = settings.value(QStringLiteral("lastExportFolder")).toString();
    }

    m_autosaveTimer = new QTimer(this);
    connect(m_autosaveTimer, &QTimer::timeout, this, [this] {
        if (m_currentFile.isEmpty() || !m_document->isDirty()) return;
        QString ignored;
        if (m_document->save(m_currentFile + QStringLiteral(".autosave"), &ignored)) showStatus(tr("Auto-saved"));
    });
    {
        QSettings settings = appSettings();
        const int minutes = settings.value(QStringLiteral("autosaveMinutes"), 5).toInt();
        if (minutes > 0) m_autosaveTimer->start(minutes * 60000);
    }

    setWindowTitle(tr("Breefer Composer"));
    resize(1366, 740);
    setMinimumSize(960, 560);
    {
        QSettings settings = appSettings();
        VideoDecoder::setHardwareDecodeEnabled(settings.value(QStringLiteral("hardwareDecode"), true).toBool());
        // Low-end default: 1/2 playback on 4 threads or fewer, 1/4 on 2 threads.
        const int cores = QThread::idealThreadCount();
        m_playbackDivisor = settings.value(QStringLiteral("playbackDivisor"), cores <= 2 ? 4 : 2).toInt();
    }

    buildActions();   // tool actions must exist before the Tools panel is built
    buildUi();
    buildMenus();
    buildToolbar();
    wire();

    QSettings settings = appSettings();
    if (settings.contains(QStringLiteral("geometry.v4"))) {
        restoreGeometry(settings.value(QStringLiteral("geometry.v4")).toByteArray());
        m_vSplit->restoreState(settings.value(QStringLiteral("split.v")).toByteArray());
        m_topSplit->restoreState(settings.value(QStringLiteral("split.top")).toByteArray());
        m_bottomSplit->restoreState(settings.value(QStringLiteral("split.bottom")).toByteArray());
    } else {
        setWindowState(Qt::WindowMaximized);
    }
    setPlaybackDivisor(m_playbackDivisor);
    {
        // Old dual/quad-thread CPUs (Core i3/i5 2nd-4th gen) start in low-end mode.
        const QVariant stored = settings.value(QStringLiteral("lowEndMode"));
        const bool lowEnd = stored.isValid() ? stored.toBool() : QThread::idealThreadCount() <= 4;
        if (lowEnd) applyLowEndMode(true);
    }
    {
        const int frames = settings.value(QStringLiteral("defaultTransitionFrames"), 25).toInt();
        m_timeline->setDefaultTransitionFrames(frames);
    }

    syncEngineSources();
    refreshTitle();
    showStatus(tr("Ready. Import media (Ctrl+I), double-click it to open in Source, then press , to insert or drag it to the timeline."));
}

MainWindow::~MainWindow()
{
    if (m_audio) {
        m_audio->shutdown();
        delete m_audio;
        m_audio = nullptr;
    }
}

// ------------------------------------------------------------------------- UI

void MainWindow::buildUi()
{
    m_programMonitor = new MonitorWidget(MonitorWidget::Role::Program);
    m_sourceMonitor = new MonitorWidget(MonitorWidget::Role::Source);
    m_mediaBin = new MediaBin(m_document);
    m_effects = new EffectsPanel(m_document);
    m_history = new HistoryPanel(m_document);
    m_timeline = new TimelineWidget(m_document);
    m_effectControls = new EffectControls(m_document);
    m_lumetri = new LumetriPanel(m_document);
    m_mixer = new AudioMixerPanel(m_document);
    m_scopes = new ScopesPanel;
    m_queue = new RenderQueuePanel(m_exporter);
    m_meters = new AudioMeters;

    auto frame = [this](QWidget* content) {
        auto* f = new PanelFrame(content);
        m_frames << f;
        return f;
    };
    auto group = [this](PanelTabs*& tabs) {
        tabs = new PanelTabs;
        connect(tabs, &PanelTabs::maximizeRequested, this, [this, tabs] {
            toggleMaximize(qobject_cast<PanelFrame*>(tabs->parentWidget()));
        });
        return tabs;
    };

    // Upper left: Source / Effect Controls / Lumetri Color / Audio Track Mixer / Lumetri Scopes.
    group(m_upperLeft);
    m_upperLeft->addPanel(m_sourceMonitor, tr("Source: (no clips)"));
    m_upperLeft->addPanel(m_effectControls, tr("Effect Controls"));
    m_upperLeft->addPanel(m_lumetri, tr("Lumetri Color"));
    m_upperLeft->addPanel(m_mixer, tr("Audio Track Mixer"));
    m_upperLeft->addPanel(m_scopes, tr("Lumetri Scopes"));
    m_upperLeft->setMenuProvider([this](int index, QMenu* menu) {
        if (m_upperLeft->widget(index) == m_scopes) {
            menu->addAction(tr("Waveform (Luma)"), this, [this] { m_scopes->setMode(0); });
            menu->addAction(tr("RGB Parade"), this, [this] { m_scopes->setMode(1); });
            menu->addAction(tr("Vectorscope YUV"), this, [this] { m_scopes->setMode(2); });
        } else if (m_upperLeft->widget(index) == m_sourceMonitor) {
            menu->addAction(tr("Close Source Clip"), this, [this] {
                m_sourceMediaId.clear();
                m_sourceMonitor->clearImage();
                m_upperLeft->setTabText(0, tr("Source: (no clips)"));
            });
        }
    });
    m_upperLeftFrame = frame(m_upperLeft);

    group(m_programTabs);
    m_programTabs->addPanel(m_programMonitor, tr("Program"));
    m_programTabs->setMenuProvider([this](int, QMenu* menu) {
        QAction* safe = menu->addAction(tr("Safe Margins"), this, [this](bool on) { m_programMonitor->setSafeMargins(on); });
        safe->setCheckable(true);
        safe->setChecked(m_programMonitor->safeMargins());
        menu->addAction(tr("Export Frame..."), this, &MainWindow::exportFrame);
    });
    m_programFrame = frame(m_programTabs);

    // Lower left: Project / Effects / History / Render Queue.
    group(m_lowerLeft);
    m_lowerLeft->addPanel(m_mediaBin, tr("Project"));
    m_lowerLeft->addPanel(m_effects, tr("Effects"));
    m_lowerLeft->addPanel(m_history, tr("History"));
    m_lowerLeft->addPanel(m_queue, tr("Render Queue"));
    m_lowerLeft->setMenuProvider([this](int index, QMenu* menu) {
        if (m_lowerLeft->widget(index) == m_mediaBin) {
            menu->addAction(tr("Import..."), m_mediaBin, &MediaBin::importFiles);
            menu->addAction(tr("Import Folder..."), m_mediaBin, &MediaBin::importFolder);
            menu->addAction(tr("New Sequence..."), this, &MainWindow::newSequence);
        }
    });
    m_lowerLeftFrame = frame(m_lowerLeft);

    m_toolsFrame = frame(buildToolsPanel());
    m_timelineFrame = frame(buildTimelinePanel());
    m_metersFrame = frame(m_meters);
    m_meters->setMinimumWidth(42);

    m_topSplit = new QSplitter(Qt::Horizontal);
    m_topSplit->setObjectName(QStringLiteral("Gutter"));
    m_topSplit->setHandleWidth(4);
    m_topSplit->setChildrenCollapsible(false);
    m_topSplit->addWidget(m_upperLeftFrame);
    m_topSplit->addWidget(m_programFrame);
    m_topSplit->setStretchFactor(0, 1);
    m_topSplit->setStretchFactor(1, 1);

    m_bottomSplit = new QSplitter(Qt::Horizontal);
    m_bottomSplit->setObjectName(QStringLiteral("Gutter"));
    m_bottomSplit->setHandleWidth(4);
    m_bottomSplit->setChildrenCollapsible(false);
    m_bottomSplit->addWidget(m_lowerLeftFrame);
    m_bottomSplit->addWidget(m_toolsFrame);
    m_bottomSplit->addWidget(m_timelineFrame);
    m_bottomSplit->addWidget(m_metersFrame);
    m_bottomSplit->setStretchFactor(0, 0);
    m_bottomSplit->setStretchFactor(1, 0);
    m_bottomSplit->setStretchFactor(2, 1);
    m_bottomSplit->setStretchFactor(3, 0);

    m_vSplit = new QSplitter(Qt::Vertical);
    m_vSplit->setObjectName(QStringLiteral("Gutter"));
    m_vSplit->setHandleWidth(4);
    m_vSplit->setChildrenCollapsible(false);
    m_vSplit->addWidget(m_topSplit);
    m_vSplit->addWidget(m_bottomSplit);
    m_vSplit->setStretchFactor(0, 5);
    m_vSplit->setStretchFactor(1, 4);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("HeaderBar"));
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(4, 0, 4, 4);
    layout->setSpacing(4);
    layout->addWidget(buildHeaderBar());
    layout->addWidget(m_vSplit, 1);
    setCentralWidget(central);

    // Default proportions tuned for 1366x768.
    m_topSplit->setSizes({560, 780});
    m_bottomSplit->setSizes({300, 34, 960, 46});
    m_vSplit->setSizes({390, 320});

    // Premiere outlines the panel that has focus.
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        PanelFrame* active = nullptr;
        for (QWidget* w = now; w; w = w->parentWidget())
            if (auto* f = qobject_cast<PanelFrame*>(w)) { active = f; break; }
        if (!active) return;
        for (PanelFrame* f : m_frames) f->setActive(f == active);
    });
}

QWidget* MainWindow::buildHeaderBar()
{
    auto* bar = new QWidget;
    bar->setObjectName(QStringLiteral("HeaderBar"));
    bar->setFixedHeight(30);
    auto* row = new QHBoxLayout(bar);
    row->setContentsMargins(4, 0, 4, 0);
    row->setSpacing(2);

    auto* home = iconButton(icons::Id::Home, tr("Home (Open Recent)"), bar, false, 16);
    home->setPopupMode(QToolButton::InstantPopup);
    auto* homeMenu = new QMenu(home);
    connect(homeMenu, &QMenu::aboutToShow, this, [this, homeMenu] {
        homeMenu->clear();
        homeMenu->addAction(tr("New Project..."), this, &MainWindow::newProject);
        homeMenu->addAction(tr("Open Project..."), this, &MainWindow::openProject);
        homeMenu->addSeparator();
        if (m_recentMenu)
            for (QAction* a : m_recentMenu->actions()) homeMenu->addAction(a);
    });
    home->setMenu(homeMenu);
    row->addWidget(home);

    m_messageLabel = new QLabel(bar);
    m_messageLabel->setObjectName(QStringLiteral("HeaderStatus"));
    m_messageLabel->setMinimumWidth(120);
    row->addWidget(m_messageLabel, 1);

    QStringList modes = {tr("Import"), tr("Edit"), tr("Export")};
    for (int i = 0; i < modes.size(); ++i) {
        auto* b = new QToolButton(bar);
        b->setObjectName(QStringLiteral("ModeButton"));
        b->setText(modes[i]);
        b->setCheckable(true);
        b->setChecked(i == 1);
        b->setFocusPolicy(Qt::NoFocus);
        row->addWidget(b);
        m_modeButtons << b;
        connect(b, &QToolButton::clicked, this, [this, i] {
            for (int k = 0; k < m_modeButtons.size(); ++k) m_modeButtons[k]->setChecked(k == 1);
            if (i == 0) {
                raisePanel(m_mediaBin);
                m_mediaBin->importFiles();
            } else if (i == 2) {
                showExportDialog();
            }
        });
    }

    m_statsLabel = new QLabel(bar);
    m_statsLabel->setObjectName(QStringLiteral("HeaderStatus"));
    m_statsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_statsLabel->setMinimumWidth(120);
    row->addWidget(m_statsLabel, 1);

    m_toolLabel = new QLabel(bar);   // kept for tool feedback in tooltips
    m_toolLabel->hide();

    m_proxyButton = iconButton(icons::Id::Proxy, tr("Toggle Proxies"), bar, true, 16);
    m_proxyButton->setChecked(true);
    connect(m_proxyButton, &QToolButton::toggled, this, [this](bool enabled) {
        m_useProxies = enabled;
        syncEngineSources();
        m_programEngine->invalidate();
        if (enabled) autoQueueProxies();
        showStatus(enabled ? tr("Proxies on") : tr("Proxies off - decoding originals"));
    });
    row->addWidget(m_proxyButton);

    auto* quickExport = iconButton(icons::Id::Export, tr("Quick Export (Ctrl+M)"), bar, false, 16);
    connect(quickExport, &QToolButton::clicked, this, &MainWindow::showExportDialog);
    row->addWidget(quickExport);
    return bar;
}

QWidget* MainWindow::buildToolsPanel()
{
    auto* panel = new QWidget;
    panel->setFixedWidth(30);
    auto* col = new QVBoxLayout(panel);
    col->setContentsMargins(2, 6, 2, 6);
    col->setSpacing(2);

    auto toolButton = [&](QList<TimelineWidget::Tool> group) {
        auto* b = new QToolButton(panel);
        b->setIconSize(QSize(18, 18));
        b->setFixedSize(26, 26);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setDefaultAction(m_toolActions.value(static_cast<int>(group.first())));
        if (group.size() > 1) {
            // Premiere-style flyout: press and hold (or right-click) for the group.
            auto* menu = new QMenu(b);
            for (TimelineWidget::Tool t : group) {
                QAction* a = m_toolActions.value(static_cast<int>(t));
                menu->addAction(a);
                connect(a, &QAction::triggered, b, [b, a] { b->setDefaultAction(a); });
            }
            b->setMenu(menu);
            b->setPopupMode(QToolButton::DelayedPopup);
        }
        col->addWidget(b, 0, Qt::AlignHCenter);
        return b;
    };
    toolButton({TimelineWidget::Tool::Select});
    toolButton({TimelineWidget::Tool::TrackSelect});
    toolButton({TimelineWidget::Tool::Ripple, TimelineWidget::Tool::Roll, TimelineWidget::Tool::RateStretch});
    toolButton({TimelineWidget::Tool::Razor});
    toolButton({TimelineWidget::Tool::Slip, TimelineWidget::Tool::Slide});
    toolButton({TimelineWidget::Tool::Hand, TimelineWidget::Tool::Zoom});

    auto* type = new QToolButton(panel);
    type->setIcon(icons::icon(icons::Id::Type));
    type->setIconSize(QSize(18, 18));
    type->setFixedSize(26, 26);
    type->setAutoRaise(true);
    type->setFocusPolicy(Qt::NoFocus);
    type->setToolTip(tr("Type Tool (T) - new title at the playhead"));
    connect(type, &QToolButton::clicked, this, &MainWindow::newTitle);
    col->addWidget(type, 0, Qt::AlignHCenter);
    col->addStretch(1);
    return panel;
}

QWidget* MainWindow::buildTimelinePanel()
{
    auto* panel = new QWidget;
    auto* col = new QVBoxLayout(panel);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    auto* top = new QWidget(panel);
    auto* row = new QHBoxLayout(top);
    row->setContentsMargins(0, 0, 2, 0);
    row->setSpacing(0);
    m_sequenceTabs = new QTabBar(top);
    m_sequenceTabs->setObjectName(QStringLiteral("PanelTabBar"));
    m_sequenceTabs->setDrawBase(false);
    m_sequenceTabs->setExpanding(false);
    m_sequenceTabs->setTabsClosable(false);
    m_sequenceTabs->setUsesScrollButtons(true);
    m_sequenceTabs->setFocusPolicy(Qt::NoFocus);
    connect(m_sequenceTabs, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index < 0) return;
        pauseProgram();
        m_document->setSequence(m_sequenceTabs->tabData(index).toString());
        QTimer::singleShot(0, m_timeline, &TimelineWidget::zoomToFit);
    });
    connect(m_sequenceTabs, &QTabBar::tabBarDoubleClicked, this, [this](int) { toggleMaximize(m_timelineFrame); });
    row->addWidget(m_sequenceTabs, 1);

    auto* menuButton = iconButton(icons::Id::Hamburger, tr("Panel menu"), top, false, 16);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(menuButton);
    menu->addAction(tr("Sequence Settings..."), this, &MainWindow::sequenceSettings);
    menu->addAction(tr("New Sequence..."), this, &MainWindow::newSequence);
    menu->addSeparator();
    menu->addAction(tr("Add Video Track"), this, [this] { m_timeline->addTrack(false); });
    menu->addAction(tr("Add Audio Track"), this, [this] { m_timeline->addTrack(true); });
    menu->addAction(tr("Close Gaps"), m_timeline, &TimelineWidget::closeAllGaps);
    menu->addSeparator();
    menu->addAction(tr("Maximize Frame  (`)"), this, [this] { toggleMaximize(m_timelineFrame); });
    menuButton->setMenu(menu);
    row->addWidget(menuButton);
    col->addWidget(top);
    col->addWidget(m_timeline, 1);
    return panel;
}

void MainWindow::raisePanel(QWidget* panel)
{
    if (m_maximized) toggleMaximize(m_maximized);
    for (PanelTabs* tabs : {m_upperLeft, m_programTabs, m_lowerLeft}) {
        if (tabs->indexOf(panel) >= 0) {
            tabs->raisePanel(panel);
            return;
        }
    }
}

PanelFrame* MainWindow::frameUnderCursor() const
{
    for (QWidget* w = QApplication::widgetAt(QCursor::pos()); w; w = w->parentWidget())
        if (auto* f = qobject_cast<PanelFrame*>(w)) return f;
    return nullptr;
}

void MainWindow::toggleMaximize(PanelFrame* frame)
{
    if (!frame) return;
    if (m_maximized) {
        for (PanelFrame* f : m_frames) f->show();
        m_topSplit->show();
        m_bottomSplit->show();
        m_maximized = nullptr;
        return;
    }
    m_maximized = frame;
    for (PanelFrame* f : m_frames) f->setVisible(f == frame);
    const bool inTop = m_topSplit->indexOf(frame) >= 0;
    m_topSplit->setVisible(inTop);
    m_bottomSplit->setVisible(!inTop);
}

void MainWindow::setPlaybackDivisor(int divisor)
{
    m_playbackDivisor = std::clamp(divisor, 1, 16);
    const Sequence* s = m_document->sequence();
    const int height = std::max(90, (s ? s->height : 1080) / m_playbackDivisor);
    m_programEngine->setPreviewHeight(height);
    m_programMonitor->setResolutionDivisor(m_playbackDivisor);
    appSettings().setValue(QStringLiteral("playbackDivisor"), m_playbackDivisor);
}

QAction* MainWindow::action(const QString& text, const QKeySequence& shortcut, const std::function<void()>& fn,
                            QMenu* menu)
{
    auto* a = new QAction(text, this);
    if (!shortcut.isEmpty()) {
        a->setShortcut(shortcut);
        a->setShortcutContext(Qt::WindowShortcut);
    }
    // Navigation keys belong to lists, text fields and spin boxes when they have
    // focus; only plain editing keys are re-routed, everything else is global.
    const bool navigationKey = shortcut.count() == 1
        && QList<int>{Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down, Qt::Key_Home, Qt::Key_End,
                      Qt::Key_Delete, Qt::Key_Space}.contains(shortcut[0].key())
        && shortcut[0].keyboardModifiers() == Qt::NoModifier;
    connect(a, &QAction::triggered, this, [this, fn, navigationKey, shortcut] {
        QWidget* focus = QApplication::focusWidget();
        if (navigationKey && focus
            && (qobject_cast<QAbstractItemView*>(focus) || qobject_cast<QLineEdit*>(focus)
                || qobject_cast<QAbstractSpinBox*>(focus) || qobject_cast<QPlainTextEdit*>(focus)
                || qobject_cast<QComboBox*>(focus))) {
            const int key = shortcut[0].key();
            if (key == Qt::Key_Delete && m_mediaBin->isAncestorOf(focus)) {
                m_mediaBin->removeSelected();
                return;
            }
            QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, key == Qt::Key_Space ? QStringLiteral(" ") : QString());
            QApplication::sendEvent(focus, &press);
            return;
        }
        fn();
    });
    addAction(a);
    if (menu) menu->addAction(a);
    return a;
}

void MainWindow::buildActions()
{
    m_undoAction = new QAction(icons::undo(), tr("&Undo"), this);
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_undoAction->setEnabled(false);
    connect(m_undoAction, &QAction::triggered, m_document, &Document::undo);

    m_redoAction = new QAction(icons::redo(), tr("&Redo"), this);
    m_redoAction->setShortcuts({QKeySequence(QStringLiteral("Ctrl+Shift+Z")), QKeySequence::Redo});
    m_redoAction->setEnabled(false);
    connect(m_redoAction, &QAction::triggered, m_document, &Document::redo);

    m_toolGroup = new QActionGroup(this);
    m_toolGroup->setExclusive(true);
    struct ToolDef { TimelineWidget::Tool tool; icons::Id icon; const char* key; };
    const ToolDef tools[] = {
        {TimelineWidget::Tool::Select, icons::Id::Selection, "V"},
        {TimelineWidget::Tool::TrackSelect, icons::Id::TrackSelectForward, "A"},
        {TimelineWidget::Tool::Ripple, icons::Id::RippleEdit, "B"},
        {TimelineWidget::Tool::Roll, icons::Id::RollingEdit, "N"},
        {TimelineWidget::Tool::RateStretch, icons::Id::RateStretch, "R"},
        {TimelineWidget::Tool::Razor, icons::Id::Razor, "C"},
        {TimelineWidget::Tool::Slip, icons::Id::Slip, "Y"},
        {TimelineWidget::Tool::Slide, icons::Id::Slide, "U"},
        {TimelineWidget::Tool::Hand, icons::Id::Hand, "H"},
        {TimelineWidget::Tool::Zoom, icons::Id::Zoom, "Z"},
    };
    for (const auto& t : tools) {
        auto* a = new QAction(icons::icon(t.icon), TimelineWidget::toolName(t.tool), this);
        a->setToolTip(TimelineWidget::toolName(t.tool));
        a->setShortcut(QKeySequence(QString::fromLatin1(t.key)));
        a->setCheckable(true);
        a->setChecked(t.tool == TimelineWidget::Tool::Select);
        m_toolGroup->addAction(a);
        addAction(a);
        const auto tool = t.tool;
        connect(a, &QAction::triggered, this, [this, tool] { m_timeline->setTool(tool); });
        m_toolActions.insert(static_cast<int>(t.tool), a);
    }
    action(tr("Type Tool"), QKeySequence(Qt::Key_T), [this] { newTitle(); });
    action(tr("Maximize Frame Under Cursor"), QKeySequence(Qt::Key_QuoteLeft), [this] {
        PanelFrame* f = m_maximized ? m_maximized : frameUnderCursor();
        toggleMaximize(f ? f : m_timelineFrame);
    });

    m_snapAction = new QAction(tr("Snap in Timeline"), this);
    m_snapAction->setCheckable(true);
    m_snapAction->setChecked(true);
    m_snapAction->setShortcut(QKeySequence(Qt::Key_S));
    connect(m_snapAction, &QAction::toggled, this, [this](bool on) {
        m_timeline->setSnapping(on);
        showStatus(on ? tr("Snapping on") : tr("Snapping off"));
    });
    addAction(m_snapAction);

    m_linkedAction = new QAction(tr("Linked Selection"), this);
    m_linkedAction->setCheckable(true);
    m_linkedAction->setChecked(true);
    m_linkedAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    connect(m_linkedAction, &QAction::toggled, this, [this](bool on) { m_timeline->setLinkedSelection(on); });
    addAction(m_linkedAction);
}

void MainWindow::buildMenus()
{
    // ---------------------------------------------------------------- File
    QMenu* file = menuBar()->addMenu(tr("&File"));
    action(tr("&New Project..."), QKeySequence(QStringLiteral("Ctrl+Alt+N")), [this] { newProject(); }, file);
    action(tr("New &Sequence..."), QKeySequence::New, [this] { newSequence(); }, file);
    action(tr("&Open Project..."), QKeySequence::Open, [this] { openProject(); }, file);
    m_recentMenu = file->addMenu(tr("Open &Recent"));
    rebuildRecentMenu();
    file->addSeparator();
    action(tr("&Save"), QKeySequence::Save, [this] { saveProjectAs(false); }, file);
    action(tr("Save &As..."), QKeySequence(QStringLiteral("Ctrl+Shift+S")), [this] { saveProjectAs(true); }, file);
    file->addSeparator();
    action(tr("&Import..."), QKeySequence(QStringLiteral("Ctrl+I")), [this] { m_mediaBin->importFiles(); }, file);
    action(tr("Import &Folder..."), {}, [this] { m_mediaBin->importFolder(); }, file);
    action(tr("Import Premiere Pro &Project / Timeline (.prproj, XML, OTIO, EDL)..."), QKeySequence(QStringLiteral("Ctrl+Alt+I")),
           [this] { importInterchange(); }, file);
    file->addSeparator();
    QMenu* newMenu = file->addMenu(tr("Ne&w"));
    action(tr("Title..."), QKeySequence(QStringLiteral("Ctrl+T")), [this] { newTitle(); }, newMenu);
    action(tr("Color Matte..."), {}, [this] { newColorMatte(); }, newMenu);
    action(tr("Adjustment Layer"), {}, [this] { newAdjustmentLayer(); }, newMenu);
    action(tr("3D Model (OBJ)..."), {}, [this] { newModel3D(); }, newMenu);
    action(tr("Remotion / React Composition..."), QKeySequence(QStringLiteral("Ctrl+Alt+R")), [this] { newRemotion(); }, newMenu);
    QMenu* exportMenu = file->addMenu(tr("&Export"));
    action(tr("&Media..."), QKeySequence(QStringLiteral("Ctrl+M")), [this] { showExportDialog(); }, exportMenu);
    action(tr("&Frame..."), QKeySequence(QStringLiteral("Ctrl+Shift+E")), [this] { exportFrame(); }, exportMenu);
    exportMenu->addSeparator();
    action(tr("CMX 3600 EDL..."), {}, [this] { exportInterchange(QStringLiteral("edl")); }, exportMenu);
    action(tr("Final Cut Pro 7 XML (Premiere / Resolve)..."), {}, [this] { exportInterchange(QStringLiteral("fcp7")); }, exportMenu);
    action(tr("FCPXML..."), {}, [this] { exportInterchange(QStringLiteral("fcpxml")); }, exportMenu);
    action(tr("OpenTimelineIO (.otio)..."), {}, [this] { exportInterchange(QStringLiteral("otio")); }, exportMenu);
    file->addSeparator();
    action(tr("E&xit"), QKeySequence::Quit, [this] { close(); }, file);

    // ---------------------------------------------------------------- Edit
    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    addAction(m_undoAction);
    addAction(m_redoAction);
    editMenu->addSeparator();
    action(tr("Cu&t"), QKeySequence::Cut, [this] { m_timeline->cutSelection(); }, editMenu);
    action(tr("&Copy"), QKeySequence::Copy, [this] { m_timeline->copySelection(); }, editMenu);
    action(tr("&Paste"), QKeySequence::Paste, [this] { m_timeline->paste(false); }, editMenu);
    action(tr("Paste &Insert"), QKeySequence(QStringLiteral("Ctrl+Shift+V")), [this] { m_timeline->paste(true); }, editMenu);
    action(tr("C&lear"), QKeySequence::Delete, [this] { m_timeline->deleteSelected(false); }, editMenu);
    action(tr("&Ripple Delete"), QKeySequence(QStringLiteral("Shift+Del")), [this] { m_timeline->deleteSelected(true); }, editMenu);
    action(tr("D&uplicate"), QKeySequence(QStringLiteral("Ctrl+Shift+/")), [this] { m_timeline->duplicateSelection(); }, editMenu);
    editMenu->addSeparator();
    action(tr("Select &All"), QKeySequence::SelectAll, [this] { m_timeline->selectAll(); }, editMenu);
    action(tr("&Deselect All"), QKeySequence(QStringLiteral("Ctrl+Shift+A")), [this] { m_timeline->deselectAll(); }, editMenu);
    editMenu->addSeparator();
    action(tr("&Keyboard Shortcuts..."), QKeySequence(QStringLiteral("Ctrl+Alt+K")), [this] { ShortcutsDialog(this).exec(); }, editMenu);
    action(tr("P&lugins..."), {}, [this] { showPluginManager(); }, editMenu);
    action(tr("Pr&eferences..."), QKeySequence(QStringLiteral("Ctrl+,")), [this] { showPreferences(); }, editMenu);

    // ---------------------------------------------------------------- Clip
    QMenu* clip = menuBar()->addMenu(tr("&Clip"));
    action(tr("&Rename..."), {}, [this] { m_timeline->renameSelected(); }, clip);
    action(tr("Speed/&Duration..."), QKeySequence(QStringLiteral("Ctrl+R")), [this] { m_timeline->speedDialog(); }, clip);
    action(tr("Re&verse"), {}, [this] { m_timeline->reverseSelection(); }, clip);
    action(tr("&Enable"), QKeySequence(QStringLiteral("Shift+E")), [this] { m_timeline->toggleEnableSelection(); }, clip);
    clip->addSeparator();
    action(tr("&Link"), QKeySequence(QStringLiteral("Ctrl+L")), [this] {
        // Ctrl+L toggles: unlink when the selection is one linked group.
        const Sequence* seq = m_document->sequence();
        const QStringList sel = m_document->selectedClips();
        bool allLinked = !sel.isEmpty();
        QString link;
        for (const QString& id : sel) {
            const Clip* c = seq ? seq->findClip(id) : nullptr;
            if (!c || c->linkId.isEmpty() || (!link.isEmpty() && c->linkId != link)) allLinked = false;
            if (c) link = c->linkId;
        }
        if (allLinked) m_timeline->unlinkSelection();
        else m_timeline->linkSelection();
    }, clip);
    action(tr("&Unlink"), {}, [this] { m_timeline->unlinkSelection(); }, clip);
    clip->addSeparator();
    action(tr("Edit &Title..."), {}, [this] { editTitle(m_document->selectedClipId()); }, clip);
    action(tr("&Nest..."), {}, [this] { m_timeline->nestSelection(); }, clip);
    action(tr("Nudge Left"), QKeySequence(QStringLiteral("Alt+Left")), [this] { m_timeline->nudge(-1); }, clip);
    action(tr("Nudge Right"), QKeySequence(QStringLiteral("Alt+Right")), [this] { m_timeline->nudge(1); }, clip);
    action(tr("Nudge Up a Track"), QKeySequence(QStringLiteral("Alt+Up")), [this] { m_timeline->nudgeLane(1); }, clip);
    action(tr("Nudge Down a Track"), QKeySequence(QStringLiteral("Alt+Down")), [this] { m_timeline->nudgeLane(-1); }, clip);

    // ------------------------------------------------------------ Sequence
    QMenu* sequence = menuBar()->addMenu(tr("&Sequence"));
    action(tr("Sequence &Settings..."), {}, [this] { sequenceSettings(); }, sequence);
    sequence->addSeparator();
    action(tr("&Play / Stop"), QKeySequence(Qt::Key_Space), [this] { playPauseProgram(); }, sequence);
    action(tr("Shuttle Left"), QKeySequence(Qt::Key_J), [this] { shuttle(-1); }, sequence);
    action(tr("Shuttle Stop"), QKeySequence(Qt::Key_K), [this] { shuttle(0); }, sequence);
    action(tr("Shuttle Right"), QKeySequence(Qt::Key_L), [this] { shuttle(1); }, sequence);
    action(tr("Loop Playback"), {}, [this] { setLoop(!m_loop); showStatus(m_loop ? tr("Loop on") : tr("Loop off")); }, sequence);
    sequence->addSeparator();
    action(tr("&Insert from Source"), QKeySequence(Qt::Key_Comma), [this] { editFromSource(edit::PlaceMode::Insert); }, sequence);
    action(tr("&Overwrite from Source"), QKeySequence(Qt::Key_Period), [this] { editFromSource(edit::PlaceMode::Overwrite); }, sequence);
    action(tr("&Lift"), QKeySequence(Qt::Key_Semicolon), [this] { m_timeline->liftInOut(); }, sequence);
    action(tr("E&xtract"), QKeySequence(Qt::Key_Apostrophe), [this] { m_timeline->extractInOut(); }, sequence);
    sequence->addSeparator();
    action(tr("Add &Edit"), QKeySequence(QStringLiteral("Ctrl+K")), [this] { m_timeline->splitAtPlayhead(); }, sequence);
    action(tr("Add Edit to &All Tracks"), QKeySequence(QStringLiteral("Ctrl+Shift+K")), [this] { m_timeline->splitAllAtPlayhead(); }, sequence);
    action(tr("Ripple Trim Previous Edit to Playhead"), QKeySequence(Qt::Key_Q), [this] { m_timeline->rippleTrimToPlayhead(true); }, sequence);
    action(tr("Ripple Trim Next Edit to Playhead"), QKeySequence(Qt::Key_W), [this] { m_timeline->rippleTrimToPlayhead(false); }, sequence);
    action(tr("Close &Gaps"), {}, [this] { m_timeline->closeAllGaps(); }, sequence);
    sequence->addSeparator();
    action(tr("Apply &Video Transition"), QKeySequence(QStringLiteral("Ctrl+D")), [this] { m_timeline->addDefaultTransition(false); }, sequence);
    action(tr("Apply &Audio Transition"), QKeySequence(QStringLiteral("Ctrl+Shift+D")), [this] { m_timeline->addDefaultTransition(true); }, sequence);
    sequence->addSeparator();
    action(tr("Match &Frame"), QKeySequence(Qt::Key_F), [this] { m_timeline->matchFrame(); }, sequence);
    action(tr("Select Clip at Playhead"), QKeySequence(Qt::Key_D), [this] { m_timeline->selectClipAtPlayhead(); }, sequence);
    action(tr("Go to Previous Edit"), QKeySequence(Qt::Key_Up), [this] { m_timeline->goToEdit(false); }, sequence);
    action(tr("Go to Next Edit"), QKeySequence(Qt::Key_Down), [this] { m_timeline->goToEdit(true); }, sequence);
    action(tr("Step Back"), QKeySequence(Qt::Key_Left), [this] { stepProgram(-1); }, nullptr);
    action(tr("Step Forward"), QKeySequence(Qt::Key_Right), [this] { stepProgram(1); }, nullptr);
    action(tr("Step Back 5"), QKeySequence(QStringLiteral("Shift+Left")), [this] { stepProgram(-5); }, nullptr);
    action(tr("Step Forward 5"), QKeySequence(QStringLiteral("Shift+Right")), [this] { stepProgram(5); }, nullptr);
    action(tr("Go to Start"), QKeySequence(Qt::Key_Home), [this] { pauseProgram(); m_document->setPlayhead(0); }, nullptr);
    action(tr("Go to End"), QKeySequence(Qt::Key_End), [this] {
        pauseProgram();
        if (const Sequence* s = m_document->sequence()) m_document->setPlayhead(s->duration);
    }, nullptr);
    sequence->addSeparator();
    sequence->addAction(m_snapAction);
    sequence->addAction(m_linkedAction);
    sequence->addSeparator();
    action(tr("Add Video Track"), {}, [this] { m_timeline->addTrack(false); }, sequence);
    action(tr("Add Audio Track"), {}, [this] { m_timeline->addTrack(true); }, sequence);
    sequence->addSeparator();
    action(tr("Zoom In"), QKeySequence(Qt::Key_Equal), [this] { m_timeline->zoomIn(); }, sequence);
    action(tr("Zoom Out"), QKeySequence(Qt::Key_Minus), [this] { m_timeline->zoomOut(); }, sequence);
    action(tr("Zoom to Sequence"), QKeySequence(Qt::Key_Backslash), [this] { m_timeline->zoomToFit(); }, sequence);

    // ------------------------------------------------------------- Markers
    QMenu* markers = menuBar()->addMenu(tr("&Markers"));
    action(tr("Mark &In"), QKeySequence(Qt::Key_I), [this] { m_timeline->markIn(); }, markers);
    action(tr("Mark &Out"), QKeySequence(Qt::Key_O), [this] { m_timeline->markOut(); }, markers);
    action(tr("Mark &Clip"), QKeySequence(Qt::Key_X), [this] { m_timeline->markClip(); }, markers);
    action(tr("Go to In"), QKeySequence(QStringLiteral("Shift+I")), [this] {
        if (const Sequence* s = m_document->sequence(); s && s->inPoint >= 0) m_document->setPlayhead(s->inPoint);
    }, markers);
    action(tr("Go to Out"), QKeySequence(QStringLiteral("Shift+O")), [this] {
        if (const Sequence* s = m_document->sequence(); s && s->outPoint >= 0) m_document->setPlayhead(s->outPoint);
    }, markers);
    action(tr("Clear In and Out"), QKeySequence(QStringLiteral("Ctrl+Shift+X")), [this] { m_timeline->clearInOut(); }, markers);
    markers->addSeparator();
    action(tr("Add &Marker"), QKeySequence(Qt::Key_M), [this] { m_timeline->addMarker(); }, markers);
    action(tr("Go to Next Marker"), QKeySequence(QStringLiteral("Shift+M")), [this] { m_timeline->goToMarker(true); }, markers);
    action(tr("Go to Previous Marker"), QKeySequence(QStringLiteral("Ctrl+Shift+M")), [this] { m_timeline->goToMarker(false); }, markers);
    action(tr("Clear Current Marker"), QKeySequence(QStringLiteral("Ctrl+Alt+M")), [this] { m_timeline->clearMarkerAtPlayhead(); }, markers);

    // -------------------------------------------------------------- Window
    QMenu* window = menuBar()->addMenu(tr("&Window"));
    QMenu* workspaces = window->addMenu(tr("Workspaces"));
    const QStringList names = {tr("Editing"), tr("Assembly"), tr("Color"), tr("Effects"), tr("Audio")};
    for (int i = 0; i < names.size(); ++i)
        action(names[i], QKeySequence(QStringLiteral("Alt+Shift+%1").arg(i + 1)), [this, i] {
            applyWorkspace(i);
        }, workspaces);
    action(tr("Reset Layout"), {}, [this] {
        if (m_maximized) toggleMaximize(m_maximized);
        m_topSplit->setSizes({560, 780});
        m_bottomSplit->setSizes({300, 34, 960, 46});
        m_vSplit->setSizes({390, 320});
        applyWorkspace(0);
    }, workspaces);
    window->addSeparator();
    const QList<QPair<QString, QWidget*>> panels = {
        {tr("Project"), m_mediaBin}, {tr("Effects"), m_effects}, {tr("History"), m_history},
        {tr("Render Queue"), m_queue}, {tr("Source Monitor"), m_sourceMonitor},
        {tr("Effect Controls"), m_effectControls}, {tr("Lumetri Color"), m_lumetri},
        {tr("Audio Track Mixer"), m_mixer}, {tr("Lumetri Scopes"), m_scopes},
        {tr("Program Monitor"), m_programMonitor}};
    for (const auto& p : panels) {
        QWidget* w = p.second;
        action(p.first, {}, [this, w] { raisePanel(w); }, window);
    }
    action(tr("Timeline"), {}, [this] { if (m_maximized) toggleMaximize(m_maximized); m_timeline->setFocus(); }, window);
    window->addSeparator();
    QMenu* scopeMenu = window->addMenu(tr("Scope Mode"));
    action(tr("Waveform (Luma)"), {}, [this] { m_scopes->setMode(0); raisePanel(m_scopes); }, scopeMenu);
    action(tr("RGB Parade"), {}, [this] { m_scopes->setMode(1); raisePanel(m_scopes); }, scopeMenu);
    action(tr("Vectorscope YUV"), {}, [this] { m_scopes->setMode(2); raisePanel(m_scopes); }, scopeMenu);

    // ---------------------------------------------------------------- Help
    QMenu* help = menuBar()->addMenu(tr("&Help"));
    action(tr("&Keyboard Shortcuts"), QKeySequence(Qt::Key_F1), [this] { ShortcutsDialog(this).exec(); }, help);
    action(tr("&About Breefer Composer"), {}, [this] {
        QMessageBox::about(this, tr("Breefer Composer"),
                           tr("<h3>Breefer Composer %1</h3>"
                              "<p>A free, open-source non-linear video editor built for modest hardware.</p>"
                              "<p>Encoders available in this build: %2</p>"
                              "<p>Qt %3 - FFmpeg (LGPL) - miniaudio</p>")
                               .arg(QApplication::applicationVersion(),
                                    Exporter::availableVideoCodecs().join(QStringLiteral(", ")),
                                    QString::fromLatin1(qVersion())));
    }, help);
}

void MainWindow::buildToolbar()
{
    // Premiere has no main toolbar: tools live in the Tools panel and the
    // header bar carries Import / Edit / Export.
}

void MainWindow::applyWorkspace(int index)
{
    // 0 Editing, 1 Assembly, 2 Color, 3 Effects, 4 Audio
    if (m_maximized) toggleMaximize(m_maximized);
    const int total = std::max(800, m_topSplit->width());
    switch (index) {
    case 1:   // Assembly: big Project panel
        raisePanel(m_mediaBin);
        m_mediaBin->setFocus();
        m_bottomSplit->setSizes({std::max(420, total / 2), 34, total / 2, 46});
        break;
    case 2:   // Color
        raisePanel(m_lumetri);
        raisePanel(m_scopes == nullptr ? m_lumetri : m_lumetri);
        break;
    case 3:   // Effects
        raisePanel(m_effectControls);
        raisePanel(m_effects);
        break;
    case 4:   // Audio
        raisePanel(m_mixer);
        break;
    default:  // Editing
        raisePanel(m_sourceMonitor);
        raisePanel(m_mediaBin);
        m_bottomSplit->setSizes({300, 34, std::max(400, total - 380), 46});
        break;
    }
    showStatus(tr("Workspace: %1").arg(QStringList{tr("Editing"), tr("Assembly"), tr("Color"), tr("Effects"), tr("Audio")}.value(index)));
}

// ----------------------------------------------------------------------- wire

void MainWindow::wire()
{
    connect(m_document, &Document::structureChanged, this, &MainWindow::syncEngineSources);
    connect(m_document, &Document::structureChanged, this, &MainWindow::updateUndoActions);
    connect(m_document, &Document::structureChanged, this, &MainWindow::refreshTitle);
    connect(m_document, &Document::selectionChanged, this, &MainWindow::updateUndoActions);
    connect(m_document, &Document::sequencesChanged, this, &MainWindow::refreshSequenceCombo);
    connect(m_document, &Document::documentLoaded, this, [this] {
        // Every video clip carries Motion and Opacity, as in Premiere.
        for (auto& seq : m_document->project().sequences)
            for (auto& t : seq.tracks)
                if (t.kind == TrackKind::Video)
                    for (auto& c : t.clips) fx::ensureIntrinsics(c);
        syncEngineSources();
        refreshSequenceCombo();
        m_programEngine->invalidate();
        QTimer::singleShot(0, m_timeline, &TimelineWidget::zoomToFit);
    });

    connect(m_document, &Document::playheadChanged, this, [this](Tick tick) {
        const Sequence* seq = m_document->sequence();
        const FrameRate rate = seq ? seq->rate : FrameRate{25, 1};
        m_programMonitor->setTimecode(formatTimecode(tick, rate));
        m_programMonitor->setPosition(tick);
        if (m_syncingPlayhead) return;
        // A user-driven move while playing jumps playback there.
        m_programEngine->seek(tick);
        if (m_programEngine->isPlaying() && m_programEngine->speed() == 1.0) m_audio->seek(tick);
    });

    connect(m_programEngine, &PlaybackEngine::frameReady, this, [this](const QImage& image, Tick) {
        m_programMonitor->setImage(image);
        if (m_scopes && m_scopes->isVisible()) m_scopes->setImage(image);
    });
    connect(m_programEngine, &PlaybackEngine::positionChanged, this, [this](Tick tick) {
        m_syncingPlayhead = true;
        m_document->setPlayhead(tick);
        m_syncingPlayhead = false;
    });
    connect(m_programEngine, &PlaybackEngine::playbackStateChanged, this, [this](bool playing) {
        m_programMonitor->setPlaying(playing);
        if (!playing) m_audio->pause();
    });
    connect(m_programEngine, &PlaybackEngine::looped, this, [this](Tick to) { m_audio->seek(to); });
    connect(m_programEngine, &PlaybackEngine::qualityChanged, this, [this](int height) {
        // Adaptive drop: reflect the resolution actually used in the monitor menu.
        const Sequence* s = m_document->sequence();
        const int ratio = std::max(1, (s ? s->height : 1080) / std::max(1, height));
        int divisor = 1;
        while (divisor * 2 <= ratio && divisor < 16) divisor *= 2;
        m_programMonitor->setResolutionDivisor(divisor);
    });
    connect(m_programMonitor, &MonitorWidget::resolutionChanged, this, &MainWindow::setPlaybackDivisor);
    connect(m_effectControls, &EffectControls::editTitleRequested, this, &MainWindow::editTitle);
    connect(m_timeline, &TimelineWidget::snappingToggled, m_snapAction, &QAction::setChecked);
    connect(m_timeline, &TimelineWidget::linkedSelectionToggled, m_linkedAction, &QAction::setChecked);
    connect(m_snapAction, &QAction::toggled, m_timeline, &TimelineWidget::setSnapping);
    connect(m_linkedAction, &QAction::toggled, m_timeline, &TimelineWidget::setLinkedSelection);
    connect(m_programEngine, &PlaybackEngine::droppedFramesChanged, this,
            [this](int total) { m_statsLabel->setText(tr("dropped frames: %1").arg(total)); });

    connect(m_timeline, &TimelineWidget::scrubStarted, this, &MainWindow::pauseProgram);
    connect(m_timeline, &TimelineWidget::statusMessage, this, &MainWindow::showStatus);
    connect(m_timeline, &TimelineWidget::toolChanged, this, [this](TimelineWidget::Tool tool) {
        m_toolLabel->setText(TimelineWidget::toolName(tool));
        showStatus(TimelineWidget::toolName(tool));
        if (QAction* a = m_toolActions.value(static_cast<int>(tool))) a->setChecked(true);
    });
    connect(m_timeline, &TimelineWidget::matchFrameRequested, this,
            [this](const QString& mediaId, Tick at) { setSourceMedia(mediaId, at); });
    connect(m_timeline, &TimelineWidget::clipActivated, this, [this](const QString& clipId) {
        const Sequence* seq = m_document->sequence();
        const Clip* c = seq ? seq->findClip(clipId) : nullptr;
        if (c && !c->isGenerator()) setSourceMedia(c->mediaId, c->sourceIn);
    });
    connect(m_timeline, &TimelineWidget::titleEditRequested, this, &MainWindow::editTitle);
    connect(m_timeline, &TimelineWidget::openSequenceRequested, this, [this](const QString& id) {
        pauseProgram();
        m_document->setSequence(id);
        QTimer::singleShot(0, m_timeline, &TimelineWidget::zoomToFit);
    });
    connect(m_timeline, &TimelineWidget::mediaDropped, this,
            [this](const QStringList& ids, Tick at, int track, bool insert) {
                placeMedia(ids, at, track, insert ? edit::PlaceMode::Insert : edit::PlaceMode::Overwrite);
            });
    connect(m_timeline, &TimelineWidget::filesDropped, this,
            [this](const QStringList& dropped, Tick at, int track, bool insert) {
                QStringList paths;
                for (const QString& p : dropped) {
                    const QString ext = QFileInfo(p).suffix().toLower();
                    if (ext == QLatin1String("prproj") || ext == QLatin1String("otio") || ext == QLatin1String("edl")
                        || ext == QLatin1String("xml"))
                        importInterchangeFile(p);
                    else
                        paths << p;
                }
                if (paths.isEmpty()) return;
                QStringList failures;
                const QStringList ids = m_mediaBin->importPaths(paths, &failures);
                if (!ids.isEmpty()) placeMedia(ids, at, track, insert ? edit::PlaceMode::Insert : edit::PlaceMode::Overwrite);
                if (!failures.isEmpty())
                    QMessageBox::warning(this, tr("Some files could not be imported"), failures.join(QLatin1Char('\n')));
            });

    connect(m_mediaBin, &MediaBin::mediaActivated, this, [this](const QString& id) { setSourceMedia(id); });
    connect(m_mediaBin, &MediaBin::insertRequested, this, [this](const QString& id) {
        setSourceMedia(id);
        editFromSource(edit::PlaceMode::Insert);
    });
    connect(m_mediaBin, &MediaBin::overwriteRequested, this, [this](const QString& id) {
        setSourceMedia(id);
        editFromSource(edit::PlaceMode::Overwrite);
    });
    connect(m_mediaBin, &MediaBin::newSequenceRequested, this, &MainWindow::newSequence);
    connect(m_mediaBin, &MediaBin::newTitleRequested, this, &MainWindow::newTitle);
    connect(m_mediaBin, &MediaBin::newColorMatteRequested, this, &MainWindow::newColorMatte);
    connect(m_mediaBin, &MediaBin::newAdjustmentRequested, this, &MainWindow::newAdjustmentLayer);
    connect(m_mediaBin, &MediaBin::newModelRequested, this, &MainWindow::newModel3D);
    connect(m_mediaBin, &MediaBin::newRemotionRequested, this, &MainWindow::newRemotion);
    connect(m_mediaBin, &MediaBin::rerenderRequested, this, &MainWindow::rerenderRemotion);
    connect(m_mediaBin, &MediaBin::sequenceActivated, this, [this](const QString& id) {
        pauseProgram();
        m_document->setSequence(id);
        m_timeline->zoomToFit();
    });
    connect(m_mediaBin, &MediaBin::statusMessage, this, &MainWindow::showStatus);

    connect(m_document, &Document::mediaChanged, this, &MainWindow::autoQueueProxies);
    connect(m_proxies, &ProxyManager::progress, this, [this](const QString&, int percent) {
        if (percent % 10 == 0) showStatus(tr("Generating proxy: %1%").arg(percent));
    });
    connect(m_proxies, &ProxyManager::finished, this, [this](const QString& mediaId, const QString& proxyPath) {
        m_document->setMediaProxy(mediaId, proxyPath);
        m_programEngine->invalidate();
        m_sourceEngine->invalidate();
        showStatus(tr("Proxy ready: %1").arg(QFileInfo(proxyPath).fileName()));
    });
    connect(m_proxies, &ProxyManager::failed, this,
            [this](const QString&, const QString& error) { showStatus(tr("Proxy generation failed: %1").arg(error)); });

    // Source monitor.
    connect(m_sourceEngine, &PlaybackEngine::frameReady, this,
            [this](const QImage& image, Tick) { m_sourceMonitor->setImage(image); });
    connect(m_sourceEngine, &PlaybackEngine::positionChanged, this, [this](Tick tick) {
        m_sourceMonitor->setTimecode(formatTimecode(tick, m_sourceSequence.rate));
        m_sourceMonitor->setPosition(tick);
    });
    connect(m_sourceEngine, &PlaybackEngine::playbackStateChanged, m_sourceMonitor, &MonitorWidget::setPlaying);
    auto sourceSeek = [this](Tick t) {
        t = std::clamp<Tick>(t, 0, std::max<Tick>(0, m_sourceSequence.duration - m_sourceSequence.rate.ticksPerFrame()));
        m_sourceEngine->seek(t);
        m_sourceMonitor->setTimecode(formatTimecode(t, m_sourceSequence.rate));
        m_sourceMonitor->setPosition(t);
    };
    connect(m_sourceMonitor, &MonitorWidget::playPauseRequested, this, &MainWindow::playPauseSource);
    connect(m_sourceMonitor, &MonitorWidget::goStartRequested, this, [sourceSeek] { sourceSeek(0); });
    connect(m_sourceMonitor, &MonitorWidget::goEndRequested, this, [this, sourceSeek] { sourceSeek(m_sourceSequence.duration); });
    connect(m_sourceMonitor, &MonitorWidget::scrubbed, this, sourceSeek);
    connect(m_sourceMonitor, &MonitorWidget::scrubStarted, m_sourceEngine, &PlaybackEngine::pause);
    connect(m_sourceMonitor, &MonitorWidget::stepRequested, this, [this, sourceSeek](int frames) {
        sourceSeek(m_sourceEngine->position() + m_sourceSequence.rate.ticksForFrames(frames));
    });
    connect(m_sourceMonitor, &MonitorWidget::setInRequested, this, [this] {
        m_sourceIn = m_sourceEngine->position();
        if (m_sourceOut <= m_sourceIn) m_sourceOut = -1;
        updateSourceMarks();
    });
    connect(m_sourceMonitor, &MonitorWidget::setOutRequested, this, [this] {
        m_sourceOut = m_sourceEngine->position() + m_sourceSequence.rate.ticksPerFrame();
        if (m_sourceIn >= m_sourceOut) m_sourceIn = -1;
        updateSourceMarks();
    });
    connect(m_sourceMonitor, &MonitorWidget::goInRequested, this, [this, sourceSeek] { sourceSeek(std::max<Tick>(0, m_sourceIn)); });
    connect(m_sourceMonitor, &MonitorWidget::goOutRequested, this, [this, sourceSeek] {
        sourceSeek(m_sourceOut > 0 ? m_sourceOut - m_sourceSequence.rate.ticksPerFrame() : m_sourceSequence.duration);
    });
    connect(m_sourceMonitor, &MonitorWidget::insertRequested, this, [this] { editFromSource(edit::PlaceMode::Insert); });
    connect(m_sourceMonitor, &MonitorWidget::overwriteRequested, this, [this] { editFromSource(edit::PlaceMode::Overwrite); });
    connect(m_sourceMonitor, &MonitorWidget::exportFrameRequested, this, [this] {
        const QImage img = m_sourceMonitor->currentImage();
        if (img.isNull()) return;
        const QString path = QFileDialog::getSaveFileName(this, tr("Export Frame"), QStringLiteral("source-frame.png"),
                                                          tr("PNG (*.png);;JPEG (*.jpg)"));
        if (!path.isEmpty() && img.save(path)) showStatus(tr("Saved %1").arg(QFileInfo(path).fileName()));
    });

    // Program monitor.
    connect(m_programMonitor, &MonitorWidget::playPauseRequested, this, &MainWindow::playPauseProgram);
    connect(m_programMonitor, &MonitorWidget::goStartRequested, this, [this] { pauseProgram(); m_document->setPlayhead(0); });
    connect(m_programMonitor, &MonitorWidget::goEndRequested, this, [this] {
        pauseProgram();
        if (const Sequence* seq = m_document->sequence()) m_document->setPlayhead(seq->duration);
    });
    connect(m_programMonitor, &MonitorWidget::stepRequested, this, &MainWindow::stepProgram);
    connect(m_programMonitor, &MonitorWidget::scrubStarted, this, &MainWindow::pauseProgram);
    connect(m_programMonitor, &MonitorWidget::scrubbed, this, [this](Tick t) {
        if (const Sequence* s = m_document->sequence()) m_document->setPlayhead(s->rate.snap(t));
    });
    connect(m_programMonitor, &MonitorWidget::setInRequested, m_timeline, &TimelineWidget::markIn);
    connect(m_programMonitor, &MonitorWidget::setOutRequested, m_timeline, &TimelineWidget::markOut);
    connect(m_programMonitor, &MonitorWidget::goInRequested, this, [this] {
        if (const Sequence* s = m_document->sequence(); s && s->inPoint >= 0) m_document->setPlayhead(s->inPoint);
    });
    connect(m_programMonitor, &MonitorWidget::goOutRequested, this, [this] {
        if (const Sequence* s = m_document->sequence(); s && s->outPoint >= 0) m_document->setPlayhead(s->outPoint);
    });
    connect(m_programMonitor, &MonitorWidget::liftRequested, m_timeline, &TimelineWidget::liftInOut);
    connect(m_programMonitor, &MonitorWidget::extractRequested, m_timeline, &TimelineWidget::extractInOut);
    connect(m_programMonitor, &MonitorWidget::addMarkerRequested, m_timeline, &TimelineWidget::addMarker);
    connect(m_sourceMonitor, &MonitorWidget::addMarkerRequested, this, [this] { showStatus(tr("Source clip markers are not supported yet - add markers on the timeline")); });
    connect(m_programMonitor, &MonitorWidget::exportFrameRequested, this, &MainWindow::exportFrame);
    connect(m_programMonitor, &MonitorWidget::loopToggled, this, &MainWindow::setLoop);

    connect(m_exporter, &Exporter::finished, this, [this](const QString& path) {
        showStatus(tr("Export finished: %1").arg(QFileInfo(path).fileName()));
    });
    connect(m_exporter, &Exporter::failed, this, [this](const QString& error) {
        showStatus(tr("Export failed: %1").arg(error));
    });
    connect(m_exporter, &Exporter::progress, this, [this](int p) {
        m_statsLabel->setText(tr("Exporting %1%").arg(p));
    });

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(50);
    connect(m_meterTimer, &QTimer::timeout, this, [this] {
        float left = 0.0f;
        float right = 0.0f;
        m_audio->peaks(left, right);
        m_meters->setLevels(left, right);
    });
    m_meterTimer->start();

    updateUndoActions();
    refreshSequenceCombo();
}

// ------------------------------------------------------------------- document

void MainWindow::syncEngineSources()
{
    const Sequence seq = m_document->snapshotSequence();
    QVector<MediaRef> media = m_document->project().media;
    if (!m_useProxies)
        for (auto& ref : media) ref.proxyPath.clear();

    m_programEngine->setSource(seq, media, m_document->project().sequences);
    m_audio->setSource(seq, media, m_document->project().sequences);
    if (m_loop) setLoop(true);
    updateProgramInfo();
}

void MainWindow::updateProgramInfo()
{
    const Sequence* s = m_document->sequence();
    if (!s) return;
    m_programMonitor->setInfoText(tr("%1x%2 - %3 fps").arg(s->width).arg(s->height).arg(s->rate.label()));
    m_programMonitor->setDuration(std::max<Tick>(s->duration, s->rate.ticksPerFrame()));
    m_programMonitor->setDurationText(formatTimecode(s->duration, s->rate));
    m_programMonitor->setMarks(s->inPoint, s->outPoint);
    QVector<Tick> markers;
    for (const auto& m : s->markers) markers << m.time;
    m_programMonitor->setMarkers(markers);
}

void MainWindow::refreshSequenceCombo()
{
    if (!m_sequenceTabs) return;
    QSignalBlocker blocker(m_sequenceTabs);
    while (m_sequenceTabs->count() > 0) m_sequenceTabs->removeTab(0);
    for (const auto& s : m_document->project().sequences) {
        const int i = m_sequenceTabs->addTab(s.name);
        m_sequenceTabs->setTabData(i, s.id);
        if (s.id == m_document->sequenceId()) m_sequenceTabs->setCurrentIndex(i);
    }
    if (const Sequence* s = m_document->sequence()) m_programTabs->setTabText(0, tr("Program: %1").arg(s->name));
}

void MainWindow::autoQueueProxies()
{
    if (m_scanningProxies) return;
    m_scanningProxies = true;
    const QVector<MediaRef> media = m_document->project().media;
    for (const auto& ref : media) {
        if (!ref.hasVideo || ref.height <= m_proxyThreshold || ref.offline || ref.isStillImage()) continue;
        const QString existing = ProxyManager::proxyPathFor(ref, 540);
        if (QFileInfo::exists(existing)) {
            if (ref.proxyPath != existing) m_document->setMediaProxy(ref.id, existing);
            continue;
        }
        if (!ref.proxyPath.isEmpty()) continue;
        m_proxies->enqueue(ref, 540);
        showStatus(tr("Generating proxy for %1...").arg(ref.name));
    }
    m_scanningProxies = false;
}

void MainWindow::refreshTitle()
{
    const QString name = m_currentFile.isEmpty() ? m_document->project().name : QFileInfo(m_currentFile).fileName();
    setWindowTitle(tr("%1%2 - Breefer Composer").arg(name, m_document->isDirty() ? QStringLiteral(" *") : QString()));
}

void MainWindow::updateUndoActions()
{
    m_undoAction->setEnabled(m_document->undoStack().canUndo());
    m_redoAction->setEnabled(m_document->undoStack().canRedo());
    const QString undoText = m_document->undoStack().undoText();
    m_undoAction->setText(undoText.isEmpty() ? tr("&Undo") : tr("&Undo %1").arg(undoText));
    const QString redoText = m_document->undoStack().redoText();
    m_redoAction->setText(redoText.isEmpty() ? tr("&Redo") : tr("&Redo %1").arg(redoText));
}

void MainWindow::showStatus(const QString& message)
{
    if (m_messageLabel) m_messageLabel->setText(message);
}

void MainWindow::addRecent(const QString& path)
{
    QSettings s = appSettings();
    QStringList recent = s.value(QStringLiteral("recentProjects")).toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > 10) recent.removeLast();
    s.setValue(QStringLiteral("recentProjects"), recent);
    rebuildRecentMenu();
}

void MainWindow::rebuildRecentMenu()
{
    if (!m_recentMenu) return;
    m_recentMenu->clear();
    QSettings s = appSettings();
    const QStringList recent = s.value(QStringLiteral("recentProjects")).toStringList();
    for (const QString& path : recent) {
        if (!QFileInfo::exists(path)) continue;
        m_recentMenu->addAction(QFileInfo(path).fileName(), this, [this, path] {
            if (maybeSave()) openProjectFile(path);
        })->setToolTip(path);
    }
    if (m_recentMenu->isEmpty()) m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
}

bool MainWindow::maybeSave()
{
    if (!m_document->isDirty()) return true;
    const auto answer = QMessageBox::question(this, tr("Save changes?"),
                                              tr("Save changes to \"%1\" before closing?")
                                                  .arg(m_currentFile.isEmpty() ? m_document->project().name
                                                                               : QFileInfo(m_currentFile).fileName()),
                                              QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Cancel) return false;
    if (answer == QMessageBox::Save) return saveProjectAs(false);
    return true;
}

void MainWindow::createProject(const QString& name, const QString& folder)
{
    pauseProgram();
    m_document->newProject();
    m_document->project().name = name;
    QDir dir(folder);
    dir.mkpath(QStringLiteral("."));
    m_currentFile = dir.filePath(name + QStringLiteral(".bcproj"));
    refreshTitle();
    saveProjectAs(false);
    showStatus(tr("Created %1").arg(QFileInfo(m_currentFile).fileName()));
}

void MainWindow::newProject()
{
    if (!maybeSave()) return;
    pauseProgram();
    m_document->newProject();
    m_currentFile.clear();
    m_sourceMediaId.clear();
    m_sourceMonitor->clearImage();
    refreshTitle();
    showStatus(tr("New project"));
}

void MainWindow::openProject()
{
    if (!maybeSave()) return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QString(),
                                                      tr("Breefer Project (*.bcproj);;All Files (*)"));
    if (!path.isEmpty()) openProjectFile(path);
}

bool MainWindow::openProjectFile(const QString& path)
{
    pauseProgram();
    QString toLoad = path;
    const QString autosave = path + QStringLiteral(".autosave");
    if (QFileInfo::exists(autosave) && QFileInfo(autosave).lastModified() > QFileInfo(path).lastModified()) {
        if (QMessageBox::question(this, tr("Recover project"),
                                  tr("An auto-saved version of this project is newer than the saved file "
                                     "(%1). Recover it?").arg(QFileInfo(autosave).lastModified().toString()))
            == QMessageBox::Yes)
            toLoad = autosave;
    }
    QString error;
    if (!m_document->load(toLoad, &error)) {
        QMessageBox::warning(this, tr("Cannot open project"), error);
        return false;
    }
    if (toLoad != path) m_document->markMetaDirty();
    m_currentFile = path;
    m_sourceMediaId.clear();
    m_sourceMonitor->clearImage();
    addRecent(path);
    refreshTitle();

    int offline = 0;
    for (const auto& m : m_document->project().media)
        if (m.offline) ++offline;
    showStatus(offline > 0 ? tr("Opened %1 - %2 media file(s) offline: right-click them in the Project panel to relink")
                                 .arg(QFileInfo(path).fileName()).arg(offline)
                           : tr("Opened %1").arg(QFileInfo(path).fileName()));
    return true;
}

bool MainWindow::saveProjectAs(bool forceDialog)
{
    QString path = m_currentFile;
    if (path.isEmpty() || forceDialog) {
        path = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                            path.isEmpty() ? m_document->project().name + QStringLiteral(".bcproj") : path,
                                            tr("Breefer Project (*.bcproj)"));
        if (path.isEmpty()) return false;
        if (!path.endsWith(QStringLiteral(".bcproj"), Qt::CaseInsensitive)) path += QStringLiteral(".bcproj");
    }
    QString error;
    if (!m_document->save(path, &error)) {
        QMessageBox::warning(this, tr("Cannot save project"), error);
        return false;
    }
    QFile::remove(path + QStringLiteral(".autosave"));
    m_currentFile = path;
    addRecent(path);
    refreshTitle();
    showStatus(tr("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::exportInterchange(const QString& kind)
{
    const Sequence* sequence = m_document->sequence();
    if (!sequence) return;
    QString filter;
    QString suffix;
    if (kind == QLatin1String("edl")) { filter = tr("CMX 3600 EDL (*.edl)"); suffix = QStringLiteral("edl"); }
    else if (kind == QLatin1String("fcp7")) { filter = tr("FCP 7 XML (*.xml)"); suffix = QStringLiteral("xml"); }
    else if (kind == QLatin1String("fcpxml")) { filter = tr("FCPXML (*.fcpxml)"); suffix = QStringLiteral("fcpxml"); }
    else { filter = tr("OpenTimelineIO (*.otio)"); suffix = QStringLiteral("otio"); }

    const QString path = QFileDialog::getSaveFileName(this, tr("Export Timeline"), sequence->name + QLatin1Char('.') + suffix, filter);
    if (path.isEmpty()) return;
    QString error;
    bool ok = false;
    if (kind == QLatin1String("edl")) ok = io::exportEdl(*sequence, m_document->project(), path, &error);
    else if (kind == QLatin1String("fcp7")) ok = io::exportFcp7Xml(*sequence, m_document->project(), path, &error);
    else if (kind == QLatin1String("fcpxml")) ok = io::exportFcpXml(*sequence, m_document->project(), path, &error);
    else ok = io::exportOtio(*sequence, m_document->project(), path, &error);
    if (!ok) {
        QMessageBox::warning(this, tr("Export failed"), error);
        return;
    }
    showStatus(tr("Exported %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::importInterchange()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Import Project or Timeline"), QString(),
        tr("Projects and timelines (*.prproj *.otio *.xml *.edl);;Premiere Pro Project (*.prproj);;All Files (*)"));
    if (!path.isEmpty()) importInterchangeFile(path);
}

void MainWindow::importInterchangeFile(const QString& path)
{
    QVector<Sequence> sequences;
    QVector<MediaRef> media;
    QStringList warnings;
    QString error;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = false;
    if (path.endsWith(QStringLiteral(".prproj"), Qt::CaseInsensitive)) {
        ok = io::importPremiereProject(path, sequences, media, &warnings, &error);
    } else {
        Sequence seq;
        ok = io::importTimeline(path, seq, media, &warnings, &error);
        if (ok) sequences << seq;
    }
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Import failed"), error);
        return;
    }

    // Reuse media already in the project; probe the rest so sizes/streams are real.
    QHash<QString, QString> remap;
    int offline = 0;
    for (auto& m : media) {
        for (const auto& existing : m_document->project().media) {
            if (!m.path.isEmpty() && QFileInfo(existing.path) == QFileInfo(m.path)) {
                remap.insert(m.id, existing.id);
                break;
            }
        }
        if (remap.contains(m.id)) continue;
        if (QFileInfo::exists(m.path)) {
            ProbeResult probe = probeMedia(m.path);
            if (probe.ok) {
                const QString id = m.id;
                m = probe.ref;
                m.id = id;
            }
        } else {
            m.offline = true;
            ++offline;
        }
        m_document->addMedia(m);
    }
    int clips = 0;
    for (auto& seq : sequences) {
        for (auto& t : seq.tracks)
            for (auto& c : t.clips) {
                c.mediaId = remap.value(c.mediaId, c.mediaId);
                if (t.kind == TrackKind::Video) fx::ensureIntrinsics(c);
                ++clips;
            }
        m_document->addSequence(seq);
    }
    m_timeline->zoomToFit();
    QString message = tr("Imported %n sequence(s), %1 clips", nullptr, sequences.size()).arg(clips);
    if (offline > 0) message += tr(" - %1 media offline (right-click in Project to relink)").arg(offline);
    showStatus(message);
    if (!warnings.isEmpty())
        QMessageBox::information(this, tr("Import notes"), warnings.mid(0, 30).join(QLatin1Char('\n')));
}

void MainWindow::newSequence()
{
    const Sequence* current = m_document->sequence();
    SequenceDialog dialog(tr("Sequence %1").arg(m_document->project().sequences.size() + 1),
                          current ? current->width : 1920, current ? current->height : 1080,
                          current ? current->rate : FrameRate{25, 1}, true, this);
    if (dialog.exec() != QDialog::Accepted) return;
    pauseProgram();
    m_document->addSequence(makeDefaultSequence(dialog.name(), dialog.sequenceWidth(), dialog.sequenceHeight(), dialog.rate()));
    m_timeline->zoomToFit();
    showStatus(tr("Created %1").arg(dialog.name()));
}

void MainWindow::sequenceSettings()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    SequenceDialog dialog(seq->name, seq->width, seq->height, seq->rate, false, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const QString name = dialog.name();
    const int w = dialog.sequenceWidth();
    const int h = dialog.sequenceHeight();
    const FrameRate rate = dialog.rate();
    m_document->applyEdit(tr("Sequence Settings"), [&](Sequence& s) {
        s.name = name;
        s.width = w;
        s.height = h;
        s.rate = rate;
        return true;
    });
    refreshSequenceCombo();
    m_mediaBin->refresh();
    updateProgramInfo();
}

void MainWindow::placeGenerator(Clip clip)
{
    Sequence* seq = m_document->sequence();
    if (!seq) return;
    const Tick at = m_document->playhead();
    clip.id = makeId(QStringLiteral("clip"));
    clip.start = at;
    if (clip.duration <= 0) clip.duration = seq->rate.snap(secToTicks(5.0));
    fx::ensureStandardStack(clip);

    // First video lane above V1 that is free over the clip's range; add one if needed.
    QString newId;
    m_document->applyEdit(clip.generator == GeneratorKind::Title ? tr("New Title") : tr("New Color Matte"), [&](Sequence& s) {
        const int startLane = clip.generator == GeneratorKind::Title ? 2 : 1;
        int target = -1;
        for (int lane = startLane; lane <= s.videoTrackCount(); ++lane) {
            const int ti = s.trackForLane(TrackKind::Video, lane);
            if (ti < 0 || s.tracks[ti].locked) continue;
            bool free = true;
            for (const auto& c : s.tracks[ti].clips)
                if (c.start < clip.end() && clip.start < c.end()) free = false;
            if (free) { target = ti; break; }
        }
        if (target < 0) {
            edit::addTrack(s, TrackKind::Video);
            target = s.trackForLane(TrackKind::Video, s.videoTrackCount());
        }
        newId = clip.id;
        return edit::place(s, {{target, clip}}, edit::PlaceMode::Overwrite);
    });
    if (!newId.isEmpty()) m_document->setSelectionList({newId}, newId);
}

void MainWindow::newTitle()
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    TitleSpec spec;
    spec.fontSize = seq->height * 0.08;
    TitleDialog dialog(spec, seq->width, seq->height, this);
    if (dialog.exec() != QDialog::Accepted) return;
    Clip clip;
    clip.generator = GeneratorKind::Title;
    clip.title = dialog.spec();
    clip.label = clip.title.text.section(QLatin1Char('\n'), 0, 0).left(40);
    placeGenerator(clip);
}

void MainWindow::editTitle(const QString& clipId)
{
    const Sequence* seq = m_document->sequence();
    const Clip* c = seq ? seq->findClip(clipId) : nullptr;
    if (!c || c->generator != GeneratorKind::Title) {
        showStatus(tr("Select a title clip first"));
        return;
    }
    TitleDialog dialog(c->title, seq->width, seq->height, this);
    if (dialog.exec() != QDialog::Accepted) return;
    const TitleSpec spec = dialog.spec();
    m_document->applyEdit(tr("Edit Title"), [&](Sequence& s) {
        Clip* clip = s.findClip(clipId);
        if (!clip) return false;
        clip->title = spec;
        clip->label = spec.text.section(QLatin1Char('\n'), 0, 0).left(40);
        return true;
    });
}

void MainWindow::newAdjustmentLayer()
{
    Clip clip;
    clip.generator = GeneratorKind::Adjustment;
    clip.label = tr("Adjustment Layer");
    placeGenerator(clip);
    showStatus(tr("Adjustment layer added: drop effects on it to affect every track below"));
}

void MainWindow::newModel3D()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("3D Model"), QString(), tr("Wavefront OBJ (*.obj)"));
    if (path.isEmpty()) return;
    const auto mesh = Mesh::load(path);
    if (!mesh->valid()) {
        QMessageBox::warning(this, tr("3D Model"), tr("Could not load %1: %2").arg(QFileInfo(path).fileName(), mesh->error));
        return;
    }
    Clip clip;
    clip.generator = GeneratorKind::Model3D;
    clip.modelPath = path;
    clip.label = QFileInfo(path).completeBaseName();
    clip.effects.push_back(fx::makeModel3D());
    placeGenerator(clip);
    raisePanel(m_effectControls);
    showStatus(tr("%1: %2 triangles. Animate rotation, camera and light in Effect Controls.")
                   .arg(QFileInfo(path).fileName()).arg(mesh->triangles.size()));
}

void MainWindow::ensureRemotionBridge()
{
    if (m_remotion) return;
    {
        m_remotion = new RemotionBridge(this);
        connect(m_remotion, &RemotionBridge::progress, this, [this](int percent, const QString&) {
            m_statsLabel->setText(tr("Remotion %1%").arg(percent));
        });
        connect(m_remotion, &RemotionBridge::failed, this, [this](const QString& error) {
            m_statsLabel->clear();
            QMessageBox::warning(this, tr("Remotion"), error);
        });
        connect(m_remotion, &RemotionBridge::finished, this, [this](const QString& output) {
            m_statsLabel->clear();
            ProbeResult probe = probeMedia(output);
            if (!probe.ok) {
                QMessageBox::warning(this, tr("Remotion"), probe.error);
                return;
            }
            const QJsonObject meta = m_remotion->property("request").toJsonObject();
            if (!m_remotionReplace.isEmpty()) {
                MediaRef updated = probe.ref;
                if (const MediaRef* old = m_document->project().mediaById(m_remotionReplace)) {
                    updated.remotionEntry = old->remotionEntry;
                    updated.remotionComposition = old->remotionComposition;
                    updated.remotionProps = old->remotionProps;
                    updated.bin = old->bin;
                }
                ThumbnailCache::instance().forget(output);
                m_document->replaceMediaPath(m_remotionReplace, updated);
                m_programEngine->invalidate();
                m_remotionReplace.clear();
                showStatus(tr("Re-rendered %1").arg(QFileInfo(output).fileName()));
                return;
            }
            MediaRef ref = probe.ref;
            ref.remotionEntry = meta.value(QStringLiteral("project")).toString();
            ref.remotionComposition = meta.value(QStringLiteral("composition")).toString();
            ref.remotionProps = meta.value(QStringLiteral("props")).toString();
            ref.bin = QStringLiteral("Remotion");
            if (!m_document->project().bins.contains(ref.bin)) m_document->project().bins << ref.bin;
            m_document->addMedia(ref);
            // Overlay on the first video lane above V1 at the playhead.
            const Sequence* s = m_document->sequence();
            const int target = s ? s->trackForLane(TrackKind::Video, std::min(2, s->videoTrackCount())) : -1;
            placeMedia({ref.id}, m_document->playhead(), target, edit::PlaceMode::Overwrite);
            showStatus(tr("Remotion composition %1 rendered and placed").arg(ref.remotionComposition));
        });
    }
}

void MainWindow::newRemotion()
{
    RemotionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    ensureRemotionBridge();
    if (dialog.studioRequested()) {
        m_remotion->openStudio(dialog.project(), dialog.entry());
        showStatus(tr("Opening Remotion Studio (http://localhost:3000)..."));
        return;
    }
    const QString base = m_currentFile.isEmpty()
                             ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/Breefer Remotion")
                             : QFileInfo(m_currentFile).absolutePath() + QStringLiteral("/Remotion Renders");
    RemotionBridge::Request request;
    request.project = dialog.project();
    request.entry = dialog.entry();
    request.composition = dialog.composition();
    request.propsJson = dialog.props();
    request.output = QDir(base).filePath(QStringLiteral("%1-%2.mov").arg(request.composition)
                                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))));
    m_remotion->setProperty("request", QJsonObject{{"project", request.project}, {"composition", request.composition},
                                                    {"props", request.propsJson}});
    m_remotionReplace.clear();
    m_remotion->render(request);
}

void MainWindow::rerenderRemotion(const QString& mediaId)
{
    const MediaRef* ref = m_document->project().mediaById(mediaId);
    if (!ref || ref->remotionEntry.isEmpty()) return;
    ensureRemotionBridge();
    if (m_remotion->isBusy()) return;
    RemotionBridge::Request request;
    request.project = ref->remotionEntry;
    request.composition = ref->remotionComposition;
    request.propsJson = ref->remotionProps;
    request.output = ref->path;
    m_remotionReplace = mediaId;
    m_remotion->render(request);
}

void MainWindow::showPluginManager()
{
    PluginHost& host = PluginHost::instance();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Plugins"));
    dialog.resize(560, 420);
    auto* layout = new QVBoxLayout(&dialog);
    auto* list = new QListWidget(&dialog);
    auto fill = [&] {
        list->clear();
        for (const auto& e : host.effects())
            list->addItem(QStringLiteral("%1  -  %2  (%3)").arg(e.name, e.category, e.library));
        for (const auto& err : host.errors()) list->addItem(tr("Error: %1").arg(err));
        if (list->count() == 0) list->addItem(tr("No plugins installed."));
    };
    fill();
    layout->addWidget(new QLabel(tr("Native effect plugins (C API, see sdk/include/breefer_plugin.h). Folders:\n%1")
                                     .arg(host.folders().join(QLatin1Char('\n'))), &dialog));
    layout->addWidget(list, 1);
    auto* row = new QHBoxLayout;
    auto* rescan = new QPushButton(tr("Rescan"), &dialog);
    auto* open = new QPushButton(tr("Open User Plugin Folder"), &dialog);
    auto* close = new QPushButton(tr("Close"), &dialog);
    row->addWidget(rescan);
    row->addWidget(open);
    row->addStretch(1);
    row->addWidget(close);
    layout->addLayout(row);
    connect(rescan, &QPushButton::clicked, &dialog, [&] { host.scan(); fill(); m_effects->refreshList(); });
    connect(open, &QPushButton::clicked, &dialog, [&] {
        const QString dir = host.folders().value(1);
        QDir().mkpath(dir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
    dialog.exec();
}

void MainWindow::applyLowEndMode(bool on)
{
    m_lowEnd = on;
    appSettings().setValue(QStringLiteral("lowEndMode"), on);
    m_proxyThreshold = on ? 720 : 1080;
    m_timeline->setShowThumbnails(!on);
    setPlaybackDivisor(on ? 4 : 2);
    autoQueueProxies();
    showStatus(on ? tr("Low-end mode on: 1/4 playback, proxies above 720p, no timeline thumbnails")
                  : tr("Low-end mode off"));
}

void MainWindow::newColorMatte()
{
    const QColor color = QColorDialog::getColor(QColor(0x20, 0x40, 0x80), this, tr("Color Matte"));
    if (!color.isValid()) return;
    Clip clip;
    clip.generator = GeneratorKind::ColorMatte;
    clip.matteColor = color;
    placeGenerator(clip);
}

void MainWindow::exportFrame()
{
    const Sequence seq = m_document->snapshotSequence();
    if (seq.tracks.isEmpty()) return;
    QVector<MediaRef> media = m_document->project().media;
    for (auto& m : media) m.proxyPath.clear();   // full quality from originals

    QApplication::setOverrideCursor(Qt::WaitCursor);
    Compositor compositor;
    compositor.setPreviewSize(seq.width, seq.height);
    compositor.setHighQuality(true);
    compositor.setLibrary(m_document->project().sequences);
    const QImage frame = compositor.render(seq, media, m_document->playhead());
    QApplication::restoreOverrideCursor();

    const QString suggested = QStringLiteral("%1 %2.png").arg(seq.name, formatTimecode(m_document->playhead(), seq.rate).replace(QLatin1Char(':'), QLatin1Char('-')));
    const QString path = QFileDialog::getSaveFileName(this, tr("Export Frame"),
                                                      QDir(m_lastExportFolder.isEmpty() ? QDir::homePath() : m_lastExportFolder).filePath(suggested),
                                                      tr("PNG (*.png);;JPEG (*.jpg);;TIFF (*.tif)"));
    if (path.isEmpty()) return;
    if (frame.convertToFormat(QImage::Format_RGB32).save(path)) showStatus(tr("Saved frame %1").arg(QFileInfo(path).fileName()));
    else QMessageBox::warning(this, tr("Export Frame"), tr("Could not write %1").arg(path));
}

// ------------------------------------------------------------------ playback

void MainWindow::playPauseProgram()
{
    if (m_programEngine->isPlaying()) {
        pauseProgram();
        return;
    }
    Tick from = m_document->playhead();
    if (const Sequence* s = m_document->sequence(); s && from >= s->duration && s->duration > 0) from = 0;
    if (m_loop) {
        const Sequence* s = m_document->sequence();
        const Tick in = s && s->inPoint >= 0 ? s->inPoint : 0;
        const Tick out = s && s->outPoint >= 0 ? s->outPoint : (s ? s->duration : 0);
        if (from < in || from >= out) from = in;
    }
    m_programEngine->setSpeed(1.0);
    m_programEngine->play(from);
    m_audio->play(from);
}

void MainWindow::pauseProgram()
{
    if (!m_programEngine->isPlaying()) return;
    m_programEngine->pause();
    m_audio->pause();
}

void MainWindow::shuttle(int direction)
{
    if (direction == 0) {
        pauseProgram();
        return;
    }
    double speed = m_programEngine->isPlaying() ? m_programEngine->speed() : 0.0;
    if (direction > 0) speed = speed <= 0.0 ? 1.0 : std::min(8.0, speed * 2.0);
    else speed = speed >= 0.0 ? -1.0 : std::max(-8.0, speed * 2.0);

    const Tick from = m_document->playhead();
    if (!m_programEngine->isPlaying()) m_programEngine->play(from);
    m_programEngine->setSpeed(speed);
    if (speed == 1.0) m_audio->play(from);
    else m_audio->pause();
    showStatus(tr("Shuttle %1x").arg(speed));
}

void MainWindow::stepProgram(int frames)
{
    const Sequence* seq = m_document->sequence();
    if (!seq) return;
    pauseProgram();
    m_document->setPlayhead(std::max<Tick>(0, m_document->playhead() + seq->rate.ticksForFrames(frames)));
}

void MainWindow::setLoop(bool on)
{
    m_loop = on;
    const Sequence* s = m_document->sequence();
    if (!s) return;
    const Tick in = s->inPoint >= 0 ? s->inPoint : 0;
    const Tick out = s->outPoint >= 0 ? s->outPoint : s->duration;
    m_programEngine->setLoop(on, in, out);
}

// -------------------------------------------------------------------- source

void MainWindow::setSourceMedia(const QString& mediaId, Tick at)
{
    const MediaRef* media = m_document->project().mediaById(mediaId);
    if (!media) return;

    m_sourceMediaId = mediaId;
    m_sourceIn = -1;
    m_sourceOut = -1;

    Sequence source;
    source.id = QStringLiteral("source");
    source.name = media->name;
    source.width = media->width > 0 ? media->width : 1920;
    source.height = media->height > 0 ? media->height : 1080;
    source.rate = media->rate;
    const Tick duration = media->isStillImage() ? secToTicks(5.0) : media->duration;

    if (media->hasVideo) {
        Track track;
        track.id = QStringLiteral("source_v");
        track.kind = TrackKind::Video;
        Clip clip;
        clip.id = QStringLiteral("source_clip");
        clip.mediaId = mediaId;
        clip.duration = duration;
        track.clips.push_back(clip);
        source.tracks.push_back(track);
    }
    source.duration = duration;
    m_sourceSequence = source;

    QVector<MediaRef> media2 = m_document->project().media;
    if (!m_useProxies)
        for (auto& r : media2) r.proxyPath.clear();
    m_sourceEngine->pause();
    m_sourceEngine->setPreviewHeight(360);
    m_sourceEngine->setSource(source, media2);
    const Tick start = at >= 0 ? std::clamp<Tick>(at, 0, std::max<Tick>(0, duration - media->rate.ticksPerFrame())) : 0;
    m_sourceEngine->seek(start);

    m_sourceMonitor->setDragMediaId(mediaId);
    m_sourceMonitor->setFrameSize(QSize(source.width, source.height));
    m_upperLeft->setTabText(m_upperLeft->indexOf(m_sourceMonitor), tr("Source: %1").arg(media->name));
    raisePanel(m_sourceMonitor);
    m_sourceMonitor->setDuration(duration);
    m_sourceMonitor->setDurationText(formatTimecode(duration, source.rate));
    m_sourceMonitor->setPosition(start);
    m_sourceMonitor->setTimecode(formatTimecode(start, source.rate));
    m_sourceMonitor->setInfoText(media->hasVideo ? QStringLiteral("%1 - %2x%3 - %4 fps").arg(media->name).arg(media->width).arg(media->height).arg(media->rate.label())
                                                 : media->name);
    if (!media->hasVideo) m_sourceMonitor->clearImage();
    updateSourceMarks();
}

void MainWindow::playPauseSource()
{
    if (m_sourceMediaId.isEmpty()) return;
    if (m_sourceEngine->isPlaying()) {
        m_sourceEngine->pause();
        return;
    }
    pauseProgram();
    m_sourceEngine->play(m_sourceEngine->position());
}

void MainWindow::updateSourceMarks()
{
    m_sourceMonitor->setMarks(m_sourceIn, m_sourceOut);
}

void MainWindow::maybeMatchSequenceToMedia(const MediaRef& media)
{
    Sequence* seq = m_document->sequence();
    if (!seq || !media.hasVideo || media.width <= 0) return;
    for (const auto& t : seq->tracks)
        if (!t.clips.isEmpty()) return;
    if (seq->width == media.width && seq->height == media.height && seq->rate == media.rate) return;

    QSettings s = appSettings();
    const QString policy = s.value(QStringLiteral("matchSequencePolicy"), QStringLiteral("ask")).toString();
    bool change = policy == QLatin1String("always");
    if (policy == QLatin1String("ask")) {
        QMessageBox box(this);
        box.setWindowTitle(tr("Clip Mismatch Warning"));
        box.setText(tr("This clip (%1x%2, %3 fps) does not match the sequence settings (%4x%5, %6 fps).")
                        .arg(media.width).arg(media.height).arg(media.rate.label())
                        .arg(seq->width).arg(seq->height).arg(seq->rate.label()));
        QPushButton* changeBtn = box.addButton(tr("Change sequence settings"), QMessageBox::AcceptRole);
        box.addButton(tr("Keep existing settings"), QMessageBox::RejectRole);
        auto* remember = new QCheckBox(tr("Always do this"), &box);
        box.setCheckBox(remember);
        box.exec();
        change = box.clickedButton() == changeBtn;
        if (remember->isChecked()) s.setValue(QStringLiteral("matchSequencePolicy"), change ? QStringLiteral("always") : QStringLiteral("never"));
    }
    if (!change) return;
    seq->width = media.width;
    seq->height = media.height;
    seq->rate = media.rate;
    m_document->notifyChanged();
    updateProgramInfo();
    m_mediaBin->refresh();
}

QStringList MainWindow::placeMedia(const QStringList& mediaIds, Tick at, int trackIndex, edit::PlaceMode mode,
                                   Tick sourceIn, Tick sourceOut, bool usePatching)
{
    Sequence* seq = m_document->sequence();
    if (!seq || mediaIds.isEmpty()) return {};
    if (const MediaRef* first = m_document->project().mediaById(mediaIds.first())) maybeMatchSequenceToMedia(*first);
    seq = m_document->sequence();

    // Target lanes: the drop track's lane (mirrored for the other kind), else
    // the lowest targeted tracks.
    int videoLane = 1;
    int audioLane = 1;
    if (usePatching) {
        videoLane = seq->patchVideo;
        audioLane = seq->patchAudio;
        if (videoLane <= 0 && audioLane <= 0) {
            showStatus(tr("No source tracks patched - click V1/A1 in the left column of the track headers"));
            return {};
        }
    } else if (seq->track(trackIndex)) {
        videoLane = audioLane = seq->laneOf(trackIndex);
    } else {
        for (int lane = 1; lane <= seq->videoTrackCount(); ++lane) {
            const int ti = seq->trackForLane(TrackKind::Video, lane);
            if (ti >= 0 && seq->tracks[ti].targeted && !seq->tracks[ti].locked) { videoLane = lane; break; }
        }
        for (int lane = 1; lane <= seq->audioTrackCount(); ++lane) {
            const int ti = seq->trackForLane(TrackKind::Audio, lane);
            if (ti >= 0 && seq->tracks[ti].targeted && !seq->tracks[ti].locked) { audioLane = lane; break; }
        }
    }

    QStringList created;
    const Tick stillDuration = seq->rate.snap(secToTicks(5.0));
    m_document->applyEdit(mode == edit::PlaceMode::Insert ? tr("Insert") : tr("Overwrite"), [&](Sequence& s) {
        Tick cursor = s.rate.snap(at);
        for (const QString& id : mediaIds) {
            const MediaRef* media = m_document->project().mediaById(id);
            if (!media) continue;
            Tick in = sourceIn >= 0 ? sourceIn : 0;
            Tick out = sourceOut > in ? sourceOut : (media->isStillImage() ? stillDuration : media->duration);
            if (media->isStillImage()) in = 0;
            const Tick duration = s.rate.snap(out - in);
            if (duration <= 0) continue;

            const bool useVideo = media->hasVideo && videoLane > 0;
            const bool useAudio = media->hasAudio && audioLane > 0;
            while (useVideo && s.trackForLane(TrackKind::Video, videoLane) < 0) edit::addTrack(s, TrackKind::Video);
            while (useAudio && s.trackForLane(TrackKind::Audio, audioLane) < 0) edit::addTrack(s, TrackKind::Audio);

            const QString link = (useVideo && useAudio) ? makeId(QStringLiteral("link")) : QString();
            QVector<edit::Placement> placements;
            if (useVideo) {
                Clip c;
                c.id = makeId(QStringLiteral("clip"));
                c.mediaId = id;
                c.start = cursor;
                c.duration = duration;
                c.sourceIn = in;
                c.linkId = link;
                fx::ensureIntrinsics(c);
                placements.push_back({s.trackForLane(TrackKind::Video, videoLane), c});
                created << c.id;
            }
            if (useAudio) {
                Clip c;
                c.id = makeId(QStringLiteral("clip"));
                c.mediaId = id;
                c.start = cursor;
                c.duration = duration;
                c.sourceIn = in;
                c.linkId = link;
                fx::ensureAudioStack(c);
                placements.push_back({s.trackForLane(TrackKind::Audio, audioLane), c});
                created << c.id;
            }
            if (!edit::place(s, placements, mode)) return false;
            cursor += duration;
        }
        return !created.isEmpty();
    });

    if (!created.isEmpty()) {
        m_document->setSelectionList(created);
        Tick end = at;
        if (const Sequence* s = m_document->sequence())
            for (const QString& id : created)
                if (const Clip* c = s->findClip(id)) end = std::max(end, c->end());
        m_document->setPlayhead(end);
        showStatus(tr("%1 %n clip(s)", nullptr, created.size()).arg(mode == edit::PlaceMode::Insert ? tr("Inserted") : tr("Placed")));
    }
    return created;
}

void MainWindow::editFromSource(edit::PlaceMode mode)
{
    if (m_sourceMediaId.isEmpty()) {
        const QStringList selected = m_mediaBin->selectedMediaIds();
        if (selected.isEmpty()) {
            showStatus(tr("Load a clip into the Source monitor first (double-click it in the Project panel)"));
            return;
        }
        placeMedia(selected, m_document->playhead(), -1, mode);
        return;
    }
    const Sequence* seq = m_document->sequence();
    // Three-point edit: sequence In wins over the playhead.
    const Tick at = (seq && seq->inPoint >= 0) ? seq->inPoint : m_document->playhead();
    Tick in = m_sourceIn;
    Tick out = m_sourceOut;
    if (seq && seq->hasRange() && out < 0 && in >= 0) out = in + (seq->outPoint - seq->inPoint);
    placeMedia({m_sourceMediaId}, at, -1, mode, in, out, true);
}

// -------------------------------------------------------------------- export

void MainWindow::showExportDialog()
{
    const Sequence* seq = m_document->sequence();
    if (!seq || seq->duration <= 0) {
        showStatus(tr("Nothing to export - the timeline is empty"));
        return;
    }
    pauseProgram();
    const QString base = m_currentFile.isEmpty() ? seq->name : QFileInfo(m_currentFile).completeBaseName() + QStringLiteral(" - ") + seq->name;
    ExportDialog dialog(*seq, base, m_lastExportFolder, this);
    if (dialog.exec() != QDialog::Accepted) return;

    const Exporter::Settings settings = dialog.settings();
    if (QFileInfo::exists(settings.outputPath)
        && QMessageBox::question(this, tr("Replace file?"), tr("%1 already exists. Replace it?").arg(settings.outputPath)) != QMessageBox::Yes)
        return;
    m_lastExportFolder = QFileInfo(settings.outputPath).absolutePath();
    appSettings().setValue(QStringLiteral("lastExportFolder"), m_lastExportFolder);

    QVector<MediaRef> media = m_document->project().media;
    for (auto& m : media) m.proxyPath.clear();   // always render from originals
    m_exporter->start(m_document->snapshotSequence(), media, settings, m_document->project().sequences);
    raisePanel(m_queue);
    showStatus(dialog.queueOnly() ? tr("Queued %1").arg(settings.label) : tr("Exporting %1...").arg(settings.label));
}

void MainWindow::showPreferences()
{
    QSettings settings = appSettings();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Preferences"));
    dialog.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;

    auto* cacheRow = new QWidget(&dialog);
    auto* cacheLayout = new QHBoxLayout(cacheRow);
    cacheLayout->setContentsMargins(0, 0, 0, 0);
    auto* cacheEdit = new QLineEdit(settings.value(QStringLiteral("cacheDir")).toString(), cacheRow);
    cacheEdit->setPlaceholderText(tr("Default (system cache)"));
    cacheLayout->addWidget(cacheEdit, 1);
    auto* browse = new QPushButton(tr("Browse..."), cacheRow);
    cacheLayout->addWidget(browse);
    form->addRow(tr("Media cache folder"), cacheRow);

    auto* autosave = new QSpinBox(&dialog);
    autosave->setRange(0, 120);
    autosave->setValue(settings.value(QStringLiteral("autosaveMinutes"), 5).toInt());
    autosave->setSuffix(tr(" min"));
    autosave->setSpecialValueText(tr("Off"));
    form->addRow(tr("Auto-save every"), autosave);

    auto* transition = new QSpinBox(&dialog);
    transition->setRange(1, 600);
    transition->setValue(settings.value(QStringLiteral("defaultTransitionFrames"), 25).toInt());
    transition->setSuffix(tr(" frames"));
    form->addRow(tr("Default transition duration"), transition);

    auto* lowEnd = new QCheckBox(tr("Low-end mode (Intel Core 2nd-4th gen, 4-8 GB RAM, HDD)"), &dialog);
    lowEnd->setToolTip(tr("1/4 playback resolution, proxies for anything above 720p, no timeline thumbnails"));
    lowEnd->setChecked(m_lowEnd);
    form->addRow(tr("Performance"), lowEnd);
    auto* hwDecode = new QCheckBox(tr("Hardware decoding (DirectX / VA-API / VideoToolbox)"), &dialog);
    hwDecode->setChecked(VideoDecoder::hardwareDecodeEnabled());
    form->addRow(QString(), hwDecode);

    auto* match = new QComboBox(&dialog);
    match->addItem(tr("Ask"), QStringLiteral("ask"));
    match->addItem(tr("Always change sequence"), QStringLiteral("always"));
    match->addItem(tr("Never change sequence"), QStringLiteral("never"));
    match->setCurrentIndex(std::max(0, match->findData(settings.value(QStringLiteral("matchSequencePolicy"), QStringLiteral("ask")))));
    form->addRow(tr("First clip mismatch"), match);

    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    connect(browse, &QPushButton::clicked, &dialog, [cacheEdit] {
        const QString dir = QFileDialog::getExistingDirectory(nullptr, QObject::tr("Media Cache Folder"));
        if (!dir.isEmpty()) cacheEdit->setText(dir);
    });

    if (dialog.exec() != QDialog::Accepted) return;
    settings.setValue(QStringLiteral("cacheDir"), cacheEdit->text());
    settings.setValue(QStringLiteral("autosaveMinutes"), autosave->value());
    settings.setValue(QStringLiteral("defaultTransitionFrames"), transition->value());
    settings.setValue(QStringLiteral("matchSequencePolicy"), match->currentData());
    settings.setValue(QStringLiteral("hardwareDecode"), hwDecode->isChecked());
    if (hwDecode->isChecked() != VideoDecoder::hardwareDecodeEnabled()) {
        VideoDecoder::setHardwareDecodeEnabled(hwDecode->isChecked());
        m_programEngine->invalidate();
    }
    if (lowEnd->isChecked() != m_lowEnd) applyLowEndMode(lowEnd->isChecked());
    m_timeline->setDefaultTransitionFrames(transition->value());
    if (autosave->value() > 0) m_autosaveTimer->start(autosave->value() * 60000);
    else m_autosaveTimer->stop();
    showStatus(tr("Preferences saved"));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    pauseProgram();
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    if (m_exporter->isRunning()
        && QMessageBox::question(this, tr("Export in progress"), tr("An export is still running. Quit and cancel it?"))
               != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    QSettings settings = appSettings();
    if (m_maximized) toggleMaximize(m_maximized);
    settings.setValue(QStringLiteral("geometry.v4"), saveGeometry());
    settings.setValue(QStringLiteral("split.v"), m_vSplit->saveState());
    settings.setValue(QStringLiteral("split.top"), m_topSplit->saveState());
    settings.setValue(QStringLiteral("split.bottom"), m_bottomSplit->saveState());
    if (!m_currentFile.isEmpty()) QFile::remove(m_currentFile + QStringLiteral(".autosave"));
    if (m_audio) m_audio->shutdown();
    event->accept();
}

} // namespace bc
