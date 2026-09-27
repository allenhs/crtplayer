// Tape and speaker sound ("crttape"): generated audio through the element, measured.
#include "playback/TapeAudio.h"
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

static const int kRate = 48000;
static int g_fail = 0;
static void check(const char* name, bool ok, const char* detail)
{
    std::printf("%s  %s: %s\n", ok ? "PASS" : "FAIL", name, detail);
    if (!ok) ++g_fail;
}

// interleaved stereo in, interleaved stereo out
static std::vector<float> run(const std::vector<float>& in, const TapeParams& p)
{
    GstElement* pipe = gst_parse_launch("appsrc name=src format=time ! crttape name=tape ! appsink name=sink sync=false", nullptr);
    GstElement* src = gst_bin_get_by_name(GST_BIN(pipe), "src");
    GstElement* tape = gst_bin_get_by_name(GST_BIN(pipe), "tape");
    GstElement* sink = gst_bin_get_by_name(GST_BIN(pipe), "sink");
    crtTapeSetParams(tape, p);
    GstCaps* caps = gst_caps_from_string("audio/x-raw,format=F32LE,layout=interleaved,rate=48000,channels=2");
    gst_app_src_set_caps(GST_APP_SRC(src), caps);
    gst_caps_unref(caps);
    gst_element_set_state(pipe, GST_STATE_PLAYING);
    const size_t chunk = 1024 * 2;
    for (size_t i = 0; i < in.size(); i += chunk) {
        const size_t n = std::min(chunk, in.size() - i);
        GstBuffer* b = gst_buffer_new_allocate(nullptr, n * sizeof(float), nullptr);
        gst_buffer_fill(b, 0, in.data() + i, n * sizeof(float));
        GST_BUFFER_PTS(b) = gst_util_uint64_scale(i / 2, GST_SECOND, kRate);
        GST_BUFFER_DURATION(b) = gst_util_uint64_scale(n / 2, GST_SECOND, kRate);
        gst_app_src_push_buffer(GST_APP_SRC(src), b);
    }
    gst_app_src_end_of_stream(GST_APP_SRC(src));
    std::vector<float> out;
    while (GstSample* s = gst_app_sink_pull_sample(GST_APP_SINK(sink))) {
        GstBuffer* b = gst_sample_get_buffer(s);
        GstMapInfo m;
        gst_buffer_map(b, &m, GST_MAP_READ);
        const float* f = reinterpret_cast<const float*>(m.data);
        out.insert(out.end(), f, f + m.size / sizeof(float));
        gst_buffer_unmap(b, &m);
        gst_sample_unref(s);
    }
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(src); gst_object_unref(tape); gst_object_unref(sink); gst_object_unref(pipe);
    return out;
}

static std::vector<float> sine(double f, double secs, double amp = 0.5, bool rightSilent = false)
{
    std::vector<float> v(size_t(secs * kRate) * 2);
    for (size_t i = 0; i < v.size() / 2; ++i) {
        const float x = float(amp * std::sin(2 * M_PI * f * double(i) / kRate));
        v[i * 2] = x; v[i * 2 + 1] = rightSilent ? 0.f : x;
    }
    return v;
}
static double rmsDb(const std::vector<float>& v, size_t from = 0)   // left channel
{
    double s = 0; size_t n = 0;
    for (size_t i = from * 2; i < v.size(); i += 2) { s += double(v[i]) * v[i]; ++n; }
    return 10 * std::log10(s / std::max<size_t>(1, n) + 1e-20);
}
static double goertzelDb(const std::vector<float>& v, double f, size_t from)   // magnitude of f (left)
{
    const double w = 2 * M_PI * f / kRate, c = 2 * std::cos(w);
    double s1 = 0, s2 = 0; size_t n = 0;
    for (size_t i = from * 2; i < v.size(); i += 2, ++n) { const double s0 = v[i] + c * s1 - s2; s2 = s1; s1 = s0; }
    const double p = s1 * s1 + s2 * s2 - c * s1 * s2;
    return 10 * std::log10(p / (double(n) * n) + 1e-30);
}

