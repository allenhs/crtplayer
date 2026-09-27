#pragma once
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <thread>

// Your own 3D models (statues) for the 90s CG room: OBJ and STL (binary or text) from a
// folder, loaded on a worker thread. Each model is centred, stood on its base, scaled to
// statue size, and given smooth normals when the file has none.
class ModelLibrary : public QObject {
    Q_OBJECT
public:
    struct Mesh {
        QString name;
        QVector<float> vertices;   // x y z  nx ny nz  r g b  (floor frame: y up, base at y = 0)
        int triangles = 0;
        bool hasColours = false;
    };
    static constexpr int kMaxModels = 6;
    static constexpr int kMaxTriangles = 400000;   // per model
    static constexpr float kHeight = 1.5f;         // statue height (the set is about 1)

    explicit ModelLibrary(QObject* parent = nullptr);
    ~ModelLibrary() override;
    void setFolder(const QString& folder);         // "" = none; loads in the background
    QString folder() const { return m_folder; }
    bool loading() const { return m_loading; }
    // The loaded meshes; `generation` changes whenever a new set is ready.
    QVector<Mesh> meshes(int* generation = nullptr) const;
    QStringList skipped() const;                    // "file: reason"
    static Mesh load(const QString& path, QString* error);
signals:
    void loaded();
private:
    void finish();
    QString m_folder;
    std::thread m_worker;
    std::atomic<bool> m_loading{false};
    std::atomic<int> m_ticket{0};
    mutable QMutex m_mutex;
    QVector<Mesh> m_meshes;
    QStringList m_skipped;
    int m_generation = 0;
};
