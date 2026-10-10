#pragma once
#include "render/Geometry.h"
#include "settings/AppSettings.h"
#include "settings/CrtParams.h"
#include "settings/PresetManager.h"

#include <QJsonObject>
#include <QElapsedTimer>
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
#include "playback/WebSource.h"
#include <QHash>

class Player;
class VideoWidget;
class ControlBar;
class CrtPanel;
class DisplayPanel;
class PlaybackPanel;
class PlaylistPanel;
class QComboBox;
class QDockWidget;
class QLabel;
class QMenu;
class QTabWidget;
class QActionGroup;
class DeskWindow;
class CutDialog;
class GifDialog;
class TvController;
class TvPanel;
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
    QComboBox* lookSelector() const;   // on the bar in use (the desk's in desk mode)

    // Editing: A–B section (the R key), lossless cut and GIF clips
    qint64 loopA() const { return m_loopA; }
    qint64 loopB() const { return m_loopB; }
    void setLoop(qint64 aNs, qint64 bNs);
    CutDialog* showCutDialog();
    GifDialog* showGifDialog();

    // Cable TV
    TvController* tv() const { return m_tv; }
    bool tvOn() const;
    void setTvMode(bool on);
    void setGifOptions(int width, int fps, bool look);
    void seekKeyframe(bool forward);

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
    QDockWidget* m_settingsDock = nullptr;
    QDockWidget* m_playlistDock = nullptr;
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

    // 2.17: videos from web sites, found with yt-dlp (Online.cpp)
public:
    void openLink(const QString& address, bool playNow = true);   // a page with a video on it, or a media address
    void promptOpenLink();                                        // asks for the address (Ctrl+L)
    bool pasteLink();                                             // the address on the clipboard (Ctrl+V)
    void fetchYtDlp(bool ownCopy = false);                                            // what the settings' button does
    void setOnlineMaxHeight(int height);
    void setYtDlpPath(const QString& path);
    class OnlineResolver* online() const { return m_online; }
    // The browser of web videos (2.18).
    bool browseOpen() const;
    void setupBrowse();
    void browseYtDlpDone(bool ok, const QString& message);
    QJsonObject browseReport() const;
    void showBrowse(bool on);
    class BrowseScreen* browseScreen() const { return m_browse; }
    class WebBrowse* webBrowse() const { return m_webBrowse; }
    QJsonObject onlineReport() const;
    static QStringList addressesIn(const QString& text);          // the http(s) addresses in pasted or dropped text
private:
    void playWeb(const QString& entry, int index);
    void askYtDlp(const QString& page, int index, int request);
    void webResolved(const struct OnlineResult& r, const QString& entry, int index, int request);
    void webFailed(const QString& title, const QString& text, const QString& details);
    void offerYtDlp(const QString& entry, int index);
    void updateOnlineStatus(bool askVersion = true);
    struct OnlineOptions onlineOptions() const;
    QString resumeKey() const;            // what the playing video's place is kept under
    void setHint(const QString& text);    // the words in the empty picture; empty: the usual ones
    class OnlineResolver* m_online = nullptr;
    class WebBrowse* m_webBrowse = nullptr;
    class BrowseScreen* m_browse = nullptr;
    bool m_browseResume = false;     // the video played before the browser was opened (it plays on when it closes)
    QJsonObject m_browsePicked;      // the video chosen in the browser (what it knows goes to the history)
    QString m_webPage;                    // the page of the web video that is open
    QString m_webAsking;                  // the page yt-dlp is being asked about
    QString m_webSite, m_webWhat;         // "Youtube", "1080p · VP9 + Opus"
    bool m_webLive = false;
    QList<QPair<QByteArray, QByteArray>> m_webHeaders;
    QHash<QString, QPair<QString, bool>> m_webSubs;   // subtitle address → file type, automatic captions?
    QVector<QPair<qint64, QString>> m_webChapters;   // start (ns), title
    int m_webRequest = 0;                 // only the latest question's answer is used
    int m_webDepth = 0;                   // a playlist inside a playlist inside...
    bool m_webIsPage = true;              // the address being asked about is a page (its server said so, or the playlist did)
    bool m_webInner = false;              // (the next thing played is an entry of the list just found)
    bool m_ytAsking = false;              // yt-dlp is being asked for its version
    bool m_webRetried = false, m_webRetryNext = false;   // the addresses stopped working and were asked for again
    qint64 m_webStartNs = 0;
    QElapsedTimer m_webResolvedAt;        // since yt-dlp last named this video's addresses
    QString m_webFallbackNote;            // yt-dlp found nothing and the address is tried as it is: what yt-dlp said
    QString m_webPending;                 // waits for yt-dlp to be fetched
    int m_webPendingIndex = -1;
    QString m_ytVersion, m_ytStatus, m_onlineAsked;
    QStringList m_webNotesShown;
    QPointer<QDialog> m_ytDialog;
    int m_webResolves = 0, m_webReasks = 0, m_webFailures = 0, m_ytFetches = 0, m_ytFetchFailures = 0;

    // 2.12: playback without a graphics card (FastPath.cpp)
