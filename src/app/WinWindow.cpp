#include "WinWindow.h"

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <QWidget>
#include <QWindow>

namespace {
// Read by Qt's Windows plugin when it makes the native window (QWindowsWindow).
const char kQtBorder[] = "_q_has_border_in_fullscreen";
// Ours: set when Qt was given the request in time for this window.
const char kInTime[] = "crt_border_in_fullscreen";

bool borderWanted() { return qgetenv("CRTPLAYER_FULLSCREEN_BORDER") != "0"; }

QJsonObject rectJson(const RECT& r)
{
    return {{"x", int(r.left)}, {"y", int(r.top)}, {"w", int(r.right - r.left)}, {"h", int(r.bottom - r.top)}};
}
} // namespace

bool WinWindow::borderInFullScreen() { return borderWanted(); }

void WinWindow::keepComposed(QWidget* w)
{
    if (!w || !w->isWindow() || !borderWanted()) return;
    // The window object now (with every child already in place, so that it is made for OpenGL
    // from the start); the native window itself still comes with the first show.
    w->setAttribute(Qt::WA_NativeWindow);
    QWindow* h = w->windowHandle();
    if (!h || h->property(kQtBorder).toBool()) return;
    h->setProperty(kInTime, h->handle() == nullptr);
    h->setProperty(kQtBorder, true);
}

bool WinWindow::setTopmost(QWidget* w, bool on)
{
    if (!w || !w->isWindow() || !w->internalWinId()) return false;
    SetWindowPos(HWND(w->internalWinId()), on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

QJsonObject WinWindow::report(const QWidget* w)
{
    QJsonObject o;
    if (!w || !w->isWindow() || !w->internalWinId()) return o;
    const HWND hwnd = HWND(w->internalWinId());
    const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR ex = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    RECT wr{}, cr{}, mr{};
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    POINT tl{cr.left, cr.top}, br{cr.right, cr.bottom};
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    const RECT inside{tl.x, tl.y, br.x, br.y};
    MONITORINFO mi{};
    mi.cbSize = sizeof mi;
    if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) mr = mi.rcMonitor;
    o["visible"] = bool(IsWindowVisible(hwnd));
    o["style"] = QString::number(qulonglong(style) & 0xffffffffu, 16);
    o["exStyle"] = QString::number(qulonglong(ex) & 0xffffffffu, 16);
    o["border"] = bool(style & WS_BORDER);
    o["layered"] = bool(ex & WS_EX_LAYERED);
    o["topmost"] = bool(ex & WS_EX_TOPMOST);
    o["windowRect"] = rectJson(wr);
    o["insideRect"] = rectJson(inside);
    o["monitorRect"] = rectJson(mr);
    // What takes a window out of the desktop's composition: its inside is exactly the screen.
    o["insideIsWholeScreen"] = bool(EqualRect(&inside, &mr));
    o["windowIsWholeScreen"] = bool(EqualRect(&wr, &mr));
    if (HRGN rgn = CreateRectRgn(0, 0, 0, 0)) {
        RECT box{};
        const int kind = GetWindowRgn(hwnd, rgn);
        o["shaped"] = kind != ERROR && kind != NULLREGION;
        if (kind != ERROR && GetRgnBox(rgn, &box) != ERROR) o["shapeBox"] = rectJson(box);
        DeleteObject(rgn);
    }
    if (HDC dc = GetDC(hwnd)) {
        // (0 when OpenGL comes from a library that keeps the pixel format to itself, as in the tests.)
        const int pf = GetPixelFormat(dc);
        PIXELFORMATDESCRIPTOR pfd{};
        o["pixelFormat"] = pf;
        if (pf > 0 && DescribePixelFormat(dc, pf, sizeof pfd, &pfd)) o["alphaBits"] = int(pfd.cAlphaBits);
        ReleaseDC(hwnd, dc);
    }
    if (const QWindow* h = w->windowHandle()) {
        o["openGl"] = h->surfaceType() == QSurface::OpenGLSurface;
        o["alphaAsked"] = h->requestedFormat().alphaBufferSize();
        o["alphaGot"] = h->format().alphaBufferSize();
        o["borderAsked"] = h->property(kQtBorder).toBool();
        o["borderAskedInTime"] = h->property(kInTime).toBool();
    }
    return o;
}

#else

bool WinWindow::borderInFullScreen() { return false; }
void WinWindow::keepComposed(QWidget*) {}
bool WinWindow::setTopmost(QWidget*, bool) { return false; }
QJsonObject WinWindow::report(const QWidget*) { return {}; }

#endif
