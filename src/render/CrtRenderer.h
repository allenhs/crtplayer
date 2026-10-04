#pragma once
#include <QColor>
#include "render/Geometry.h"
#include "render/ViewSettings.h"
#include "settings/CrtParams.h"

#include <QImage>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QRectF>
#include <QString>
#include <QVector>
#include <gst/gst.h>
#include <gst/video/video.h>

// Owns every GL resource of the video pipeline. Must be used with a current GL 3.3 context.
//
//  decoded planes --convert.frag--> oriented RGB image (mipmapped)
//                                     |--downsample.frag + blur.frag x2--> blur texture
//                                     '--crt.frag (+ blur) --> target framebuffer
class CrtRenderer : protected QOpenGLExtraFunctions {
public:
    struct DrawParams {
        QSize viewport;          // target size in pixels
        QRectF tube;             // simulated glass rect (px, top-left origin)
        QRectF image;            // visible picture rect (px)
        QRectF src;              // uv window of the picture shown in `image`
        CrtParams params;
        float lines = 0;         // scanline count (already resolved from "auto")
        bool bypass = false;
        float split = -1;        // compare divider x (px), < 0 = off
        float time = 0;
        float frameRand = 0;
        float opacity = 1;       // < 1: blend over the existing target instead of clearing it
        float srcLod = 0;        // mip level at which the picture has ~one row per scanline
        QSize pixelSize;         // lowered picture resolution ("bigger pixels"); empty = native
        int transType = 0;       // set moments: 0 none, 1 power on, 2 power off
        float transT = 0;        // progress 0..1
        float staticLevel = 0;   // static between items, 0..1
        float osdAlpha = 0;      // VCR on-screen display
        QRectF osdRect;          // OSD placement in picture uv
        bool history = true;     // phosphor persistence may use/advance the afterglow buffer
        // Enhance (2.13)
        bool enhanceUp = false;  // effects off and the picture larger than the video: upscale it sharply
        float enhanceSharp = 0.5f;
        float framePhase = 1.f;  // frame generation: 1 = this frame; 0..1 = that far from the frame before to this one
    };

    // Builds draw parameters for `area` (target pixels) exactly as the flat view does.
    static DrawParams makeDrawParams(const ViewSettings& vs, const SourceFormat& src, const QSize& viewport,
                                     const QRectF& area, float time, quint32 frameCounter);

    CrtRenderer() = default;
    ~CrtRenderer();
    bool initialize(QString* error);
    void destroy();
    bool isInitialized() const { return m_initialized; }

    // Uploads a decoded frame and converts it to the oriented RGB image.
    bool uploadSample(GstSample* sample);
    void setOrientation(const Orientation& o);
    bool hasFrame() const { return m_hasFrame; }
    SourceFormat sourceFormat() const;
    QString pixelFormatName() const;
    QString colorimetryName() const;

    void draw(GLuint targetFbo, const DrawParams& p);
    // Renders the unfiltered picture (PAR-corrected, rotated) at outSize.
    QImage renderOriginal(const QSize& outSize);
    // Renders DrawParams into an offscreen target and returns it (for filtered screenshots).
    QImage renderToImage(const DrawParams& p);
    // Renders into an internal, mipmapped texture (for the 3D glass) and returns it.
    GLuint renderToTexture(const DrawParams& p);
    // Blurred copy of the picture (long side 480 px); computed on demand.
    GLuint blurTexture();
    // VCR on-screen display text (straight RGBA image, uploaded premultiplied).
    void setOsdImage(const QImage& img);
    void resetPersistence() { m_histValid = false; }
    // FMV console: colours in the palette of the frame shown, and how many codec frames were drawn.
    QImage fmvImage();   // the console's screen as last drawn (its own grid, e.g. 256 x 224)
    int fmvColorsUsed() const { return m_fmvColorsUsed; }
    quint64 fmvFramesDrawn() const { return m_fmvDrawn; }

private:
    void convert();
    void ensureTarget(GLuint& tex, GLuint& fbo, QSize& curSize, const QSize& size, bool mipmaps, bool blackBorder);
    void computeBlur(GLuint src = 0);
    void drawCrt(GLuint targetFbo, const DrawParams& p);
    void drawQuad();
    bool loadProgram(QOpenGLShaderProgram& prog, const char* frag, QString* error, const char* defines = nullptr);

