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
class PanelFrame;
class PanelTabs;
class PlaybackEngine;
class ProxyManager;
class RenderQueuePanel;
class ScopesPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openProjectFile(const QString& path);
    void createProject(const QString& name, const QString& folder);

protected:
    void closeEvent(QCloseEvent* event) override;

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
    void toggleMaximize(PanelFrame* frame);
    PanelFrame* frameUnderCursor() const;
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
    void newSequence();
    void sequenceSettings();
    void newTitle();
    void editTitle(const QString& clipId);
    void newColorMatte();
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

    // Premiere layout: panel groups in gutter splitters.
    QSplitter* m_vSplit = nullptr;
    QSplitter* m_topSplit = nullptr;
    QSplitter* m_bottomSplit = nullptr;
    PanelTabs* m_upperLeft = nullptr;     // Source, Effect Controls, Lumetri, Mixer, Scopes
    PanelTabs* m_programTabs = nullptr;
    PanelTabs* m_lowerLeft = nullptr;     // Project, Effects, History, Render Queue
    PanelFrame* m_upperLeftFrame = nullptr;
    PanelFrame* m_programFrame = nullptr;
    PanelFrame* m_lowerLeftFrame = nullptr;
    PanelFrame* m_toolsFrame = nullptr;
    PanelFrame* m_timelineFrame = nullptr;
    PanelFrame* m_metersFrame = nullptr;
    QList<PanelFrame*> m_frames;
    PanelFrame* m_maximized = nullptr;
    QTabBar* m_sequenceTabs = nullptr;
    QList<QToolButton*> m_modeButtons;
    QToolButton* m_proxyButton = nullptr;
    int m_playbackDivisor = 2;

    QTimer* m_meterTimer = nullptr;
    QTimer* m_autosaveTimer = nullptr;
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
