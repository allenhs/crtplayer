// A stand-in for NVIDIA's libVideoFX.so (see mock_image.cpp). Its two "effects" are plain
// arithmetic: Video Super Resolution scales bilinearly, Video Frame Generation mixes the
// two frames by the time position (and gives the newest frame at a shot change). It
// checks the calls the way the real SDK's documentation asks for them.
//
// For the tests, through the environment:
//   NVFX_MOCK_MARK=1          each effect stamps its result's top left corner: super resolution a red
//                             square and a blue one beside it, frame generation a green one below them
//   NVFX_MOCK_DELAY_MS=n      every run takes n milliseconds longer
//   NVFX_MOCK_FAIL_CREATE=1   no effect can be created
//   NVFX_MOCK_FAIL_LOAD=sr|fg that effect cannot be loaded
//   NVFX_MOCK_FAIL_SIZE=w     super resolution cannot be loaded for a result w pixels wide
//   NVFX_MOCK_CRASH_AFTER=n   the program dies at the n-th run
//   NVFX_MOCK_HANG_AFTER=n    the n-th run never returns
//   NVFX_MOCK_FAIL_CARD=1     (mock_image.cpp) no hand-over between effects on the card
#include "nvCVImage.h"
#include "nvVideoEffects.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

struct NvVFX_Object {
    std::string kind;
    NvCVImage* in0 = nullptr;
    NvCVImage* in1 = nullptr;
    NvCVImage* out = nullptr;
    unsigned quality = 0, mode = 1, multiplier = 0, index = 0, shot = 0, width = 0, height = 0;
    float timestep = 0.5f;
    bool loaded = false, pairSet = false;
};

