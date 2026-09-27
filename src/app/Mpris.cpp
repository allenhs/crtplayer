#include "Mpris.h"
#include "app/MainWindow.h"
#include "playback/Player.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTimer>

static const char* kPath = "/org/mpris/MediaPlayer2";

MprisRoot::MprisRoot(QObject* parent, MainWindow* w) : QDBusAbstractAdaptor(parent), m_w(w) {}
bool MprisRoot::fullscreen() const { return m_w->isFullscreenMode(); }
void MprisRoot::setFullscreen(bool on) { m_w->remoteAction("fullscreen", on ? 1 : 0); }
QStringList MprisRoot::mimeTypes() const
{
    return {"video/mp4", "video/x-matroska", "video/webm", "video/quicktime", "video/x-msvideo", "video/mpeg",
            "video/ogg", "video/x-flv", "video/3gpp", "video/mp2t"};
}
void MprisRoot::Raise() { m_w->remoteAction("raise"); }
void MprisRoot::Quit() { m_w->remoteAction("quit"); }

MprisPlayer::MprisPlayer(QObject* parent, MainWindow* w, Player* p) : QDBusAbstractAdaptor(parent), m_w(w), m_p(p) {}
QString MprisPlayer::playbackStatus() const
{
    if (!m_p->hasMedia()) return QStringLiteral("Stopped");
    return m_p->isPlaying() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}
double MprisPlayer::rate() const { return m_p->rate(); }
void MprisPlayer::setRate(double r) { if (r > 0) m_w->remoteAction("rate", r); }
double MprisPlayer::volume() const { return m_w->volume01(); }
void MprisPlayer::setVolume(double v) { m_w->remoteAction("volume", v); }
qlonglong MprisPlayer::position() const { return m_p->hasMedia() ? m_p->position() / 1000 : 0; }
bool MprisPlayer::canGoNext() const { return m_w->playlistIndex() + 1 < m_w->playlistCount(); }
bool MprisPlayer::canGoPrevious() const { return m_w->playlistIndex() > 0; }
bool MprisPlayer::canPlay() const { return m_p->hasMedia() || m_w->playlistCount() > 0; }
bool MprisPlayer::canSeek() const { return m_p->hasMedia() && m_p->duration() > 0; }
QDBusObjectPath MprisPlayer::trackId() const
{
    return QDBusObjectPath(QStringLiteral("/io/github/crtplayer/track/%1").arg(std::max(0, m_w->playlistIndex())));
}
QVariantMap MprisPlayer::metadata() const
{
    QVariantMap m;
    if (!m_p->hasMedia()) return m;
    m.insert("mpris:trackid", QVariant::fromValue(trackId()));
    if (m_p->duration() > 0) m.insert("mpris:length", qlonglong(m_p->duration() / 1000));
    m.insert("xesam:title", m_w->mediaTitle());
    // Only local files get a URL: a network stream's address stays inside the app.
    if (!m_w->currentLocalFile().isEmpty()) m.insert("xesam:url", QUrl::fromLocalFile(m_w->currentLocalFile()).toString());
    return m;
}
void MprisPlayer::Next() { m_w->remoteAction("next"); }
void MprisPlayer::Previous() { m_w->remoteAction("prev"); }
void MprisPlayer::Pause() { m_w->remoteAction("pause"); }
void MprisPlayer::PlayPause() { m_w->remoteAction("playpause"); }
void MprisPlayer::Stop() { m_w->remoteAction("stop"); }
void MprisPlayer::Play() { m_w->remoteAction("play"); }
void MprisPlayer::Seek(qlonglong offsetUs) { if (canSeek()) m_w->remoteAction("seekrel", double(offsetUs) * 1000.0); }
void MprisPlayer::SetPosition(const QDBusObjectPath& track, qlonglong positionUs)
{
    // Per the spec: ignored unless it is for the current track and within its length.
    if (track.path() != trackId().path() || positionUs < 0 || positionUs * 1000 > m_p->duration()) return;
    m_w->remoteAction("seekabs", double(positionUs) * 1000.0);
}
void MprisPlayer::OpenUri(const QString& uri) { m_w->openFiles({uri}); }

Mpris::Mpris(MainWindow* w, Player* p) : QObject(w), m_w(w), m_p(p)
{
    new MprisRoot(this, w);
    m_player = new MprisPlayer(this, w, p);
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    // One instance takes the plain name; further ones add ".instance<pid>" (as the spec allows).
    m_service = QStringLiteral("org.mpris.MediaPlayer2.crtplayer");
    if (!bus.registerService(m_service)) {
        m_service += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
        if (!bus.registerService(m_service)) return;
    }
    m_registered = bus.registerObject(kPath, this, QDBusConnection::ExportAdaptors);
    // Announce changes: state, track, volume, rate and capabilities, polled cheaply.
    auto* t = new QTimer(this);
    t->setInterval(250);
    connect(t, &QTimer::timeout, this, &Mpris::refresh);
    t->start();
    connect(p, &Player::stateChanged, this, &Mpris::refresh);
    connect(p, &Player::mediaLoaded, this, &Mpris::refresh);
}

void Mpris::refresh()
{
    if (!m_registered) return;
    QVariantMap now{{"PlaybackStatus", m_player->playbackStatus()}, {"Metadata", m_player->metadata()},
                    {"Volume", m_player->volume()}, {"Rate", m_player->rate()},
                    {"CanGoNext", m_player->canGoNext()}, {"CanGoPrevious", m_player->canGoPrevious()},
                    {"CanPlay", m_player->canPlay()}, {"CanPause", m_player->canPause()}, {"CanSeek", m_player->canSeek()}};
    QVariantMap changed;
    for (auto it = now.begin(); it != now.end(); ++it)
        if (m_last.value(it.key()) != it.value()) changed.insert(it.key(), it.value());
    m_last = now;
    if (!changed.isEmpty()) emitChanged(QStringLiteral("org.mpris.MediaPlayer2.Player"), changed);
}

void Mpris::notifySeeked()
{
    if (m_registered) emit m_player->Seeked(m_player->position());
}

void Mpris::emitChanged(const QString& iface, const QVariantMap& props)
{
    QDBusMessage msg = QDBusMessage::createSignal(kPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << iface << props << QStringList();
    QDBusConnection::sessionBus().send(msg);
}
