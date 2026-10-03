#pragma once
#include <gst/gst.h>

// Tape and speaker sound for the looks: an in-app GStreamer audio filter ("crttape",
// 32-bit float, any rate and channel count). With every amount at 0 the audio passes
// through untouched (bit-exact).
struct TapeParams {
    float hiss = 0.f;        // tape hiss (0..1)
    float wow = 0.f;         // wow and flutter: pitch wobble of the tape transport
    float saturation = 0.f;  // tape saturation: warm soft clipping
    float tone = 0.f;        // treble loss of a VHS linear track
    float speaker = 0.f;     // a small TV speaker: thin bass, boxy mids, no highs, mono at 1
    float crackle = 0.f;     // film soundtrack crackle and pops
    float dropouts = 0.f;    // brief level dips (VHS dropouts)
    float crush = 0.f;       // console PCM: 8-bit samples at a low rate, played without smoothing (at 1: 8-bit, 11 kHz)
    float noiseGain = 1.f;   // volume of the added noise (hiss and crackle): 0 silent .. 2 twice as loud
    bool any() const { return hiss > 0.f || wow > 0.f || saturation > 0.f || tone > 0.f || speaker > 0.f || crackle > 0.f || dropouts > 0.f || crush > 0.f; }
};

void crtTapeRegister();                                  // once, after gst_init
void crtTapeSetParams(GstElement* tape, const TapeParams& p);
