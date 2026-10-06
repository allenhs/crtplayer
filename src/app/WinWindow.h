#pragma once
#include <QJsonObject>

class QWidget;

// Windows only: on other systems these do nothing and report nothing.
namespace WinWindow {

// Menus and drop-down lists open at once, without Windows' slide and fade (which show a
// stand-in picture of the list first, in a window of its own). Call once, after QApplication.
void plainPopups();

// A window drawn with OpenGL that covers a whole screen is taken out of the desktop's
// composition by Windows: menus and lists opened over it are there but not seen, and a
// see-through window shows black instead of the desktop. One pixel of border in full screen
// keeps it composed (Qt's own remedy for this). Call before the window is first shown.
void keepComposed(QWidget* topLevel);

// After showing the window full screen: puts that border back if a change of the window's
// flags took it away.
void fullScreenShown(QWidget* topLevel);

// The native window as Windows has it (styles and rectangles), for the tests.
QJsonObject report(const QWidget* topLevel);

} // namespace WinWindow
