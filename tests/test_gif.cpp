// GifEncoder: frames decode (with Qt's GIF reader) to what was put in.
//  1. <=256 colours that the 15-bit histogram holds exactly: decoded pixels identical
//     (proves the LZW coder, including dictionary resets on big frames).
//  2. A smooth photo-like frame: average error small, frame count and delays right.
#include "edit/GifEncoder.h"
#include <QCoreApplication>
#include <QDir>
#include <QImageReader>
#include <QRandomGenerator>
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what); if (!ok) ++fails; }

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString path = QDir::temp().filePath(QStringLiteral("crt_test_%1.gif").arg(QCoreApplication::applicationPid()));
    QFile::remove(path);
    const int W = 320, H = 240;
    QVector<QImage> frames;
    QRandomGenerator rng(7);
    for (int f = 0; f < 3; ++f) {
        QImage img(W, H, QImage::Format_RGB32);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                // 200 distinct colours at 5-bit bin centres, noisy (worst case for LZW)
                const int c = int(rng.bounded(200u));
                img.setPixel(x, y, qRgb((c % 8) * 32 + 4, ((c / 8) % 5) * 40 + 4, (c / 40) * 48 + 4));
            }
        frames << img;
    }
    QImage smooth(W, H, QImage::Format_RGB32);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            smooth.setPixel(x, y, qRgb(x * 255 / W, y * 255 / H, (x + y) * 255 / (W + H)));
    frames << smooth;

    GifEncoder enc;
    check(enc.open(path, W, H), "opens a new file");
    GifEncoder again;
    check(!again.open(path, W, H), "refuses to write over an existing file");
    for (int i = 0; i < frames.size(); ++i) enc.addFrame(frames[i], 4 + i);
    check(enc.close(), "closes without a write error");

    QImageReader r(path);
    r.setDecideFormatFromContent(true);
    check(r.canRead() && r.supportsAnimation(), "Qt reads it as an animation");
    check(r.imageCount() == frames.size() || r.imageCount() == 0, "frame count (or unknown to the reader)");
    int n = 0;
    bool exact = true;
    double smoothErr = -1;
    QVector<int> delays;
    while (r.canRead()) {
        const QImage img = r.read().convertToFormat(QImage::Format_RGB32);
        if (img.isNull()) break;
        delays << r.nextImageDelay();
        if (n < 3) {
            for (int y = 0; y < H && exact; ++y)
                for (int x = 0; x < W; ++x)
                    if (img.pixel(x, y) != frames[n].pixel(x, y)) { exact = false; break; }
        } else if (n == 3) {
            // Dithering trades per-pixel error for the right average: compare 4x4 averages,
            // as the eye (and a downscale) sees them.
            double e = 0;
            int blocks = 0;
            for (int by = 0; by + 4 <= H; by += 4)
                for (int bx = 0; bx + 4 <= W; bx += 4) {
                    double da[3] = {0, 0, 0};
                    for (int y = by; y < by + 4; ++y)
                        for (int x = bx; x < bx + 4; ++x) {
                            const QRgb a = img.pixel(x, y), b = smooth.pixel(x, y);
                            da[0] += qRed(a) - qRed(b); da[1] += qGreen(a) - qGreen(b); da[2] += qBlue(a) - qBlue(b);
                        }
                    e += (std::abs(da[0]) + std::abs(da[1]) + std::abs(da[2])) / 16.0;
                    ++blocks;
                }
            smoothErr = e / (blocks * 3.0);
        }
        ++n;
    }
    std::printf("decoded %d frames, smooth-frame mean error %.2f, delays", n, smoothErr);
    for (int d : delays) std::printf(" %d", d);
    std::printf("\n");
    check(n == frames.size(), "all frames decode");
    check(exact, "256-colour frames decode pixel for pixel (LZW is right)");
    check(smoothErr >= 0 && smoothErr < 2.0, "a smooth gradient stays close (4x4 averages within 2 of 255)");
    check(delays.size() >= 3 && delays[0] == 40 && delays[1] == 50, "frame delays (40 ms, 50 ms)");
    QFile::remove(path);
    std::printf(fails ? "\n%d GIF check(s) failed\n" : "\nAll GIF checks passed\n", fails);
    return fails ? 1 : 0;
}
