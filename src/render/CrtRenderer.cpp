#include "CrtRenderer.h"

#include <QDebug>
#include <QFile>
#include <cstring>

#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER 0x812D
#endif
#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR 0x1004
#endif

CrtRenderer::~CrtRenderer() = default;

bool CrtRenderer::loadProgram(QOpenGLShaderProgram& prog, const char* frag, QString* error)
{
    if (!prog.addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/quad.vert")) ||
        !prog.addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, QString::fromLatin1(frag)) ||
        !prog.link()) {
        if (error) *error = QStringLiteral("Shader %1 failed to build:\n%2").arg(QString::fromLatin1(frag), prog.log());
        return false;
    }
    return true;
}

bool CrtRenderer::initialize(QString* error)
{
    initializeOpenGLFunctions();
    if (!loadProgram(m_convert, ":/shaders/convert.frag", error)) return false;
    if (!loadProgram(m_down, ":/shaders/downsample.frag", error)) return false;
    if (!loadProgram(m_blur, ":/shaders/blur.frag", error)) return false;
    if (!loadProgram(m_crt, ":/shaders/crt.frag", error)) return false;
    if (!loadProgram(m_persist, ":/shaders/persist.frag", error)) return false;
    if (!loadProgram(m_copy, ":/shaders/copy.frag", error)) return false;

    static const GLfloat quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(quad, sizeof(quad));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    m_vbo.release();
    m_vao.release();
    m_initialized = true;
    return true;
}

void CrtRenderer::destroy()
{
    if (!m_initialized) return;
    glDeleteTextures(3, m_planeTex);
    std::memset(m_planeTex, 0, sizeof(m_planeTex));
    if (m_imgTex) glDeleteTextures(1, &m_imgTex);
    if (m_outTex) glDeleteTextures(1, &m_outTex);
    if (m_curTex) glDeleteTextures(1, &m_curTex);
    if (m_curFbo) glDeleteFramebuffers(1, &m_curFbo);
    glDeleteTextures(2, m_histTex);
    glDeleteFramebuffers(2, m_histFbo);
    if (m_osdTex) glDeleteTextures(1, &m_osdTex);
    m_curTex = m_curFbo = m_osdTex = 0;
    m_histTex[0] = m_histTex[1] = m_histFbo[0] = m_histFbo[1] = 0;
    if (m_lowTex) glDeleteTextures(1, &m_lowTex);
    if (m_lowFbo) glDeleteFramebuffers(1, &m_lowFbo);
    m_lowTex = m_lowFbo = 0;
    if (m_outFbo) glDeleteFramebuffers(1, &m_outFbo);
    m_outTex = m_outFbo = 0;
    if (m_imgFbo) glDeleteFramebuffers(1, &m_imgFbo);
    glDeleteTextures(2, m_blurTex);
    glDeleteFramebuffers(2, m_blurFbo);
    m_imgTex = m_imgFbo = 0;
    m_blurTex[0] = m_blurTex[1] = m_blurFbo[0] = m_blurFbo[1] = 0;
    m_vbo.destroy();
    m_vao.destroy();
    m_initialized = false;
}

void CrtRenderer::drawQuad()
{
    m_vao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_vao.release();
}

SourceFormat CrtRenderer::sourceFormat() const
{
    SourceFormat f;
    if (!m_infoValid) return f;
    f.width = GST_VIDEO_INFO_WIDTH(&m_info);
    f.height = GST_VIDEO_INFO_HEIGHT(&m_info);
    f.parN = GST_VIDEO_INFO_PAR_N(&m_info) > 0 ? GST_VIDEO_INFO_PAR_N(&m_info) : 1;
    f.parD = GST_VIDEO_INFO_PAR_D(&m_info) > 0 ? GST_VIDEO_INFO_PAR_D(&m_info) : 1;
    f.orient = m_orient;
    return f;
}

QString CrtRenderer::pixelFormatName() const
{
    return m_infoValid ? QString::fromUtf8(gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&m_info))) : QString();
}

QString CrtRenderer::colorimetryName() const
{
    if (!m_infoValid) return {};
    gchar* s = gst_video_colorimetry_to_string(&m_info.colorimetry);
    QString out = s ? QString::fromUtf8(s) : QStringLiteral("unknown");
    g_free(s);
    return out;
}

