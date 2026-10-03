// FMV console colour: the Mega Drive grid, the per-frame palette, the screen grid.
#include "render/FmvPalette.h"
#include <QSet>
#include <cstdio>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }
static bool onGrid(QRgb c)
{
    auto lvl = [](int v) { for (int l : kMegaDriveLevels) if (l == v) return true; return false; };
    return lvl(qRed(c)) && lvl(qGreen(c)) && lvl(qBlue(c));
}

int main()
{
    check(megaDriveLevel(0) == 0 && megaDriveLevel(255) == 7 && megaDriveLevel(60) == 1 && megaDriveLevel(130) == 3 && megaDriveLevel(131) == 4,
          "8-bit values map to the nearest of the console's eight levels");
    // A frame of smooth gradients: far more than 64 colours on the grid.
    const int W = 256, H = 224;
    std::vector<uchar> px(size_t(W) * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            uchar* p = &px[(size_t(y) * W + x) * 4];
            p[0] = uchar(x); p[1] = uchar(y * 255 / H); p[2] = uchar((x + y) / 2); p[3] = 255;
        }
    for (int n : {16, 64, 128}) {
        const QVector<QRgb> pal = fmvPalette(px.data(), W, H, W * 4, n);
        bool grid = true;
        for (QRgb c : pal) grid = grid && onGrid(c);
        QSet<QRgb> uniq(pal.begin(), pal.end());
        std::printf("asked %d: %d colours, %d distinct\n", n, int(pal.size()), int(uniq.size()));
        check(pal.size() <= n && pal.size() >= n * 3 / 4 && grid && uniq.size() == pal.size(), "a rich frame: about as many colours as asked, all different, all on the grid");
    }
    // A frame with five colours keeps exactly those (snapped to the grid).
    for (int i = 0; i < W * H; ++i) {
        const int k = i % 5;
        px[i * 4] = uchar(k * 60); px[i * 4 + 1] = uchar(255 - k * 50); px[i * 4 + 2] = uchar(k == 2 ? 255 : 0);
    }
    const QVector<QRgb> few = fmvPalette(px.data(), W, H, W * 4, 64);
    check(few.size() == 5, "a frame with five colours gets exactly five");
    // A dominant background must not starve a small, different detail.
    for (int i = 0; i < W * H; ++i) { px[i * 4] = uchar(20 + i % 7); px[i * 4 + 1] = uchar(30 + (i / W) % 60); px[i * 4 + 2] = uchar(120 + i % 90); }
    for (int y = 100; y < 108; ++y) for (int x = 100; x < 108; ++x) { uchar* p = &px[(size_t(y) * W + x) * 4]; p[0] = 255; p[1] = 40; p[2] = 30; }
    bool hasRed = false;
    for (QRgb c : fmvPalette(px.data(), W, H, W * 4, 16)) if (qRed(c) >= 206 && qGreen(c) <= 87 && qBlue(c) <= 87) hasRed = true;
    check(hasRed, "a small red detail on a big blue background keeps a red of its own (16 colours)");

    check(fmvGridSize(4.0 / 3.0, 0, 0) == QSize(256, 224), "a 4:3 picture is 256 x 224");
    check(fmvGridSize(16.0 / 9.0, 0, 0) == QSize(340, 224), "a 16:9 picture keeps the console's pixel shape: 340 x 224");
    check(fmvGridSize(4.0 / 3.0, 240, 320) == QSize(320, 240), "explicit rows and columns are used as given");
    const QRect w = fmvWindowRect(QSize(256, 224), 0.62);
    check(w.width() % 8 == 0 && w.height() % 8 == 0 && w.x() % 4 == 0 && w.y() % 4 == 0 && qAbs(w.center().x() - 128) <= 4 && qAbs(w.center().y() - 112) <= 4
          && qAbs(w.width() - 159) <= 8, "a 62% window: whole blocks, centred");
    check(fmvWindowRect(QSize(256, 224), 1.0) == QRect(0, 0, 256, 224), "a full window is the whole screen");
    std::printf(fails ? "\n%d FMV check(s) failed\n" : "\nAll FMV checks passed\n", fails);
    return fails ? 1 : 0;
}
