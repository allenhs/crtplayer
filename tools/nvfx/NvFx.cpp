#include "NvFx.h"
#include "NvProxy.h"

#include "nvCVImage.h"
#include "nvVideoEffects.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

// The two features' selectors (the SDK's feature headers define the same strings).
#define FX_SUPER_RES "VideoSuperRes"
#define FX_SR_QUALITY "QualityLevel"
#define FX_SR_ENCODING "ImageEncodingMode"
#define FX_FRAME_GEN "VideoFrameGeneration"
#define FX_FG_MULTIPLIER "FrameMultiplier"
#define FX_FG_TIMESTEP "Timestep"
#define FX_FG_SHOT_CHANGE "ShotChange"
#define FX_FG_AUTO_SHOT "AutomaticShotChangeDetectionEnabled"
#define FX_FG_MODE "Mode"

namespace {
using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }

bool isDir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
bool isFile(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode); }

bool failed(NvCV_Status st, const char* what, std::string* error)
{
    if (st == NVCV_SUCCESS) return false;
    if (error) *error = std::string(what) + ": " + NvCV_GetErrorStringFromCode(st) + " (" + std::to_string(int(st)) + ")";
    return true;
}
// A setting that not every version of a feature knows: "no such setting" is not an error.
bool failedOptional(NvCV_Status st, const char* what, std::string* error) { return st != NVCV_ERR_SELECTOR && failed(st, what, error); }

void copyRows(const uint8_t* src, int srcPitch, uint8_t* dst, int dstPitch, int width, int height)
{
    for (int y = 0; y < height; ++y) std::memcpy(dst + size_t(y) * dstPitch, src + size_t(y) * srcPitch, size_t(width) * 4);
}
// The caller's RGBA picture, described for the SDK (no copy).
void wrap(NvCVImage& im, const uint8_t* pixels, int width, int height, int pitch)
{
    NvCVImage_Init(&im, unsigned(width), unsigned(height), pitch, const_cast<uint8_t*>(pixels), NVCV_RGBA, NVCV_U8, NVCV_CHUNKY, NVCV_CPU);
}
} // namespace

std::string nvFindSdk(const std::string& hint)
{
    if (!hint.empty()) return isFile(hint + "/lib/libVideoFX.so") ? hint : std::string();   // a folder named by hand: that one or none
    std::vector<std::string> tries;
    if (const char* e = std::getenv("CRTPLAYER_NVFX_SDK")) tries.push_back(e);
    if (const char* home = std::getenv("HOME")) {
        tries.push_back(std::string(home) + "/.local/share/crtplayer/VideoFX");
        tries.push_back(std::string(home) + "/.local/share/VideoFX");
        tries.push_back(std::string(home) + "/VideoFX");
    }
    tries.push_back("/usr/local/VideoFX");
    tries.push_back("/var/usrlocal/VideoFX");
    tries.push_back("/opt/VideoFX");
    for (const std::string& t : tries)
        if (isFile(t + "/lib/libVideoFX.so")) return t;
    return std::string();
}

std::vector<std::string> nvFeatures(const std::string& sdkRoot)
{
    std::vector<std::string> out;
    if (DIR* dir = opendir((sdkRoot + "/features").c_str())) {
        while (dirent* e = readdir(dir)) {
            const std::string name = e->d_name;
            if (name != "." && name != ".." && isDir(sdkRoot + "/features/" + name + "/lib")) out.push_back(name);
        }
        closedir(dir);
    }
    return out;
}

std::string nvLibraryPath(const std::string& sdkRoot)
{
    std::string path = sdkRoot + "/lib";
    for (const std::string& f : nvFeatures(sdkRoot)) path += ":" + sdkRoot + "/features/" + f + "/lib";
    for (const char* sub : {"/external/cuda/lib", "/external/tensorrt/lib"})
        if (isDir(sdkRoot + sub)) path += ":" + sdkRoot + sub;
    return path;
}

bool nvInit(const std::string& sdkRoot, std::string* error) { return nvProxyLoad(sdkRoot, error); }

std::string nvVersion()
{
    unsigned v = 0;
    if (NvVFX_GetVersion(&v) != NVCV_SUCCESS) return std::string();
    return std::to_string(v >> 24) + "." + std::to_string((v >> 16) & 255) + "." + std::to_string((v >> 8) & 255);
}

// (frame generation's state first: super resolution can take its pictures straight from it)
struct NvFrameGen::Impl {
    NvVFX_Handle eff = nullptr;
    NvCVImage a, b, dst, tmp;
    NvCVImage* prev = &a;   // the frame before
    NvCVImage* cur = &b;    // the newest frame
    int width = 0, height = 0, pushed = 0;
    bool pinned = false;
};

