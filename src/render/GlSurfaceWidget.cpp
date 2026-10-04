#include "GlSurfaceWidget.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLWidget>
#include <QPainter>
#include <QResizeEvent>
#include <algorithm>
#include <cstring>
#include <vector>

#ifndef GL_PACK_INVERT_MESA
#define GL_PACK_INVERT_MESA 0x8758
#endif

class GlSurfaceInner : public QOpenGLWidget {
public:
    GlSurfaceInner(GlSurfaceWidget* owner) : QOpenGLWidget(owner), m_owner(owner) {}
protected:
    void initializeGL() override { m_owner->initializeGL(); }
    void resizeGL(int w, int h) override { m_owner->resizeGL(w, h); }
    void paintGL() override { m_owner->paintGL(); }
private:
    GlSurfaceWidget* m_owner;
};

bool GlSurfaceWidget::isSoftwareRenderer(const QString& glRenderer)
{
    const QString r = glRenderer.toLower();
    return r.contains("llvmpipe") || r.contains("softpipe") || r.contains("swrast") || r.contains("swiftshader") ||
           r.contains("lavapipe") || r.contains("software rasterizer");
}

QString GlSurfaceWidget::probedRenderer()
{
    static QString renderer;
    static bool done = false;
    if (done) return renderer;
    done = true;
    QOffscreenSurface surface;
    surface.setFormat(QSurfaceFormat::defaultFormat());
    surface.create();
    QOpenGLContext ctx;
    ctx.setFormat(QSurfaceFormat::defaultFormat());
    if (surface.isValid() && ctx.create() && ctx.makeCurrent(&surface)) {
        const auto* s = reinterpret_cast<const char*>(ctx.functions()->glGetString(GL_RENDERER));
        renderer = QString::fromUtf8(s ? s : "");
        ctx.doneCurrent();
    }
    return renderer;
}

GlSurfaceWidget::Mode GlSurfaceWidget::preferredMode()
{
    const QByteArray env = qgetenv("CRTPLAYER_VIDEO_SURFACE").toLower();
    if (env == "gl") return Mode::GlWidget;
    if (env == "raster") return Mode::Raster;
    return isSoftwareRenderer(probedRenderer()) ? Mode::Raster : Mode::GlWidget;
}

GlSurfaceWidget::GlSurfaceWidget(Mode mode, QWidget* parent) : QWidget(parent), m_mode(mode)
{
    if (m_mode == Mode::GlWidget) {
        m_inner = new GlSurfaceInner(this);
        m_inner->setAttribute(Qt::WA_TransparentForMouseEvents);   // the mouse belongs to this widget
        m_inner->setFocusPolicy(Qt::NoFocus);
        m_inner->lower();
        connect(m_inner, &QOpenGLWidget::aboutToCompose, this, &GlSurfaceWidget::aboutToCompose);
        connect(m_inner, &QOpenGLWidget::frameSwapped, this, &GlSurfaceWidget::frameSwapped);
    } else {
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_NoSystemBackground);
    }
}

GlSurfaceWidget::~GlSurfaceWidget()
{
    if (m_ctx) {
        if (m_ctx->makeCurrent(m_surface)) { delete m_fbo; m_ctx->doneCurrent(); }
        delete m_ctx;
        delete m_surface;
    }
}

bool GlSurfaceWidget::ensureContext()
{
    if (m_ctx) return true;
    if (m_ctxFailed) return false;
    m_surface = new QOffscreenSurface;
    m_surface->setFormat(QSurfaceFormat::defaultFormat());
    m_surface->create();
    m_ctx = new QOpenGLContext;
    m_ctx->setFormat(QSurfaceFormat::defaultFormat());
    if (!m_surface->isValid() || !m_ctx->create() || !m_ctx->makeCurrent(m_surface)) {
        delete m_ctx; m_ctx = nullptr;
        delete m_surface; m_surface = nullptr;
        m_ctxFailed = true;
        return false;
    }
    m_packInvert = m_ctx->hasExtension("GL_MESA_pack_invert");
    initializeGL();
    return true;
}

