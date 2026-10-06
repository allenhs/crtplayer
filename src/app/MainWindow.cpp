#include "MainWindow.h"

#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "render/DeskView.h"
#include "render/DeskRenderer.h"
#include "app/DeskWindow.h"
#include "app/WinWindow.h"
#include "jellyfin/JellyfinClient.h"
#include "online/OnlineVideo.h"
#include "playback/Thumbnailer.h"
#include "app/Gamepad.h"
#ifndef _WIN32
#include "app/Mpris.h"   // media keys and the desktop's media widget (Linux: D-Bus)
#endif
#include "app/SleepInhibitor.h"
#include "playback/Distro.h"
#include "playback/TapeAudio.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QButtonGroup>
#include <QStandardItemModel>
#include <QFormLayout>
#include <QPushButton>
#include "settings/ResumeStore.h"
#include "ui/JellyfinPanel.h"
#include "ui/ControlBar.h"
#include "ui/CutDialog.h"
#include "ui/TvPanel.h"
#include "tv/TvController.h"
#include "edit/LosslessCutter.h"
#include "ui/GifDialog.h"
#include "ui/CrtPanel.h"
#include "ui/DisplayPanel.h"
#include "ui/Icons.h"
#include "ui/PlaybackPanel.h"
#include "ui/PlaylistPanel.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QClipboard>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QVBoxLayout>
#include <QDirIterator>
#include <QGuiApplication>
#include <QScreen>
#include <QSysInfo>
#include <gst/gst.h>
#include <QEventLoop>
#include <QNetworkReply>
#include <QLineEdit>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QScrollArea>
#include <QTabWidget>
#include <QScreen>
#include <QToolButton>
#include <QUrl>
#include <QWindow>

namespace {
const char* kVideoFilter =
    "Video files (*.mp4 *.m4v *.mkv *.webm *.mov *.avi *.wmv *.flv *.mpg *.mpeg *.ts *.m2ts *.mts *.vob *.ogv *.3gp *.divx *.asf *.y4m);;"
    "Audio files (*.mp3 *.flac *.ogg *.opus *.m4a *.wav *.aac);;Playlists (*.m3u8 *.m3u);;All files (*)";

QJsonObject rectJson(const QRectF& r) { return {{"x", r.x()}, {"y", r.y()}, {"w", r.width()}, {"h", r.height()}}; }
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_player = new Player(this);
    buildUi();
    buildActions();
    restoreSettings();

    m_posTimer.setInterval(100);
    connect(&m_posTimer, &QTimer::timeout, this, &MainWindow::updatePosition);
    m_posTimer.start();
    m_hideTimer.setSingleShot(true);
    m_hideTimer.setInterval(2200);
    connect(&m_hideTimer, &QTimer::timeout, this, &MainWindow::updateAutoHide);
    m_osdTimer.setSingleShot(true);
    connect(&m_osdTimer, &QTimer::timeout, m_osd, &QLabel::hide);
    m_infoTimer.setInterval(500);
    connect(&m_infoTimer, &QTimer::timeout, this, &MainWindow::updateInfoOverlay);

    // Player -> UI
    connect(m_player, &Player::stateChanged, this, [this](Player::State s) {
        if (s == Player::State::Playing) vcr(QStringLiteral("PLAY ▶"), 3.0);
        else if (s == Player::State::Paused) vcr(QStringLiteral("PAUSE ❚❚"), 0);
        m_controls->setPlaying(s == Player::State::Playing);
        if (s == Player::State::Playing) m_video->resetSyncStats();
        onMouseActivity();
    });
    connect(m_player, &Player::durationChanged, this, [this](qint64 ns) { m_controls->setDuration(ns > 0 ? ns / 1000000 : 0); });
    connect(m_player, &Player::tracksChanged, this, [this] { rebuildAudioMenu(); rebuildSubtitleMenu(); });
    // 2.16: a subtitle track of a video played from the server: its lines are fetched as a file, which the server
    // makes of it (the player then knows them all, wherever in the video it jumps to; a second reading of the
    // video itself, over the network, would fetch everything twice).
    connect(m_player, &Player::embeddedSubtitleFileWanted, this, [this](int track) {
        // (only when the server and the player count the same subtitle tracks)
        if (m_jfItemId.isEmpty() || m_jfTranscoding || track < 0 || track >= m_jfOwnSubs.size() || m_jfOwnSubs.size() != m_player->embeddedTextCount() ||
            m_jfOwnSubs[track].isEmpty()) {
            m_player->setEmbeddedSubtitleFile(track, QString());   // (not to be had: the playback library draws the track)
            return;
        }
        const QString item = m_jfItemId;
        const QUrl u = m_jfOwnSubs[track];
        m_jf->fetchBytes(u, [this, item, track, u](const QByteArray& data, const QString& err) {
            if (item != m_jfItemId || m_jfTranscoding) return;   // (another video by now)
            const bool ok = err.isEmpty() && !data.trimmed().isEmpty();
            if (!ok) qWarning() << "Jellyfin: no subtitle file for track" << track << ":" << err;
            m_player->setEmbeddedSubtitleFile(track, ok ? stageSubtitle(data, u.toString(QUrl::RemoveQuery)) : QString());
        });
    });
    // Subtitles wanted, none in the video, but the server offers some: load them (once the tracks are known).
    connect(m_player, &Player::mediaLoaded, this, [this] { QTimer::singleShot(400, this, [this] { maybeLoadOfferedSubtitle(); }); });
    m_subRefresh.setSingleShot(true);
    m_subRefresh.setInterval(250);
    connect(&m_subRefresh, &QTimer::timeout, this, [this] { refreshSubtitlesNow(); });
    // Without a graphics card: which frames the video pipeline should deliver follows what is shown.
    m_pathTimer.setInterval(250);
    connect(&m_pathTimer, &QTimer::timeout, this, [this] { updateVideoPath(); });
    m_pathTimer.start();
    m_sleepTimer.setInterval(500);
    connect(&m_sleepTimer, &QTimer::timeout, this, [this] { sleepTick(); });
    connect(m_player, &Player::orientationChanged, m_video, &VideoWidget::setOrientationTag);
    connect(m_player, &Player::decoderChanged, this, &MainWindow::updateDecoderStatus);
    connect(m_player, &Player::mediaLoaded, this, [this] { updateDecoderStatus(); updateTitle(); });
    connect(m_player, &Player::endOfStream, this, [this] {
        if (tvOn()) {   // Cable TV: whatever the channel has on next
            // (Only the end of the programme that was asked for counts, and only once: an end
            // reported twice, or by a file that was already replaced, would skip a programme.)
            if (!m_tvLoaded) return;
            m_tvLoaded = false;
            if (m_sleepAtEnd) goToSleep();   // the sleep timer was waiting for this programme to end
            else m_tv->programEnded();
            return;
        }
        // An A-B loop whose end lies at the video's end (or so near it that the end came first): round again.
        if (m_loopB > m_loopA && m_loopA >= 0 && !m_gifRecording) {
            m_player->seek(m_loopA, Player::SeekMode::Accurate);
            m_player->play();
            return;
        }
        if (m_sleepAtEnd) { goToSleep(); return; }   // the sleep timer was waiting for this
        if (!playAfterEnd()) {
            m_player->pause();
            showOsd(tr("End of playlist"));
            vcr(QStringLiteral("STOP ■"), 3.0);
            m_video->powerOff();   // when the preset has power effects
        }
    });
    connect(m_player, &Player::errorOccurred, this, [this](const QString& titleIn, const QString& detailsIn) {
        QString title = titleIn, details = detailsIn;
        // A web video whose addresses stopped working (they do, after a few hours: a video left paused
        // overnight): once, yt-dlp is asked for new ones, and the video goes on where it was.
        // (Again whenever that happens: but not twice in a row, when the new addresses do not work either.)
        if (!m_webPage.isEmpty() && (!m_webRetried || m_webResolvedAt.elapsed() > 30000) && m_player->lastErrorFromSource() && !tvOn()) {
            m_webRetried = true;
            m_webRetryNext = true;
            ++m_webReasks;
            m_lastWarning = title + ": " + details.section("\n\nDetails:", 0, 0);
            const qint64 pos = m_player->position();
            m_pendingStartNs = m_webLive ? -1 : pos > 0 ? pos : m_webStartNs;
            showOsd(tr("The video's address stopped working — asking yt-dlp for a new one"), 5000);
            QTimer::singleShot(0, this, [this] { playSource(m_lastSource, m_lastSourceIndex); });
            return;
        }
        if (!m_webPage.isEmpty() && m_player->lastErrorFromSource()) {
            // yt-dlp found the video and the site will not send it (asked twice by now).
            const QString said = details.section("\n\nDetails:", 0, 0);
            details = tr("yt-dlp found the video, but the site would not send it (%1).").arg(said);
            const int age = OnlineResolver::versionAgeDays(m_ytVersion, QDate::currentDate());
            if (age > 60)
                details += QStringLiteral("\n\n") + tr("This yt-dlp is from %1. Video sites change every few weeks, and an older yt-dlp stops finding their "
                                                       "videos: a newer one usually helps (Settings → Playback → Videos from web sites).").arg(m_ytVersion);
            details += QStringLiteral("\n\nDetails: ") + detailsIn.section("\n\nDetails: ", 1);
            title = tr("The site would not send the video");
        }
        if (!m_webFallbackNote.isEmpty()) {   // yt-dlp found no video at this address, and as a stream it does not play either
            details = tr("yt-dlp found no video at this address (%1), and it does not play as a file or stream either.").arg(m_webFallbackNote)
                      + QStringLiteral("\n\n") + details;
            title = tr("No video was found at this address");
        }
        // A Jellyfin original that won't play here (a codec this computer lacks, a damaged
        // file): once, the server is asked to convert it instead.
        if (!m_jfItemId.isEmpty() && !m_jfTranscoding && !m_jfRetried && m_jf->isSignedIn()) {
            m_jfRetried = true;
            m_jfForceNext = true;
            m_lastWarning = title + ": " + details.section("\n\nDetails:", 0, 0);
            const qint64 pos = m_player->position();
            m_pendingStartNs = pos > 0 ? pos : m_jfStartNs;
            showOsd(tr("The original file won't play here — asking the server to convert it"), 5000);
            QTimer::singleShot(0, this, [this] { playSource(m_lastSource, m_lastSourceIndex); });
            return;
        }
        m_lastError = title + ": " + details;
        if (tvOn()) {   // Cable TV: no dialog; the channel shows "no signal" and carries on with the next programme
            qWarning() << "Cable TV programme failed:" << m_lastError;
            m_tv->programFailed(title);
            return;
        }
        m_emptyHint->setText(title + tr("\nDetails are in the error dialog. Drop another file to continue."));
        m_emptyHint->show();
        layoutOverlays();
        auto* box = new QMessageBox(QMessageBox::Warning, title, details.section("\n\nDetails:", 0, 0), QMessageBox::Ok, this);
        box->setDetailedText(details);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->open();
    });
    connect(m_player, &Player::warningOccurred, this, [this](const QString& w) {
        m_lastWarning = w;
        showOsd(w, 5000);
    });
    connect(m_video, &VideoWidget::sourceChanged, this, [this] {
        m_emptyHint->setVisible(!m_video->hasFrame() && m_player->state() != Player::State::Error && m_player->currentUri().isEmpty());
        updateSourceInfo();
    });
    connect(m_video, &VideoWidget::glFailed, this, [this](const QString& msg) {
        QMessageBox::critical(this, tr("OpenGL 3.3 is required"),
                              tr("The video renderer could not start:\n%1\n\nCheck that your graphics card's driver is installed.").arg(msg));
    }, Qt::QueuedConnection);
    connect(m_video, &VideoWidget::mouseActivity, this, &MainWindow::onMouseActivity);
    connect(m_video, &VideoWidget::doubleClicked, this, [this] { setFullscreen(!m_fullscreen); });
    connect(m_video, &VideoWidget::splitChanged, this, [this](double f) { m_settings.split = f; });

    // Controls -> player
    connect(m_controls->openButton, &QToolButton::clicked, this, [this] { openDialog(); });
    connect(m_controls->playButton, &QToolButton::clicked, this, [this] {
        if (m_player->currentUri().isEmpty() && m_playlist->count() > 0) playIndex(std::max(0, m_playlist->currentIndex()));
        else m_player->togglePause();
    });
    connect(m_controls->prevButton, &QToolButton::clicked, this, [this] { nextItem(-1); });
    connect(m_controls->nextButton, &QToolButton::clicked, this, [this] { nextItem(1); });
    connect(m_controls->stepBackButton, &QToolButton::clicked, this, [this] { m_player->stepFrame(false); });
    connect(m_controls->stepFwdButton, &QToolButton::clicked, this, [this] { m_player->stepFrame(true); });
    connect(m_controls->muteButton, &QToolButton::clicked, this, [this] {
        m_player->setMuted(!m_player->isMuted());
        m_controls->setMutedIcon(m_player->isMuted());
        showOsd(m_player->isMuted() ? tr("Muted") : tr("Sound on"));
    });
    connect(m_controls, &ControlBar::volumeChanged, this, [this](double v) {
        m_player->setVolume(v);
        if (m_player->isMuted() && v > 0) { m_player->setMuted(false); m_controls->setMutedIcon(false); }
    });
    connect(m_controls->seek(), &SeekSlider::scrubStarted, this, [this] {
        m_hideTimer.stop();
        if (m_desk) m_desk->view()->setScrubbing(true);   // no tile reveal while dragging the timeline
    });
    connect(m_controls->seek(), &SeekSlider::scrubbedTo, this, [this](qint64 ms) { m_player->seek(ms * 1000000, Player::SeekMode::Fast); });
    connect(m_controls->seek(), &SeekSlider::scrubFinished, this, [this](qint64 ms) {
        vcr(ms * 1000000 < m_player->position() ? QStringLiteral("◀◀ REW") : QStringLiteral("▶▶ FF"), 1.2);
        m_player->seek(ms * 1000000, Player::SeekMode::Accurate);
        onMouseActivity();
        if (m_desk) QTimer::singleShot(1500, m_desk, [this] { if (m_desk) m_desk->view()->setScrubbing(false); });   // after the final seek's dip
    });
    connect(m_controls->crtButton, &QToolButton::clicked, this, [this](bool on) { setBypass(!on); });
    connect(m_controls->compareButton, &QToolButton::clicked, this, [this](bool on) { setCompare(on); });
    connect(m_controls->fullscreenButton, &QToolButton::clicked, this, [this] { setFullscreen(!m_fullscreen); });
    connect(m_controls->settingsButton, &QToolButton::clicked, this, [this](bool on) { m_settingsDock->setVisible(on); });
    connect(m_controls->playlistButton, &QToolButton::clicked, this, [this](bool on) { m_playlistDock->setVisible(on); });
        connect(m_controls->presetCombo(), &QComboBox::activated, this, [this](int i) { selectPreset(m_controls->presetCombo()->itemText(i)); });

    // Panels
    connect(m_crtPanel, &CrtPanel::paramsChanged, this, [this](const CrtParams& p) { applyParams(p, false); });
    connect(m_crtPanel, &CrtPanel::presetSelected, this, [this](const QString& n) { selectPreset(n); });
    connect(m_crtPanel, &CrtPanel::saveAsRequested, this, &MainWindow::savePresetAs);
    connect(m_crtPanel, &CrtPanel::saveRequested, this, &MainWindow::savePreset);
    connect(m_crtPanel, &CrtPanel::renameRequested, this, &MainWindow::renamePreset);
    connect(m_crtPanel, &CrtPanel::deleteRequested, this, &MainWindow::deletePreset);
    connect(m_crtPanel, &CrtPanel::importRequested, this, &MainWindow::importPreset);
    connect(m_crtPanel, &CrtPanel::exportRequested, this, &MainWindow::exportPreset);
    connect(m_crtPanel, &CrtPanel::revertRequested, this, [this] { selectPreset(m_presetName); });
    connect(m_crtPanel->bypassButton(), &QPushButton::clicked, this, [this](bool on) { setBypass(on); });
    connect(m_crtPanel->compareButton(), &QPushButton::clicked, this, [this](bool on) { setCompare(on); });
    connect(m_displayPanel, &DisplayPanel::scaleModeChanged, this, &MainWindow::setScaleMode);
    connect(m_displayPanel, &DisplayPanel::cropChanged, this, [this](const CropFractions& c) {
        setCrop(c);
        if (!c.isNull()) setScaleMode(ScaleMode::Crop);
    });
    connect(m_displayPanel, &DisplayPanel::cropToAspectRequested, this, [this](double a) {
        setCrop(cropForAspect(displaySize(m_video->sourceFormat(), m_video->aspectOverride()), a));
        setScaleMode(ScaleMode::Crop);
    });
    connect(m_displayPanel, &DisplayPanel::aspectOverrideChanged, this, &MainWindow::setAspectOverride);
    connect(m_playbackPanel, &PlaybackPanel::hardwareDecodingChanged, this, [this](bool on) {
        m_settings.hardwareDecoding = on;
        m_player->setHardwareDecoding(on);
        reopenForDecoderChange();
    });
    connect(m_playbackPanel, &PlaybackPanel::screenshotFilteredChanged, this, [this](bool f) { m_settings.screenshotFiltered = f; });
    connect(m_playbackPanel, &PlaybackPanel::screenshotDirChanged, this, [this](const QString& d) { m_settings.screenshotDir = d; });
    connect(m_playlist, &PlaylistPanel::activated, this, &MainWindow::playIndex);
    connect(m_playlist, &PlaylistPanel::addRequested, this, &MainWindow::addDialog);
    connect(m_playlist, &PlaylistPanel::shuffleChanged, this, &MainWindow::setShuffle);
    connect(m_playlist, &PlaylistPanel::repeatModeChanged, this, &MainWindow::setRepeatMode);
    connect(m_playlist, &PlaylistPanel::saveRequested, this, &MainWindow::savePlaylistDialog);
    connect(m_playlist, &PlaylistPanel::changed, this, [this] { m_shufflePlayed.clear(); m_shuffleHistory.clear(); });
    connect(m_settingsDock, &QDockWidget::visibilityChanged, this, [this](bool) {
        QSignalBlocker b(m_controls->settingsButton);
        m_controls->settingsButton->setChecked(m_settingsDock->isVisible());
        if (m_settingsDock->isVisible()) updateOnlineStatus();   // (which yt-dlp there is: asked when it is looked at)
    });
    connect(m_playbackPanel, &PlaybackPanel::onlineHeightChanged, this, &MainWindow::setOnlineMaxHeight);
    connect(m_playbackPanel, &PlaybackPanel::ytDlpRequested, this, &MainWindow::fetchYtDlp);
    connect(m_playlistDock, &QDockWidget::visibilityChanged, this, [this](bool) {
        QSignalBlocker b(m_controls->playlistButton);
        m_controls->playlistButton->setChecked(m_playlistDock->isVisible());
    });

    // Jellyfin
    connect(m_jfPanel, &JellyfinPanel::playRequested, this, &MainWindow::playJellyfin);
    connect(m_jfPanel, &JellyfinPanel::enqueueRequested, this, &MainWindow::enqueueJellyfin);
    connect(m_jfPanel, &JellyfinPanel::convertedPlayRequested, this, &MainWindow::playJellyfinConverted);
    m_jfPanel->setMaxBitrateMbps(m_settings.jfMaxBitrateMbps);
    connect(m_jfPanel, &JellyfinPanel::maxBitrateChanged, this, [this](int mbps) {
        m_settings.jfMaxBitrateMbps = mbps;
        showOsd(mbps > 0 ? tr("Jellyfin quality: up to %1 Mbit/s, from the next video").arg(mbps)
                         : tr("Jellyfin quality: the original file, from the next video"));
    });
    connect(m_controls->jellyfinButton, &QToolButton::clicked, this, [this](bool on) { showJellyfin(on); });
    connect(m_jfDock, &QDockWidget::visibilityChanged, this, [this](bool) {
        QSignalBlocker b(m_controls->jellyfinButton);
        m_controls->jellyfinButton->setChecked(m_jfDock->isVisible());
    });
    connect(m_jf, &JellyfinClient::requestFailed, this, [this](const QString& m) { showOsd(m, 4000); });
    connect(m_jf, &JellyfinClient::homeLoaded, this, [this](const QVector<JfItem>& resume, const QVector<JfItem>&) { m_jfResume = resume; });
    m_jfTimer.setInterval(10000);   // progress reports keep resume points current on the server
    connect(&m_jfTimer, &QTimer::timeout, this, [this] {
        if (!m_jfItemId.isEmpty() && m_jfStarted) m_jf->reportProgress(m_jfItemId, m_player->position(), !m_player->isPlaying());
    });
    connect(m_player, &Player::mediaLoaded, this, [this] {
        if (m_jfItemId.isEmpty() || m_jfStarted) return;
        if (tvOn()) return;   // Cable TV leaves the server's resume points and "watched" marks alone
        m_jf->reportStart(m_jfItemId, m_player->position(), false);
        m_jfStarted = true;
        m_jfTimer.start();
    });
    connect(m_player, &Player::stateChanged, this, [this](Player::State st) {
        if (m_jfItemId.isEmpty() || !m_jfStarted) return;
        if (st == Player::State::Paused) m_jf->reportProgress(m_jfItemId, m_player->position(), true, "pause");
        else if (st == Player::State::Playing) m_jf->reportProgress(m_jfItemId, m_player->position(), false, "unpause");
    });
    connect(m_player, &Player::endOfStream, this, [this] {
        if (!m_jfItemId.isEmpty() && m_jfStarted) {
            m_jf->reportStopped(m_jfItemId, m_player->duration());   // at the end: the server marks it played
            m_jfStarted = false;
            m_jfPlaySession.clear();   // the report ended the conversion
            m_jfTimer.stop();
        }
    });
    m_jf->restoreSession();

