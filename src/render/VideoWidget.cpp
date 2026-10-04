#include <QtGlobal>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <time.h>
#endif
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include "VideoWidget.h"
#include "playback/Player.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QOpenGLContext>
#include <QRandomGenerator>
#include <QtMath>

QJsonObject SyncStats::toJson() const
{
    return {{"frames", frames}, {"meanMs", meanMs}, {"stddevMs", stddevMs},
            {"maxAbsMs", maxAbsMs}, {"framesWithin20ms", within20ms}, {"presentedFps", presentedFps},
            {"paintMsAvg", paintMsAvg}, {"paintMsMax", paintMsMax}};
}

VideoWidget::VideoWidget(Player* player, QWidget* parent, GlSurfaceWidget::Mode mode) : GlSurfaceWidget(mode, parent), m_player(player)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);
    m_clock.start();
    m_syncTimer.start();
    m_softwareGl = isSoftwareRenderer(probedRenderer());
    connect(m_player, &Player::frameReady, this, [this] { update(); });
    m_animTimer.setInterval(16);
    m_animTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_animTimer, &QTimer::timeout, this, [this] {
        if (needsAnimation()) update();
        else m_animTimer.stop();
    });

    auto mkLabel = [this](const QString& t) {
        auto* l = new QLabel(t, this);
        l->setObjectName("compareLabel");
        l->setAttribute(Qt::WA_TransparentForMouseEvents);
        l->hide();
        return l;
    };
    m_labelOriginal = mkLabel(tr("Original"));
    m_labelCrt = mkLabel(tr("CRT"));
}

VideoWidget::~VideoWidget()
{
    if (m_curSample) gst_sample_unref(m_curSample);
    makeCurrent();
    m_renderer.destroy();
    doneCurrent();
}

void VideoWidget::initializeGL()
{
    QString err;
    if (!m_renderer.initialize(&err)) {
        m_glError = err;
        emit glFailed(err);
        return;
    }
    auto* f = context()->functions();
    const auto* ver = reinterpret_cast<const char*>(f->glGetString(GL_VERSION));
    const auto* ren = reinterpret_cast<const char*>(f->glGetString(GL_RENDERER));
    m_glInfo = QStringLiteral("%1 | %2").arg(QString::fromUtf8(ren ? ren : "?"), QString::fromUtf8(ver ? ver : "?"));
    // A software rasteriser: no GPU, or a virtual machine without 3D acceleration.
    m_softwareGl = isSoftwareRenderer(QString::fromUtf8(ren ? ren : ""));
}

void VideoWidget::resizeGL(int, int) {}

void VideoWidget::resizeEvent(QResizeEvent* e)
{
    GlSurfaceWidget::resizeEvent(e);
    updateCompareLabels();
}

bool VideoWidget::needsAnimation() const
{
    const CrtParams& p = m_params;
    if (momentsAnimating()) return true;
    // Effects off: nothing of the look is drawn, so nothing of it needs redrawing between frames.
    if (m_bypass && !m_compare) return false;
    // Without a graphics card every redraw is expensive: the look moves with the video's frames.
    if (m_softwareGl) return false;
    const bool animated = p.persistence > 0.f || p.noise > 0.f || p.flicker > 0.f || p.vhsJitter > 0.f || p.vhsTracking > 0.f ||
                          p.filmGrain > 0.f || p.gateWeave > 0.f || p.filmFlicker > 0.f || p.filmDamage > 0.f ||
                          p.vhsHeadSwitch > 0.f || p.vhsDropouts > 0.f || p.dotCrawl > 0.f || p.rainbow > 0.f ||
                          p.laserRot > 0.f || (p.scanType == 3 && p.scanStrength > 0.f);
    return m_hasFrame && !m_bypass && m_player->isPlaying() && animated;
}

