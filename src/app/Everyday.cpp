// Everyday playback (2.11), as part of MainWindow: what carries over from video to video
// (subtitles on or off, the languages picked, the sound delay), subtitle timing and style,
// night mode, deinterlacing, shuffle and repeat, playlist files, carrying on with the
// next video in a folder, and the sleep timer.
#include "MainWindow.h"

#include "playback/Player.h"
#include "render/VideoWidget.h"
#include "tv/TvController.h"
#include "ui/ControlBar.h"
#include "ui/CutDialog.h"
#include "ui/GifDialog.h"
#include "ui/PlaybackPanel.h"
#include "ui/PlaylistPanel.h"

#include <QDateTime>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSlider>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <algorithm>
#include <gst/tag/tag.h>

namespace {
const QStringList& videoExtensions()
{
    static const QStringList e = {"mp4", "m4v", "mkv", "webm", "mov", "avi", "wmv", "flv", "mpg", "mpeg", "ts", "m2ts", "mts",
                                  "vob", "ogv", "3gp", "divx", "asf", "y4m"};
    return e;
}
// Name order with numbers read as numbers: "Episode 2" before "Episode 10". (Done by hand:
// the system's collation does not sort numbers in every locale.)
bool naturalLess(const QString& a, const QString& b)
{
    int i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            int i2 = i, j2 = j;
            while (i2 < a.size() && a[i2].isDigit()) ++i2;
            while (j2 < b.size() && b[j2].isDigit()) ++j2;
            QString na = a.mid(i, i2 - i), nb = b.mid(j, j2 - j);
            while (na.size() > 1 && na[0] == '0') na.remove(0, 1);
            while (nb.size() > 1 && nb[0] == '0') nb.remove(0, 1);
            if (na.size() != nb.size()) return na.size() < nb.size();
            if (na != nb) return na < nb;
            i = i2; j = j2;
        } else {
            const QChar ca = a[i].toLower(), cb = b[j].toLower();
            if (ca != cb) return ca < cb;
            ++i; ++j;
        }
    }
    return a.size() - i < b.size() - j;
}

QString signedMs(int ms) { return (ms > 0 ? QStringLiteral("+") : QString()) + QString::number(ms) + QStringLiteral(" ms"); }
}

// ---- at startup: the stored choices go to the player and the panels ----------------------

void MainWindow::applyEverydaySettings()
{
    m_player->setPreferredLanguages(m_settings.audioLang, m_settings.subtitleLang);
    m_player->setSubtitlesWanted(m_settings.subtitlesOn);
    m_player->setAudioDelay(m_settings.audioDelayMs);
    m_player->setSubtitleStyle(subtitleStyleFromSettings());
    m_player->setDeinterlace(m_settings.deinterlace);
    m_playbackPanel->setSubtitlesWanted(m_settings.subtitlesOn);
    m_playbackPanel->setSubtitleStyle(m_settings.subSize, m_settings.subColor, m_settings.subBackground, m_settings.subPosition);
    m_playbackPanel->setAudioDelay(m_settings.audioDelayMs);
    m_playbackPanel->setNightMode(m_settings.nightMode);
    m_playbackPanel->setDeinterlace(m_settings.deinterlace);
    m_playbackPanel->setVideoPath(m_settings.videoPath);
    m_playbackPanel->setLookDetail(m_settings.lookDetail);
    m_playbackPanel->setEnhance(m_settings.enhanceUpscale, m_settings.enhanceSharpness, m_settings.smoothMotion);
    applyEnhance();
    m_playbackPanel->setAutoNext(m_settings.autoNext);
    m_playbackPanel->setSleepTimer(0, QString());
    m_playlist->setShuffle(m_settings.shuffle);
    m_playlist->setRepeatMode(m_settings.repeatMode);
    applyLookSound();   // (night mode is part of the sound chain)
}

SubtitleStyle MainWindow::subtitleStyleFromSettings() const
{
    SubtitleStyle st;
    st.size = m_settings.subSize; st.color = m_settings.subColor;
    st.background = m_settings.subBackground; st.position = m_settings.subPosition;
    return st;
}

// ---- subtitles and languages: they stay as set ---------------------------------------------