    // ---- Cable TV (2.10)
    {
        TvController::Host h;
        h.play = [this](const QString& source, qint64 offsetNs, const QString& title, bool burst) { tvPlay(source, offsetNs, title, burst); };
        h.snow = [this] { tvSnow(); };
        h.setOverlay = [this](const QImage& img) {
            m_video->setOverlayImage(img);
            if (m_desk) m_desk->view()->update();
        };
        h.pictureAspect = [this] {
            QSizeF ds = m_video->displaySizeNow();
            if (m_deskActive && m_desk && !m_desk->view()->pictureDisplaySize().isEmpty()) ds = m_desk->view()->pictureDisplaySize();
            return ds.height() > 0 ? ds.width() / ds.height() : 4.0 / 3.0;
        };
        h.positionMs = [this] { return m_player->position() / 1000000; };
        h.framesShown = [this] { return m_tvLoaded ? int(std::min<quint64>(m_player->frameSerial() - m_tvLoadedSerial, 1000000)) : 0; };
        h.jellyfin = m_jf;
        m_tv = new TvController(h, QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("channels.json"), this);
        m_tvPanel->setController(m_tv);
        connect(m_tvPanel, &TvPanel::tvModeRequested, this, &MainWindow::setTvMode);
        connect(m_tv, &TvController::stateChanged, this, [this] { m_tvPanel->refresh(); updateTitle(); });
        connect(m_player, &Player::mediaLoaded, this, [this] { m_tvLoadedSerial = m_player->frameSerial(); m_tvLoaded = true; });
        connect(m_jfPanel, &JellyfinPanel::tvChannelRequested, this, [this](const JfItem& item) {
            const int n = m_tv->addJellyfinChannel(item.id, item.displayName());
            showOsd(tr("Channel %1: %2").arg(n).arg(item.displayName()), 3000);
        });
        connect(m_jf, &JellyfinClient::signedIn, this, [this] {   // channels waiting for the server
            for (const TvChannel& c : m_tv->channels()) if (!c.jellyfin.isEmpty() && c.lineup.programs.isEmpty()) m_tv->rescan(c.number);
        });
    }

    // ---- Bazzite fit (1.9): media keys / KDE media widget (MPRIS), game controllers
#ifndef _WIN32
    m_mpris = new Mpris(this, m_player);
#endif
    m_gamepad = new Gamepad(this);
    if (inGamescope()) m_gamepad->setAlwaysActive(true);   // gamescope shows only this app
    // No screen dimming or sleep while a video plays (released on pause / stop / quit).
    m_sleep = new SleepInhibitor(this);
    auto updateSleep = [this] { m_sleep->setActive(m_settings.keepAwake && m_player->isPlaying()); };
    connect(m_player, &Player::stateChanged, this, updateSleep);
    m_playbackPanel->setKeepAwake(m_settings.keepAwake);
    m_playbackPanel->setLookSound(m_settings.lookSound);
    m_playbackPanel->setNoiseVolume(m_settings.noiseVolume);
    m_playbackPanel->setEffectStrength(m_settings.effectStrength);
    connect(m_playbackPanel, &PlaybackPanel::noiseVolumeChanged, this, [this](double v) { setSoundLevels(v, m_settings.effectStrength); });
    connect(m_playbackPanel, &PlaybackPanel::effectStrengthChanged, this, [this](double v) { setSoundLevels(m_settings.noiseVolume, v); });
    connect(m_playbackPanel, &PlaybackPanel::lookSoundChanged, this, [this](bool on) { m_settings.lookSound = on; applyLookSound(); });
    connect(m_playbackPanel, &PlaybackPanel::subtitlesWantedChanged, this, [this](bool on) { setSubtitlesWanted(on); });
    connect(m_playbackPanel, &PlaybackPanel::subtitleStyleChanged, this, [this](int size, int color, int back, int pos) {
        SubtitleStyle st; st.size = size; st.color = color; st.background = back; st.position = pos;
        setSubtitleStyle(st);
    });
    connect(m_playbackPanel, &PlaybackPanel::subtitleDelayChanged, this, [this](int ms) { setSubtitleDelay(ms, false); });
    connect(m_playbackPanel, &PlaybackPanel::audioDelayChanged, this, [this](int ms) { setAudioDelay(ms, false); });
    connect(m_playbackPanel, &PlaybackPanel::nightModeChanged, this, [this](bool on) { setNightMode(on, false); });
    connect(m_playbackPanel, &PlaybackPanel::deinterlaceChanged, this, &MainWindow::setDeinterlace);
    connect(m_playbackPanel, &PlaybackPanel::videoPathChanged, this, &MainWindow::setVideoPath);
    connect(m_playbackPanel, &PlaybackPanel::lookDetailChanged, this, &MainWindow::setLookDetail);
    connect(m_playbackPanel, &PlaybackPanel::enhanceUpscaleChanged, this, &MainWindow::setEnhanceUpscale);
    connect(m_playbackPanel, &PlaybackPanel::enhanceSharpnessChanged, this, &MainWindow::setEnhanceSharpness);
    connect(m_playbackPanel, &PlaybackPanel::smoothMotionChanged, this, &MainWindow::setSmoothMotion);
    connect(m_playbackPanel, &PlaybackPanel::nvidiaChanged, this, &MainWindow::setEnhanceNvidia);
    {
        // What NVIDIA's methods are doing changes as the video plays: the line in the panel follows.
        auto* nvTimer = new QTimer(this);
        nvTimer->setInterval(1000);
        connect(nvTimer, &QTimer::timeout, this, &MainWindow::refreshNvidiaStatus);
        nvTimer->start();
    }
    connect(m_playbackPanel, &PlaybackPanel::autoNextChanged, this, &MainWindow::setAutoNext);
    connect(m_playbackPanel, &PlaybackPanel::sleepTimerChanged, this, &MainWindow::setSleepTimer);
    connect(m_playbackPanel, &PlaybackPanel::keepAwakeChanged, this, [this, updateSleep](bool on) { m_settings.keepAwake = on; updateSleep(); });

    // ---- resume, recent files, subtitles, speed, loop, chapters, previews (1.8)
    m_resume = new ResumeStore;
    m_resumeTimer.setInterval(15000);
    connect(&m_resumeTimer, &QTimer::timeout, this, &MainWindow::rememberPosition);
    m_resumeTimer.start();
    connect(m_player, &Player::mediaLoaded, this, [this] {
        // A web video's chapters come from its page, not from its file.
        if (!m_webPage.isEmpty() && !m_webChapters.isEmpty() && m_player->chapters().isEmpty()) {
            QVector<ChapterInfo> chapters;
            for (const auto& c : m_webChapters) chapters.append(ChapterInfo{c.first, c.second});
            m_player->setChapters(chapters);
        }
        QString entry = m_currentLocal;
        if (entry.isEmpty() && !m_webPage.isEmpty() && !tvOn()) {   // (kept as its page; the title is for the menu)
            entry = OnlineResolver::reference(m_webPage, m_mediaTitle);
            for (int i = m_settings.recentFiles.size() - 1; i >= 0; --i)
                if (OnlineResolver::isReference(m_settings.recentFiles[i]) && OnlineResolver::referencePage(m_settings.recentFiles[i]) == m_webPage)
                    m_settings.recentFiles.removeAt(i);
        }
        if (entry.isEmpty()) return;
        m_settings.recentFiles.removeAll(entry);
        m_settings.recentFiles.prepend(entry);
        while (m_settings.recentFiles.size() > 15) m_settings.recentFiles.removeLast();
    });
    connect(m_player, &Player::endOfStream, this, [this] { if (!resumeKey().isEmpty() && !tvOn()) m_resume->forget(resumeKey()); });
    // Movie theater curtains follow playback.
    connect(m_player, &Player::stateChanged, this, &MainWindow::updateTheater);
    connect(m_player, &Player::seekFinished, this, [this] { if (m_desk) m_desk->view()->noteSeek(); });
    connect(m_player, &Player::mediaLoaded, this, [this] {
        m_theaterEnded = false;
        if (m_desk) m_desk->view()->notifyNewMedia();   // its start plays the CG room's tile reveal
        updateTheater();
    });
    connect(m_player, &Player::endOfStream, this, [this] { m_theaterEnded = true; QTimer::singleShot(300, this, &MainWindow::updateTheater); });
    connect(m_player, &Player::chaptersChanged, this, &MainWindow::updateSeekMarks);
    connect(m_player, &Player::durationChanged, this, [this](qint64) { updateSeekMarks(); });
    {
        auto* recent = new QMenu(this);
        connect(recent, &QMenu::aboutToShow, this, [this, recent] { recent->clear(); fillRecentMenu(recent); });
        m_controls->openButton->setMenu(recent);
        m_controls->openButton->setPopupMode(QToolButton::MenuButtonPopup);
    }
    connect(m_playbackPanel, &PlaybackPanel::systemReportRequested, this, [this] {
        QGuiApplication::clipboard()->setText(systemReport());
        showOsd(tr("System report copied to the clipboard"));
    });
    connect(m_jf, &JellyfinClient::subtitlesLoaded, this, [this](const QString& id, const QList<QPair<QString, QUrl>>& subs) {
        if (id != m_jfItemId) return;
        for (const auto& s : subs) m_extSubs.append({s.first, s.second.toString(QUrl::FullyEncoded)});
        rebuildSubtitleMenu();
    });
    // Seek-bar previews (local files): a small frame and the time/chapter above the bar.
    m_thumbs = new Thumbnailer(this);
    m_preview = new QFrame(this);
    m_preview->setObjectName("seekPreview");
    m_preview->setStyleSheet("#seekPreview { background: rgba(20,22,27,235); border: 1px solid rgba(242,163,58,120); border-radius: 6px; }");
    auto* pv = new QVBoxLayout(m_preview);
    pv->setContentsMargins(4, 4, 4, 4);
    pv->setSpacing(3);
    m_previewImage = new QLabel;
    m_previewImage->setAlignment(Qt::AlignCenter);
    m_previewText = new QLabel;
    m_previewText->setAlignment(Qt::AlignCenter);
    m_previewText->setStyleSheet("color: #e8e4da; font-weight: 600;");
    pv->addWidget(m_previewImage);
    pv->addWidget(m_previewText);
    m_preview->hide();
    m_preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_controls->seek()->setRichHover(true);
    connect(m_controls->seek(), &SeekSlider::hovered, this, &MainWindow::showPreview);
    connect(m_controls->seek(), &SeekSlider::hoverLeft, m_preview, &QWidget::hide);
    connect(m_thumbs, &Thumbnailer::ready, this, [this](const QString& uri, qint64 ns, const QImage& img) {
        if (!m_preview->isVisible() || uri != m_player->currentUri()) return;
        m_previewImage->setPixmap(QPixmap::fromImage(img));
        m_preview->adjustSize();
        Q_UNUSED(ns);
    });

    qApp->installEventFilter(this);
    // Save on every exit path (window close, Ctrl+Q, session logout, automation exit).
    connect(qApp, &QCoreApplication::aboutToQuit, this, &MainWindow::saveSettings);
    updateTitle();
    // (Windows: a full-screen window drawn with OpenGL, see there. Last, with every child in place.)
    if (m_video->surfaceMode() == GlSurfaceWidget::Mode::GlWidget) WinWindow::keepComposed(this);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    // (the docks are destroyed after this, with the window's other children, and say so: nobody is listening by then)
    if (m_settingsDock) m_settingsDock->disconnect(this);
    if (m_playlistDock) m_playlistDock->disconnect(this);
    if (m_jfDock) m_jfDock->disconnect(this);
    delete m_desk;
}

void MainWindow::buildUi()
{
    setAcceptDrops(true);
    resize(1280, 800);
    setMinimumSize(640, 400);
    setDockOptions(QMainWindow::AnimatedDocks);

    m_video = new VideoWidget(m_player, this);
    m_video->setNvidiaInstall(NvEnhancer::findInstall());
    m_video->installEventFilter(this);
    setCentralWidget(m_video);

    m_controls = new ControlBar(m_video);
    m_osd = new QLabel(m_video);
    m_osd->setObjectName("osd");
    m_osd->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_osd->hide();
    m_info = new QLabel(m_video);
    m_info->setObjectName("infoOverlay");
    m_info->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_info->setTextFormat(Qt::PlainText);
    m_info->hide();
    m_emptyHint = new QLabel(tr("Drop video files here, press Ctrl+O to open a file\nor Ctrl+L to open a link"), m_video);
    m_emptyHint->setObjectName("emptyHint");
    m_emptyHint->setAlignment(Qt::AlignCenter);
    m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents);

    // Menus attached to control-bar buttons
    m_aspectMenu = new QMenu(this);
    m_aspectGroup = new QActionGroup(this);
    for (ScaleMode m : {ScaleMode::Original, ScaleMode::Fit, ScaleMode::Fill, ScaleMode::Crop}) {
        QAction* a = m_aspectMenu->addAction(scaleModeName(m));
        a->setCheckable(true);
        a->setData(int(m));
        m_aspectGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, m] { setScaleMode(m); });
    }
    m_aspectMenu->addSeparator();
    m_aspectMenu->addAction(tr("Crop and aspect settings…"), this, [this] {
        m_settingsDock->show();
        m_tabs->setCurrentIndex(1);
    });
    m_controls->aspectButton->setMenu(m_aspectMenu);
    m_audioMenu = new QMenu(this);
    m_subMenu = new QMenu(this);
    m_controls->audioButton->setMenu(m_audioMenu);
    m_controls->subtitleButton->setMenu(m_subMenu);
    auto* shotMenu = new QMenu(this);
    shotMenu->addAction(tr("Save filtered frame, as shown\tCtrl+S"), this, [this] { takeScreenshot(true); });
    shotMenu->addAction(tr("Save original frame, no effects\tShift+S"), this, [this] { takeScreenshot(false); });
    shotMenu->addSeparator();
    shotMenu->addAction(tr("Save GIF clip of A–B…"), this, [this] { showGifDialog(); });
    shotMenu->addAction(tr("Cut A–B without re-encoding…\tX"), this, [this] { showCutDialog(); });
    shotMenu->addSeparator();
    shotMenu->addAction(tr("Open screenshot folder"), this, [this] {
        QDir().mkpath(m_settings.screenshotDir);
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings.screenshotDir));
    });
    m_controls->screenshotButton->setMenu(shotMenu);

    // Settings dock
    m_settingsDock = new QDockWidget(tr("Picture"), this);
    m_settingsDock->setObjectName("settingsDock");
    m_settingsDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_tabs = new QTabWidget(m_settingsDock);
    m_crtPanel = new CrtPanel;
    m_displayPanel = new DisplayPanel;
    m_playbackPanel = new PlaybackPanel;
    m_tabs->addTab(m_crtPanel, tr("CRT"));
    m_tabs->addTab(m_displayPanel, tr("Display"));
    {   // (the Playback tab has grown: it scrolls)
        auto* sc = new QScrollArea;
        sc->setWidget(m_playbackPanel);
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        sc->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_tabs->addTab(sc, tr("Playback"));
    }
    m_tvPanel = new TvPanel;
    m_tabs->addTab(m_tvPanel, tr("TV"));
    m_settingsDock->setWidget(m_tabs);
    addDockWidget(Qt::RightDockWidgetArea, m_settingsDock);
    resizeDocks({m_settingsDock}, {380}, Qt::Horizontal);
    m_settingsDock->hide();

    m_playlistDock = new QDockWidget(tr("Playlist"), this);
    m_playlistDock->setObjectName("playlistDock");
    m_playlistDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_playlist = new PlaylistPanel;
    m_playlistDock->setWidget(m_playlist);
    addDockWidget(Qt::LeftDockWidgetArea, m_playlistDock);
    resizeDocks({m_playlistDock}, {240}, Qt::Horizontal);
    m_playlistDock->hide();

    m_jf = new JellyfinClient(this);
    m_online = new OnlineResolver(this);
    m_jfDock = new QDockWidget(tr("Jellyfin"), this);
    m_jfDock->setObjectName("jellyfinDock");
    m_jfDock->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);
    m_jfPanel = new JellyfinPanel(m_jf);
    m_jfDock->setWidget(m_jfPanel);
    addDockWidget(Qt::LeftDockWidgetArea, m_jfDock);
    resizeDocks({m_jfDock}, {420}, Qt::Horizontal);
    m_jfDock->hide();
}

