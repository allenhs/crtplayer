#include <QFontMetrics>
#include <QRegularExpression>
#include <QPainterPath>
#include <QPainter>
#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include "DeskView.h"
#include <QOpenGLFramebufferObject>
#include "playback/Player.h"
#include "render/VideoWidget.h"

#include <QMouseEvent>
#include <QOpenGLContext>
#include <QWheelEvent>
#include <QtMath>

namespace {
double smootherstep(double x)
{
    x = std::clamp(x, 0.0, 1.0);
    return x * x * x * (x * (x * 6 - 15) + 10);
}
double smoothstepD(double a, double b, double x)
{
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}
} // namespace

DeskView::DeskView(Player* player, VideoWidget* flat, QWidget* parent)
    : QOpenGLWidget(parent), m_player(player), m_flat(flat)
{
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setAlphaBufferSize(8);      // transparent around the set
    fmt.setStencilBufferSize(8);    // shadows of your 3D models (each floor pixel darkened once)
    fmt.setDepthBufferSize(24);
    setFormat(fmt);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    m_clock.start();
    connect(&m_library, &ModelLibrary::loaded, this, qOverload<>(&QWidget::update));
    m_lastMove.start();
    m_ticker.setInterval(16);
    m_ticker.setTimerType(Qt::PreciseTimer);
    connect(&m_ticker, &QTimer::timeout, this, &DeskView::tick);
    connect(m_player, &Player::frameReady, this, [this] { update(); });
}

DeskView::~DeskView()
{
    makeCurrent();
    m_desk.destroy();
    m_crt.destroy();
    doneCurrent();
}

QString DeskView::phaseName() const
{
    switch (m_phase) {
    case Phase::Desk: return QStringLiteral("desk");
    case Phase::FlyingIn: return QStringLiteral("flying-in");
    case Phase::Full: return QStringLiteral("full");
    case Phase::FlyingOut: return QStringLiteral("flying-out");
    }
    return {};
}

void DeskView::initializeGL()
{
    QString err;
    m_glOk = m_crt.initialize(&err) && m_desk.initialize(&err);
    if (!m_glOk) m_glError = err;
}

void DeskView::clearFrame()
{
    m_hasFrame = false;
    m_source = SourceFormat{};
    m_source.orient = m_orient;
    update();
}

void DeskView::setClassicShape(bool classic) { m_classic = classic; update(); }
void DeskView::setCabinet(int cabinet) { m_cabinet = std::clamp(cabinet, 0, int(DeskRenderer::ArcadeCabinet)); update(); }
QString DeskView::sceneName(int scene)
{
    return scene == 1 ? QStringLiteral("desk") : scene == 2 ? QStringLiteral("wall") : scene == 3 ? QStringLiteral("theater")
         : scene == 4 ? QStringLiteral("cg") : QStringLiteral("desktop");
}

QString DeskView::cabinetName(int cabinet)
{
    switch (cabinet) {
    case DeskRenderer::FlatFaceCrt: return QStringLiteral("flat-crt");
    case DeskRenderer::FlatPanel: return QStringLiteral("flat-panel");
    case DeskRenderer::WoodConsole: return QStringLiteral("wood-console");
    case DeskRenderer::PvmMonitor: return QStringLiteral("pvm");
    case DeskRenderer::BeigeMonitor: return QStringLiteral("beige-monitor");
    case DeskRenderer::TheaterScreen: return QStringLiteral("theater-screen");
    case DeskRenderer::ArcadeCabinet: return QStringLiteral("arcade");
    default: return QStringLiteral("crt");
    }
}
void DeskView::setDeskPose(const DeskPose& p)
{
    m_pose = p;
    // The 90s CG room is an open world: all the way round the set.
    m_pose.yaw = m_sceneSet.scene == 4 ? std::remainder(m_pose.yaw, 360.0) : std::clamp(m_pose.yaw, -80.0, 80.0);
    m_pose.pitch = std::clamp(m_pose.pitch, -35.0, 45.0);
    m_pose.height = std::clamp(m_pose.height, 0.18, 0.92);
    m_pose.cx = std::clamp(m_pose.cx, 0.05, 0.95);
    m_pose.cy = std::clamp(m_pose.cy, 0.05, 0.95);
    if (m_sceneSet.scene == 3) {   // a seat in the audience: no walking around the screen
        m_pose.yaw = std::clamp(m_pose.yaw, -30.0, 30.0);
        keepTheaterSeat();
    }
    keepAboveFloor();
    update();
}

DeskView::DeskPose DeskView::theaterSeat()
{
    DeskPose p;
    p.yaw = 0; p.pitch = 4; p.height = 0.24; p.cx = 0.5; p.cy = 0.40;   // about row 10, with the wide lens
    return p;
}

void DeskView::setScene(const Scene& s)
{
    const bool enteringTheater = s.scene == 3 && m_sceneSet.scene != 3;
    m_sceneSet = s;
    m_desk.setFovY(s.scene == 3 ? 58.f : s.scene == 4 ? 34.f : 30.f);
    if (enteringTheater) setDeskPose(theaterSeat());
    keepTheaterSeat();
    keepAboveFloor();
    update();
}

