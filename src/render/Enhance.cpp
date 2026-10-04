// Enhance (2.13), as part of CrtRenderer: sharper upscaling with effects off, and frame
// generation (the picture at moments between two frames, along the motion between them).
//
// Both are for a graphics card. Neither is an AI model: the upscaler is a Lanczos
// reconstruction with a contrast-adaptive sharpening pass, and frame generation is a
// coarse-to-fine block motion search with a motion-compensated blend.
#include "CrtRenderer.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

bool CrtRenderer::initEnhance(QString* error)
{
    return loadProgram(m_enhUp, ":/shaders/enh_upscale.frag", error) && loadProgram(m_enhSharp, ":/shaders/enh_sharpen.frag", error) &&
           loadProgram(m_fiLuma, ":/shaders/fi_luma.frag", error) && loadProgram(m_fiFlow, ":/shaders/fi_flow.frag", error) &&
           loadProgram(m_fiBlend, ":/shaders/fi_blend.frag", error);
}

void CrtRenderer::destroyEnhance()
{
    auto drop = [this](GLuint& tex, GLuint& fbo) {
        if (tex) glDeleteTextures(1, &tex);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        tex = fbo = 0;
    };
    for (int i = 0; i < 2; ++i) {
        drop(m_upTex[i], m_upFbo[i]);
        drop(m_lumTex[i], m_lumFbo[i]);
        for (int l = 0; l < kFlowLevels; ++l) drop(m_flowTex[i][l], m_flowFbo[i][l]);
        m_upSize[i] = m_lumSize[i] = QSize();
        m_lumValid[i] = false;
    }
    drop(m_prevTex, m_prevFbo);
    drop(m_fgTex, m_fgFbo);
    m_prevSize = m_fgSize = QSize();
    m_prevHas = m_pairValid = m_flowReady = false;
    m_upDirty = true;
}

// A float target (vectors): RGBA16F, filtered.
void CrtRenderer::ensureTargetF(GLuint& tex, GLuint& fbo, QSize& cur, const QSize& size, bool mipmaps)
{
    if (tex && cur == size) return;
    if (!tex) glGenTextures(1, &tex);
    if (!fbo) glGenFramebuffers(1, &fbo);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, size.width(), size.height(), 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (mipmaps) glGenerateMipmap(GL_TEXTURE_2D);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    cur = size;
}

// ---- upscaling ------------------------------------------------------------------------