void MainWindow::buildActions()
{
    auto add = [this](const QList<QKeySequence>& keys, auto fn) {
        auto* a = new QAction(this);
        a->setShortcuts(keys);
        a->setShortcutContext(Qt::WindowShortcut);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
        return a;
    };
    add({Qt::Key_Space, Qt::Key_K, Qt::Key_MediaPlay, Qt::Key_MediaTogglePlayPause}, [this] { m_controls->playButton->click(); });
    add({Qt::Key_Right}, [this] { m_player->seekRelative(5'000'000'000LL); showOsd(tr("+5 s")); vcr(QStringLiteral("▶▶ FF"), 1.2); });
    add({Qt::Key_Left}, [this] { m_player->seekRelative(-5'000'000'000LL); showOsd(tr("−5 s")); vcr(QStringLiteral("◀◀ REW"), 1.2); });
    add({QKeySequence(Qt::CTRL | Qt::Key_Right)}, [this] { m_player->seekRelative(30'000'000'000LL); showOsd(tr("+30 s")); vcr(QStringLiteral("▶▶ FF"), 1.2); });
    add({QKeySequence(Qt::CTRL | Qt::Key_Left)}, [this] { m_player->seekRelative(-30'000'000'000LL); showOsd(tr("−30 s")); vcr(QStringLiteral("◀◀ REW"), 1.2); });
    add({Qt::Key_Home}, [this] { m_player->seek(0, Player::SeekMode::Accurate); });
    add({Qt::Key_Minus}, [this] { setSpeed(m_player->rate() <= 0.5 ? 0.25 : m_player->rate() - (m_player->rate() > 1.0 ? 0.25 : 0.25)); });
    add({Qt::Key_Plus, Qt::Key_Equal}, [this] { setSpeed(m_player->rate() + 0.25); });
    add({Qt::Key_Backspace}, [this] { setSpeed(1.0); });
    add({Qt::Key_R}, [this] { cycleLoop(); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_PageDown)}, [this] { jumpChapter(+1); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_PageUp)}, [this] { jumpChapter(-1); });
    add({Qt::Key_Period}, [this] { m_player->stepFrame(true); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_Right)}, [this] { seekKeyframe(true); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_Left)}, [this] { seekKeyframe(false); });
    add({Qt::Key_X}, [this] { showCutDialog(); });
    // Cable TV: Ctrl+T on/off, W the guide ("what's on"), digits the channel number.
    add({QKeySequence(Qt::CTRL | Qt::Key_T)}, [this] { setTvMode(!tvOn()); });
    add({Qt::Key_W}, [this] {
        if (tvOn()) m_tv->setGuide(!m_tv->guideVisible());
        else showOsd(tr("The guide is part of TV mode (Ctrl+T)"));
    });
    for (int d = 0; d <= 9; ++d) add({Qt::Key(Qt::Key_0 + d)}, [this, d] { if (tvOn()) m_tv->digit(d); });
    add({Qt::Key_G}, [this] { showGifDialog()->record(); });   // record straight away, with the last options
    add({Qt::Key_Comma}, [this] { m_player->stepFrame(false); });
    add({Qt::Key_Up}, [this] { m_controls->volumeSlider()->setValue(m_controls->volumeSlider()->value() + 5); showOsd(tr("Volume %1%").arg(m_controls->volumeSlider()->value())); });
    add({Qt::Key_Down}, [this] { m_controls->volumeSlider()->setValue(m_controls->volumeSlider()->value() - 5); showOsd(tr("Volume %1%").arg(m_controls->volumeSlider()->value())); });
    add({Qt::Key_M}, [this] { m_controls->muteButton->click(); });
    add({Qt::Key_F, Qt::Key_F11}, [this] {
        if (m_deskActive) m_desk->view()->toggleFly(); else setFullscreen(!m_fullscreen);
    });
    add({Qt::Key_Escape}, [this] {
        if (m_deskActive) {
            const auto ph = m_desk->view()->phase();
            if (ph == DeskView::Phase::Desk) leaveDeskMode(); else m_desk->view()->flyOut();
        } else if (m_fullscreen) {
            setFullscreen(false);
        }
    });
    add({Qt::Key_T}, [this] { toggleDeskMode(); });
    add({Qt::Key_B}, [this] { setBypass(!m_video->bypass()); showOsd(m_video->bypass() ? tr("CRT effects off") : tr("CRT effects on")); });
    add({Qt::Key_C}, [this] { setCompare(!m_video->compare()); showOsd(m_video->compare() ? tr("Before / after comparison") : tr("Comparison off")); });
    add({Qt::Key_BracketLeft}, [this] { cyclePreset(-1); });
    add({Qt::Key_BracketRight}, [this] { cyclePreset(1); });
    add({Qt::Key_Z}, [this] {
        setScaleMode(ScaleMode((int(m_video->scaleMode()) + 1) % 4));
        showOsd(scaleModeName(m_video->scaleMode()));
    });
    add({Qt::Key_A}, [this] { cycleAudio(); });
    add({Qt::Key_J}, [this] { cycleSubtitle(); });
    add({Qt::Key_V}, [this] { setSubtitlesWanted(!m_settings.subtitlesOn); });   // stays as set, from video to video
    // 2.11: delays, night mode, sleep timer, shuffle and repeat
    add({QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)}, [this] { setAudioDelay(m_player->audioDelay() + 50); });
    add({QKeySequence(Qt::CTRL | Qt::Key_Minus)}, [this] { setAudioDelay(m_player->audioDelay() - 50); });
    add({Qt::Key_H}, [this] { setSubtitleDelay(m_player->subtitleDelay() + 100); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_H)}, [this] { setSubtitleDelay(m_player->subtitleDelay() - 100); });
    add({Qt::Key_D}, [this] { setNightMode(!m_settings.nightMode); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_Z)}, [this] { cycleSleepTimer(); });
    add({QKeySequence(Qt::CTRL | Qt::Key_H)}, [this] { setShuffle(!m_settings.shuffle); });
    add({QKeySequence(Qt::CTRL | Qt::Key_R)}, [this] { setRepeatMode((m_settings.repeatMode + 1) % 3); });
    add({Qt::Key_S}, [this] { takeScreenshot(m_settings.screenshotFiltered); });
    add({QKeySequence(Qt::SHIFT | Qt::Key_S)}, [this] { takeScreenshot(false); });
    add({QKeySequence(Qt::CTRL | Qt::Key_S)}, [this] { takeScreenshot(true); });
    // (Cable TV: Page Up is channel up, as on a remote; next / previous are channel up / down.)
    add({Qt::Key_PageDown}, [this] { if (tvOn()) m_tv->channelStep(-1); else nextItem(1); });
    add({Qt::Key_PageUp}, [this] { if (tvOn()) m_tv->channelStep(+1); else nextItem(-1); });
    add({Qt::Key_MediaNext, Qt::Key_N}, [this] { nextItem(1); });
    add({Qt::Key_MediaPrevious, Qt::Key_P}, [this] { nextItem(-1); });
    add({QKeySequence::Open}, [this] { openDialog(); });
    add({QKeySequence(Qt::CTRL | Qt::Key_L)}, [this] { promptOpenLink(); });   // a video page, or a stream's address (2.17)
    add({QKeySequence::Paste}, [this] { pasteLink(); });                      // the address on the clipboard
    add({Qt::Key_E}, [this] { m_settingsDock->setVisible(!m_settingsDock->isVisible()); });
    add({Qt::Key_L}, [this] { m_playlistDock->setVisible(!m_playlistDock->isVisible()); });
    add({QKeySequence(Qt::CTRL | Qt::Key_J)}, [this] { showJellyfin(!m_jfDock->isVisible()); });
    add({Qt::Key_I}, [this] { setInfoOverlay(!m_info->isVisible()); });
    add({QKeySequence::Quit, QKeySequence(Qt::CTRL | Qt::Key_Q)}, [] { qApp->quit(); });   // also from the desk window
}

void MainWindow::setInfoOverlay(bool on)
{
    m_info->setVisible(on);
    if (on) { updateInfoOverlay(); m_infoTimer.start(); } else m_infoTimer.stop();
    layoutOverlays();
}

void MainWindow::restoreSettings()
{
    m_settings.load();
    m_player->setHardwareDecoding(m_settings.hardwareDecoding);
    m_player->setVolume(m_settings.volume);
    m_player->setMuted(m_settings.muted);
    {
        QSignalBlocker b(m_controls->volumeSlider());
        m_controls->volumeSlider()->setValue(qRound(m_settings.volume * 100));
    }
    m_controls->setMutedIcon(m_settings.muted);
    m_playbackPanel->setHardwareDecoding(m_settings.hardwareDecoding);
    m_playbackPanel->setScreenshotFiltered(m_settings.screenshotFiltered);
    m_playbackPanel->setScreenshotDir(m_settings.screenshotDir);
    setScaleMode(m_settings.scaleMode);
    setCrop(m_settings.crop);
    setAspectOverride(m_settings.aspectOverride);

    // m_settings desk fields are applied when desk mode is first opened.
    const CrtPreset* p = m_presets.find(m_settings.presetName);
    if (!p) p = m_presets.find("Consumer Television");
    m_presetName = p->name;
    applyParams(m_settings.hasParams ? m_settings.params : p->params, false);
    setBypass(m_settings.bypass);
    m_video->setSplitFraction(m_settings.split);
    setCompare(m_settings.compare);

    m_playlist->setItems(m_settings.playlist);
    m_playlist->setCurrentIndex(m_settings.playlistIndex < m_playlist->count() ? m_settings.playlistIndex : -1);
    applyEverydaySettings();
    if (!m_settings.geometry.isEmpty()) restoreGeometry(m_settings.geometry);
    if (!m_settings.windowState.isEmpty()) restoreState(m_settings.windowState);
    m_settingsDock->setVisible(m_settings.settingsVisible);
    m_playlistDock->setVisible(m_settings.playlistVisible);
    m_jfDock->setVisible(m_settings.jellyfinVisible);
    refreshPresetUi();
}

void MainWindow::saveSettings()
{
    if (m_sleep) m_sleep->setActive(false);   // never leave an inhibition behind
    storeDeskPose();
    rememberPosition();
    jellyfinStopCurrent(true);
    m_settings.jellyfinVisible = m_fullscreen ? m_settings.jellyfinVisible : m_jfDock->isVisible();
    m_settings.volume = m_sleepFading ? volume01() : m_player->volume();   // (never the faded-out volume)
    m_settings.muted = m_player->isMuted();
    m_settings.scaleMode = m_video->scaleMode();
    m_settings.crop = m_video->crop();
    m_settings.aspectOverride = m_video->aspectOverride();
    m_settings.presetName = m_presetName;
    m_settings.params = m_params;
    m_settings.hasParams = true;
    m_settings.bypass = m_video->bypass();
    m_settings.compare = m_video->compare();
    m_settings.split = m_video->splitFraction();
    m_settings.playlist = m_playlist->items();
    m_settings.playlistIndex = m_playlist->currentIndex();
    if (m_fullscreen) {
        m_settings.settingsVisible = m_dockSettingsBeforeFs;
        m_settings.playlistVisible = m_dockPlaylistBeforeFs;
    } else {
        m_settings.geometry = saveGeometry();
        m_settings.windowState = saveState();
        m_settings.settingsVisible = m_settingsDock->isVisible();
        m_settings.playlistVisible = m_playlistDock->isVisible();
    }
    m_settings.save();
}

void MainWindow::closeEvent(QCloseEvent* e)
{
    saveSettings();
    e->accept();
}

// ---- files & playlist ------------------------------------------------------

void MainWindow::openFiles(const QStringList& pathsIn, bool playNow)
{
    if (pathsIn.isEmpty()) return;
    QStringList paths;   // playlist files (.m3u, .m3u8) stand for what they list
    for (const QString& p : pathsIn) {
        // (a playlist *file*: an address ending in .m3u8 is a stream, which the player plays as it is)
        if (isPlaylistFile(p) && (!p.contains(QStringLiteral("://")) || p.startsWith(QLatin1String("file:")))) paths += readPlaylistFile(p);
        else paths << p;
    }
    if (paths.isEmpty()) { showOsd(tr("Nothing to play in that playlist")); return; }
    const int first = m_playlist->addItems(paths);
    if (QFileInfo::exists(paths.first())) m_settings.lastDir = QFileInfo(paths.first()).absolutePath();
    if (playNow) playIndex(first);
    if (paths.size() > 1) showOsd(tr("Added %1 files to the playlist").arg(paths.size()));
}

void MainWindow::playIndex(int i)
{
    const QString path = m_playlist->at(i);
    if (path.isEmpty()) return;
    if (m_tv && m_tv->isOn()) m_tv->setOn(false);   // playing something of one's own turns the TV off
    playSource(path, i);
}

// Plays a file, an address or a "jellyfin:" reference. i: its place in the playlist, or -1
// when it isn't from the playlist (Cable TV).
void MainWindow::playSource(const QString& path, int i)
{
    const bool tv = tvOn();
    m_playLog.append(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName());
    if (m_playLog.size() > 300) m_playLog.removeFirst();
    m_lastSource = path;
    m_lastSourceIndex = i;
    rememberPosition();
    jellyfinStopCurrent(false);
    // Whatever was asked of yt-dlp for the video before this one is not waited for any more.
    ++m_webRequest;
    m_online->cancel();
    m_webPage.clear();
    m_webAsking.clear();
    m_webSite.clear();
    m_webWhat.clear();
    m_webLive = false;
    m_webHeaders.clear();
    m_webSubs.clear();
    m_webChapters.clear();
    m_webFallbackNote.clear();
    m_webPending.clear();
    if (!m_webInner) m_webDepth = 0;
    m_webInner = false;
    if (!m_webRetryNext) m_webRetried = false;
    m_webRetryNext = false;
    setHint(QString());
    // A server conversion that was never reported (Cable TV doesn't report) ends here.
    if (m_jfTranscoding && !m_jfPlaySession.isEmpty()) { m_jf->stopTranscode(m_jfPlaySession); m_jfPlaySession.clear(); }
    m_extSubs.clear();
    m_extSubLabel.clear();
    m_jfOwnSubs.clear();
    m_player->setExternalSubtitle(QString());
    setLoop(-1, -1);
    if (m_player->subtitleDelay() != 0) setSubtitleDelay(0, false);   // (a subtitle delay belongs to one video)
    // What carries over: subtitles on or off and the languages last picked.
    m_player->setPreferredLanguages(m_settings.audioLang, m_settings.subtitleLang);
    m_player->setSubtitlesWanted(m_settings.subtitlesOn);
    QString uri = path;
    qint64 start = 0;
    if (path.startsWith(QLatin1String("jellyfin:"))) {
        // "jellyfin:///<itemId>#<title>": resolved to an authenticated stream only now, so
        // the playlist never holds a token or even the server address.
        const QString id = QUrl(path).path().section('/', -1);
        if (!m_jf->isSignedIn() && tv) { m_pendingStartNs = -1; m_tv->programFailed(tr("Not signed in to Jellyfin")); return; }
        if (!m_jf->isSignedIn()) {
            showOsd(tr("Sign in to Jellyfin to play this item"), 3000);
            showJellyfin(true);
            m_pendingStartNs = -1;
            return;
        }
        m_mediaTitle = QUrl::fromPercentEncoding(path.section('#', 1).toUtf8());
        m_currentLocal.clear();
        m_jfTranscoding = false;
        m_jfPlaySession.clear();
        m_jfReasons.clear();
        m_jfItemId = id;
        m_jfStarted = false;
        const QByteArray auth = m_jf->authorizationHeader();
        m_player->setHttpHeaders({{"Authorization", auth}, {"X-Emby-Authorization", auth}});
        if (m_pendingStartNs >= 0) start = m_pendingStartNs;
        else if (const JfItem* it = m_jf->cachedItem(id); it && !it->played) start = it->positionTicks * 100;
        const bool force = m_jfForceNext;
        m_jfForceNext = false;
        if (!force) m_jfRetried = false;
        // Ask the server how: the original file when this computer can decode it (and it is
        // within the quality limit), otherwise converted on the server.
        JellyfinClient::Capabilities caps;
        Player::localFormats(&caps.containers, &caps.videoCodecs, &caps.audioCodecs);
        caps.h264 = caps.videoCodecs.contains(QStringLiteral("h264"));
        const int req = ++m_jfRequest;
        m_pendingStartNs = -1;
        if (i >= 0) m_playlist->setCurrentIndex(i);
        const QString shown = m_mediaTitle;
        m_jf->requestPlayback(id, qint64(m_settings.jfMaxBitrateMbps) * 1000000, force, caps,
                              [this, req, id, i, start, shown, force](const JfPlayback& pb) {
            if (req != m_jfRequest || id != m_jfItemId) {   // something else was opened meanwhile
                if (pb.transcode) m_jf->stopTranscode(pb.playSessionId);
                return;
            }
            QString note;
            if (!pb.error.isEmpty()) {
                if (force) {   // a conversion was needed and the server can't make one
                    m_lastError = tr("Cannot play this item: %1").arg(pb.error);
                    showOsd(m_lastError, 6000);
                    return;
                }
                // Older servers, or PlaybackInfo refused: the original file, as before.
                qWarning() << "Jellyfin PlaybackInfo failed, playing the original file:" << pb.error;
                m_jf->setPlayMethod(QStringLiteral("DirectPlay"), QString(), id);
                m_jf->fetchSubtitles(id);
            } else {
                m_jfTranscoding = pb.transcode;
                m_jfPlaySession = pb.playSessionId;
                m_jfReasons = pb.transcodeReasons;
                m_jf->setPlayMethod(pb.transcode ? QStringLiteral("Transcode") : QStringLiteral("DirectPlay"),
                                    pb.playSessionId, pb.mediaSourceId);
                for (const auto& s : pb.subtitles) m_extSubs.append({s.first, s.second.toString(QUrl::FullyEncoded)});
                m_jfOwnSubs = pb.ownSubtitles;
                rebuildSubtitleMenu();
                if (pb.transcode) note = tr("converted by the server");
            }
            m_jfStartNs = start;
            openResolved(i, pb.url.toString(QUrl::FullyEncoded), start, shown, note);
        });
        m_lastError.clear();
        m_emptyHint->hide();
        updateTitle();
        return;
    } else if (!tv && (OnlineResolver::isReference(path) || OnlineResolver::isPage(path))) {
        // A page with a video on it (2.17, Online.cpp): yt-dlp says where the video is.
        m_player->setHttpHeaders({});
        m_jfItemId.clear();
        m_jfTranscoding = false;
        m_currentLocal.clear();
        m_webStartNs = m_pendingStartNs;
        m_pendingStartNs = -1;
        playWeb(path, i);
        return;
    } else {
        m_player->setHttpHeaders({});
        m_jfItemId.clear();
        m_jfTranscoding = false;
        m_mediaTitle.clear();
        const QFileInfo fi(path);
        m_currentLocal = fi.exists() ? fi.absoluteFilePath() : QString();
        if (!m_currentLocal.isEmpty()) {
            if (m_pendingStartNs >= 0) start = m_pendingStartNs;   // (Cable TV: where the broadcast is)
            else start = m_resume->position(m_currentLocal) * 1000000;   // where it was left
            // Subtitle files next to the video ("Movie.srt", "Movie.en.srt", ...): the first loads.
            for (const QString& sub : findSidecarSubtitles(m_currentLocal))
                m_extSubs.append({QFileInfo(sub).fileName(), QUrl::fromLocalFile(sub).toString(QUrl::FullyEncoded)});
            if (!m_extSubs.isEmpty()) {
                QFile sf(QUrl(m_extSubs.first().second).toLocalFile());
                if (sf.open(QIODevice::ReadOnly) && sf.size() < 50 * 1024 * 1024) {
                    m_player->setExternalSubtitle(stageSubtitle(sf.readAll(), sf.fileName()));
                    m_extSubLabel = m_extSubs.first().first;
                }
            }
        }
    }
    m_pendingStartNs = -1;
    if (tv && !m_tvTitle.isEmpty()) m_mediaTitle = m_tvTitle;
    openResolved(i, uri, start, m_mediaTitle.isEmpty() ? QFileInfo(path).fileName() : m_mediaTitle, QString());
}

void MainWindow::openResolved(int i, const QString& uri, qint64 start, const QString& shown, const QString& note, const QList<WebStream>& streams)
{
    if (i >= 0) m_playlist->setCurrentIndex(i);
    m_lastError.clear();
    m_emptyHint->hide();
    m_video->nvidiaForgive();   // (what NVIDIA's helper could not do for the last video it may do for this one)
    const bool tv = tvOn();
    if (tv) {
        // Changing channel: static until the picture arrives. One programme following
        // another: the last frame stays until the next one's first.
        if (m_tvBurst) m_video->channelChange();
    } else if (m_params.channelStatic && m_video->hasFrame()) {
        m_video->channelChange();   // static over the old picture until the new file's first frame
    } else {
        m_video->clearFrame();
        if (m_desk) m_desk->view()->clearFrame();
    }
    if (!streams.isEmpty()) m_player->openWeb(streams, true, start);   // (a web video: its picture and its sound may be two addresses)
    else m_player->open(uri, true, start);
    updateTitle();
    if (tv) return;   // (the channel's own banner says what is on)
    QString msg = start > 0 ? tr("%1 — resuming at %2 (Home: start over)").arg(shown, formatTime(start / 1000000)) : shown;
    if (!note.isEmpty()) msg += QStringLiteral(" · ") + note;
    showOsd(msg);
}

// ---- Cable TV ---------------------------------------------------------------------------

bool MainWindow::tvOn() const { return m_tv && m_tv->isOn(); }

void MainWindow::setTvMode(bool on)
{
    if (on == tvOn()) return;
    if (on) {
        if (m_tv->channels().isEmpty()) {
            showOsd(tr("No TV channels yet: add a folder in the TV panel"), 4000);
            if (m_deskActive) leaveDeskMode();
            m_settingsDock->show();
            m_tabs->setCurrentWidget(m_tvPanel);
            return;
        }
        rememberPosition();          // what was playing keeps its place
        jellyfinStopCurrent(false);
        setLoop(-1, -1);
    }
    m_tv->setOn(on);
    if (!on) {
        // The set is off: the programme stops (and one still opening is dropped). The playlist
        // and what was open before are as they were left.
        ++m_jfRequest;
        m_pendingStartNs = -1;
        m_tvLoaded = false;
        if (m_jfTranscoding && !m_jfPlaySession.isEmpty()) { m_jf->stopTranscode(m_jfPlaySession); m_jfPlaySession.clear(); }
        m_jfItemId.clear();
        m_player->close();
        m_mediaTitle.clear();
        m_tvTitle.clear();
        m_video->setSnow(false);
        m_video->releaseStatic();
        updateTitle();
        if (m_desk) m_desk->view()->update();
        showOsd(tr("TV off"));
    }
}

void MainWindow::tvPlay(const QString& source, qint64 offsetNs, const QString& title, bool burst)
{
    m_tvBurst = burst || !m_video->hasFrame() || m_video->snow();
    m_video->setSnow(false);
    m_tvTitle = title;
    m_tvLoaded = false;
    m_pendingStartNs = std::max<qint64>(0, offsetNs);
    playSource(source, -1);
}

void MainWindow::tvSnow()
{
    // Nothing is on: whatever was playing stops, and so does anything still opening (a file
    // that loads a moment after the channel was left, a Jellyfin stream being asked for).
    ++m_jfRequest;
    m_pendingStartNs = -1;
    m_tvLoaded = false;
    if (m_jfTranscoding && !m_jfPlaySession.isEmpty()) { m_jf->stopTranscode(m_jfPlaySession); m_jfPlaySession.clear(); }
    m_jfItemId.clear();
    m_tvTitle.clear();
    m_mediaTitle.clear();
    m_video->setSnow(true);   // static until something is tuned in
    m_player->close();
    updateTitle();
    if (m_desk) m_desk->view()->update();
}


QString MainWindow::jellyfinPlayDescription() const
{
    if (m_jfItemId.isEmpty()) return QString();
    if (!m_jfTranscoding) return tr("original file");
    QStringList why;
    for (const QString& r : m_jfReasons) {
        if (r.contains("Bitrate")) why << tr("over the quality limit");
        else if (r.startsWith("VideoCodec") || r.startsWith("VideoProfile") || r.startsWith("VideoLevel") || r.startsWith("VideoBitDepth")) why << tr("video format");
        else if (r.startsWith("AudioCodec") || r.startsWith("AudioChannels") || r.startsWith("AudioProfile")) why << tr("audio format");
        else if (r.startsWith("Container")) why << tr("file type");
        else if (r.startsWith("Subtitle")) why << tr("subtitles");
        else why << r;
    }
    if (m_jfRetried) why.prepend(tr("the original didn't play here"));
    why.removeDuplicates();
    return why.isEmpty() ? tr("converted by the server") : tr("converted by the server: %1").arg(why.join(", "));
}

void MainWindow::jellyfinStopCurrent(bool waitForServer)
{
    if (m_jfItemId.isEmpty() || !m_jfStarted) return;
    QNetworkReply* r = m_jf->reportStopped(m_jfItemId, m_player->position());   // (also ends a conversion)
    m_jfStarted = false;
    m_jfPlaySession.clear();
    m_jfTimer.stop();
    if (waitForServer && r && !r->isFinished()) {
        // On quit, give the final position a moment to reach the server.
        QEventLoop loop;
        QTimer::singleShot(1500, &loop, &QEventLoop::quit);
        connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
    }
}

void MainWindow::vcr(const QString& text, double seconds)
{
    if (m_params.vcrOsd) m_video->setOsdText(text, seconds);
}

void MainWindow::showJellyfin(bool on)
{
    if (m_deskActive && on) leaveDeskMode();
    m_jfDock->setVisible(on);
    if (on) m_jfDock->raise();
}

void MainWindow::playJellyfin(const JfItem& item, bool fromStart)
{
    const QString ref = QStringLiteral("jellyfin:///%1#%2").arg(item.id, QString::fromUtf8(QUrl::toPercentEncoding(item.displayName())));
    const int idx = m_playlist->addItems({ref});
    m_pendingStartNs = fromStart ? 0 : (item.played ? 0 : item.positionTicks * 100);
    playIndex(idx);
}

void MainWindow::playJellyfinConverted(const JfItem& item)
{
    m_jfForceNext = true;
    playJellyfin(item, false);
}

void MainWindow::enqueueJellyfin(const JfItem& item)
{
    const QString ref = QStringLiteral("jellyfin:///%1#%2").arg(item.id, QString::fromUtf8(QUrl::toPercentEncoding(item.displayName())));
    m_playlist->addItems({ref});
    showOsd(tr("Added to playlist: %1").arg(item.displayName()));
}

void MainWindow::nextItem(int dir)
{
    if (tvOn()) { m_tv->channelStep(dir); return; }   // Cable TV: channel up / down
    const int i = pickNext(dir);
    if (i >= 0) { playIndex(i); return; }
    if (dir > 0 && m_settings.autoNext && !m_settings.shuffle && !m_currentLocal.isEmpty()) {   // on into the folder
        const QString f = nextInFolder(m_currentLocal);
        if (!f.isEmpty()) {
            int idx = m_playlist->items().indexOf(f);
            if (idx < 0) idx = m_playlist->addItems({f});
            playIndex(idx);
            return;
        }
    }
    showOsd(dir > 0 ? tr("End of playlist") : tr("Start of playlist"));
}

void MainWindow::openDialog(QWidget* parent)
{
    const QStringList files = QFileDialog::getOpenFileNames(parent ? parent : this, tr("Open video"), m_settings.lastDir, tr(kVideoFilter));
    if (!files.isEmpty()) openFiles(files, true);
}

void MainWindow::addDialog()
{
    const QStringList files = QFileDialog::getOpenFileNames(this, tr("Add to playlist"), m_settings.lastDir, tr(kVideoFilter));
    if (!files.isEmpty()) openFiles(files, m_player->currentUri().isEmpty());
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->mimeData()->hasUrls() || !addressesIn(e->mimeData()->text()).isEmpty()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e)
{
    QStringList files;
    for (const QUrl& u : e->mimeData()->urls()) {
        if (u.isLocalFile()) {
            const QFileInfo fi(u.toLocalFile());
            if (fi.isDir()) {
                for (const QFileInfo& f : QDir(fi.absoluteFilePath()).entryInfoList(QDir::Files, QDir::Name)) files << f.absoluteFilePath();
            } else {
                files << fi.absoluteFilePath();
            }
        } else {
            files << u.toString();
        }
    }
    // (a link dragged out of a browser's page, or a piece of text with an address in it)
    if (files.isEmpty()) files = addressesIn(e->mimeData()->text());
    if (!files.isEmpty()) { openFiles(files, true); e->acceptProposedAction(); }
}

void MainWindow::updateTitle()
{
    const QString p = m_player->currentPath();
    const QString name = !m_mediaTitle.isEmpty() ? m_mediaTitle : QFileInfo(p).fileName();
    setWindowTitle(p.isEmpty() ? tr("CRT Player") : name + tr(" — CRT Player"));
    if (m_desk) m_desk->view()->setMarqueeText(DeskView::marqueeTitle(mediaTitle()));   // the arcade cabinet's marquee
}

// ---- presets & CRT parameters ----------------------------------------------

void MainWindow::applyParams(const CrtParams& p, bool fromPreset)
{
    Q_UNUSED(fromPreset);
    m_params = p;
    m_video->setParams(p);
    applyLookSound();
    if (m_crtPanel->params() != p) m_crtPanel->setParams(p);
    refreshPresetUi();
}

void MainWindow::showSettingsTab(const QString& name)
{
    m_settingsDock->show();
    for (int i = 0; i < m_tabs->count(); ++i)
        if (m_tabs->tabText(i).remove('&').compare(name, Qt::CaseInsensitive) == 0 ||
            m_tabs->widget(i)->objectName().compare(name, Qt::CaseInsensitive) == 0) m_tabs->setCurrentIndex(i);
}

void MainWindow::setLookSound(bool on)
{
    m_settings.lookSound = on;
    m_playbackPanel->setLookSound(on);
    applyLookSound();
}

void MainWindow::setSoundLevels(double noiseVolume, double effectStrength)
{
    m_settings.noiseVolume = std::clamp(noiseVolume, 0.0, 2.0);
    m_settings.effectStrength = std::clamp(effectStrength, 0.0, 1.0);
    m_playbackPanel->setNoiseVolume(m_settings.noiseVolume);
    m_playbackPanel->setEffectStrength(m_settings.effectStrength);
    applyLookSound();
}

void MainWindow::applyLookSound()
{
    // The look's tape and speaker sound (unless switched off in Playback).
    TapeParams t;
    if (m_settings.lookSound) {
        // Noise volume: how loud the added hiss and crackle are. Effect strength: how strongly
        // the rest changes the sound.
        const float k = float(m_settings.effectStrength);
        t.hiss = m_params.tapeHiss; t.crackle = m_params.filmCrackle;
        t.noiseGain = float(m_settings.noiseVolume);
        t.wow = m_params.wowFlutter * k; t.saturation = m_params.tapeSaturation * k;
        t.tone = m_params.tapeTone * k; t.speaker = m_params.tvSpeaker * k;
        t.dropouts = m_params.tapeHiss > 0.f ? m_params.vhsDropouts * k : 0.f;   // tape dropouts dip the sound too
        t.crush = m_params.pcmCrush * k;
    }
    if (m_settings.nightMode) t.night = 1.f;   // (a setting of its own, not part of a look)
    m_lastTape = t;
    m_player->setTapeParams(t);
}

void MainWindow::refreshPresetUi()
{
    const CrtPreset* cur = m_presets.find(m_presetName);
    const bool modified = cur && cur->params != m_params;
    m_crtPanel->setPresetList(m_presets.builtinNames(), m_presets.userNames());
    m_crtPanel->setCurrentPreset(m_presetName, modified, cur && cur->builtin);
    QComboBox* combo = m_controls->presetCombo();
    QSignalBlocker b(combo);
    combo->clear();
    combo->addItems(m_presets.builtinNames());
    if (!m_presets.userNames().isEmpty()) {
        combo->insertSeparator(combo->count());
        combo->addItems(m_presets.userNames());
    }
    combo->setCurrentIndex(combo->findText(m_presetName));
    combo->setToolTip(modified ? tr("%1 (modified)").arg(m_presetName) : m_presetName);
    if (m_desk) {
        QComboBox* dc = m_desk->bar()->presetCombo();
        QSignalBlocker db(dc);
        dc->clear();
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemText(i).isEmpty()) dc->insertSeparator(dc->count());
            else dc->addItem(combo->itemText(i));
        }
        dc->setCurrentIndex(dc->findText(m_presetName));
    }
}

QComboBox* MainWindow::lookSelector() const
{
    return (m_deskActive && m_desk) ? m_desk->bar()->presetCombo() : m_controls->presetCombo();
}

bool MainWindow::selectPreset(const QString& name)
{
    const CrtPreset* p = m_presets.find(name);
    if (!p) return false;
    m_presetName = p->name;
    applyParams(p->params, true);
    showOsd(p->name);
    return true;
}

void MainWindow::cyclePreset(int dir)
{
    QStringList all = m_presets.builtinNames() + m_presets.userNames();
    int i = all.indexOf(m_presetName);
    i = (i + dir + all.size()) % all.size();
    selectPreset(all.at(i));
}

void MainWindow::setParams(const CrtParams& p) { applyParams(p, false); }

void MainWindow::savePresetAs()
{
    bool ok = false;
    const QString suggestion = m_presets.uniqueName(m_presetName.isEmpty() ? tr("My preset") : m_presetName);
    const QString name = QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"), QLineEdit::Normal, suggestion, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (const CrtPreset* existing = m_presets.find(name)) {
        if (existing->builtin) {
            QMessageBox::warning(this, tr("Save preset"), tr("\"%1\" is a built-in preset. Choose another name.").arg(name));
            return;
        }
        if (QMessageBox::question(this, tr("Save preset"), tr("Replace the preset \"%1\"?").arg(existing->name)) != QMessageBox::Yes) return;
    }
    QString err;
    if (!m_presets.saveUser(name, m_params, &err)) { QMessageBox::warning(this, tr("Save preset"), err); return; }
    m_presetName = m_presets.find(name)->name;
    refreshPresetUi();
    showOsd(tr("Saved preset “%1”").arg(m_presetName));
}

void MainWindow::savePreset()
{
    QString err;
    if (!m_presets.saveUser(m_presetName, m_params, &err)) { QMessageBox::warning(this, tr("Save preset"), err); return; }
    refreshPresetUi();
    showOsd(tr("Saved preset “%1”").arg(m_presetName));
}

void MainWindow::renamePreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename preset"), tr("New name:"), QLineEdit::Normal, m_presetName, &ok).trimmed();
    if (!ok || name.isEmpty() || name == m_presetName) return;
    const bool modified = m_presets.find(m_presetName) && m_presets.find(m_presetName)->params != m_params;
    const CrtParams current = m_params;
    QString err;
    if (!m_presets.rename(m_presetName, name, &err)) { QMessageBox::warning(this, tr("Rename preset"), err); return; }
    m_presetName = name;
    applyParams(modified ? current : m_presets.find(name)->params, false);
    showOsd(tr("Renamed to “%1”").arg(name));
}

void MainWindow::deletePreset()
{
    if (QMessageBox::question(this, tr("Delete preset"), tr("Delete the preset \"%1\"?").arg(m_presetName)) != QMessageBox::Yes) return;
    QString err;
    if (!m_presets.remove(m_presetName, &err)) { QMessageBox::warning(this, tr("Delete preset"), err); return; }
    const QString removed = m_presetName;
    m_presetName = m_presets.builtinNames().first();
    refreshPresetUi();
    showOsd(tr("Deleted “%1”; current settings kept").arg(removed));
}

void MainWindow::importPreset()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Import CRT preset"), m_settings.lastDir, tr("CRT Player presets (*.json);;All files (*)"));
    if (path.isEmpty()) return;
    QString name, err;
    if (!m_presets.importFile(path, &name, &err)) { QMessageBox::warning(this, tr("Import preset"), err); return; }
    selectPreset(name);
    showOsd(tr("Imported “%1”").arg(name));
}