// ---------------------------------------------------------------- Video Super Resolution
struct NvSuperRes::Impl {
    NvVFX_Handle eff = nullptr;
    NvCVImage src, dst, tmp;   // on the card (or page-locked), and the SDK's scratch picture
    int srcW = 0, srcH = 0, dstW = 0, dstH = 0;
    bool pinned = false;
};

NvSuperRes::NvSuperRes() : d(nullptr) {}
NvSuperRes::~NvSuperRes() { close(); }
bool NvSuperRes::isOpen() const { return d != nullptr; }

void NvSuperRes::close()
{
    if (!d) return;
    if (d->eff) NvVFX_DestroyEffect(d->eff);
    delete d;
    d = nullptr;
}

bool NvSuperRes::open(int srcW, int srcH, int dstW, int dstH, int quality, bool pinned, std::string* error)
{
    close();
    if (!nvProxyLoaded()) { if (error) *error = "the NVIDIA Video Effects SDK is not loaded"; return false; }
    d = new Impl;
    d->srcW = srcW; d->srcH = srcH; d->dstW = dstW; d->dstH = dstH; d->pinned = pinned;
    const unsigned mem = pinned ? NVCV_CPU_PINNED : NVCV_GPU;
    bool ok = !failed(NvVFX_CreateEffect(FX_SUPER_RES, &d->eff), "creating Video Super Resolution", error)
        && !failed(NvCVImage_Alloc(&d->src, unsigned(srcW), unsigned(srcH), NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED, mem, 32), "allocating the input picture", error)
        && !failed(NvCVImage_Alloc(&d->dst, unsigned(dstW), unsigned(dstH), NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED, mem, 32), "allocating the output picture", error)
        && !failedOptional(NvVFX_SetU32(d->eff, FX_SR_ENCODING, 0u), "choosing 8-bit pictures", error)
        && !failed(NvVFX_SetImage(d->eff, NVVFX_INPUT_IMAGE, &d->src), "setting the input picture", error)
        && !failed(NvVFX_SetImage(d->eff, NVVFX_OUTPUT_IMAGE, &d->dst), "setting the output picture", error)
        && !failed(NvVFX_SetCudaStream(d->eff, NVVFX_CUDA_STREAM, nullptr), "setting the CUDA stream", error)
        && !failedOptional(NvVFX_SetU32(d->eff, FX_SR_QUALITY, unsigned(quality)), "setting the quality level", error)
        && !failed(NvVFX_Load(d->eff), "loading Video Super Resolution", error);
    if (!ok) close();
    return ok;
}

bool NvSuperRes::run(const uint8_t* srcRgba, int srcPitch, uint8_t* dstRgba, int dstPitch, NvTiming* timing, std::string* error)
{
    if (!d) { if (error) *error = "Video Super Resolution is not open"; return false; }
    Clock::time_point t0 = Clock::now();
    if (d->pinned) copyRows(srcRgba, srcPitch, static_cast<uint8_t*>(d->src.pixels), d->src.pitch, d->srcW, d->srcH);
    else {
        NvCVImage cpu;
        wrap(cpu, srcRgba, d->srcW, d->srcH, srcPitch);
        if (failed(NvCVImage_Transfer(&cpu, &d->src, 1.f, nullptr, &d->tmp), "sending the picture to the graphics card", error)) return false;
    }
    if (timing) timing->uploadMs = msSince(t0);
    t0 = Clock::now();
    if (failed(NvVFX_Run(d->eff, 0), "running Video Super Resolution", error)) return false;
    if (timing) timing->runMs = msSince(t0);
    t0 = Clock::now();
    if (d->pinned) copyRows(static_cast<const uint8_t*>(d->dst.pixels), d->dst.pitch, dstRgba, dstPitch, d->dstW, d->dstH);
    else {
        NvCVImage cpu;
        wrap(cpu, dstRgba, d->dstW, d->dstH, dstPitch);
        if (failed(NvCVImage_Transfer(&d->dst, &cpu, 1.f, nullptr, &d->tmp), "fetching the picture from the graphics card", error)) return false;
    }
    if (timing) timing->downloadMs = msSince(t0);
    return true;
}

int NvSuperRes::srcWidth() const { return d ? d->srcW : 0; }
int NvSuperRes::srcHeight() const { return d ? d->srcH : 0; }
int NvSuperRes::dstWidth() const { return d ? d->dstW : 0; }
int NvSuperRes::dstHeight() const { return d ? d->dstH : 0; }

