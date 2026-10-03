#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

// All CRT effect parameters. Every field is exposed in the UI and saved in presets.
struct CrtParams {
    // Resolution: render the picture at a lower resolution for bigger pixels
    int   pixelHeight  = 0;      // rows; 0 = native
    int   pixelWidth   = 0;      // columns; 0 = automatic (square pixels at the display aspect)
    int   pixelFilter  = 1;      // 0 hard blocks, 1 sharp (anti-aliased edges), 2 soft (bilinear)
    int   colorDepth   = 0;      // 0 full; 1 15-bit, 2 12-bit, 3 9-bit, 4 8-bit (3-3-2), 5 6-bit, 6 3-bit,
                                 // 7 Game Boy (4 greens), 8 CGA (4 colours), 9 EGA (16 colours)
    int   dither       = 0;      // 0 none, 1 ordered 4x4, 2 ordered 8x8
    int   videoStandard = 0;     // 0 NTSC (480 lines, 59.94 fields/s), 1 PAL (576 lines, 50 fields/s)
    // FMV console: the picture as an early-90s CD console played full-motion video.
    // Sega CD: a Cinepak-style codec (4x4 blocks, flat ones and unchanged ones kept), a few
    // dozen colours picked per frame from the Mega Drive's 512, ordered dither, a low frame
    // rate, often in a window smaller than the screen. Works on the lowered resolution
    // (rows/columns above; 224 rows with 8:7 pixels when those are left at native/auto).
    int   fmvMode      = 0;      // 0 off, 1 Sega CD
    int   fmvColors    = 64;     // colours on screen at once (8..256); the Mega Drive shows 61-64
    int   fmvFps       = 15;     // frames per second (0 = the video's own)
    float fmvBlocks    = 0.6f;   // codec strength: how readily blocks go flat or stay unchanged
    float fmvDither    = 0.7f;   // ordered-dither amount
    float fmvWindow    = 1.0f;   // the video's share of the screen (0.5..1); the rest is black
    // Set behaviour
    bool  powerEffects = false;  // power-on / power-off animation
    bool  channelStatic = false; // static between playlist items
    bool  vcrOsd       = false;  // VCR on-screen display (PLAY, PAUSE, REW, FF, STOP)
    // Scanlines
    float scanStrength = 0.35f;  // 0..1
    float scanWidth    = 0.5f;   // 0 (thin beam) .. 1 (wide beam)
    int   scanLines    = 0;      // 0 = automatic (derived from the visible source lines)
    int   scanType     = 0;      // 0 soft, 1 sharp, 2 dynamic, 3 interlaced, 4 VGA double-scan, 5 pixel beam,
                                 // 6-9 beam reconstruction (geom/lottes/easymode/hyllian-style), 10 MAME HLSL-style sine
    // Phosphor mask
    int   maskType     = 1;      // 0 none, 1 aperture grille, 2 shadow mask, 3 slot mask
    float maskStrength = 0.3f;   // 0..1
    float maskScale    = 1.0f;   // 1..4 (size of one mask cell in screen pixels)
    // Geometry
    float curvature    = 0.0f;   // 0..0.5
    float cornerRadius = 0.0f;   // 0..0.25 (fraction of the shorter side)
    float overscan     = 0.0f;   // 0..0.10 (crops edges; opt-in only)
    bool  includeBars  = false;  // apply the tube to letterbox/pillarbox areas too
    // Light
    float bloom        = 0.0f;
    float glow         = 0.0f;
    float persistence  = 0.0f;   // 0..1 phosphor afterglow (trails behind moving highlights)
    // Signal
    float chroma       = 0.0f;   // 0..3 (in 1/1000 of the tube size)
    float bleed        = 0.0f;
    float noise        = 0.0f;
    float flicker      = 0.0f;
    // VHS tape (all measured in picture lines / picture width, so they scale with any aspect)
    float vhsJitter      = 0.0f;  // per-line timebase wobble
    float vhsTracking    = 0.0f;  // rolling tracking-error band
    float vhsHeadSwitch  = 0.0f;  // head-switching skew/noise at the bottom of the picture
    float vhsChromaDelay = 0.0f;  // colour shifted right of luma
    float vhsDropouts    = 0.0f;  // white streaks from missing oxide
    // Film projection (24 frames per second, whatever the video's rate)
    float filmGrain      = 0.0f;  // grain, fresh every film frame
    float gateWeave      = 0.0f;  // the frame drifting in the projector's gate
    float filmFlicker    = 0.0f;  // brightness pumping from frame to frame
    float filmDamage     = 0.0f;  // dust specks and scratches
    // Sound (tape and speaker)
    float tapeHiss       = 0.0f;  // tape hiss
    float wowFlutter     = 0.0f;  // pitch wobble of the tape transport
    float tapeSaturation = 0.0f;  // warm soft clipping
    float tapeTone       = 0.0f;  // treble loss of a VHS linear track
    float tvSpeaker      = 0.0f;  // a small TV speaker (mono at 1)
    float filmCrackle    = 0.0f;  // optical soundtrack crackle and pops
    float pcmCrush       = 0.0f;  // console PCM: 8-bit samples at a low rate (at 1: 8-bit, 11 kHz)
    float vhsSoftness    = 0.0f;  // reduced luma/chroma bandwidth (~240 TVL at 1.0)
    // Composite video & LaserDisc
    float dotCrawl       = 0.0f;  // moving dots along colour edges
    float rainbow        = 0.0f;  // cross-colour on fine luma detail
    float laserRot       = 0.0f;  // LaserDisc laser-rot speckles
    // Picture
    float vignette     = 0.0f;
    float brightness   = 1.0f;   // 0.5..1.5
    float contrast     = 1.0f;   // 0.5..1.5
    float saturation   = 1.0f;   // 0..2
    float warmth       = 0.0f;   // -1..1

    QJsonObject toJson() const;
    static CrtParams fromJson(const QJsonObject& o);
    void clamp();
    bool operator==(const CrtParams& o) const;
    bool operator!=(const CrtParams& o) const { return !(*this == o); }
};

// Describes one float parameter; the UI and the JSON code are generated from this table.
struct ParamDesc {
    const char* key;
    const char* label;
    const char* group;
    float CrtParams::*member;
    float min, max;
    int decimals;
    const char* tooltip;
};
const QVector<ParamDesc>& floatParamDescs();
QStringList maskTypeNames();
QStringList scanTypeNames();
QStringList pixelFilterNames();
QStringList colorDepthNames();
QStringList ditherNames();
QStringList fmvModeNames();

struct CrtPreset {
    QString name;
    CrtParams params;
    bool builtin = false;
    QString filePath; // user presets only
};

QVector<CrtPreset> builtinPresets();

// Preset file format: { "format": "crtplayer-preset", "version": 1, "name": "...", "params": {...} }
QJsonObject presetToJson(const QString& name, const CrtParams& p);
bool presetFromJson(const QJsonObject& o, QString* name, CrtParams* p, QString* error);