void CrtRenderer::setOrientation(const Orientation& o)
{
    if (o == m_orient) return;
    m_orient = o;
    if (m_hasFrame) convert();
}

bool CrtRenderer::uploadSample(GstSample* sample)
{
    GstCaps* caps = gst_sample_get_caps(sample);
    GstBuffer* buf = gst_sample_get_buffer(sample);
    if (!caps || !buf) return false;
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) return false;
    m_info = info;
    m_infoValid = true;
    switch (GST_VIDEO_INFO_FORMAT(&m_info)) {
    case GST_VIDEO_FORMAT_RGBA: case GST_VIDEO_FORMAT_RGBx: m_format = 0; break;
    case GST_VIDEO_FORMAT_BGRA: case GST_VIDEO_FORMAT_BGRx: m_format = 1; break;
    case GST_VIDEO_FORMAT_NV12: m_format = 2; break;
    case GST_VIDEO_FORMAT_I420: m_format = 3; break;
    default:
        qWarning() << "Unexpected video format" << pixelFormatName();
        return false;
    }

    GstVideoFrame frame;
    if (!gst_video_frame_map(&frame, &m_info, buf, GST_MAP_READ)) return false;
    const int planes = GST_VIDEO_FRAME_N_PLANES(&frame);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int p = 0; p < planes && p < 3; ++p) {
        const int comp = p;   // for NV12/I420/RGB planes, plane p starts with component p
        const int w = GST_VIDEO_FRAME_COMP_WIDTH(&frame, comp);
        const int h = GST_VIDEO_FRAME_COMP_HEIGHT(&frame, comp);
        const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&frame, p);
        GLenum fmt, ifmt;
        int bpp;
        if (m_format <= 1) { fmt = GL_RGBA; ifmt = GL_RGBA8; bpp = 4; }
        else if (m_format == 2 && p == 1) { fmt = GL_RG; ifmt = GL_RG8; bpp = 2; }
        else { fmt = GL_RED; ifmt = GL_R8; bpp = 1; }
        if (!m_planeTex[p]) glGenTextures(1, &m_planeTex[p]);
        glBindTexture(GL_TEXTURE_2D, m_planeTex[p]);
        if (m_planeSize[p] != QSize(w, h) || m_planeFmt[p] != ifmt) {
            glTexImage2D(GL_TEXTURE_2D, 0, GLint(ifmt), w, h, 0, fmt, GL_UNSIGNED_BYTE, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            m_planeSize[p] = QSize(w, h);
            m_planeFmt[p] = ifmt;
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, stride / bpp);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, fmt, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA(&frame, p));
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gst_video_frame_unmap(&frame);
    m_hasFrame = true;
    convert();
    return true;
}