#define API __attribute__((visibility("default")))
extern "C" {
API NvCV_Status NvVFX_GetVersion(unsigned int* version) { if (!version) return NVCV_ERR_PARAMETER; *version = (1u << 24) | (3u << 16); return NVCV_SUCCESS; }

API NvCV_Status NvVFX_CreateEffect(NvVFX_EffectSelector code, NvVFX_Handle* effect)
{
    if (!code || !effect) return NVCV_ERR_PARAMETER;
    const std::string k = code;
    if (k != "VideoSuperRes" && k != "VideoFrameGeneration") return NVCV_ERR_EFFECT;
    if (std::getenv("NVFX_MOCK_FAIL_CREATE")) return NVCV_ERR_LIBRARY;
    *effect = new NvVFX_Object;
    (*effect)->kind = k;
    return NVCV_SUCCESS;
}
API void NvVFX_DestroyEffect(NvVFX_Handle effect) { delete effect; }

API NvCV_Status NvVFX_SetU32(NvVFX_Handle e, NvVFX_ParameterSelector name, unsigned int val)
{
    if (!e || !name) return NVCV_ERR_PARAMETER;
    const std::string n = name;
    if (e->kind == "VideoSuperRes") {
        if (n == "QualityLevel") { e->quality = val; return NVCV_SUCCESS; }
        if (n == "ImageEncodingMode") return val == 0 ? NVCV_SUCCESS : NVCV_ERR_PARAMETER;
        return NVCV_ERR_SELECTOR;
    }
    if (n == "InputWidth") { e->width = val; return NVCV_SUCCESS; }
    if (n == "InputHeight") { e->height = val; return NVCV_SUCCESS; }
    if (n == "Mode") { if (val > 2) return NVCV_ERR_PARAMETER; e->mode = val; return NVCV_SUCCESS; }
    if (n == "AutomaticShotChangeDetectionEnabled") return NVCV_SUCCESS;
    if (n == "FrameMultiplier") { if (val == 1 || val > 8) return NVCV_ERR_PARAMETER; e->multiplier = val; return NVCV_SUCCESS; }
    if (n == "FrameIndex") { e->index = val; return NVCV_SUCCESS; }
    if (n == "ShotChange") { e->shot = val; return NVCV_SUCCESS; }
    return NVCV_ERR_SELECTOR;
}
API NvCV_Status NvVFX_SetF32(NvVFX_Handle e, NvVFX_ParameterSelector name, float val)
{
    if (!e || !name) return NVCV_ERR_PARAMETER;
    const std::string n = name;
    if (e->kind == "VideoFrameGeneration" && n == "Timestep") { if (!(val > 0.f && val < 1.f)) return NVCV_ERR_PARAMETER; e->timestep = val; return NVCV_SUCCESS; }
    if (e->kind == "VideoSuperRes" && n == "Strength") return NVCV_SUCCESS;
    return NVCV_ERR_SELECTOR;
}
API NvCV_Status NvVFX_SetString(NvVFX_Handle e, NvVFX_ParameterSelector, const char*) { return e ? NVCV_SUCCESS : NVCV_ERR_PARAMETER; }
API NvCV_Status NvVFX_SetCudaStream(NvVFX_Handle e, NvVFX_ParameterSelector, CUstream) { return e ? NVCV_SUCCESS : NVCV_ERR_PARAMETER; }

API NvCV_Status NvVFX_SetImage(NvVFX_Handle e, NvVFX_ParameterSelector name, NvCVImage* im)
{
    if (!e || !name || !im) return NVCV_ERR_PARAMETER;
    if (im->gpuMem == NVCV_CPU) return NVCV_ERR_PARAMETER;   // the SDK wants card or page-locked memory
    const std::string n = name;
    if (n == "SrcImage0") { e->in0 = im; e->pairSet = true; }
    else if (n == "SrcImage1") e->in1 = im;
    else if (n == "DstImage0") e->out = im;
    else return NVCV_ERR_SELECTOR;
    return NVCV_SUCCESS;
}

API NvCV_Status NvVFX_Load(NvVFX_Handle e)
{
    if (!e) return NVCV_ERR_PARAMETER;
    if (e->kind == "VideoSuperRes" && (!e->in0 || !e->out)) return NVCV_ERR_PARAMETER;
    if (e->kind == "VideoFrameGeneration" && (!e->width || !e->height)) return NVCV_ERR_PARAMETER;
    if (const char* f = std::getenv("NVFX_MOCK_FAIL_LOAD"))
        if ((e->kind == "VideoSuperRes") == (std::string(f) == "sr")) return NVCV_ERR_MODEL;
    if (const char* f = std::getenv("NVFX_MOCK_FAIL_SIZE"))
        if (e->kind == "VideoSuperRes" && e->out->width == unsigned(std::atoi(f))) return NVCV_ERR_RESOLUTION;
    e->loaded = true;
    return NVCV_SUCCESS;
}

static void block(NvCVImage* o, unsigned x0, unsigned y0, unsigned side, unsigned char r, unsigned char g, unsigned char b)
{
    for (unsigned y = y0; y < y0 + side && y < o->height; ++y)
        for (unsigned x = x0; x < x0 + side && x < o->width; ++x) {
            unsigned char* d = static_cast<unsigned char*>(o->pixels) + size_t(y) * o->pitch + size_t(x) * 4;
            const bool rgba = o->pixelFormat == NVCV_RGBA;
            d[0] = rgba ? r : b; d[1] = g; d[2] = rgba ? b : r; d[3] = 255;
        }
}

static inline const unsigned char* at(const NvCVImage* im, int x, int y) { return static_cast<const unsigned char*>(im->pixels) + size_t(y) * im->pitch + size_t(x) * 4; }

API NvCV_Status NvVFX_Run(NvVFX_Handle e, int)
{
    if (!e || !e->loaded || !e->out) return NVCV_ERR_PARAMETER;
    static int runs = 0;
    ++runs;
    if (const char* v = std::getenv("NVFX_MOCK_CRASH_AFTER")) if (runs >= std::atoi(v)) std::abort();
    if (const char* v = std::getenv("NVFX_MOCK_HANG_AFTER")) if (runs >= std::atoi(v)) for (;;) sleep(1000);
    if (const char* v = std::getenv("NVFX_MOCK_DELAY_MS")) usleep(useconds_t(std::atoi(v)) * 1000);
    const bool mark = std::getenv("NVFX_MOCK_MARK") != nullptr;
    NvCVImage* o = e->out;
    const unsigned side = std::max(4u, o->height / 20);
    if (e->kind == "VideoSuperRes") {
        const NvCVImage* s = e->in0;
        // (whole numbers, weights in 256ths: quick enough to play video through)
        std::vector<int> xi(o->width), xw(o->width);
        for (unsigned x = 0; x < o->width; ++x) {
            const double fx = std::clamp((x + 0.5) * s->width / o->width - 0.5, 0.0, s->width - 1.0);
            xi[x] = std::min<int>(int(fx), int(s->width) - 2 < 0 ? 0 : int(s->width) - 2);
            xw[x] = s->width < 2 ? 0 : int((fx - xi[x]) * 256.0 + 0.5);
        }
        const int step = s->width < 2 ? 0 : 4;
        for (unsigned y = 0; y < o->height; ++y) {
            const double fy = std::clamp((y + 0.5) * s->height / o->height - 0.5, 0.0, s->height - 1.0);
            const int y0 = int(fy), y1 = std::min<int>(y0 + 1, s->height - 1);
            const int wy = int((fy - y0) * 256.0 + 0.5);
            const unsigned char* r0 = at(s, 0, y0);
            const unsigned char* r1 = at(s, 0, y1);
            unsigned char* d = static_cast<unsigned char*>(o->pixels) + size_t(y) * o->pitch;
            for (unsigned x = 0; x < o->width; ++x) {
                const unsigned char* a = r0 + xi[x] * 4;
                const unsigned char* b = r1 + xi[x] * 4;
                const int wx = xw[x];
                for (int c = 0; c < 4; ++c) {
                    const int top = a[c] * (256 - wx) + a[c + step] * wx, bottom = b[c] * (256 - wx) + b[c + step] * wx;
                    d[x * 4 + c] = static_cast<unsigned char>((top * (256 - wy) + bottom * wy + 32768) >> 16);
                }
            }
        }
        if (mark) { block(o, 0, 0, side, 255, 0, 0); block(o, side, 0, side, 0, 0, 255); }
        return NVCV_SUCCESS;
    }
    if (!e->in0 || !e->in1) return NVCV_ERR_PARAMETER;
    if (e->in0->width != e->width || e->in0->height != e->height || o->width != e->width || o->height != e->height) return NVCV_ERR_PARAMETER;
    if (!e->pairSet) return NVCV_ERR_PARAMETER;   // the inputs must be set anew for every pair of frames
    const float t = e->shot ? 1.f : e->multiplier ? float(e->index) / e->multiplier : e->timestep;
    for (unsigned y = 0; y < o->height; ++y) {
        unsigned char* d = static_cast<unsigned char*>(o->pixels) + size_t(y) * o->pitch;
        const unsigned char* a = at(e->in0, 0, int(y));
        const unsigned char* b = at(e->in1, 0, int(y));
        const int w = int(t * 256.f + 0.5f);
        for (unsigned i = 0; i < o->width * 4; ++i) d[i] = static_cast<unsigned char>((a[i] * (256 - w) + b[i] * w + 128) >> 8);
    }
    if (mark) block(o, 0, side, side, 0, 255, 0);
    return NVCV_SUCCESS;
}

API NvCV_Status NvVFX_CudaStreamCreate(CUstream* stream) { if (stream) *stream = nullptr; return NVCV_SUCCESS; }
API NvCV_Status NvVFX_CudaStreamDestroy(CUstream) { return NVCV_SUCCESS; }
API NvCV_Status NvVFX_CudaStreamSynchronize(CUstream) { return NVCV_SUCCESS; }
}