bool NvSuperRes::upload(const uint8_t* srcRgba, int srcPitch, std::string* error)
{
    if (!d) { if (error) *error = "Video Super Resolution is not open"; return false; }
    if (d->pinned) { copyRows(srcRgba, srcPitch, static_cast<uint8_t*>(d->src.pixels), d->src.pitch, d->srcW, d->srcH); return true; }
    NvCVImage cpu;
    wrap(cpu, srcRgba, d->srcW, d->srcH, srcPitch);
    return !failed(NvCVImage_Transfer(&cpu, &d->src, 1.f, nullptr, &d->tmp), "sending the picture to the graphics card", error);
}

bool NvSuperRes::runOnly(std::string* error)
{
    if (!d) { if (error) *error = "Video Super Resolution is not open"; return false; }
    return !failed(NvVFX_Run(d->eff, 0), "running Video Super Resolution", error);
}

bool NvSuperRes::download(uint8_t* dstRgba, int dstPitch, std::string* error)
{
    if (!d) { if (error) *error = "Video Super Resolution is not open"; return false; }
    if (d->pinned) { copyRows(static_cast<const uint8_t*>(d->dst.pixels), d->dst.pitch, dstRgba, dstPitch, d->dstW, d->dstH); return true; }
    NvCVImage cpu;
    wrap(cpu, dstRgba, d->dstW, d->dstH, dstPitch);
    return !failed(NvCVImage_Transfer(&d->dst, &cpu, 1.f, nullptr, &d->tmp), "fetching the picture from the graphics card", error);
}

// ---------------------------------------------------------------- Video Frame Generation
int NvFrameGen::width() const { return d ? d->width : 0; }
int NvFrameGen::height() const { return d ? d->height : 0; }
bool NvFrameGen::pinned() const { return d && d->pinned; }

void NvFrameGen::forget() { if (d) d->pushed = 0; }

// (shared by push and pushFrom: the new frame is in `cur`)
static bool pairSet(NvVFX_Handle eff, NvCVImage* prev, NvCVImage* cur, bool shotChange, std::string* error)
{
    // Both inputs are set again for every new pair, even though only their roles have changed (NVIDIA: required).
    return !failed(NvVFX_SetImage(eff, NVVFX_INPUT_IMAGE_0, prev), "setting the frame before", error)
        && !failed(NvVFX_SetImage(eff, NVVFX_INPUT_IMAGE_1, cur), "setting the newest frame", error)
        && !failed(NvVFX_SetU32(eff, FX_FG_SHOT_CHANGE, shotChange ? 1u : 0u), "marking a shot change", error);
}

bool NvFrameGen::pushFrom(NvSuperRes& from, bool shotChange, std::string* error)
{
    if (!d) { if (error) *error = "Video Frame Generation is not open"; return false; }
    if (!from.d || from.d->dstW != d->width || from.d->dstH != d->height) { if (error) *error = "super resolution has no picture of that size"; return false; }
    // (the roles change only once the picture has arrived: a failure leaves the pair as it was)
    if (failed(NvCVImage_Transfer(&from.d->dst, d->prev, 1.f, nullptr, &d->tmp), "handing the picture over on the graphics card", error)) return false;
    std::swap(d->prev, d->cur);
    ++d->pushed;
    return d->pushed < 2 || pairSet(d->eff, d->prev, d->cur, shotChange, error);
}

bool NvFrameGen::fetch(int which, uint8_t* dstRgba, int dstPitch, std::string* error)
{
    if (!d) { if (error) *error = "Video Frame Generation is not open"; return false; }
    if (d->pushed < (which == 1 ? 2 : 1)) { if (error) *error = "frame generation has no such frame yet"; return false; }
    NvCVImage* from = which == 1 ? d->prev : d->cur;
    if (d->pinned) { copyRows(static_cast<const uint8_t*>(from->pixels), from->pitch, dstRgba, dstPitch, d->width, d->height); return true; }
    NvCVImage cpu;
    wrap(cpu, dstRgba, d->width, d->height, dstPitch);
    return !failed(NvCVImage_Transfer(from, &cpu, 1.f, nullptr, &d->tmp), "fetching the frame from the graphics card", error);
}
NvFrameGen::NvFrameGen() : d(nullptr) {}
NvFrameGen::~NvFrameGen() { close(); }
bool NvFrameGen::isOpen() const { return d != nullptr; }
int NvFrameGen::pushed() const { return d ? d->pushed : 0; }

void NvFrameGen::close()
{
    if (!d) return;
    if (d->eff) NvVFX_DestroyEffect(d->eff);
    delete d;
    d = nullptr;
}

