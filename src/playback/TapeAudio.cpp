#include "TapeAudio.h"

#include <gst/audio/gstaudiofilter.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <vector>

namespace {
struct Biquad {   // RBJ audio-EQ cookbook filter, direct form I
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    void lowpass(double sr, double f, double q) { design(sr, f, q, 0); }
    void highpass(double sr, double f, double q) { design(sr, f, q, 1); }
    void peak(double sr, double f, double q, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0), w = 2 * M_PI * std::min(f, sr * 0.45) / sr, al = std::sin(w) / (2 * q);
        const double a0 = 1 + al / A;
        b0 = (1 + al * A) / a0; b1 = -2 * std::cos(w) / a0; b2 = (1 - al * A) / a0; a1 = -2 * std::cos(w) / a0; a2 = (1 - al / A) / a0;
    }
    void design(double sr, double f, double q, int type)
    {
        const double w = 2 * M_PI * std::min(f, sr * 0.45) / sr, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        if (type == 0) { b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; }
        else { b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; }
        a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
};
struct BiquadState {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float run(const Biquad& f, double x)
    {
        const double y = f.b0 * x + f.b1 * x1 + f.b2 * x2 - f.a1 * y1 - f.a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return float(y);
    }
};
} // namespace

G_BEGIN_DECLS
#define CRT_TYPE_TAPE (crt_tape_get_type())
G_DECLARE_FINAL_TYPE(CrtTape, crt_tape, CRT, TAPE, GstAudioFilter)
G_END_DECLS

struct _CrtTape {
    GstAudioFilter parent;
    // amounts (set from the GUI thread)
    std::atomic<float> hiss, wow, saturation, tone, speaker, crackle, dropouts, crush, noiseGain;
    // processing state (streaming thread)
    int rate, channels;
    std::vector<std::vector<float>>* delay;
    std::vector<BiquadState>* tape;
    std::vector<BiquadState>* spk;       // 3 per channel: highpass, peak, lowpass
    std::vector<float>* hissHp;          // one-pole state per channel
    std::vector<float>* click;           // current click amplitude per channel
    std::vector<float>* held;            // console PCM: the sample being held, per channel
    double holdPh;
    int writePos;
    double wowPh, flutPh, drift, driftTarget;
    double dipGain, dipLeft;
    uint64_t rng;
    float lastTone, lastSpeaker;
    Biquad toneF, hp, pk, lp;
};

G_DEFINE_TYPE(CrtTape, crt_tape, GST_TYPE_AUDIO_FILTER)

static inline double rnd(uint64_t& s)   // xorshift, uniform 0..1
{
    s ^= s << 13; s ^= s >> 7; s ^= s << 17;
    return double(s >> 11) * (1.0 / 9007199254740992.0);
}

static gboolean crt_tape_setup(GstAudioFilter* f, const GstAudioInfo* info)
{
    CrtTape* t = CRT_TAPE(f);
    t->rate = GST_AUDIO_INFO_RATE(info);
    t->channels = GST_AUDIO_INFO_CHANNELS(info);
    const int len = std::max(64, int(t->rate * 0.05));   // 50 ms delay line (the wow reads inside it)
    t->delay->assign(t->channels, std::vector<float>(len, 0.f));
    t->tape->assign(t->channels, BiquadState());
    t->spk->assign(size_t(t->channels) * 3, BiquadState());
    t->hissHp->assign(t->channels, 0.f);
    t->click->assign(t->channels, 0.f);
    t->held->assign(t->channels, 0.f);
    t->holdPh = 1.0;
    t->writePos = 0;
    t->lastTone = t->lastSpeaker = -1.f;
    return TRUE;
}

