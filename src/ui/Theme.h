#pragma once
#include <QColor>
class QApplication;

// "Phosphor" palette: graphite glass with a warm amber accent, chosen so the video
// stays the brightest, most saturated thing on screen.
namespace Theme {
QColor background();   // window / docks
QColor surface();      // panels
QColor raised();       // inputs, hover
QColor text();
QColor muted();
QColor accent();
void apply(QApplication& app);
}
