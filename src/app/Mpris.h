#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

class MainWindow;
class Player;

// MPRIS (org.mpris.MediaPlayer2) on the session bus, as "org.mpris.MediaPlayer2.crtplayer":
// media keys, KDE's media widget and lock-screen controls, and KDE Connect.
class MprisRoot : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool Fullscreen READ fullscreen WRITE setFullscreen)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ uriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)
public:
    MprisRoot(QObject* parent, MainWindow* w);
    bool canQuit() const { return true; }
    bool canRaise() const { return true; }
    bool canSetFullscreen() const { return true; }
    bool fullscreen() const;
    void setFullscreen(bool on);
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("CRT Player"); }
    QString desktopEntry() const { return QStringLiteral("io.github.crtplayer"); }
    QStringList uriSchemes() const { return {"file", "http", "https"}; }
    QStringList mimeTypes() const;
public slots:
    void Raise();
    void Quit();
private:
    MainWindow* m_w;
};

class MprisPlayer : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    MprisPlayer(QObject* parent, MainWindow* w, Player* p);
    QString playbackStatus() const;
    QString loopStatus() const { return QStringLiteral("None"); }
    void setLoopStatus(const QString&) {}
    double rate() const;
    void setRate(double r);
    bool shuffle() const { return false; }
    void setShuffle(bool) {}
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double v);
    qlonglong position() const;
    double minimumRate() const { return 0.25; }
    double maximumRate() const { return 4.0; }
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const { return canPlay(); }
    bool canSeek() const;
    bool canControl() const { return true; }
    QDBusObjectPath trackId() const;
public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offsetUs);
    void SetPosition(const QDBusObjectPath& track, qlonglong positionUs);
    void OpenUri(const QString& uri);
signals:
    void Seeked(qlonglong positionUs);
private:
    MainWindow* m_w;
    Player* m_p;
};

// Owns the adaptors, registers the service and announces property changes.
class Mpris : public QObject {
    Q_OBJECT
public:
    Mpris(MainWindow* w, Player* p);
    bool isRegistered() const { return m_registered; }
    QString serviceName() const { return m_service; }
    void notifySeeked();
private:
    void emitChanged(const QString& iface, const QVariantMap& props);
    void refresh();
    MainWindow* m_w;
    Player* m_p;
    MprisPlayer* m_player = nullptr;
    bool m_registered = false;
    QString m_service;
    QVariantMap m_last;   // last announced Player properties
    int m_track = 0;
};
