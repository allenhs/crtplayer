#include "CrtParams.h"
#include <QtMath>
#include <algorithm>

const QVector<ParamDesc>& floatParamDescs()
{
    static const QVector<ParamDesc> d = {
        {"scanStrength", "Strength",   "Scanlines", &CrtParams::scanStrength, 0.f, 1.f, 2, "How dark the gaps between scanlines are"},
        {"scanWidth",    "Beam width", "Scanlines", &CrtParams::scanWidth,    0.f, 1.f, 2, "Thickness of each scanline; bright areas bloom wider automatically"},
        {"maskStrength", "Strength",   "Phosphor mask", &CrtParams::maskStrength, 0.f, 1.f, 2, "Visibility of the phosphor mask pattern"},
        {"maskScale",    "Scale",      "Phosphor mask", &CrtParams::maskScale,    1.f, 4.f, 2, "Size of one mask cell in screen pixels (raise on HiDPI or 4K screens)"},
        {"curvature",    "Curvature",  "Geometry", &CrtParams::curvature,    0.f, 0.5f, 3, "Bulge of the tube. The whole picture stays visible; only the corners outside the tube turn black"},
        {"cornerRadius", "Corner rounding", "Geometry", &CrtParams::cornerRadius, 0.f, 0.25f, 3, "Rounded tube corners, relative to the shorter side"},
        {"overscan",     "Overscan",   "Geometry", &CrtParams::overscan,     0.f, 0.10f, 3, "Zooms past the picture edges like a consumer TV. This crops picture content"},
        {"bloom",        "Bloom",      "Light", &CrtParams::bloom, 0.f, 1.f, 2, "Bright areas spill light into their surroundings"},
        {"glow",         "Phosphor glow", "Light", &CrtParams::glow, 0.f, 1.f, 2, "Soft halation that fills the dark gaps of the mask and scanlines"},
        {"persistence",  "Persistence", "Light", &CrtParams::persistence, 0.f, 1.f, 2, "Phosphor afterglow: bright moving objects leave a brief trail (red and green fade slower than blue)"},
        {"chroma",       "Chromatic aberration", "Signal", &CrtParams::chroma, 0.f, 3.f, 2, "Red/blue convergence error that grows toward the edges"},
        {"bleed",        "Colour bleed", "Signal", &CrtParams::bleed, 0.f, 1.f, 2, "Horizontal smearing of colour (composite/VHS style); luminance stays sharp"},
        {"noise",        "Noise",      "Signal", &CrtParams::noise, 0.f, 1.f, 2, "Animated signal noise"},
        {"flicker",      "Flicker",    "Signal", &CrtParams::flicker, 0.f, 1.f, 2, "Subtle brightness flicker (kept low even at maximum)"},
        {"vhsJitter",    "Timebase jitter", "VHS tape", &CrtParams::vhsJitter, 0.f, 1.f, 2, "Each line wobbles sideways a little, like an unstable tape transport"},
        {"vhsTracking",  "Tracking error", "VHS tape", &CrtParams::vhsTracking, 0.f, 1.f, 2, "A band of noise and displaced lines that slowly rolls through the picture"},
        {"vhsHeadSwitch","Head switching", "VHS tape", &CrtParams::vhsHeadSwitch, 0.f, 1.f, 2, "Skewed, noisy lines at the bottom edge of the picture"},
        {"vhsChromaDelay","Chroma delay", "VHS tape", &CrtParams::vhsChromaDelay, 0.f, 1.f, 2, "Colour lands to the right of the picture detail"},
        {"vhsDropouts",  "Dropouts",   "VHS tape", &CrtParams::vhsDropouts, 0.f, 1.f, 2, "Brief white streaks where the tape lost signal"},
        {"filmGrain",    "Grain",      "Film projection", &CrtParams::filmGrain, 0.f, 1.f, 2, "Film grain, fresh every frame, strongest in the mid-tones"},
        {"gateWeave",    "Gate weave", "Film projection", &CrtParams::gateWeave, 0.f, 1.f, 2, "The picture drifting slightly as the film runs through the projector"},
        {"filmFlicker",  "Projector flicker", "Film projection", &CrtParams::filmFlicker, 0.f, 1.f, 2, "Brightness pumping from frame to frame"},
        {"filmDamage",   "Dust and scratches", "Film projection", &CrtParams::filmDamage, 0.f, 1.f, 2, "Specks of dust and scratches on a worn print"},
        {"tapeHiss",     "Tape hiss",    "Sound", &CrtParams::tapeHiss, 0.f, 1.f, 2, "The hiss of a tape recording"},
        {"wowFlutter",   "Wow and flutter", "Sound", &CrtParams::wowFlutter, 0.f, 1.f, 2, "The pitch wobbling as the tape runs unevenly"},
        {"tapeSaturation", "Tape saturation", "Sound", &CrtParams::tapeSaturation, 0.f, 1.f, 2, "Warm, slightly compressed loud passages"},
        {"tapeTone",     "Treble loss",  "Sound", &CrtParams::tapeTone, 0.f, 1.f, 2, "The dull top end of a VHS linear soundtrack"},
        {"tvSpeaker",    "TV speaker",   "Sound", &CrtParams::tvSpeaker, 0.f, 1.f, 2, "A small, boxy TV speaker: thin bass, no highs, mono when full"},
        {"filmCrackle",  "Film crackle", "Sound", &CrtParams::filmCrackle, 0.f, 1.f, 2, "Crackle and pops of an optical film soundtrack"},
        {"vhsSoftness",  "Tape softness", "VHS tape", &CrtParams::vhsSoftness, 0.f, 1.f, 2, "Lower horizontal resolution; at 1.0 about 240 TV lines, colour much softer than luma"},
        {"dotCrawl",     "Dot crawl",  "Composite & LaserDisc", &CrtParams::dotCrawl, 0.f, 1.f, 2, "Crawling dots along colour edges from the composite subcarrier"},
        {"rainbow",      "Rainbow crosstalk", "Composite & LaserDisc", &CrtParams::rainbow, 0.f, 1.f, 2, "Shimmering false colour on fine stripes and text"},
        {"laserRot",     "Laser rot",  "Composite & LaserDisc", &CrtParams::laserRot, 0.f, 1.f, 2, "Flickering speckles from a deteriorating LaserDisc"},
        {"vignette",     "Vignette",   "Picture", &CrtParams::vignette, 0.f, 1.f, 2, "Darkening toward the edges of the tube"},
        {"brightness",   "Brightness", "Picture", &CrtParams::brightness, 0.5f, 1.5f, 2, "Overall gain"},
        {"contrast",     "Contrast",   "Picture", &CrtParams::contrast, 0.5f, 1.5f, 2, "Contrast around mid grey"},
        {"saturation",   "Saturation", "Picture", &CrtParams::saturation, 0.f, 2.f, 2, "Colour saturation"},
        {"warmth",       "Colour temperature", "Picture", &CrtParams::warmth, -1.f, 1.f, 2, "Negative is cooler (bluish), positive is warmer"},
    };
    return d;
}