bool NvFrameGen::open(int width, int height, int mode, bool pinned, std::string* error)
{
    close();
    if (!nvProxyLoaded()) { if (error) *error = "the NVIDIA Video Effects SDK is not loaded"; return false; }
    d = new Impl;
    d->width = width; d->height = height; d->pinned = pinned;
    const unsigned mem = pinned ? NVCV_CPU_PINNED : NVCV_GPU, w = unsigned(width), h = unsigned(height);
    // (the order of these calls is the one in NVIDIA's sample program)
    bool ok = !failed(NvVFX_CreateEffect(FX_FRAME_GEN, &d->eff), "creating Video Frame Generation", error)
        && !failed(NvCVImage_Alloc(&d->a, w, h, NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED, mem, 32), "allocating a frame", error)
        && !failed(NvCVImage_Alloc(&d->b, w, h, NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED, mem, 32), "allocating a frame", error)
        && !failed(NvCVImage_Alloc(&d->dst, w, h, NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED, mem, 32), "allocating the generated frame", error)
        && !failed(NvVFX_SetCudaStream(d->eff, NVVFX_CUDA_STREAM, nullptr), "setting the CUDA stream", error)
        && !failed(NvVFX_SetU32(d->eff, NVVFX_INPUT_WIDTH, w), "setting the width", error)
        && !failed(NvVFX_SetU32(d->eff, NVVFX_INPUT_HEIGHT, h), "setting the height", error)
        && !failed(NvVFX_SetU32(d->eff, FX_FG_MODE, unsigned(mode)), "setting the mode", error)
        && !failed(NvVFX_SetU32(d->eff, FX_FG_AUTO_SHOT, 1u), "turning on shot-change detection", error)
        && !failed(NvVFX_Load(d->eff), "loading Video Frame Generation", error)
        && !failed(NvVFX_SetImage(d->eff, NVVFX_OUTPUT_IMAGE, &d->dst), "setting the output picture", error)
        && !failed(NvVFX_SetU32(d->eff, FX_FG_MULTIPLIER, 0u), "choosing explicit time positions", error);
    if (!ok) close();
    return ok;
}

bool NvFrameGen::push(const uint8_t* rgba, int pitch, bool shotChange, NvTiming* timing, std::string* error)
{
    if (!d) { if (error) *error = "Video Frame Generation is not open"; return false; }
    const Clock::time_point t0 = Clock::now();
    std::swap(d->prev, d->cur);   // the newest becomes the frame before; its old buffer takes the new frame
    if (d->pinned) copyRows(rgba, pitch, static_cast<uint8_t*>(d->cur->pixels), d->cur->pitch, d->width, d->height);
    else {
        NvCVImage cpu;
        wrap(cpu, rgba, d->width, d->height, pitch);
        if (failed(NvCVImage_Transfer(&cpu, d->cur, 1.f, nullptr, &d->tmp), "sending the frame to the graphics card", error)) return false;
    }
    ++d->pushed;
    if (d->pushed >= 2 && !pairSet(d->eff, d->prev, d->cur, shotChange, error)) return false;
    if (timing) { timing->uploadMs = msSince(t0); timing->runMs = 0; timing->downloadMs = 0; }
    return true;
}

bool NvFrameGen::generate(float t, uint8_t* dstRgba, int dstPitch, NvTiming* timing, std::string* error)
{
    if (!d) { if (error) *error = "Video Frame Generation is not open"; return false; }
    if (d->pushed < 2) { if (error) *error = "two frames are needed before one can be generated between them"; return false; }
    Clock::time_point t0 = Clock::now();
    if (failed(NvVFX_SetF32(d->eff, FX_FG_TIMESTEP, t), "setting the time position", error)) return false;
    if (failed(NvVFX_Run(d->eff, 0), "running Video Frame Generation", error)) return false;
    if (timing) { timing->uploadMs = 0; timing->runMs = msSince(t0); }
    t0 = Clock::now();
    if (d->pinned) copyRows(static_cast<const uint8_t*>(d->dst.pixels), d->dst.pitch, dstRgba, dstPitch, d->width, d->height);
    else {
        NvCVImage cpu;
        wrap(cpu, dstRgba, d->width, d->height, dstPitch);
        if (failed(NvCVImage_Transfer(&d->dst, &cpu, 1.f, nullptr, &d->tmp), "fetching the generated frame", error)) return false;
    }
    if (timing) timing->downloadMs = msSince(t0);
    return true;
}