void DeskView::finishTheaterMoves()
{
    m_curtain = m_curtainTarget ? 1.0 : 0.0;
    m_beam = (m_projecting && m_curtain > 0.3) ? 1.0 : 0.0;
    const float a = float(geometry().glassAspect) * 0.5f;
    m_mask = QVector4D(-a, a, 0.f, 1.f);
    m_maskInit = true;
    update();
}

void DeskView::setTheaterState(bool curtainsOpen, bool projecting)
{
    // 90s CG room: starting a video "renders" the picture tile by tile, and so does resuming
    // after playback was stopped a while. A seek only interrupts playback for a moment, so
    // skipping back and forth never does, nor does dragging the timeline.
    if (projecting && !m_projecting) {
        const bool longStop = m_stoppedFor.isValid() && m_stoppedFor.elapsed() >= kRevealAfterPauseMs;
        if (m_sceneSet.scene == 4 && m_sceneSet.cgReveal && !m_scrubbing && (m_freshMedia || longStop)) m_reveal = 0.0;
        m_freshMedia = false;
    }
    if (!projecting && m_projecting) { m_stoppedFor.restart(); m_idleFor.restart(); }
    m_curtainTarget = curtainsOpen;
    m_projecting = projecting;
    update();
}

bool DeskView::animateTheater()
{
    // Real time even on a slow renderer (a 0.25 s cap keeps a stall from jumping).
    const double dt = m_theaterClock.isValid() ? std::min(0.25, m_theaterClock.restart() / 1000.0) : 0.0;
    if (!m_theaterClock.isValid()) m_theaterClock.start();
    bool moving = false;
    // curtains: ~3 s to open or close, eased
    const double ct = m_curtainTarget ? 1.0 : 0.0;
    if (std::abs(m_curtain - ct) > 1e-3) { m_curtain += std::clamp(ct - m_curtain, -dt / 3.0, dt / 3.0); moving = true; }
    else m_curtain = ct;
    const double bt = (m_projecting && m_curtain > 0.3) ? 1.0 : 0.0;
    if (std::abs(m_beam - bt) > 1e-3) { m_beam += std::clamp(bt - m_beam, -dt * 1.5, dt * 1.5); moving = true; }
    else m_beam = bt;
    // masking glides to the picture's shape (in theater space the picture is x +-A/2, y 0..1)
    const float a = float(geometry().glassAspect) * 0.5f;
    const QVector4D target(-a, a, 0.f, 1.f);
    if (!m_maskInit) { m_mask = target; m_maskInit = true; }
    const QVector4D d = target - m_mask;
    if (d.length() > 1e-3f) { m_mask += d * float(std::min(1.0, dt * 2.5)); moving = true; }
    else m_mask = target;
    return moving;
}