int main(int argc, char** argv)
{
    gst_init(&argc, &argv);
    crtTapeRegister();
    char d[256];
    const size_t settle = kRate / 2;   // skip the first half second (filters settling, the delay line filling)

    {   // bypass: bit-exact
        std::vector<float> in(kRate * 2);
        uint32_t s = 12345;
        for (float& x : in) { s = s * 1664525u + 1013904223u; x = float(int32_t(s) / 2147483648.0 * 0.5); }
        const auto out = run(in, TapeParams());
        bool same = out.size() == in.size();
        for (size_t i = 0; same && i < in.size(); ++i) same = out[i] == in[i];
        std::snprintf(d, sizeof d, "%zu samples compared", in.size());
        check("with every amount at 0 the sound is untouched (bit-exact)", same, d);
    }
    {   // hiss on silence
        TapeParams p; p.hiss = 1.f;
        const double full = rmsDb(run(std::vector<float>(kRate * 4, 0.f), p), settle);
        p.hiss = 0.3f;
        const double low = rmsDb(run(std::vector<float>(kRate * 4, 0.f), p), settle);
        std::snprintf(d, sizeof d, "%.1f dBFS at full, %.1f dBFS at 0.3", full, low);
        check("tape hiss: a believable level, following the amount", full > -40 && full < -24 && full - low > 6, d);
    }
    {   // noise volume: the hiss and crackle get louder or quieter (not more or fewer)
        TapeParams p; p.hiss = 1.f;
        const double base = rmsDb(run(std::vector<float>(kRate * 4, 0.f), p), settle);
        p.noiseGain = 2.f;
        const double twice = rmsDb(run(std::vector<float>(kRate * 4, 0.f), p), settle);
        p.noiseGain = 0.f;
        const auto silent = run(std::vector<float>(kRate * 4, 0.f), p);
        double peak = 0;
        for (float x : silent) peak = std::max(peak, double(std::abs(x)));
        std::snprintf(d, sizeof d, "hiss %+.1f dB at 200%%; at 0%% the peak is %.2g", twice - base, peak);
        check("noise volume: 200% is twice as loud, 0% is silent", std::abs(twice - base - 6.02) < 0.5 && peak == 0.0, d);
        TapeParams c; c.crackle = 1.f;
        auto popPeak = [&](float g) { c.noiseGain = g; double m = 0; for (float x : run(std::vector<float>(kRate * 2 * 3, 0.f), c)) m = std::max(m, double(std::abs(x))); return m; };
        const double p1 = popPeak(1.f), p05 = popPeak(0.5f);
        std::snprintf(d, sizeof d, "loudest pop %.3f at 100%%, %.3f at 50%%", p1, p05);
        check("noise volume scales the crackle's loudness", std::abs(p05 / p1 - 0.5) < 0.02, d);
    }
    {   // TV speaker: band-limited, boxy, mono
        TapeParams p; p.speaker = 1.f;
        const double ref = goertzelDb(sine(1000, 2), 1000, settle);
        const double g100 = goertzelDb(run(sine(100, 2), p), 100, settle) - goertzelDb(sine(100, 2), 100, settle);
        const double g1k = goertzelDb(run(sine(1000, 2), p), 1000, settle) - ref;
        const double g10k = goertzelDb(run(sine(10000, 2), p), 10000, settle) - goertzelDb(sine(10000, 2), 10000, settle);
        std::snprintf(d, sizeof d, "100 Hz %+.1f dB, 1 kHz %+.1f dB, 10 kHz %+.1f dB", g100, g1k, g10k);
        check("TV speaker: thin bass, mids kept, no highs", g100 < -12 && std::abs(g1k) < 4 && g10k < -12, d);
        const auto st = run(sine(1000, 1, 0.5, true), p);
        double maxDiff = 0;
        for (size_t i = settle * 2; i + 1 < st.size(); i += 2) maxDiff = std::max(maxDiff, double(std::abs(st[i] - st[i + 1])));
        std::snprintf(d, sizeof d, "left-only input: left/right differ by at most %.2g", maxDiff);
        check("TV speaker at full is mono", maxDiff < 1e-5, d);
    }
    {   // treble loss
        TapeParams p; p.tone = 1.f;
        const double g1k = goertzelDb(run(sine(1000, 2), p), 1000, settle) - goertzelDb(sine(1000, 2), 1000, settle);
        const double g10k = goertzelDb(run(sine(10000, 2), p), 10000, settle) - goertzelDb(sine(10000, 2), 10000, settle);
        std::snprintf(d, sizeof d, "1 kHz %+.1f dB, 10 kHz %+.1f dB", g1k, g10k);
        check("treble loss: highs cut, mids kept", std::abs(g1k) < 3 && g10k < -15, d);
    }
    {   // wow and flutter: the pitch of a steady tone wobbles
        auto spread = [&](const std::vector<float>& v) {   // spread of periods (from rising zero crossings), in %
            std::vector<double> xs;
            for (size_t i = settle * 2 + 2; i < v.size(); i += 2)
                if (v[i - 2] < 0 && v[i] >= 0) xs.push_back(double(i / 2 - 1) + v[i - 2] / (v[i - 2] - v[i]));
            std::vector<double> per;   // average period over 20 cycles
            for (size_t k = 20; k < xs.size(); k += 20) per.push_back((xs[k] - xs[k - 20]) / 20);
            double lo = 1e9, hi = 0, sum = 0;
            for (double p : per) { lo = std::min(lo, p); hi = std::max(hi, p); sum += p; }
            return per.empty() ? 0.0 : (hi - lo) / (sum / per.size()) * 100;
        };
        TapeParams p; p.wow = 1.f;
        const double on = spread(run(sine(1000, 4), p));
        TapeParams q; q.tone = 0.001f;   // not bypassed, but no wow
        const double off = spread(run(sine(1000, 4), q));
        std::snprintf(d, sizeof d, "pitch spread %.2f%% with wow and flutter, %.3f%% without", on, off);
        check("wow and flutter: the pitch wobbles, within what a worn tape does", on > 0.3 && on < 3.0 && off < 0.05, d);
    }
    {   // saturation: harmonics on a loud tone
        TapeParams p; p.saturation = 1.f;
        const auto in = sine(1000, 2, 0.9), out = run(in, p);
        const double h3 = goertzelDb(out, 3000, settle) - goertzelDb(out, 1000, settle);
        const double h3in = goertzelDb(in, 3000, settle) - goertzelDb(in, 1000, settle);
        std::snprintf(d, sizeof d, "3rd harmonic %.0f dB below the tone (%.0f dB without)", -h3, -h3in);
        check("tape saturation: warm harmonics", h3 > -40 && h3in < -80, d);
    }
    {   // crackle on silence
        TapeParams p; p.crackle = 1.f;
        const auto out = run(std::vector<float>(kRate * 2 * 3, 0.f), p);
        int pops = 0;
        for (size_t i = 2; i < out.size(); i += 2) if (std::abs(out[i]) > 0.05f && std::abs(out[i - 2]) <= 0.05f) ++pops;
        std::snprintf(d, sizeof d, "%d pops in 3 s of silence", pops);
        check("film crackle: pops", pops >= 10, d);
    }
    {   // dropouts: dips in level
        TapeParams p; p.dropouts = 1.f;
        const auto out = run(sine(1000, 6), p);
        const size_t win = kRate / 100;   // 10 ms
        int dips = 0; bool in = false;
        for (size_t w = settle; w + win < out.size() / 2; w += win) {
            double s = 0;
            for (size_t i = w; i < w + win; ++i) s += double(out[i * 2]) * out[i * 2];
            const double r = std::sqrt(s / win) / (0.5 / std::sqrt(2.0));
            if (r < 0.5 && !in) { ++dips; in = true; } else if (r > 0.8) in = false;
        }
        std::snprintf(d, sizeof d, "%d dips in 5.5 s", dips);
        check("dropouts: the level dips now and then", dips >= 1, d);
    }
    std::printf(g_fail ? "\n%d tape sound check(s) failed\n" : "\nAll tape sound checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}
