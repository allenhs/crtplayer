#pragma once
#include <QImage>
#include <QWidget>
#include <QtGui/qopengl.h>

class QOffscreenSurface;
class QOpenGLContext;
class QOpenGLFramebufferObject;
class QOpenGLWidget;
class QPainter;

// A widget drawn with OpenGL, shown in whichever way suits the machine:
//
//  - GlWidget: a QOpenGLWidget fills it. Right with a graphics card: Qt composes the
//    window on the GPU.
//  - Raster: the widget draws into a framebuffer of an OpenGL context of its own, reads
//    the picture back and paints it like any other widget. Right without a graphics card
//    (a software OpenGL renderer): a window holding a QOpenGLWidget is composed with
//    OpenGL for every frame, which costs far more there than the drawing itself, and a
//    window that never held one is not. In this mode a subclass can also paint a frame
//    with no OpenGL at all (paintPlain).
//
// Subclasses use it as they would a QOpenGLWidget: initializeGL / resizeGL / paintGL,
// makeCurrent / doneCurrent, defaultFramebufferObject, update.
class GlSurfaceWidget : public QWidget {
    Q_OBJECT
public:
    enum class Mode { GlWidget, Raster };
    // Raster when OpenGL here is a software rasteriser (looked up once, with a context of its own).
    // CRTPLAYER_VIDEO_SURFACE=gl|raster overrides.
    static Mode preferredMode();
    static QString probedRenderer();
    static bool isSoftwareRenderer(const QString& glRenderer);

    GlSurfaceWidget(Mode mode, QWidget* parent);
    ~GlSurfaceWidget() override;
    Mode surfaceMode() const { return m_mode; }
    void makeCurrent();
    void doneCurrent();
    QOpenGLContext* context() const;
    GLuint defaultFramebufferObject() const;
    // Raster mode: paintGL draws into a framebuffer this fraction of the widget's size (1 = the
    // same size), and the result is enlarged when painted. No effect in GlWidget mode.
    void setSurfaceScale(double s);
    double surfaceScale() const { return m_inner ? 1.0 : m_scale; }
    QSize surfacePixelSize() const;   // the size paintGL draws at
    void update();   // (shadows QWidget::update: in GlWidget mode it is the inner widget that repaints)

signals:
    void aboutToCompose();
    void frameSwapped();

protected:
    virtual void initializeGL() {}
    virtual void resizeGL(int, int) {}
    virtual void paintGL() {}
    // Raster mode: paint this frame without OpenGL and return true, or return false to have paintGL draw it.
    virtual bool paintPlain(QPainter&) { return false; }
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    friend class GlSurfaceInner;
    bool ensureContext();
    Mode m_mode;
    QOpenGLWidget* m_inner = nullptr;
    QOffscreenSurface* m_surface = nullptr;
    QOpenGLContext* m_ctx = nullptr;
    QOpenGLFramebufferObject* m_fbo = nullptr;
    QImage m_image;
    bool m_ctxFailed = false, m_packInvert = false;
    double m_scale = 1.0;
};