void VideoWidget::setParams(const CrtParams& p) { m_params = p; update(); }
void VideoWidget::setBypass(bool on) { m_bypass = on; setSurfaceScale(on ? 1.0 : m_lookScale); updateCompareLabels(); update(); }
void VideoWidget::setCompare(bool on) { m_compare = on; updateCompareLabels(); update(); }
void VideoWidget::setSplitFraction(double f) { m_split = std::clamp(f, 0.02, 0.98); updateCompareLabels(); update(); }
void VideoWidget::setScaleMode(ScaleMode m) { m_mode = m; update(); }
void VideoWidget::setCrop(const CropFractions& c) { m_crop = clampCrop(c); update(); }
void VideoWidget::setAspectOverride(double a) { m_aspectOverride = a > 0 ? a : 0; update(); emit sourceChanged(); }
void VideoWidget::setBottomInset(int px) { m_bottomInset = std::max(0, px); updateCompareLabels(); update(); }

void VideoWidget::setOrientationTag(const QString& tag)
{
    const Orientation o = orientationFromTag(tag);
    if (o == m_orient) return;
    m_orient = o;
    m_orientDirty = true;
    m_source.orient = o;
    emit sourceChanged();
    update();
}

void VideoWidget::clearFrame()
{
    if (m_curSample) { gst_sample_unref(m_curSample); m_curSample = nullptr; }
    m_uploaded = false;
    m_hasFrame = false;
    m_source = SourceFormat{};
    m_source.orient = m_orient;
    resetSyncStats();
    emit sourceChanged();
    update();
}

QSizeF VideoWidget::displaySizeNow() const { return displaySize(m_source, m_aspectOverride); }

QRectF VideoWidget::videoAreaPx() const
{
    const qreal dpr = devicePixelRatioF();
    return QRectF(0, 0, width() * dpr, std::max(1.0, (height() - m_bottomInset) * dpr));
}

LayoutResult VideoWidget::currentLayout() const
{
    return computeLayout(displaySizeNow(), m_mode, m_crop, videoAreaPx());
}

double VideoWidget::splitX() const { return width() * m_split; }

ViewSettings VideoWidget::viewSettings() const
{
    ViewSettings vs;
    vs.params = m_params;
    vs.bypass = m_bypass;
    vs.compare = m_compare;
    vs.split = m_split;
    vs.mode = m_mode;
    vs.crop = m_crop;
    vs.aspectOverride = m_aspectOverride;
    return vs;
}

double VideoWidget::effectClock() const
{
    return m_clockOverride >= 0 ? m_clockOverride : m_clock.elapsed() / 1000.0;
}

CrtRenderer::DrawParams VideoWidget::makeDrawParams(const QSize& viewport, const QRectF& area) const
{
    return CrtRenderer::makeDrawParams(viewSettings(), m_source, viewport, area, float(effectClock()), m_frameCounter);
}

QString VideoWidget::momentName() const
{
    if (m_staticHold || m_snow || m_staticRelease >= 0) return QStringLiteral("static");
    if (m_moment == 1) return QStringLiteral("power-on");
    if (m_moment == 2) return m_poweredOn ? QStringLiteral("power-off") : QStringLiteral("off");
    return QStringLiteral("none");
}

bool VideoWidget::momentsAnimating() const
{
    // Only what is actually changing: a running power-on/off, static, a timed OSD message
    // (a finished power-off is just a dark screen; "PAUSE" shown until resume is static).
    const double now = effectClock();
    if (m_moment == 1 || m_snow) return true;   // (snow keeps moving)
    if (m_moment == 2 && now - m_momentStart < 0.9) return true;
    if (m_staticHold || m_staticRelease >= 0) return true;
    return m_osdOn && m_osdUntil < 1e17;
}

void VideoWidget::startMoment(int type)
{
    if (type == 0) {   // end every moment: set on, no static
        m_moment = 0;
        m_poweredOn = true;
        m_staticHold = false;
        m_staticRelease = -1;
        update();
        return;
    }
    m_moment = type;
    m_momentStart = effectClock();
    m_poweredOn = type != 2;
    if (type == 2) m_offSerial = m_player->sampleSerial();
    update();
}

