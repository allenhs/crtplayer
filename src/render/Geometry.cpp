#include "Geometry.h"
#include <algorithm>
#include <cmath>

QString scaleModeName(ScaleMode m)
{
    switch (m) {
    case ScaleMode::Original: return "Original size";
    case ScaleMode::Fit: return "Fit";
    case ScaleMode::Fill: return "Fill";
    case ScaleMode::Crop: return "Crop";
    }
    return {};
}

Orientation orientationFromTag(const QString& tagIn)
{
    Orientation o;
    QString tag = tagIn.trimmed().toLower();
    if (tag.startsWith("flip-")) { o.flip = true; tag = tag.mid(5); }
    if (tag == "rotate-90") o.rotation = 90;
    else if (tag == "rotate-180") o.rotation = 180;
    else if (tag == "rotate-270") o.rotation = 270;
    else o.rotation = 0;
    return o;
}

QString orientationToString(const Orientation& o)
{
    QString s = QString::number(o.rotation) + QStringLiteral("°");
    if (o.flip) s += " + mirror";
    return s;
}

bool orientationSwapsAxes(const Orientation& o) { return o.rotation == 90 || o.rotation == 270; }

void orientationMatrix(const Orientation& o, float r0[3], float r1[3])
{
    // Inverse mapping oriented uv -> (unflipped) storage uv. See docs/PIPELINE.md.
    switch (o.rotation) {
    case 90:  r0[0] = 0;  r0[1] = 1;  r0[2] = 0; r1[0] = -1; r1[1] = 0;  r1[2] = 1; break;
    case 180: r0[0] = -1; r0[1] = 0;  r0[2] = 1; r1[0] = 0;  r1[1] = -1; r1[2] = 1; break;
    case 270: r0[0] = 0;  r0[1] = -1; r0[2] = 1; r1[0] = 1;  r1[1] = 0;  r1[2] = 0; break;
    default:  r0[0] = 1;  r0[1] = 0;  r0[2] = 0; r1[0] = 0;  r1[1] = 1;  r1[2] = 0; break;
    }
    if (o.flip) { r0[0] = -r0[0]; r0[1] = -r0[1]; r0[2] = 1.0f - r0[2]; }
}

double displayAspect(const SourceFormat& f)
{
    if (!f.isValid()) return 0;
    double a = double(f.width) * f.parN / (double(f.height) * f.parD);
    return orientationSwapsAxes(f.orient) ? 1.0 / a : a;
}

QSizeF displaySize(const SourceFormat& f, double aspectOverride)
{
    if (!f.isValid()) return {};
    double w = double(f.width) * f.parN / f.parD;
    double h = f.height;
    if (orientationSwapsAxes(f.orient)) std::swap(w, h);
    if (aspectOverride > 0.0) {
        // Keep the pixel count comparable: preserve height, derive width.
        w = h * aspectOverride;
    }
    return {w, h};
}

QSize orientedStorageSize(const SourceFormat& f)
{
    if (!f.isValid()) return {};
    return orientationSwapsAxes(f.orient) ? QSize(f.height, f.width) : QSize(f.width, f.height);
}

CropFractions clampCrop(const CropFractions& in)
{
    CropFractions c = in;
    auto cl = [](double v) { return std::clamp(std::isfinite(v) ? v : 0.0, 0.0, 0.45); };
    c.left = cl(c.left); c.right = cl(c.right); c.top = cl(c.top); c.bottom = cl(c.bottom);
    return c;
}

CropFractions cropForAspect(const QSizeF& disp, double target)
{
    CropFractions c;
    if (disp.isEmpty() || target <= 0) return c;
    const double a = disp.width() / disp.height();
    if (a > target) {           // too wide: trim left/right
        double keep = target / a;
        c.left = c.right = (1.0 - keep) / 2.0;
    } else if (a < target) {    // too tall: trim top/bottom
        double keep = a / target;
        c.top = c.bottom = (1.0 - keep) / 2.0;
    }
    return clampCrop(c);
}

LayoutResult computeLayout(const QSizeF& disp, ScaleMode mode, const CropFractions& cropIn, const QRectF& area)
{
    LayoutResult r;
    if (disp.isEmpty() || area.isEmpty()) return r;
    const CropFractions crop = mode == ScaleMode::Crop ? clampCrop(cropIn) : CropFractions{};
    const QRectF src(crop.left, crop.top, 1.0 - crop.left - crop.right, 1.0 - crop.top - crop.bottom);
    const double cw = disp.width() * src.width();
    const double ch = disp.height() * src.height();
    const double fit = std::min(area.width() / cw, area.height() / ch);
    const double fill = std::max(area.width() / cw, area.height() / ch);
    double s = fit;
    if (mode == ScaleMode::Fill) s = fill;
    else if (mode == ScaleMode::Original) s = std::min(1.0, fit); // 1:1, shrink only if it doesn't fit
    const double iw = cw * s, ih = ch * s;
    r.imageRect = QRectF(area.center().x() - iw / 2.0, area.center().y() - ih / 2.0, iw, ih);
    r.visibleRect = r.imageRect.intersected(area);
    if (r.visibleRect.isEmpty()) return r;
    const double x0 = (r.visibleRect.left() - r.imageRect.left()) / iw;
    const double y0 = (r.visibleRect.top() - r.imageRect.top()) / ih;
    const double x1 = (r.visibleRect.right() - r.imageRect.left()) / iw;
    const double y1 = (r.visibleRect.bottom() - r.imageRect.top()) / ih;
    r.srcRect = QRectF(src.left() + x0 * src.width(), src.top() + y0 * src.height(),
                       (x1 - x0) * src.width(), (y1 - y0) * src.height());
    r.scale = s;
    r.valid = true;
    return r;
}

QSize pixelatedSize(const SourceFormat& f, double aspectOverride, int rows, int columns)
{
    if (!f.isValid() || rows <= 0) return {};
    const QSize os = orientedStorageSize(f);
    if (rows >= os.height() && (columns <= 0 || columns >= os.width())) return {};
    const int h = std::min(rows, os.height());
    const QSizeF ds = displaySize(f, aspectOverride);
    int w = columns > 0 ? columns : int(std::lround(h * ds.width() / ds.height()));
    w = std::clamp(w, 1, std::max(1, os.width()));
    return {w, h};
}

double autoScanlineCount(double lines, double tubeHeightPx, double minPx)
{
    if (lines <= 0 || tubeHeightPx <= 0) return 0;
    while (lines > 16 && tubeHeightPx / lines < minPx) lines /= 2.0;
    return lines;
}
