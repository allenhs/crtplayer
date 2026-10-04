#pragma once
// A 3D model as read from its file, before it is stood upright and placed (see
// ModelLibrary). Internal to the model loader.
#include <QImage>
#include <QString>
#include <QVector>
#include <QVector3D>

struct RawModel {
    QVector<QVector3D> v;      // positions
    QVector<QVector3D> vn;     // normals, one per position (or empty)
    QVector<QVector3D> vc;     // colours, one per position (or empty)
    QVector<quint32> idx;      // three per triangle; empty for a point cloud
    QVector<QVector3D> cn;     // normals, one per triangle corner (or empty)
    QVector<QVector3D> cc;     // colours, one per triangle corner (or empty)
    // Pictures wrapped around the model (or none). A textured triangle takes its colour
    // from its picture at `uv`, multiplied by `cmul`; the others use `cc`.
    QVector<QImage> textures;  // RGB32, at most 1024 pixels on a side
    QVector<int> triTexture;   // one per triangle: which picture, or -1
    QVector<float> uv;         // two per triangle corner; v runs down from the top of the picture
    QVector<QVector3D> cmul;   // one per triangle corner, linear light (or empty: the picture as it is)
    qint64 filePoints = 0;     // a point cloud: points in the file (it may be thinned on reading)
    bool points = false;       // a point cloud
};

// Each returns false with a short reason in *error.
bool loadObjModel(const QString& path, RawModel& raw, QString* error);
bool loadStlModel(const QString& path, RawModel& raw, QString* error);
bool loadPlyModel(const QString& path, RawModel& raw, QString* error);
bool loadGltfModel(const QString& path, RawModel& raw, QString* error);   // .glb and .gltf
bool loadFbxModel(const QString& path, RawModel& raw, QString* error);
// A picture for a model, from a file's bytes: whatever Qt reads, and TGA (which Qt reads
// only in its newer form, and only with a plugin).
QImage modelPicture(const QByteArray& bytes);
