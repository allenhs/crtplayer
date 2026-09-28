#pragma once
#include "render/Geometry.h"
#include "settings/AppSettings.h"
#include "settings/CrtParams.h"
#include "settings/PresetManager.h"

#include <QJsonObject>
#include <QMainWindow>
#include "playback/TapeAudio.h"
#include <QTimer>
#include "render/DeskView.h"
#include <QDialog>
#include <QPointer>
#include <QLabel>
#include <QFrame>
#include <QVector>
#include "jellyfin/JellyfinClient.h"

class Player;
class VideoWidget;
class ControlBar;
class CrtPanel;
class DisplayPanel;
class PlaybackPanel;
class PlaylistPanel;
class QDockWidget;
class QLabel;
class QMenu;
class QTabWidget;
class QActionGroup;
class DeskWindow;
class JellyfinClient;
class JellyfinPanel;
struct JfItem;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Public control surface (used by the UI and by --automation).
    void openFiles(const QStringList& paths, bool playNow = true);
    void playIndex(int i);
    void setFullscreen(bool on);
    bool isFullscreenMode() const { return m_fullscreen; }
    bool selectPreset(const QString& name);
    void setBypass(bool on);
    void setCompare(bool on);
    void setScaleMode(ScaleMode m);
    void setCrop(const CropFractions& c);
    void setAspectOverride(double a);
    void setParams(const CrtParams& p);
    QString takeScreenshot(bool filtered, const QString& path = {});
    bool controlsVisible() const;
    Player* player() const { return m_player; }
    VideoWidget* video() const { return m_video; }
    QJsonObject stateReport() const;
    QString lastError() const { return m_lastError; }
    QString lastWarning() const { return m_lastWarning; }
    void showOsd(const QString& text, int ms = 1600);

    // Desk mode: the TV on the desktop (optional; the regular window is hidden meanwhile).
    void enterDeskMode();
    void leaveDeskMode();
    void toggleDeskMode() { if (m_deskActive) leaveDeskMode(); else enterDeskMode(); }
    bool isDeskMode() const { return m_deskActive; }

    // Jellyfin
    JellyfinClient* jellyfin() const { return m_jf; }
    JellyfinPanel* jellyfinPanel() const { return m_jfPanel; }
    void playJellyfin(const JfItem& item, bool fromStart);
    void playJellyfinConverted(const JfItem& item);
    void enqueueJellyfin(const JfItem& item);
    void showJellyfin(bool on);
    DeskWindow* deskWindow() const { return m_desk; }

