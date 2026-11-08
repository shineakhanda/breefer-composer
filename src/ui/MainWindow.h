#pragma once

#include "core/EditOps.h"
#include "core/Project.h"
#include "ui/TimelineWidget.h"

#include <QMainWindow>

class QAction;
class QActionGroup;
class QComboBox;
class QLabel;
class QMenu;
class QSplitter;
class QTabBar;
class QTimer;
class QToolButton;

namespace bc {

class AudioEngine;
class AudioMeters;
class AudioMixerPanel;
class Document;
class EffectControls;
class EffectsPanel;
class Exporter;
class HistoryPanel;
class LumetriPanel;
class MediaBin;
class MonitorWidget;
class DockPanel;
class PanelFrame;
class PlaybackEngine;
class ProxyManager;
class RemotionBridge;
class RenderQueuePanel;
class ScopesPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openProjectFile(const QString& path);
    void reportPreviousCrash(const QString& reportPath);
    void createProject(const QString& name, const QString& folder);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool event(QEvent* e) override;

private:
    void buildUi();
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void wire();

    QAction* action(const QString& text, const QKeySequence& shortcut, const std::function<void()>& fn,
                    QMenu* menu = nullptr);

    void syncEngineSources();
    void autoQueueProxies();
    void refreshTitle();
    void updateUndoActions();
    void showStatus(const QString& message);
    void updateProgramInfo();
    void refreshSequenceCombo();
    void applyWorkspace(int index);
    void raisePanel(QWidget* panel);
    void toggleMaximize(DockPanel* dock);
    DockPanel* dockUnderCursor() const;
    DockPanel* addDock(const QString& id, const QString& title, QWidget* content);
    DockPanel* dockFor(QWidget* content) const;
    void applyDefaultLayout();
    void updateDockTitles();
    void saveCurrentWorkspace();
    void rebuildWorkspaceMenu(QMenu* menu);
    void setPanelsLocked(bool locked);
    void fitToScreen();
    void governMemory();
    static QString recoveryPath();
    void showSystemInfo();
    QWidget* buildToolsPanel();
    QWidget* buildHeaderBar();
    QWidget* buildTimelinePanel();
    void setPlaybackDivisor(int divisor);
    void addRecent(const QString& path);
    void rebuildRecentMenu();

    bool maybeSave();
    void newProject();
    void openProject();
    bool saveProjectAs(bool forceDialog);
    void exportInterchange(const QString& kind);
    void importInterchange();
    void importInterchangeFile(const QString& path);
    void newSequence();
    void sequenceSettings();
    void newTitle();
    void editTitle(const QString& clipId);
    void newColorMatte();
    void newAdjustmentLayer();
    void newModel3D();
    void newRemotion();
    void ensureRemotionBridge();
    void rerenderRemotion(const QString& mediaId);
    void showPluginManager();
    void applyLowEndMode(bool on);
    void exportFrame();

    void playPauseProgram();
    void pauseProgram();
    void shuttle(int direction);   // J=-1, K=0, L=+1
    void stepProgram(int frames);
    void showExportDialog();
    void showPreferences();
    void setLoop(bool on);

    // Source monitor / 3-point editing.
    void setSourceMedia(const QString& mediaId, Tick at = -1);
    void playPauseSource();
    void updateSourceMarks();
    void editFromSource(edit::PlaceMode mode);
    QStringList placeMedia(const QStringList& mediaIds, Tick at, int trackIndex, edit::PlaceMode mode,
                           Tick sourceIn = -1, Tick sourceOut = -1, bool usePatching = false);
    void maybeMatchSequenceToMedia(const MediaRef& media);
    void placeGenerator(Clip clip);

    Document* m_document = nullptr;
    PlaybackEngine* m_programEngine = nullptr;
    PlaybackEngine* m_sourceEngine = nullptr;
    AudioEngine* m_audio = nullptr;
    ProxyManager* m_proxies = nullptr;
    Exporter* m_exporter = nullptr;
    bool m_useProxies = true;
    bool m_scanningProxies = false;
    bool m_loop = false;

    TimelineWidget* m_timeline = nullptr;
    MonitorWidget* m_programMonitor = nullptr;
    MonitorWidget* m_sourceMonitor = nullptr;
    MediaBin* m_mediaBin = nullptr;
    EffectControls* m_effectControls = nullptr;
    AudioMeters* m_meters = nullptr;
    LumetriPanel* m_lumetri = nullptr;
    ScopesPanel* m_scopes = nullptr;
    EffectsPanel* m_effects = nullptr;
    AudioMixerPanel* m_mixer = nullptr;
    HistoryPanel* m_history = nullptr;
    RenderQueuePanel* m_queue = nullptr;

    // Premiere layout: every panel is a dock that can be moved, tabbed, split or floated.
    QList<DockPanel*> m_docks;
    DockPanel* m_dockSource = nullptr;
    DockPanel* m_dockEffectControls = nullptr;
    DockPanel* m_dockLumetri = nullptr;
    DockPanel* m_dockMixer = nullptr;
    DockPanel* m_dockScopes = nullptr;
    DockPanel* m_dockProgram = nullptr;
    DockPanel* m_dockProject = nullptr;
    DockPanel* m_dockEffects = nullptr;
    DockPanel* m_dockHistory = nullptr;
    DockPanel* m_dockQueue = nullptr;
    DockPanel* m_dockTools = nullptr;
    DockPanel* m_dockTimeline = nullptr;
    DockPanel* m_dockMeters = nullptr;
    QList<PanelFrame*> m_frames;
    DockPanel* m_maximized = nullptr;
    QByteArray m_preMaximizeState;
    bool m_panelsLocked = false;
    bool m_titleUpdatePending = false;
    QTabBar* m_sequenceTabs = nullptr;
    QList<QToolButton*> m_modeButtons;
    QToolButton* m_proxyButton = nullptr;
    int m_playbackDivisor = 2;
    int m_proxyThreshold = 1080;   // proxies for anything taller (720 in low-end mode)
    bool m_lowEnd = false;
    RemotionBridge* m_remotion = nullptr;
    QString m_remotionReplace;     // media id being re-rendered

    QTimer* m_meterTimer = nullptr;
    QTimer* m_autosaveTimer = nullptr;
    QTimer* m_memoryTimer = nullptr;
    int m_lastCacheBudgetKB = 0;
    bool m_lowMemoryNoted = false;
    QComboBox* m_qualityCombo = nullptr;
    QComboBox* m_sequenceCombo = nullptr;
    QTabBar* m_workspaceTabs = nullptr;
    QLabel* m_statsLabel = nullptr;
    QLabel* m_messageLabel = nullptr;
    QLabel* m_toolLabel = nullptr;
    QMenu* m_recentMenu = nullptr;

    QAction* m_undoAction = nullptr;
    QAction* m_redoAction = nullptr;
    QAction* m_snapAction = nullptr;
    QAction* m_linkedAction = nullptr;
    QActionGroup* m_toolGroup = nullptr;
    QHash<int, QAction*> m_toolActions;

    QString m_sourceMediaId;
    Tick m_sourceIn = -1;
    Tick m_sourceOut = -1;
    Sequence m_sourceSequence;

    bool m_syncingPlayhead = false;
    QString m_currentFile;
    QString m_lastExportFolder;
};

} // namespace bc
