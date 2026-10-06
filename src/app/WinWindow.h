#pragma once
#include <QJsonObject>

class QWidget;

// Windows only: on other systems these do nothing and report nothing.
//
// A window drawn with OpenGL whose inside covers a whole screen is taken out of the desktop's
// composition by Windows: menus and lists opened over it are there but not seen, and a
// see-through window shows black instead of the desktop. Qt's remedy is one pixel of border
// around the window while it is full screen, so that its inside is not the whole screen.
namespace WinWindow {

// False when the remedy is switched off (CRTPLAYER_FULLSCREEN_BORDER=0) and on other systems.
bool borderInFullScreen();

// Asks Qt for that border. Call before the window is first shown, with its children in place.
// (Qt gives no border to a window with Qt::FramelessWindowHint: such a window must do without
// that flag, which changes nothing in full screen.)
void keepComposed(QWidget* topLevel);

// "Always on top" for a window that is already made, without a change of window flags (which
// would make Qt set the window's styles anew, as for a window that is not full screen).
// False where that does not apply: then the flag is the way.
bool setTopmost(QWidget* topLevel, bool on);

// The native window as Windows has it (styles and rectangles), for the tests.
QJsonObject report(const QWidget* topLevel);

} // namespace WinWindow
