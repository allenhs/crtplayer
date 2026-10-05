// The NVIDIA Video Effects SDK is not linked: its two libraries are opened at run time
// from wherever the user installed the SDK, and the API functions the helper uses are
// defined here as calls through to them. (NVIDIA's own samples do the same with their
// "proxy" sources.) Without the SDK every call returns NVCV_ERR_LIBRARY.
#include "NvProxy.h"

#include "nvCVImage.h"
#include "nvVideoEffects.h"

#include <cstring>
#include <dlfcn.h>

namespace {
void* gImage = nullptr;   // libNVCVImage.so
void* gVfx = nullptr;     // libVideoFX.so

template <typename F> F find(void* lib, const char* name) { return lib ? reinterpret_cast<F>(dlsym(lib, name)) : nullptr; }
} // namespace

bool nvProxyLoad(const std::string& sdkRoot, std::string* error)
{
    if (gVfx) return true;
    // The image library first: the effects library needs it.
    gImage = dlopen((sdkRoot + "/lib/libNVCVImage.so").c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!gImage) { if (error) *error = dlerror(); return false; }
    gVfx = dlopen((sdkRoot + "/lib/libVideoFX.so").c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!gVfx) { if (error) *error = dlerror(); return false; }
    return true;
}

bool nvProxyLoaded() { return gVfx != nullptr; }

#define IMG(name) static const auto fn = find<decltype(&name)>(gImage, #name)
#define VFX(name) static const auto fn = find<decltype(&name)>(gVfx, #name)

extern "C" {

const char* NvCV_GetErrorStringFromCode(NvCV_Status code)
{
    static auto fn = find<decltype(&NvCV_GetErrorStringFromCode)>(gVfx, "NvCV_GetErrorStringFromCode");
    if (!fn) fn = find<decltype(&NvCV_GetErrorStringFromCode)>(gImage, "NvCV_GetErrorStringFromCode");
    return fn ? fn(code) : (code == NVCV_ERR_LIBRARY ? "the NVIDIA Video Effects SDK is not loaded" : "error");
}

NvCV_Status NvCVImage_Init(NvCVImage* im, unsigned width, unsigned height, int pitch, void* pixels, NvCVImage_PixelFormat format,
                           NvCVImage_ComponentType type, unsigned layout, unsigned memSpace)
{
    IMG(NvCVImage_Init);
    return fn ? fn(im, width, height, pitch, pixels, format, type, layout, memSpace) : NVCV_ERR_LIBRARY;
}

void NvCVImage_InitView(NvCVImage* subImg, NvCVImage* fullImg, int x, int y, unsigned width, unsigned height)
{
    IMG(NvCVImage_InitView);
    if (fn) fn(subImg, fullImg, x, y, width, height);
}

NvCV_Status NvCVImage_Alloc(NvCVImage* im, unsigned width, unsigned height, NvCVImage_PixelFormat format, NvCVImage_ComponentType type,
                            unsigned layout, unsigned memSpace, unsigned alignment)
{
    IMG(NvCVImage_Alloc);
    if (!fn) { std::memset(static_cast<void*>(im), 0, sizeof(*im)); return NVCV_ERR_LIBRARY; }   // (an empty image, safe to destroy)
    return fn(im, width, height, format, type, layout, memSpace, alignment);
}

NvCV_Status NvCVImage_Realloc(NvCVImage* im, unsigned width, unsigned height, NvCVImage_PixelFormat format, NvCVImage_ComponentType type,
                              unsigned layout, unsigned memSpace, unsigned alignment)
{
    IMG(NvCVImage_Realloc);
    return fn ? fn(im, width, height, format, type, layout, memSpace, alignment) : NVCV_ERR_LIBRARY;
}

void NvCVImage_Dealloc(NvCVImage* im)
{
    IMG(NvCVImage_Dealloc);
    if (fn) fn(im);
}

NvCV_Status NvCVImage_Transfer(const NvCVImage* src, NvCVImage* dst, float scale, struct CUstream_st* stream, NvCVImage* tmp)
{
    IMG(NvCVImage_Transfer);
    return fn ? fn(src, dst, scale, stream, tmp) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvCVImage_TransferRect(const NvCVImage* src, const NvCVRect2i* srcRect, NvCVImage* dst, const NvCVPoint2i* dstPt, float scale,
                                   struct CUstream_st* stream, NvCVImage* tmp)
{
    IMG(NvCVImage_TransferRect);
    return fn ? fn(src, srcRect, dst, dstPt, scale, stream, tmp) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_GetVersion(unsigned int* version)
{
    VFX(NvVFX_GetVersion);
    return fn ? fn(version) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_CreateEffect(NvVFX_EffectSelector code, NvVFX_Handle* effect)
{
    VFX(NvVFX_CreateEffect);
    return fn ? fn(code, effect) : NVCV_ERR_LIBRARY;
}

void NvVFX_DestroyEffect(NvVFX_Handle effect)
{
    VFX(NvVFX_DestroyEffect);
    if (fn) fn(effect);
}

NvCV_Status NvVFX_SetU32(NvVFX_Handle effect, NvVFX_ParameterSelector paramName, unsigned int val)
{
    VFX(NvVFX_SetU32);
    return fn ? fn(effect, paramName, val) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_SetF32(NvVFX_Handle effect, NvVFX_ParameterSelector paramName, float val)
{
    VFX(NvVFX_SetF32);
    return fn ? fn(effect, paramName, val) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_SetString(NvVFX_Handle effect, NvVFX_ParameterSelector paramName, const char* str)
{
    VFX(NvVFX_SetString);
    return fn ? fn(effect, paramName, str) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_SetImage(NvVFX_Handle effect, NvVFX_ParameterSelector paramName, NvCVImage* im)
{
    VFX(NvVFX_SetImage);
    return fn ? fn(effect, paramName, im) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_SetCudaStream(NvVFX_Handle effect, NvVFX_ParameterSelector paramName, CUstream stream)
{
    VFX(NvVFX_SetCudaStream);
    return fn ? fn(effect, paramName, stream) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_Load(NvVFX_Handle effect)
{
    VFX(NvVFX_Load);
    return fn ? fn(effect) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_Run(NvVFX_Handle effect, int async)
{
    VFX(NvVFX_Run);
    return fn ? fn(effect, async) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_CudaStreamCreate(CUstream* stream)
{
    VFX(NvVFX_CudaStreamCreate);
    return fn ? fn(stream) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_CudaStreamDestroy(CUstream stream)
{
    VFX(NvVFX_CudaStreamDestroy);
    return fn ? fn(stream) : NVCV_ERR_LIBRARY;
}

NvCV_Status NvVFX_CudaStreamSynchronize(CUstream stream)
{
    VFX(NvVFX_CudaStreamSynchronize);
    return fn ? fn(stream) : NVCV_ERR_LIBRARY;
}

} // extern "C"