void CrtRenderer::ensureTarget(GLuint& tex, GLuint& fbo, QSize& cur, const QSize& size, bool mipmaps, bool blackBorder)
{
    if (tex && cur == size) return;
    if (!tex) glGenTextures(1, &tex);
    if (!fbo) glGenFramebuffers(1, &fbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const GLint wrap = blackBorder ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if (blackBorder) {
        const GLfloat black[4] = {0, 0, 0, 1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, black);
    }
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    cur = size;
}

void CrtRenderer::convert()
{
    if (!m_hasFrame || !m_infoValid) return;
    const SourceFormat sf = sourceFormat();
    const QSize out = orientedStorageSize(sf);
    if (out.isEmpty()) return;
    ensureTarget(m_imgTex, m_imgFbo, m_imgSize, out, true, false);
    glBindFramebuffer(GL_FRAMEBUFFER, m_imgFbo);
    glViewport(0, 0, out.width(), out.height());
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);

    m_convert.bind();
    m_convert.setUniformValue("uOutSize", QVector2D(out.width(), out.height()));
    m_convert.setUniformValue("uFormat", m_format);
    m_convert.setUniformValue("uTex0", 0);
    m_convert.setUniformValue("uTex1", 1);
    m_convert.setUniformValue("uTex2", 2);
    float r0[3], r1[3];
    orientationMatrix(m_orient, r0, r1);
    m_convert.setUniformValue("uOrient0", QVector3D(r0[0], r0[1], r0[2]));
    m_convert.setUniformValue("uOrient1", QVector3D(r1[0], r1[1], r1[2]));

    // YCbCr -> RGB from the stream's colorimetry (BT.601/709/2020, limited or full range).
    gdouble kr = 0.2126, kb = 0.0722;
    if (!gst_video_color_matrix_get_Kr_Kb(m_info.colorimetry.matrix, &kr, &kb)) {
        if (GST_VIDEO_INFO_HEIGHT(&m_info) < 720) { kr = 0.299; kb = 0.114; }
    }
    const double kg = 1.0 - kr - kb;
    const bool full = m_info.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255;
    QVector3D offset(full ? 0.f : 16.f / 255.f, 128.f / 255.f, 128.f / 255.f);
    QVector3D scale(full ? 1.f : 255.f / 219.f, full ? 1.f : 255.f / 224.f, full ? 1.f : 255.f / 224.f);
    QMatrix3x3 m;
    // column-major for GLSL: rows R,G,B; columns Y,Cb,Cr
    const float vals[9] = {
        1.f, 0.f, float(2.0 * (1.0 - kr)),
        1.f, float(-2.0 * kb * (1.0 - kb) / kg), float(-2.0 * kr * (1.0 - kr) / kg),
        1.f, float(2.0 * (1.0 - kb)), 0.f};
    m = QMatrix3x3(vals);
    m_convert.setUniformValue("uYuvOffset", offset);
    m_convert.setUniformValue("uYuvScale", scale);
    m_convert.setUniformValue("uYuvToRgb", m);

    for (int i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i);
        glBindTexture(GL_TEXTURE_2D, m_planeTex[i] ? m_planeTex[i] : m_planeTex[0]);
    }
    drawQuad();
    m_convert.release();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    glGenerateMipmap(GL_TEXTURE_2D);
    m_blurDirty = true;
    m_lowDirty = true;
}

void CrtRenderer::updateLowRes(const QSize& size)
{
    // Area-averaged downsample of the oriented picture (the 4-tap pass samples the
    // mipmapped image at the matching level), mipmapped again for the beam styles.
    if (!m_lowDirty && m_lowTex && m_lowSize == size) return;
    ensureTarget(m_lowTex, m_lowFbo, m_lowSize, size, true, false);
    glBindFramebuffer(GL_FRAMEBUFFER, m_lowFbo);
    glViewport(0, 0, size.width(), size.height());
    glDisable(GL_BLEND);
    m_down.bind();
    m_down.setUniformValue("uSrc", 0);
    m_down.setUniformValue("uOutSize", QVector2D(size.width(), size.height()));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    drawQuad();
    m_down.release();
    glBindTexture(GL_TEXTURE_2D, m_lowTex);
    glGenerateMipmap(GL_TEXTURE_2D);
    m_lowDirty = false;
}

void CrtRenderer::computeBlur()
{
    // Work at a fixed size relative to the picture (long side 480 px) so glow radii
    // look the same for SD, HD, ultrawide and vertical sources.
    const double longSide = std::max(m_imgSize.width(), m_imgSize.height());
    const double s = std::min(1.0, 480.0 / longSide);
    const QSize bs(std::max(1, int(m_imgSize.width() * s)), std::max(1, int(m_imgSize.height() * s)));
    ensureTarget(m_blurTex[0], m_blurFbo[0], m_blurSize[0], bs, false, true);
    ensureTarget(m_blurTex[1], m_blurFbo[1], m_blurSize[1], bs, false, true);
    glViewport(0, 0, bs.width(), bs.height());

    glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo[0]);
    m_down.bind();
    m_down.setUniformValue("uSrc", 0);
    m_down.setUniformValue("uOutSize", QVector2D(bs.width(), bs.height()));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    drawQuad();
    m_down.release();

    m_blur.bind();
    m_blur.setUniformValue("uSrc", 0);
    m_blur.setUniformValue("uOutSize", QVector2D(bs.width(), bs.height()));
    for (int iter = 0; iter < 2; ++iter) {
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo[1]);
        glBindTexture(GL_TEXTURE_2D, m_blurTex[0]);
        m_blur.setUniformValue("uDir", QVector2D(1.f + iter, 0.f));
        drawQuad();
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurFbo[0]);
        glBindTexture(GL_TEXTURE_2D, m_blurTex[1]);
        m_blur.setUniformValue("uDir", QVector2D(0.f, 1.f + iter));
        drawQuad();
    }
    m_blur.release();
    m_blurDirty = false;
}

