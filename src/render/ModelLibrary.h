#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>
#include <atomic>
#include <thread>

// Your own 3D models (statues) for the 90s CG room: OBJ, STL (binary or text), PLY (text
// or binary; meshes and point clouds), glTF / GLB and FBX from a folder, loaded on a
// worker thread.
// Each model is stood upright (on its flat base when it has one), centred, scaled to
// statue size, simplified when it has more triangles than can be drawn, and given
// normals when the file has none.
class ModelLibrary : public QObject {
    Q_OBJECT
public:
    // Which of the file's axes points up. Automatic: FBX and glTF files say it themselves;
    // for the others the model's flat base decides between Y and Z, and without one the
    // file type's convention (STL: Z; OBJ and PLY: Y).
    enum Up { UpAuto = 0, UpY, UpZ, UpX, UpNegY, UpNegZ, UpNegX };
    struct Mesh {
        QString name;
        QVector<float> vertices;   // x y z  nx ny nz  r g b  (floor frame: y up, base at y = 0)
        int triangles = 0;
        bool hasColours = false;
        bool points = false;       // a point cloud, drawn as dots
        int sourceTriangles = 0;   // in the file (more than `triangles` when it was simplified)
        int sourcePoints = 0;      // a point cloud: points in the file
        Up up = UpY;               // which of the file's axes points up
        int upWhy = 1;             // 0 it stands on its flat base, 1 the file type's convention, 2 chosen, 3 its flat top, 4 the file says
        float flatBase = 0.f;      // how much of its footprint lies flat on the floor (0..1)
        QVector3D size;            // width, height, depth as placed
        QString describe() const;  // one line for the settings window
    };
    static constexpr int kMaxModels = 6;
    static constexpr int kMaxTriangles = 400000;       // drawn, per model (larger models are simplified)
    static constexpr int kMaxFileTriangles = 8000000;  // read, per model
    static constexpr int kMaxPoints = 200000;          // of a point cloud (larger ones are thinned)
    static constexpr float kHeight = 1.5f;             // statue height (the set is about 1)
    // Without a graphics card every triangle is drawn by the processor: lighter limits.
    static constexpr int kLightTriangles = 120000;
    static constexpr int kLightPoints = 60000;

    explicit ModelLibrary(QObject* parent = nullptr);
    ~ModelLibrary() override;
    void setFolder(const QString& folder, Up up = UpAuto);   // "" = none; loads in the background
    void setLight(bool light) { m_light = light; }           // before setFolder: the lighter limits (no graphics card)
    bool light() const { return m_light; }
    QString folder() const { return m_folder; }
    Up up() const { return m_up; }
    bool loading() const { return m_loading; }
    // The loaded meshes; `generation` changes whenever a new set is ready.
    QVector<Mesh> meshes(int* generation = nullptr) const;
    QStringList skipped() const;                    // "file: reason"
    QStringList described() const;                  // one line per loaded model
    static Mesh load(const QString& path, QString* error, Up up = UpAuto, bool light = false);
    static QString upName(Up up);                   // "Y", "Z", "X", "-Y", "-Z", "-X"
    static Up upFromName(const QString& name);      // "auto", "y", "z", "x", "-y", "-z", "-x"
signals:
    void loaded();
private:
    QString m_folder;
    Up m_up = UpAuto;
    bool m_light = false, m_lightLoaded = false;
    std::thread m_worker;
    std::atomic<bool> m_loading{false};
    std::atomic<int> m_ticket{0};
    mutable QMutex m_mutex;
    QVector<Mesh> m_meshes;
    QStringList m_skipped;
    int m_generation = 0;
};