// ---------------------------------------------------------------- the two together
NvPipeline::NvPipeline() {}
NvPipeline::~NvPipeline() { close(); }

void NvPipeline::close()
{
    m_sr.close();
    m_fg.close();
    m_cpu.clear();
    m_cpu.shrink_to_fit();
    m_open = false;
    m_frames = 0;
}

bool NvPipeline::open(const NvPipelineConfig& c, std::string* error)
{
    close();
    auto sane = [](int v) { return v >= 16 && v <= 8192; };
    if (!sane(c.srcW) || !sane(c.srcH) || !sane(c.outW) || !sane(c.outH)) { if (error) *error = "a picture size out of range"; return false; }
    if (!c.superRes() && !c.frameGen()) { if (error) *error = "nothing to do: neither super resolution nor frame generation"; return false; }
    if (!c.superRes() && (c.outW != c.srcW || c.outH != c.srcH)) { if (error) *error = "another size needs super resolution"; return false; }
    m_cfg = c;
    m_onCard = true;
    if (c.superRes() && !m_sr.open(c.srcW, c.srcH, c.outW, c.outH, c.srQuality, false, error)) { close(); return false; }
    if (c.frameGen() && !m_fg.open(c.outW, c.outH, c.fgMode, false, error)) { close(); return false; }
    m_open = true;
    std::string why;
    if (!trial(&why) && c.superRes() && c.frameGen() && m_onCard) {
        // Perhaps only the hand-over on the card: once more with the upscaled frame going through ordinary memory.
        m_onCard = false;
        m_fg.close();
        if (!m_fg.open(c.outW, c.outH, c.fgMode, false, error)) { close(); return false; }
        why.clear();
        trial(&why);
    }
    if (!why.empty()) { if (error) *error = why; close(); return false; }
    m_fg.forget();
    m_frames = 0;
    m_lastCut = false;
    return true;
}

bool NvPipeline::trial(std::string* error)
{
    std::vector<uint8_t> in(size_t(m_cfg.srcW) * m_cfg.srcH * 4, 128), out(size_t(m_cfg.outW) * m_cfg.outH * 4);
    char kind = 0;
    m_fg.forget();
    m_frames = 0;
    if (!frame(in.data(), true, error) || !frame(in.data(), false, error)) return false;
    if (!get(1.f, out.data(), &kind, nullptr, error)) return false;
    if (m_cfg.frameGen() && (!get(0.5f, out.data(), &kind, nullptr, error) || !get(0.f, out.data(), &kind, nullptr, error))) return false;
    return true;
}

bool NvPipeline::frame(const uint8_t* src, bool cut, std::string* error)
{
    if (!m_open) { if (error) *error = "nothing is open"; return false; }
    const int srcPitch = m_cfg.srcW * 4, outPitch = m_cfg.outW * 4;
    if (m_cfg.superRes()) {
        if (!m_sr.upload(src, srcPitch, error) || !m_sr.runOnly(error)) return false;
        if (m_cfg.frameGen()) {
            if (m_onCard) { if (!m_fg.pushFrom(m_sr, cut, error)) return false; }
            else {
                m_cpu.resize(size_t(outPitch) * m_cfg.outH);
                if (!m_sr.download(m_cpu.data(), outPitch, error) || !m_fg.push(m_cpu.data(), outPitch, cut, nullptr, error)) return false;
            }
        }
    } else if (!m_fg.push(src, srcPitch, cut, nullptr, error)) return false;
    ++m_frames;
    m_lastCut = cut;
    return true;
}

bool NvPipeline::get(float t, uint8_t* dst, char* kind, NvTiming* timing, std::string* error)
{
    if (!m_open) { if (error) *error = "nothing is open"; return false; }
    if (m_frames < 1) { if (error) *error = "no frame has arrived yet"; return false; }
    const Clock::time_point t0 = Clock::now();
    const int pitch = m_cfg.outW * 4;
    char k = 'c';
    if (m_cfg.frameGen() && m_frames >= 2) {
        if (t <= 0.f) k = 'p';
        else if (t < 1.f) k = m_lastCut ? (t < 0.5f ? 'p' : 'c') : 'g';   // (nothing is generated across a cut)
    }
    if (kind) *kind = k;
    if (k == 'g') return m_fg.generate(t, dst, pitch, timing, error);
    const bool ok = m_cfg.frameGen() ? m_fg.fetch(k == 'p' ? 1 : 2, dst, pitch, error) : m_sr.download(dst, pitch, error);
    if (timing) { timing->uploadMs = 0; timing->runMs = 0; timing->downloadMs = msSince(t0); }
    return ok;
}