void CrtRenderer::setOsdImage(const QImage& img)
{
    if (!m_osdTex) glGenTextures(1, &m_osdTex);
    const QImage im = img.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    glBindTexture(GL_TEXTURE_2D, m_osdTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, im.width(), im.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, im.constBits());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void CrtRenderer::draw(GLuint targetFbo, const DrawParams& d)
{
    // Not while comparing: the "original" half shares this pass and must stay clean.
    const bool persist = d.history && !d.bypass && d.opacity >= 1.f && m_hasFrame && d.params.persistence > 0.f &&
                         !d.image.isEmpty() && d.split < 0.f;
    if (!persist) m_histValid = false;
    const GLuint finalTarget = targetFbo;
    if (persist) {
        ensureTarget(m_curTex, m_curFbo, m_curSize, d.viewport, false, false);
        targetFbo = m_curFbo;
    }
    drawCrt(targetFbo, d);
    if (!persist) return;

    // Afterglow: decay the history by the elapsed time, keep the brighter, show it.
    for (int i = 0; i < 2; ++i) {
        if (m_histSize[i] != d.viewport) m_histValid = false;
        ensureTarget(m_histTex[i], m_histFbo[i], m_histSize[i], d.viewport, false, false);
    }
    if (!m_histValid) {
        for (int i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_histFbo[i]);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glClear(GL_COLOR_BUFFER_BIT);
        }
        m_histTime = d.time;
        m_histValid = true;
    }
    const float dt = std::clamp(d.time - m_histTime, 0.f, 0.25f);
    m_histTime = d.time;
    const float tau = 0.004f + d.params.persistence * 0.12f;   // seconds; blue fades ~2x faster
    const QVector3D decay(std::exp(-dt / tau), std::exp(-dt / tau), std::exp(-dt / (tau * 0.45f)));
    const int next = 1 - m_histIdx;
    glBindFramebuffer(GL_FRAMEBUFFER, m_histFbo[next]);
    glViewport(0, 0, d.viewport.width(), d.viewport.height());
    glDisable(GL_BLEND);
    m_persist.bind();
    m_persist.setUniformValue("uCurrent", 0);
    m_persist.setUniformValue("uHistory", 1);
    m_persist.setUniformValue("uOutSize", QVector2D(d.viewport.width(), d.viewport.height()));
    m_persist.setUniformValue("uDecay", decay);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_histTex[m_histIdx]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_curTex);
    drawQuad();
    m_persist.release();
    m_histIdx = next;
    glBindFramebuffer(GL_FRAMEBUFFER, finalTarget);
    glViewport(0, 0, d.viewport.width(), d.viewport.height());
    m_copy.bind();
    m_copy.setUniformValue("uSrc", 0);
    m_copy.setUniformValue("uOutSize", QVector2D(d.viewport.width(), d.viewport.height()));
    glBindTexture(GL_TEXTURE_2D, m_histTex[m_histIdx]);
    drawQuad();
    m_copy.release();
}

