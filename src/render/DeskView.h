#pragma once
#include "render/CrtRenderer.h"
#include "render/DeskRenderer.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QVector4D>
#include <QPolygonF>
#include <QTimer>

class Player;
class VideoWidget;

// Desk mode: the CRT output shown on a 3D television that stands on the desktop,
// inside a transparent window. It reads presentation settings from the flat
// VideoWidget, so both always show the same CRT look.
//
// Phases: Desk (set on the desktop) -> FlyingIn -> Full (normal flat fullscreen
// rendering, pixel-identical to the regular player) -> FlyingOut -> Desk.
class DeskView : public QOpenGLWidget {
    Q_OBJECT
public:
    enum class Phase { Desk, FlyingIn, Full, FlyingOut };
    Q_ENUM(Phase)

    DeskView(Player* player, VideoWidget* flat, QWidget* parent = nullptr);
    ~DeskView() override;

    Phase phase() const { return m_phase; }
    QString phaseName() const;
    double flightProgress() const { return m_t; }
    void flyIn();
    void flyOut();
    void toggleFly();

    // "Follow video": the tube takes the video's shape (portrait video -> pivoted set).
    // "Classic": a 4:3 tube with other shapes letterboxed inside, as it really looked.
    void setClassicShape(bool classic);
    bool classicShape() const { return m_classic; }
    // 0 CRT television, 1 flat-face CRT, 2 flat-panel screen (see DeskRenderer::Cabinet).
    void setCabinet(int cabinet);
    int cabinet() const { return m_cabinet; }
    static QString cabinetName(int cabinet);
    // Room backdrop (desk surface + wall) instead of showing the desktop through.
    // The scene around the set (see DeskRenderer::Frame).
    struct Scene {
        int scene = 0;            // 0 your desktop (transparent), 1 desk, 2 wall-mounted TV
        int mood = 0;             // 0 evening, 1 night, 2 lights off
        int wood = 0;             // 0 walnut, 1 oak, 2 cherry
        int fog = 0;              // 0 off, 1 light haze, 2 thick fog
        double fogStrength = 1.0; // 0.25 .. 2
        int quality = 1;          // 0 low, 1 medium, 2 high (fog samples)
        // wall-mounted TV
        int wallStyle = 0;        // 0 warm white, 1 sage, 2 navy, 3 charcoal, 4 pinstripe, 5 damask
        int frameStyle = 0;       // 0 black, 1 wood, 2 gold
        int frameLayout = 1;      // 0 none, 1 one each side, 2 two left, 3 two right, 4 two each side
        QStringList framePaths;   // up to 4 pictures, in slot order
        double tvHeight = 1.7;    // how high the set hangs: its bottom above the floor (1.0 .. 3.0)
        double picHeight = 0.0;   // pictures' height relative to the screen's centre (-0.6 .. 0.6)
        double picSpacing = 1.0;  // distance from the set (x the automatic gap, 0.5 .. 2)
        double picSize = 1.0;     // picture size (0.6 .. 1.5)
        // 90s CG room (scene 4)
        int cgPalette = 0;        // 0 workstation, 1 sunset, 2 deep space
        int cgFloor = 0;          // 0 checkerboard, 1 neon grid
        int cgStand = 0;          // 0 chrome pedestal, 1 marble plinth, 2 floating
        bool cgObjects = true, cgBanding = false, cgReveal = true, cgOrbit = true;
        int cgObjectSet = 0;      // 0 chrome & marble (default), 1 toybox, 2 organic, 3 mannequins, 4 mixed
        bool cgBackground = true; // the background crowd
        QString modelsFolder;     // your 3D models (OBJ / STL)
        bool models = true;
        int modelFinish = 0;      // 0 marble, 1 bronze, 2 chrome, 3 plastic, 4 own colours
    };
    void setScene(const Scene& s);
    // In a scene the camera never goes below the floor (or behind the wall).
    void keepAboveFloor();
    double floorDrop() const
    {
        if (m_cabinet == DeskRenderer::ArcadeCabinet && m_sceneSet.scene != 3) return 0.0;   // it stands on the floor
        if (m_sceneSet.scene == 2) return m_sceneSet.tvHeight;
        if (m_sceneSet.scene == 4) return m_sceneSet.cgStand == 0 ? 1.0 : m_sceneSet.cgStand == 1 ? 0.7 : 1.3;
        return 0.0;
    }
    double revealProgress() const { return m_reveal; }
    void finishReveal() { m_reveal = 1.0; update(); }
    // The arcade cabinet: its art (0 space, 1 sunset, 2 neon, 3 70s woodgrain) and the title
    // on its marquee.
    void setArcadeArt(int art) { m_arcadeArt = std::clamp(art, 0, 3); update(); }
    int arcadeArt() const { return m_arcadeArt; }
    void setMarqueeText(const QString& text) { m_marqueeText = text; update(); }
    QString marqueeText() const { return m_marqueeText; }
    static QString marqueeTitle(const QString& raw);   // a file name tidied for a marquee
    QImage marqueeImage() const;                        // what the marquee's title looks like
    // The tile reveal plays when a video starts, or after playback was stopped a while
    // (kRevealAfterPauseMs); never for seeking, scrubbing or a short pause.
    static constexpr qint64 kRevealAfterPauseMs = 10000;
    void notifyNewMedia() { m_freshMedia = true; }
    void setScrubbing(bool on) { m_scrubbing = on; m_idleFor.restart(); }
    // Any seek means someone is using the player: the demo-reel orbit waits again.
    void noteSeek() { m_idleFor.restart(); ++m_seeksNoted; }
    qint64 idleMs() const { return m_idleFor.isValid() ? m_idleFor.elapsed() : -1; }
    int seeksNoted() const { return m_seeksNoted; }
    ModelLibrary* modelLibrary() { return &m_library; }
    int modelsShown() const { return m_desk.modelCount(); }
    // Movie theater: the curtains open while a video plays (and stay open when it is paused);
    // the house lights follow them; the projector beam shows while it plays.
    void setTheaterState(bool curtainsOpen, bool projecting);
    double curtainOpen() const { return m_curtain; }
    void finishTheaterMoves();   // curtains, beam and masking straight to where they are going (tests)
    void setMarchReference(bool on) { m_marchRef = on; update(); }
    // The auditorium (shared with shaders/theater*.frag): stadium rows in theater space
    // (screen: y 0..1, z = 0); each row one step higher than the one in front.
    static constexpr double kAudY = -0.45, kRow0Z = 1.2, kRowD = 0.22, kRise = 0.07, kEye = 0.23;
    static constexpr int kRows = 20;
    static double seatEye(double z);   // seated eye height at depth z (a smooth ramp over the rows)
    QVector3D theaterCamera();         // where the camera sits (theater space)
    void keepTheaterSeat();            // zoom = rows, sideways = along the row, eye at seated height
    double houseLights() const { return 1.0 - m_curtain; }
    QRectF maskRect() const { return QRectF(m_mask.x(), m_mask.z(), m_mask.y() - m_mask.x(), m_mask.w() - m_mask.z()); }

