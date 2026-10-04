#pragma once
#include "render/CrtRenderer.h"
#include "render/Geometry.h"
#include "settings/CrtParams.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include "GlSurfaceWidget.h"
#include <gst/gst.h>
#include <QTimer>

class Player;
class QLabel;

struct SyncStats {
    int frames = 0;
    double meanMs = 0, maxAbsMs = 0, stddevMs = 0;
    int within20ms = 0;   // frames presented within ±20 ms of their clock time
    double presentedFps = 0;   // new frames shown per second while playing
    double paintMsAvg = 0, paintMsMax = 0;   // CPU time of paintGL (includes software GL work)
    QJsonObject toJson() const;
};

class VideoWidget : public GlSurfaceWidget {
    Q_OBJECT
public:
    explicit VideoWidget(Player* player, QWidget* parent = nullptr, GlSurfaceWidget::Mode mode = GlSurfaceWidget::preferredMode());
    ~VideoWidget() override;

    void setParams(const CrtParams& p);
    const CrtParams& params() const { return m_params; }
    void setBypass(bool on);
    bool bypass() const { return m_bypass; }
    void setCompare(bool on);
    bool compare() const { return m_compare; }
    void setSplitFraction(double f);
    double splitFraction() const { return m_split; }
    void setScaleMode(ScaleMode m);
    ScaleMode scaleMode() const { return m_mode; }
    void setCrop(const CropFractions& c);
    CropFractions crop() const { return m_crop; }
    void setAspectOverride(double a);
    double aspectOverride() const { return m_aspectOverride; }
    void setOrientationTag(const QString& tag);
    Orientation orientation() const { return m_orient; }
    ViewSettings viewSettings() const;
    // Test hook: pins the effect clock (noise, flicker, fields, crawl) to a fixed time in
    // seconds so time-animated effects can be captured deterministically; < 0 = live.
    void setClockOverride(double seconds) { m_clockOverride = seconds; update(); }
    // The effect clock (seconds): shared by every view of the picture, so time-based effects
    // (grain, flicker, VHS jitter, static) match between them; pinned by tests.
    double clockOverride() const { return m_clockOverride; }
    double effectTime() const { return m_clockOverride >= 0.0 ? m_clockOverride : m_clock.elapsed() / 1000.0; }
    double effectClock() const;

    // Set moments (used when the current CRT parameters enable them).
    void powerOff();                                  // collapse to a dot, then stay dark
    void channelChange();                             // static until the next file's first frame
    void startMoment(int type);                       // test hook: 1 power on, 2 power off, 0 end all moments
    void setOsdText(const QString& text, double seconds);   // VCR on-screen display; seconds <= 0: until changed
    // Adds the set-moment and OSD state for the current effect clock to draw parameters.
    // Shared with the desk view, so both show the same moments.
    void decorate(CrtRenderer::DrawParams& d);
    const QImage& osdImage() const { return m_overlayOn ? m_overlayImage : m_osdImage; }
    // A picture-sized overlay drawn into the signal (Cable TV: channel number, banners, the
    // guide). It goes through the tube like the picture. A null image removes it.
    void setOverlayImage(const QImage& img);
    bool hasOverlay() const { return m_overlayOn; }
    void releaseStatic() { m_staticHold = false; m_staticRelease = -1; update(); }   // ends held static
    void setSnow(bool on) { if (m_snow != on) { m_snow = on; update(); } }           // static until switched off (a channel with nothing on)
    bool snow() const { return m_snow; }
    int osdVersion() const { return m_osdVersion; }
    QString momentName() const;
    bool momentsAnimating() const;
    void setBottomInset(int logicalPx);
    void clearFrame();

    SourceFormat sourceFormat() const { return m_source; }
    QSizeF displaySizeNow() const;
    bool hasFrame() const { return m_hasFrame; }
    QString pixelFormat() const { return m_pixFmt; }
    QString colorimetry() const { return m_colorimetry; }
    QString glInfo() const { return m_glInfo; }
    QString glError() const { return m_glError; }
    qint64 framesPresented() const { return m_framesPresented; }
    QImage grabFmvFrame() { makeCurrent(); uploadCurrent(); QImage i = m_renderer.fmvImage(); doneCurrent(); return i; }
    int fmvColorsUsed() const { return m_renderer.fmvColorsUsed(); }
    quint64 fmvFramesDrawn() const { return m_renderer.fmvFramesDrawn(); }

    // Layout of the current frame in device pixels (for the info overlay and tests).
    LayoutResult currentLayout() const;
    QRectF videoAreaPx() const;
    double currentScanlines() const;

    QImage grabOriginalFrame();
    QImage grabFilteredFrame();
    // The visible picture's size fitted into `box` (height 0: by width only), even numbers.
    QSize pictureSizeIn(const QSize& box) const;
    // The newest frame's picture rendered for exactly `size` (any size, up to 4K and beyond):
    // with the look as on screen, or the original. clock < 0: the live effect clock.
    QImage renderPictureAt(const QSize& size, bool filtered, double clock = -1);