void MainWindow::exportPreset()
{
    QString fname = m_presetName;
    fname.replace(QRegularExpression("[^A-Za-z0-9 _-]"), "_");
    const QString path = QFileDialog::getSaveFileName(this, tr("Export CRT preset"),
                                                      QDir(m_settings.lastDir).filePath(fname + ".json"), tr("CRT Player presets (*.json)"));
    if (path.isEmpty()) return;
    QString err;
    // Exports the settings as currently shown (including unsaved tweaks) under the preset's name.
    if (!m_presets.exportFile(m_presetName, m_params, path, &err)) { QMessageBox::warning(this, tr("Export preset"), err); return; }
    showOsd(tr("Exported to %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::setBypass(bool on)
{
    m_video->setBypass(on);
    QSignalBlocker b1(m_controls->crtButton), b2(m_crtPanel->bypassButton());
    m_controls->crtButton->setChecked(!on);
    m_crtPanel->bypassButton()->setChecked(on);
}

void MainWindow::setCompare(bool on)
{
    if (on && m_video->bypass()) setBypass(false);
    m_video->setCompare(on);
    QSignalBlocker b1(m_controls->compareButton), b2(m_crtPanel->compareButton());
    m_controls->compareButton->setChecked(on);
    m_crtPanel->compareButton()->setChecked(on);
}

void MainWindow::setScaleMode(ScaleMode m)
{
    m_video->setScaleMode(m);
    m_displayPanel->setScaleMode(m);
    for (QAction* a : m_aspectGroup->actions()) a->setChecked(a->data().toInt() == int(m));
    updateSourceInfo();
}

void MainWindow::setCrop(const CropFractions& c)
{
    m_video->setCrop(c);
    m_displayPanel->setCrop(m_video->crop());
    updateSourceInfo();
}

void MainWindow::setAspectOverride(double a)
{
    m_video->setAspectOverride(a);
    m_displayPanel->setAspectOverride(a);
}

// ---- tracks ----------------------------------------------------------------

void MainWindow::rebuildAudioMenu()
{
    m_audioMenu->clear();
    const auto tracks = m_player->audioTracks();
    const int cur = m_player->currentAudioTrack();
    if (tracks.isEmpty()) { m_audioMenu->addAction(tr("No audio tracks"))->setEnabled(false); return; }
    auto* g = new QActionGroup(m_audioMenu);
    for (const TrackInfo& t : tracks) {
        QAction* a = m_audioMenu->addAction(t.label);
        a->setCheckable(true);
        a->setChecked(t.index == cur);
        g->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { chooseAudioTrack(t.index); });
    }
}

void MainWindow::rebuildSubtitleMenu()
{
    m_subMenu->clear();
    const auto tracks = m_player->subtitleTracks();
    const int cur = m_player->currentSubtitleTrack();
    auto* g = new QActionGroup(m_subMenu);
    QAction* off = m_subMenu->addAction(tr("Off"));
    off->setCheckable(true);
    off->setChecked(cur < 0);
    g->addAction(off);
    connect(off, &QAction::triggered, this, [this] { chooseSubtitleTrack(-1); });
    if (tracks.isEmpty()) m_subMenu->addAction(tr("No subtitle tracks in this file"))->setEnabled(false);
    for (const TrackInfo& t : tracks) {
        QAction* a = m_subMenu->addAction(t.label);
        a->setCheckable(true);
        a->setChecked(t.index == cur);
        g->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { chooseSubtitleTrack(t.index); });
    }
    // External subtitle files: next to a local video, or offered by the Jellyfin server.
    m_subMenu->addSection(tr("Subtitle files"));
    for (const auto& ext : m_extSubs) {
        QAction* a = m_subMenu->addAction(ext.first);
        a->setCheckable(true);
        a->setChecked(ext.first == m_extSubLabel);
        connect(a, &QAction::triggered, this, [this, ext] { setExternalSubtitleFile(ext.second, ext.first); });
    }
    if (!m_extSubLabel.isEmpty())
        m_subMenu->addAction(tr("No subtitle file"), this, [this] { setExternalSubtitleFile(QString(), QString()); });
    if (!m_currentLocal.isEmpty())
        m_subMenu->addAction(tr("Load subtitle file…"), this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Load subtitle file"), QFileInfo(m_currentLocal).absolutePath(),
                                                           tr("Subtitles (*.srt *.ass *.ssa *.vtt *.sub *.txt);;All files (*)"));
            if (!f.isEmpty()) {
                m_extSubs.append({QFileInfo(f).fileName(), QUrl::fromLocalFile(f).toString(QUrl::FullyEncoded)});
                setExternalSubtitleFile(m_extSubs.last().second, m_extSubs.last().first);
            }
        });
}

QStringList MainWindow::findSidecarSubtitles(const QString& videoPath) const
{
    const QFileInfo fi(videoPath);
    const QString base = fi.completeBaseName();
    static const QStringList exts = {"srt", "ass", "ssa", "vtt", "sub"};
    QStringList exact, prefixed;
    const QFileInfoList files = QDir(fi.absolutePath()).entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo& f : files) {
        if (!exts.contains(f.suffix().toLower())) continue;
        if (f.completeBaseName() == base) exact << f.absoluteFilePath();
        else if (f.fileName().startsWith(base + '.')) prefixed << f.absoluteFilePath();   // Movie.en.srt
    }
    return exact + prefixed;
}