void MainWindow::setSubtitlesWanted(bool on, bool announce)
{
    m_settings.subtitlesOn = on;
    m_player->setPreferredLanguages(m_settings.audioLang, m_settings.subtitleLang);
    m_player->setSubtitlesWanted(on);
    m_playbackPanel->setSubtitlesWanted(on);
    rebuildSubtitleMenu();
    refreshSubtitlesSoon(false);
    if (on) maybeLoadOfferedSubtitle();
    if (!announce) return;
    if (!on) { showOsd(tr("Subtitles off")); return; }
    const auto tracks = m_player->subtitleTracks();
    const int cur = m_player->currentSubtitleTrack();
    if (!m_player->hasMedia()) showOsd(tr("Subtitles on"));
    else if (tracks.isEmpty()) showOsd(tr("Subtitles on (this video has none)"));
    else showOsd(tr("Subtitles: %1").arg(tracks.value(std::clamp(cur, 0, int(tracks.size()) - 1)).label));
}

void MainWindow::chooseSubtitleTrack(int index)
{
    const auto tracks = m_player->subtitleTracks();
    if (index < 0 || index >= tracks.size()) { setSubtitlesWanted(false); return; }
    m_settings.subtitlesOn = true;
    if (!tracks[index].lang.isEmpty()) m_settings.subtitleLang = tracks[index].lang;   // this language, from now on
    m_player->setPreferredLanguages(m_settings.audioLang, m_settings.subtitleLang);
    m_player->setSubtitlesWanted(true);
    m_player->setSubtitleTrack(index);
    m_playbackPanel->setSubtitlesWanted(true);
    showOsd(tracks[index].label);
    rebuildSubtitleMenu();
    refreshSubtitlesSoon(false);
}

void MainWindow::chooseAudioTrack(int index)
{
    const auto tracks = m_player->audioTracks();
    if (index < 0 || index >= tracks.size()) return;
    if (!tracks[index].lang.isEmpty()) m_settings.audioLang = tracks[index].lang;
    m_player->setPreferredLanguages(m_settings.audioLang, m_settings.subtitleLang);
    m_player->setAudioTrack(index);
    showOsd(tracks[index].label);
    rebuildAudioMenu();
}

// Subtitles are wanted, the video has none of its own, and a Jellyfin server offers some as
// files: the one in the preferred language (or the first) is loaded, once per video.
void MainWindow::maybeLoadOfferedSubtitle()
{
    if (!m_settings.subtitlesOn || m_jfItemId.isEmpty() || m_extSubs.isEmpty() || !m_extSubLabel.isEmpty()) return;
    if (!m_player->hasMedia() || !m_player->subtitleTracks().isEmpty() || m_autoSubTried == m_jfItemId) return;
    m_autoSubTried = m_jfItemId;
    int pick = 0;
    if (!m_settings.subtitleLang.isEmpty()) {
        const gchar* name = gst_tag_get_language_name(m_settings.subtitleLang.toUtf8().constData());
        const QString langName = name ? QString::fromUtf8(name) : QString();
        for (int i = 0; i < m_extSubs.size(); ++i) {
            const QString label = m_extSubs[i].first;
            if ((!langName.isEmpty() && label.contains(langName, Qt::CaseInsensitive))
                || label.contains(QStringLiteral("(%1)").arg(m_settings.subtitleLang), Qt::CaseInsensitive)) { pick = i; break; }
        }
    }
    setExternalSubtitleFile(m_extSubs[pick].second, m_extSubs[pick].first);
}

// ---- delays ---------------------------------------------------------------------------

void MainWindow::setAudioDelay(int ms, bool announce)
{
    ms = std::clamp(ms, -10000, 10000);
    m_settings.audioDelayMs = ms;
    m_player->setAudioDelay(ms);
    m_playbackPanel->setAudioDelay(ms);
    if (announce) showOsd(ms == 0 ? tr("Sound delay: none") : tr("Sound delay: %1").arg(signedMs(ms)));
}

