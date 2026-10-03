// Unit tests for the aspect-ratio / scaling math (no GPU or GStreamer needed).
#include "render/Geometry.h"
#include "settings/CrtParams.h"
#include <QJsonDocument>
#include <cmath>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)
static bool near(double a, double b, double eps = 0.51) { return std::abs(a - b) <= eps; }

int main()
{
    const QRectF area(0, 0, 1920, 1080);
    { // 4:3 in a 16:9 area -> pillarbox, 1440x1080 centred
        SourceFormat f{640, 480};
        auto L = computeLayout(displaySize(f), ScaleMode::Fit, {}, area);
        CHECK(L.valid && near(L.visibleRect.width(), 1440) && near(L.visibleRect.height(), 1080) && near(L.visibleRect.x(), 240));
        CHECK(near(L.srcRect.width(), 1, 1e-9));
    }
    { // 16:9 fills exactly
        SourceFormat f{1280, 720};
        auto L = computeLayout(displaySize(f), ScaleMode::Fit, {}, area);
        CHECK(near(L.visibleRect.width(), 1920) && near(L.visibleRect.height(), 1080));
    }
    { // Vertical 9:16 -> 607.5x1080
        SourceFormat f{720, 1280};
        auto L = computeLayout(displaySize(f), ScaleMode::Fit, {}, area);
        CHECK(near(L.visibleRect.width(), 607.5) && near(L.visibleRect.height(), 1080));
    }
    { // Ultrawide 64:27 -> letterbox 1920x810
        SourceFormat f{2560, 1080};
        auto L = computeLayout(displaySize(f), ScaleMode::Fit, {}, area);
        CHECK(near(L.visibleRect.width(), 1920) && near(L.visibleRect.height(), 810) && near(L.visibleRect.y(), 135));
    }
    { // Anamorphic DVD 720x480 PAR 32:27 -> DAR 16:9
        SourceFormat f{720, 480, 32, 27};
        CHECK(near(displayAspect(f), 16.0 / 9.0, 1e-9));
        auto L = computeLayout(displaySize(f), ScaleMode::Fit, {}, area);
        CHECK(near(L.visibleRect.width(), 1920) && near(L.visibleRect.height(), 1080));
    }
    { // Phone video stored 1920x1080, rotate-90 -> vertical
        SourceFormat f{1920, 1080};
        f.orient = orientationFromTag("rotate-90");
        CHECK(f.orient.rotation == 90);
        CHECK(near(displayAspect(f), 9.0 / 16.0, 1e-9));
        CHECK(orientedStorageSize(f) == QSize(1080, 1920));
    }
    { // Fill: 4:3 in 16:9 fills width and crops top/bottom symmetrically
        SourceFormat f{640, 480};
        auto L = computeLayout(displaySize(f), ScaleMode::Fill, {}, area);
        CHECK(near(L.visibleRect.width(), 1920) && near(L.visibleRect.height(), 1080));
        CHECK(near(L.srcRect.height(), 0.75, 1e-6) && near(L.srcRect.y(), 0.125, 1e-6));
        CHECK(near(L.imageRect.width() / L.imageRect.height(), 4.0 / 3.0, 1e-9));   // no distortion
    }
    { // Original: 640x480 shown 1:1; 4K shrinks to fit
        auto L = computeLayout(QSizeF(640, 480), ScaleMode::Original, {}, area);
        CHECK(near(L.visibleRect.width(), 640) && near(L.visibleRect.height(), 480));
        auto L2 = computeLayout(QSizeF(3840, 2160), ScaleMode::Original, {}, area);
        CHECK(near(L2.visibleRect.width(), 1920));
    }
    { // Crop 16:9 -> 4:3 via cropForAspect keeps aspect and is centred
        const QSizeF d(1920, 1080);
        CropFractions c = cropForAspect(d, 4.0 / 3.0);
        CHECK(near(c.left, 0.125, 1e-6) && near(c.right, 0.125, 1e-6) && c.top == 0);
        auto L = computeLayout(d, ScaleMode::Crop, c, area);
        CHECK(near(L.visibleRect.width() / L.visibleRect.height(), 4.0 / 3.0, 1e-6));
        CHECK(near(L.srcRect.x(), 0.125, 1e-6));
    }
    { // Crop is ignored outside Crop mode (never crops unexpectedly)
        CropFractions c{0.1, 0.1, 0.1, 0.1};
        auto L = computeLayout(QSizeF(1920, 1080), ScaleMode::Fit, c, area);
        CHECK(near(L.srcRect.width(), 1, 1e-9));
    }
    { // Orientation matrix maps corners correctly for rotate-90: oriented top-left = storage bottom-left
        float r0[3], r1[3];
        orientationMatrix(orientationFromTag("rotate-90"), r0, r1);
        const double su = r0[0] * 0 + r0[1] * 0 + r0[2], sv = r1[0] * 0 + r1[1] * 0 + r1[2];
        CHECK(near(su, 0, 1e-6) && near(sv, 1, 1e-6));
        orientationMatrix(orientationFromTag("rotate-270"), r0, r1);
        CHECK(near(r0[2], 1, 1e-6) && near(r1[2], 0, 1e-6));   // top-left <- storage top-right
    }
    { // Auto scanlines never drop below 2.5 px per line
        CHECK(near(autoScanlineCount(480, 1080), 240, 1e-9));
        CHECK(near(autoScanlineCount(480, 710), 240, 1e-9));
        CHECK(near(autoScanlineCount(1080, 1080), 270, 1e-9));
        CHECK(near(autoScanlineCount(240, 1080), 240, 1e-9));
    }
    { // Lower resolution ("bigger pixels")
        SourceFormat hd{1920, 1080};
        CHECK(pixelatedSize(hd, 0, 240, 0) == QSize(427, 240));          // square pixels at 16:9
        CHECK(pixelatedSize(hd, 0, 0, 0).isEmpty());                     // native
        CHECK(pixelatedSize(hd, 0, 1080, 0).isEmpty());                  // not lower than the source
        CHECK(pixelatedSize(hd, 0, 224, 256) == QSize(256, 224));        // fixed width: wide pixels
        SourceFormat dvd{720, 480, 32, 27};
        CHECK(pixelatedSize(dvd, 0, 240, 0) == QSize(427, 240));         // anamorphic: square on screen
        SourceFormat portrait{1080, 1920};
        CHECK(pixelatedSize(portrait, 0, 240, 0) == QSize(135, 240));    // rows along the vertical
        SourceFormat phone{1920, 1080};
        phone.orient = orientationFromTag("rotate-90");
        CHECK(pixelatedSize(phone, 0, 240, 0) == QSize(135, 240));       // rotation respected
        CHECK(pixelatedSize(hd, 4.0 / 3.0, 240, 0) == QSize(320, 240));  // aspect override
        CrtParams p; p.pixelHeight = 5; p.pixelFilter = 9; p.clamp();
        CHECK(p.pixelHeight == 16 && p.pixelFilter == 2);
    }
    { // Preset JSON round trip
        for (const auto& p : builtinPresets()) {
            QString n; CrtParams q; QString err;
            const QJsonObject o = QJsonDocument::fromJson(QJsonDocument(presetToJson(p.name, p.params)).toJson()).object();
            CHECK(presetFromJson(o, &n, &q, &err) && n == p.name && q == p.params);
        }
        QString err;
        CHECK(!presetFromJson(QJsonObject{{"name", "x"}}, nullptr, nullptr, &err) && !err.isEmpty());
        // Presets written before the VHS/LaserDisc/scanline-style options still load, with those off.
        const QJsonObject old{{"format", "crtplayer-preset"}, {"version", 1}, {"name", "Old"},
                              {"params", QJsonObject{{"scanStrength", 0.5}, {"maskType", 2}, {"curvature", 0.1}}}};
        QString n; CrtParams q;
        CHECK(presetFromJson(old, &n, &q, &err) && q.scanType == 0 && q.vhsJitter == 0.f && q.laserRot == 0.f && qAbs(q.curvature - 0.1f) < 1e-6);
        // New fields round-trip and are clamped.
        CrtParams r; r.scanType = 9; r.vhsTracking = 5.f; r.dotCrawl = 0.4f; r.clamp();
        CHECK(r.scanType == 9 && r.vhsTracking == 1.f);
        CrtParams r2; r2.scanType = 42; r2.clamp();
        CHECK(r2.scanType == 10);
        CHECK(CrtParams::fromJson(r.toJson()) == r);
        CHECK(builtinPresets().size() == 25);
    }
    std::printf(failures ? "%d geometry test(s) FAILED\n" : "All geometry tests passed\n", failures);
    return failures ? 1 : 0;
}
