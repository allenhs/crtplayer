// A stand-in for NVIDIA's libNVCVImage.so, for tests on machines without the SDK or a
// graphics card: "card" memory is ordinary memory. Only what the helper uses is here.
#include "nvCVImage.h"
#include <cstdlib>
#include <cstring>

extern "C" {
__attribute__((visibility("default"))) const char* NvCV_GetErrorStringFromCode(NvCV_Status code)
{
    switch (code) {
    case NVCV_SUCCESS: return "success";
    case NVCV_ERR_SELECTOR: return "no such setting (mock)";
    case NVCV_ERR_PARAMETER: return "bad value (mock)";
    case NVCV_ERR_MEMORY: return "out of memory (mock)";
    case NVCV_ERR_PIXELFORMAT: return "pixel format not handled (mock)";
    default: return "error (mock)";
    }
}

__attribute__((visibility("default"))) NvCV_Status NvCVImage_Init(NvCVImage* im, unsigned width, unsigned height, int pitch, void* pixels,
        NvCVImage_PixelFormat format, NvCVImage_ComponentType type, unsigned layout, unsigned memSpace)
{
    if (!im) return NVCV_ERR_PARAMETER;
    std::memset(static_cast<void*>(im), 0, sizeof(*im));
    im->width = width; im->height = height; im->pitch = pitch; im->pixels = pixels;
    im->pixelFormat = format; im->componentType = type;
    im->numComponents = format == NVCV_RGBA || format == NVCV_BGRA ? 4 : format == NVCV_RGB || format == NVCV_BGR ? 3 : 1;
    im->componentBytes = 1; im->pixelBytes = im->numComponents;
    im->planar = static_cast<unsigned char>(layout); im->gpuMem = static_cast<unsigned char>(memSpace);
    return NVCV_SUCCESS;
}

__attribute__((visibility("default"))) void NvCVImage_Dealloc(NvCVImage* im)
{
    if (!im) return;
    if (im->deletePtr) std::free(im->deletePtr);
    im->deletePtr = nullptr; im->pixels = nullptr; im->bufferBytes = 0;
}

__attribute__((visibility("default"))) NvCV_Status NvCVImage_Alloc(NvCVImage* im, unsigned width, unsigned height, NvCVImage_PixelFormat format,
        NvCVImage_ComponentType type, unsigned layout, unsigned memSpace, unsigned alignment)
{
    if (!im) return NVCV_ERR_PARAMETER;
    if (width == 0 || height == 0) { std::memset(static_cast<void*>(im), 0, sizeof(*im)); return NVCV_SUCCESS; }
    if ((format != NVCV_RGBA && format != NVCV_BGRA) || type != NVCV_U8) return NVCV_ERR_PIXELFORMAT;
    const unsigned al = alignment ? alignment : 4;
    const int pitch = int((width * 4 + al - 1) / al * al);
    void* mem = std::calloc(size_t(pitch) * height, 1);
    if (!mem) return NVCV_ERR_MEMORY;
    NvCVImage_Init(im, width, height, pitch, mem, format, type, layout, memSpace);
    im->deletePtr = mem;
    im->bufferBytes = size_t(pitch) * height;
    return NVCV_SUCCESS;
}

__attribute__((visibility("default"))) NvCV_Status NvCVImage_Realloc(NvCVImage* im, unsigned width, unsigned height, NvCVImage_PixelFormat format,
        NvCVImage_ComponentType type, unsigned layout, unsigned memSpace, unsigned alignment)
{
    NvCVImage_Dealloc(im);
    return NvCVImage_Alloc(im, width, height, format, type, layout, memSpace, alignment);
}

__attribute__((visibility("default"))) void NvCVImage_InitView(NvCVImage* sub, NvCVImage* full, int x, int y, unsigned width, unsigned height)
{
    *sub = *full;
    sub->deletePtr = nullptr;
    sub->pixels = static_cast<unsigned char*>(full->pixels) + size_t(y) * full->pitch + size_t(x) * full->pixelBytes;
    sub->width = width; sub->height = height;
}

__attribute__((visibility("default"))) NvCV_Status NvCVImage_Transfer(const NvCVImage* src, NvCVImage* dst, float, struct CUstream_st*, NvCVImage*)
{
    if (!src || !dst || !src->pixels || !dst->pixels) return NVCV_ERR_PARAMETER;
    if (src->width != dst->width || src->height != dst->height) return NVCV_ERR_PARAMETER;
    if (src->pixelBytes != 4 || dst->pixelBytes != 4) return NVCV_ERR_PIXELFORMAT;
    // NVFX_MOCK_FAIL_CARD: pictures cannot be handed from one effect to the other on the "card".
    if (src->gpuMem == NVCV_GPU && dst->gpuMem == NVCV_GPU && std::getenv("NVFX_MOCK_FAIL_CARD")) return NVCV_ERR_CUDA;
    const bool swap = src->pixelFormat != dst->pixelFormat;   // RGBA <-> BGRA
    for (unsigned y = 0; y < src->height; ++y) {
        const unsigned char* s = static_cast<const unsigned char*>(src->pixels) + size_t(y) * src->pitch;
        unsigned char* d = static_cast<unsigned char*>(dst->pixels) + size_t(y) * dst->pitch;
        if (!swap) std::memcpy(d, s, size_t(src->width) * 4);
        else for (unsigned x = 0; x < src->width; ++x) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = s[x * 4 + 3]; }
    }
    return NVCV_SUCCESS;
}
}