void MainWindow::setSubtitleDelay(int ms, bool announce)
{
    ms = std::clamp(ms, -60000, 60000);
    const bool changed = ms != m_player->subtitleDelay();
    m_player->setSubtitleDelay(ms);
    m_playbackPanel->setSubtitleDelay(ms);
    if (changed) refreshSubtitlesSoon(false);
    if (announce) showOsd(ms == 0 ? tr("Subtitle delay: none") : tr("Subtitle delay: %1").arg(signedMs(ms)));
}

void MainWindow::setSubtitleStyle(const SubtitleStyle& st)
{
    m_settings.subSize = std::clamp(st.size, 0, 3);
    m_settings.subColor = std::clamp(st.color, 0, 1);
    m_settings.subBackground = std::clamp(st.background, 0, 1);
    m_settings.subPosition = std::clamp(st.position, 0, 2);
    m_player->setSubtitleStyle(subtitleStyleFromSettings());
    m_playbackPanel->setSubtitleStyle(m_settings.subSize, m_settings.subColor, m_settings.subBackground, m_settings.subPosition);
    refreshSubtitlesSoon(false);
}

// Subtitles are drawn into the frames as they pass. Paused, nothing passes, so a change
// would only show on the next frame: the frame is fetched again. (While playing, a new
// delay holds from the next subtitle line on; the one on screen keeps its times.)
void MainWindow::refreshSubtitlesSoon(bool evenWhilePlaying)
{
    if (!m_player->hasMedia()) return;
    if (evenWhilePlaying) m_subRefreshAlways = true;
    m_subRefresh.start();
}

void MainWindow::refreshSubtitlesNow()
{
    const bool always = m_subRefreshAlways;
    m_subRefreshAlways = false;
    if (!m_player->hasMedia() || m_player->isSeeking() || m_gifBusy()) return;
    if (m_player->state() == Player::State::Paused || (always && m_player->isPlaying()))
        m_player->seek(m_player->position(), Player::SeekMode::Accurate);
}

// ---- night mode, deinterlacing ----------------------------------------------------------

void MainWindow::setNightMode(bool on, bool announce)
{
    m_settings.nightMode = on;
    m_playbackPanel->setNightMode(on);
    applyLookSound();
    if (announce) showOsd(on ? tr("Night mode: loud and quiet evened out") : tr("Night mode off"));
}

void MainWindow::setDeinterlace(bool on)
{
    if (m_settings.deinterlace == on) return;
    m_settings.deinterlace = on;
    m_player->setDeinterlace(on);
    m_playbackPanel->setDeinterlace(on);
    // It takes effect when a video opens: the current one is opened again where it is.
    const QString cur = m_player->currentUri();
    if (!cur.isEmpty() && m_player->hasMedia()) m_player->open(cur, m_player->isPlaying(), m_player->position());
    showOsd(on ? tr("Deinterlacing on") : tr("Deinterlacing off"));
}

// ---- shuffle, repeat, what comes next ------------------------------------------------------

void MainWindow::setShuffle(bool on)
{
    m_settings.shuffle = on;
    m_shufflePlayed.clear();
    m_shuffleHistory.clear();
    m_playlist->setShuffle(on);
    showOsd(on ? tr("Shuffle on") : tr("Shuffle off"));
}

void MainWindow::setRepeatMode(int mode)
{
    m_settings.repeatMode = std::clamp(mode, 0, 2);
    m_playlist->setRepeatMode(m_settings.repeatMode);
    showOsd(m_settings.repeatMode == 0 ? tr("Repeat off") : m_settings.repeatMode == 1 ? tr("Repeat: the whole playlist") : tr("Repeat: this video"));
}

void MainWindow::setAutoNext(bool on)
{
    m_settings.autoNext = on;
    m_playbackPanel->setAutoNext(on);
}