    // A small picture of the current view with another scene (for the settings window).
    QImage scenePreview(int scene, const QSize& size);
    // The whole view rendered for another size of the same shape (GIF clips up to 4K).
    QSizeF pictureDisplaySize() const;   // of the video being shown (empty before the first frame)
    QSize sceneSizeIn(const QSize& box) const;
    QImage grabScene(const QSize& size);
    int framesShown() const { return m_desk.frameCount(); }
    int picturesLoaded() const;
    double cameraClearance();   // the camera's height above the scene's floor (for tests)
    Scene scene() const { return m_sceneSet; }
    static QString sceneName(int scene);
    // Any scene other than the desktop: nothing to click through to.
    bool room() const { return m_sceneSet.scene != 0; }
    // The set actually drawn: the movie theater shows its screen instead of the chosen set.
    int effectiveCabinet() const { return m_sceneSet.scene == 3 ? int(DeskRenderer::TheaterScreen) : m_cabinet; }
    void setRoom(bool on) { m_sceneSet.scene = on ? std::max(1, m_sceneSet.scene) : 0; update(); }

    struct DeskPose { double yaw = -24, pitch = 9, height = 0.46, cx = 0.5, cy = 0.52; };
    void setDeskPose(const DeskPose& p);
    DeskPose deskPose() const { return m_pose; }
    void resetDeskPose();
    static DeskPose theaterSeat();   // where you sit in the movie theater

