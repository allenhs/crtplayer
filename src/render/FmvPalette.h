#pragma once
#include <QColor>
#include <QRect>
#include <QSize>
#include <QVector>

// Colour for the "FMV console" looks (pure logic, unit-tested).

// The Mega Drive / Sega CD shows 3 bits per channel: 512 colours. These are the eight
// output levels of its video DAC (not evenly spaced), as 8-bit values.
extern const int kMegaDriveLevels[8];
int megaDriveLevel(int value8);   // index 0..7 of the nearest level

// Picks up to `colors` colours for one frame from the console's 512: a median cut over the
// frame's colours on the 3-bit grid. Every returned colour is on the grid. `rgba` is
// width*height pixels of 4 bytes (R, G, B, A), rows `stride` bytes apart.
QVector<QRgb> fmvPalette(const uchar* rgba, int width, int height, int stride, int colors);
// How far the frame's pixels are from their nearest colour in `palette`: the root of the
// mean squared distance, in 8-bit steps (0: every pixel has its own colour in the palette).
double fmvPaletteError(const uchar* rgba, int width, int height, int stride, const QVector<QRgb>& palette);
// The palette to show a frame with when the frame before was shown with `previous`: the
// previous one for as long as it serves the frame nearly as well as `fresh` (made for this
// frame) does, so that areas which do not change keep their colours from frame to frame.
bool fmvKeepPalette(double previousError, double freshError);

// The picture grid of the Sega CD look for a picture of the given display aspect:
// `rows` rows (224 when 0) and, with `columns` 0, as many columns as give the console's
// 8:7-wide pixels (256 for a 4:3 picture). Both are multiples of 4 (the codec's blocks).
QSize fmvGridSize(double displayAspect, int rows, int columns);
// The video's window inside that grid for a share of the screen (0.5..1), centred, in
// whole blocks.
QRect fmvWindowRect(const QSize& grid, double window);