// The playlist entry to play after (dir > 0) or before (dir < 0) the current one, or -1
// when there is none. With shuffle, every entry plays once per round, in a random order;
// "before" walks back through what was played.
int MainWindow::pickNext(int dir)
{
    const int n = m_playlist->count(), cur = m_playlist->currentIndex();
    if (n == 0) return -1;
    if (m_settings.shuffle && n > 1) {
        const QStringList items = m_playlist->items();
        if (dir < 0) {
            while (!m_shuffleHistory.isEmpty()) {
                const int i = items.indexOf(m_shuffleHistory.takeLast());
                if (i >= 0 && i != cur) return i;
            }
            return -1;
        }
        const QString curPath = m_playlist->at(cur);
        if (!curPath.isEmpty()) {
            if (!m_shufflePlayed.contains(curPath)) m_shufflePlayed.append(curPath);
            m_shuffleHistory.append(curPath);
            if (m_shuffleHistory.size() > 500) m_shuffleHistory.removeFirst();
        }
        QVector<int> left;
        for (int i = 0; i < n; ++i) if (i != cur && !m_shufflePlayed.contains(items[i])) left.append(i);
        if (left.isEmpty()) {   // the round is complete
            if (m_settings.repeatMode != 1) return -1;
            m_shufflePlayed.clear();
            if (!curPath.isEmpty()) m_shufflePlayed.append(curPath);
            for (int i = 0; i < n; ++i) if (i != cur) left.append(i);
        }
        return left[int(QRandomGenerator::global()->bounded(quint32(left.size())))];
    }
    const int i = cur + dir;
    if (i >= 0 && i < n) return i;
    if (m_settings.repeatMode == 1 && n > 0) return (i + n) % n;
    return -1;
}

// The video after `path` in its folder, in natural name order ("Episode 2" before "Episode 10").
QString MainWindow::nextInFolder(const QString& path)
{
    const QFileInfo fi(path);
    if (!fi.exists()) return QString();
    QStringList names;
    for (const QFileInfo& f : QDir(fi.absolutePath()).entryInfoList(QDir::Files))
        if (videoExtensions().contains(f.suffix().toLower())) names << f.fileName();
    std::sort(names.begin(), names.end(), naturalLess);
    const int i = names.indexOf(fi.fileName());
    return (i >= 0 && i + 1 < names.size()) ? QDir(fi.absolutePath()).filePath(names[i + 1]) : QString();
}

// A video has ended (not in TV mode): what plays now. Returns false when nothing does.
bool MainWindow::playAfterEnd()
{
    const int cur = m_playlist->currentIndex();
    if (m_settings.repeatMode == 2 && cur >= 0) {   // this video, again from its start
        m_pendingStartNs = 0;
        playIndex(cur);
        return true;
    }
    const int next = pickNext(+1);
    if (next >= 0) {
        if (next <= cur || m_settings.shuffle) m_pendingStartNs = 0;   // (a repeat round starts videos from their beginning)
        playIndex(next);
        return true;
    }
    if (m_settings.autoNext && !m_settings.shuffle && !m_currentLocal.isEmpty()) {
        const QString f = nextInFolder(m_currentLocal);
        if (!f.isEmpty()) {
            int idx = m_playlist->items().indexOf(f);
            if (idx < 0) idx = m_playlist->addItems({f});
            playIndex(idx);
            return true;
        }
    }
    return false;
}

// ---- playlist files (.m3u, .m3u8) ----------------------------------------------------------

bool MainWindow::isPlaylistFile(const QString& path)
{
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QLatin1String("m3u") || ext == QLatin1String("m3u8");
}

// The entries of a playlist file: paths relative to the file become absolute, addresses
// (http, jellyfin) are kept, comments are skipped.
QStringList MainWindow::readPlaylistFile(const QString& path)
{
    QStringList out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > 8 * 1024 * 1024) return out;
    QByteArray data = f.readAll();
    if (data.startsWith("\xEF\xBB\xBF")) data.remove(0, 3);
    // .m3u8 is UTF-8; .m3u is whatever wrote it: UTF-8 when it is valid UTF-8, otherwise the system's encoding
    const QString text = QString::fromUtf8(data).contains(QChar(0xFFFD)) ? QString::fromLocal8Bit(data) : QString::fromUtf8(data);
    const QDir base = QFileInfo(path).absoluteDir();
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        if (line.contains(QStringLiteral("://"))) {
            const QUrl u(line);
            out << (u.isLocalFile() ? u.toLocalFile() : line);
        } else {
            line.replace('\\', '/');   // (lists written on Windows)
            out << QDir::cleanPath(QFileInfo(line).isAbsolute() ? line : base.absoluteFilePath(line));
        }
        if (out.size() >= 20000) break;
    }
    return out;
}

