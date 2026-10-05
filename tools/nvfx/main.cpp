// crtplayer-nvfx: the helper through which CRT Player uses NVIDIA's Video Super Resolution
// and Video Frame Generation (the NVIDIA Video Effects SDK, installed separately).
//   --serve   at work for the player (Serve.cpp; the player starts it this way)
//   --where   says where the SDK is and which features it has, without loading it
//   --probe   runs both effects on test pictures at several sizes and settings, reports how long
//             each frame takes, checks the results against the true pictures, and saves a few crops
#include "NvFx.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

FILE* gReport = nullptr;
void say(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    fputs(buf, stdout);
    fflush(stdout);
    if (gReport) { fputs(buf, gReport); fflush(gReport); }
}

struct Picture {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // RGBA
    Picture() = default;
    Picture(int w_, int h_) : w(w_), h(h_), px(size_t(w_) * h_ * 4, 255) {}
    int pitch() const { return w * 4; }
};

// A test scene, the same at any size: a panning background of soft bands, a fine diagonal
// weave (detail to restore), and three things moving their own ways. `t` in frames.
Picture renderScene(int w, int h, double t)
{
    Picture p(w, h);
    const double sx = 1920.0 / w, sy = 1080.0 / h;   // scene coordinates are 1920 x 1080
    const double pan = t * 9.0;                       // the background moves 9 scene pixels a frame
    const double cx = 500 + t * 14.0, cy = 420 + t * 6.0;        // a disc
    const double bx = 1400 - t * 11.0, by = 620;                 // a striped box
    const double qx = 960 + 220 * std::sin(t * 0.21), qy = 240;  // a small square on a curve
    auto rows = [&](int from, int to) {
        for (int y = from; y < to; ++y) {
            uint8_t* row = &p.px[size_t(y) * w * 4];
            const double Y = (y + 0.5) * sy;
            for (int x = 0; x < w; ++x) {
                const double X = (x + 0.5) * sx, U = X + pan;
                double r = 0.45 + 0.25 * std::sin(U * 0.011) + 0.10 * std::sin(Y * 0.017);
                double g = 0.42 + 0.22 * std::sin(U * 0.007 + 1.3) + 0.12 * std::sin((U + Y) * 0.009);
                double b = 0.50 + 0.25 * std::sin(Y * 0.012 + 0.6) + 0.10 * std::sin(U * 0.019);
                if (Y > 760) {
                    const double weave = 0.5 + 0.5 * std::sin((U + Y) * 0.9) * std::sin((U - Y) * 0.55);   // fine detail
                    r = r * 0.6 + 0.4 * weave; g = g * 0.6 + 0.4 * weave; b = b * 0.6 + 0.35 * weave;
                }
                const double dc = std::hypot(X - cx, Y - cy);
                if (dc < 130) { const double e = std::min(1.0, (130 - dc) / 2.0); r += (0.95 - r) * e; g += (0.75 - g) * e; b += (0.15 - b) * e; }
                if (std::abs(X - bx) < 150 && std::abs(Y - by) < 90) {
                    const bool stripe = std::fmod(std::abs(X - bx + 1000), 24.0) < 12.0;
                    r = stripe ? 0.92 : 0.10; g = stripe ? 0.92 : 0.25; b = stripe ? 0.95 : 0.55;
                }
                if (std::abs(X - qx) < 45 && std::abs(Y - qy) < 45) {
                    const bool chk = (int((X - qx + 45) / 9) + int((Y - qy + 45) / 9)) % 2 == 0;
                    r = chk ? 0.85 : 0.2; g = chk ? 0.2 : 0.8; b = 0.3;
                }
                row[x * 4] = uint8_t(std::clamp(r, 0.0, 1.0) * 255.0 + 0.5);
                row[x * 4 + 1] = uint8_t(std::clamp(g, 0.0, 1.0) * 255.0 + 0.5);
                row[x * 4 + 2] = uint8_t(std::clamp(b, 0.0, 1.0) * 255.0 + 0.5);
                row[x * 4 + 3] = 255;
            }
        }
    };
    const int n = int(std::clamp(std::thread::hardware_concurrency(), 1u, 16u));
    std::vector<std::thread> pool;
    for (int i = 0; i < n; ++i) pool.emplace_back(rows, h * i / n, h * (i + 1) / n);
    for (std::thread& th : pool) th.join();
    return p;
}
// (the same pictures are wanted by several cases: they are kept until the size changes)
const Picture& scene(int w, int h, double t)
{
    static std::map<int, Picture> kept;
    static int keptW = 0, keptH = 0;
    if (w != keptW || h != keptH) { kept.clear(); keptW = w; keptH = h; }
    const int key = int(std::lround(t * 16));
    auto it = kept.find(key);
    if (it == kept.end()) it = kept.emplace(key, renderScene(w, h, t)).first;
    return it->second;
}

