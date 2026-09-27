#pragma once
#include "render/CrtRenderer.h"
#include "render/Geometry.h"
#include "settings/CrtParams.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QOpenGLWidget>
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

class VideoWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit VideoWidget(Player* player, QWidget* parent = nullptr);
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
    const QImage& osdImage() const { return m_osdImage; }
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

    // Layout of the current frame in device pixels (for the info overlay and tests).
    LayoutResult currentLayout() const;
    QRectF videoAreaPx() const;
    double currentScanlines() const;

    QImage grabOriginalFrame();
    QImage grabFilteredFrame();

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
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    CrtRenderer::DrawParams makeDrawParams(const QSize& viewport, const QRectF& area) const;
    void updateCompareLabels();
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
    int m_osdVersion = 0, m_osdUploaded = -1;
    double m_osdUntil = 0;
    bool m_osdOn = false;
    qint64 m_framesPresented = 0;

    // Sync statistics (Welford)
    int m_syncN = 0;
    double m_syncMean = 0, m_syncM2 = 0, m_syncMaxAbs = 0;
    int m_syncWithin = 0;
    QElapsedTimer m_syncTimer;
    int m_paintN = 0;
    double m_paintSum = 0, m_paintMax = 0;

    QLabel* m_labelOriginal = nullptr;
    QLabel* m_labelCrt = nullptr;
};