bool MainWindow::savePlaylistFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << "#EXTM3U\n";
    const QDir base = QFileInfo(path).absoluteDir();
    for (int i = 0; i < m_playlist->count(); ++i) {
        const QString item = m_playlist->at(i);
        ts << "#EXTINF:-1," << m_playlist->labelAt(i) << "\n";
        if (item.contains(QStringLiteral("://"))) { ts << item << "\n"; continue; }
        // Videos under the list's own folder are written relative to it, so the folder can be moved.
        const QString rel = base.relativeFilePath(item);
        ts << (rel.startsWith(QStringLiteral("..")) ? item : rel) << "\n";
    }
    return f.error() == QFile::NoError;
}

void MainWindow::savePlaylistDialog()
{
    if (m_playlist->count() == 0) { showOsd(tr("The playlist is empty")); return; }
    QString dir = m_settings.lastDir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) : m_settings.lastDir;
    QString f = QFileDialog::getSaveFileName(this, tr("Save playlist"), QDir(dir).filePath(tr("Playlist.m3u8")), tr("Playlists (*.m3u8 *.m3u)"));
    if (f.isEmpty()) return;
    if (QFileInfo(f).suffix().isEmpty()) f += QStringLiteral(".m3u8");
    showOsd(savePlaylistFile(f) ? tr("Playlist saved: %1").arg(QFileInfo(f).fileName()) : tr("Could not save the playlist there"), 3500);
}

// ---- sleep timer ---------------------------------------------------------------------------

void MainWindow::setSleepTimer(int minutes) { setSleepTimerSeconds(minutes < 0 ? -1 : minutes * 60); }

void MainWindow::setSleepTimerSeconds(int seconds)
{
    if (m_sleepFading) { m_player->setVolume(m_controls->volumeSlider()->value() / 100.0); m_sleepFading = false; }
    m_sleepAtEnd = seconds < 0;
    m_sleepAt = seconds > 0 ? QDateTime::currentMSecsSinceEpoch() + qint64(seconds) * 1000 : 0;
    m_sleepMinutes = seconds < 0 ? -1 : (seconds + 59) / 60;
    m_sleepWarned = seconds > 0 && seconds <= 60;
    if (m_sleepAt > 0) m_sleepTimer.start(); else m_sleepTimer.stop();
    updateSleepStatus();
    showOsd(m_sleepAtEnd ? tr("Sleep timer: at the end of this video")
            : seconds == 0 ? tr("Sleep timer off")
            : seconds >= 60 ? tr("Sleep timer: %1 minutes").arg((seconds + 30) / 60) : tr("Sleep timer: %1 seconds").arg(seconds));
}

void MainWindow::cycleSleepTimer()
{
    static const int steps[] = {0, 15, 30, 45, 60, 90, 120, -1};
    int i = 0;
    for (int k = 0; k < 8; ++k) if (steps[k] == m_sleepMinutes) i = k;
    if (m_sleepAt == 0 && !m_sleepAtEnd) i = 0;
    setSleepTimer(steps[(i + 1) % 8]);
}

int MainWindow::sleepRemainingSeconds() const
{
    return m_sleepAt > 0 ? int(std::max<qint64>(0, m_sleepAt - QDateTime::currentMSecsSinceEpoch() + 999) / 1000) : 0;
}

void MainWindow::updateSleepStatus()
{
    QString status;
    if (m_sleepAtEnd) status = tr("Stops when this video ends");
    else if (m_sleepAt > 0) {
        const int s = sleepRemainingSeconds();
        status = s >= 90 ? tr("Stops in %1 minutes").arg((s + 30) / 60) : tr("Stops in %1 seconds").arg(s);
    }
    m_playbackPanel->setSleepTimer(m_sleepAtEnd ? -1 : (m_sleepAt > 0 ? m_sleepMinutes : 0), status);
}