QString MainWindow::stageSubtitle(const QByteArray& data, const QString& nameHint)
{
    // External subtitles are played from a local copy. SRT files have no header, and
    // GStreamer's type detection cannot recognise very short ones (a few cues), so short
    // files are padded with trailing blank lines, which subtitle formats ignore.
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)).filePath("subtitles");
    QDir().mkpath(dir);
    QString ext = QFileInfo(nameHint).suffix().toLower();
    if (ext.isEmpty() || ext.size() > 4) ext = "srt";
    const QString name = QString::fromLatin1(QCryptographicHash::hash(nameHint.toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
    const QString path = QDir(dir).filePath(name + '.' + ext);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    f.write(data);
    if (data.size() < 4096) f.write(QByteArray(4096 - data.size(), '\n'));
    f.close();
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

void MainWindow::setExternalSubtitleFile(const QString& uri, const QString& label)
{
    if (uri.isEmpty()) { applyExternalSubtitle(QString(), QString()); return; }
    const QUrl u(uri);
    if (u.isLocalFile()) {
        QFile f(u.toLocalFile());
        if (!f.open(QIODevice::ReadOnly) || f.size() > 50 * 1024 * 1024) { showOsd(tr("Could not read the subtitle file")); return; }
        applyExternalSubtitle(stageSubtitle(f.readAll(), u.toLocalFile()), label);
        return;
    }
    showOsd(tr("Loading subtitles…"));
    if (m_webSubs.contains(uri)) {
        // A web video's subtitle file, fetched the way the site wants it (2.17). Automatic captions come with
        // every line repeated while the next one rolls in: tidied to plain lines first.
        const QPair<QString, bool> kind = m_webSubs.value(uri);
        const int request = m_webRequest;
        m_online->fetch(uri, m_webHeaders, [this, uri, label, kind, request](const QByteArray& data, const QString& err) {
            if (request != m_webRequest) return;   // (another video by now)
            if (!err.isEmpty() || data.trimmed().isEmpty()) { showOsd(tr("Could not load the subtitles: %1").arg(err.isEmpty() ? tr("the file is empty") : err), 4000); return; }
            const QByteArray lines = kind.second ? OnlineResolver::tidyCaptions(data) : data;
            applyExternalSubtitle(stageSubtitle(lines, uri + QLatin1Char('.') + (kind.first.isEmpty() ? QStringLiteral("vtt") : kind.first)), label);
        });
        return;
    }
    // Server subtitle (Jellyfin): download it with the session's authentication first.
    m_jf->fetchBytes(u, [this, uri, label](const QByteArray& data, const QString& err) {
        if (!err.isEmpty()) { showOsd(tr("Could not load the subtitles: %1").arg(err), 4000); return; }
        applyExternalSubtitle(stageSubtitle(data, uri), label);
    });
}

void MainWindow::applyExternalSubtitle(const QString& uri, const QString& label)
{
    const qint64 pos = m_player->position();
    const bool playing = m_player->isPlaying();
    const QString cur = m_player->currentUri();
    if (cur.isEmpty()) return;
    m_extSubLabel = label;
    if (!label.isEmpty()) {   // picking a subtitle file is asking for subtitles
        m_settings.subtitlesOn = true;
        m_player->setSubtitlesWanted(true);
        m_playbackPanel->setSubtitlesWanted(true);
    }
    // The player hands the file's lines over itself and the video goes on as it is. (Without that, where the
    // playback library reads the file, it does so only when a video opens: the video is opened again, here.)
    if (!m_player->changeExternalSubtitle(uri, true)) m_player->open(cur, playing, pos);
    showOsd(label.isEmpty() ? tr("Subtitle file off") : tr("Subtitles: %1").arg(label));
}

void MainWindow::showPluginNotice()
{
    const auto gaps = Player::missingRecommended();
    if (gaps.isEmpty()) return;
    QStringList items, pkgs;
    for (const auto& g : gaps) { items << QStringLiteral("  •  ") + g.what; if (!pkgs.contains(g.package)) pkgs << g.package; }
    const QString key = pkgs.join(',');
    if (m_settings.hiddenPluginNotice == key) return;   // the user said "don't show again" for exactly this
    const QString cmd = Distro::installCommand(pkgs);
    auto* box = new QMessageBox(QMessageBox::Information, tr("Some videos won't play yet"),
                                tr("CRT Player uses the GStreamer plugins installed on this system. These are missing, so "
                                   "the following won't work:\n\n%1\n\nOn %2, install them with:\n\n    %3%4")
                                    .arg(items.join('\n'), Distro::prettyName(), cmd,
                                         Distro::codecNote().isEmpty() ? QString() : "\n\n" + Distro::codecNote()),
                                QMessageBox::Close, this);
    box->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton* copy = box->addButton(tr("Copy command"), QMessageBox::ActionRole);
    copy->disconnect();   // keep the box open after copying
    connect(copy, &QPushButton::clicked, this, [cmd, copy] {
        QString c = cmd;
        QGuiApplication::clipboard()->setText(c.remove(QStringLiteral("   (then reboot)")));
        copy->setText(tr("Copied"));
    });
    auto* dontShow = new QCheckBox(tr("Don't show this again (until the list changes)"));
    box->setCheckBox(dontShow);
    connect(box, &QMessageBox::finished, this, [this, dontShow, key] { if (dontShow->isChecked()) m_settings.hiddenPluginNotice = key; });
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->show();
}

void MainWindow::theaterLook(bool entering)
{
    static const char* kLooks[] = {"Film Print (35mm)", "Worn Film (16mm)", "Drive-in Movie"};
    if (entering) {
        if (m_preTheaterValid || m_settings.theaterLook >= 3) return;
        m_preTheaterName = m_presetName;
        m_preTheaterParams = m_params;
        m_preTheaterValid = true;
        selectPreset(QString::fromLatin1(kLooks[m_settings.theaterLook]));
    } else if (m_preTheaterValid) {
        m_preTheaterValid = false;
        m_presetName = m_preTheaterName;
        applyParams(m_preTheaterParams, true);
    }
}

void MainWindow::setDeskScene(const DeskView::Scene& s)
{
    if (!m_desk) return;
    const int before = m_desk->view()->scene().scene;
    m_desk->view()->setScene(s);
    if (s.scene == 3 && before != 3) theaterLook(true);
    else if (s.scene != 3 && before == 3) theaterLook(false);
    updateTheater();
    m_desk->update();
}

void MainWindow::updateTheater()
{
    if (!m_desk) return;
    // Open while playing, and while paused part-way through; closed before the start and
    // at the end of the playlist.
    const bool playing = m_player->isPlaying();
    if (playing) m_theaterEnded = false;
    const bool open = playing || (m_player->hasMedia() && !m_theaterEnded && m_player->position() > 500000000LL);
    m_desk->view()->setTheaterState(open, playing);
}

void MainWindow::openSceneDialog()
{
    if (!m_desk) return;
    if (m_sceneDialog) { m_sceneDialog->show(); m_sceneDialog->raise(); m_sceneDialog->activateWindow(); return; }
    DeskView* view = m_desk->view();
    auto* dlg = new QDialog(m_desk);
    dlg->setWindowTitle(tr("Scene settings"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    auto* v = new QVBoxLayout(dlg);
    v->setContentsMargins(18, 16, 18, 16);
    v->setSpacing(12);

    // Scene cards with previews of the current view.
    auto* cards = new QHBoxLayout;
    auto* group = new QButtonGroup(dlg);
    const QStringList names = {tr("Your desktop"), tr("Desk"), tr("Wall-mounted TV"), tr("Movie theater"), tr("90s CG room")};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QToolButton(dlg);
        b->setCheckable(true);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIcon(QIcon(QPixmap::fromImage(view->scenePreview(i, QSize(144, 81)))));
        b->setIconSize(QSize(144, 81));
        b->setText(names[i]);
        b->setChecked(view->scene().scene == i);
        b->setEnabled(!(inGamescope() && i == 0));   // Game Mode has no desktop to show
        group->addButton(b, i);
        cards->addWidget(b);
    }
    v->addLayout(cards);

    auto combo = [dlg](const QStringList& items, int current) {
        auto* c = new QComboBox(dlg);
        c->addItems(items);
        c->setCurrentIndex(current);
        return c;
    };
    const DeskView::Scene cur = view->scene();
    auto* room = new QGroupBox(tr("Room"), dlg);
    auto* rf = new QFormLayout(room);
    auto* mood = combo({tr("Evening (lamp on)"), tr("Night (lamp dimmed)"), tr("Lights off")}, cur.mood);
    auto* fog = combo({tr("Off"), tr("Light haze"), tr("Thick fog")}, cur.fog);
    auto* strength = new QSlider(Qt::Horizontal, dlg);
    strength->setRange(25, 200);
    strength->setValue(int(cur.fogStrength * 100));
    auto* wood = combo({tr("Walnut"), tr("Oak"), tr("Cherry")}, cur.wood);
    auto* quality = combo({tr("Low (fastest)"), tr("Medium"), tr("High")}, cur.quality);
    quality->setToolTip(tr("Fog detail. Lower it if desk mode stutters with fog on."));
    rf->addRow(tr("Mood"), mood);
    rf->addRow(tr("Fog"), fog);
    rf->addRow(tr("Fog strength"), strength);
    rf->addRow(tr("Wood"), wood);
    rf->addRow(tr("Quality"), quality);
    v->addWidget(room);

    auto* wallBox = new QGroupBox(tr("Wall and pictures"), dlg);
    auto* wf = new QFormLayout(wallBox);
    auto* wallStyle = combo({tr("Warm white"), tr("Sage green"), tr("Navy"), tr("Charcoal"), tr("Pinstripe wallpaper"), tr("Damask wallpaper")}, cur.wallStyle);
    auto* layout = combo({tr("None"), tr("One on each side"), tr("Two on the left"), tr("Two on the right"), tr("Two on each side")}, cur.frameLayout);
    auto* frameStyle = combo({tr("Black"), tr("Wood"), tr("Gold")}, cur.frameStyle);
    wf->addRow(tr("Wall"), wallStyle);
    wf->addRow(tr("Pictures"), layout);
    wf->addRow(tr("Frames"), frameStyle);
    // Placement: how high the set hangs, and where the pictures go.
    auto slider = [dlg](int lo, int hi, double value, const QString& tip) {
        auto* sl = new QSlider(Qt::Horizontal, dlg);
        sl->setRange(lo, hi);
        sl->setValue(int(std::lround(value * 100)));
        sl->setToolTip(tip);
        return sl;
    };
    auto* tvHeight = slider(100, 300, cur.tvHeight, tr("How high the set hangs on the wall"));
    auto* picHeight = slider(-60, 60, cur.picHeight, tr("The pictures' height, relative to the middle of the screen"));
    auto* picSpacing = slider(50, 200, cur.picSpacing, tr("How far the pictures hang from the set"));
    auto* picSize = slider(60, 150, cur.picSize, tr("How big the pictures are"));
    wf->addRow(tr("TV height"), tvHeight);
    wf->addRow(tr("Picture height"), picHeight);
    wf->addRow(tr("Picture spacing"), picSpacing);
    wf->addRow(tr("Picture size"), picSize);
    auto paths = std::make_shared<QStringList>(cur.framePaths);
    while (paths->size() < 4) paths->append(QString());
    QLabel* names4[4];
    for (int i = 0; i < 4; ++i) {
        auto* row = new QHBoxLayout;
        names4[i] = new QLabel(dlg);
        names4[i]->setMinimumWidth(160);
        auto* choose = new QPushButton(tr("Choose…"), dlg);
        auto* clear = new QToolButton(dlg);
        clear->setText(QStringLiteral("✕"));
        clear->setToolTip(tr("Remove this picture"));
        row->addWidget(names4[i], 1);
        row->addWidget(choose);
        row->addWidget(clear);
        wf->addRow(tr("Picture %1").arg(i + 1), row);
        connect(choose, &QPushButton::clicked, dlg, [this, dlg, paths, i, names4] {
            const QString f = QFileDialog::getOpenFileName(dlg, tr("Choose a picture"), QFileInfo(paths->value(i)).absolutePath(),
                                                           tr("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif)"));
            if (f.isEmpty()) return;
            (*paths)[i] = f;
            names4[i]->setText(QFileInfo(f).fileName());
            DeskView::Scene s = m_desk->view()->scene(); s.framePaths = *paths; setDeskScene(s);
        });
        connect(clear, &QToolButton::clicked, dlg, [this, paths, i, names4] {
            (*paths)[i].clear();
            names4[i]->setText(tr("(empty)"));
            DeskView::Scene s = m_desk->view()->scene(); s.framePaths = *paths; setDeskScene(s);
        });
        names4[i]->setText(paths->value(i).isEmpty() ? tr("(empty)") : QFileInfo(paths->value(i)).fileName());
    }
    v->addWidget(wallBox);
    wallBox->setVisible(cur.scene == 2);

    auto* cgBox = new QGroupBox(tr("90s CG room"), dlg);
    auto* cf = new QFormLayout(cgBox);
    auto* cgPalette = combo({tr("Workstation (teal)"), tr("Sunset"), tr("Deep space")}, cur.cgPalette);
    auto* cgFloor = combo({tr("Mirror checkerboard"), tr("Neon grid")}, cur.cgFloor);
    auto* cgStand = combo({tr("Chrome pedestal"), tr("Marble plinth"), tr("Floating")}, cur.cgStand);
    auto check = [dlg](const QString& text, bool on, const QString& tip) {
        auto* c = new QCheckBox(text, dlg); c->setChecked(on); c->setToolTip(tip); return c;
    };
    auto* cgSet = combo({tr("Chrome and marble"), tr("Toybox (plastic)"), tr("Organic"), tr("Wooden mannequins"), tr("Mixed")}, cur.cgObjectSet);
    cgSet->setToolTip(tr("Which shapes surround the set: chrome and marble (the default), glossy plastic toys, "
                         "bumpy and glowing organic forms, walking wooden mannequins, or some of each"));
    auto* cgObjects = check(tr("Show the objects"), cur.cgObjects, tr("The shapes around the set"));
    auto* cgBackground = check(tr("Background crowd"), cur.cgBackground,
                               tr("Shapes floating in the distance and wooden figures walking behind the set, whichever objects you choose"));
    auto* cgBanding = check(tr("90s colour banding"), cur.cgBanding, tr("Smooth gradients step slightly, like the limited colour depth of the time"));
    auto* cgReveal = check(tr("Tile reveal when playback starts"), cur.cgReveal, tr("The picture resolves tile by tile, as if it were being ray traced"));
    auto* cgOrbit = check(tr("Demo-reel orbit while nothing plays"), cur.cgOrbit, tr("The camera slowly circles the set while paused or stopped"));
    cf->addRow(tr("Palette"), cgPalette);
    cf->addRow(tr("Floor"), cgFloor);
    cf->addRow(tr("TV stand"), cgStand);
    cf->addRow(tr("Objects"), cgSet);
    cf->addRow(cgObjects);
    cf->addRow(cgBackground);
    cf->addRow(cgBanding);
    cf->addRow(cgReveal);
    cf->addRow(cgOrbit);
    // Your own 3D models (statues), from a folder of OBJ / STL / PLY / GLB / FBX files
    auto* modelsRow = new QHBoxLayout;
    auto* modelsInfo = new QLabel(dlg);
    modelsInfo->setWordWrap(true);
    modelsInfo->setMinimumWidth(200);
    auto* chooseModels = new QPushButton(tr("Choose folder…"), dlg);
    auto* clearModels = new QToolButton(dlg);
    clearModels->setText(QStringLiteral("✕"));
    clearModels->setToolTip(tr("No models"));
    modelsRow->addWidget(modelsInfo, 1);
    modelsRow->addWidget(chooseModels);
    modelsRow->addWidget(clearModels);
    auto* modelFinish = combo({tr("Marble"), tr("Bronze"), tr("Chrome"), tr("Candy plastic"), tr("Their own colours")}, cur.modelFinish);
    auto* showModels = check(tr("Show my models"), cur.models, tr("Statues from your folder, on plinths around the set"));
    cf->addRow(tr("Your 3D models"), modelsRow);
    auto* modelsUp = combo({tr("Automatic (on the flat base)"), tr("Y is up"), tr("Z is up"), tr("X is up"), tr("Y is down"), tr("Z is down"), tr("X is down")}, cur.modelsUp);
    modelsUp->setToolTip(tr("Which way is up in your model files. Automatic stands a model on its flat base; choose an axis when a model lies on its side or stands on its head."));
    cf->addRow(tr("Models stand"), modelsUp);
    auto* modelsFace = combo({tr("Turning slowly"), tr("As in the file"), tr("Turned by 90°"), tr("Turned by 180°"), tr("Turned by 270°")}, cur.modelsFace);
    modelsFace->setToolTip(tr("A model file does not say which side is its front. The statues turn slowly, or stand still, turned as you choose."));
    cf->addRow(tr("Models face"), modelsFace);
    cf->addRow(tr("Model finish"), modelFinish);
    cf->addRow(showModels);
    auto refreshModels = [this, modelsInfo] {
        if (!m_desk) return;
        ModelLibrary* lib = m_desk->view()->modelLibrary();
        const QString folder = m_desk->view()->scene().modelsFolder;
        if (folder.isEmpty()) { modelsInfo->setText(tr("None (OBJ, STL, PLY, GLB or FBX files, up to 6)")); return; }
        if (lib->loading()) { modelsInfo->setText(tr("%1: loading…").arg(QFileInfo(folder).fileName())); return; }
        QString t = tr("%1: %n model(s)", "", lib->meshes().size()).arg(QFileInfo(folder).fileName());
        const QStringList sk = lib->skipped();
        // each model: its size and which way it was stood; then what was skipped, and why
        for (const QString& line : lib->described()) t += QLatin1Char('\n') + line;
        for (const QString& line : sk) t += QLatin1Char('\n') + tr("skipped %1").arg(line);
        modelsInfo->setText(t);
    };
    refreshModels();
    connect(m_desk->view()->modelLibrary(), &ModelLibrary::loaded, dlg, refreshModels);
    connect(chooseModels, &QPushButton::clicked, dlg, [this, dlg, refreshModels] {
        const QString d = QFileDialog::getExistingDirectory(dlg, tr("Folder with 3D models (OBJ, STL, PLY, GLB, FBX)"), m_desk->view()->scene().modelsFolder);
        if (d.isEmpty()) return;
        DeskView::Scene s = m_desk->view()->scene(); s.modelsFolder = d; s.models = true; setDeskScene(s);
        m_desk->view()->modelLibrary()->setFolder(d, ModelLibrary::Up(std::clamp(s.modelsUp, 0, 6)));
        refreshModels();
    });
    connect(clearModels, &QToolButton::clicked, dlg, [this, refreshModels] {
        DeskView::Scene s = m_desk->view()->scene(); s.modelsFolder.clear(); setDeskScene(s); refreshModels();
    });
    connect(modelFinish, &QComboBox::currentIndexChanged, dlg, [this](int i) { DeskView::Scene s = m_desk->view()->scene(); s.modelFinish = i; setDeskScene(s); });
    connect(modelsFace, &QComboBox::currentIndexChanged, dlg, [this](int i) { DeskView::Scene s = m_desk->view()->scene(); s.modelsFace = i; setDeskScene(s); });
    connect(modelsUp, &QComboBox::currentIndexChanged, dlg, [this, refreshModels](int i) {
        DeskView::Scene s = m_desk->view()->scene(); s.modelsUp = i; setDeskScene(s);
        m_desk->view()->modelLibrary()->setFolder(s.models ? s.modelsFolder : QString(), ModelLibrary::Up(i));
        refreshModels();
    });
    connect(showModels, &QCheckBox::toggled, dlg, [this](bool on) { DeskView::Scene s = m_desk->view()->scene(); s.models = on; setDeskScene(s); });
    v->addWidget(cgBox);
    cgBox->setVisible(cur.scene == 4);
    auto applyCg = [this, cgPalette, cgFloor, cgStand, cgObjects, cgBanding, cgReveal, cgOrbit, cgSet, cgBackground] {
        DeskView::Scene s = m_desk->view()->scene();
        s.cgObjectSet = cgSet->currentIndex();
        s.cgBackground = cgBackground->isChecked();
        s.cgPalette = cgPalette->currentIndex(); s.cgFloor = cgFloor->currentIndex(); s.cgStand = cgStand->currentIndex();
        s.cgObjects = cgObjects->isChecked(); s.cgBanding = cgBanding->isChecked();
        s.cgReveal = cgReveal->isChecked(); s.cgOrbit = cgOrbit->isChecked();
        setDeskScene(s);
    };
    for (QComboBox* c : {cgPalette, cgFloor, cgStand, cgSet}) connect(c, &QComboBox::currentIndexChanged, dlg, applyCg);
    for (QCheckBox* c : {cgObjects, cgBanding, cgReveal, cgOrbit, cgBackground}) connect(c, &QCheckBox::toggled, dlg, applyCg);

    auto* theaterBox = new QGroupBox(tr("Theater"), dlg);
    auto* tf = new QFormLayout(theaterBox);
    auto* look = combo({tr("35mm film print"), tr("Worn 16mm print"), tr("Drive-in"), tr("Keep my current look")}, m_settings.theaterLook);
    look->setToolTip(tr("The picture on the big screen. Leaving the theater brings your own look back."));
    tf->addRow(tr("Picture"), look);
    v->addWidget(theaterBox);
    theaterBox->setVisible(cur.scene == 3);
    connect(look, &QComboBox::currentIndexChanged, dlg, [this](int i) {
        m_settings.theaterLook = i;
        if (m_desk && m_desk->view()->scene().scene == 3) { theaterLook(false); theaterLook(true); }
    });

    auto apply = [this, group, mood, fog, strength, wood, quality, wallStyle, layout, frameStyle, wallBox, room, dlg,
                  tvHeight, picHeight, picSpacing, picSize, theaterBox, cgBox] {
        DeskView::Scene s = m_desk->view()->scene();
        s.scene = group->checkedId(); s.mood = mood->currentIndex(); s.fog = fog->currentIndex();
        s.fogStrength = strength->value() / 100.0; s.wood = wood->currentIndex(); s.quality = quality->currentIndex();
        s.wallStyle = wallStyle->currentIndex(); s.frameLayout = layout->currentIndex(); s.frameStyle = frameStyle->currentIndex();
        s.tvHeight = tvHeight->value() / 100.0; s.picHeight = picHeight->value() / 100.0;
        s.picSpacing = picSpacing->value() / 100.0; s.picSize = picSize->value() / 100.0;
        setDeskScene(s);
        wallBox->setVisible(s.scene == 2);
        theaterBox->setVisible(s.scene == 3);
        cgBox->setVisible(s.scene == 4);
        room->setEnabled(s.scene != 0);
        dlg->adjustSize();
    };
    connect(group, &QButtonGroup::idClicked, dlg, apply);
    for (QComboBox* c : {mood, fog, wood, quality, wallStyle, layout, frameStyle}) connect(c, &QComboBox::currentIndexChanged, dlg, apply);
    for (QSlider* sl : {strength, tvHeight, picHeight, picSpacing, picSize}) connect(sl, &QSlider::valueChanged, dlg, apply);
    room->setEnabled(cur.scene != 0);

    m_sceneDialog = dlg;
    // Beside the set, not over the picture: the right edge of the screen, near the top.
    dlg->adjustSize();
    const QRect avail = (m_desk->screen() ? m_desk->screen() : QGuiApplication::primaryScreen())->availableGeometry();
    dlg->move(avail.right() - dlg->width() - 40, avail.top() + 60);
    dlg->show();
}

bool MainWindow::inGamescope()
{
    // Steam Game Mode runs apps inside gamescope, which sets these.
    return qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") ||
           qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QLatin1String("gamescope"), Qt::CaseInsensitive);
}

bool MainWindow::isVideoFocus(QWidget* w) const
{
    for (QWidget* p = w; p; p = p->parentWidget())
        if (p == m_jfDock || p == m_playlistDock || p == m_settingsDock) return false;
    return true;
}

QString MainWindow::mediaTitle() const
{
    if (!m_mediaTitle.isEmpty()) return m_mediaTitle;
    const QString p = m_player->currentPath();
    return p.isEmpty() ? QString() : QFileInfo(p).completeBaseName();
}
int MainWindow::playlistCount() const { return m_playlist->count(); }
int MainWindow::playlistIndex() const { return m_playlist->currentIndex(); }
double MainWindow::volume01() const { return m_controls->volumeSlider()->value() / 100.0; }

void MainWindow::showControlsBriefly()
{
    // Remote input (gamepad) has no pointer: show the control bar as mouse movement would.
    if (m_deskActive) return;
    QMouseEvent e(QEvent::MouseMove, QPointF(m_video->width() / 2.0, m_video->height() - 20.0),
                  m_video->mapToGlobal(QPointF(m_video->width() / 2.0, m_video->height() - 20.0)), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(m_video, &e);
}

bool MainWindow::remoteAction(const QString& a, double v)
{
    if (a == "playpause") m_controls->playButton->click();
    else if (a == "play") m_player->play();
    else if (a == "pause") m_player->pause();
    else if (a == "stop") { m_player->pause(); m_player->seek(0, Player::SeekMode::Accurate); }
    else if (a == "next") nextItem(1);
    else if (a == "prev") nextItem(-1);
    else if (a == "seekrel") { m_player->seekRelative(qint64(v)); showOsd(v >= 0 ? tr("+%1 s").arg(qRound(v / 1e9)) : tr("−%1 s").arg(qRound(-v / 1e9))); }
    else if (a == "seekabs") m_player->seek(qint64(v), Player::SeekMode::Accurate);
    else if (a == "volume") { m_controls->volumeSlider()->setValue(qRound(std::clamp(v, 0.0, 1.0) * 100)); }
    else if (a == "volup" || a == "voldown") {
        m_controls->volumeSlider()->setValue(m_controls->volumeSlider()->value() + (a == "volup" ? 5 : -5));
        showOsd(tr("Volume %1%").arg(m_controls->volumeSlider()->value()));
    }
    else if (a == "rate") setSpeed(v);
    else if (a == "fullscreen") setFullscreen(v != 0);
    else if (a == "raise") { if (m_deskActive && m_desk) { m_desk->raise(); m_desk->activateWindow(); } else { show(); raise(); activateWindow(); } }
    else if (a == "quit") close();
    else if (a == "preset+") cyclePreset(1);
    else if (a == "preset-") cyclePreset(-1);
    else if (a == "subs") cycleSubtitle();
    else if (a == "audio") cycleAudio();
    else if (a == "desk") toggleDeskMode();
    else if (a == "fly") { if (m_deskActive && m_desk) m_desk->view()->toggleFly(); else setFullscreen(!m_fullscreen); }
    else if (a == "chapter+") jumpChapter(+1);
    else if (a == "chapter-") jumpChapter(-1);
    else if (a == "controls") {
        if (m_deskActive && m_desk) {   // in desk mode the Menu button changes the scene
            DeskView::Scene sc = m_desk->view()->scene();
            sc.scene = (sc.scene + 1) % 5;
            if (inGamescope() && sc.scene == 0) sc.scene = 1;
            setDeskScene(sc);
            showOsd(sc.scene == 0 ? tr("Scene: your desktop") : sc.scene == 1 ? tr("Scene: desk")
                    : sc.scene == 2 ? tr("Scene: wall-mounted TV") : sc.scene == 3 ? tr("Scene: movie theater") : tr("Scene: 90s CG room"));
        } else showControlsBriefly();
    }
    else if (a == "back") {
        // Back out one level: a flight into fullscreen, then fullscreen, then panels.
        if (m_deskActive && m_desk && m_desk->view()->phase() != DeskView::Phase::Desk) m_desk->view()->toggleFly();
        else if (m_fullscreen) setFullscreen(false);
        else if (m_jfDock->isVisible()) m_jfDock->hide();
        else if (m_playlistDock->isVisible()) m_playlistDock->hide();
        else if (m_settingsDock->isVisible()) m_settingsDock->hide();
    }
    else return false;
    return true;
}

bool MainWindow::loadSubtitleOffer(int index)
{
    if (index < 0 || index >= m_extSubs.size()) return false;
    setExternalSubtitleFile(m_extSubs[index].second, m_extSubs[index].first);
    return true;
}

void MainWindow::rememberPosition()
{
    const QString key = resumeKey();   // a local file's path, or a web video's page
    if (key.isEmpty() || !m_player->hasMedia()) return;
    if (tvOn()) return;   // a channel's programme isn't something to resume
    m_resume->remember(key, m_player->position() / 1000000, m_player->duration() / 1000000);
    m_resume->save();
}

void MainWindow::fillRecentMenu(QMenu* menu)
{
    QStringList existing;
    for (const QString& f : m_settings.recentFiles) if (OnlineResolver::isReference(f) || QFileInfo::exists(f)) existing << f;
    if (existing.isEmpty()) { menu->addAction(tr("No recent files"))->setEnabled(false); return; }
    for (const QString& f : existing) {
        const bool web = OnlineResolver::isReference(f);
        const QString page = web ? OnlineResolver::referencePage(f) : QString();
        const qint64 at = m_resume->position(web ? QStringLiteral("web:") + page : f);
        QString name = QFileInfo(f).fileName();
        if (web) name = tr("Web · %1").arg(OnlineResolver::referenceTitle(f).isEmpty() ? QUrl(page).host() + QUrl(page).path() : OnlineResolver::referenceTitle(f));
        name.replace(QLatin1Char('&'), QStringLiteral("&&"));
        const QString label = at > 0 ? tr("%1  (at %2)").arg(name, formatTime(at)) : name;
        QAction* a = menu->addAction(label, this, [this, f] { playIndex(m_playlist->addItems({f})); });
        a->setToolTip(web ? page : f);
    }
    menu->addSeparator();
    menu->addAction(tr("Clear recent files"), this, [this] { m_settings.recentFiles.clear(); });
}

void MainWindow::setSpeed(double rate)
{
    rate = std::clamp(rate, 0.25, 4.0);
    m_player->setRate(rate);
    showOsd(qFuzzyCompare(rate, 1.0) ? tr("Normal speed") : tr("Speed %1×").arg(rate, 0, 'g', 3));
}

void MainWindow::setLoop(qint64 aNs, qint64 bNs)
{
    m_loopA = aNs;
    m_loopB = bNs;
    updateSeekMarks();
    if (m_cutDialog && m_cutDialog->isVisible()) m_cutDialog->refresh();
    if (m_gifDialog && m_gifDialog->isVisible()) m_gifDialog->refresh();
}

void MainWindow::seekKeyframe(bool forward)
{
    // Local files: keyframes are looked up on the file itself (quick seeks with a demuxer of
    // its own; "the keyframe at or before T" is what every demuxer answers reliably), then
    // the player goes there exactly. Streams: the player's own key-unit seek.
    const QString src = m_jfItemId.isEmpty() ? m_currentLocal : QString();
    if (src.isEmpty()) { m_player->seekKeyframe(forward); return; }
    if (!m_keyframes) {
        m_keyframes = new LosslessCutter(this);
        connect(m_keyframes, &LosslessCutter::keyframeFound, this, [this](qint64, qint64 k) {
            auto done = [this](qint64 at) {
                m_kfBusy = false;
                if (at < 0) { showOsd(m_kfForward ? tr("No keyframe after this") : tr("No keyframe before this")); return; }
                m_player->seek(at, Player::SeekMode::Accurate);
                const qint64 ms = at / 1000000;
                showOsd(tr("Keyframe %1").arg(formatTime(ms) + QString::asprintf(".%03lld", ms % 1000)), 1500);
            };
            if (!m_kfForward) { done(k); return; }
            // Forward: widen the look-ahead until a keyframe beyond here turns up, then
            // step back from it to the first one after here.
            const qint64 from = m_kfFrom + 10000000;
            if (k > from) {
                m_kfBest = k;
                m_kfRefining = true;
                QTimer::singleShot(0, this, [this, k] { m_keyframes->findKeyframe(m_kfSource, k - 1000000); });
                return;
            }
            if (m_kfRefining) { done(m_kfBest); return; }
            const qint64 dur = m_player->duration();
            if (dur > 0 && m_kfFrom + m_kfStep > dur) { done(-1); return; }   // looked up to the end
            m_kfStep *= 2;
            if (m_kfStep > 256'000'000'000LL) { done(-1); return; }
            QTimer::singleShot(0, this, [this] { m_keyframes->findKeyframe(m_kfSource, m_kfFrom + m_kfStep); });
        });
    }
    if (m_kfBusy) return;
    m_kfBusy = true;
    m_kfForward = forward;
    m_kfSource = src;
    m_kfFrom = m_player->position();
    m_kfStep = 500000000;
    m_kfBest = -1;
    m_kfRefining = false;
    m_keyframes->findKeyframe(src, forward ? m_kfFrom + m_kfStep : std::max<qint64>(0, m_kfFrom - 20000000));
}

GifDialog* MainWindow::showGifDialog()
{
    if (!m_gifDialog) {
        GifDialog::Host h;
        h.position = [this] { return m_player->position(); };
        h.duration = [this] { return m_player->duration(); };
        h.loopA = [this] { return m_loopA; };
        h.loopB = [this] { return m_loopB; };
        h.title = [this] { return mediaTitle(); };
        h.folder = [this] { return m_settings.screenshotDir; };
        h.deskMode = [this] { return m_deskActive && m_desk; };
        h.recorder.pictureSize = [this](const QSize& box) {
            if (m_deskActive && m_desk) return m_desk->view()->sceneSizeIn(box);   // the whole scene
            return m_video->pictureSizeIn(box);
        };
        h.recorder.render = [this](const QSize& size, double clock) -> QImage {
            if (m_deskActive && m_desk) {
                m_video->setClockOverride(clock);
                QImage img = m_desk->view()->grabScene(size);
                m_video->setClockOverride(m_gifClockSaved);
                return img.convertToFormat(QImage::Format_RGB32);
            }
            if (!m_gifLook) return m_video->renderPictureAt(size, false);
            // Drawn for this very size, as a window of this size would show it: the look thins
            // out its scanlines by itself when there are too few rows for them (no moiré).
            return m_video->renderPictureAt(size, true, clock);
        };
        h.recorder.framePts = [this] { return m_player->lastFrameStreamTime(); };
        h.recorder.frameSerial = [this] { return m_player->frameSerial(); };
        h.recorder.isSeeking = [this] { return m_player->isSeeking(); };
        h.recorder.isPlaying = [this] { return m_player->isPlaying(); };
        h.recorder.effectClock = [this] { return m_video->effectTime(); };
        h.recorder.seekPaused = [this](qint64 ns) {
            if (m_player->isPlaying()) m_player->pause();
            m_player->seek(ns, Player::SeekMode::Accurate);
        };
        h.recorder.step = [this] { m_player->stepFrame(true); };
        h.recorder.restore = [this](bool wasPlaying) { if (wasPlaying) m_player->play(); };
        h.setLook = [this](bool on) { m_gifLook = on; };
        h.recording = [this](bool on) {
            m_gifRecording = on;
            updateVideoPath();   // (a GIF is drawn from frames as decoded)
            if (on) m_gifClockSaved = m_video->clockOverride();
            if (on) showOsd(tr("Making the GIF…"), 2000);
        };
        h.saveSettings = [this](const GifDialog::Settings& s) {
            m_settings.gifWidth = s.width;
            m_settings.gifFps = s.fps;
            m_settings.gifLook = s.look;
        };
        m_gifDialog = new GifDialog(h, {m_settings.gifWidth, m_settings.gifFps, m_settings.gifLook}, this);
        connect(m_gifDialog, &GifDialog::finished, this, [this](const GifRecorder::Result& r) {
            showOsd(r.ok ? tr("GIF saved: %1").arg(QFileInfo(r.path).fileName()) : tr("GIF not saved: %1").arg(r.error), 3000);
        });
    }
    m_gifDialog->refresh();
    m_gifDialog->show();
    m_gifDialog->raise();
    return m_gifDialog;
}

void MainWindow::setGifOptions(int width, int fps, bool look)
{
    m_settings.gifWidth = width;
    m_settings.gifFps = fps;
    m_settings.gifLook = look;
    delete m_gifDialog;   // rebuilt with the new options
    m_gifDialog = nullptr;
    showGifDialog();
}

CutDialog* MainWindow::showCutDialog()
{
    if (!m_cutDialog) {
        CutDialog::Host h;
        h.sourceFile = [this] { return m_jfItemId.isEmpty() ? m_currentLocal : QString(); };
        h.position = [this] { return m_player->position(); };
        h.duration = [this] { return m_player->duration(); };
        h.loopA = [this] { return m_loopA; };
        h.loopB = [this] { return m_loopB; };
        h.setLoop = [this](qint64 a, qint64 b) { setLoop(a, b); };
        h.seek = [this](qint64 ns) { m_player->seek(ns, Player::SeekMode::Accurate); };
        h.open = [this](const QString& path) { openFiles({path}, true); };
        m_cutDialog = new CutDialog(h, this);
        connect(m_cutDialog, &CutDialog::finished, this, [this](const LosslessCutter::Result& r) {
            showOsd(r.ok ? tr("Cut saved: %1").arg(QFileInfo(r.output).fileName()) : tr("Cut not saved"), 3000);
        });
    }
    m_cutDialog->refresh();
    m_cutDialog->show();
    m_cutDialog->raise();
    return m_cutDialog;
}

void MainWindow::cycleLoop()
{
    const qint64 pos = m_player->position();
    if (m_loopA < 0) { setLoop(pos, -1); showOsd(tr("Loop from %1 — press R again to set the end").arg(formatTime(pos / 1000000))); }
    else if (m_loopB < 0 && pos > m_loopA + 200000000) {
        setLoop(m_loopA, pos);
        showOsd(tr("Looping %1 – %2 (R: off)").arg(formatTime(m_loopA / 1000000), formatTime(pos / 1000000)));
        m_player->seek(m_loopA, Player::SeekMode::Accurate);
    } else { setLoop(-1, -1); showOsd(tr("Loop off")); }
}

void MainWindow::jumpChapter(int dir)
{
    const auto ch = m_player->chapters();
    if (ch.isEmpty()) { showOsd(tr("No chapters")); return; }
    const qint64 pos = m_player->position();
    int target = -1;
    if (dir > 0) { for (int i = 0; i < ch.size(); ++i) if (ch[i].startNs > pos + 500000000) { target = i; break; } }
    else {
        // Back: to the start of the current chapter, or the previous one if right at its start.
        for (int i = ch.size() - 1; i >= 0; --i) if (ch[i].startNs < pos - 2000000000) { target = i; break; }
        if (target < 0) target = 0;
    }
    if (target < 0) { showOsd(tr("Last chapter")); return; }
    m_player->seek(ch[target].startNs, Player::SeekMode::Accurate);
    showOsd(tr("Chapter %1: %2").arg(target + 1).arg(ch[target].title));
}

void MainWindow::updateSeekMarks()
{
    QVector<qint64> ms;
    for (const ChapterInfo& c : m_player->chapters()) ms << c.startNs / 1000000;
    m_controls->seek()->setMarks(ms, m_loopA >= 0 ? m_loopA / 1000000 : -1, m_loopB >= 0 ? m_loopB / 1000000 : -1);
}

void MainWindow::showPreview(qint64 ms, int x)
{
    QString text = formatTime(ms, m_player->duration() >= 3600000000000LL);
    for (const ChapterInfo& c : m_player->chapters()) if (c.startNs / 1000000 <= ms) text = formatTime(ms) + QStringLiteral("  ·  ") + c.title;
    m_previewText->setText(text);
    const bool thumbs = !m_currentLocal.isEmpty() && m_player->currentUri().startsWith(QLatin1String("file:"));
    m_previewImage->setVisible(thumbs);
    if (thumbs && std::llabs(ms - m_previewMs) > 250) { m_previewMs = ms; m_thumbs->request(m_player->currentUri(), ms * 1000000); }
    m_preview->adjustSize();
    const QPoint at = m_controls->seek()->mapTo(this, QPoint(x, 0));
    const int w = m_preview->width(), h = m_preview->height();
    m_preview->move(std::clamp(at.x() - w / 2, 4, width() - w - 4), at.y() - h - 10);
    m_preview->show();
    m_preview->raise();
}

QString MainWindow::systemReport() const
{
    // For bug reports: no file names, paths, user names or server addresses.
    QStringList r;
    auto line = [&](const QString& k, const QString& v) { r << QStringLiteral("%1: %2").arg(k, v); };
    QString distro = QSysInfo::prettyProductName();
    line("CRT Player", QCoreApplication::applicationVersion() + (qEnvironmentVariableIsSet("APPIMAGE") ? " (AppImage)" : " (binary)"));
    line("OS", distro + " | kernel " + QSysInfo::kernelVersion() + " | " + QSysInfo::currentCpuArchitecture());
    line("Session", qEnvironmentVariable("XDG_SESSION_TYPE", "?") + " | desktop " + qEnvironmentVariable("XDG_CURRENT_DESKTOP", "?") +
                        " | Qt platform " + QGuiApplication::platformName() +
                        (qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY") ? " | gamescope" : ""));
    line("Qt", QStringLiteral("%1 (built with %2)%3").arg(qVersion(), QT_VERSION_STR,
         qEnvironmentVariableIsSet("CRTPLAYER_QT_MODE") ? QStringLiteral(", AppImage using the %1 Qt").arg(qEnvironmentVariable("CRTPLAYER_QT_MODE")) : QString()));
    line("OpenGL", m_video->glInfo());
    QStringList scr;
    for (QScreen* s : QGuiApplication::screens())
        scr << QStringLiteral("%1x%2@%3x %4 Hz").arg(s->size().width()).arg(s->size().height()).arg(s->devicePixelRatio()).arg(s->refreshRate(), 0, 'f', 0);
    line("Screens", scr.join(", "));
    gchar* gv = gst_version_string();
    line("GStreamer", QString::fromUtf8(gv));
    g_free(gv);
    const QStringList hw = Player::availableHardwareDecoders();
    line("Hardware decoders", hw.isEmpty() ? QStringLiteral("none found") : hw.join(", "));
    const QStringList missing = Player::missingEssentialElements();
    line("Missing essentials", missing.isEmpty() ? QStringLiteral("none") : missing.join(", "));
    line("Hardware decoding setting", m_settings.hardwareDecoding ? "on" : "off");
    line("Distribution", Distro::prettyName() + " (" + Distro::familyName(Distro::family()) + ")");
    {
        QStringList g;
        for (const auto& gap : Player::missingRecommended()) g << gap.what + " [" + gap.package + "]";
        line("Missing for common formats", g.isEmpty() ? QStringLiteral("nothing") : g.join("; "));
    }
    line("Audio output", m_player->audioOutput());
    if (m_player->hasMedia()) {
        const SourceFormat f = m_video->sourceFormat();
        line("Current video", QStringLiteral("%1 | video %2 | audio %3 | %4x%5 | %6 fps | decoder %7 | source %8")
                                  .arg(m_player->containerFormat(), m_player->videoCodec(), m_player->audioCodec())
                                  .arg(f.width).arg(f.height).arg(m_player->frameRate(), 0, 'f', 3)
                                  .arg(m_player->videoDecoder(), m_jfItemId.isEmpty() ? "local file" : "Jellyfin stream"));
        line("Playback", QStringLiteral("state %1 | speed %2x | frames presented %3")
                             .arg(m_player->isPlaying() ? "playing" : "paused").arg(m_player->rate())
                             .arg(m_video->framesPresented()));
    } else line("Current video", "none");
    line("Look", QStringLiteral("preset %1 | desk mode %2").arg(m_presetName, m_deskActive ? "on" : "off"));
    line("Controllers", m_gamepad ? m_gamepad->status() : QStringLiteral("off"));
 line("Keep awake while playing", m_settings.keepAwake ? QStringLiteral("on (now: %1)").arg(m_sleep ? m_sleep->method() : QString()) : QStringLiteral("off"));
#ifndef _WIN32
    line("MPRIS", m_mpris && m_mpris->isRegistered() ? QStringLiteral("registered") : QStringLiteral("not available (no session bus)"));
#endif
    line("Jellyfin", m_jf->isSignedIn() ? QStringLiteral("signed in, server version %1").arg(m_jf->serverVersion()) : QStringLiteral("not signed in"));
    return r.join('\n');
}

void MainWindow::cycleAudio()
{
    const auto tracks = m_player->audioTracks();
    if (tracks.size() < 2) { showOsd(tracks.isEmpty() ? tr("No audio tracks") : tr("Only one audio track")); return; }
    chooseAudioTrack((m_player->currentAudioTrack() + 1) % tracks.size());
}

void MainWindow::cycleSubtitle()
{
    const auto tracks = m_player->subtitleTracks();
    if (tracks.isEmpty()) { showOsd(tr("No subtitle tracks")); return; }
    int next = m_player->currentSubtitleTrack() + 1;   // -1 (off) -> 0 -> ... -> off
    if (next >= tracks.size()) next = -1;
    chooseSubtitleTrack(next);
}

// ---- screenshots -------------------------------------------------------------

QString MainWindow::takeScreenshot(bool filtered, const QString& pathIn)
{
    if (!m_video->hasFrame()) { showOsd(tr("Nothing to capture yet")); return {}; }
    // The original frame is the frame as decoded: not one already scaled for the screen.
    // (A screenshot with the look shows what the window shows, from the same frame.)
    if (!filtered) ensureNativeFrame();
    const QImage img = filtered ? m_video->grabFilteredFrame() : m_video->grabOriginalFrame();
    if (!filtered) releaseNativeFrame();
    if (img.isNull()) { showOsd(tr("Screenshot failed")); return {}; }
    QString path = pathIn;
    if (path.isEmpty()) {
        QDir().mkpath(m_settings.screenshotDir);
        QString base = m_mediaTitle.isEmpty() ? QFileInfo(m_player->currentPath()).completeBaseName() : m_mediaTitle;
        base.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "_");
        const qint64 ms = m_player->position() / 1000000;
        const QString stamp = QString::asprintf("%02lld-%02lld-%02lld.%03lld", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
        path = QDir(m_settings.screenshotDir).filePath(QStringLiteral("%1_%2_%3.png").arg(base.isEmpty() ? "frame" : base, stamp, filtered ? "crt" : "original"));
    }
    if (!img.save(path)) { showOsd(tr("Could not write %1").arg(path), 4000); return {}; }
    showOsd(tr("Saved %1 screenshot: %2").arg(filtered ? tr("filtered") : tr("original"), QFileInfo(path).fileName()), 2500);
    return path;
}

// ---- overlays, fullscreen and auto-hide ----------------------------------------

void MainWindow::layoutOverlays()
{
    const int w = m_video->width(), h = m_video->height();
    const int barH = m_controls->sizeHint().height();
    if (m_fullscreen) {
        const int bw = std::min(w - 48, 1280);
        m_controls->setGeometry((w - bw) / 2, h - barH - 24, bw, barH);
        m_video->setBottomInset(0);
    } else {
        m_controls->setGeometry(8, h - barH - 8, w - 16, barH);
        m_video->setBottomInset(barH + 16);
    }
    m_osd->adjustSize();
    m_osd->move((w - m_osd->width()) / 2, 28);
    if (m_info->isVisible()) { m_info->adjustSize(); m_info->move(16, 16); }
    m_emptyHint->setGeometry(0, 0, w, h - (m_fullscreen ? 0 : barH + 16));
    m_controls->raise();
    m_osd->raise();
}

void MainWindow::setFullscreen(bool on)
{
    if (m_deskActive) {   // in desk mode "fullscreen" means flying into the set
        if (on) m_desk->view()->flyIn(); else m_desk->view()->flyOut();
        return;
    }
    if (on == m_fullscreen) return;
    m_fullscreen = on;
    if (on) {
        m_dockSettingsBeforeFs = m_settingsDock->isVisible();
        m_dockPlaylistBeforeFs = m_playlistDock->isVisible();
        m_settingsDock->hide();
        m_playlistDock->hide();
        m_wasMaximized = isMaximized();
        showFullScreen();
    } else {
        if (m_wasMaximized) showMaximized(); else showNormal();
        m_settingsDock->setVisible(m_dockSettingsBeforeFs);
        m_playlistDock->setVisible(m_dockPlaylistBeforeFs);
    }
    m_controls->setFullscreenIcon(on);
    layoutOverlays();
    setControlsShown(true);
    onMouseActivity();
}

bool MainWindow::controlsVisible() const { return m_controls->isVisible(); }

void MainWindow::setControlsShown(bool shown)
{
    m_controlsShown = shown;
    m_controls->setVisible(shown);
    if (shown) m_video->unsetCursor();
    else m_video->setCursor(Qt::BlankCursor);
}

void MainWindow::onMouseActivity()
{
    if (!m_controlsShown) setControlsShown(true);
    if (m_fullscreen) m_hideTimer.start();
}

void MainWindow::updateAutoHide()
{
    if (!m_fullscreen) { setControlsShown(true); return; }
    if (!m_player->isPlaying() || m_controls->isInteracting() || QApplication::activePopupWidget()) {
        m_hideTimer.start();
        return;
    }
    setControlsShown(false);
}

void MainWindow::changeEvent(QEvent* e)
{
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange) {
        // Keep our flag in sync if the compositor or window manager leaves fullscreen.
        const bool fs = windowState() & Qt::WindowFullScreen;
        if (!fs && m_fullscreen) {
            m_fullscreen = false;
            m_settingsDock->setVisible(m_dockSettingsBeforeFs);
            m_playlistDock->setVisible(m_dockPlaylistBeforeFs);
            m_controls->setFullscreenIcon(false);
            setControlsShown(true);
            layoutOverlays();
        }
    }
}

bool MainWindow::eventFilter(QObject* o, QEvent* e)
{
    if (o == m_video && e->type() == QEvent::Resize) layoutOverlays();
    if (e->type() == QEvent::MouseMove && m_fullscreen) {
        if (auto* w = qobject_cast<QWidget*>(o); w && w->window() == this) onMouseActivity();
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::showOsd(const QString& text, int ms)
{
    m_osd->setText(text);
    m_osd->adjustSize();
    m_osd->move((m_video->width() - m_osd->width()) / 2, 28);
    m_osd->show();
    m_osd->raise();
    m_osdTimer.start(ms);
}

// ---- periodic updates & info ---------------------------------------------------

void MainWindow::updatePosition()
{
    if (m_loopB > m_loopA && m_loopA >= 0 && m_player->isPlaying() && !m_controls->seek()->isScrubbing() && !m_gifRecording &&
        m_player->position() >= m_loopB)
        m_player->seek(m_loopA, Player::SeekMode::Accurate);
    const qint64 pos = m_player->position() / 1000000;
    const qint64 dur = m_player->duration() > 0 ? m_player->duration() / 1000000 : 0;
    m_controls->setTimes(pos, dur);
    if (m_deskActive) syncDeskBar();
}

void MainWindow::updateSourceInfo()
{
    const SourceFormat f = m_video->sourceFormat();
    if (!f.isValid()) { m_displayPanel->setSourceInfo(tr("No video loaded.")); return; }
    const QSizeF ds = displaySize(f, 0);
    m_displayPanel->setSourceInfo(tr("Coded %1×%2, pixel aspect %3:%4, rotation %5\nDisplays as %6×%7 (aspect %8:1)")
                                      .arg(f.width).arg(f.height).arg(f.parN).arg(f.parD)
                                      .arg(orientationToString(f.orient))
                                      .arg(qRound(ds.width())).arg(qRound(ds.height()))
                                      .arg(ds.width() / ds.height(), 0, 'f', 3));
    if (m_info->isVisible()) updateInfoOverlay();
}

void MainWindow::updateDecoderStatus()
{
    QString s;
    const QString vd = m_player->videoDecoder();
    if (!vd.isEmpty()) s += tr("Video decoder: %1 (%2)\n").arg(vd, m_player->videoDecoderIsHardware() ? tr("hardware") : tr("software"));
    const QString ad = m_player->audioDecoder();
    if (!ad.isEmpty()) s += tr("Audio decoder: %1\n").arg(ad);
    if (m_player->fellBackToSoftware()) s += tr("Hardware decoding failed for this file; using software.\n");
    const QStringList hw = Player::availableHardwareDecoders();
    s += hw.isEmpty() ? tr("No hardware video decoders are registered with GStreamer on this system.")
                      : tr("Hardware decoders available: %1").arg(hw.join(", "));
    m_playbackPanel->setDecoderStatus(s);
}

void MainWindow::reopenForDecoderChange()
{
    if (m_player->currentUri().isEmpty()) return;
    const qint64 pos = m_player->position();
    const bool playing = m_player->isPlaying();
    m_player->open(m_player->currentUri(), playing, pos);
    showOsd(m_settings.hardwareDecoding ? tr("Hardware decoding enabled") : tr("Software decoding"));
}

void MainWindow::updateInfoOverlay()
{
    const SourceFormat f = m_video->sourceFormat();
    const LayoutResult L = m_video->currentLayout();
    const SyncStats st = m_video->syncStats();
    QStringList lines;
    if (!m_jfItemId.isEmpty()) {   // (a network address, possibly with a token: never shown)
        lines << tr("File        %1").arg(mediaTitle());
        lines << tr("Jellyfin    %1").arg(jellyfinPlayDescription());
    } else if (!m_webPage.isEmpty()) {   // (the streams' own addresses say nothing, and carry a signature)
        lines << tr("File        %1").arg(mediaTitle());
        lines << tr("Web         %1").arg(QStringList{m_webSite, m_webWhat, m_player->isWeb() && m_player->webReport().value("streams").toInt() > 1
                                                                                  ? tr("picture and sound as two streams") : QString()}.join(QStringLiteral(" · ")));
    } else {
        lines << tr("File        %1").arg(QFileInfo(m_player->currentPath()).fileName());
    }
    lines << tr("Container   %1").arg(m_player->containerFormat());
    lines << tr("Video       %1 via %2 [%3]").arg(m_player->videoCodec(), m_player->videoDecoder(),
                                                  m_player->videoDecoderIsHardware() ? "HW" : "SW");
    lines << tr("Audio       %1 via %2").arg(m_player->audioCodec(), m_player->audioDecoder());
    lines << tr("Drawing     %1").arg(videoPathDescription());
    if (const QString enh = enhanceDescription(); !enh.isEmpty()) lines << tr("Enhance     %1").arg(enh);
    // NVIDIA's methods at work (or failing): their line from the settings panel, a sentence to a row.
    if (m_settings.enhanceNvidia && (m_settings.enhanceUpscale || m_settings.smoothMotion) && m_video->nvidia().install().usable()) {
        if (m_video->nvidiaUpscaling() || m_video->nvidiaMotion() || !m_video->nvidia().error().isEmpty())
            lines << tr("NVIDIA      %1").arg(nvidiaStatus().replace(QStringLiteral(". "), QStringLiteral(".\n            ")));
    }
    if (m_player->deinterlacing()) lines << tr("Interlaced  deinterlaced for display");
    if (m_player->audioDelay() != 0) lines << tr("Sound delay %1 ms").arg(m_player->audioDelay());
    if (f.isValid()) {
        const QSizeF ds = displaySize(f, m_video->aspectOverride());
        lines << tr("Coded       %1×%2 %3, %4 fps").arg(f.width).arg(f.height).arg(m_video->pixelFormat()).arg(m_player->frameRate(), 0, 'f', 3);
        lines << tr("PAR / DAR   %1:%2 / %3").arg(f.parN).arg(f.parD).arg(ds.width() / ds.height(), 0, 'f', 4);
        lines << tr("Rotation    %1").arg(orientationToString(f.orient));
        lines << tr("Colour      %1").arg(m_video->colorimetry());
    }
    lines << tr("Scaling     %1, picture %2×%3 px at (%4, %5)").arg(scaleModeName(m_video->scaleMode()))
                 .arg(qRound(L.visibleRect.width())).arg(qRound(L.visibleRect.height()))
                 .arg(qRound(L.visibleRect.x())).arg(qRound(L.visibleRect.y()));
    lines << tr("CRT         %1%2, %3 scanlines").arg(m_presetName, m_video->bypass() ? tr(" (bypassed)") : QString())
                 .arg(m_video->currentScanlines(), 0, 'f', 0);
    lines << tr("A/V sync    %1 frames, mean %2 ms, sd %3 ms (clock %4)").arg(st.frames).arg(st.meanMs, 0, 'f', 1)
                 .arg(st.stddevMs, 0, 'f', 1).arg(m_player->clockName());
    lines << tr("GL          %1").arg(m_video->glInfo());
    m_info->setText(lines.join('\n'));
    m_info->adjustSize();
    m_info->move(16, 16);
}

QJsonObject MainWindow::stateReport() const
{
    QJsonObject o;
    // Report the view that is actually showing the video.
    const SourceFormat f = (m_deskActive && m_desk) ? m_desk->view()->sourceFormat() : m_video->sourceFormat();
    const LayoutResult L = m_video->currentLayout();
    const QSizeF ds = displaySize(f, m_video->aspectOverride());
    static const char* states[] = {"idle", "loading", "paused", "playing", "error"};
    o["file"] = m_mediaTitle.isEmpty() ? QFileInfo(m_player->currentPath()).fileName() : m_mediaTitle;
    o["online"] = onlineReport();
    o["source"] = m_jfItemId.isEmpty() ? QStringLiteral("file") : QStringLiteral("jellyfin");
    o["jellyfinSignedIn"] = m_jf->isSignedIn();
    o["tv"] = m_tv->report();
    o["staticMoment"] = m_video->momentName();
    o["playlistCount"] = m_playlist->count();
    o["fmvColorsUsed"] = m_video->fmvColorsUsed();
    o["fmvFramesDrawn"] = double(m_video->fmvFramesDrawn());
    o["jellyfinPlayMethod"] = m_jfItemId.isEmpty() ? QString() : m_jfTranscoding ? QStringLiteral("Transcode") : QStringLiteral("DirectPlay");
    o["jellyfinTranscodeReasons"] = QJsonArray::fromStringList(m_jfReasons);
    o["jellyfinConvertedAfterFailure"] = m_jfRetried;
    o["jellyfinPlayDescription"] = jellyfinPlayDescription();
    o["jellyfinMaxBitrateMbps"] = m_settings.jfMaxBitrateMbps;
    o["jellyfinListingTotal"] = m_jfPanel->totalCount();
    o["jellyfinUser"] = m_jf->userName();
    o["jellyfinServerVersion"] = m_jf->serverVersion();
    o["jellyfinItem"] = m_jfItemId;
    o["jellyfinListing"] = m_jfPanel->currentTitle();
    o["jellyfinListingCount"] = m_jfPanel->itemCount();
    o["windowTitle"] = windowTitle();
    o["state"] = states[int(m_player->state())];
    o["positionMs"] = double(m_player->position() / 1000000);
    o["framePtsMs"] = m_player->lastFrameStreamTime() >= 0 ? m_player->lastFrameStreamTime() / 1e6 : -1.0;
    o["fps"] = m_player->frameRate();
    o["durationMs"] = double(m_player->duration() / 1000000);
    o["codedWidth"] = f.width;
    o["codedHeight"] = f.height;
    o["par"] = QStringLiteral("%1:%2").arg(f.parN).arg(f.parD);
    o["rotation"] = f.orient.rotation;
    o["displayWidth"] = ds.width();
    o["displayHeight"] = ds.height();
    o["displayAspect"] = ds.height() > 0 ? ds.width() / ds.height() : 0.0;
    o["pixelFormat"] = m_video->pixelFormat();
    o["videoDecoder"] = m_player->videoDecoder();
    o["videoDecoderHardware"] = m_player->videoDecoderIsHardware();
    o["audioDecoder"] = m_player->audioDecoder();
    o["clock"] = m_player->clockName();
    o["videoArea"] = rectJson(m_video->videoAreaPx());
    o["pictureRect"] = rectJson(L.visibleRect);
    o["pictureAspectOnScreen"] = L.visibleRect.height() > 0 ? L.visibleRect.width() / L.visibleRect.height() : 0.0;
    o["srcRect"] = rectJson(L.srcRect);
    o["scaleMode"] = scaleModeName(m_video->scaleMode());
    o["preset"] = m_presetName;
    o["loopAMs"] = m_loopA >= 0 ? double(m_loopA) / 1e6 : -1.0;
    o["loopBMs"] = m_loopB >= 0 ? double(m_loopB) / 1e6 : -1.0;
    o["bypass"] = m_video->bypass();
    o["compare"] = m_video->compare();
    o["scanlines"] = m_video->currentScanlines();
    o["moment"] = m_video->momentName();
    o["gamepad"] = m_gamepad ? m_gamepad->status() : QString();
    o["audioOutput"] = m_player->audioOutput();
    o["lookSound"] = m_settings.lookSound;
    o["noiseVolume"] = m_settings.noiseVolume;
    o["effectStrength"] = m_settings.effectStrength;
    o["tapeSound"] = QJsonObject{{"hiss", m_lastTape.hiss}, {"crackle", m_lastTape.crackle}, {"noiseGain", m_lastTape.noiseGain},
                                 {"wow", m_lastTape.wow}, {"saturation", m_lastTape.saturation}, {"tone", m_lastTape.tone},
                                 {"speaker", m_lastTape.speaker}, {"dropouts", m_lastTape.dropouts}, {"crush", m_lastTape.crush}};
    o["tapeSoundActive"] = m_player->hasTapeSound() && m_settings.lookSound &&
                           (m_params.tapeHiss > 0 || m_params.wowFlutter > 0 || m_params.tapeSaturation > 0 || m_params.tapeTone > 0 ||
                            m_params.tvSpeaker > 0 || m_params.filmCrackle > 0);
    o["sleepInhibited"] = m_sleep && m_sleep->active();
    o["sleepInhibitMethod"] = m_sleep ? m_sleep->method() : QString();
    o["qtVersion"] = QString::fromLatin1(qVersion());
    o["qtMode"] = qEnvironmentVariable("CRTPLAYER_QT_MODE", "not an AppImage");
    o["qtPlatform"] = QGuiApplication::platformName();
    o["distroFamily"] = Distro::familyName(Distro::family());
    {
        QJsonArray gaps;
        for (const auto& g : Player::missingRecommended()) gaps.append(g.what + " [" + g.package + "]");
        o["missingRecommended"] = gaps;
        o["installCommand"] = Distro::fullCodecCommand();
    }
    o["gamepadLastAction"] = m_gamepad ? m_gamepad->lastAction() : QString();
#ifndef _WIN32
    o["mprisService"] = m_mpris && m_mpris->isRegistered() ? m_mpris->serviceName() : QString();
#endif
    o["inGamescope"] = inGamescope();
    o["volume"] = volume01();
    o["rate"] = m_player->rate();
    o["loopA"] = double(m_loopA / 1000000);
    o["loopB"] = double(m_loopB / 1000000);
    o["chapterCount"] = int(m_player->chapters().size());
    o["externalSubtitle"] = m_extSubLabel;
    o["externalSubtitleOffers"] = int(m_extSubs.size());
    o["resumeMs"] = double(m_currentLocal.isEmpty() ? 0 : m_resume->position(m_currentLocal));
    QJsonArray rec;
    for (const QString& f : m_settings.recentFiles) rec.append(OnlineResolver::isReference(f) ? QStringLiteral("web:") + OnlineResolver::referenceTitle(f) : QFileInfo(f).fileName());
    o["recentFiles"] = rec;
    {
        const QSize ps = pixelatedSize(f, m_video->aspectOverride(), m_params.pixelHeight, m_params.pixelWidth);
        o["pixelResolution"] = ps.isEmpty() ? QStringLiteral("native") : QStringLiteral("%1x%2").arg(ps.width()).arg(ps.height());
    }
    o["fullscreen"] = m_fullscreen;
    o["windowFullScreenState"] = bool(windowState() & Qt::WindowFullScreen);
    o["controlsVisible"] = m_controls->isVisible();
    o["framesPresented"] = double(m_video->framesPresented());
    o["hasFrame"] = (m_deskActive && m_desk) ? m_desk->view()->hasFrame() : m_video->hasFrame();
    o["sync"] = m_video->syncStats().toJson();
    QJsonArray at, st;
    for (const auto& t : m_player->audioTracks()) at.append(t.label);
    for (const auto& t : m_player->subtitleTracks()) st.append(t.label);
    o["audioTracks"] = at;
    o["subtitleTracks"] = st;
    o["currentAudio"] = m_player->currentAudioTrack();
    o["currentSubtitle"] = m_player->currentSubtitleTrack();
    o["subtitleFeed"] = m_player->subtitleFeedReport();
    o["everyday"] = everydayReport();
    o["profile"] = m_video->profileReport();
    o["videoPath"] = videoPathReport();
    {
        QJsonObject e = m_video->enhanceReport();
        e["pictureLatencyMs"] = m_player->pictureLatencyMs();
        o["enhance"] = e;
    }
    o["missingPlugins"] = QJsonArray::fromStringList(m_player->missingPlugins());
    o["lastError"] = m_lastError;
    o["lastWarning"] = m_lastWarning;
    o["gl"] = m_video->glInfo();
    o["platform"] = QGuiApplication::platformName();
    {   // The look selector's list on the bar in use, where it is on the screen (tests: is it really shown there?)
        QComboBox* combo = lookSelector();
        QWidget* list = combo->view()->window();
        const bool open = combo->view()->isVisible();
        o["lookListOpen"] = open;
        if (open) o["lookListRect"] = rectJson(QRectF(list->mapToGlobal(QPoint(0, 0)), list->size()));
        if (const QScreen* s = screen()) {
            o["screenRect"] = rectJson(s->geometry());
            o["screenScale"] = s->devicePixelRatio();
        }
    }
    if (const QJsonObject nw = WinWindow::report(this); !nw.isEmpty()) o["nativeWindow"] = nw;
    if (m_desk) {
        if (const QJsonObject nw = WinWindow::report(m_desk); !nw.isEmpty()) o["deskNativeWindow"] = nw;
    }
    o["deskMode"] = m_deskActive;
    if (m_desk) {
        DeskView* v = m_desk->view();
        o["deskPhase"] = v->phaseName();
        o["deskProgress"] = v->flightProgress();
        o["deskPivot"] = v->isPivoted();
        o["deskShadowVisible"] = v->shadowVisible();
        o["deskClassic"] = v->classicShape();
        o["deskCabinet"] = DeskView::cabinetName(v->cabinet());
        o["deskBackdrop"] = v->room() ? QStringLiteral("room") : QStringLiteral("desktop");
        o["deskScene"] = DeskView::sceneName(v->scene().scene);
        o["sceneMood"] = v->scene().mood;
        o["sceneFog"] = v->scene().fog;
        o["sceneWood"] = v->scene().wood;
        o["sceneWall"] = v->scene().wallStyle;
        o["framesShown"] = v->framesShown();
        o["picturesLoaded"] = v->picturesLoaded();
        o["deskPitch"] = v->deskPose().pitch;
        o["deskClearance"] = v->cameraClearance();
        o["curtainOpen"] = v->curtainOpen();
        {
            const QVector3D c = v->theaterCamera();
            o["theaterCamera"] = QJsonArray{c.x(), c.y(), c.z()};
            o["seatEye"] = DeskView::seatEye(c.z());
        }
        o["houseLights"] = v->houseLights();
        o["deskCabinetDrawn"] = DeskView::cabinetName(v->effectiveCabinet());
        o["arcadeArt"] = v->arcadeArt();
        o["marqueeText"] = v->marqueeText();
        o["cgReveal"] = v->revealProgress();
        o["cgIdleMs"] = double(v->idleMs());
        o["seeksNoted"] = v->seeksNoted();
        o["cgPalette"] = v->scene().cgPalette;
        o["modelsShown"] = v->modelsShown();
        o["modelsLoading"] = v->modelLibrary()->loading();
        o["modelsSkipped"] = QJsonArray::fromStringList(v->modelLibrary()->skipped());
        QJsonArray modelsInfo;
        for (const ModelLibrary::Mesh& m : v->modelLibrary()->meshes())
            modelsInfo.append(QJsonObject{{"name", m.name}, {"triangles", m.triangles}, {"sourceTriangles", m.sourceTriangles}, {"points", m.points},
                                          {"sourcePoints", m.sourcePoints}, {"colours", m.hasColours}, {"up", ModelLibrary::upName(m.up)}, {"upWhy", m.upWhy},
                                          {"flatBase", m.flatBase}, {"width", m.size.x()}, {"height", m.size.y()}, {"depth", m.size.z()}, {"text", m.describe()}});
        o["modelsInfo"] = modelsInfo;
        o["modelsUp"] = ModelLibrary::upName(v->modelLibrary()->up());
        o["modelsFace"] = v->scene().modelsFace;
        o["deskPaints"] = double(v->paints());
        o["modelsLight"] = v->modelLibrary()->light();
        o["floorDrop"] = v->floorDrop();
        o["maskRect"] = QJsonArray{v->maskRect().left(), v->maskRect().right(), v->maskRect().top(), v->maskRect().bottom()};
        o["deskTvRect"] = rectJson(v->silhouetteLogical().boundingRect());
        o["deskGlassRect"] = rectJson(v->glassRectLogical());
        o["deskGlassAspect"] = v->glassRectLogical().height() > 0 ? v->glassRectLogical().width() / v->glassRectLogical().height() : 0.0;
        o["deskMaskRect"] = rectJson(m_desk->currentMask().boundingRect());
        o["deskControlsVisible"] = m_desk->controlsVisible();
        o["deskWindowVisible"] = m_desk->isVisible();
        o["mainWindowVisible"] = isVisible();
        const auto dp = v->deskPose();
        o["deskYaw"] = dp.yaw;
        o["deskPitch"] = dp.pitch;
    }
    return o;
}

// ---- desk mode -------------------------------------------------------------------

void MainWindow::createDesk()
{
    if (m_desk) return;
    m_desk = new DeskWindow(m_player, m_video, nullptr);
    m_desk->addActions(actions());   // every shortcut also works on the desk
    DeskView* v = m_desk->view();
    DeskView::DeskPose pose;
    pose.yaw = m_settings.deskYaw; pose.pitch = m_settings.deskPitch; pose.height = m_settings.deskHeight;
    pose.cx = m_settings.deskCx; pose.cy = m_settings.deskCy;
    v->setDeskPose(pose);
    v->setClassicShape(m_settings.deskClassic);
    v->setCabinet(m_settings.deskCabinet);
    v->setArcadeArt(m_settings.arcadeArt);
    v->setMarqueeText(DeskView::marqueeTitle(mediaTitle()));
    // Steam Game Mode (gamescope) cannot show the desktop through a window: always a scene.
    {
        DeskView::Scene sc;
        sc.scene = m_settings.deskScene; sc.mood = m_settings.sceneMood; sc.wood = m_settings.sceneWood;
        sc.fog = m_settings.sceneFog; sc.fogStrength = m_settings.sceneFogStrength; sc.quality = m_settings.sceneQuality;
        sc.wallStyle = m_settings.sceneWall; sc.frameStyle = m_settings.sceneFrameStyle;
        sc.frameLayout = m_settings.sceneFrameLayout; sc.framePaths = m_settings.sceneFrames;
        sc.tvHeight = m_settings.sceneTvHeight; sc.picHeight = m_settings.scenePicHeight;
        sc.picSpacing = m_settings.scenePicSpacing; sc.picSize = m_settings.scenePicSize;
        sc.cgPalette = m_settings.cgPalette; sc.cgFloor = m_settings.cgFloor; sc.cgStand = m_settings.cgStand;
        sc.cgObjects = m_settings.cgObjects; sc.cgBanding = m_settings.cgBanding;
        sc.cgReveal = m_settings.cgReveal; sc.cgOrbit = m_settings.cgOrbit; sc.cgObjectSet = m_settings.cgObjectSet; sc.cgBackground = m_settings.cgBackground;
        sc.modelsFolder = m_settings.cgModelsFolder; sc.modelsUp = m_settings.cgModelsUp; sc.modelsFace = m_settings.cgModelsFace; sc.models = m_settings.cgModels; sc.modelFinish = m_settings.cgModelFinish;
        if (inGamescope() && sc.scene == 0) sc.scene = 1;
        v->setScene(sc);
        if (sc.scene == 3) theaterLook(true);
        QTimer::singleShot(0, this, &MainWindow::updateTheater);
    }
    m_desk->setKeepOnTop(m_settings.deskOnTop);

    // The desk control strip mirrors the main controls.
    ControlBar* b = m_desk->bar();
    b->playlistButton->hide();
    b->settingsButton->hide();
    b->jellyfinButton->hide();
    b->aspectButton->setMenu(m_controls->aspectButton->menu());
    b->audioButton->setMenu(m_controls->audioButton->menu());
    b->subtitleButton->setMenu(m_controls->subtitleButton->menu());
    b->screenshotButton->setMenu(m_controls->screenshotButton->menu());
    b->fullscreenButton->setToolTip(tr("Fly into / out of the screen (F, double-click the set)"));
    for (auto pair : {std::pair{b->openButton, m_controls->openButton}, std::pair{b->prevButton, m_controls->prevButton},
                      std::pair{b->playButton, m_controls->playButton}, std::pair{b->nextButton, m_controls->nextButton},
                      std::pair{b->stepBackButton, m_controls->stepBackButton}, std::pair{b->stepFwdButton, m_controls->stepFwdButton},
                      std::pair{b->muteButton, m_controls->muteButton}}) {
        QToolButton* main = pair.second;
        connect(pair.first, &QToolButton::clicked, main, [main] { main->click(); });
    }
    connect(b->crtButton, &QToolButton::clicked, this, [this](bool on) { setBypass(!on); });
    connect(b->compareButton, &QToolButton::clicked, this, [this](bool on) { setCompare(on); });
    connect(b->fullscreenButton, &QToolButton::clicked, this, [v] { v->toggleFly(); });
    connect(b->presetCombo(), &QComboBox::activated, this, [this, b](int i) { selectPreset(b->presetCombo()->itemText(i)); });
    connect(b, &ControlBar::volumeChanged, this, [this](double vol) { m_controls->volumeSlider()->setValue(qRound(vol * 100)); });
    connect(b->seek(), &SeekSlider::scrubbedTo, this, [this](qint64 ms) { m_player->seek(ms * 1000000, Player::SeekMode::Fast); });
    connect(b->seek(), &SeekSlider::scrubFinished, this, [this](qint64 ms) { m_player->seek(ms * 1000000, Player::SeekMode::Accurate); });
    connect(v, &DeskView::contextMenuRequested, this, &MainWindow::showDeskMenu);
    connect(m_desk, &DeskWindow::closed, this, [this] { if (m_deskActive) leaveDeskMode(); });
    connect(v, &DeskView::phaseChanged, this, [this](DeskView::Phase ph) {
        if (ph == DeskView::Phase::Full) showOsd(tr("Fullscreen — press Esc or double-click to return to the desk"), 2200);
    });
    refreshPresetUi();
}

void MainWindow::enterDeskMode()
{
    if (m_deskActive) return;
    createDesk();
    QScreen* screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    if (m_fullscreen) setFullscreen(false);
    m_deskActive = true;
    m_desk->openOn(screen);
    syncDeskBar();
    hide();
}

void MainWindow::leaveDeskMode()
{
    if (!m_deskActive) return;
    storeDeskPose();
    m_deskActive = false;
    m_desk->hide();
    show();
    raise();
    activateWindow();
    m_video->update();
}

void MainWindow::storeDeskPose()
{
    if (!m_desk) return;
    const auto p = m_desk->view()->deskPose();
    m_settings.deskYaw = p.yaw; m_settings.deskPitch = p.pitch; m_settings.deskHeight = p.height;
    m_settings.deskCx = p.cx; m_settings.deskCy = p.cy;
    m_settings.deskClassic = m_desk->view()->classicShape();
    m_settings.deskCabinet = m_desk->view()->cabinet();
    m_settings.arcadeArt = m_desk->view()->arcadeArt();
    {
        const DeskView::Scene sc = m_desk->view()->scene();
        if (!inGamescope() || sc.scene != 0) m_settings.deskScene = sc.scene;
        theaterLook(false);   // the regular window gets your own look back
        m_settings.sceneMood = sc.mood; m_settings.sceneWood = sc.wood; m_settings.sceneFog = sc.fog;
        m_settings.sceneFogStrength = sc.fogStrength; m_settings.sceneQuality = sc.quality;
        m_settings.sceneWall = sc.wallStyle; m_settings.sceneFrameStyle = sc.frameStyle;
        m_settings.sceneFrameLayout = sc.frameLayout; m_settings.sceneFrames = sc.framePaths;
        m_settings.sceneTvHeight = sc.tvHeight; m_settings.scenePicHeight = sc.picHeight;
        m_settings.scenePicSpacing = sc.picSpacing; m_settings.scenePicSize = sc.picSize;
        m_settings.cgPalette = sc.cgPalette; m_settings.cgFloor = sc.cgFloor; m_settings.cgStand = sc.cgStand;
        m_settings.cgObjects = sc.cgObjects; m_settings.cgBanding = sc.cgBanding;
        m_settings.cgReveal = sc.cgReveal; m_settings.cgOrbit = sc.cgOrbit; m_settings.cgObjectSet = sc.cgObjectSet; m_settings.cgBackground = sc.cgBackground;
        m_settings.cgModelsFolder = sc.modelsFolder; m_settings.cgModelsUp = sc.modelsUp; m_settings.cgModelsFace = sc.modelsFace; m_settings.cgModels = sc.models; m_settings.cgModelFinish = sc.modelFinish;
    }
    m_settings.deskOnTop = m_desk->keepOnTop();
}

void MainWindow::syncDeskBar()
{
    if (!m_desk) return;
    ControlBar* b = m_desk->bar();
    const qint64 pos = m_player->position() / 1000000;
    const qint64 dur = m_player->duration() > 0 ? m_player->duration() / 1000000 : 0;
    b->setTimes(pos, dur);
    b->setPlaying(m_player->isPlaying());
    b->setMutedIcon(m_player->isMuted());
    QSignalBlocker vb(b->volumeSlider());
    b->volumeSlider()->setValue(m_controls->volumeSlider()->value());
    QSignalBlocker cb(b->crtButton), pb(b->compareButton);
    b->crtButton->setChecked(!m_video->bypass());
    b->compareButton->setChecked(m_video->compare());
}

void MainWindow::showDeskMenu(const QPoint& globalPos)
{
    DeskView* v = m_desk->view();
    QMenu menu;
    const bool inside = v->phase() == DeskView::Phase::Full || v->phase() == DeskView::Phase::FlyingIn;
    menu.addAction(inside ? tr("Back to the desk\tEsc") : tr("Fly into fullscreen\tF"), v, [v] { v->toggleFly(); });
    menu.addSeparator();
    // Choose something to watch without leaving the desk.
    menu.addAction(tr("Open files…\tCtrl+O"), this, [this] { openDialog(m_desk); });
    QMenu* pl = menu.addMenu(tr("Playlist"));
    if (m_playlist->count() == 0) pl->addAction(tr("Empty — open files or drop them on the set"))->setEnabled(false);
    for (int i = 0; i < m_playlist->count() && i < 40; ++i) {
        QAction* a = pl->addAction(m_playlist->labelAt(i), this, [this, i] { playIndex(i); });
        a->setCheckable(true);
        a->setChecked(i == m_playlist->currentIndex());
    }
    {   // shuffle and repeat live with the playlist
        pl->addSeparator();
        QAction* sh = pl->addAction(tr("Shuffle\tCtrl+H"), this, [this] { setShuffle(!m_settings.shuffle); });
        sh->setCheckable(true);
        sh->setChecked(m_settings.shuffle);
        QMenu* rp = pl->addMenu(tr("Repeat"));
        const QStringList names = {tr("Off"), tr("The whole playlist"), tr("This video")};
        for (int i = 0; i < 3; ++i) {
            QAction* a = rp->addAction(names[i], this, [this, i] { setRepeatMode(i); });
            a->setCheckable(true);
            a->setChecked(m_settings.repeatMode == i);
        }
    }
    fillRecentMenu(menu.addMenu(tr("Recent files")));
    {   // 2.11: the things the settings panel holds, within reach in desk mode
        QAction* subs = menu.addAction(tr("Subtitles\tV"), this, [this] { setSubtitlesWanted(!m_settings.subtitlesOn); });
        subs->setCheckable(true);
        subs->setChecked(m_settings.subtitlesOn);
        QAction* night = menu.addAction(tr("Night mode (even out loud and quiet)\tD"), this, [this] { setNightMode(!m_settings.nightMode); });
        night->setCheckable(true);
        night->setChecked(m_settings.nightMode);
        QMenu* sm = menu.addMenu(tr("Sleep timer"));
        const bool running = m_sleepAt > 0 || m_sleepAtEnd;
        auto addSleep = [&](const QString& text, int minutes) {
            QAction* a = sm->addAction(text, this, [this, minutes] { setSleepTimer(minutes); });
            a->setCheckable(true);
            a->setChecked(running ? m_sleepMinutes == minutes : minutes == 0);
        };
        addSleep(tr("Off"), 0);
        for (int m : {15, 30, 45, 60, 90, 120}) addSleep(tr("%1 minutes").arg(m), m);
        addSleep(tr("At the end of this video"), -1);
    }
    if (m_tv && !m_tv->channels().isEmpty()) {   // Cable TV: on/off, the guide, and the channels
        QMenu* tm = menu.addMenu(tr("Cable TV"));
        QAction* on = tm->addAction(tr("TV on\tCtrl+T"), this, [this] { setTvMode(!tvOn()); });
        on->setCheckable(true);
        on->setChecked(tvOn());
        if (tvOn()) {
            QAction* g = tm->addAction(tr("Guide\tW"), this, [this] { m_tv->setGuide(!m_tv->guideVisible()); });
            g->setCheckable(true);
            g->setChecked(m_tv->guideVisible());
        }
        tm->addSeparator();
        for (const TvChannel& c : m_tv->channels()) {
            const int n = c.number;
            QAction* a = tm->addAction(QStringLiteral("%1  %2").arg(n).arg(c.name), this, [this, n] { setTvMode(true); if (tvOn()) m_tv->tune(n); });
            a->setCheckable(true);
            a->setChecked(tvOn() && m_tv->currentChannel() == n);
        }
    }
    if (m_jf->isSignedIn()) {
        QMenu* jm = menu.addMenu(tr("Jellyfin: continue watching"));
        if (m_jfResume.isEmpty()) jm->addAction(tr("Nothing to continue"))->setEnabled(false);
        for (const JfItem& it : m_jfResume) jm->addAction(it.displayName(), this, [this, it] { playJellyfin(it, false); });
        jm->addSeparator();
        jm->addAction(tr("Browse the library (opens the regular window)"), this, [this] { showJellyfin(true); });
        m_jf->loadHome();   // refresh the list for next time
    }
    menu.addSeparator();
    QMenu* presets = menu.addMenu(tr("CRT preset"));
    for (const QString& n : m_presets.builtinNames() + m_presets.userNames()) {
        QAction* a = presets->addAction(n, this, [this, n] { selectPreset(n); });
        a->setCheckable(true);
        a->setChecked(n == m_presetName);
    }
    QMenu* set = menu.addMenu(tr("Set"));
    const struct { int id; const char* label; } sets[] = {
        {0, "CRT television (curved glass)"}, {1, "Flat-face CRT (late 90s)"}, {2, "Flat-panel screen"},
        {3, "80s wood-grain console TV"}, {4, "Broadcast monitor (PVM)"}, {5, "Beige PC monitor"}, {6, "Arcade cabinet"}};
    for (const auto& c : sets) {
        QAction* a = set->addAction(tr(c.label), this, [v, id = c.id] { v->setCabinet(id); });
        a->setCheckable(true);
        a->setChecked(v->cabinet() == c.id);
    }
    if (v->cabinet() == DeskRenderer::ArcadeCabinet) {
        QMenu* art = set->addMenu(tr("Arcade art"));
        const QStringList arts = {tr("Space (stars, a ringed planet, a neon grid)"), tr("Sunset (striped sun, mountains)"),
                                  tr("Neon (stripes and triangles)"), tr("70s woodgrain (with a stripe)")};
        for (int i = 0; i < arts.size(); ++i) {
            QAction* a = art->addAction(arts[i], this, [v, i] { v->setArcadeArt(i); });
            a->setCheckable(true);
            a->setChecked(v->arcadeArt() == i);
        }
    }
    QMenu* sceneMenu = menu.addMenu(tr("Scene"));
    {
        const DeskView::Scene cur = v->scene();
        auto choice = [this, sceneMenu, cur](const QString& title, const QStringList& names, int current,
                                             std::function<void(DeskView::Scene&, int)> set, bool sub) {
            QMenu* m = sub ? sceneMenu->addMenu(title) : sceneMenu;
            for (int i = 0; i < names.size(); ++i) {
                QAction* a = m->addAction(names[i], this, [this, set, i] { DeskView::Scene s = m_desk->view()->scene(); set(s, i); setDeskScene(s); });
                a->setCheckable(true);
                a->setChecked(i == current);
            }
        };
        choice(QString(), {tr("Your desktop (transparent)"), tr("Desk (wooden desk, wall and lamp)"), tr("Wall-mounted TV (with pictures)"),
                           tr("Movie theater"), tr("90s CG room")}, cur.scene,
               [](DeskView::Scene& s, int i) { s.scene = i; }, false);
        if (inGamescope()) sceneMenu->actions().first()->setEnabled(false);   // Game Mode has no desktop to show
        sceneMenu->addSeparator();
        choice(tr("Mood"), {tr("Evening (lamp on)"), tr("Night (lamp dimmed)"), tr("Lights off (only the screen)")}, cur.mood,
               [](DeskView::Scene& s, int i) { s.mood = i; }, true);
        choice(tr("Fog"), {tr("Off"), tr("Light haze"), tr("Thick fog")}, cur.fog, [](DeskView::Scene& s, int i) { s.fog = i; }, true);
        choice(tr("Wood"), {tr("Walnut"), tr("Oak"), tr("Cherry")}, cur.wood, [](DeskView::Scene& s, int i) { s.wood = i; }, true);
        choice(tr("Quality"), {tr("Low (fastest)"), tr("Medium"), tr("High")}, cur.quality, [](DeskView::Scene& s, int i) { s.quality = i; }, true);
        if (cur.scene == 4) {
            choice(tr("Palette"), {tr("Workstation (teal)"), tr("Sunset"), tr("Deep space")}, cur.cgPalette,
                   [](DeskView::Scene& s, int i) { s.cgPalette = i; }, true);
            choice(tr("Floor"), {tr("Mirror checkerboard"), tr("Neon grid")}, cur.cgFloor, [](DeskView::Scene& s, int i) { s.cgFloor = i; }, true);
            choice(tr("TV stand"), {tr("Chrome pedestal"), tr("Marble plinth"), tr("Floating")}, cur.cgStand,
                   [](DeskView::Scene& s, int i) { s.cgStand = i; }, true);
            choice(tr("Objects"), {tr("Chrome and marble"), tr("Toybox (plastic)"), tr("Organic"), tr("Wooden mannequins"), tr("Mixed")},
                   cur.cgObjectSet, [](DeskView::Scene& s, int i) { s.cgObjectSet = i; }, true);
        }
        if (cur.scene == 2) {
            choice(tr("Wall"), {tr("Warm white"), tr("Sage green"), tr("Navy"), tr("Charcoal"), tr("Pinstripe wallpaper"), tr("Damask wallpaper")},
                   cur.wallStyle, [](DeskView::Scene& s, int i) { s.wallStyle = i; }, true);
            choice(tr("Pictures"), {tr("None"), tr("One on each side"), tr("Two on the left"), tr("Two on the right"), tr("Two on each side")},
                   cur.frameLayout, [](DeskView::Scene& s, int i) { s.frameLayout = i; }, true);
            choice(tr("Frames"), {tr("Black"), tr("Wood"), tr("Gold")}, cur.frameStyle, [](DeskView::Scene& s, int i) { s.frameStyle = i; }, true);
        }
        sceneMenu->addSeparator();
        sceneMenu->addAction(tr("Scene settings…"), this, &MainWindow::openSceneDialog);
    }
    QMenu* shape = menu.addMenu(tr("Screen shape"));
    QAction* follow = shape->addAction(tr("Follow the video (portrait video pivots the set)"), this, [v] { v->setClassicShape(false); });
    QAction* classic = shape->addAction(tr("Classic 4:3 tube (letterboxed)"), this, [v] { v->setClassicShape(true); });
    follow->setCheckable(true); classic->setCheckable(true);
    follow->setChecked(!v->classicShape()); classic->setChecked(v->classicShape());
    QAction* top = menu.addAction(tr("Keep on top of other windows"), this, [this](bool on) { m_desk->setKeepOnTop(on); });
    top->setCheckable(true);
    top->setChecked(m_desk->keepOnTop());
    menu.addAction(tr("Reset position and angle"), this, [v] { v->resetDeskPose(); });
    menu.addSeparator();
    menu.addAction(tr("Open the regular player window\tT"), this, [this] { leaveDeskMode(); });
    menu.exec(globalPos);
}