QStringList colorDepthNames()
{
    return {"Full colour", "15-bit (32 768 colours)", "12-bit (4 096 colours)", "9-bit (512 colours)",
            "8-bit (256 colours, 3-3-2)", "6-bit (64 colours)", "3-bit (8 colours)",
            "Game Boy (4 greens)", "CGA (4 colours)", "EGA (16 colours)"};
}

QStringList ditherNames()
{
    return {"None", "Ordered 4×4", "Ordered 8×8"};
}

QStringList pixelFilterNames()
{
    return {"Hard (pure blocks)", "Sharp (anti-aliased edges)", "Soft (smooth)"};
}

QStringList scanTypeNames()
{
    return {"Soft (Gaussian beam)", "Sharp (PVM/BVM)", "Dynamic (brightness-adaptive)",
            "Interlaced (alternating fields)", "VGA double-scan", "Pixel beam (low-res console)",
            "Geom-style beams", "Lottes-style beams", "Easymode-style beams", "Hyllian-style beams",
            "MAME HLSL-style sine"};
}

QStringList maskTypeNames()
{
    return {"None", "Aperture grille", "Shadow mask", "Slot mask"};
}

QJsonObject CrtParams::toJson() const
{
    QJsonObject o;
    for (const auto& d : floatParamDescs())
        o.insert(d.key, double(this->*(d.member)));
    o.insert("scanLines", scanLines);
    o.insert("scanType", scanType);
    o.insert("pixelHeight", pixelHeight);
    o.insert("pixelWidth", pixelWidth);
    o.insert("pixelFilter", pixelFilter);
    o.insert("colorDepth", colorDepth);
    o.insert("dither", dither);
    o.insert("videoStandard", videoStandard);
    o.insert("powerEffects", powerEffects);
    o.insert("channelStatic", channelStatic);
    o.insert("vcrOsd", vcrOsd);
    o.insert("maskType", maskType);
    o.insert("includeBars", includeBars);
    return o;
}