void VideoWidget::powerOff()
{
    if (!m_params.powerEffects) return;
    startMoment(2);
}

void VideoWidget::channelChange()
{
    m_staticHold = true;
    m_holdSerial = m_player->sampleSerial();
    m_staticRelease = -1;
    resetSyncStats();
    update();
}

void VideoWidget::setOsdText(const QString& text, double seconds)
{
    if (text.isEmpty()) { m_osdOn = false; update(); return; }
    // Chunky white VCR lettering with a dark outline, on a transparent background.
    QImage img(640, 96, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f;
    f.setFamilies({"DejaVu Sans Mono", "Noto Sans Mono", "monospace"});
    f.setBold(true);
    f.setPixelSize(64);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 4);
    QPainterPath path;
    path.addText(12, 72, f, text);
    p.setPen(QPen(QColor(0, 0, 0, 230), 10, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(path);
    p.fillPath(path, QColor(245, 245, 245));
    p.end();
    m_osdImage = img;
    ++m_osdVersion;
    m_osdOn = true;
    m_osdUntil = seconds > 0 ? effectClock() + seconds : 1e18;
    update();
}

void VideoWidget::setOverlayImage(const QImage& img)
{
    m_overlayOn = !img.isNull();
    m_overlayImage = img;
    ++m_osdVersion;   // (also when it goes: the VCR text's image is uploaded again)
    update();
}

void VideoWidget::decorate(CrtRenderer::DrawParams& d)
{
    const double now = effectClock();
    const quint64 serial = m_player->sampleSerial();
    // Channel change: static holds until the next file delivers a frame, then fades.
    if (m_staticHold && serial != m_holdSerial && m_player->hasMedia()) {
        m_staticHold = false;
        m_staticRelease = now;
    }
    if (m_staticHold || m_snow) d.staticLevel = 1.f;
    else if (m_staticRelease >= 0) {
        const double k = (now - m_staticRelease) / 0.35;
        if (k >= 1.0 || k < 0.0) m_staticRelease = -1;
        else d.staticLevel = float(1.0 - k);
    }
    // Power on after being off (first picture, or after the playlist ended).
    if (m_params.powerEffects && !m_poweredOn && m_hasFrame && m_player->isPlaying() &&
        (m_moment == 0 || serial != m_offSerial)) startMoment(1);
    if (!m_params.powerEffects && m_moment != 0) { m_moment = 0; m_poweredOn = true; }
    if (m_moment == 1) {
        const double t = (now - m_momentStart) / 0.75;
        if (t >= 1.0 || t < 0.0) m_moment = 0;
        else { d.transType = 1; d.transT = float(t); }
    } else if (m_moment == 2) {
        d.transType = 2;
        d.transT = float(std::clamp((now - m_momentStart) / 0.9, 0.0, 1.0));   // stays dark at the end
    }
    // VCR text in the picture's top-left corner, sized relative to the picture height.
    if (m_osdOn && now > m_osdUntil) m_osdOn = false;
    if (m_overlayOn) {   // Cable TV's overlay covers the whole picture
        d.osdAlpha = 1.f;
        d.osdRect = QRectF(0, 0, 1, 1);
    } else if (m_osdOn && m_params.vcrOsd && !m_osdImage.isNull()) {
        const QSizeF ds = displaySizeNow();
        const double h = 0.075;
        const double w = h * (double(m_osdImage.width()) / m_osdImage.height()) * (ds.height() / std::max(1.0, ds.width()));
        d.osdAlpha = 1.f;
        d.osdRect = QRectF(0.06, 0.07, w, h);
    }
}

double VideoWidget::currentScanlines() const
{
    const qreal dpr = devicePixelRatioF();
    return makeDrawParams(QSize(int(width() * dpr), int(height() * dpr)), videoAreaPx()).lines;
}

// Uploads the newest decoded frame, if there is one that hasn't been shown. (GL context current.)
bool VideoWidget::uploadCurrent()
{
    if (!m_curSample) return false;
    if (m_uploaded) return true;
    m_uploaded = m_renderer.uploadSample(m_curSample);
    return m_uploaded;
}

void VideoWidget::takeNewFrame(bool forGl)
{
    quint64 serial = 0;
    GstSample* s = m_player->latestSample(&serial);
    if (s && serial == m_shownSerial && m_hasFrame) { gst_sample_unref(s); s = nullptr; }   // nothing new
    if (!s) {
        if (forGl) uploadCurrent();   // (a frame painted plainly so far is now needed by the renderer)
        return;
    }
    m_shownSerial = serial;
    qint64 lateNs = 0;
    const bool measure = m_player->isPlaying() && !m_player->isSeeking() && m_player->frameLateness(s, &lateNs);
    if (m_curSample) gst_sample_unref(m_curSample);
    m_curSample = s;   // (keeps the reference)
    m_uploaded = false;
    // What was delivered: its size, and whether it is 32-bit RGB.
    GstVideoInfo vi;
    GstCaps* caps = gst_sample_get_caps(s);
    const bool known = caps && gst_video_info_from_caps(&vi, caps);
    m_curSize = known ? QSize(GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi)) : QSize();
    m_curRgb = known && (GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_BGRx || GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_BGRA);
    if (forGl && !uploadCurrent()) return;   // (otherwise it is uploaded when something draws with OpenGL)
    if (!known) return;
    // The video as decoded describes the picture (its size, pixel shape and format),
    // whatever size and format the frames are delivered in.
    SourceFormat sf;
    sf.width = GST_VIDEO_INFO_WIDTH(&vi);
    sf.height = GST_VIDEO_INFO_HEIGHT(&vi);
    sf.parN = GST_VIDEO_INFO_PAR_N(&vi) > 0 ? GST_VIDEO_INFO_PAR_N(&vi) : 1;
    sf.parD = GST_VIDEO_INFO_PAR_D(&vi) > 0 ? GST_VIDEO_INFO_PAR_D(&vi) : 1;
    QString pf = QString::fromUtf8(gst_video_format_to_string(GST_VIDEO_INFO_FORMAT(&vi)));
    {
        int nw = 0, nh = 0, pn = 1, pd = 1;
        QString nf;
        if (m_player->nativeFormat(&nw, &nh, &pn, &pd, &nf)) {
            // Converted for the screen: RGB delivered for a video that is not RGB itself, or another size.
            const GstVideoFormatInfo* ni = gst_video_format_get_info(gst_video_format_from_string(nf.toUtf8().constData()));
            m_curConverted = (m_curRgb && ni && !GST_VIDEO_FORMAT_INFO_IS_RGB(ni)) || nw != sf.width || nh != sf.height;
            sf.width = nw; sf.height = nh; sf.parN = pn; sf.parD = pd; pf = nf;
        } else {
            m_curConverted = false;
        }
    }
    if (!m_hasFrame || sf.width != m_source.width || sf.height != m_source.height ||
        sf.parN * m_source.parD != m_source.parN * sf.parD || pf != m_pixFmt) {
        m_source.width = sf.width; m_source.height = sf.height;
        m_source.parN = sf.parN; m_source.parD = sf.parD;
        m_pixFmt = pf;
        gchar* cs = gst_video_colorimetry_to_string(&vi.colorimetry);
        m_colorimetry = cs ? QString::fromUtf8(cs) : QStringLiteral("unknown");
        g_free(cs);
        m_hasFrame = true;
        emit sourceChanged();
    }
    m_hasFrame = true;
    ++m_framesPresented;
    if (m_intervalTimer.isValid()) { m_intervalSum += m_intervalTimer.nsecsElapsed() / 1e6; ++m_intervalN; }
    m_intervalTimer.start();
    if (measure) {
        const double ms = lateNs / 1e6;
        ++m_syncN;
        const double delta = ms - m_syncMean;
        m_syncMean += delta / m_syncN;
        m_syncM2 += delta * (ms - m_syncMean);
        m_syncMaxAbs = std::max(m_syncMaxAbs, std::abs(ms));
        if (std::abs(ms) <= 20.0) ++m_syncWithin;
    }
}

// The frame on screen was delivered converted or scaled; the same frame as decoded takes its
// place (the picture stays the same picture). False when the player no longer has it.
bool VideoWidget::showNativeFrame()
{
    if (!m_curSample || !m_curConverted) return m_curSample != nullptr;
    GstSample* n = m_player->nativeSample(m_curSample);
    if (!n) return false;
    GstVideoInfo vi;
    GstCaps* caps = gst_sample_get_caps(n);
    if (!caps || !gst_video_info_from_caps(&vi, caps)) { gst_sample_unref(n); return false; }
    gst_sample_unref(m_curSample);
    m_curSample = n;
    m_uploaded = false;
    m_curSize = QSize(GST_VIDEO_INFO_WIDTH(&vi), GST_VIDEO_INFO_HEIGHT(&vi));
    m_curRgb = GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_BGRx || GST_VIDEO_INFO_FORMAT(&vi) == GST_VIDEO_FORMAT_BGRA;
    m_curConverted = false;
    update();
    return true;
}

// Effects off without a graphics card: the frame, already RGB and at the size it is shown,
// is painted straight into the window. No OpenGL is involved at all.
bool VideoWidget::plainPaintable() const
{
    return m_bypass && !m_compare && m_orient == Orientation();
}

bool VideoWidget::paintPlain(QPainter& p)
{
    if (!plainPaintable()) return false;
    QElapsedTimer paintTimer;
    paintTimer.start();
    takeNewFrame(false);
    if (m_hasFrame && (!m_curSample || !m_curRgb)) return false;   // not a frame that can be painted as it is
    ++m_frameCounter;
    const qreal dpr = devicePixelRatioF();
    {   // (the set's moments still move on: static ends when a frame arrives, and so on)
        CrtRenderer::DrawParams d = makeDrawParams(QSize(int(width() * dpr), int(height() * dpr)), videoAreaPx());
        decorate(d);
    }
    p.fillRect(rect(), Qt::black);
    const LayoutResult L = currentLayout();
    if (m_hasFrame && L.valid) {
        GstBuffer* buf = gst_sample_get_buffer(m_curSample);
        GstVideoInfo vi;
        GstVideoFrame frame;
        if (buf && gst_video_info_from_caps(&vi, gst_sample_get_caps(m_curSample)) && gst_video_frame_map(&frame, &vi, buf, GST_MAP_READ)) {
            const int w = GST_VIDEO_FRAME_WIDTH(&frame), h = GST_VIDEO_FRAME_HEIGHT(&frame);
            const QImage img(static_cast<const uchar*>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)), w, h, GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0),
                             QImage::Format_RGB32);
            // The visible part of the picture, in widget coordinates; the matching part of the frame.
            const QRectF target(L.visibleRect.x() / dpr, L.visibleRect.y() / dpr, L.visibleRect.width() / dpr, L.visibleRect.height() / dpr);
            const QRectF source(L.srcRect.x() * w, L.srcRect.y() * h, L.srcRect.width() * w, L.srcRect.height() * h);
            const bool exact = qAbs(source.width() - L.visibleRect.width()) < 1.0 && qAbs(source.height() - L.visibleRect.height()) < 1.0;
            p.setRenderHint(QPainter::SmoothPixmapTransform, !exact);
            if (exact && dpr == 1.0) p.drawImage(target.topLeft().toPoint(), img, source.toRect());   // a straight copy
            else p.drawImage(target, img, source);
            gst_video_frame_unmap(&frame);
            // The set's own overlay (Cable TV's channel number, banners and guide) over the whole picture.
            const QImage osd = osdImage();
            if (hasOverlay() && !osd.isNull()) {
                p.setCompositionMode(QPainter::CompositionMode_SourceOver);
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                p.setClipRect(target);
                p.drawImage(QRectF(L.imageRect.x() / dpr, L.imageRect.y() / dpr, L.imageRect.width() / dpr, L.imageRect.height() / dpr), osd);
                p.setClipping(false);
            }
        }
    }
    const double pms = paintTimer.nsecsElapsed() / 1e6;
    ++m_paintN;
    m_paintSum += pms;
    m_paintMax = std::max(m_paintMax, pms);
    return true;
}