void GlSurfaceWidget::makeCurrent()
{
    if (m_inner) { m_inner->makeCurrent(); return; }
    if (ensureContext()) m_ctx->makeCurrent(m_surface);
}

void GlSurfaceWidget::doneCurrent()
{
    if (m_inner) m_inner->doneCurrent();
    else if (m_ctx) m_ctx->doneCurrent();
}

QOpenGLContext* GlSurfaceWidget::context() const
{
    return m_inner ? m_inner->context() : m_ctx;
}

GLuint GlSurfaceWidget::defaultFramebufferObject() const
{
    if (m_inner) return m_inner->defaultFramebufferObject();
    return m_fbo ? m_fbo->handle() : 0;
}

void GlSurfaceWidget::setSurfaceScale(double s)
{
    m_scale = std::clamp(s, 0.25, 1.0);
}

QSize GlSurfaceWidget::surfacePixelSize() const
{
    const qreal k = devicePixelRatioF() * (m_inner ? 1.0 : m_scale);
    return QSize(std::max(1, int(width() * k)), std::max(1, int(height() * k)));
}

void GlSurfaceWidget::update()
{
    if (m_inner) m_inner->update();
    QWidget::update();
}

void GlSurfaceWidget::resizeEvent(QResizeEvent* e)
{
    QWidget::resizeEvent(e);
    if (m_inner) m_inner->setGeometry(rect());
}

void GlSurfaceWidget::paintEvent(QPaintEvent*)
{
    if (m_inner) return;   // (the inner widget paints itself)
    emit aboutToCompose();
    QPainter p(this);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    if (!paintPlain(p)) {
        const qreal dpr = devicePixelRatioF();
        const QSize px = surfacePixelSize();
        if (!ensureContext() || !m_ctx->makeCurrent(m_surface)) {
            p.fillRect(rect(), Qt::black);
        } else {
            if (!m_fbo || m_fbo->size() != px) {
                delete m_fbo;
                m_fbo = new QOpenGLFramebufferObject(px, QOpenGLFramebufferObject::NoAttachment, GL_TEXTURE_2D, GL_RGBA8);
                m_image = QImage(px, QImage::Format_RGB32);
                resizeGL(px.width(), px.height());
            }
            auto* f = m_ctx->extraFunctions();
            f->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo->handle());
            f->glViewport(0, 0, px.width(), px.height());
            paintGL();
            // Read the picture back, top row first, as the 32-bit pixels the window's own buffer holds.
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_fbo->handle());
            f->glPixelStorei(GL_PACK_ALIGNMENT, 4);
            if (m_packInvert) f->glPixelStorei(GL_PACK_INVERT_MESA, 1);
            f->glReadPixels(0, 0, px.width(), px.height(), GL_BGRA, GL_UNSIGNED_BYTE, m_image.bits());
            if (m_packInvert) {
                f->glPixelStorei(GL_PACK_INVERT_MESA, 0);
            } else {   // OpenGL's rows run bottom to top
                const int stride = int(m_image.bytesPerLine());
                std::vector<uchar> row = std::vector<uchar>(static_cast<size_t>(stride));
                uchar* bits = m_image.bits();
                for (int y = 0, h = px.height(); y < h / 2; ++y) {
                    uchar* a = bits + size_t(y) * stride;
                    uchar* b = bits + size_t(h - 1 - y) * stride;
                    std::memcpy(row.data(), a, size_t(stride));
                    std::memcpy(a, b, size_t(stride));
                    std::memcpy(b, row.data(), size_t(stride));
                }
            }
            f->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_ctx->doneCurrent();
            if (m_scale >= 1.0) {
                m_image.setDevicePixelRatio(dpr);
                p.drawImage(QPointF(0, 0), m_image);
            } else {   // drawn smaller: enlarged to the widget
                m_image.setDevicePixelRatio(1.0);
                p.setRenderHint(QPainter::SmoothPixmapTransform, true);
                p.drawImage(QRectF(rect()), m_image, QRectF(m_image.rect()));
            }
        }
    }
    p.end();
    emit frameSwapped();
}