CrtParams CrtParams::fromJson(const QJsonObject& o)
{
    CrtParams p;
    for (const auto& d : floatParamDescs())
        if (o.contains(d.key) && o.value(d.key).isDouble())
            p.*(d.member) = float(o.value(d.key).toDouble());
    if (o.contains("scanLines")) p.scanLines = o.value("scanLines").toInt();
    if (o.contains("scanType"))  p.scanType = o.value("scanType").toInt();
    if (o.contains("pixelHeight")) p.pixelHeight = o.value("pixelHeight").toInt();
    if (o.contains("pixelWidth"))  p.pixelWidth = o.value("pixelWidth").toInt();
    if (o.contains("pixelFilter")) p.pixelFilter = o.value("pixelFilter").toInt();
    if (o.contains("colorDepth"))  p.colorDepth = o.value("colorDepth").toInt();
    if (o.contains("dither"))      p.dither = o.value("dither").toInt();
    if (o.contains("videoStandard")) p.videoStandard = o.value("videoStandard").toInt();
    if (o.contains("powerEffects")) p.powerEffects = o.value("powerEffects").toBool();
    if (o.contains("channelStatic")) p.channelStatic = o.value("channelStatic").toBool();
    if (o.contains("vcrOsd"))      p.vcrOsd = o.value("vcrOsd").toBool();
    if (o.contains("maskType"))  p.maskType = o.value("maskType").toInt();
    if (o.contains("includeBars")) p.includeBars = o.value("includeBars").toBool();
    p.clamp();
    return p;
}

void CrtParams::clamp()
{
    for (const auto& d : floatParamDescs()) {
        float& v = this->*(d.member);
        if (!qIsFinite(v)) v = d.min;
        v = std::clamp(v, d.min, d.max);
    }
    scanLines = std::clamp(scanLines, 0, 2160);
    maskType = std::clamp(maskType, 0, 3);
    scanType = std::clamp(scanType, 0, 10);
    pixelHeight = std::clamp(pixelHeight, 0, 2160);
    if (pixelHeight > 0 && pixelHeight < 16) pixelHeight = 16;
    pixelWidth = std::clamp(pixelWidth, 0, 3840);
    if (pixelWidth > 0 && pixelWidth < 16) pixelWidth = 16;
    pixelFilter = std::clamp(pixelFilter, 0, 2);
    colorDepth = std::clamp(colorDepth, 0, 9);
    dither = std::clamp(dither, 0, 2);
    videoStandard = std::clamp(videoStandard, 0, 1);
}

bool CrtParams::operator==(const CrtParams& o) const
{
    for (const auto& d : floatParamDescs())
        if (qAbs(this->*(d.member) - o.*(d.member)) > 1e-4f) return false;
    return scanLines == o.scanLines && scanType == o.scanType && maskType == o.maskType && includeBars == o.includeBars &&
           pixelHeight == o.pixelHeight && pixelWidth == o.pixelWidth && pixelFilter == o.pixelFilter &&
           colorDepth == o.colorDepth && dither == o.dither && videoStandard == o.videoStandard &&
           powerEffects == o.powerEffects && channelStatic == o.channelStatic && vcrOsd == o.vcrOsd;
}