    QPolygonF silhouetteLogical() const { return m_silhouette; }  // widget coordinates
    QRectF glassRectLogical() const { return m_glassRect; }
    bool isPivoted() const { return m_pivot; }
    bool shadowVisible() const { return m_shadowVisible; }   // false when viewed from below the floor
    bool hasFrame() const { return m_hasFrame; }
    SourceFormat sourceFormat() const { return m_source; }
    void clearFrame();   // a new file is opening: drop the previous picture
    QString glError() const { return m_glError; }

signals:
    void phaseChanged(DeskView::Phase phase);
    void silhouetteChanged(const QPolygonF& logicalPolygon);
    void contextMenuRequested(const QPoint& globalPos);
    void mouseActivity();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    struct Geo { bool pivot = false; float glassAspect = 4.f / 3.f; float displayAspect = 4.f / 3.f; };
    Geo geometry() const;
    DeskRenderer::Pose poseAt(double e, const QSize& vp, const Geo& g) const;
    QRectF fullGlassRect(const QSize& vp, const Geo& g) const;
    void startFlight(Phase to);
    void tick();
    bool needsTicks() const;
    bool insideTv(const QPointF& p) const;

    Player* m_player;
    VideoWidget* m_flat;
    CrtRenderer m_crt;
    DeskRenderer m_desk;
    bool m_glOk = false;
    QString m_glError;

    SourceFormat m_source;
    bool m_hasFrame = false;
    quint64 m_shownSerial = 0;
    unsigned m_grabFbo = 0;      // a scene grab in progress renders here
    double m_grabScale = 1.0;
    int m_osdUploaded = -1;
    Orientation m_orient;
    QElapsedTimer m_clock;
    quint32 m_frameCounter = 0;

    DeskPose m_pose;
    bool m_classic = false;
    int m_cabinet = DeskRenderer::CrtTelevision;
    Scene m_sceneSet;
    bool m_curtainTarget = false, m_projecting = false;
    bool m_marchRef = false;
    double m_reveal = 1.0;       // 90s CG room: the picture resolving tile by tile
    bool m_freshMedia = false;   // a new video (told by MainWindow): its start plays the reveal
    bool m_scrubbing = false;    // the timeline is being dragged
    QElapsedTimer m_stoppedFor;  // how long playback has been stopped
    QElapsedTimer m_idleFor;     // stopped and untouched (no seeks): the orbit starts after 10 s
    int m_seeksNoted = 0;
    ModelLibrary m_library;
    int m_arcadeArt = 0;
    QString m_marqueeText, m_marqueeLoaded = QStringLiteral("\x01");
    unsigned m_marqueeTex = 0;
    void syncMarquee();
    int m_modelsUploaded = -1;
    QElapsedTimer m_cgClock;
    double m_curtain = 0.0, m_beam = 0.0;
    QVector4D m_mask = QVector4D(-0.66f, 0.66f, 0.f, 1.f);
    bool m_maskInit = false;
    QElapsedTimer m_theaterClock;
    bool animateTheater();   // advances curtains, beam and masking; true while moving
    QSize m_glassSize;
    void syncFrameTextures();
    QStringList m_loadedFramePaths;
    unsigned m_frameTex[4] = {0, 0, 0, 0};
    float m_frameAspect[4] = {1, 1, 1, 1};
    bool m_pivot = false;
    Phase m_phase = Phase::Desk;
    double m_t = 0;                 // 0 = on the desk, 1 = fullscreen
    double m_flightFrom = 0;
    QElapsedTimer m_flightClock;
    static constexpr double kFlightMs = 950.0;

    enum class Drag { None, Rotate, Move };
    Drag m_drag = Drag::None;
    QPointF m_lastPos;
    QPointF m_velocity;             // degrees per tick, for inertia after a flick
    QElapsedTimer m_lastMove;
    QTimer m_ticker;

    QPolygonF m_silhouette;
    bool m_shadowVisible = true;
    QRectF m_glassRect;
};