// The picture at 1/n of its size, each pixel the mean of n x n (what a video of lower resolution holds).
Picture shrink(const Picture& s, int n)
{
    Picture d(s.w / n, s.h / n);
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x)
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                for (int j = 0; j < n; ++j) for (int i = 0; i < n; ++i) sum += s.px[(size_t(y * n + j) * s.w + x * n + i) * 4 + c];
                d.px[(size_t(y) * d.w + x) * 4 + c] = uint8_t((sum + n * n / 2) / (n * n));
            }
    return d;
}

Picture bilinear(const Picture& s, int w, int h)
{
    Picture d(w, h);
    for (int y = 0; y < h; ++y) {
        const double fy = std::clamp((y + 0.5) * s.h / h - 0.5, 0.0, s.h - 1.0);
        const int y0 = int(fy), y1 = std::min(y0 + 1, s.h - 1);
        const double wy = fy - y0;
        for (int x = 0; x < w; ++x) {
            const double fx = std::clamp((x + 0.5) * s.w / w - 0.5, 0.0, s.w - 1.0);
            const int x0 = int(fx), x1 = std::min(x0 + 1, s.w - 1);
            const double wx = fx - x0;
            for (int c = 0; c < 3; ++c) {
                const double v = (s.px[(size_t(y0) * s.w + x0) * 4 + c] * (1 - wx) + s.px[(size_t(y0) * s.w + x1) * 4 + c] * wx) * (1 - wy)
                               + (s.px[(size_t(y1) * s.w + x0) * 4 + c] * (1 - wx) + s.px[(size_t(y1) * s.w + x1) * 4 + c] * wx) * wy;
                d.px[(size_t(y) * w + x) * 4 + c] = uint8_t(v + 0.5);
            }
        }
    }
    return d;
}

Picture mix(const Picture& a, const Picture& b)
{
    Picture d(a.w, a.h);
    for (size_t i = 0; i < d.px.size(); ++i) d.px[i] = uint8_t((a.px[i] + b.px[i] + 1) / 2);
    return d;
}