    bool m_initialized = false;
    QOpenGLShaderProgram m_convert, m_down, m_blur, m_crt, m_persist, m_copy, m_fmvCodec, m_fmvPal;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};

    GLuint m_planeTex[3] = {0, 0, 0};
    QSize m_planeSize[3];
    GLenum m_planeFmt[3] = {0, 0, 0};

    GLuint m_imgTex = 0, m_imgFbo = 0;
    QSize m_imgSize;
    GLuint m_blurTex[2] = {0, 0}, m_blurFbo[2] = {0, 0};
    QSize m_blurSize[2];
    bool m_blurDirty = true;
    GLuint m_outTex = 0, m_outFbo = 0;
    QSize m_outSize;
    GLuint m_lowTex = 0, m_lowFbo = 0;   // lowered-resolution copy of the picture
    QSize m_lowSize;
    bool m_lowDirty = true;
    // Phosphor persistence: CRT output -> m_cur, blended with the decaying history.
    GLuint m_curTex = 0, m_curFbo = 0;
    QSize m_curSize;
    GLuint m_histTex[2] = {0, 0}, m_histFbo[2] = {0, 0};
    QSize m_histSize[2];
    int m_histIdx = 0;
    bool m_histValid = false;
    float m_histTime = 0;
    GLuint m_osdTex = 0;
    void updateLowRes(const QSize& size);
    // FMV console (Sega CD): the codec pass (ping-pong, so unchanged blocks can stay), the
    // frame's palette, and the picture shown with it. Updated at the look's frame rate.
    bool updateFmv(const QSize& grid, const CrtParams& p);
    GLuint m_fmvDecTex[2] = {0, 0}, m_fmvDecFbo[2] = {0, 0};
    QSize m_fmvDecSize[2];
    int m_fmvIdx = 0;
    GLuint m_fmvTex = 0, m_fmvFbo = 0, m_fmvPalTex = 0;
    QSize m_fmvSize;
    bool m_fmvValid = false;
    qint64 m_fmvFrame = -1;          // the codec frame shown
    int m_fmvSinceKey = 0;
    QVector<float> m_fmvKey;         // the settings it was made with
    int m_fmvColorsUsed = 0;
    QVector<QRgb> m_fmvPalette;      // the colours the frame shown was drawn with
    quint64 m_fmvDrawn = 0;
    qint64 m_framePts = -1;          // ns, stream time of the uploaded frame
    quint64 m_uploads = 0;
public:
    // Where the time goes, stage by stage (ms, summed; frames counted). With profiling on,
    // every stage is finished (glFinish) before the next starts, so the times are the stages' own.
    struct Profile { double upload = 0, convert = 0, mipmap = 0, blur = 0, draw = 0; int frames = 0, draws = 0; };
    // Frames that arrive as RGB the right way up go straight into the picture texture (no
    // conversion pass). plainOnly: nothing but the effects-off picture will be drawn from
    // them, at about their own size, so the smaller copies (mipmaps) are not made either.
    void setPlainOnly(bool on) { m_plainOnly = on; }
    bool lastUploadDirect() const { return m_direct; }
    QSize frameSize() const { return m_imgSize; }   // of the picture texture (the frame as delivered, after rotation)
    void setProfiling(bool on) { m_profiling = on; }
    bool profiling() const { return m_profiling; }
    Profile profile() const { return m_profile; }
    void resetProfile() { m_profile = Profile(); }
    // ---- Enhance (2.13): sharper upscaling with effects off, and frame generation ----
    // Frame generation keeps the frame before and works out the motion between the two, so
    // that a draw with framePhase < 1 shows the picture at a moment between them.
    void setFrameGeneration(bool on);
    bool frameGeneration() const { return m_fgOn; }
    bool framePairValid() const { return m_fgOn && m_pairValid; }
    qint64 framePairNs() const { return m_pairValid ? m_framePts - m_prevPts : 0; }   // video time between the two frames
    struct EnhanceStats { quint64 upscaled = 0, pairs = 0, between = 0; QSize upSize, flowSize; };
    EnhanceStats enhanceStats() const { return m_enh; }
    // The picture at phase t between the frame before and this one, at the video's own size
    // (mode 1: a plain mix of the two frames, for comparison). Null without a valid pair.
    QImage renderBetweenImage(float t, int mode = 0);
private:
    bool initEnhance(QString* error);
    void destroyEnhance();
    void keepPreviousFrame();
    void frameArrived();
    GLuint upscaledPicture(const DrawParams& d);
    void ensureTargetF(GLuint& tex, GLuint& fbo, QSize& cur, const QSize& size, bool mipmaps);
    void computeFlow();
    bool renderBetween(float t, int mode);
    bool beginBetween(float t);
    void endBetween();
    QOpenGLShaderProgram m_enhUp, m_enhSharp, m_fiLuma, m_fiFlow, m_fiBlend;
    GLuint m_upTex[2] = {0, 0}, m_upFbo[2] = {0, 0};
    QSize m_upSize[2];
    bool m_upDirty = true;
    float m_upSharp = -1.f;
    bool m_fgOn = false, m_prevHas = false, m_pairValid = false, m_flowReady = false;
    GLuint m_prevTex = 0, m_prevFbo = 0;
    QSize m_prevSize;
    qint64 m_prevPts = -1;
    GLuint m_lumTex[2] = {0, 0}, m_lumFbo[2] = {0, 0};   // brightness of this frame [0] and the one before [1]
    QSize m_lumSize[2];
    bool m_lumValid[2] = {false, false};
    static const int kFlowLevels = 5;
    GLuint m_flowTex[2][kFlowLevels] = {}, m_flowFbo[2][kFlowLevels] = {};
    QSize m_flowSize[2][kFlowLevels];
    GLuint m_fgTex = 0, m_fgFbo = 0;
    QSize m_fgSize;
    int m_fgSwap = 0;                 // what a draw has put in the picture's place: 0 nothing, 1 the frame before, 2 a frame between
    bool m_fgSavedMips = false;
    EnhanceStats m_enh;
    bool m_profiling = false;
    bool m_plainOnly = false, m_direct = false, m_mipsStale = false;
    QOpenGLShaderProgram m_plain, m_plainOsd;
    void ensureMipmaps();
    Profile m_profile;
    double stageMs(class QElapsedTimer& t);
    GLuint m_blurSrc = 0;            // what the blur was last made from

    GstVideoInfo m_info;
    bool m_infoValid = false;
    int m_format = 0;          // 0 RGBA, 1 BGRA, 2 NV12, 3 I420
    bool m_hasFrame = false;
    Orientation m_orient;
};
