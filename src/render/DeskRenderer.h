#pragma once
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>
#include "ModelLibrary.h"
#include <QOpenGLVertexArrayObject>
#include <QPolygonF>
#include <QRectF>
#include <QVector3D>
#include <QVector4D>
#include <QVector>

// Procedural 3D CRT television for "desk mode".
//
// Model space: the glass is centred on the origin, 1 unit tall and `aspect` units wide
// (always landscape; portrait video uses the same set rolled 90°, like a pivoted
// arcade monitor). +Y is up, +Z points out of the screen towards the viewer.
class DeskRenderer : protected QOpenGLExtraFunctions {
public:
    struct Pose {
        float yaw = -24.f;        // degrees, turning the set left/right
        float pitch = 9.f;        // degrees, tilting it towards/away from the viewer
        bool pivot = false;       // portrait: set rolled 90°
        float distance = 4.f;     // camera distance
        QVector3D pivotPoint;     // model-space point placed at the world origin
        QPointF lensShift;        // NDC shift that places the origin on screen
    };
    struct Frame {
        QSize viewport;           // device pixels
        Pose pose;
        float clearAlpha = 0.f;   // 0 = fully transparent background (desk), 1 = opaque black
        unsigned glassTexture = 0;// CRT output (bottom-up texture), glass-shaped
        unsigned blurTexture = 0; // blurred picture (top-down), for light spill
        QRectF picInGlass{0, 0, 1, 1}; // picture rect inside the glass, glass uv (top-down)
        float glassBulge = 0.03f;
        float ledOn = 1.f;
        int arcadeArt = 0;            // arcade cabinet art: 0 space, 1 sunset, 2 neon, 3 70s woodgrain
        unsigned marqueeTexture = 0;  // the arcade marquee's title (RGBA)
        float time = 0.f;
        // Scene around the set: 0 = the desktop (transparent), 1 = desk (wooden desk, wall, lamp).
        int scene = 0;
        int mood = 0;             // 0 evening, 1 night, 2 lights off
        int wood = 0;             // 0 walnut, 1 oak, 2 cherry
        float fog = 0.f;          // 0 = off
        int fogSteps = 16;        // quality
        QSize glassSize;          // size of glassTexture (for its average-colour mip level)
        // Wall-mounted TV scene (scene 2)
        int wallStyle = 0;        // 0 warm white, 1 sage, 2 navy, 3 charcoal, 4 pinstripe, 5 damask
        int frameStyle = 0;       // 0 black, 1 wood, 2 gold
        int frameLayout = 1;      // 0 none, 1 one each side, 2 two left, 3 two right, 4 two each side
        unsigned frameTex[4] = {0, 0, 0, 0};   // pictures in slot order (0 = empty slot)
        float frameAspect[4] = {1, 1, 1, 1};
        float floorDrop = 0.f;    // floor below the set's bottom (wall scene)
        float picHeight = 0.f, picSpacing = 1.f, picSize = 1.f;
        // Movie theater (scene 3)
        float house = 1.f;        // house lights 0..1
        float curtain = 0.f;      // 0 closed .. 1 open
        QVector4D mask;           // masking opening: x0, x1, y0, y1 (theater space)
        float beam = 0.f;         // projector beam 0..1
        bool marchReference = false;   // tests: near-exact (slow) seat rendering
        // 90s CG room (scene 4)
        int cgPalette = 0, cgFloor = 0, cgStand = 0;
        bool cgObjects = true, cgBanding = false;
        int cgObjectSet = 0;      // 0 chrome & marble, 1 toybox, 2 organic, 3 mannequins, 4 mixed
        bool cgBackground = true; // a crowd in the distance: floating shapes and walking figures
        bool models = false;      // your 3D models (statues)
        int modelFinish = 0;      // 0 marble, 1 bronze, 2 chrome, 3 plastic, 4 own colours
        float reveal = 1.f;       // tile reveal of the picture (1 = done)
    };

    bool initialize(QString* error);
    void destroy();