// The picture at the size it is shown at, upscaled and sharpened; 0 when it is not being
// enlarged (or by too little to matter). Made once per frame and size.
GLuint CrtRenderer::upscaledPicture(const DrawParams& d)
{
    if (!m_hasFrame || !m_imgTex || d.image.isEmpty() || d.src.width() <= 0 || d.src.height() <= 0) return 0;
    // The whole picture's size on screen (the part shown may be a window of it).
    double w = d.image.width() / d.src.width(), h = d.image.height() / d.src.height();
    if (std::max(w / m_imgSize.width(), h / m_imgSize.height()) < 1.15) return 0;
    // Not more than four times the video in either direction, nor much more than the target can show.
    const double cap = std::min({1.0, 4.0 * m_imgSize.width() / w, 4.0 * m_imgSize.height() / h, 4096.0 / w, 4096.0 / h,
                                 2.0 * d.viewport.width() / w, 2.0 * d.viewport.height() / h});
    const QSize target(std::max(2, int(std::lround(w * cap))), std::max(2, int(std::lround(h * cap))));
    if (!m_upDirty && m_upTex[1] && m_upSize[1] == target && m_upSharp == d.enhanceSharp) return m_upTex[1];
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    ensureTarget(m_upTex[0], m_upFbo[0], m_upSize[0], target, false, false);
    glBindFramebuffer(GL_FRAMEBUFFER, m_upFbo[0]);
    glViewport(0, 0, target.width(), target.height());
    m_enhUp.bind();
    m_enhUp.setUniformValue("uSrc", 0);
    m_enhUp.setUniformValue("uSrcSize", QVector2D(m_imgSize.width(), m_imgSize.height()));
    m_enhUp.setUniformValue("uOutSize", QVector2D(target.width(), target.height()));
    m_enhUp.setUniformValue("uAntiRing", 0.8f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    drawQuad();
    m_enhUp.release();
    ensureTarget(m_upTex[1], m_upFbo[1], m_upSize[1], target, false, false);
    glBindFramebuffer(GL_FRAMEBUFFER, m_upFbo[1]);
    m_enhSharp.bind();
    m_enhSharp.setUniformValue("uSrc", 0);
    m_enhSharp.setUniformValue("uSize", QVector2D(target.width(), target.height()));
    m_enhSharp.setUniformValue("uSharp", std::clamp(d.enhanceSharp, 0.f, 1.f));
    glBindTexture(GL_TEXTURE_2D, m_upTex[0]);
    drawQuad();
    m_enhSharp.release();
    m_upDirty = false;
    m_upSharp = d.enhanceSharp;
    ++m_enh.upscaled;
    m_enh.upSize = target;
    return m_upTex[1];
}

// ---- frame generation -----------------------------------------------------------------

void CrtRenderer::setFrameGeneration(bool on)
{
    if (m_fgOn == on) return;
    m_fgOn = on;
    m_prevHas = m_pairValid = m_flowReady = false;
    m_lumValid[0] = m_lumValid[1] = false;
}

// A new frame is about to be uploaded: the picture so far becomes the frame before. The two
// textures change places (nothing is copied); the new frame is written into the older one.
void CrtRenderer::keepPreviousFrame()
{
    if (!m_fgOn || !m_hasFrame || !m_imgTex) return;
    ensureMipmaps();   // (the blend compares the two frames a little blurred)
    std::swap(m_imgTex, m_prevTex);
    std::swap(m_imgFbo, m_prevFbo);
    std::swap(m_imgSize, m_prevSize);
    std::swap(m_lumTex[0], m_lumTex[1]);
    std::swap(m_lumFbo[0], m_lumFbo[1]);
    std::swap(m_lumSize[0], m_lumSize[1]);
    m_lumValid[1] = m_lumValid[0];
    m_lumValid[0] = false;
    m_prevPts = m_framePts;
    m_prevHas = true;
}

void CrtRenderer::frameArrived()
{
    m_upDirty = true;
    m_flowReady = false;
    m_lumValid[0] = false;
    // A pair to generate frames between: two frames of the same size that follow each other
    // closely in the video (not across a jump, and not after a long gap).
    const qint64 gap = m_framePts - m_prevPts;
    m_pairValid = m_fgOn && m_prevHas && m_prevTex && m_prevSize == m_imgSize && m_prevPts >= 0 && m_framePts >= 0 &&
                  gap > 0 && gap < 130 * 1000000ll;
}

void CrtRenderer::computeFlow()
{
    if (!m_pairValid) return;
    ensureMipmaps();
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    // Motion is searched on brightness, 480 pixels wide (or the video's own size if smaller).
    const int fw = std::max(64, std::min(480, m_imgSize.width()) / 16 * 16);
    const int fh = std::max(32, int(std::lround(double(fw) * m_imgSize.height() / m_imgSize.width() / 16.0)) * 16);
    const QSize fs(fw, fh);
    m_enh.flowSize = fs;
    const float lod = float(std::max(0.0, std::log2(double(m_imgSize.width()) / fw)));
    const GLuint rgb[2] = {m_imgTex, m_prevTex};
    for (int i = 0; i < 2; ++i) {
        if (m_lumValid[i] && m_lumSize[i] == fs) continue;
        ensureTarget(m_lumTex[i], m_lumFbo[i], m_lumSize[i], fs, true, false);
        glBindFramebuffer(GL_FRAMEBUFFER, m_lumFbo[i]);
        glViewport(0, 0, fw, fh);
        m_fiLuma.bind();
        m_fiLuma.setUniformValue("uSrc", 0);
        m_fiLuma.setUniformValue("uOutSize", QVector2D(fw, fh));
        m_fiLuma.setUniformValue("uLod", lod);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, rgb[i]);
        drawQuad();
        m_fiLuma.release();
        glBindTexture(GL_TEXTURE_2D, m_lumTex[i]);
        glGenerateMipmap(GL_TEXTURE_2D);
        m_lumValid[i] = true;
    }
    // Both directions: from the frame before to this one (on the frame before's grid), and back.
    m_fiFlow.bind();
    m_fiFlow.setUniformValue("uA", 0);
    m_fiFlow.setUniformValue("uB", 1);
    m_fiFlow.setUniformValue("uPrev", 2);
    for (int dir = 0; dir < 2; ++dir) {
        const GLuint a = dir == 0 ? m_lumTex[1] : m_lumTex[0], b = dir == 0 ? m_lumTex[0] : m_lumTex[1];
        for (int level = kFlowLevels - 1; level >= 0; --level) {
            const QSize ls(std::max(4, fw >> level), std::max(4, fh >> level));
            ensureTargetF(m_flowTex[dir][level], m_flowFbo[dir][level], m_flowSize[dir][level], ls, level == 0);
            glBindFramebuffer(GL_FRAMEBUFFER, m_flowFbo[dir][level]);
            glViewport(0, 0, ls.width(), ls.height());
            const bool coarsest = level == kFlowLevels - 1;
            m_fiFlow.setUniformValue("uSize", QVector2D(ls.width(), ls.height()));
            m_fiFlow.setUniformValue("uLevel", float(level));
            m_fiFlow.setUniformValue("uHavePrev", coarsest ? 0 : 1);
            // Coarsest: a wide search (3 texels of 30 across: a tenth of the picture per frame).
            // Finer levels: one texel around each prediction (its own and its four neighbours'). The
            // finest: around its own prediction only, then placed between texels.
            m_fiFlow.setUniformValue("uRadius", coarsest ? 3 : (level == 0 ? 2 : 1));
            m_fiFlow.setUniformValue("uPreds", level == 0 ? 1 : 5);
            m_fiFlow.setUniformValue("uStep", level == 0 ? 0.5f : 1.0f);
            m_fiFlow.setUniformValue("uSubTexel", 0);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, coarsest ? m_flowTex[dir][level] : m_flowTex[dir][level + 1]);
            if (coarsest) glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, b);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, a);
            drawQuad();
        }
        glBindTexture(GL_TEXTURE_2D, m_flowTex[dir][0]);
        glGenerateMipmap(GL_TEXTURE_2D);   // (its smallest copy is the match over the whole picture: a cut shows there)
    }
    m_fiFlow.release();
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    m_flowReady = true;
    ++m_enh.pairs;
}