static GstFlowReturn crt_tape_transform_ip(GstBaseTransform* bt, GstBuffer* buf)
{
    CrtTape* t = CRT_TAPE(bt);
    const float hiss = t->hiss, wow = t->wow, sat = t->saturation, tone = t->tone, spk = t->speaker, crackle = t->crackle, drop = t->dropouts, crush = t->crush;
    const double noise = t->noiseGain;   // volume of the added hiss and crackle
    if (hiss <= 0.f && wow <= 0.f && sat <= 0.f && tone <= 0.f && spk <= 0.f && crackle <= 0.f && drop <= 0.f && crush <= 0.f)
        return GST_FLOW_OK;   // untouched
    GstMapInfo map;
    if (!gst_buffer_map(buf, &map, GST_MAP_READWRITE)) return GST_FLOW_ERROR;
    float* s = reinterpret_cast<float*>(map.data);
    const int ch = t->channels, frames = int(map.size / sizeof(float) / std::max(1, ch));
    const double sr = t->rate;
    if (tone != t->lastTone) { t->toneF.lowpass(sr, 20000.0 * (1.0 - tone) + 3800.0 * tone, 0.707); t->lastTone = tone; }
    if (spk != t->lastSpeaker) {
        t->hp.highpass(sr, 40.0 + 260.0 * spk, 0.8);
        t->pk.peak(sr, 2400.0, 1.1, 4.0 * spk);
        t->lp.lowpass(sr, 20000.0 * (1.0 - spk) + 5200.0 * spk, 0.8);
        t->lastSpeaker = spk;
    }
    auto& dl = *t->delay;
    const int len = int(dl[0].size());
    for (int i = 0; i < frames; ++i) {
        // tape transport: slow wow, fast flutter, and a random drift
        t->wowPh += 2 * M_PI * 0.55 / sr;
        t->flutPh += 2 * M_PI * 7.3 / sr;
        if (rnd(t->rng) < 2.0 / sr) t->driftTarget = rnd(t->rng) * 2 - 1;
        t->drift += (t->driftTarget - t->drift) * (1.0 / sr);
        // at full: about +-0.6 % slow wow and +-0.5 % flutter (a badly worn tape); the presets use less
        const double delaySamples = sr * (0.012 + wow * (0.0017 * std::sin(t->wowPh) + 0.00011 * std::sin(t->flutPh) + 0.0006 * t->drift));
        // dropouts: brief dips in level
        if (t->dipLeft <= 0 && rnd(t->rng) < drop * 0.9 / sr) t->dipLeft = sr * (0.03 + 0.05 * rnd(t->rng));
        const double dipTarget = t->dipLeft > 0 ? 0.25 : 1.0;
        t->dipGain += (dipTarget - t->dipGain) * (200.0 / sr);
        if (t->dipLeft > 0) t->dipLeft -= 1;
        const bool pop = crackle > 0 && rnd(t->rng) < crackle * 9.0 / sr;
        const double popAmp = pop ? (rnd(t->rng) * 0.3 + 0.1) * (rnd(t->rng) < 0.5 ? -1 : 1) : 0.0;
        const bool tick = crackle > 0 && rnd(t->rng) < crackle * 120.0 / sr;
        // console PCM: a new sample only every so often (down to 11 kHz), held in between
        bool newSample = false;
        if (crush > 0) {
            t->holdPh += (sr * (1.0 - crush) + std::min(sr, 11025.0) * crush) / sr;
            if (t->holdPh >= 1.0) { t->holdPh -= 1.0; newSample = true; }
        }
        float mono = 0.f;
        for (int c = 0; c < ch; ++c) {
            float& x = s[i * ch + c];
            dl[c][t->writePos] = x;
            double v = x;
            if (wow > 0) {   // read the delay line at the wobbling position (linear interpolation)
                double rp = t->writePos - delaySamples;
                while (rp < 0) rp += len;
                const int i0 = int(rp) % len, i1 = (i0 + 1) % len;
                const double fr = rp - std::floor(rp);
                v = dl[c][i0] * (1 - fr) + dl[c][i1] * fr;
            }
            if (tone > 0) v = (*t->tape)[c].run(t->toneF, v);
            if (hiss > 0) {   // tape hiss: white noise with its lows taken out
                const double w = rnd(t->rng) * 2 - 1;
                float& hpS = (*t->hissHp)[c];
                const double hissed = w - hpS;
                hpS += float((w - hpS) * 0.15);
                v += hissed * hiss * 0.045 * noise;
            }
            if (sat > 0) v = (1 - sat) * v + sat * std::tanh(2.5 * v) / std::tanh(2.5);
            v *= t->dipGain;
            if (crackle > 0) {   // a pop is a short decaying click; ticks are tiny ones
                float& cl = (*t->click)[c];
                if (pop) cl = float(popAmp);
                if (tick) cl += float((rnd(t->rng) - 0.5) * 0.04);
                v += cl * noise;
                cl *= float(std::exp(-1.0 / (sr * 0.0007)));
            }
            if (crush > 0) {   // ... and stored with few bits (16 down to 8)
                float& h = (*t->held)[c];
                if (newSample) {
                    const double levels = std::pow(2.0, 15.0 - 8.0 * crush);
                    h = float(std::round(std::clamp(v, -1.0, 1.0) * levels) / levels);
                }
                v = h;
            }
            if (spk > 0) {
                auto* st = &(*t->spk)[size_t(c) * 3];
                v = st[0].run(t->hp, v);
                v = st[1].run(t->pk, v);
                v = st[2].run(t->lp, v);
            }
            x = float(v);
            mono += x;
        }
        if (spk > 0 && ch > 1) {   // a single small speaker: mono as it gets smaller
            mono /= ch;
            for (int c = 0; c < ch; ++c) s[i * ch + c] = s[i * ch + c] * (1 - spk) + mono * spk;
        }
        t->writePos = (t->writePos + 1) % len;
    }
    gst_buffer_unmap(buf, &map);
    return GST_FLOW_OK;
}