    // Set styles. The CRT television has bulging glass (from Curvature); the flat-face CRT
    // is the same set with flat glass; the flat panel is a slim display on a stand.
    // TheaterScreen: only the screen (the movie theater scene draws the room around it).
    // ArcadeCabinet: an upright arcade cabinet (marquee, control panel, coin door), standing on
    // the floor in every scene.
    enum Cabinet { CrtTelevision = 0, FlatFaceCrt = 1, FlatPanel = 2, WoodConsole = 3, PvmMonitor = 4, BeigeMonitor = 5,
                   ArcadeCabinet = 6, TheaterScreen = 7 };
    // The arcade cabinet's layout (upright frame: the screen centred at the origin, facing +z).
    struct ArcadeLayout { float uw, uh, ci, cw, floorY; };
    ArcadeLayout arcadeLayout() const;
    // Rebuilds the cabinet when the glass aspect (always >= 1), pivot or style changes.
    // wallMount: the set hangs on a wall (no stand, legs or base; a bracket or an
    // articulating arm with a tray instead).
    void setLayout(float glassAspect, bool pivot, int cabinet, bool wallMount = false);
    void setGlassAspect(float aspect) { setLayout(aspect, m_pivot, m_cabinet, m_wallMount); }
    bool wallMount() const { return m_wallMount; }
    // Wall-mounted: the wall plane's z (upright frame; the mount's plate touches it).
    float wallZ() const { return m_wallZ; }
    // Camera height above the floor the scene shows (negative = below it). floorDrop lowers
    // the floor below the set (the wall scene's floor is far below the mounted set).
    float cameraClearance(const Pose& p, float floorDrop) const;
    // The camera in the scene's floor frame (x across, y up from the set's lowest point,
    // z towards the viewer) - for the theater, where it sits in the auditorium.
    QVector3D cameraInScene(const Pose& p) const;
    // The room around the desk and wall-mounted scenes (floor coordinates, centred on the set):
    // half its width, and the ceiling's height above the floor (floorDrop: the wall scene's
    // floor lies that far below the set).
    static float roomHalfWidth(int scene) { return scene == 2 ? 3.8f : 3.4f; }
    float ceilingHeight(int scene, float floorDrop) const
    {
        return scene == 2 ? std::max(5.0f, floorDrop + (m_bmax.y() - m_bmin.y()) + 0.8f) : 4.6f;
    }
    bool hasGeometry() const { return !m_verts.isEmpty(); }
    // Picture frames placed on the wall for the last drawn frame (wall-local: centre u, v,
    // half width, half height), for tests and hit-testing.
    int frameCount() const { return m_frameCount; }
    // Your 3D models for the 90s CG room (the GL context must be current).
    void setModels(const QVector<ModelLibrary::Mesh>& meshes);
    int modelCount() const { return m_models.size(); }
    int cabinet() const { return m_cabinet; }
    float glassAspect() const { return m_aspect; }

    void draw(unsigned targetFbo, const Frame& f);

    // Geometry queries (model space) and projections (device pixels, top-left origin).
    QVector3D boundsMin() const { return m_bmin; }
    QVector3D boundsMax() const { return m_bmax; }
    QVector3D bodyCentre() const { return (m_bmin + m_bmax) * 0.5f; }
    float cabinetHeight() const { return m_bmax.y() - m_bmin.y(); }
    float cabinetWidth() const { return m_bmax.x() - m_bmin.x(); }
    float glassRimZ() const { return m_glassZ; }

    QMatrix4x4 modelMatrix(const Pose& p) const;
    QMatrix4x4 viewProjection(const Pose& p, const QSize& vp) const;
    QPointF project(const QVector3D& modelPoint, const Pose& p, const QSize& vp) const;
    QPolygonF silhouette(const Pose& p, const QSize& vp) const;     // convex hull incl. shadow (when visible)
    bool cameraAboveFloor(const Pose& p) const;                     // false: the shadow is hidden
    QRectF glassRect(const Pose& p, const QSize& vp) const;         // bounding rect of the glass
    // Vertical field of view: a narrow lens for a set on the desk; the movie theater uses a
    // wide, human one, so the rows in front and the room are in view.
    float fovY() const { return m_fovY; }
    void setFovY(float degrees) { m_fovY = degrees; }

private:
    struct Vertex { float px, py, pz, nx, ny, nz, mat; };
    void build();
    void addTri(const Vertex& a, const Vertex& b, const Vertex& c);
    void buildArcade();

    QOpenGLShaderProgram m_prog;
    QOpenGLShaderProgram m_backdrop;
    QOpenGLShaderProgram m_haze;
    QOpenGLShaderProgram m_theater, m_theaterFront, m_cg, m_flare, m_model;
    struct GpuModel { QOpenGLVertexArrayObject* vao = nullptr; QOpenGLBuffer* vbo = nullptr; int count = 0; float halfW = 0.3f, halfD = 0.3f; };
    QVector<GpuModel> m_models;
    GpuModel m_plinth;
    GpuModel upload(const QVector<float>& vertices);
    void drawModels(int mode, const Frame& f, const QMatrix4x4& vp, const QMatrix4x4& floorToWorld, const QVector3D& setCentre,
                    const QVector3D& sunF, const QVector3D& camF, const QVector3D& tvF, float glassLod);
    QOpenGLVertexArrayObject m_quadVao;
    QOpenGLBuffer m_quadVbo;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QVector<Vertex> m_verts;
    int m_shadowFirst = 0, m_shadowCount = 0;
    bool m_initialized = false, m_dirty = true;
    float m_aspect = 4.f / 3.f;
    QVector3D m_bmin, m_bmax;
    float m_glassZ = -0.045f;
    float m_glassRadius = 0.06f;
    bool m_pivot = false;
    bool m_wallMount = false;
    float m_wallZ = -1.2f;
    int m_frameCount = 0;
    float m_fovY = 30.f;
    int m_cabinet = CrtTelevision;
    // cabinet layout (model units), exposed to the shader for procedural details
    float m_marginSide = 0.13f, m_marginTop = 0.12f, m_marginBottom = 0.30f;
    float m_floorY = 0.f;
    QRectF m_footprint;   // x/z extents of the base, for the shadow
};
