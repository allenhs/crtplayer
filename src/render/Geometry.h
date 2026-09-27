#pragma once
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

// Picture scaling. None of the modes distort the image: the aspect ratio always comes
// from the stream's display aspect (or an explicit user override).
enum class ScaleMode { Original = 0, Fit = 1, Fill = 2, Crop = 3 };
QString scaleModeName(ScaleMode m);

// Orientation metadata (GStreamer "image-orientation" tag). rotation is clockwise.
struct Orientation {
    int rotation = 0;   // 0, 90, 180, 270
    bool flip = false;  // horizontal mirror applied before the rotation
    bool operator==(const Orientation& o) const { return rotation == o.rotation && flip == o.flip; }
    bool operator!=(const Orientation& o) const { return !(*this == o); }
};
Orientation orientationFromTag(const QString& tag);
QString orientationToString(const Orientation& o);
bool orientationSwapsAxes(const Orientation& o);

// Maps oriented image uv (top-left origin) to storage uv: su = r0.x*u + r0.y*v + r0.z, sv likewise with r1.
void orientationMatrix(const Orientation& o, float r0[3], float r1[3]);

struct SourceFormat {
    int width = 0, height = 0;   // coded frame size in storage pixels
    int parN = 1, parD = 1;      // pixel aspect ratio
    Orientation orient;
    bool isValid() const { return width > 0 && height > 0 && parN > 0 && parD > 0; }
};

// Size of the picture in square pixels after PAR correction and rotation.
// aspectOverride > 0 replaces the display aspect ratio (width/height).
QSizeF displaySize(const SourceFormat& f, double aspectOverride = 0.0);
// Storage-pixel size after rotation (the resolution the renderer works at).
QSize orientedStorageSize(const SourceFormat& f);
double displayAspect(const SourceFormat& f);

struct CropFractions {
    double left = 0, top = 0, right = 0, bottom = 0; // fractions of the oriented picture
    bool isNull() const { return left == 0 && top == 0 && right == 0 && bottom == 0; }
};
CropFractions clampCrop(const CropFractions& c);
// Symmetric crop that turns a picture of size disp into targetAspect (w/h).
CropFractions cropForAspect(const QSizeF& disp, double targetAspect);

struct LayoutResult {
    bool valid = false;
    QRectF imageRect;    // whole (possibly partly off-screen) picture rect, in target pixels
    QRectF visibleRect;  // imageRect clipped to the video area
    QRectF srcRect;      // normalized [0,1] picture region shown in visibleRect
    double scale = 1.0;  // target pixels per display pixel
};
// area: the video area in target (device) pixels. The picture is centred in it.
LayoutResult computeLayout(const QSizeF& disp, ScaleMode mode, const CropFractions& crop, const QRectF& area);

// Size of the lowered-resolution picture (in oriented-image texels) for "bigger pixels".
// rows = 0, or rows >= the source's rows, means native (returns an empty size).
// columns = 0 picks square pixels at the picture's display aspect, so a 16:9 video at
// 240 rows becomes 427x240, and a 32:27-PAR DVD frame also becomes 427x240.
QSize pixelatedSize(const SourceFormat& f, double aspectOverride, int rows, int columns);

// Number of simulated scanlines when "auto" is selected: the visible source lines,
// halved (line-doubling in reverse) until each line covers at least minPx target pixels
// (2.5 px keeps 240-line SD content at 240 lines on a 720p-sized window).
double autoScanlineCount(double visibleSourceLines, double tubeHeightPx, double minPx = 2.5);
