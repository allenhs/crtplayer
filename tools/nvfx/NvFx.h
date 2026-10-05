#pragma once
// NVIDIA's Video Super Resolution and Video Frame Generation (Video Effects SDK 1.3), as
// two small classes working on plain RGBA pictures in memory. Used by the helper program
// that the player starts when the SDK is installed; nothing here needs Qt.
#include <cstdint>
#include <string>
#include <vector>

struct NvTiming {
    double uploadMs = 0, runMs = 0, downloadMs = 0;
    double total() const { return uploadMs + runMs + downloadMs; }
};

// Where an installed SDK is: `hint` if given, else $CRTPLAYER_NVFX_SDK, then the usual places.
// Empty when none is found.
std::string nvFindSdk(const std::string& hint);
// The folders the SDK's libraries are loaded from, joined with ':' (for LD_LIBRARY_PATH).
std::string nvLibraryPath(const std::string& sdkRoot);
// The features installed in the SDK ("nvvfxvideosuperres", ...).
std::vector<std::string> nvFeatures(const std::string& sdkRoot);
// Loads the SDK. LD_LIBRARY_PATH must already hold nvLibraryPath() (it is read when a process starts).
bool nvInit(const std::string& sdkRoot, std::string* error);
std::string nvVersion();

// Quality levels of Video Super Resolution (NVIDIA's numbering).
enum NvSuperResQuality { NvSrLow = 1, NvSrMedium = 2, NvSrHigh = 3, NvSrUltra = 4, NvSrStreamingMedium = 21, NvSrStreamingUltra = 23 };

class NvFrameGen;

class NvSuperRes {
public:
    NvSuperRes();
    ~NvSuperRes();
    NvSuperRes(const NvSuperRes&) = delete;
    NvSuperRes& operator=(const NvSuperRes&) = delete;
    // pinned: the pictures are handed over in page-locked memory instead of being copied to the card by the caller.
    bool open(int srcW, int srcH, int dstW, int dstH, int quality, bool pinned, std::string* error);
    bool run(const uint8_t* srcRgba, int srcPitch, uint8_t* dstRgba, int dstPitch, NvTiming* timing, std::string* error);
    // The three steps of run(), one at a time: the picture to the card, the effect, and the result back.
    bool upload(const uint8_t* srcRgba, int srcPitch, std::string* error);
    bool runOnly(std::string* error);
    bool download(uint8_t* dstRgba, int dstPitch, std::string* error);
    void close();
    bool isOpen() const;
    int srcWidth() const;
    int srcHeight() const;
    int dstWidth() const;
    int dstHeight() const;
private:
    friend class NvFrameGen;
    struct Impl;
    Impl* d;
};

class NvFrameGen {
public:
    NvFrameGen();
    ~NvFrameGen();
    NvFrameGen(const NvFrameGen&) = delete;
    NvFrameGen& operator=(const NvFrameGen&) = delete;
    // mode: 0 low, 1 medium, 2 high (NVIDIA's three models, by cost).
    bool open(int width, int height, int mode, bool pinned, std::string* error);
    // The video's next frame. The frame pushed before it becomes "the frame before".
    // shotChange: the two are not consecutive pictures of one scene (a cut, a jump).
    bool push(const uint8_t* rgba, int pitch, bool shotChange, NvTiming* timing, std::string* error);
    // A picture between the last two frames pushed: t in (0, 1), 0 = the frame before, 1 = the newest.
    bool generate(float t, uint8_t* dstRgba, int dstPitch, NvTiming* timing, std::string* error);
    // The next frame, taken from super resolution's result on the card (nothing passes through ordinary memory).
    bool pushFrom(NvSuperRes& from, bool shotChange, std::string* error);
    // One of the two frames held, as it is: which = 1 the frame before, 2 the newest.
    bool fetch(int which, uint8_t* dstRgba, int dstPitch, std::string* error);
    void forget();   // no frames pushed yet (the next two start a new pair)
    int pushed() const;
    int width() const;
    int height() const;
    bool pinned() const;
    void close();
    bool isOpen() const;
private:
    struct Impl;
    Impl* d;
};

// The helper's work for the player: the video's frames go in, pictures come out. With
// both effects the frame is upscaled first and frames are generated at the larger size
// (super resolution then sees only real, consecutive frames, and a generated picture
// costs one step instead of two).
struct NvPipelineConfig {
    int srcW = 0, srcH = 0;    // the video's frames
    int outW = 0, outH = 0;    // the pictures wanted (the same size without super resolution)
    int srQuality = 0;         // 0: no super resolution
    int fgMode = -1;           // -1: no frame generation
    bool superRes() const { return srQuality > 0; }
    bool frameGen() const { return fgMode >= 0; }
};

class NvPipeline {
public:
    NvPipeline();
    ~NvPipeline();
    NvPipeline(const NvPipeline&) = delete;
    NvPipeline& operator=(const NvPipeline&) = delete;
    // Opens the effects and runs them once on blank pictures, so that a fault shows here and not on the first frame.
    bool open(const NvPipelineConfig& config, std::string* error);
    void close();
    bool isOpen() const { return m_open; }
    const NvPipelineConfig& config() const { return m_cfg; }
    bool onCard() const { return m_onCard; }   // with both effects: the upscaled frame reaches frame generation on the card
    // The video's next frame (srcW x srcH, RGBA, rows packed). cut: it does not follow the frame before.
    bool frame(const uint8_t* srcRgba, bool cut, std::string* error);
    // The picture at t (outW x outH, RGBA, rows packed): t <= 0 the frame before, t >= 1 the newest
    // frame, otherwise one generated between them. kind: 'p' the frame before, 'c' the newest, 'g' generated.
    bool get(float t, uint8_t* dstRgba, char* kind, NvTiming* timing, std::string* error);
    int frames() const { return m_frames; }
private:
    bool trial(std::string* error);
    NvPipelineConfig m_cfg;
    NvSuperRes m_sr;
    NvFrameGen m_fg;
    std::vector<uint8_t> m_cpu;   // the upscaled frame, when it cannot stay on the card
    bool m_open = false, m_onCard = true, m_lastCut = false;
    int m_frames = 0;
};