public:
    void setVideoPath(int mode);                // 0 automatic, 1 always the fast path, 2 never
    void setLookDetail(int mode);               // without a graphics card: 0 automatic, 1 full size, 2 half size
    // 2.13: Enhance (EnhanceUi.cpp)
    void setEnhanceUpscale(bool on);
    void setEnhanceSharpness(double v);
    void setSmoothMotion(bool on);
    void applyEnhance();
    QString enhanceDescription() const;
    // 2.15: NVIDIA's AI methods for Enhance (where the NVIDIA Video Effects SDK is installed)
    void setInfoOverlay(bool on);     // the technical info overlay (I)
    void setEnhanceNvidia(bool on, int quality, int mode);
    QString nvidiaStatus() const;
    void refreshNvidiaStatus();
    double m_nvDrawRate = 0;          // pictures drawn a second, lately (for the status line)
    double m_nvDrawsSeen = -1;
    QElapsedTimer m_nvRateTimer;
    void updateVideoPath();
    qint64 shownFrameTime() const;
    bool ensureNativeFrame();                   // a frame as decoded, for original-frame grabs; then releaseNativeFrame()
    void releaseNativeFrame();
    QJsonObject videoPathReport() const;
    QString videoPathDescription() const;
private:
    bool fastPathWanted() const;
    int m_fastHold = 0;
    bool m_fastHeld = false;                   // the pipeline is asked for frames as decoded meanwhile
    QTimer m_pathTimer;

    // 2.11: everyday playback (Everyday.cpp)
public:
    void setSubtitlesWanted(bool on, bool announce = true);   // off until asked for; then it stays as set
    void chooseSubtitleTrack(int index);                       // -1: off
    void chooseAudioTrack(int index);
    void setAudioDelay(int ms, bool announce = true);
    void setSubtitleDelay(int ms, bool announce = true);
    void setSubtitleStyle(const struct SubtitleStyle& st);
    void setNightMode(bool on, bool announce = true);
    void setDeinterlace(bool on);
    void setShuffle(bool on);
    void setRepeatMode(int mode);
    void setAutoNext(bool on);
    void setSleepTimer(int minutes);            // 0 off, -1 at the end of this video
    void setSleepTimerSeconds(int seconds);     // (tests)
    void cycleSleepTimer();
    bool savePlaylistFile(const QString& path);
    static QStringList readPlaylistFile(const QString& path);
    static bool isPlaylistFile(const QString& path);
    static QString nextInFolder(const QString& path);
    QJsonObject everydayReport() const;
    PlaylistPanel* playlistPanel() const { return m_playlist; }
    void showPlaylist(bool on);
private:
    void applyEverydaySettings();
    struct SubtitleStyle subtitleStyleFromSettings() const;
    void maybeLoadOfferedSubtitle();
    void refreshSubtitlesSoon(bool evenWhilePlaying);
    void refreshSubtitlesNow();
    bool m_gifBusy() const;
    QTimer m_subRefresh;
    bool m_subRefreshAlways = false;
    int pickNext(int dir);
    bool playAfterEnd();
    void savePlaylistDialog();
    int sleepRemainingSeconds() const;
    void updateSleepStatus();
    void sleepTick();
    void goToSleep();
    QStringList m_shufflePlayed, m_shuffleHistory;   // this round's videos; the way back
    QTimer m_sleepTimer;
    qint64 m_sleepAt = 0;              // wall clock ms; 0: no timer
    bool m_sleepAtEnd = false, m_sleepWarned = false, m_sleepFading = false;
    int m_sleepMinutes = 0, m_sleepCount = 0;
    QStringList m_playLog;             // what was opened, in order (the checks read it)
    QString m_autoSubTried;            // the Jellyfin item whose offered subtitles were already tried

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
    QList<QUrl> m_jfOwnSubs;   // (Jellyfin, the original file) where the server hands out each of its own subtitle tracks as a file
    QString m_extSubLabel;                      // the one loaded, if any
    qint64 m_loopA = -1, m_loopB = -1;          // ns
    CutDialog* m_cutDialog = nullptr;
    GifDialog* m_gifDialog = nullptr;
    TvController* m_tv = nullptr;
    TvPanel* m_tvPanel = nullptr;
    bool m_tvBurst = false;
    bool m_tvLoaded = false;          // the programme the TV asked for is open
    quint64 m_tvLoadedSerial = 0;     // the frame count when it opened
    QString m_tvTitle;
    QString m_lastSource;          // what was last opened, to open it again another way
    int m_lastSourceIndex = -1;
    class LosslessCutter* m_keyframes = nullptr;   // finds keyframes for Shift+← / Shift+→
    bool m_kfBusy = false, m_kfForward = true, m_kfRefining = false;
    qint64 m_kfFrom = 0, m_kfStep = 0, m_kfBest = -1;
    QString m_kfSource;
    bool m_gifRecording = false;   // the A–B loop waits meanwhile
    bool m_gifLook = true;
    double m_gifClockSaved = -1.0;
    QTimer m_resumeTimer;
    void jellyfinStopCurrent(bool waitForServer);
    void openResolved(int index, const QString& uri, qint64 start, const QString& shown, const QString& note, const QList<WebStream>& streams = {});
    void playSource(const QString& path, int index);
    void tvPlay(const QString& source, qint64 offsetNs, const QString& title, bool burst);
    void tvSnow();
    QString jellyfinPlayDescription() const;   // "original file" / "converted by the server (…)"
    void vcr(const QString& text, double seconds);   // VCR on-screen display, when enabled
    bool m_deskActive = false;
};
