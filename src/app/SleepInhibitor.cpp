#include "SleepInhibitor.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QVariantMap>
#endif

namespace {
[[maybe_unused]] const char* kApp = "CRT Player";
[[maybe_unused]] const char* kReason = "Playing a video";
}

SleepInhibitor::SleepInhibitor(QObject* parent) : QObject(parent) {}
SleepInhibitor::~SleepInhibitor() { release(); }

void SleepInhibitor::setActive(bool on)
{
    if (on == m_active) return;
    m_active = on;
    if (on) inhibit(); else release();
}

#ifndef _WIN32
void SleepInhibitor::inhibit()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    {   // screen dimming, blanking and locking
        QDBusInterface ss("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", bus);
        ss.setTimeout(1500);
        if (ss.isValid()) {
            QDBusReply<quint32> r = ss.call("Inhibit", QString::fromLatin1(kApp), QString::fromLatin1(kReason));
            if (r.isValid()) { m_ssCookie = r.value(); m_ss = true; }
        }
    }
    {   // automatic suspend (KDE)
        QDBusInterface pm("org.freedesktop.PowerManagement", "/org/freedesktop/PowerManagement/Inhibit",
                          "org.freedesktop.PowerManagement.Inhibit", bus);
        pm.setTimeout(1500);
        if (pm.isValid()) {
            QDBusReply<quint32> r = pm.call("Inhibit", QString::fromLatin1(kApp), QString::fromLatin1(kReason));
            if (r.isValid()) { m_pmCookie = r.value(); m_pm = true; }
        }
    }
    if (!m_ss) {   // desktop portal (GNOME and others): 4 = suspend, 8 = idle
        QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Inhibit", bus);
        portal.setTimeout(1500);
        if (portal.isValid()) {
            QDBusReply<QDBusObjectPath> r = portal.call("Inhibit", QString(), quint32(4 | 8),
                                                        QVariantMap{{"reason", QString::fromLatin1(kReason)}});
            if (r.isValid()) m_portalHandle = r.value().path();
        }
    }
}

void SleepInhibitor::release()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_ss) {
        QDBusInterface ss("org.freedesktop.ScreenSaver", "/org/freedesktop/ScreenSaver", "org.freedesktop.ScreenSaver", bus);
        ss.setTimeout(1500);
        ss.call("UnInhibit", m_ssCookie);
        m_ss = false;
    }
    if (m_pm) {
        QDBusInterface pm("org.freedesktop.PowerManagement", "/org/freedesktop/PowerManagement/Inhibit",
                          "org.freedesktop.PowerManagement.Inhibit", bus);
        pm.setTimeout(1500);
        pm.call("UnInhibit", m_pmCookie);
        m_pm = false;
    }
    if (!m_portalHandle.isEmpty()) {
        QDBusInterface req("org.freedesktop.portal.Desktop", m_portalHandle, "org.freedesktop.portal.Request", bus);
        req.setTimeout(1500);
        req.call("Close");
        m_portalHandle.clear();
    }
}

QString SleepInhibitor::method() const
{
    QStringList m;
    if (m_ss) m << "ScreenSaver";
    if (m_pm) m << "PowerManagement";
    if (!m_portalHandle.isEmpty()) m << "portal";
    return m.isEmpty() ? QStringLiteral("none") : m.join('+');
}

#else   // Windows: one call keeps the display on and the PC awake; cleared on release.

void SleepInhibitor::inhibit()
{
    m_winActive = SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED) != 0;
}

void SleepInhibitor::release()
{
    if (m_winActive) SetThreadExecutionState(ES_CONTINUOUS);
    m_winActive = false;
}

QString SleepInhibitor::method() const
{
    return m_winActive ? QStringLiteral("SetThreadExecutionState") : QStringLiteral("none");
}
#endif