QVector<CrtPreset> builtinPresets()
{
    QVector<CrtPreset> v;
    auto add = [&](const char* name, auto fn) { CrtPreset p; p.name = name; p.builtin = true; fn(p.params); p.params.clamp(); v.push_back(p); };
    // Shared base for consumer sets fed by analog sources.
    auto consumerTv = [](CrtParams& p) {
        p.scanStrength = 0.40f; p.scanWidth = 0.50f; p.scanType = 0;
        p.maskType = 2; p.maskStrength = 0.30f; p.maskScale = 1.5f;
        p.curvature = 0.12f; p.cornerRadius = 0.06f;
        p.bloom = 0.25f; p.glow = 0.22f;
        p.vignette = 0.30f; p.brightness = 1.08f; p.contrast = 1.02f; p.saturation = 1.10f; p.warmth = 0.10f;
    };

    add("Clean Broadcast Monitor", [](CrtParams& p) {
        p.scanStrength = 0.35f; p.scanWidth = 0.55f; p.scanType = 1;
        p.maskType = 1; p.maskStrength = 0.18f; p.maskScale = 1.0f;
        p.curvature = 0.03f; p.cornerRadius = 0.015f;
        p.bloom = 0.10f; p.glow = 0.10f;
        p.vignette = 0.10f; p.brightness = 1.05f; p.contrast = 1.05f;
    });
    add("Consumer Television", [&](CrtParams& p) {
        consumerTv(p);
        p.powerEffects = true; p.channelStatic = true; p.persistence = 0.15f;
        p.chroma = 0.6f; p.bleed = 0.30f; p.noise = 0.03f; p.flicker = 0.10f;
            p.tvSpeaker = 0.60f;   // sound
    });
    add("Arcade Display", [](CrtParams& p) {
        p.scanStrength = 0.65f; p.scanWidth = 0.40f; p.scanLines = 240; p.scanType = 2;
        p.maskType = 3; p.maskStrength = 0.32f; p.maskScale = 2.0f;
        p.curvature = 0.08f; p.cornerRadius = 0.03f;
        p.bloom = 0.35f; p.glow = 0.30f; p.persistence = 0.30f;
        p.chroma = 0.3f; p.bleed = 0.10f;
        p.vignette = 0.20f; p.brightness = 1.15f; p.contrast = 1.10f; p.saturation = 1.20f;
            p.tvSpeaker = 0.45f; p.tapeSaturation = 0.15f;   // sound
    });
    add("Worn VHS TV", [&](CrtParams& p) {
        consumerTv(p);
        p.scanWidth = 0.60f; p.curvature = 0.15f; p.cornerRadius = 0.08f; p.bloom = 0.30f; p.glow = 0.28f;
        p.chroma = 1.2f; p.bleed = 0.50f; p.noise = 0.14f; p.flicker = 0.25f;
        p.vhsJitter = 0.40f; p.vhsTracking = 0.30f; p.vhsHeadSwitch = 0.70f; p.vhsChromaDelay = 0.55f;
        p.vhsDropouts = 0.35f; p.vhsSoftness = 0.60f;
        p.powerEffects = true; p.channelStatic = true; p.vcrOsd = true;
        p.vignette = 0.45f; p.brightness = 1.0f; p.contrast = 0.95f; p.saturation = 0.80f; p.warmth = 0.15f;
            p.tapeHiss = 0.55f; p.wowFlutter = 0.45f; p.tapeSaturation = 0.35f; p.tapeTone = 0.60f; p.tvSpeaker = 0.65f;   // sound
    });
    add("VHS Home Recording", [&](CrtParams& p) {
        consumerTv(p);
        p.chroma = 0.5f; p.bleed = 0.35f; p.noise = 0.05f; p.flicker = 0.08f;
        p.vhsJitter = 0.15f; p.vhsHeadSwitch = 0.45f; p.vhsChromaDelay = 0.30f;
        p.vhsDropouts = 0.05f; p.vhsSoftness = 0.35f;
        p.powerEffects = true; p.channelStatic = true; p.vcrOsd = true;
            p.tapeHiss = 0.35f; p.wowFlutter = 0.25f; p.tapeSaturation = 0.25f; p.tapeTone = 0.45f; p.tvSpeaker = 0.55f;   // sound
    });
    add("LaserDisc Composite", [&](CrtParams& p) {
        consumerTv(p);
        p.chroma = 0.3f; p.bleed = 0.10f; p.noise = 0.02f; p.flicker = 0.05f;
        p.dotCrawl = 0.45f; p.rainbow = 0.35f; p.vhsSoftness = 0.08f;
    });
    add("LaserDisc with Laser Rot", [&](CrtParams& p) {
        consumerTv(p);
        p.chroma = 0.35f; p.bleed = 0.12f; p.noise = 0.07f; p.flicker = 0.08f;
        p.dotCrawl = 0.50f; p.rainbow = 0.40f; p.laserRot = 0.60f; p.vhsSoftness = 0.08f;
    });
    add("Interlaced Broadcast Monitor", [](CrtParams& p) {
        p.scanStrength = 0.50f; p.scanWidth = 0.45f; p.scanType = 3;
        p.maskType = 1; p.maskStrength = 0.20f; p.maskScale = 1.0f;
        p.curvature = 0.03f; p.cornerRadius = 0.015f;
        p.bloom = 0.12f; p.glow = 0.12f; p.vignette = 0.10f; p.brightness = 1.08f; p.contrast = 1.05f;
    });
    add("VGA Double-Scan Monitor", [](CrtParams& p) {
        p.scanStrength = 0.50f; p.scanWidth = 0.60f; p.scanType = 4;
        p.maskType = 2; p.maskStrength = 0.15f; p.maskScale = 1.0f;
        p.curvature = 0.05f; p.cornerRadius = 0.02f;
        p.bloom = 0.08f; p.glow = 0.08f; p.vignette = 0.08f; p.brightness = 1.05f; p.contrast = 1.05f;
    });
    add("Low-Res Console", [](CrtParams& p) {
        p.pixelHeight = 240; p.pixelFilter = 1; p.colorDepth = 3; p.dither = 1;   // 9-bit colour, dithered
        p.scanStrength = 0.55f; p.scanWidth = 0.50f; p.scanLines = 0; p.scanType = 5;
        p.maskType = 1; p.maskStrength = 0.25f; p.maskScale = 1.5f;
        p.curvature = 0.10f; p.cornerRadius = 0.05f;
        p.bloom = 0.25f; p.glow = 0.30f; p.bleed = 0.15f;
        p.vignette = 0.20f; p.brightness = 1.12f; p.contrast = 1.05f; p.saturation = 1.10f;
    });
    // Looks modelled on well-known emulator shaders. Independent implementations of the
    // same techniques, tuned by eye to their characteristic appearance; not ports.
    add("Geom Look", [](CrtParams& p) {
        p.scanStrength = 0.85f; p.scanWidth = 0.5f; p.scanType = 6;
        p.maskType = 1; p.maskStrength = 0.22f; p.maskScale = 1.0f;
        p.curvature = 0.10f; p.cornerRadius = 0.03f;
        p.bloom = 0.05f; p.glow = 0.05f; p.vignette = 0.10f; p.brightness = 1.05f;
    });
    add("Lottes Look", [](CrtParams& p) {
        p.scanStrength = 0.9f; p.scanWidth = 0.5f; p.scanType = 7;
        p.maskType = 2; p.maskStrength = 0.35f; p.maskScale = 1.0f;
        p.curvature = 0.04f; p.cornerRadius = 0.02f;
        p.bloom = 0.20f; p.glow = 0.12f; p.vignette = 0.08f; p.brightness = 1.10f; p.contrast = 1.05f;
    });
    add("Easymode Look", [](CrtParams& p) {
        p.scanStrength = 0.85f; p.scanWidth = 0.5f; p.scanType = 8;
        p.maskType = 1; p.maskStrength = 0.30f; p.maskScale = 1.0f;
        p.bloom = 0.05f; p.brightness = 1.12f;
    });
    add("Hyllian Look", [](CrtParams& p) {
        p.scanStrength = 0.85f; p.scanWidth = 0.5f; p.scanType = 9;
        p.maskType = 1; p.maskStrength = 0.15f; p.maskScale = 1.0f;
        p.bloom = 0.05f; p.brightness = 1.08f; p.contrast = 1.05f;
    });
    add("MAME HLSL Look", [](CrtParams& p) {
        p.scanStrength = 0.6f; p.scanWidth = 0.45f; p.scanType = 10;
        p.maskType = 2; p.maskStrength = 0.35f; p.maskScale = 1.0f;
        p.curvature = 0.03f; p.cornerRadius = 0.02f;
        p.bloom = 0.25f; p.glow = 0.10f; p.chroma = 0.25f; p.vignette = 0.20f; p.brightness = 1.10f;
    });
    add("16-bit Console (256×224)", [](CrtParams& p) {
        p.pixelHeight = 224; p.pixelWidth = 256; p.pixelFilter = 1; p.colorDepth = 1;   // 15-bit colour
        p.scanStrength = 0.8f; p.scanWidth = 0.5f; p.scanType = 6;
        p.maskType = 2; p.maskStrength = 0.25f; p.maskScale = 1.0f;
        p.curvature = 0.08f; p.cornerRadius = 0.03f;
        p.bloom = 0.15f; p.glow = 0.12f; p.bleed = 0.15f; p.vignette = 0.15f; p.brightness = 1.08f; p.saturation = 1.08f;
    });
    add("Home Computer (320×200)", [](CrtParams& p) {
        p.pixelHeight = 200; p.pixelWidth = 320; p.pixelFilter = 0; p.colorDepth = 9; p.dither = 1;   // EGA, dithered
        p.scanStrength = 0.45f; p.scanWidth = 0.55f; p.scanType = 1;
        p.maskType = 2; p.maskStrength = 0.20f; p.maskScale = 1.0f;
        p.curvature = 0.06f; p.cornerRadius = 0.03f;
        p.bloom = 0.10f; p.glow = 0.10f; p.vignette = 0.12f; p.brightness = 1.05f;
    });
    add("PAL Television", [&](CrtParams& p) {
        consumerTv(p);
        p.videoStandard = 1;
        p.chroma = 0.5f; p.bleed = 0.30f; p.noise = 0.03f; p.flicker = 0.08f; p.rainbow = 0.25f; p.dotCrawl = 0.2f;
        p.powerEffects = true; p.channelStatic = true; p.persistence = 0.15f;
            p.tvSpeaker = 0.60f;   // sound
    });
    add("Handheld (4 greens)", [](CrtParams& p) {
        p.pixelHeight = 144; p.pixelWidth = 160; p.pixelFilter = 1; p.colorDepth = 7;
        p.scanStrength = 0.0f; p.maskType = 0; p.maskStrength = 0.0f;
        p.glow = 0.05f; p.persistence = 0.45f;   // the slow LCD's ghosting
        p.brightness = 1.0f; p.contrast = 1.0f;
    });
    add("Sharp PC Monitor", [](CrtParams& p) {
        p.scanStrength = 0.20f; p.scanWidth = 0.70f;
        p.maskType = 1; p.maskStrength = 0.12f; p.maskScale = 1.0f;
        p.curvature = 0.02f; p.cornerRadius = 0.01f;
        p.bloom = 0.05f; p.glow = 0.05f;
        p.vignette = 0.05f; p.brightness = 1.02f; p.contrast = 1.02f;
    });
    // Film projection: no tube (no scanlines, mask or curvature), film's own character.
    auto film = [](CrtParams& p) {
        p.scanStrength = 0.f; p.maskStrength = 0.f; p.curvature = 0.f; p.cornerRadius = 0.f;
        p.chroma = 0.f; p.bleed = 0.f; p.noise = 0.f; p.flicker = 0.f;
    };
    add("Film Print (35mm)", [film](CrtParams& p) {
        film(p);
        p.filmGrain = 0.30f; p.gateWeave = 0.25f; p.filmFlicker = 0.20f; p.filmDamage = 0.08f;
        p.bloom = 0.20f; p.glow = 0.18f; p.vignette = 0.22f; p.warmth = 0.06f; p.contrast = 1.05f;
            p.filmCrackle = 0.20f; p.tapeTone = 0.20f;   // sound
    });
    add("Worn Film (16mm)", [film](CrtParams& p) {
        film(p);
        p.filmGrain = 0.65f; p.gateWeave = 0.55f; p.filmFlicker = 0.45f; p.filmDamage = 0.60f;
        p.bloom = 0.25f; p.glow = 0.22f; p.vignette = 0.40f; p.warmth = 0.18f; p.saturation = 0.75f; p.contrast = 0.95f;
            p.filmCrackle = 0.60f; p.tapeTone = 0.45f; p.wowFlutter = 0.15f;   // sound
    });
    add("Drive-in Movie", [film](CrtParams& p) {
        film(p);
        p.filmGrain = 0.35f; p.gateWeave = 0.20f; p.filmFlicker = 0.15f; p.filmDamage = 0.20f;
        p.bloom = 0.35f; p.glow = 0.30f; p.vignette = 0.50f; p.warmth = 0.10f; p.saturation = 0.85f;
        p.contrast = 0.85f; p.brightness = 1.05f;
            p.filmCrackle = 0.30f; p.tvSpeaker = 0.50f; p.tapeTone = 0.30f;   // sound
    });
    return v;
}

QJsonObject presetToJson(const QString& name, const CrtParams& p)
{
    QJsonObject o;
    o.insert("format", "crtplayer-preset");
    o.insert("version", 1);
    o.insert("name", name);
    o.insert("params", p.toJson());
    return o;
}

bool presetFromJson(const QJsonObject& o, QString* name, CrtParams* p, QString* error)
{
    if (o.value("format").toString() != "crtplayer-preset" || !o.value("params").isObject()) {
        if (error) *error = "This file is not a CRT Player preset (missing \"format\": \"crtplayer-preset\" or \"params\").";
        return false;
    }
    const QString n = o.value("name").toString().trimmed();
    if (n.isEmpty()) {
        if (error) *error = "The preset has no name.";
        return false;
    }
    if (name) *name = n;
    if (p) *p = CrtParams::fromJson(o.value("params").toObject());
    return true;
}