void MainWindow::sleepTick()
{
    if (m_sleepAt <= 0) { m_sleepTimer.stop(); return; }
    const qint64 left = m_sleepAt - QDateTime::currentMSecsSinceEpoch();
    if (left <= 60000 && !m_sleepWarned) {
        m_sleepWarned = true;
        showOsd(tr("Sleep timer: stopping in a minute (Shift+Z for longer)"), 5000);
    }
    const qint64 fade = 8000;   // the sound fades out over the last seconds
    if (left <= fade && left > 0) {
        m_sleepFading = true;
        const double k = double(left) / fade;
        m_player->setVolume(m_controls->volumeSlider()->value() / 100.0 * k * k);
    }
    if (left <= 0) goToSleep();
    else if ((left / 1000) % 5 == 0 || left < 90000) updateSleepStatus();
}

// The sleep timer has run out (or the video it was waiting for has ended).
void MainWindow::goToSleep()
{
    m_sleepTimer.stop();
    m_sleepAt = 0;
    m_sleepAtEnd = false;
    m_sleepMinutes = 0;
    if (tvOn()) setTvMode(false);
    else if (m_player->hasMedia()) m_player->pause();
    // Paused, the screen is free to dim and sleep again; the volume is back for next time.
    m_player->setVolume(m_controls->volumeSlider()->value() / 100.0);
    m_sleepFading = false;
    ++m_sleepCount;
    updateSleepStatus();
    showOsd(tr("Sleep timer: stopped"), 4000);
}

void MainWindow::showPlaylist(bool on) { m_playlistDock->setVisible(on); }

// ---- for the checks ------------------------------------------------------------------------

QJsonObject MainWindow::everydayReport() const
{
    QJsonObject o;
    o["subtitlesWanted"] = m_settings.subtitlesOn;
    o["subtitleLang"] = m_settings.subtitleLang;
    o["audioLang"] = m_settings.audioLang;
    const auto at = m_player->audioTracks();
    const auto st = m_player->subtitleTracks();
    const int ca = m_player->currentAudioTrack(), cs = m_player->currentSubtitleTrack();
    o["currentAudioLang"] = (ca >= 0 && ca < at.size()) ? at[ca].lang : QString();
    o["currentSubtitleLang"] = (cs >= 0 && cs < st.size()) ? st[cs].lang : QString();
    QJsonArray al, sl;
    for (const auto& t : at) al.append(t.lang);
    for (const auto& t : st) sl.append(t.lang);
    o["audioLangs"] = al;
    o["subtitleLangs"] = sl;
    o["audioDelayMs"] = m_player->audioDelay();
    o["subtitleDelayMs"] = m_player->subtitleDelay();
    {
        int a = 0, v = 0, t = 0;
        m_player->appliedOffsets(&a, &v, &t);
        o["audioSinkOffsetMs"] = a; o["videoSinkOffsetMs"] = v; o["textOffsetMs"] = t;
    }
    o["soundLevelDb"] = m_player->soundLevelDb();
    o["soundPitchHz"] = m_player->soundPitchHz();
    const SubtitleStyle ss = m_player->subtitleStyle();
    o["subtitleStyle"] = QJsonArray{ss.size, ss.color, ss.background, ss.position};
    o["nightMode"] = m_settings.nightMode;
    o["deinterlace"] = m_settings.deinterlace;
    o["deinterlacing"] = m_player->deinterlacing();
    o["shuffle"] = m_settings.shuffle;
    o["repeat"] = m_settings.repeatMode;
    o["autoNext"] = m_settings.autoNext;
    o["sleepSeconds"] = sleepRemainingSeconds();
    o["sleepAtEnd"] = m_sleepAtEnd;
    o["sleepCount"] = m_sleepCount;
    o["volume"] = m_player->volume();
    QJsonArray items;
    for (const QString& p : m_playlist->items()) items.append(QFileInfo(p).fileName().isEmpty() ? p : QFileInfo(p).fileName());
    o["playlist"] = items;
    o["playlistIndex"] = m_playlist->currentIndex();
    o["played"] = QJsonArray::fromStringList(m_playLog);
    return o;
}

// A GIF or a cut in progress steps the video itself: no frame refreshes meanwhile.
bool MainWindow::m_gifBusy() const
{
    return (m_gifDialog && m_gifDialog->isBusy()) || (m_cutDialog && m_cutDialog->isBusy());
}