bool CrtRenderer::renderBetween(float t, int mode)
{
    if (!m_pairValid) return false;
    if (!m_flowReady) computeFlow();
    ensureMipmaps();
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    ensureTarget(m_fgTex, m_fgFbo, m_fgSize, m_imgSize, true, false);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fgFbo);
    glViewport(0, 0, m_imgSize.width(), m_imgSize.height());
    m_fiBlend.bind();
    m_fiBlend.setUniformValue("uA", 0);
    m_fiBlend.setUniformValue("uB", 1);
    m_fiBlend.setUniformValue("uFwd", 2);
    m_fiBlend.setUniformValue("uBwd", 3);
    m_fiBlend.setUniformValue("uOutSize", QVector2D(m_imgSize.width(), m_imgSize.height()));
    m_fiBlend.setUniformValue("uT", std::clamp(t, 0.f, 1.f));
    m_fiBlend.setUniformValue("uMode", mode);
    // (Above 1080p the extra tries cost more than they show: four times the pixels, each a fortieth of the picture's detail.)
    m_fiBlend.setUniformValue("uFine", qint64(m_imgSize.width()) * m_imgSize.height() <= 2300000 ? 1 : 0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_flowTex[1][0]);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_flowTex[0][0]);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_imgTex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_prevTex);
    drawQuad();
    m_fiBlend.release();
    ++m_enh.between;
    return true;
}

// For one draw, the picture's place is taken by the frame before (phase 0) or by a frame
// generated between the two. Everything downstream (the looks, the glow, upscaling) then
// works from it as from any frame. endBetween() puts the real frame back.
bool CrtRenderer::beginBetween(float t)
{
    if (!m_fgOn || !m_pairValid || m_fgSwap != 0 || t >= 0.995f) return false;
    m_fgSavedMips = m_mipsStale;
    if (t <= 0.01f) {
        std::swap(m_imgTex, m_prevTex);
        std::swap(m_imgFbo, m_prevFbo);
        m_fgSwap = 1;
        m_mipsStale = false;   // (made when it became the frame before)
    } else {
        if (!renderBetween(t, 0)) return false;
        std::swap(m_imgTex, m_fgTex);
        std::swap(m_imgFbo, m_fgFbo);
        m_fgSwap = 2;
        m_mipsStale = true;
    }
    m_blurDirty = m_lowDirty = m_upDirty = true;
    return true;
}

void CrtRenderer::endBetween()
{
    if (m_fgSwap == 1) { std::swap(m_imgTex, m_prevTex); std::swap(m_imgFbo, m_prevFbo); }
    else if (m_fgSwap == 2) { std::swap(m_imgTex, m_fgTex); std::swap(m_imgFbo, m_fgFbo); }
    m_fgSwap = 0;
    m_mipsStale = m_fgSavedMips;
    m_blurDirty = m_lowDirty = m_upDirty = true;   // (they were made from the picture in between)
}

QImage CrtRenderer::renderBetweenImage(float t, int mode)
{
    if (!m_pairValid || !renderBetween(t, mode)) return {};
    QImage img(m_imgSize, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fgFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, m_imgSize.width(), m_imgSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    return img.convertToFormat(QImage::Format_RGB32);   // (the picture's textures have row 0 at the top)
}
