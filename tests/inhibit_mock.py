#!/usr/bin/env python3
"""Stand-ins for the desktop's "don't sleep" services, logging every call (JSON lines).
mode "kde":    org.freedesktop.ScreenSaver + org.freedesktop.PowerManagement(.Inhibit)
mode "portal": org.freedesktop.portal.Desktop (Inhibit, returning a Request object)
Usage: inhibit_mock.py MODE LOGFILE   (on the session bus in DBUS_SESSION_BUS_ADDRESS)"""
import json, sys, itertools
import dbus, dbus.service, dbus.mainloop.glib
from gi.repository import GLib

mode, logfile = sys.argv[1], sys.argv[2]
cookies = itertools.count(1001)
def log(**e):
    with open(logfile, 'a') as f: f.write(json.dumps(e) + '\n')

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()

class ScreenSaver(dbus.service.Object):
    @dbus.service.method('org.freedesktop.ScreenSaver', in_signature='ss', out_signature='u')
    def Inhibit(self, app, reason):
        c = next(cookies); log(iface='ScreenSaver', call='Inhibit', app=str(app), reason=str(reason), cookie=c); return dbus.UInt32(c)
    @dbus.service.method('org.freedesktop.ScreenSaver', in_signature='u')
    def UnInhibit(self, cookie):
        log(iface='ScreenSaver', call='UnInhibit', cookie=int(cookie))

class PowerManagement(dbus.service.Object):
    @dbus.service.method('org.freedesktop.PowerManagement.Inhibit', in_signature='ss', out_signature='u')
    def Inhibit(self, app, reason):
        c = next(cookies); log(iface='PowerManagement', call='Inhibit', cookie=c); return dbus.UInt32(c)
    @dbus.service.method('org.freedesktop.PowerManagement.Inhibit', in_signature='u')
    def UnInhibit(self, cookie):
        log(iface='PowerManagement', call='UnInhibit', cookie=int(cookie))

class Request(dbus.service.Object):
    def __init__(self, b, path):
        super().__init__(b, path); self.path = path
    @dbus.service.method('org.freedesktop.portal.Request')
    def Close(self):
        log(iface='portal', call='Close', handle=self.path); self.remove_from_connection()

class Portal(dbus.service.Object):
    n = itertools.count(1)
    @dbus.service.method('org.freedesktop.portal.Inhibit', in_signature='sua{sv}', out_signature='o')
    def Inhibit(self, window, flags, options):
        path = f'/org/freedesktop/portal/desktop/request/test/r{next(self.n)}'
        Request(bus, path)
        log(iface='portal', call='Inhibit', flags=int(flags), reason=str(options.get('reason', '')), handle=path)
        return dbus.ObjectPath(path)

names = []
if mode == 'kde':
    names.append(dbus.service.BusName('org.freedesktop.ScreenSaver', bus))
    ScreenSaver(bus, '/org/freedesktop/ScreenSaver')
    names.append(dbus.service.BusName('org.freedesktop.PowerManagement', bus))
    PowerManagement(bus, '/org/freedesktop/PowerManagement/Inhibit')
else:
    names.append(dbus.service.BusName('org.freedesktop.portal.Desktop', bus))
    Portal(bus, '/org/freedesktop/portal/desktop')
log(ready=True, mode=mode)
GLib.MainLoop().run()
