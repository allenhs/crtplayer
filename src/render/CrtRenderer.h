#pragma once
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
    bool loadProgram(QOpenGLShaderProgram& prog, const char* frag, QString* error);

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
    quint64 m_fmvDrawn = 0;
    qint64 m_framePts = -1;          // ns, stream time of the uploaded frame
    quint64 m_uploads = 0;
    GLuint m_blurSrc = 0;            // what the blur was last made from

    GstVideoInfo m_info;
    bool m_infoValid = false;
    int m_format = 0;          // 0 RGBA, 1 BGRA, 2 NV12, 3 I420
    bool m_hasFrame = false;
    Orientation m_orient;
};