protected:
    void closeEvent(QCloseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void buildUi();
    void buildActions();
    void restoreSettings();
    void saveSettings();
    void layoutOverlays();
    void onMouseActivity();
    void updateAutoHide();
    void setControlsShown(bool shown);
    void refreshPresetUi();
    void applyParams(const CrtParams& p, bool fromPreset);
    void cyclePreset(int dir);
    void rebuildAudioMenu();
    void rebuildSubtitleMenu();
    void cycleAudio();
    void cycleSubtitle();
    void updatePosition();
    void updateInfoOverlay();
    void updateSourceInfo();
    void updateDecoderStatus();
    void openDialog(QWidget* parent = nullptr);
    void addDialog();
    void nextItem(int dir);
    void savePresetAs();
    void savePreset();
    void renamePreset();
    void deletePreset();
    void importPreset();
    void exportPreset();
    void reopenForDecoderChange();
    void updateTitle();
    void createDesk();
    void syncDeskBar();
    void showDeskMenu(const QPoint& globalPos);
    void storeDeskPose();

    Player* m_player;
    VideoWidget* m_video;
    ControlBar* m_controls;
    QLabel* m_osd;
    QLabel* m_info;
    QLabel* m_emptyHint;
    QDockWidget* m_settingsDock;
    QDockWidget* m_playlistDock;
    QTabWidget* m_tabs;
    CrtPanel* m_crtPanel;
    DisplayPanel* m_displayPanel;
    PlaybackPanel* m_playbackPanel;
    PlaylistPanel* m_playlist;
    QMenu* m_audioMenu;
    QMenu* m_subMenu;
    QMenu* m_aspectMenu;
    QActionGroup* m_aspectGroup;

    AppSettings m_settings;
    PresetManager m_presets;
    QString m_presetName;
    CrtParams m_params;

    QTimer m_posTimer, m_hideTimer, m_osdTimer, m_infoTimer;
    bool m_fullscreen = false;
    bool m_wasMaximized = false;
    bool m_controlsShown = true;
    bool m_dockSettingsBeforeFs = false, m_dockPlaylistBeforeFs = false;
    QString m_lastError, m_lastWarning;
    DeskWindow* m_desk = nullptr;
    JellyfinClient* m_jf = nullptr;
    JellyfinPanel* m_jfPanel = nullptr;
    QDockWidget* m_jfDock = nullptr;
    QString m_jfItemId;          // Jellyfin item currently playing
    bool m_jfStarted = false;    // start reported to the server
    int m_jfRequest = 0;         // PlaybackInfo requests: only the latest one's answer is used
    bool m_jfTranscoding = false;        // the server converts the current item
    QString m_jfPlaySession;             // the server's play session (its conversion)
    QStringList m_jfReasons;             // why it converts
    bool m_jfForceNext = false;          // the next Jellyfin open asks for a conversion
    bool m_jfRetried = false;            // the original failed and a conversion was tried
    qint64 m_jfStartNs = 0;              // where the current item was opened
    qint64 m_pendingStartNs = -1;
    QString m_mediaTitle;        // shown instead of a file name (network items)
    QTimer m_jfTimer;
    QVector<JfItem> m_jfResume;   // Continue watching, for the desk-mode menu

    // 1.8: resume, recent files, external subtitles, speed, A-B loop, chapters, previews
public:
    void setExternalSubtitleFile(const QString& uri, const QString& label);   // reloads at the current position
    void setSpeed(double rate);
    bool loadSubtitleOffer(int index);   // one of the offered subtitle files (sidecar or Jellyfin)
    // One entry point for remote control (MPRIS, gamepad, automation). Returns false for
    // an unknown action. Names: playpause play pause stop next prev seekrel(ns) seekabs(ns)
    // volume(0..1) volup voldown rate(x) fullscreen(0/1) raise quit preset+ preset- subs
    // audio desk fly back chapter+ chapter- controls
    bool remoteAction(const QString& name, double value = 0);
    QString mediaTitle() const;
    int playlistCount() const;
    int playlistIndex() const;
    double volume01() const;
    void showControlsBriefly();
    void showOsdMessage(const QString& text, int ms) { showOsd(text, ms); }
    bool isVideoFocus(QWidget* w) const;   // the video area (not a panel) has focus
    class Gamepad* gamepad() const { return m_gamepad; }
    class Mpris* mpris() const { return m_mpris; }
    static bool inGamescope();
    // Non-blocking notice when common formats cannot play (missing GStreamer plugins).
    void showPluginNotice();
    void openSceneDialog();
    void setDeskScene(const DeskView::Scene& s);   // applies (and remembers) desk-mode scene settings
    void updateTheater();                           // curtains follow playback in the movie theater
    void applyLookSound();                          // the look's tape / speaker sound to the player
    TapeParams m_lastTape;                          // what was last sent to the player (reports)
    void setLookSound(bool on);
    void setSoundLevels(double noiseVolume, double effectStrength);
    const AppSettings& settings() const { return m_settings; }
    void showSettingsTab(const QString& name);   // the settings panel, on a tab ("playback", "crt", ...)
    void theaterLook(bool entering);                // the film look while in the theater, and back
    void setTheaterLookSetting(int look) { m_settings.theaterLook = std::clamp(look, 0, 3); }
private:
    class Gamepad* m_gamepad = nullptr;
    class Mpris* m_mpris = nullptr;
    class SleepInhibitor* m_sleep = nullptr;
    QPointer<QDialog> m_sceneDialog;
    bool m_theaterEnded = false;
    bool m_preTheaterValid = false;   // the look to restore when leaving the theater
    QString m_preTheaterName;
    CrtParams m_preTheaterParams;
public:
    void cycleLoop();
    void setLoop(qint64 aNs, qint64 bNs);
    void jumpChapter(int dir);
    QString systemReport() const;
    class Thumbnailer* thumbnailer() const { return m_thumbs; }
    QString currentLocalFile() const { return m_currentLocal; }
private:
    void rememberPosition();
    void fillRecentMenu(QMenu* menu);
    void updateSeekMarks();
    void showPreview(qint64 ms, int x);
    QStringList findSidecarSubtitles(const QString& videoPath) const;
    static QString stageSubtitle(const QByteArray& data, const QString& nameHint);
    void applyExternalSubtitle(const QString& localUri, const QString& label);
    class ResumeStore* m_resume = nullptr;
    class Thumbnailer* m_thumbs = nullptr;
    QFrame* m_preview = nullptr;
    QLabel* m_previewImage = nullptr;
    QLabel* m_previewText = nullptr;
    qint64 m_previewMs = -1;
    QString m_currentLocal;                     // absolute path of the playing local file
    QList<QPair<QString, QString>> m_extSubs;   // external subtitles on offer: label, URI
    QString m_extSubLabel;                      // the one loaded, if any
    qint64 m_loopA = -1, m_loopB = -1;          // ns
    QTimer m_resumeTimer;
    void jellyfinStopCurrent(bool waitForServer);
    void openResolved(int index, const QString& uri, qint64 start, const QString& shown, const QString& note);
    QString jellyfinPlayDescription() const;   // "original file" / "converted by the server (…)"
    void vcr(const QString& text, double seconds);   // VCR on-screen display, when enabled
    bool m_deskActive = false;
};