void DeskView::syncFrameTextures()
{
    // Load the chosen pictures when the list changes (GL context is current: called from paintGL).
    if (m_sceneSet.framePaths == m_loadedFramePaths) return;
    m_loadedFramePaths = m_sceneSet.framePaths;
    QOpenGLFunctions* gl = context()->functions();
    for (int i = 0; i < 4; ++i) {
        if (m_frameTex[i]) { gl->glDeleteTextures(1, &m_frameTex[i]); m_frameTex[i] = 0; }
        const QString path = m_sceneSet.framePaths.value(i);
        if (path.isEmpty()) continue;
        QImage img(path);                          // missing or unreadable files are simply skipped
        if (img.isNull()) continue;
        if (img.width() > 1024 || img.height() > 1024) img = img.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        img = img.convertToFormat(QImage::Format_RGBA8888);
        m_frameAspect[i] = float(img.width()) / float(std::max(1, img.height()));
        gl->glGenTextures(1, &m_frameTex[i]);
        gl->glBindTexture(GL_TEXTURE_2D, m_frameTex[i]);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
        gl->glGenerateMipmap(GL_TEXTURE_2D);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
}

QString DeskView::marqueeTitle(const QString& raw)
{
    // "Oblivion.2013.1080p.BluRay.x264" -> "OBLIVION": words split on dots and underscores, and
    // everything from the year or the first quality / source tag onwards dropped.
    QString t = raw;
    t.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
    static const QRegularExpression cut(QStringLiteral(
        "(?i)[\\[(]?\\b((19|20)\\d\\d|\\d{3,4}p|4k|uhd|hdr|bluray|blu ray|bdrip|brrip|web ?dl|webrip|hdtv|dvdrip|x26[45]|h ?26[45]|hevc|xvid|remux|s\\d\\de\\d\\d)\\b.*$"));
    const QString shortened = QString(t).remove(cut).trimmed();
    if (!shortened.isEmpty()) t = shortened;
    t = t.simplified().toUpper();
    return t.isEmpty() ? QStringLiteral("CRT PLAYER") : t;
}

QImage DeskView::marqueeImage() const
{
    // A backlit sign's lettering: heavy italic capitals with a glow, a dark outline and a
    // gradient fill, in the art's colours. Premultiplied RGBA (composited over the art).
    const int W = 1024, H = 224;
    QImage img(W, H, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);
    const QString text = m_marqueeText.isEmpty() ? QStringLiteral("CRT PLAYER") : m_marqueeText;
    QFont f;
    f.setFamilies({QStringLiteral("Impact"), QStringLiteral("Anton"), QStringLiteral("Bebas Neue"), QStringLiteral("Oswald"),
                   QStringLiteral("DejaVu Sans Condensed"), QStringLiteral("Liberation Sans Narrow"), QStringLiteral("Sans Serif")});
    f.setWeight(QFont::Black);
    f.setItalic(true);
    int px = 150;
    QPainterPath path;
    for (; px >= 48; px -= 6) {
        f.setPixelSize(px);
        path = QPainterPath();
        path.addText(0, 0, f, text);
        if (path.boundingRect().width() <= W - 90) break;
    }
    if (path.boundingRect().width() > W - 90) {   // still too long: shorten
        const QFontMetrics fm(f);
        path = QPainterPath();
        path.addText(0, 0, f, fm.elidedText(text, Qt::ElideRight, W - 90));
    }
    const QRectF b = path.boundingRect();
    path.translate(W * 0.5 - b.center().x(), H * 0.5 - b.center().y());
    QColor glow, top, bottom;
    switch (m_arcadeArt) {
    case 1: glow = QColor(255, 120, 30); top = QColor(255, 245, 150); bottom = QColor(255, 90, 40); break;
    case 2: glow = QColor(255, 40, 200); top = QColor(255, 255, 255); bottom = QColor(255, 80, 220); break;
    case 3: glow = QColor(255, 170, 60); top = QColor(255, 240, 200); bottom = QColor(240, 140, 30); break;
    default: glow = QColor(60, 170, 255); top = QColor(255, 255, 255); bottom = QColor(90, 220, 255); break;
    }
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    for (int i = 5; i >= 1; --i) {   // soft glow
        QColor g = glow;
        g.setAlpha(26);
        p.strokePath(path, QPen(g, i * 7.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    }
    p.strokePath(path, QPen(QColor(10, 6, 20), 12.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));   // outline
    QLinearGradient grad(0, b.top() + H * 0.5 - b.center().y(), 0, b.bottom() + H * 0.5 - b.center().y());
    grad.setColorAt(0.0, top);
    grad.setColorAt(0.55, top);
    grad.setColorAt(0.56, bottom);
    grad.setColorAt(1.0, bottom.darker(130));
    p.fillPath(path, grad);
    p.end();
    return img;
}

void DeskView::syncMarquee()
{
    // (GL context current: called from paintGL)
    const QString key = m_marqueeText + QChar(0x1f) + QString::number(m_arcadeArt);
    if (key == m_marqueeLoaded) return;
    m_marqueeLoaded = key;
    QOpenGLFunctions* gl = context()->functions();
    const QImage img = marqueeImage();
    if (!m_marqueeTex) gl->glGenTextures(1, &m_marqueeTex);
    gl->glBindTexture(GL_TEXTURE_2D, m_marqueeTex);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, img.width(), img.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
    gl->glGenerateMipmap(GL_TEXTURE_2D);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

double DeskView::cameraClearance()
{
    if (!m_desk.hasGeometry() || width() <= 0) return 0.0;
    return m_desk.cameraClearance(poseAt(0.0, size() * devicePixelRatioF(), geometry()),
                                  float(floorDrop()));
}

int DeskView::picturesLoaded() const
{
    int n = 0;
    for (unsigned t : m_frameTex) n += t ? 1 : 0;
    return n;
}

QImage DeskView::scenePreview(int scene, const QSize& size)
{
    const Scene saved = m_sceneSet;
    const DeskPose pose = m_pose;
    m_sceneSet.scene = scene;
    m_desk.setFovY(scene == 3 ? 58.f : scene == 4 ? 34.f : 30.f);
    if (scene == 3) { m_pose = theaterSeat(); keepTheaterSeat(); }   // the theater from an audience seat
    keepAboveFloor();
    QImage img = grabFramebuffer();
    m_sceneSet = saved;
    m_pose = pose;
    m_desk.setFovY(saved.scene == 3 ? 58.f : saved.scene == 4 ? 34.f : 30.f);
    update();
    if (scene == 0) {   // the desktop scene is transparent: show it over a neutral checkerboard
        QImage bg(img.size(), QImage::Format_ARGB32_Premultiplied);
        QPainter p(&bg);
        const int c = std::max(8, img.width() / 24);
        for (int y = 0; y < bg.height(); y += c)
            for (int x = 0; x < bg.width(); x += c)
                p.fillRect(x, y, c, c, ((x / c + y / c) % 2) ? QColor(58, 62, 70) : QColor(44, 47, 54));
        p.drawImage(0, 0, img);
        p.end();
        img = bg;
    }
    return img.scaled(size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation)
        .copy(0, 0, size.width(), size.height());
}

double DeskView::seatEye(double z)
{
    const double rows = std::clamp((z - kRow0Z) / kRowD, 0.0, double(kRows - 1));
    return kAudY + kRise + rows * kRise + kEye;
}

QVector3D DeskView::theaterCamera()
{
    if (!m_desk.hasGeometry() || width() <= 0) return {};
    return m_desk.cameraInScene(poseAt(0.0, size() * devicePixelRatioF(), geometry()));
}

void DeskView::keepTheaterSeat()
{
    if (m_sceneSet.scene != 3 || !m_desk.hasGeometry() || width() <= 0) return;
    const QSize vp = size() * devicePixelRatioF();
    const Geo g = geometry();
    const double wallLimit = (std::max(double(g.glassAspect), 2.39) + 0.30) * 0.5 + 0.9 - 0.25;
    // From row 3 back (the first rows would crane the neck); to the back row.
    const double frontZ = kRow0Z + 3.0 * kRowD, backZ = kRow0Z + (kRows - 1) * kRowD - 0.05;
    for (int i = 0; i < 120; ++i) {
        const QVector3D c = m_desk.cameraInScene(poseAt(0.0, vp, g));
        // Zoom moves you between the front and back rows; sideways stays inside the walls.
        if (c.z() < frontZ && m_pose.height > 0.06) { m_pose.height *= 0.97; continue; }
        if (c.z() > backZ && m_pose.height < 0.95) { m_pose.height *= 1.03; continue; }
        if (std::abs(c.x()) > wallLimit && std::abs(m_pose.yaw) > 0.3) { m_pose.yaw *= 0.95; continue; }
        // Your eye at seated height for the row you are in.
        const double err = seatEye(c.z()) - c.y();
        if (std::abs(err) < 0.002) break;
        const double dist = std::max(0.5, double(c.length()));
        m_pose.pitch = std::clamp(m_pose.pitch + qRadiansToDegrees(err / dist), -40.0, 40.0);
    }
}

void DeskView::keepAboveFloor()
{
    // Only where a scene shows a floor; on the transparent desktop the set may be seen from below.
    // The theater has its own rule: you sit in a row (keepTheaterSeat).
    if (m_sceneSet.scene == 0 || m_sceneSet.scene == 3 || !m_desk.hasGeometry() || width() <= 0) return;
    const QSize vp = size() * devicePixelRatioF();
    const Geo g = geometry();
    const double drop = floorDrop();
    for (int i = 0; i < 200 && m_pose.pitch < 45.0; ++i) {
        if (m_desk.cameraClearance(poseAt(0.0, vp, g), float(drop)) > 0.12f) break;   // a little above the boards
        m_pose.pitch = std::min(45.0, m_pose.pitch + 0.5);
    }
    // ... and inside the room: not through a side wall, not above the ceiling.
    if (m_sceneSet.scene == 4) return;   // the 90s CG room is an open world
    const float halfW = DeskRenderer::roomHalfWidth(m_sceneSet.scene) - 0.3f;
    const float ceil = m_desk.ceilingHeight(m_sceneSet.scene, float(drop)) - 0.3f;
    for (int i = 0; i < 200; ++i) {
        const QVector3D c = m_desk.cameraInScene(poseAt(0.0, vp, g));
        if (std::abs(c.x()) > halfW && std::abs(m_pose.yaw) > 0.3) { m_pose.yaw *= 0.97; continue; }
        if (c.y() + float(drop) > ceil && m_pose.pitch > 0.3) { m_pose.pitch *= 0.97; continue; }
        break;
    }
}
void DeskView::resetDeskPose() { setDeskPose(DeskPose{}); }

DeskView::Geo DeskView::geometry() const
{
    Geo g;
    const QSizeF ds = m_hasFrame ? displaySize(m_source, m_flat->aspectOverride()) : QSizeF(4, 3);
    const float a = float(ds.width() / std::max(1e-6, ds.height()));
    if (m_classic) {
        g.pivot = false;
        g.glassAspect = 4.f / 3.f;
    } else {
        g.pivot = a < 0.95f;
        // Extremely wide pictures are letterboxed inside a very wide (but plausible) tube.
        g.glassAspect = std::clamp(g.pivot ? 1.f / a : a, 1.f, 2.6f);
    }
    g.displayAspect = g.pivot ? 1.f / g.glassAspect : g.glassAspect;
    return g;
}

QRectF DeskView::fullGlassRect(const QSize& vp, const Geo& g) const
{
    // The glass, head-on, fitted to the whole window: where the flight ends.
    const double W = vp.width(), H = vp.height();
    double w = W, h = W / g.displayAspect;
    if (h > H) { h = H; w = H * g.displayAspect; }
    return QRectF((W - w) / 2, (H - h) / 2, w, h);
}

DeskRenderer::Pose DeskView::poseAt(double e, const QSize& vp, const Geo& g) const
{
    const float focal = float(vp.height() * 0.5 / std::tan(qDegreesToRadians(m_desk.fovY() * 0.5)));
    // Cabinet bounds after the optional roll.
    const QVector3D bmin = m_desk.boundsMin(), bmax = m_desk.boundsMax();
    const float cabH = g.pivot ? (bmax.x() - bmin.x()) : (bmax.y() - bmin.y());
    const QVector3D c = (bmin + bmax) * 0.5f;
    const QVector3D rolledCentre = g.pivot ? QVector3D(-c.y(), c.x(), c.z()) : c;

    // Desk pose: set centred at (cx, cy), `height` of the window tall.
    DeskRenderer::Pose d;
    d.pivot = g.pivot;
    d.yaw = float(m_pose.yaw);
    d.pitch = float(m_pose.pitch);
    d.pivotPoint = rolledCentre;
    d.distance = cabH * focal / float(std::max(1.0, m_pose.height * vp.height()));
    d.lensShift = QPointF(m_pose.cx * 2 - 1, 1 - m_pose.cy * 2);

    // Full pose: glass plane head-on, filling fullGlassRect exactly.
    const QRectF r = fullGlassRect(vp, g);
    DeskRenderer::Pose f;
    f.pivot = g.pivot;
    f.yaw = 0;
    f.pitch = 0;
    f.pivotPoint = QVector3D(0, 0, m_desk.glassRimZ());
    const float glassScreenH = g.pivot ? g.glassAspect : 1.f;
    f.distance = glassScreenH * focal / float(std::max(1.0, r.height()));
    f.lensShift = QPointF(0, 0);

    DeskRenderer::Pose p;
    p.pivot = g.pivot;
    p.yaw = float(d.yaw + (f.yaw - d.yaw) * e);
    p.pitch = float(d.pitch + (f.pitch - d.pitch) * e);
    if (m_sceneSet.scene == 4) {   // a sweeping 90s camera move: an arc that vanishes at both ends
        const double arc = std::sin(M_PI * e);
        p.yaw += float(40.0 * arc);
        p.pitch += float(12.0 * arc);
    }
    p.pivotPoint = d.pivotPoint + (f.pivotPoint - d.pivotPoint) * float(e);
    p.distance = float(std::exp(std::log(d.distance) + (std::log(f.distance) - std::log(d.distance)) * e));
    p.lensShift = d.lensShift + (f.lensShift - d.lensShift) * e;
    return p;
}

void DeskView::paintGL()
{
    ++m_paints;
    auto* gl = context()->extraFunctions();
    if (!m_glOk) {
        gl->glClearColor(0.2f, 0.f, 0.f, 0.6f);
        gl->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    // Presentation settings always come from the flat view.
    const ViewSettings vs = m_flat->viewSettings();
    const Orientation o = m_flat->orientation();
    if (o != m_orient) { m_orient = o; m_crt.setOrientation(o); m_source.orient = o; }

    quint64 serial = 0;
    GstSample* s = m_player->latestSample(&serial);
    if (s && (serial != m_shownSerial || !m_hasFrame)) {
        m_shownSerial = serial;
        if (m_crt.uploadSample(s)) {
            const SourceFormat sf = m_crt.sourceFormat();
            m_source.width = sf.width; m_source.height = sf.height;
            m_source.parN = sf.parN; m_source.parD = sf.parD;
            m_hasFrame = true;
        }
    }
    if (s) gst_sample_unref(s);
    ++m_frameCounter;

    // (A scene grab renders the same view larger, into its own framebuffer.)
    const qreal dpr = devicePixelRatioF() * m_grabScale;
    const GLuint target = m_grabFbo ? m_grabFbo : defaultFramebufferObject();
    const QSize vp(int(width() * dpr), int(height() * dpr));
    const float time = float(m_flat->effectTime());   // the same clock as the regular view
    const Geo g = geometry();
    m_pivot = g.pivot;
    m_desk.setLayout(g.glassAspect, g.pivot, effectiveCabinet(), m_sceneSet.scene == 2 && effectiveCabinet() != DeskRenderer::ArcadeCabinet);
    m_desk.setFovY(m_sceneSet.scene == 3 ? 58.f : m_sceneSet.scene == 4 ? 34.f : 30.f);
    keepTheaterSeat();  // the window or the video's shape may have moved the camera
    keepAboveFloor();   // the set or scene may have changed where the floor is

    // The flat image the flight ends in: identical to the regular fullscreen player
    // ("follow video"), or the picture inside a head-on 4:3 tube ("classic").
    const QRectF fullArea = m_classic ? fullGlassRect(vp, g) : QRectF(0, 0, vp.width(), vp.height());
    CrtRenderer::DrawParams flat = CrtRenderer::makeDrawParams(vs, m_source, vp, fullArea, time, m_frameCounter);
    m_flat->decorate(flat);   // set moments and VCR text come from the regular view
    if (m_osdUploaded != m_flat->osdVersion() && !m_flat->osdImage().isNull()) {
        m_crt.setOsdImage(m_flat->osdImage());
        m_osdUploaded = m_flat->osdVersion();
    }
    if (!m_hasFrame) flat.image = QRectF();

    if (m_phase == Phase::Full) {
        m_crt.draw(target, flat);
        return;
    }

    const double e = smootherstep(m_t);
    const DeskRenderer::Pose pose = poseAt(e, vp, g);

    // CRT output rendered to a texture shaped like the glass; curvature comes from the
    // real geometry here, so the flat bulge and corners are turned off.
    const QRectF gr = m_desk.glassRect(pose, vp);
    // (in whole 64s; a glass with no sensible size, as for a moment while the window is being set up, gets the smallest)
    const double wantH = std::ceil(std::max(gr.height(), gr.width() / g.displayAspect) / 64.0) * 64.0;
    const int texH = std::isfinite(wantH) ? int(std::clamp(wantH, 256.0, 2160.0)) : 256;
    const QSize texSize(std::clamp(int(texH * g.displayAspect), 64, 4096), texH);
    m_glassSize = texSize;
    ViewSettings gvs = vs;
    gvs.params.curvature = 0.f;
    gvs.params.cornerRadius = 0.f;
    gvs.params.includeBars = false;
    gvs.compare = false;
    CrtRenderer::DrawParams gp = CrtRenderer::makeDrawParams(gvs, m_source, texSize, QRectF(QPointF(0, 0), QSizeF(texSize)),
                                                             time, m_frameCounter);
    m_flat->decorate(gp);
    DeskRenderer::Frame f;
    f.viewport = vp;
    f.pose = pose;
    f.clearAlpha = float(smoothstepD(0.15, 0.85, e));
    f.scene = m_sceneSet.scene;
    f.mood = m_sceneSet.mood;
    f.wood = m_sceneSet.wood;
    f.fog = m_sceneSet.fog == 0 ? 0.f : float((m_sceneSet.fog == 1 ? 0.35 : 0.9) * m_sceneSet.fogStrength);
    f.fogSteps = m_sceneSet.quality == 0 ? 8 : m_sceneSet.quality == 1 ? 16 : 32;
    f.glassSize = m_glassSize;
    f.wallStyle = m_sceneSet.wallStyle;
    f.frameStyle = m_sceneSet.frameStyle;
    f.frameLayout = m_sceneSet.frameLayout;
    f.floorDrop = float(floorDrop());
    if (m_sceneSet.scene == 3) {
        animateTheater();
        f.curtain = float(m_curtain);
        f.house = float(0.08 + 0.92 * (1.0 - m_curtain));   // house lights dim as the curtains open
        f.mask = m_mask;
        f.beam = float(m_beam);
        f.marchReference = m_marchRef;
    }
    f.picHeight = float(m_sceneSet.picHeight);
    f.picSpacing = float(m_sceneSet.picSpacing);
    f.picSize = float(m_sceneSet.picSize);
    f.cgPalette = m_sceneSet.cgPalette; f.cgFloor = m_sceneSet.cgFloor; f.cgStand = m_sceneSet.cgStand;
    f.cgObjects = m_sceneSet.cgObjects; f.cgBanding = m_sceneSet.cgBanding; f.cgObjectSet = m_sceneSet.cgObjectSet; f.cgBackground = m_sceneSet.cgBackground;
    // Your 3D models: load the folder in the background; upload a new set once it is ready.
    if (m_sceneSet.scene == 4) {
        const QString folder = m_sceneSet.models ? m_sceneSet.modelsFolder : QString();
        const auto up = ModelLibrary::Up(std::clamp(m_sceneSet.modelsUp, 0, 6));
        m_library.setLight(m_flat->softwareRenderer());
        m_library.setFolder(folder, up);   // (nothing happens when nothing has changed)
        int gen = 0;
        const auto meshes = m_library.meshes(&gen);
        if (gen != m_modelsUploaded) { m_desk.setModels(meshes); m_modelsUploaded = gen; }
    }
    f.models = m_sceneSet.scene == 4 && m_sceneSet.models && !m_sceneSet.modelsFolder.isEmpty();
    f.modelFinish = m_sceneSet.modelFinish;
    // Which side is a model's front cannot be told from its file: by default the statues turn slowly (40 s a turn).
    f.modelTurn = m_sceneSet.modelsFace == 0 ? float(std::fmod(double(time) * 9.0, 360.0)) : float((std::clamp(m_sceneSet.modelsFace, 1, 4) - 1) * 90);
    f.modelSpread = m_sceneSet.modelsFace == 0 ? 60.f : 0.f;
    if (m_sceneSet.scene == 4) {
        const double dt = m_cgClock.isValid() ? std::min(0.25, m_cgClock.restart() / 1000.0) : 0.0;
        if (!m_cgClock.isValid()) m_cgClock.start();
        if (m_reveal < 1.0) m_reveal = std::min(1.0, m_reveal + dt / 2.5);   // ~2.5 s to "render"
        // Demo reel: once nothing has played (and nobody has sought) for 10 s, the camera slowly
        // orbits the set. Never while scrubbing, skipping or just briefly paused.
        if (!m_idleFor.isValid()) m_idleFor.start();
        if (m_sceneSet.cgOrbit && !m_projecting && !m_scrubbing && m_idleFor.elapsed() >= kRevealAfterPauseMs &&
            m_phase == Phase::Desk && m_drag == Drag::None)
            m_pose.yaw = std::remainder(m_pose.yaw + dt * 8.0, 360.0);
    }
    f.reveal = m_sceneSet.scene == 4 ? float(m_reveal) : 1.f;
    syncFrameTextures();
    if (m_cabinet == DeskRenderer::ArcadeCabinet) syncMarquee();
    for (int i = 0; i < 4; ++i) { f.frameTex[i] = m_frameTex[i]; f.frameAspect[i] = m_frameAspect[i]; }
    f.arcadeArt = m_arcadeArt;
    f.marqueeTexture = m_cabinet == DeskRenderer::ArcadeCabinet ? m_marqueeTex : 0;
    if (m_hasFrame && !gp.image.isEmpty()) {
        f.glassTexture = m_crt.renderToTexture(gp);
        f.blurTexture = m_crt.blurTexture();
        f.picInGlass = QRectF(gp.image.x() / texSize.width(), gp.image.y() / texSize.height(),
                              gp.image.width() / texSize.width(), gp.image.height() / texSize.height());
    }
    // Tube sets have bulging glass (from Curvature); the flat-face CRT and the panel are flat.
    const int cab = effectiveCabinet();
    const bool flatGlass = cab == DeskRenderer::FlatFaceCrt || cab == DeskRenderer::FlatPanel || cab == DeskRenderer::TheaterScreen;
    f.glassBulge = flatGlass ? 0.f : 0.015f + vs.params.curvature * 0.30f;
    f.ledOn = m_player->hasMedia() ? 1.f : 0.25f;
    f.time = time;
    m_desk.draw(target, f);

    // Last stretch of the flight: dissolve into the exact flat image.
    const float xfade = float(smoothstepD(0.80, 1.0, e));
    if (xfade > 0.f && m_hasFrame) {
        flat.opacity = xfade;
        m_crt.draw(target, flat);
    }

    // Screen-space outline of the set: drives click-through and control placement.
    m_shadowVisible = m_desk.cameraAboveFloor(pose);   // same test the shader and the click outline use
    QPolygonF sil = m_desk.silhouette(pose, vp);
    for (QPointF& pt : sil) pt /= dpr;
    m_glassRect = QRectF(gr.topLeft() / dpr, gr.size() / dpr);
    if (sil != m_silhouette) {
        m_silhouette = sil;
        emit silhouetteChanged(m_silhouette);
    }
    if (needsTicks() && !m_ticker.isActive()) m_ticker.start();
}

QSizeF DeskView::pictureDisplaySize() const
{
    return m_hasFrame ? displaySize(m_source, m_flat->viewSettings().aspectOverride) : QSizeF();
}

QSize DeskView::sceneSizeIn(const QSize& box) const
{
    if (width() <= 0 || height() <= 0) return {};
    const double aspect = double(width()) / height();
    double w = box.width(), h = w / aspect;
    if (box.height() > 0 && h > box.height()) { h = box.height(); w = h * aspect; }
    return QSize(std::max(2, int(std::lround(w)) & ~1), std::max(2, int(std::lround(h)) & ~1));
}

QImage DeskView::grabScene(const QSize& size)
{
    if (!m_glOk || size.isEmpty() || width() <= 0) return {};
    makeCurrent();
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
    QImage img;
    {
        QOpenGLFramebufferObject fbo(size, fmt);
        m_grabFbo = fbo.handle();
        m_grabScale = double(size.width()) / (width() * devicePixelRatioF());
        paintGL();
        m_grabFbo = 0;
        m_grabScale = 1.0;
        img = fbo.toImage();
    }
    doneCurrent();
    update();   // the window's own picture again
    img.setDevicePixelRatio(1.0);
    return img;
}

bool DeskView::needsTicks() const
{
    if (m_phase == Phase::FlyingIn || m_phase == Phase::FlyingOut) return true;
    if (m_drag == Drag::None && (std::abs(m_velocity.x()) > 0.02 || std::abs(m_velocity.y()) > 0.02)) return true;
    if (m_flat->momentsAnimating()) return true;
    if (m_sceneSet.scene != 0 && m_sceneSet.fog != 0) return true;   // drifting fog
    if (m_sceneSet.scene == 4) return true;   // the 90s CG room: objects turn and bob, the camera may orbit
    if (m_sceneSet.scene == 3 && (m_beam > 0.0 || std::abs(m_curtain - (m_curtainTarget ? 1.0 : 0.0)) > 1e-3)) return true;   // curtains, beam dust
    if (m_sceneSet.scene == 3) {
        const float a = float(geometry().glassAspect) * 0.5f;
        if ((QVector4D(-a, a, 0.f, 1.f) - m_mask).length() > 1e-3f) return true;   // masking moving
    }
    const CrtParams p = m_flat->params();
    const bool animated = p.persistence > 0.f || p.noise > 0.f || p.flicker > 0.f || p.vhsJitter > 0.f || p.vhsTracking > 0.f ||
                          p.filmGrain > 0.f || p.gateWeave > 0.f || p.filmFlicker > 0.f || p.filmDamage > 0.f ||
                          p.vhsHeadSwitch > 0.f || p.vhsDropouts > 0.f || p.dotCrawl > 0.f || p.rainbow > 0.f ||
                          p.laserRot > 0.f || (p.scanType == 3 && p.scanStrength > 0.f);
    return m_player->isPlaying() && animated && !m_flat->bypass();
}

void DeskView::tick()
{
    if (m_phase == Phase::FlyingIn || m_phase == Phase::FlyingOut) {
        const double k = m_flightClock.elapsed() / kFlightMs;
        if (m_phase == Phase::FlyingIn) {
            m_t = std::min(1.0, m_flightFrom + k);
            if (m_t >= 1.0) { m_phase = Phase::Full; emit phaseChanged(m_phase); }
        } else {
            m_t = std::max(0.0, m_flightFrom - k);
            if (m_t <= 0.0) { m_phase = Phase::Desk; emit phaseChanged(m_phase); }
        }
    }
    if (m_drag == Drag::None && m_phase == Phase::Desk) {   // inertia after a flick
        if (std::abs(m_velocity.x()) > 0.02 || std::abs(m_velocity.y()) > 0.02) {
            DeskPose p = m_pose;
            p.yaw += m_velocity.x();
            p.pitch += m_velocity.y();
            setDeskPose(p);
            m_velocity *= 0.90;
        } else {
            m_velocity = QPointF();
        }
    }
    update();
    if (!needsTicks()) m_ticker.stop();
}

void DeskView::startFlight(Phase to)
{
    m_flightFrom = m_t;
    m_flightClock.start();
    m_phase = to;
    m_velocity = QPointF();
    emit phaseChanged(m_phase);
    m_ticker.start();
    update();
}

void DeskView::flyIn()
{
    if (m_phase == Phase::Full || m_phase == Phase::FlyingIn) return;
    startFlight(Phase::FlyingIn);
}

void DeskView::flyOut()
{
    if (m_phase == Phase::Desk || m_phase == Phase::FlyingOut) return;
    startFlight(Phase::FlyingOut);
}

void DeskView::toggleFly()
{
    if (m_phase == Phase::Desk || m_phase == Phase::FlyingOut) flyIn();
    else flyOut();
}

bool DeskView::insideTv(const QPointF& p) const
{
    return m_phase != Phase::Desk || m_silhouette.containsPoint(p, Qt::OddEvenFill);
}

void DeskView::mousePressEvent(QMouseEvent* e)
{
    emit mouseActivity();
    if (e->button() == Qt::RightButton) {
        emit contextMenuRequested(e->globalPosition().toPoint());
        return;
    }
    if (m_phase != Phase::Desk || !insideTv(e->position())) { e->ignore(); return; }
    const bool move = e->button() == Qt::MiddleButton ||
                      (e->button() == Qt::LeftButton && (e->modifiers() & (Qt::ShiftModifier | Qt::AltModifier)));
    m_drag = move ? Drag::Move : (e->button() == Qt::LeftButton ? Drag::Rotate : Drag::None);
    m_lastPos = e->position();
    m_velocity = QPointF();
    m_lastMove.restart();
    if (m_drag == Drag::Move) setCursor(Qt::ClosedHandCursor);
}

void DeskView::mouseMoveEvent(QMouseEvent* e)
{
    emit mouseActivity();
    if (m_drag == Drag::None) {
        if (m_phase == Phase::Desk) setCursor(insideTv(e->position()) ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    const QPointF d = e->position() - m_lastPos;
    m_lastPos = e->position();
    DeskPose p = m_pose;
    if (m_drag == Drag::Rotate) {
        p.yaw += d.x() * 0.35;
        p.pitch += d.y() * 0.25;
        const double dt = std::max<qint64>(1, m_lastMove.restart());
        m_velocity = QPointF(d.x() * 0.35, d.y() * 0.25) * (16.0 / dt);
    } else {
        p.cx += d.x() / std::max(1, width());
        p.cy += d.y() / std::max(1, height());
    }
    setDeskPose(p);
}

void DeskView::mouseReleaseEvent(QMouseEvent*)
{
    if (m_drag == Drag::Rotate && m_lastMove.elapsed() > 80) m_velocity = QPointF();   // held still: no flick
    m_drag = Drag::None;
    unsetCursor();
    if (needsTicks()) m_ticker.start();
}

void DeskView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && insideTv(e->position())) toggleFly();
}

void DeskView::wheelEvent(QWheelEvent* e)
{
    emit mouseActivity();
    if (m_phase != Phase::Desk || !insideTv(e->position())) { e->ignore(); return; }
    DeskPose p = m_pose;
    p.height *= std::pow(1.1, e->angleDelta().y() / 120.0);
    setDeskPose(p);
}
