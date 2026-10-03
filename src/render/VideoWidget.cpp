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

VideoWidget::VideoWidget(Player* player, QWidget* parent) : QOpenGLWidget(parent), m_player(player)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(320, 180);
    m_clock.start();
    m_syncTimer.start();
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
}

void VideoWidget::resizeGL(int, int) {}

void VideoWidget::resizeEvent(QResizeEvent* e)
{
    QOpenGLWidget::resizeEvent(e);
    updateCompareLabels();
}

bool VideoWidget::needsAnimation() const
{
    const CrtParams& p = m_params;
    if (momentsAnimating()) return true;
    const bool animated = p.persistence > 0.f || p.noise > 0.f || p.flicker > 0.f || p.vhsJitter > 0.f || p.vhsTracking > 0.f ||
                          p.filmGrain > 0.f || p.gateWeave > 0.f || p.filmFlicker > 0.f || p.filmDamage > 0.f ||
                          p.vhsHeadSwitch > 0.f || p.vhsDropouts > 0.f || p.dotCrawl > 0.f || p.rainbow > 0.f ||
                          p.laserRot > 0.f || (p.scanType == 3 && p.scanStrength > 0.f);
    return m_hasFrame && !m_bypass && m_player->isPlaying() && animated;
}

void VideoWidget::setParams(const CrtParams& p) { m_params = p; update(); }
void VideoWidget::setBypass(bool on) { m_bypass = on; updateCompareLabels(); update(); }
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
void VideoWidget::takeNewFrame()
{
    quint64 serial = 0;
    GstSample* s = m_player->latestSample(&serial);
    if (s && serial == m_shownSerial && m_hasFrame) { gst_sample_unref(s); s = nullptr; }   // nothing new
    if (s) {
        m_shownSerial = serial;
        qint64 lateNs = 0;
        const bool measure = m_player->isPlaying() && !m_player->isSeeking() && m_player->frameLateness(s, &lateNs);
        if (m_renderer.uploadSample(s)) {
            const SourceFormat sf = m_renderer.sourceFormat();
            const QString pf = m_renderer.pixelFormatName();
            if (!m_hasFrame || sf.width != m_source.width || sf.height != m_source.height ||
                sf.parN * m_source.parD != m_source.parN * sf.parD || pf != m_pixFmt) {
                m_source.width = sf.width; m_source.height = sf.height;
                m_source.parN = sf.parN; m_source.parD = sf.parD;
                m_pixFmt = pf;
                m_colorimetry = m_renderer.colorimetryName();
                m_hasFrame = true;
                emit sourceChanged();
            }
            m_hasFrame = true;
            ++m_framesPresented;
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
        gst_sample_unref(s);
    }
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
    const qreal dpr = devicePixelRatioF();
    const QSize vp(int(width() * dpr), int(height() * dpr));
    CrtRenderer::DrawParams d = makeDrawParams(vp, videoAreaPx());
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

void VideoWidget::resetSyncStats()
{
    m_syncN = 0; m_syncMean = 0; m_syncM2 = 0; m_syncMaxAbs = 0; m_syncWithin = 0;
    m_paintN = 0; m_paintSum = 0; m_paintMax = 0;
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
    QOpenGLWidget::mouseMoveEvent(e);
}

void VideoWidget::mousePressEvent(QMouseEvent* e)
{
    emit mouseActivity();
    if (e->button() == Qt::LeftButton && m_compare && !m_bypass && std::abs(e->position().x() - splitX()) < 12) {
        m_draggingSplit = true;
        e->accept();
        return;
    }
    QOpenGLWidget::mousePressEvent(e);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent* e)
{
    m_draggingSplit = false;
    QOpenGLWidget::mouseReleaseEvent(e);
}

void VideoWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && !(m_compare && std::abs(e->position().x() - splitX()) < 12))
        emit doubleClicked();
}