static void crt_tape_finalize(GObject* o)
{
    CrtTape* t = CRT_TAPE(o);
    delete t->delay; delete t->tape; delete t->spk; delete t->hissHp; delete t->click; delete t->held;
    G_OBJECT_CLASS(crt_tape_parent_class)->finalize(o);
}

static void crt_tape_class_init(CrtTapeClass* klass)
{
    G_OBJECT_CLASS(klass)->finalize = crt_tape_finalize;
    GstElementClass* ec = GST_ELEMENT_CLASS(klass);
    gst_element_class_set_static_metadata(ec, "CRT Player tape sound", "Filter/Effect/Audio",
                                          "Tape hiss, wow and flutter, saturation, treble loss, a small speaker, crackle", "CRT Player");
    GstCaps* caps = gst_caps_from_string("audio/x-raw, format=(string)" GST_AUDIO_NE(F32) ", layout=(string)interleaved, "
                                         "rate=(int)[1, MAX], channels=(int)[1, MAX]");
    gst_audio_filter_class_add_pad_templates(GST_AUDIO_FILTER_CLASS(klass), caps);
    gst_caps_unref(caps);
    GST_AUDIO_FILTER_CLASS(klass)->setup = crt_tape_setup;
    GST_BASE_TRANSFORM_CLASS(klass)->transform_ip = crt_tape_transform_ip;
    GST_BASE_TRANSFORM_CLASS(klass)->transform_ip_on_passthrough = FALSE;
}

static void crt_tape_init(CrtTape* t)
{
    t->hiss = t->wow = t->saturation = t->tone = t->speaker = t->crackle = t->dropouts = t->crush = 0.f;
    t->noiseGain = 1.f;
    t->rate = 48000; t->channels = 2;
    t->delay = new std::vector<std::vector<float>>(2, std::vector<float>(2400, 0.f));
    t->tape = new std::vector<BiquadState>(2);
    t->spk = new std::vector<BiquadState>(6);
    t->hissHp = new std::vector<float>(2, 0.f);
    t->click = new std::vector<float>(2, 0.f);
    t->held = new std::vector<float>(2, 0.f);
    t->holdPh = 1.0;
    t->writePos = 0;
    t->wowPh = t->flutPh = t->drift = t->driftTarget = 0;
    t->dipGain = 1; t->dipLeft = 0;
    t->rng = 0x9E3779B97F4A7C15ull;
    t->lastTone = t->lastSpeaker = -1.f;
    gst_base_transform_set_in_place(GST_BASE_TRANSFORM(t), TRUE);
}

void crtTapeRegister()
{
    static bool done = false;
    if (done) return;
    done = true;
    gst_element_register(nullptr, "crttape", GST_RANK_NONE, CRT_TYPE_TAPE);
}

void crtTapeSetParams(GstElement* e, const TapeParams& p)
{
    if (!e || !CRT_IS_TAPE(e)) return;
    CrtTape* t = CRT_TAPE(e);
    t->hiss = p.hiss; t->wow = p.wow; t->saturation = p.saturation; t->tone = p.tone;
    t->speaker = p.speaker; t->crackle = p.crackle; t->dropouts = p.dropouts; t->crush = p.crush;
    t->noiseGain = std::clamp(p.noiseGain, 0.f, 2.f);
}