void VideoWidget::paintGL()
{
    if (!m_renderer.isInitialized()) {
        auto* f = context()->functions();
        f->glClearColor(0.1f, 0.f, 0.f, 1.f);
        f->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    QElapsedTimer paintTimer;
    paintTimer.start();
    if (m_orientDirty) {
        m_renderer.setOrientation(m_orient);
        m_orientDirty = false;
    }
    takeNewFrame();
    ++m_frameCounter;
    // (Without a graphics card the look may be drawn smaller than the widget and enlarged.)
    const QSize vp = surfacePixelSize();
    QRectF area = videoAreaPx();
    if (const double k = surfaceScale(); k < 1.0) area = QRectF(area.x() * k, area.y() * k, area.width() * k, area.height() * k);
    CrtRenderer::DrawParams d = makeDrawParams(vp, area);
    decorate(d);
    if (m_osdUploaded != m_osdVersion && !osdImage().isNull()) { m_renderer.setOsdImage(osdImage()); m_osdUploaded = m_osdVersion; }
    if (!m_hasFrame) d.image = QRectF();
    m_renderer.draw(defaultFramebufferObject(), d);
    if (needsAnimation() && !m_animTimer.isActive()) m_animTimer.start();
    const double pms = paintTimer.nsecsElapsed() / 1e6;
    ++m_paintN;
    m_paintSum += pms;
    m_paintMax = std::max(m_paintMax, pms);
}

QImage VideoWidget::grabOriginalFrame()
{
    if (!m_hasFrame) return {};
    const QSizeF ds = displaySize(m_source, 0.0);   // original: stream aspect, no override
    makeCurrent();
    uploadCurrent();
    QImage img = m_renderer.renderOriginal(QSize(qRound(ds.width()), qRound(ds.height())));
    doneCurrent();
    return img;
}

QImage VideoWidget::grabFilteredFrame()
{
    if (!m_hasFrame) return {};
    const qreal dpr = devicePixelRatioF();
    const QRectF area = videoAreaPx();
    const QSize vp(int(area.width()), int(area.height()));
    makeCurrent();
    uploadCurrent();
    CrtRenderer::DrawParams dp = makeDrawParams(vp, QRectF(QPointF(0, 0), area.size()));
    decorate(dp);
    QImage img = m_renderer.renderToImage(dp);
    doneCurrent();
    img.setDevicePixelRatio(1.0);
    Q_UNUSED(dpr);
    return img;
}

QSize VideoWidget::pictureSizeIn(const QSize& box) const
{
    const LayoutResult L = currentLayout();
    if (!L.valid || L.visibleRect.height() < 1) return {};
    const double aspect = L.visibleRect.width() / L.visibleRect.height();
    double w = box.width(), h = w / aspect;
    if (box.height() > 0 && h > box.height()) { h = box.height(); w = h * aspect; }
    return QSize(std::max(2, int(std::lround(w)) & ~1), std::max(2, int(std::lround(h)) & ~1));
}

QImage VideoWidget::renderPictureAt(const QSize& size, bool filtered, double clock)
{
    if (!m_renderer.isInitialized() || size.isEmpty()) return {};
    makeCurrent();
    if (m_orientDirty) { m_renderer.setOrientation(m_orient); m_orientDirty = false; }
    takeNewFrame();
    QImage img;
    if (m_hasFrame && !filtered) {
        img = m_renderer.renderOriginal(size);
    } else if (m_hasFrame) {
        // The picture alone, filling `size`: the same look as on screen, drawn for this size.
        CrtRenderer::DrawParams dp = CrtRenderer::makeDrawParams(viewSettings(), m_source, size, QRectF(QPointF(0, 0), QSizeF(size)),
                                                                 float(clock >= 0 ? clock : effectClock()), m_frameCounter);
        dp.split = -1;
        decorate(dp);
        if (m_osdUploaded != m_osdVersion && !osdImage().isNull()) { m_renderer.setOsdImage(osdImage()); m_osdUploaded = m_osdVersion; }
        ++m_frameCounter;
        img = m_renderer.renderToImage(dp);
    }
    doneCurrent();
    img.setDevicePixelRatio(1.0);
    return img;
}

SyncStats VideoWidget::syncStats() const
{
    SyncStats s;
    s.frames = m_syncN;
    s.meanMs = m_syncMean;
    s.stddevMs = m_syncN > 1 ? std::sqrt(m_syncM2 / (m_syncN - 1)) : 0.0;
    s.maxAbsMs = m_syncMaxAbs;
    s.within20ms = m_syncWithin;
    const double secs = m_syncTimer.elapsed() / 1000.0;
    s.presentedFps = secs > 0.2 ? m_syncN / secs : 0.0;
    s.paintMsAvg = m_paintN ? m_paintSum / m_paintN : 0.0;
    s.paintMsMax = m_paintMax;
    return s;
}

QSize VideoWidget::fastTargetSize() const
{
    const LayoutResult L = currentLayout();
    const QSizeF disp = displaySizeNow();
    if (!L.valid || disp.isEmpty() || !m_source.isValid()) return {};
    // The whole picture at exactly the size it is drawn, so that putting it on screen is a
    // straight copy; never larger than the video itself.
    const double k = std::min(1.0, std::max(L.imageRect.width() / disp.width(), L.imageRect.height() / disp.height()));
    if (k >= 1.0) return QSize(std::max(16, int(std::lround(disp.width()))), std::max(16, int(std::lround(disp.height()))));
    return QSize(std::max(16, int(std::lround(L.imageRect.width()))), std::max(16, int(std::lround(L.imageRect.height()))));
}

QSize VideoWidget::lookTargetSize() const
{
    const LayoutResult L = currentLayout();
    if (!L.valid || !m_source.isValid()) return {};
    // (compared in stored pixels: an anamorphic video at about its own size is left exactly as it is)
    const double kx = L.imageRect.width() / m_source.width, ky = L.imageRect.height() / m_source.height;
    if (std::max(kx, ky) > 0.625) return {};   // the video is under 1.6 times its picture
    return QSize(std::max(16, int(std::lround(L.imageRect.width()))), std::max(16, int(std::lround(L.imageRect.height()))));
}

void VideoWidget::setLookScale(double s)
{
    s = std::clamp(s, 0.25, 1.0);
    if (qFuzzyCompare(s, m_lookScale)) return;
    m_lookScale = s;
    setSurfaceScale(m_bypass ? 1.0 : s);
    update();
}

bool VideoWidget::frameIsScaled() const
{
    if (!m_hasFrame || !m_source.isValid()) return false;
    return m_curSample && m_curConverted;
}

void VideoWidget::pullFrame()
{
    if (!m_renderer.isInitialized()) return;
    makeCurrent();
    takeNewFrame();
    doneCurrent();
    update();
}

void VideoWidget::setProfiling(bool on)
{
    makeCurrent();
    m_renderer.setProfiling(on);
    doneCurrent();
    static bool connected = false;
    if (on && !connected) {
        connected = true;
        connect(this, &GlSurfaceWidget::aboutToCompose, this, [this] { m_composeTimer.start(); });
        connect(this, &GlSurfaceWidget::frameSwapped, this, [this] {
            if (m_composeTimer.isValid()) { m_composeSum += m_composeTimer.nsecsElapsed() / 1e6; ++m_composeN; }
        });
    }
}

// CPU time this process has used so far, on all its threads, in seconds.
static double processCpuSeconds()
{
#ifdef Q_OS_WIN
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    auto secs = [](const FILETIME& f) { return double((quint64(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e7; };
    return secs(k) + secs(u);
#else
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) != 0) return 0;
    return double(ts.tv_sec) + double(ts.tv_nsec) / 1e9;
#endif
}

QJsonObject VideoWidget::profileReport() const
{
    // How many CPU cores' worth of work the whole player did (decoding, converting, scaling, drawing).
    const double wall = m_cpuWall.isValid() ? m_cpuWall.nsecsElapsed() / 1e9 : 0;
    const double busy = wall > 0.2 ? (processCpuSeconds() - m_cpuStart) / wall : 0;
    const CrtRenderer::Profile p = m_renderer.profile();
    const double f = std::max(1, p.frames), dr = std::max(1, p.draws);
    return QJsonObject{{"frames", p.frames}, {"draws", p.draws}, {"uploadMs", p.upload / f}, {"convertMs", p.convert / f}, {"mipmapMs", p.mipmap / f},
                       {"blurMs", p.blur / dr}, {"drawMs", p.draw / dr}, {"paintMs", m_paintN ? m_paintSum / m_paintN : 0.0},
                       {"composeMs", m_composeN ? m_composeSum / m_composeN : 0.0},
                       {"frameIntervalMs", m_intervalN ? m_intervalSum / m_intervalN : 0.0}, {"cpuCoresBusy", busy}};
}

void VideoWidget::resetSyncStats()
{
    m_syncN = 0; m_syncMean = 0; m_syncM2 = 0; m_syncMaxAbs = 0; m_syncWithin = 0;
    m_paintN = 0; m_paintSum = 0; m_paintMax = 0;
    m_composeSum = m_intervalSum = 0; m_composeN = m_intervalN = 0;
    m_intervalTimer.invalidate();
    m_renderer.resetProfile();
    m_cpuStart = processCpuSeconds();
    m_cpuWall.start();
    m_syncTimer.restart();
}

void VideoWidget::updateCompareLabels()
{
    const bool show = m_compare && !m_bypass;
    m_labelOriginal->setVisible(show);
    m_labelCrt->setVisible(show);
    if (!show) return;
    m_labelOriginal->adjustSize();
    m_labelCrt->adjustSize();
    const int x = int(splitX());
    const int y = 14;
    m_labelOriginal->move(std::max(6, x - m_labelOriginal->width() - 10), y);
    m_labelCrt->move(std::min(width() - m_labelCrt->width() - 6, x + 10), y);
}

void VideoWidget::mouseMoveEvent(QMouseEvent* e)
{
    emit mouseActivity();
    if (m_draggingSplit) {
        setSplitFraction(e->position().x() / std::max(1, width()));
        emit splitChanged(m_split);
    } else if (m_compare && !m_bypass && std::abs(e->position().x() - splitX()) < 12) {
        setCursor(Qt::SplitHCursor);
    } else if (cursor().shape() == Qt::SplitHCursor) {
        unsetCursor();
    }
    QWidget::mouseMoveEvent(e);
}

void VideoWidget::mousePressEvent(QMouseEvent* e)
{
    emit mouseActivity();
    if (e->button() == Qt::LeftButton && m_compare && !m_bypass && std::abs(e->position().x() - splitX()) < 12) {
        m_draggingSplit = true;
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* e)
{
    m_draggingSplit = false;
    QWidget::mouseReleaseEvent(e);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !(m_compare && std::abs(e->position().x() - splitX()) < 12))
        emit doubleClicked();
}
