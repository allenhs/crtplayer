// Enhance (2.13), as part of CrtRenderer: sharper upscaling with effects off, and frame
// generation (the picture at moments between two frames, along the motion between them).
//
// Both are for a graphics card. The built-in methods are not AI models: the upscaler is a
// Lanczos reconstruction with a contrast-adaptive sharpening pass, and frame generation is
// a coarse-to-fine block motion search with a motion-compensated blend.
//
// 2.15: where NVIDIA's Video Effects SDK is installed, its Video Super Resolution and Video
// Frame Generation (which are AI models) take their place, through a helper program (see
// NvEnhancer.h, and the last part of this file). Whenever the helper has no picture to
// give, the draw is done with the built-in methods.
#include "CrtRenderer.h"
#include "NvEnhancer.h"

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
    nvDropTicket();
    m_nvUpEpoch = 0;
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

// The size the picture is upscaled to: the size it is shown at; empty when it is not being
// enlarged (or by too little to matter). uniform: by the same factor both ways, to even
// numbers (what NVIDIA's super resolution is given; the rest of the way it is stretched).
QSize CrtRenderer::upscaleTarget(const DrawParams& d, bool uniform) const
{
    if (!m_hasFrame || m_imgSize.isEmpty() || d.image.isEmpty() || d.src.width() <= 0 || d.src.height() <= 0) return {};
    // The whole picture's size on screen (the part shown may be a window of it).
    const double w = d.image.width() / d.src.width(), h = d.image.height() / d.src.height();
    if (std::max(w / m_imgSize.width(), h / m_imgSize.height()) < 1.15) return {};
    // Not more than four times the video in either direction, nor much more than the target can show.
    const double cap = std::min({1.0, 4.0 * m_imgSize.width() / w, 4.0 * m_imgSize.height() / h, 4096.0 / w, 4096.0 / h,
                                 2.0 * d.viewport.width() / w, 2.0 * d.viewport.height() / h});
    if (!uniform) return QSize(std::max(2, int(std::lround(w * cap))), std::max(2, int(std::lround(h * cap))));
    const double k = std::min(w * cap / m_imgSize.width(), h * cap / m_imgSize.height());
    if (k < 1.15) return {};
    return QSize(std::max(2, int(std::lround(m_imgSize.width() * k / 2.0)) * 2), std::max(2, int(std::lround(m_imgSize.height() * k / 2.0)) * 2));
}

// The picture at the size it is shown at, upscaled and sharpened; 0 when it is not being
// enlarged (or by too little to matter). Made once per frame and size.
GLuint CrtRenderer::upscaledPicture(const DrawParams& d)
{
    if (!m_hasFrame || !m_imgTex || d.image.isEmpty() || d.src.width() <= 0 || d.src.height() <= 0) return 0;
    const QSize target = upscaleTarget(d, false);
    if (target.isEmpty()) return 0;
    if (m_nvUpEpoch) { m_upDirty = true; m_nvUpEpoch = 0; }   // (the texture held NVIDIA's picture)
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
    m_prevEpoch = m_curEpoch;
    m_prevHas = true;
}

