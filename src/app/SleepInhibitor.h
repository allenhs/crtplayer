#pragma once
#include <QObject>
#include <QString>

// Keeps the screen from dimming, locking or sleeping while a video plays, through the
// standard D-Bus interfaces: org.freedesktop.ScreenSaver (KDE and others) plus
// org.freedesktop.PowerManagement.Inhibit (KDE: no automatic suspend), or the desktop
// portal's Inhibit (GNOME and others) when those are not there. On Windows:
// SetThreadExecutionState. Released on pause/stop.
class SleepInhibitor : public QObject {
    Q_OBJECT
public:
    explicit SleepInhibitor(QObject* parent = nullptr);
    ~SleepInhibitor() override;
    void setActive(bool on);
    bool active() const { return m_active; }
    QString method() const;   // which interfaces hold the inhibition now, or "none"
private:
    void inhibit();
    void release();
    bool m_active = false;
    quint32 m_ssCookie = 0, m_pmCookie = 0;
    bool m_ss = false, m_pm = false;
    QString m_portalHandle;
    bool m_winActive = false;   // Windows: SetThreadExecutionState is holding the display and system awake
};