void CrtRenderer::drawCrt(GLuint targetFbo, const DrawParams& d)
{
    const bool wantBlur = !d.bypass && (d.params.bloom > 0.f || d.params.glow > 0.f) && m_hasFrame;
    if (wantBlur && m_blurDirty) computeBlur();
    const bool lowRes = !d.bypass && m_hasFrame && !d.pixelSize.isEmpty();
    if (lowRes) updateLowRes(d.pixelSize);

    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, d.viewport.width(), d.viewport.height());
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    if (d.opacity >= 1.f) {
        glDisable(GL_BLEND);
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
    } else {
        glEnable(GL_BLEND);
        glBlendColor(0.f, 0.f, 0.f, d.opacity);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
    }
    if (!m_hasFrame || d.image.isEmpty() || d.tube.isEmpty()) { glDisable(GL_BLEND); return; }

    const CrtParams& p = d.params;
    auto rect = [](const QRectF& r) { return QVector4D(r.x(), r.y(), r.width(), r.height()); };
    m_crt.bind();
    m_crt.setUniformValue("uImage", 0);
    m_crt.setUniformValue("uBlur", 1);
    m_crt.setUniformValue("uImageFull", 2);
    m_crt.setUniformValue("uPixelMode", lowRes ? p.pixelFilter : -1);
    m_crt.setUniformValue("uColorDepth", p.colorDepth);
    m_crt.setUniformValue("uDither", p.dither);
    m_crt.setUniformValue("uGridSize", lowRes ? QVector2D(d.pixelSize.width(), d.pixelSize.height())
                                              : QVector2D(m_imgSize.width(), m_imgSize.height()));
    m_crt.setUniformValue("uVideoStd", p.videoStandard);
    m_crt.setUniformValue("uTransType", d.transType);
    m_crt.setUniformValue("uTransT", d.transT);
    m_crt.setUniformValue("uStatic", d.staticLevel);
    m_crt.setUniformValue("uOsd", 3);
    m_crt.setUniformValue("uOsdAlpha", m_osdTex ? d.osdAlpha : 0.f);
    m_crt.setUniformValue("uOsdRect", QVector4D(d.osdRect.x(), d.osdRect.y(), d.osdRect.width(), d.osdRect.height()));
    if (lowRes) {
        const QSizeF ps(d.pixelSize);
        m_crt.setUniformValue("uPixelSize", QVector2D(float(ps.width()), float(ps.height())));
        // Screen pixels per low-res pixel (for the sharp filter's one-pixel edge).
        m_crt.setUniformValue("uTexelPx", QVector2D(float(d.image.width() / d.src.width() / ps.width()),
                                                    float(d.image.height() / d.src.height() / ps.height())));
    }
    m_crt.setUniformValue("uViewport", QVector2D(d.viewport.width(), d.viewport.height()));
    m_crt.setUniformValue("uTube", rect(d.tube));
    m_crt.setUniformValue("uImg", rect(d.image));
    m_crt.setUniformValue("uSrc", rect(d.src));
    m_crt.setUniformValue("uTime", d.time);
    m_crt.setUniformValue("uFrameRand", d.frameRand);
    m_crt.setUniformValue("uBypass", d.bypass);
    m_crt.setUniformValue("uSplit", d.split);
    m_crt.setUniformValue("uHasBlur", wantBlur);
    m_crt.setUniformValue("uScanStrength", p.scanStrength);
    m_crt.setUniformValue("uScanWidth", p.scanWidth);
    m_crt.setUniformValue("uLines", d.lines);
    m_crt.setUniformValue("uScanType", p.scanType);
    m_crt.setUniformValue("uSrcLod", d.srcLod);
    m_crt.setUniformValue("uVhsJitter", p.vhsJitter);
    m_crt.setUniformValue("uVhsTracking", p.vhsTracking);
    m_crt.setUniformValue("uVhsHead", p.vhsHeadSwitch);
    m_crt.setUniformValue("uVhsChroma", p.vhsChromaDelay);
    m_crt.setUniformValue("uVhsDrop", p.vhsDropouts);
    m_crt.setUniformValue("uFilmGrain", p.filmGrain);
    m_crt.setUniformValue("uGateWeave", p.gateWeave);
    m_crt.setUniformValue("uFilmFlicker", p.filmFlicker);
    m_crt.setUniformValue("uFilmDamage", p.filmDamage);
    m_crt.setUniformValue("uVhsSoft", p.vhsSoftness);
    m_crt.setUniformValue("uDotCrawl", p.dotCrawl);
    m_crt.setUniformValue("uRainbow", p.rainbow);
    m_crt.setUniformValue("uLaserRot", p.laserRot);
    m_crt.setUniformValue("uMaskType", p.maskType);
    m_crt.setUniformValue("uMaskStrength", p.maskStrength);
    m_crt.setUniformValue("uMaskScale", std::max(1.0f, p.maskScale));
    m_crt.setUniformValue("uCurvature", p.curvature);
    m_crt.setUniformValue("uCorner", p.cornerRadius);
    m_crt.setUniformValue("uOverscan", p.overscan);
    m_crt.setUniformValue("uBloom", p.bloom);
    m_crt.setUniformValue("uGlow", p.glow);
    m_crt.setUniformValue("uChroma", p.chroma);
    m_crt.setUniformValue("uBleed", p.bleed);
    m_crt.setUniformValue("uNoise", p.noise);
    m_crt.setUniformValue("uFlicker", p.flicker);
    m_crt.setUniformValue("uVignette", p.vignette);
    m_crt.setUniformValue("uBrightness", p.brightness);
    m_crt.setUniformValue("uContrast", p.contrast);
    m_crt.setUniformValue("uSaturation", p.saturation);
    m_crt.setUniformValue("uWarmth", p.warmth);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_osdTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);                 // full resolution: bypass / "original" side
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_blurTex[0] ? m_blurTex[0] : m_imgTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, lowRes ? m_lowTex : m_imgTex);
    drawQuad();
    m_crt.release();
    glDisable(GL_BLEND);
}