void CrtRenderer::frameArrived()
{
    m_curEpoch = ++m_epochs;
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
bool CrtRenderer::beginBetween(float t, bool nvidia, bool live)
{
    if (!m_fgOn || m_fgSwap != 0) return false;
    if (nvidia) {
        // NVIDIA's frame generation: 1 its picture is in m_fgTex; 0 this draw shows the newest frame; -1 no picture from it.
        const int got = nvBetween(t, live);
        if (got == 0) return false;
        if (got == 1) {
            m_fgSavedMips = m_mipsStale;
            std::swap(m_imgTex, m_fgTex);
            std::swap(m_imgFbo, m_fgFbo);
            m_fgSwap = 2;
            m_mipsStale = true;
            m_blurDirty = m_lowDirty = m_upDirty = true;
            return true;
        }
    }
    if (!m_pairValid || t >= 0.995f) return false;
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
    if (!m_pairValid) return {};
    // NVIDIA's, when it is what a draw would use now.
    if (mode == 0 && m_nv && m_nvMotion && m_fgOn) {
        NvEnhancer::Config c;
        c.src = c.out = m_imgSize;
        c.mode = m_nvMode;
        if (m_nv->ready(c) && nvFeed(true))
            if (const uchar* px = nvTake(t <= 0.f ? 0.f : t >= 1.f ? 1.f : t, false, 1)) {
                ++m_enh.nvBetween;
                return QImage(px, m_imgSize.width(), m_imgSize.height(), m_imgSize.width() * 4, QImage::Format_RGBA8888).convertToFormat(QImage::Format_RGB32);
            }
    }
    if (!renderBetween(t, mode)) return {};
    QImage img(m_imgSize, QImage::Format_RGBA8888);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fgFbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, m_imgSize.width(), m_imgSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    return img.convertToFormat(QImage::Format_RGB32);   // (the picture's textures have row 0 at the top)
}

// ---- NVIDIA's Video Super Resolution and Video Frame Generation ---------------------------
//
// The helper is given each of the video's frames (read back from the picture texture) and
// asked for pictures, which are put into the textures the built-in methods would have
// filled: m_fgTex (a frame between, at the video's size) or m_upTex[1] (the upscaled picture).
//
// While the video plays with frame generation, a draw asks for its picture and shows the
// one asked for at the draw before: the helper then works while the player draws, instead
// of the player waiting for it, at the price of one screen refresh of delay. A draw on its
// own (paused, a screenshot) asks and waits.

void CrtRenderer::setNvidia(NvEnhancer* nv, bool upscale, bool motion, int quality, int mode)
{
    if (nv != m_nv) nvDropTicket();
    m_nv = nv;
    m_nvUp = nv && upscale;
    m_nvMotion = nv && motion;
    m_nvQuality = std::clamp(quality, 1, 4);
    m_nvMode = std::clamp(mode, 0, 2);
    if (!nv) m_enh.nvKind = 0;
}

void CrtRenderer::nvDropTicket()
{
    if (m_nv && m_nvTicket) m_nv->abandon(m_nvTicket);
    m_nvTicket = 0;
    m_nvAskAgain = false;
}

// What this draw wants of NVIDIA's, with the effects for it open: 2 the upscaled picture
// (frames generated at that size, if frame generation is on), 1 frames between at the
// video's size, 0 nothing (or the helper is not ready: the built-in methods do this draw).
int CrtRenderer::nvPrepare(const DrawParams& d, bool plain)
{
    m_enh.nvKind = 0;
    if (!m_nv || !m_hasFrame || m_imgSize.isEmpty() || m_fgSwap != 0) return 0;
    const bool motion = m_fgOn && m_nvMotion;
    NvEnhancer::Config c;
    c.src = c.out = m_imgSize;
    if (motion) c.mode = m_nvMode;
    int kind = motion ? 1 : 0;
    if (plain && d.enhanceUp && m_nvUp) {
        const QSize target = upscaleTarget(d, true);
        if (!target.isEmpty()) { c.out = target; c.quality = m_nvQuality; kind = 2; }
    }
    if (!kind || !m_nv->ready(c)) { nvDropTicket(); return 0; }
    m_enh.nvKind = kind;
    return kind;
}

bool CrtRenderer::nvSendFrame(GLuint fbo, quint64 epoch, bool cut)
{
    uchar* in = m_nv->beginFrame();
    if (!in) return false;
    QElapsedTimer t;
    t.start();
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glReadPixels(0, 0, m_imgSize.width(), m_imgSize.height(), GL_RGBA, GL_UNSIGNED_BYTE, in);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    const double ms = t.nsecsElapsed() / 1e6;
    m_enh.nvReadMs = m_enh.nvReadMs <= 0 ? ms : m_enh.nvReadMs + (ms - m_enh.nvReadMs) * 0.1;
    return m_nv->endFrame(epoch, cut);
}

// The helper has the newest frame (pair: and, before it, the frame before).
bool CrtRenderer::nvFeed(bool pair)
{
    const bool wantPair = pair && m_pairValid && m_prevTex && m_prevSize == m_imgSize;
    if (m_nv->lastEpoch() == m_curEpoch && (!wantPair || m_nv->prevEpoch() == m_prevEpoch)) return true;
    if (wantPair && m_nv->lastEpoch() != m_prevEpoch && !nvSendFrame(m_prevFbo, m_prevEpoch, true)) return false;
    return nvSendFrame(m_imgFbo, m_curEpoch, !wantPair);
}

// The helper's picture for this draw. A draw on its own asks for the picture at t and waits
// for it. With draws following one another quickly (live), the picture asked for at the
// draw before is taken and this draw's is asked for, to be taken at the next; the first
// such draw waits for its own and (nvAfterUpload) asks for it once more for the next one.
const uchar* CrtRenderer::nvTake(float t, bool live, int kind)
{
    m_nvAskAgain = false;
    if (!live) {
        nvDropTicket();
        m_nvLastLive.invalidate();
        return m_nv->wait(m_nv->request(t));
    }
    // (the draw before was a moment ago; CRTPLAYER_NVFX_QUICK_MS, for the checks: what counts as a moment)
    static const int quickMs = qEnvironmentVariableIsSet("CRTPLAYER_NVFX_QUICK_MS") ? qEnvironmentVariableIntValue("CRTPLAYER_NVFX_QUICK_MS") : 50;
    const bool quick = m_nvLastLive.isValid() && m_nvLastLive.elapsed() < quickMs;
    m_nvLastLive.start();
    const int before = m_nvTicket;
    const bool fresh = quick && before && m_nvTicketGen == m_nv->generation() && m_nvTicketKind == kind;
    m_nvTicketGen = m_nv->generation();
    m_nvTicketKind = kind;
    if (fresh) {
        m_nvTicket = m_nv->request(t);
        const uchar* px = m_nv->wait(before);
        if (px) ++m_enh.nvPassing;
        return px;
    }
    nvDropTicket();
    m_nvAskAgain = quick;
    m_nvAskAgainT = t;
    return m_nv->wait(m_nv->request(t));
}

// (after the picture taken has been put into its texture: its memory may be written again)
void CrtRenderer::nvAfterUpload()
{
    if (!m_nvAskAgain) return;
    m_nvAskAgain = false;
    m_nvTicket = m_nv->request(m_nvAskAgainT);
}

void CrtRenderer::nvUpload(const uchar* px, GLuint tex, const QSize& size)
{
    QElapsedTimer t;
    t.start();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, px);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    const double ms = t.nsecsElapsed() / 1e6;
    m_enh.nvUploadMs = m_enh.nvUploadMs <= 0 ? ms : m_enh.nvUploadMs + (ms - m_enh.nvUploadMs) * 0.1;
}