    SyncStats syncStats() const;
    void resetSyncStats();

signals:
    void sourceChanged();
    void mouseActivity();
    void doubleClicked();
    void splitChanged(double f);
    void glFailed(const QString& message);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    bool paintPlain(QPainter& p) override;   // effects off, without a graphics card: straight from the frame
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    CrtRenderer::DrawParams makeDrawParams(const QSize& viewport, const QRectF& area) const;
    void updateCompareLabels();
    // forGl: the frame is uploaded for drawing with OpenGL now. Otherwise that waits until
    // something draws with OpenGL (the plain picture is painted straight from the frame).
    void takeNewFrame(bool forGl = true);
    bool uploadCurrent();
    bool plainPaintable() const;
    GstSample* m_curSample = nullptr;   // the frame on screen (a reference)
    bool m_enhUp = false, m_enhMotion = false;
    double m_enhSharp = 0.5;
    double m_lastPhase = 1.0;
    quint64 m_phaseDraws = 0, m_betweenDraws = 0;
    GstSample* m_curNative = nullptr;   // the same frame as decoded, when the one on screen was converted or scaled
    bool m_uploaded = false;            // ... and whether the renderer has it
    QSize m_curSize;                    // its size as delivered
    bool m_curConverted = false;        // it was converted or scaled for the screen by the video pipeline (not as decoded)
    bool m_curRgb = false;              // ... and whether it is 32-bit RGB (paintable as it is)
    bool needsAnimation() const;
    double splitX() const; // logical px

    Player* m_player;
    CrtRenderer m_renderer;
    CrtParams m_params;
    bool m_bypass = false;
    bool m_compare = false;
    double m_split = 0.5;
    bool m_draggingSplit = false;
    ScaleMode m_mode = ScaleMode::Fit;
    CropFractions m_crop;
    double m_aspectOverride = 0;
    Orientation m_orient;
    bool m_orientDirty = false;
    int m_bottomInset = 0;

    SourceFormat m_source;
    bool m_hasFrame = false;
    QString m_pixFmt, m_colorimetry, m_glInfo, m_glError;
    QElapsedTimer m_clock;
    QTimer m_animTimer;   // drives noise/flicker animation at <= 60 Hz, independent of swap throttling
    quint32 m_frameCounter = 0;
    quint64 m_shownSerial = 0;
    double m_clockOverride = -1.0;
    // set moments
    int m_moment = 0;             // 0 none, 1 power on, 2 power off
    double m_momentStart = 0;
    bool m_poweredOn = false;     // false before the first picture and after power-off
    quint64 m_offSerial = 0;
    bool m_staticHold = false;    // channel change: static until a new frame arrives
    quint64 m_holdSerial = 0;
    double m_staticRelease = -1;
    QImage m_osdImage;
    QImage m_overlayImage;
    bool m_overlayOn = false;
    bool m_snow = false;
    int m_osdVersion = 0, m_osdUploaded = -1;
    double m_osdUntil = 0;
    bool m_osdOn = false;
    qint64 m_framesPresented = 0;

    // Sync statistics (Welford)
    int m_syncN = 0;
    double m_syncMean = 0, m_syncM2 = 0, m_syncMaxAbs = 0;
    int m_syncWithin = 0;
    QElapsedTimer m_syncTimer;
    // profiling: Qt putting the widget on screen (compose + present), and the time between frames shown
    QElapsedTimer m_composeTimer, m_intervalTimer;
    QElapsedTimer m_cpuWall;        // since the statistics were last reset
    double m_cpuStart = 0;
    double m_composeSum = 0, m_intervalSum = 0;
    int m_composeN = 0, m_intervalN = 0;
public:
    void setProfiling(bool on);
    // Playback without a graphics card
    bool softwareRenderer() const { return m_softwareGl; }
    bool uprightSource() const { return m_orient == Orientation(); }
    QSize fastTargetSize() const;                     // the size frames are needed at for the picture as shown now
    QSize lookTargetSize() const;                     // ... with a look: empty (the video's own size) unless it is much larger than its picture
    // Without a graphics card, the look can be drawn at a fraction of the window's size and enlarged (1 = full size).
    void setLookScale(double s);
    double lookScale() const { return m_lookScale; }
private:
    double m_lookScale = 1.0;
public:
    void setPlainOnly(bool on) { m_renderer.setPlainOnly(on); }
    QSize frameSize() const { return m_curSample ? m_curSize : m_renderer.frameSize(); }   // of the frame on screen, as delivered
    bool frameDirect() const { return m_curRgb; }            // it came as RGB (no conversion pass)
    bool frameIsScaled() const;                       // the frame on screen is smaller than the video as decoded
    bool showNativeFrame();                           // replace the frame on screen by the same frame as decoded, if at hand
    void pullFrame();                                 // take the newest frame now (without waiting for a repaint)
    QJsonObject profileReport() const;
    // Enhance (2.13): sharper upscaling with effects off, and frame generation. For a graphics
    // card; with software OpenGL only when forced (CRTPLAYER_ENHANCE_FORCE, for the checks).
    void setEnhance(bool upscale, double sharpness, bool smoothMotion);
    bool enhanceAvailable() const;
    bool smoothMotionUseful() const;       // wanted, available, and the screen refreshes clearly faster than the video's frames come
    bool smoothMotionRunning() const;      // frames are being generated between the video's own
    QJsonObject enhanceReport() const;
    QImage grabBetween(double t, int mode);   // the picture at phase t between the frame before and this one (mode 1: a plain mix)
private:
    bool m_softwareGl = false;
    int m_paintN = 0;
    double m_paintSum = 0, m_paintMax = 0;

    QLabel* m_labelOriginal = nullptr;
    QLabel* m_labelCrt = nullptr;
};