GLuint CrtRenderer::renderToTexture(const DrawParams& d)
{
    if (d.viewport.isEmpty()) return 0;
    ensureTarget(m_outTex, m_outFbo, m_outSize, d.viewport, true, true);
    draw(m_outFbo, d);
    glBindTexture(GL_TEXTURE_2D, m_outTex);
    glGenerateMipmap(GL_TEXTURE_2D);
    return m_outTex;
}

GLuint CrtRenderer::blurTexture()
{
    if (!m_hasFrame) return 0;
    if (m_blurDirty || !m_blurTex[0]) computeBlur();
    return m_blurTex[0];
}

CrtRenderer::DrawParams CrtRenderer::makeDrawParams(const ViewSettings& vs, const SourceFormat& src, const QSize& viewport,
                                                    const QRectF& area, float time, quint32 frameCounter)
{
    DrawParams d;
    d.viewport = viewport;
    const LayoutResult L = computeLayout(displaySize(src, vs.aspectOverride), vs.mode, vs.crop, area);
    if (!L.valid) return d;
    d.image = L.visibleRect;
    d.src = L.srcRect;
    d.tube = vs.params.includeBars ? area : L.visibleRect;
    d.params = vs.params;
    d.bypass = vs.bypass;
    d.split = (vs.compare && !vs.bypass) ? float(area.left() + area.width() * vs.split) : -1.f;
    d.time = time;
    d.frameRand = float((frameCounter * 2654435761u % 1000u) / 1000.0);
    d.pixelSize = pixelatedSize(src, vs.aspectOverride, vs.params.pixelHeight, vs.params.pixelWidth);
    // Rows of the picture as the CRT sees it: the lowered resolution when one is set.
    const int pictureRows = d.pixelSize.isEmpty() ? orientedStorageSize(src).height() : d.pixelSize.height();
    if (vs.params.scanLines > 0) {
        d.lines = float(vs.params.scanLines);
    } else {
        // Auto: the visible source lines, scaled to the tube when bars are included,
        // halved until each line spans >= 2.5 px so the pattern never moires.
        // With a lowered resolution this is one scanline per pixel row.
        double lines = pictureRows * L.srcRect.height();
        if (vs.params.includeBars && d.image.height() > 0) lines *= d.tube.height() / d.image.height();
        d.lines = float(autoScanlineCount(lines, d.tube.height()));
    }
    // Beam-reconstruction styles read the picture at scanline resolution: pick the mip
    // level whose row count matches the number of scanlines covering the picture.
    const double rowsVisible = pictureRows * L.srcRect.height();
    const double linesOverPicture = d.lines * (d.tube.height() > 0 ? d.image.height() / d.tube.height() : 1.0);
    d.srcLod = linesOverPicture > 0 ? float(std::max(0.0, std::log2(rowsVisible / linesOverPicture))) : 0.f;
    return d;
}

QImage CrtRenderer::renderToImage(const DrawParams& dIn)
{
    if (dIn.viewport.isEmpty()) return {};
    DrawParams d = dIn;
    d.history = false;   // a screenshot must not advance (or reset) the live afterglow
    GLuint tex = 0, fbo = 0;
    QSize cur;
    ensureTarget(tex, fbo, cur, d.viewport, false, false);
    drawCrt(fbo, d);
    QImage img(d.viewport, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, d.viewport.width(), d.viewport.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    return img.mirrored(false, true).convertToFormat(QImage::Format_RGB32);
}

QImage CrtRenderer::renderOriginal(const QSize& outSize)
{
    if (!m_hasFrame || outSize.isEmpty()) return {};
    DrawParams d;
    d.viewport = outSize;
    d.tube = d.image = QRectF(QPointF(0, 0), QSizeF(outSize));
    d.src = QRectF(0, 0, 1, 1);
    d.bypass = true;
    return renderToImage(d);
}
