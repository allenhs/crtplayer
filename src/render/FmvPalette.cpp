#include "FmvPalette.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

const int kMegaDriveLevels[8] = {0, 52, 87, 116, 144, 172, 206, 255};

int megaDriveLevel(int v)
{
    int best = 0, bestD = 1 << 30;
    for (int i = 0; i < 8; ++i) {
        const int d = std::abs(kMegaDriveLevels[i] - v);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

namespace {
struct Box { int lo[3], hi[3]; quint64 count; };

void shrink(Box& b, const std::array<quint32, 512>& hist)
{
    int lo[3] = {8, 8, 8}, hi[3] = {-1, -1, -1};
    quint64 n = 0;
    for (int r = b.lo[0]; r <= b.hi[0]; ++r)
        for (int g = b.lo[1]; g <= b.hi[1]; ++g)
            for (int bl = b.lo[2]; bl <= b.hi[2]; ++bl) {
                const quint32 c = hist[(r << 6) | (g << 3) | bl];
                if (!c) continue;
                n += c;
                const int v[3] = {r, g, bl};
                for (int k = 0; k < 3; ++k) { lo[k] = std::min(lo[k], v[k]); hi[k] = std::max(hi[k], v[k]); }
            }
    b.count = n;
    if (n) for (int k = 0; k < 3; ++k) { b.lo[k] = lo[k]; b.hi[k] = hi[k]; }
}
} // namespace

QVector<QRgb> fmvPalette(const uchar* rgba, int width, int height, int stride, int colors)
{
    colors = std::clamp(colors, 2, 512);
    static const std::array<quint8, 256> lut = [] {
        std::array<quint8, 256> t{};
        for (int v = 0; v < 256; ++v) t[v] = quint8(megaDriveLevel(v));
        return t;
    }();
    std::array<quint32, 512> hist{};
    for (int y = 0; y < height; ++y) {
        const uchar* p = rgba + qsizetype(y) * stride;
        for (int x = 0; x < width; ++x, p += 4) ++hist[(lut[p[0]] << 6) | (lut[p[1]] << 3) | lut[p[2]]];
    }
    QVector<QRgb> pal;
    int used = 0;
    for (quint32 c : hist) used += c ? 1 : 0;
    if (used <= colors) {   // few enough: every colour of the frame, as it is
        for (int i = 0; i < 512; ++i)
            if (hist[i]) pal.push_back(qRgb(kMegaDriveLevels[i >> 6], kMegaDriveLevels[(i >> 3) & 7], kMegaDriveLevels[i & 7]));
        if (pal.isEmpty()) pal.push_back(qRgb(0, 0, 0));
        return pal;
    }
    std::vector<Box> boxes{{{0, 0, 0}, {7, 7, 7}, 0}};
    shrink(boxes[0], hist);
    while (int(boxes.size()) < colors) {
        // Split the box with the most pixels times its longest side, at the median.
        int best = -1;
        double bestScore = 0;
        for (int i = 0; i < int(boxes.size()); ++i) {
            const Box& b = boxes[i];
            const int side = std::max({b.hi[0] - b.lo[0], b.hi[1] - b.lo[1], b.hi[2] - b.lo[2]});
            if (side <= 0) continue;
            // (the square root keeps small but distinct colours from being starved by big flat areas)
            const double score = std::sqrt(double(b.count)) * side;
            if (score > bestScore) { bestScore = score; best = i; }
        }
        if (best < 0) break;
        const Box b = boxes[best];
        const int d[3] = {b.hi[0] - b.lo[0], b.hi[1] - b.lo[1], b.hi[2] - b.lo[2]};
        const int axis = (d[1] >= d[0] && d[1] >= d[2]) ? 1 : (d[0] >= d[2] ? 0 : 2);
        quint64 plane[8] = {};
        for (int r = b.lo[0]; r <= b.hi[0]; ++r)
            for (int g = b.lo[1]; g <= b.hi[1]; ++g)
                for (int bl = b.lo[2]; bl <= b.hi[2]; ++bl) {
                    const int v[3] = {r, g, bl};
                    plane[v[axis]] += hist[(r << 6) | (g << 3) | bl];
                }
        quint64 acc = 0;
        int cut = b.lo[axis];
        for (int v = b.lo[axis]; v < b.hi[axis]; ++v) {
            acc += plane[v];
            cut = v;
            if (acc * 2 >= b.count) break;
        }
        Box a = b, c = b;
        a.hi[axis] = cut;
        c.lo[axis] = cut + 1;
        shrink(a, hist);
        shrink(c, hist);
        boxes[best] = a;
        if (c.count) boxes.push_back(c);
        if (!a.count) boxes.erase(boxes.begin() + best);
    }
    for (const Box& b : boxes) {
        // The box's most common colour stands for it (always one the frame really has).
        quint32 top = 0;
        int ti = -1;
        for (int r = b.lo[0]; r <= b.hi[0]; ++r)
            for (int g = b.lo[1]; g <= b.hi[1]; ++g)
                for (int bl = b.lo[2]; bl <= b.hi[2]; ++bl) {
                    const int i = (r << 6) | (g << 3) | bl;
                    if (hist[i] > top) { top = hist[i]; ti = i; }
                }
        if (ti >= 0) pal.push_back(qRgb(kMegaDriveLevels[ti >> 6], kMegaDriveLevels[(ti >> 3) & 7], kMegaDriveLevels[ti & 7]));
    }
    if (pal.isEmpty()) pal.push_back(qRgb(0, 0, 0));
    return pal;
}

QSize fmvGridSize(double displayAspect, int rows, int columns)
{
    int h = rows > 0 ? rows : 224;
    h = std::clamp(h & ~3, 16, 1080);
    int w = columns;
    if (w <= 0) {
        // A 4:3 screen is 256 columns for 224 rows (pixels 8:7 wide); other shapes keep that pixel.
        const double aspect = displayAspect > 0 ? displayAspect : 4.0 / 3.0;
        w = int(std::lround(h * (256.0 / 224.0) * aspect / (4.0 / 3.0) / 4.0)) * 4;
    }
    w = std::clamp(w & ~3, 16, 1920);
    return {w, h};
}

QRect fmvWindowRect(const QSize& grid, double window)
{
    window = std::clamp(window, 0.25, 1.0);
    if (window >= 0.995) return QRect(QPoint(0, 0), grid);
    const int w = std::clamp(int(std::lround(grid.width() * window / 8.0)) * 8, 8, grid.width());
    const int h = std::clamp(int(std::lround(grid.height() * window / 8.0)) * 8, 8, grid.height());
    return QRect(((grid.width() - w) / 2) & ~3, ((grid.height() - h) / 2) & ~3, w, h);
}