double psnr(const Picture& a, const Picture& b)
{
    if (a.w != b.w || a.h != b.h) return 0.0;
    double se = 0;
    for (size_t i = 0; i < a.px.size(); i += 4)
        for (int c = 0; c < 3; ++c) { const double e = double(a.px[i + c]) - b.px[i + c]; se += e * e; }
    const double mse = se / (double(a.w) * a.h * 3);
    return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

Picture crop(const Picture& s, int x, int y, int w, int h)
{
    x = std::clamp(x, 0, std::max(0, s.w - w)); y = std::clamp(y, 0, std::max(0, s.h - h));
    w = std::min(w, s.w); h = std::min(h, s.h);
    Picture d(w, h);
    for (int j = 0; j < h; ++j) std::memcpy(&d.px[size_t(j) * w * 4], &s.px[(size_t(y + j) * s.w + x) * 4], size_t(w) * 4);
    return d;
}

// PNG without a compression library: the picture is stored as it is (zlib's "stored" blocks).
uint32_t crc32Of(uint32_t crc, const uint8_t* data, size_t n)
{
    static uint32_t table[256];
    static bool made = false;
    if (!made) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        made = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = table[(crc ^ data[i]) & 255] ^ (crc >> 8);
    return ~crc;
}

bool savePng(const std::string& path, const Picture& p)
{
    std::vector<uint8_t> raw(size_t(p.h) * (p.w * 3 + 1));
    for (int y = 0; y < p.h; ++y) {
        uint8_t* row = &raw[size_t(y) * (p.w * 3 + 1)];
        *row++ = 0;
        for (int x = 0; x < p.w; ++x) { const uint8_t* s = &p.px[(size_t(y) * p.w + x) * 4]; *row++ = s[0]; *row++ = s[1]; *row++ = s[2]; }
    }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;   // Adler-32 of the data
    for (size_t at = 0; at < raw.size() || at == 0;) {
        const size_t n = std::min<size_t>(65535, raw.size() - at);
        const bool last = at + n >= raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(n)); z.push_back(uint8_t(n >> 8)); z.push_back(uint8_t(~n)); z.push_back(uint8_t(~n >> 8));
        z.insert(z.end(), raw.begin() + long(at), raw.begin() + long(at + n));
        for (size_t i = at; i < at + n; ++i) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
        at += n;
        if (last) break;
    }
    const uint32_t adler = (b << 16) | a;
    for (int s = 24; s >= 0; s -= 8) z.push_back(uint8_t(adler >> s));
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    auto chunk = [f](const char* type, const uint8_t* data, uint32_t n) {
        const uint8_t be[4] = {uint8_t(n >> 24), uint8_t(n >> 16), uint8_t(n >> 8), uint8_t(n)};
        fwrite(be, 1, 4, f);
        fwrite(type, 1, 4, f);
        if (n) fwrite(data, 1, n, f);
        uint32_t crc = crc32Of(0, reinterpret_cast<const uint8_t*>(type), 4);
        if (n) crc = crc32Of(crc, data, n);
        const uint8_t c[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
        fwrite(c, 1, 4, f);
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    const uint8_t ihdr[13] = {uint8_t(p.w >> 24), uint8_t(p.w >> 16), uint8_t(p.w >> 8), uint8_t(p.w),
                              uint8_t(p.h >> 24), uint8_t(p.h >> 16), uint8_t(p.h >> 8), uint8_t(p.h), 8, 2, 0, 0, 0};
    chunk("IHDR", ihdr, 13);
    chunk("IDAT", z.data(), uint32_t(z.size()));
    chunk("IEND", nullptr, 0);
    fclose(f);
    return true;
}

struct Stats {
    std::vector<double> v;
    void add(double x) { v.push_back(x); }
    double mean() const { double s = 0; for (double x : v) s += x; return v.empty() ? 0 : s / v.size(); }
    double pct(double p) const
    {
        if (v.empty()) return 0;
        std::vector<double> s = v;
        std::sort(s.begin(), s.end());
        return s[std::min(s.size() - 1, size_t(p * s.size()))];
    }
};

const char* qualityName(int q)
{
    switch (q) {
    case 1: return "Low";
    case 2: return "Medium";
    case 3: return "High";
    case 4: return "Ultra";
    case 21: return "Streaming Medium";
    case 23: return "Streaming Ultra";
    default: return "?";
    }
}

int failures = 0;

// Video Super Resolution: a video of srcW x srcH shown at `factor` times its size.
void probeSuperRes(int srcW, int srcH, int factor, int quality, bool pinned, int frames, const std::string& outDir, bool saveCrops)
{
    const int dstW = srcW * factor, dstH = srcH * factor;
    say("VSR  %4dx%-4d -> %4dx%-4d  %-16s %-6s ", srcW, srcH, dstW, dstH, qualityName(quality), pinned ? "pinned" : "gpu");
    // four moments of the scene: the true picture at the large size, and the video's (smaller) frame
    std::vector<Picture> truth, small;
    for (int i = 0; i < 4; ++i) { truth.push_back(scene(dstW, dstH, i * 3.0)); small.push_back(shrink(truth.back(), factor)); }
    NvSuperRes sr;
    std::string err;
    const Clock::time_point t0 = Clock::now();
    if (!sr.open(srcW, srcH, dstW, dstH, quality, pinned, &err)) { say("FAILED to open: %s\n", err.c_str()); ++failures; return; }
    const double loadMs = msSince(t0);
    Picture out(dstW, dstH);
    Stats up, run, down, total;
    for (int i = 0; i < frames + 5; ++i) {
        NvTiming t;
        if (!sr.run(small[i % 4].px.data(), small[i % 4].pitch(), out.px.data(), out.pitch(), &t, &err)) { say("FAILED: %s\n", err.c_str()); ++failures; return; }
        if (i < 5) continue;   // the first few runs include one-time work
        up.add(t.uploadMs); run.add(t.runMs); down.add(t.downloadMs); total.add(t.total());
    }
    // quality, on the frame last processed
    const int last = (frames + 4) % 4;
    const double got = psnr(out, truth[last]), plain = psnr(bilinear(small[last], dstW, dstH), truth[last]);
    say("load %5.0f ms | upload %5.2f  run %6.2f  download %5.2f | total %6.2f ms = %5.1f frames/s (slowest 5%%: %6.2f ms) | %.1f dB against the true picture (plain upscale %.1f)\n",
        loadMs, up.mean(), run.mean(), down.mean(), total.mean(), 1000.0 / std::max(0.001, total.mean()), total.pct(0.95), got, plain);
    if (saveCrops) {
        const int cx = dstW * 55 / 100, cy = dstH * 62 / 100;   // the striped box over the weave
        savePng(outDir + "/vsr_1_plain_upscale.png", crop(bilinear(small[last], dstW, dstH), cx, cy, 640, 400));
        savePng(outDir + "/vsr_2_nvidia.png", crop(out, cx, cy, 640, 400));
        savePng(outDir + "/vsr_3_true_picture.png", crop(truth[last], cx, cy, 640, 400));
    }
}

// Video Frame Generation: frames two steps of the scene apart, and the frame generated halfway, against the true one.
void probeFrameGen(int w, int h, int mode, bool pinned, int pairs, const std::string& outDir, bool saveCrops)
{
    static const char* modes[] = {"low", "medium", "high"};
    say("VFG  %4dx%-4d  %-6s %-6s ", w, h, modes[std::clamp(mode, 0, 2)], pinned ? "pinned" : "gpu");
    std::vector<Picture> frame;   // even moments are the video's frames, odd ones the true in-betweens
    for (int i = 0; i < 9; ++i) frame.push_back(scene(w, h, double(i)));
    NvFrameGen fg;
    std::string err;
    const Clock::time_point t0 = Clock::now();
    if (!fg.open(w, h, mode, pinned, &err)) { say("FAILED to open: %s\n", err.c_str()); ++failures; return; }
    const double loadMs = msSince(t0);
    Picture out(w, h), kept;
    Stats push, run, down, total;
    int keptAt = -1;
    NvTiming t;
    if (!fg.push(frame[0].px.data(), frame[0].pitch(), false, &t, &err)) { say("FAILED: %s\n", err.c_str()); ++failures; return; }
    for (int i = 0; i < pairs + 3; ++i) {
        const int k = (i % 4) * 2 + 2;                 // 2, 4, 6, 8, then round again (a jump back: a shot change)
        const bool cut = i > 0 && k == 2;
        NvTiming tp, tg;
        if (!fg.push(frame[k].px.data(), frame[k].pitch(), cut, &tp, &err)) { say("FAILED: %s\n", err.c_str()); ++failures; return; }
        if (!fg.generate(0.5f, out.px.data(), out.pitch(), &tg, &err)) { say("FAILED: %s\n", err.c_str()); ++failures; return; }
        if (!cut && k == 4) { kept = out; keptAt = 3; }   // between moments 2 and 4: the true picture is moment 3
        if (i < 3) continue;
        push.add(tp.uploadMs); run.add(tg.runMs); down.add(tg.downloadMs); total.add(tp.uploadMs + tg.total());
    }
    double got = 0, plain = 0;
    if (keptAt > 0) { got = psnr(kept, frame[keptAt]); plain = psnr(mix(frame[keptAt - 1], frame[keptAt + 1]), frame[keptAt]); }
    say("load %5.0f ms | new frame %5.2f  generate %6.2f  download %5.2f | total %6.2f ms per generated frame (slowest 5%%: %6.2f) | %.1f dB against the true in-between (a plain mix %.1f)\n",
        loadMs, push.mean(), run.mean(), down.mean(), total.mean(), total.pct(0.95), got, plain);
    if (saveCrops && keptAt > 0) {
        const int cx = w * 18 / 100, cy = h * 25 / 100;   // the disc
        savePng(outDir + "/vfg_1_frame_before.png", crop(frame[keptAt - 1], cx, cy, 640, 400));
        savePng(outDir + "/vfg_2_nvidia_generated.png", crop(kept, cx, cy, 640, 400));
        savePng(outDir + "/vfg_3_true_in_between.png", crop(frame[keptAt], cx, cy, 640, 400));
        savePng(outDir + "/vfg_4_frame_after.png", crop(frame[keptAt + 1], cx, cy, 640, 400));
        savePng(outDir + "/vfg_5_plain_mix.png", crop(mix(frame[keptAt - 1], frame[keptAt + 1]), cx, cy, 640, 400));
    }
}

int probe(const std::string& sdk, const std::string& outDir, bool quick)
{
    mkdir(outDir.c_str(), 0755);
    gReport = fopen((outDir + "/report.txt").c_str(), "w");
    say("crtplayer-nvfx probe\nSDK folder: %s\n", sdk.c_str());
    std::string err;
    if (!nvInit(sdk, &err)) { say("The SDK could not be loaded: %s\nLibrary path: %s\n", err.c_str(), getenv("LD_LIBRARY_PATH") ? getenv("LD_LIBRARY_PATH") : ""); return 2; }
    say("SDK version: %s\nFeatures installed:", nvVersion().c_str());
    for (const std::string& f : nvFeatures(sdk)) say(" %s", f.c_str());
    say("\nTimes are per frame, in milliseconds, for pictures going in from and coming back to ordinary memory.\n");
    say("A 60 frames-a-second screen leaves 16.7 ms a frame; a 24 frames-a-second film arrives every 41.7 ms.\n\n");
    const int n = quick ? 20 : 60;
    say("-- Video Super Resolution\n");
    probeSuperRes(1920, 1080, 2, NvSrHigh, false, n, outDir, true);
    probeSuperRes(1920, 1080, 2, NvSrLow, false, n, outDir, false);
    probeSuperRes(1920, 1080, 2, NvSrUltra, false, n, outDir, false);
    probeSuperRes(1920, 1080, 2, NvSrStreamingMedium, false, n, outDir, false);
    probeSuperRes(1920, 1080, 2, NvSrStreamingUltra, false, n, outDir, false);
    probeSuperRes(1920, 1080, 2, NvSrHigh, true, n, outDir, false);
    probeSuperRes(1280, 720, 2, NvSrHigh, false, n, outDir, false);
    probeSuperRes(1280, 720, 3, NvSrHigh, false, n, outDir, false);
    probeSuperRes(1280, 720, 3, NvSrUltra, false, n, outDir, false);
    probeSuperRes(720, 480, 3, NvSrHigh, false, n, outDir, false);
    probeSuperRes(720, 480, 4, NvSrHigh, false, n, outDir, false);
    probeSuperRes(640, 360, 3, NvSrHigh, false, n, outDir, false);
    say("\n-- Video Frame Generation\n");
    probeFrameGen(1920, 1080, 1, false, n, outDir, true);
    probeFrameGen(1920, 1080, 0, false, n, outDir, false);
    probeFrameGen(1920, 1080, 2, false, n, outDir, false);
    probeFrameGen(1920, 1080, 1, true, n, outDir, false);
    probeFrameGen(1280, 720, 1, false, n, outDir, false);
    probeFrameGen(1280, 720, 2, false, n, outDir, false);
    probeFrameGen(720, 480, 1, false, n, outDir, false);
    probeFrameGen(3840, 2160, 0, false, n / 2, outDir, false);
    probeFrameGen(3840, 2160, 1, false, n / 2, outDir, false);
    probeFrameGen(3840, 2160, 2, false, n / 2, outDir, false);
    say("\n%s\nThe report and the pictures are in: %s\n", failures ? "Some cases failed (see above)." : "Every case ran.", outDir.c_str());
    if (gReport) fclose(gReport);
    return failures ? 1 : 0;
}
} // namespace

int nvServe(const std::string& sdk);   // Serve.cpp

int main(int argc, char** argv)
{
    std::string sdkHint, outDir = "nvfx-probe";
    bool doProbe = false, quick = false, doServe = false, doWhere = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--probe") doProbe = true;
        else if (a == "--serve") doServe = true;
        else if (a == "--where") doWhere = true;
        else if (a == "--quick") quick = true;
        else if (a == "--sdk" && i + 1 < argc) sdkHint = argv[++i];
        else if (a == "--out" && i + 1 < argc) outDir = argv[++i];
        else {
            printf("crtplayer-nvfx: NVIDIA video effects for CRT Player\n"
                   "  --serve          work for the player (started by it)\n"
                   "  --where          say where the SDK is and what it has\n"
                   "  --probe          measure Video Super Resolution and Video Frame Generation, and check their results\n"
                   "  --quick          fewer frames per case\n"
                   "  --sdk FOLDER     where the NVIDIA Video Effects SDK is (default: looked for in the usual places)\n"
                   "  --out FOLDER     where the report and pictures go (default: ./nvfx-probe)\n");
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    const std::string sdk = nvFindSdk(sdkHint);
    if (doWhere) {
        if (sdk.empty()) { printf("sdk=\n"); return 3; }
        std::string feats;
        for (const std::string& f : nvFeatures(sdk)) feats += (feats.empty() ? "" : ",") + f;
        printf("sdk=%s\nfeatures=%s\n", sdk.c_str(), feats.c_str());
        return 0;
    }
    if (sdk.empty() && doServe) return nvServe(sdk);   // (it answers every request with that fact)
    if (sdk.empty()) {
        printf("The NVIDIA Video Effects SDK was not found%s%s.\nLooked in: $CRTPLAYER_NVFX_SDK, ~/.local/share/crtplayer/VideoFX, ~/.local/share/VideoFX, ~/VideoFX, /usr/local/VideoFX, /var/usrlocal/VideoFX, /opt/VideoFX\n",
               sdkHint.empty() ? "" : " in ", sdkHint.c_str());
        return 2;
    }
    // The SDK's libraries find one another through LD_LIBRARY_PATH, which is read when a
    // program starts: so the program starts itself once more with the path set.
    if (!getenv("CRTPLAYER_NVFX_STARTED")) {
        std::string path = nvLibraryPath(sdk);
        if (const char* old = getenv("LD_LIBRARY_PATH")) if (*old) path += std::string(":") + old;
        setenv("LD_LIBRARY_PATH", path.c_str(), 1);
        setenv("CRTPLAYER_NVFX_STARTED", "1", 1);
        execv("/proc/self/exe", argv);
        perror("could not restart with the SDK's library path");
        return 2;
    }
    if (doServe) return nvServe(sdk);
    if (doProbe) return probe(sdk, outDir, quick);
    printf("Nothing to do: try --probe (or --help).\n");
    return 2;
}