// The upscaled picture for this draw (at the draw's moment between two frames, with frame
// generation), in m_upTex[1]; 0: none this time.
GLuint CrtRenderer::nvPicture(const DrawParams& d)
{
    const NvEnhancer::Config c = m_nv->config();
    const bool motion = c.mode >= 0;
    if (!nvFeed(motion)) { ++m_enh.nvMissed; return 0; }
    const float t = !motion || !m_pairValid || d.framePhase >= 0.995f ? 1.f : d.framePhase <= 0.01f ? 0.f : d.framePhase;
    // A draw on its own, of what the texture already holds: nothing to ask.
    if (!d.live && m_upTex[1] && m_upSize[1] == c.out && m_nvUpEpoch == m_curEpoch && m_nvUpT == t && m_nvUpGen == m_nv->generation()) {
        nvDropTicket();
        return m_upTex[1];
    }
    const uchar* px = nvTake(t, d.live, 2);
    if (!px) { ++m_enh.nvMissed; return 0; }
    ensureTarget(m_upTex[1], m_upFbo[1], m_upSize[1], c.out, false, false);
    nvUpload(px, m_upTex[1], c.out);
    nvAfterUpload();
    m_nvUpEpoch = d.live ? ~quint64(0) : m_curEpoch;   // (a picture shown a draw late is not this frame's: not kept for another draw)
    m_nvUpT = t;
    m_nvUpGen = m_nv->generation();
    m_upDirty = true;   // (for the built-in upscaler, should it be next)
    ++m_enh.nvUpscaled;
    m_enh.upSize = c.out;
    return m_upTex[1];
}

// Frame generation at the video's size, for a draw with a look (or with nothing to upscale):
// 1 the picture for this draw is in m_fgTex; 0 this draw shows the newest frame as it is;
// -1 none this time.
int CrtRenderer::nvBetween(float t, bool live)
{
    if (!nvFeed(true)) { ++m_enh.nvMissed; return -1; }
    const bool between = m_pairValid && t > 0.01f && t < 0.995f;
    if (!live) {
        nvDropTicket();
        if (!m_pairValid || t >= 0.995f) return 0;
        if (!between) return -1;   // (the frame before: the texture has it)
    }
    const uchar* px = nvTake(between ? t : (!m_pairValid || t >= 0.995f) ? 1.f : 0.f, live, 1);
    if (!px) { ++m_enh.nvMissed; return -1; }
    ensureTarget(m_fgTex, m_fgFbo, m_fgSize, m_imgSize, true, false);
    nvUpload(px, m_fgTex, m_imgSize);
    nvAfterUpload();
    ++m_enh.nvBetween;
    return 1;
}
