// Your 3D models: the readers (OBJ, STL, PLY, glTF / GLB, FBX), which way is up, normals,
// point clouds, and models too large to draw as they are.
#include "render/ModelLibrary.h"
#include "render/ModelRaw.h"

#include <QBuffer>
#include <QFile>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtEndian>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <functional>
#include <vector>

using Mesh = ModelLibrary::Mesh;
static int fails = 0;
static const double kPi = 3.14159265358979323846;
static void check(bool ok, const char* what, const QString& detail = QString())
{
    std::printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.isEmpty() ? "" : ": ", qPrintable(detail));
    if (!ok) ++fails;
}

// ---- shapes (Y up unless turned)
struct Shape {
    std::vector<QVector3D> v;
    std::vector<std::vector<int>> f;   // polygons
};
static Shape cone(int seg = 24, float r = 0.5f, float h = 2.f)   // a flat base, the tip at the top
{
    Shape s;
    s.v.push_back(QVector3D(0, 0, 0));
    for (int i = 0; i < seg; ++i) s.v.push_back(QVector3D(r * std::cos(2 * kPi * i / seg), 0, r * std::sin(2 * kPi * i / seg)));
    s.v.push_back(QVector3D(0, h, 0));
    for (int i = 0; i < seg; ++i) {
        const int a = 1 + i, b = 1 + (i + 1) % seg;
        s.f.push_back({0, a, b});
        s.f.push_back({seg + 1, b, a});
    }
    return s;
}
static Shape sphere(int rows, int cols, float r = 1.f)
{
    Shape s;
    for (int i = 0; i <= rows; ++i)
        for (int j = 0; j < cols; ++j) {
            const double t = kPi * i / rows, p = 2 * kPi * j / cols;
            s.v.push_back(QVector3D(float(r * std::sin(t) * std::cos(p)), float(r * std::cos(t)), float(r * std::sin(t) * std::sin(p))));
        }
    for (int i = 0; i < rows; ++i)
        for (int j = 0; j < cols; ++j) {
            const int a = i * cols + j, b = i * cols + (j + 1) % cols, c = (i + 1) * cols + (j + 1) % cols, d = (i + 1) * cols + j;
            if (i > 0) s.f.push_back({a, b, c});
            if (i < rows - 1) s.f.push_back({a, c, d});
        }
    return s;
}
static Shape box(QVector3D lo, QVector3D hi, Shape s = Shape())
{
    const int o = int(s.v.size());
    for (int i = 0; i < 8; ++i) s.v.push_back(QVector3D(i & 1 ? hi.x() : lo.x(), i & 2 ? hi.y() : lo.y(), i & 4 ? hi.z() : lo.z()));
    const int q[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {1, 3, 7, 5}, {0, 4, 6, 2}};
    for (auto& f : q) s.f.push_back({o + f[0], o + f[1], o + f[2], o + f[3]});
    return s;
}
static Shape zUp(Shape s) { for (QVector3D& p : s.v) p = QVector3D(p.x(), -p.z(), p.y()); return s; }   // the same shape in a Z-up file
static std::vector<std::array<int, 3>> triangles(const Shape& s)
{
    std::vector<std::array<int, 3>> t;
    for (const auto& f : s.f) for (size_t i = 1; i + 1 < f.size(); ++i) t.push_back({f[0], f[i], f[i + 1]});
    return t;
}

// ---- writers
static void put(const QString& path, const QByteArray& data) { QFile f(path); f.open(QIODevice::WriteOnly); f.write(data); }
static QByteArray objText(const Shape& s)
{
    QByteArray o;
    for (const QVector3D& p : s.v) o += QByteArray("v ") + QByteArray::number(p.x(), 'f', 6) + ' ' + QByteArray::number(p.y(), 'f', 6) + ' ' + QByteArray::number(p.z(), 'f', 6) + '\n';
    for (const auto& f : s.f) { o += "f"; for (int i : f) o += ' ' + QByteArray::number(i + 1); o += '\n'; }
    return o;
}
static QByteArray stlBinary(const Shape& s)
{
    const auto t = triangles(s);
    QByteArray o(80, ' ');
    quint32 n = quint32(t.size());
    o.append(reinterpret_cast<const char*>(&n), 4);
    for (const auto& tri : t) {
        float rec[12] = {0, 0, 0};
        for (int k = 0; k < 3; ++k) { rec[3 + k * 3] = s.v[tri[k]].x(); rec[4 + k * 3] = s.v[tri[k]].y(); rec[5 + k * 3] = s.v[tri[k]].z(); }
        o.append(reinterpret_cast<const char*>(rec), 48);
        o.append(2, '\0');
    }
    return o;
}
template <typename T> static void raw(QByteArray& o, T v, bool big)
{
    char b[sizeof(T)];
    if (big) qToBigEndian(v, b); else qToLittleEndian(v, b);
    o.append(b, sizeof(T));
}
static void rawFloat(QByteArray& o, float v, bool big) { quint32 u; std::memcpy(&u, &v, 4); raw<quint32>(o, u, big); }
// format: 0 text, 1 binary little-endian, 2 binary big-endian
static QByteArray ply(const Shape& s, int format, bool colours = false, bool faces = true)
{
    QByteArray o = "ply\nformat ";
    o += format == 0 ? "ascii" : format == 1 ? "binary_little_endian" : "binary_big_endian";
    o += " 1.0\ncomment test\nelement vertex " + QByteArray::number(int(s.v.size())) + "\nproperty float x\nproperty float y\nproperty float z\n";
    if (colours) o += "property uchar red\nproperty uchar green\nproperty uchar blue\n";
    if (faces) o += "element face " + QByteArray::number(int(s.f.size())) + "\nproperty list uchar int vertex_indices\n";
    o += "end_header\n";
    for (const QVector3D& p : s.v) {
        if (format == 0) {
            o += QByteArray::number(p.x(), 'g', 9) + ' ' + QByteArray::number(p.y(), 'g', 9) + ' ' + QByteArray::number(p.z(), 'g', 9);
            if (colours) o += " 255 128 0";
            o += '\n';
        } else {
            for (int k = 0; k < 3; ++k) rawFloat(o, p[k], format == 2);
            if (colours) o.append("\xff\x80\x00", 3);
        }
    }
    if (faces) for (const auto& f : s.f) {
        if (format == 0) { o += QByteArray::number(int(f.size())); for (int i : f) o += ' ' + QByteArray::number(i); o += '\n'; }
        else { o += char(f.size()); for (int i : f) raw<qint32>(o, i, format == 2); }
    }
    return o;
}
static QByteArray glb(const QJsonObject& doc, const QByteArray& bin)
{
    QByteArray j = QJsonDocument(doc).toJson(QJsonDocument::Compact);
    while (j.size() % 4) j += ' ';
    QByteArray b = bin;
    while (b.size() % 4) b += '\0';
    QByteArray o = "glTF";
    raw<quint32>(o, 2, false);
    raw<quint32>(o, quint32(12 + 8 + j.size() + (b.isEmpty() ? 0 : 8 + b.size())), false);
    raw<quint32>(o, quint32(j.size()), false); raw<quint32>(o, 0x4E4F534A, false); o += j;
    if (!b.isEmpty()) { raw<quint32>(o, quint32(b.size()), false); raw<quint32>(o, 0x004E4942, false); o += b; }
    return o;
}
// One mesh: positions, indices, and perhaps colours (COLOR_0, bytes) or texture coordinates; `extra` is merged into the document.
static QByteArray glbOf(const Shape& s, const QJsonObject& node, const QJsonObject& extra = QJsonObject(), bool vertexColours = false,
                        const std::vector<QPointF>* uv = nullptr, const QByteArray& png = QByteArray())
{
    QByteArray bin;
    QJsonArray views, acc;
    auto view = [&](const QByteArray& data, const QJsonObject& a) {
        while (bin.size() % 4) bin += '\0';
        views.append(QJsonObject{{"buffer", 0}, {"byteOffset", int(bin.size())}, {"byteLength", int(data.size())}});
        bin += data;
        if (!a.isEmpty()) { QJsonObject o = a; o["bufferView"] = int(views.size()) - 1; acc.append(o); }
    };
    QByteArray pos, idx, col, tex;
    for (const QVector3D& p : s.v) for (int k = 0; k < 3; ++k) rawFloat(pos, p[k], false);
    const auto t = triangles(s);
    for (const auto& tri : t) for (int i : tri) raw<quint16>(idx, quint16(i), false);
    QJsonObject attr{{"POSITION", 0}};
    view(pos, {{"componentType", 5126}, {"count", int(s.v.size())}, {"type", "VEC3"}});
    view(idx, {{"componentType", 5123}, {"count", int(t.size() * 3)}, {"type", "SCALAR"}});
    if (vertexColours) {
        for (size_t i = 0; i < s.v.size(); ++i) col.append("\xff\x00\x00\xff", 4);   // red
        view(col, {{"componentType", 5121}, {"normalized", true}, {"count", int(s.v.size())}, {"type", "VEC4"}});
        attr["COLOR_0"] = int(acc.size()) - 1;
    }
    if (uv) {
        for (const QPointF& q : *uv) { rawFloat(tex, float(q.x()), false); rawFloat(tex, float(q.y()), false); }
        view(tex, {{"componentType", 5126}, {"count", int(s.v.size())}, {"type", "VEC2"}});
        attr["TEXCOORD_0"] = int(acc.size()) - 1;
    }
    QJsonObject prim{{"attributes", attr}, {"indices", 1}};
    QJsonObject doc{{"asset", QJsonObject{{"version", "2.0"}}}, {"scenes", QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}}}};
    if (!png.isEmpty()) {
        view(png, QJsonObject());
        doc["images"] = QJsonArray{QJsonObject{{"bufferView", int(views.size()) - 1}, {"mimeType", "image/png"}}};
        doc["textures"] = QJsonArray{QJsonObject{{"source", 0}}};
        doc["materials"] = QJsonArray{QJsonObject{{"pbrMetallicRoughness", QJsonObject{{"baseColorTexture", QJsonObject{{"index", 0}}}}}}};
        prim["material"] = 0;
    }
    for (auto it = extra.begin(); it != extra.end(); ++it) { doc[it.key()] = it.value(); if (it.key() == "materials") prim["material"] = 0; }
    QJsonObject n = node;
    n["mesh"] = 0;
    doc["nodes"] = QJsonArray{n};
    doc["meshes"] = QJsonArray{QJsonObject{{"primitives", QJsonArray{prim}}}};
    doc["buffers"] = QJsonArray{QJsonObject{{"byteLength", int(bin.size())}}};
    doc["bufferViews"] = views;
    doc["accessors"] = acc;
    return glb(doc, bin);
}
static QByteArray fbxText(const Shape& s, int upAxis)
{
    QByteArray v, i;
    for (const QVector3D& p : s.v) for (int k = 0; k < 3; ++k) v += (v.isEmpty() ? "" : ",") + QByteArray::number(p[k], 'f', 6);
    for (const auto& f : s.f) for (size_t k = 0; k < f.size(); ++k) i += (i.isEmpty() ? "" : ",") + QByteArray::number(k + 1 == f.size() ? -f[k] - 1 : f[k]);
    int count = 0;
    for (const auto& f : s.f) count += int(f.size());
    QByteArray o = "; FBX 7.4.0 project file\nFBXHeaderExtension:  {\n\tFBXHeaderVersion: 1003\n\tFBXVersion: 7400\n}\nGlobalSettings:  {\n\tVersion: 1000\n\tProperties70:  {\n";
    o += "\t\tP: \"UpAxis\", \"int\", \"Integer\", \"\"," + QByteArray::number(upAxis) + "\n\t\tP: \"UpAxisSign\", \"int\", \"Integer\", \"\",1\n";
    o += "\t\tP: \"FrontAxis\", \"int\", \"Integer\", \"\"," + QByteArray(upAxis == 2 ? "1" : "2") + "\n\t\tP: \"FrontAxisSign\", \"int\", \"Integer\", \"\"," + QByteArray(upAxis == 2 ? "-1" : "1") + "\n";
    o += "\t\tP: \"CoordAxis\", \"int\", \"Integer\", \"\",0\n\t\tP: \"CoordAxisSign\", \"int\", \"Integer\", \"\",1\n\t\tP: \"UnitScaleFactor\", \"double\", \"Number\", \"\",1\n\t}\n}\n";
    o += "Objects:  {\n\tGeometry: 1000, \"Geometry::s\", \"Mesh\" {\n\t\tVertices: *" + QByteArray::number(int(s.v.size() * 3)) + " {\n\t\t\ta: " + v + "\n\t\t}\n";
    o += "\t\tPolygonVertexIndex: *" + QByteArray::number(count) + " {\n\t\t\ta: " + i + "\n\t\t}\n";
    o += "\t\tLayerElementMaterial: 0 {\n\t\t\tVersion: 101\n\t\t\tName: \"\"\n\t\t\tMappingInformationType: \"AllSame\"\n\t\t\tReferenceInformationType: \"IndexToDirect\"\n\t\t\tMaterials: *1 {\n\t\t\t\ta: 0\n\t\t\t}\n\t\t}\n";
    o += "\t\tLayer: 0 {\n\t\t\tVersion: 100\n\t\t\tLayerElement:  {\n\t\t\t\tType: \"LayerElementMaterial\"\n\t\t\t\tTypedIndex: 0\n\t\t\t}\n\t\t}\n\t}\n";
    o += "\tModel: 2000, \"Model::s\", \"Mesh\" {\n\t\tVersion: 232\n\t\tProperties70:  {\n\t\t\tP: \"Lcl Scaling\", \"Lcl Scaling\", \"\", \"A\",3,3,3\n\t\t}\n\t}\n";
    o += "\tMaterial: 3000, \"Material::m\", \"\" {\n\t\tVersion: 102\n\t\tShadingModel: \"lambert\"\n\t\tProperties70:  {\n\t\t\tP: \"DiffuseColor\", \"Color\", \"\", \"A\",0.8,0.1,0.1\n\t\t}\n\t}\n}\n";
    o += "Connections:  {\n\tC: \"OO\",2000,0\n\tC: \"OO\",1000,2000\n\tC: \"OO\",3000,2000\n}\n";
    return o;
}

// ---- what a loaded model looks like
static QVector3D pos(const Mesh& m, int i) { return QVector3D(m.vertices[i * 9], m.vertices[i * 9 + 1], m.vertices[i * 9 + 2]); }
static QVector3D nrm(const Mesh& m, int i) { return QVector3D(m.vertices[i * 9 + 3], m.vertices[i * 9 + 4], m.vertices[i * 9 + 5]); }
static QVector3D colour(const Mesh& m, int i) { return QVector3D(m.vertices[i * 9 + 6], m.vertices[i * 9 + 7], m.vertices[i * 9 + 8]); }
static int corners(const Mesh& m) { return int(m.vertices.size() / 9); }
static QVector3D top(const Mesh& m) { QVector3D t(0, -1e9f, 0); for (int i = 0; i < corners(m); ++i) if (pos(m, i).y() > t.y()) t = pos(m, i); return t; }
static float lowest(const Mesh& m) { float y = 1e9f; for (int i = 0; i < corners(m); ++i) y = std::min(y, pos(m, i).y()); return y; }
static float areaAt(const Mesh& m, float y, float within = 2e-3f)   // the area of the triangles lying at that height
{
    float a = 0.f;
    for (int t = 0; t + 2 < corners(m); t += 3) {
        const QVector3D A = pos(m, t), B = pos(m, t + 1), C = pos(m, t + 2);
        if (std::abs(A.y() - y) < within && std::abs(B.y() - y) < within && std::abs(C.y() - y) < within) a += 0.5f * QVector3D::crossProduct(B - A, C - A).length();
    }
    return a;
}
static float volume(const Mesh& m)   // signed: negative when the model was mirrored
{
    double v = 0;
    for (int t = 0; t + 2 < corners(m); t += 3) v += QVector3D::dotProduct(pos(m, t), QVector3D::crossProduct(pos(m, t + 1), pos(m, t + 2))) / 6.0;
    return float(v);
}
static float maxDifference(const Mesh& a, const Mesh& b)
{
    if (a.vertices.size() != b.vertices.size()) return 1e9f;
    float d = 0.f;
    for (int i = 0; i < a.vertices.size(); ++i) d = std::max(d, std::abs(a.vertices[i] - b.vertices[i]));
    return d;
}
static bool upright(const Mesh& m)   // the cone: tip on the axis at statue height, the round foot on the floor
{
    const QVector3D t = top(m);
    return std::abs(t.y() - ModelLibrary::kHeight) < 1e-3f && std::abs(t.x()) < 1e-3f && std::abs(t.z()) < 1e-3f && std::abs(lowest(m)) < 1e-4f && areaAt(m, 0.f) > 0.3f;
}
static QString say(const Mesh& m, const QString& err) { return m.triangles ? m.describe() : QStringLiteral("not loaded: ") + err; }

int main()
{
    QTemporaryDir tmp;
    const QString d = tmp.path() + '/';
    QString err;
    auto load = [&](const QString& name, ModelLibrary::Up up = ModelLibrary::UpAuto) { err.clear(); return ModelLibrary::load(d + name, &err, up); };

    // ---- which way is up
    put(d + "cone_y.obj", objText(cone()));
    put(d + "cone_z.obj", objText(zUp(cone())));
    put(d + "cone_z.stl", stlBinary(zUp(cone())));
    put(d + "cone_y.stl", stlBinary(cone()));
    Mesh m = load("cone_y.obj");
    const Mesh coneY = m;
    check(m.up == ModelLibrary::UpY && m.upWhy == 0 && upright(m), "a Y-up OBJ with a flat base stands on it", say(m, err));
    check(m.triangles == 48 && std::abs(m.size.y() - 1.5f) < 1e-4f && std::abs(m.size.x() - 0.75f) < 1e-3f, "it is scaled to statue height",
          QStringLiteral("%1 triangles, %2 x %3 x %4").arg(m.triangles).arg(m.size.x()).arg(m.size.y()).arg(m.size.z()));
    m = load("cone_z.obj");
    check(m.up == ModelLibrary::UpZ && m.upWhy == 0 && upright(m), "a Z-up OBJ is stood on its flat base (not laid on its side)", say(m, err));
    check(maxDifference(m, coneY) < 1e-4f, "and is then the same statue as the Y-up file", QString::number(maxDifference(m, coneY)));
    m = load("cone_z.stl");
    check(m.up == ModelLibrary::UpZ && upright(m), "a Z-up STL stands on its base", say(m, err));
    m = load("cone_y.stl");
    check(m.up == ModelLibrary::UpY && m.upWhy == 0 && upright(m), "a Y-up STL is stood on its flat base", say(m, err));
    put(d + "ball.obj", objText(sphere(16, 24)));
    put(d + "ball.stl", stlBinary(sphere(16, 24)));
    m = load("ball.obj");
    check(m.up == ModelLibrary::UpY && m.upWhy == 1, "without a flat side an OBJ is taken as Y-up", say(m, err));
    m = load("ball.stl");
    check(m.up == ModelLibrary::UpZ && m.upWhy == 1, "and an STL as Z-up", say(m, err));
    put(d + "cube.obj", objText(box(QVector3D(-1, -1, -1), QVector3D(1, 1, 1))));
    m = load("cube.obj");
    check(m.up == ModelLibrary::UpY && m.triangles == 12, "flat on every side: the file type decides (a cube stays as it is)", say(m, err));
    Shape table = box(QVector3D(-0.6f, 0.74f, -0.4f), QVector3D(0.6f, 0.8f, 0.4f));
    for (const QVector3D& leg : {QVector3D(-0.5f, 0, -0.3f), QVector3D(0.4f, 0, -0.3f), QVector3D(-0.5f, 0, 0.2f), QVector3D(0.4f, 0, 0.2f)})
        table = box(leg, leg + QVector3D(0.1f, 0.74f, 0.1f), table);
    put(d + "table_z.obj", objText(zUp(table)));
    m = load("table_z.obj");
    check(m.up == ModelLibrary::UpZ && m.upWhy == 3 && areaAt(m, m.size.y()) > areaAt(m, 0.f) * 5.f && areaAt(m, 0.f) > 0.f,
          "a Z-up table (flat on top, legs below) stands on its legs, not on its top", say(m, err));
    m = load("cone_z.obj", ModelLibrary::UpY);
    check(m.up == ModelLibrary::UpY && m.upWhy == 2 && !upright(m), "an axis chosen by hand is used as it is", say(m, err));
    m = load("cone_z.obj", ModelLibrary::UpNegZ);
    check(std::abs(m.size.y() - 1.5f) < 1e-3f && areaAt(m, 1.5f) > 0.3f, "\"Z is down\" stands it on its tip", say(m, err));
    Shape tetra;
    tetra.v = {QVector3D(0, 0, 0), QVector3D(1, 0, 0), QVector3D(0, 2, 0), QVector3D(0, 0, 3)};
    tetra.f = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
    put(d + "tetra.obj", objText(tetra));
    bool turnsOnly = true;
    QString sizes;
    for (int u = 1; u <= 6; ++u) {
        m = load("tetra.obj", ModelLibrary::Up(u));
        turnsOnly = turnsOnly && m.triangles == 4 && volume(m) > 0.f;
        sizes += QStringLiteral("%1 %2x%3x%4  ").arg(ModelLibrary::upName(ModelLibrary::Up(u))).arg(m.size.x(), 0, 'f', 2).arg(m.size.y(), 0, 'f', 2).arg(m.size.z(), 0, 'f', 2);
    }
    check(turnsOnly, "all six choices turn the model; none mirrors it", sizes);
    check(ModelLibrary::upFromName("z") == ModelLibrary::UpZ && ModelLibrary::upFromName("-X") == ModelLibrary::UpNegX && ModelLibrary::upFromName("auto") == ModelLibrary::UpAuto,
          "axis names are read (auto, y, z, x, -y, -z, -x)");

    // ---- OBJ details
    put(d + "forms.obj", "v 0 0 0\nv 1 0 0\nv 1 0 1\nv 0 0 1\nv 0.5 1 0.5\nvt 0 0\nvn 0 -1 0\nvn 0 1 0\n"
                         "f 1/1/1 2/1/1 3/1/1 4/1/1\nf -5//2 -1//2 -4//2\nf 2 3 5\nf 3/1 4/1 5/1\nf 4 1 5\n");
    m = load("forms.obj");
    check(m.triangles == 6 && upright(m) == false && std::abs(top(m).y() - 1.4f) < 1e-3f, "OBJ: polygons, v/vt/vn in every form and negative numbers", say(m, err));
    put(d + "pic.png", [] { QImage img(8, 8, QImage::Format_RGB32); img.fill(qRgb(0, 200, 0)); QByteArray b; QBuffer buf(&b); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG"); return b; }());
    put(d + "my paints.mtl", "newmtl plain red\nKd 1 0 0\nnewmtl pictured\nKd 0.8 0.8 0.8\nmap_Kd -s 1 1 1 pic.png\n");
    put(d + "painted.obj", "mtllib my paints.mtl\nv 0 0 0\nv 1 0 0\nv 1 0 1\nv 0 0 1\nv 0.5 1 0.5\nvt 0.5 0.5\nusemtl plain red\nf 1 2 3 4\nusemtl pictured\nf 1/1 2/1 5/1\nf 2/1 3/1 5/1\nf 3/1 4/1 5/1\nf 4/1 1/1 5/1\n");
    m = load("painted.obj");
    bool footRed = m.triangles > 0, sidesGreen = m.triangles > 0;   // the foot uses the plain red material, the sides the pictured one
    for (int t = 0; t + 2 < corners(m); t += 3) {
        const bool foot = pos(m, t).y() < 1e-4f && pos(m, t + 1).y() < 1e-4f && pos(m, t + 2).y() < 1e-4f;
        for (int k = 0; k < 3; ++k) {
            if (foot) footRed = footRed && (colour(m, t + k) - QVector3D(1, 0, 0)).length() < 0.01f;
            else sidesGreen = sidesGreen && (colour(m, t + k) - QVector3D(0, 200 / 255.f, 0)).length() < 0.02f;
        }
    }
    check(m.hasColours && m.sourceTriangles == 6 && footRed && sidesGreen, "OBJ: colours from its material file (a colour, and a picture)", say(m, err));
    {   // TGA pictures (common beside OBJ and FBX models; read by the player itself)
        auto header = [](int type, int w, int h, int bits, int desc) {
            QByteArray t(18, '\0');
            t[2] = char(type); t[12] = char(w); t[14] = char(h); t[16] = char(bits); t[17] = char(desc);
            return t;
        };
        // 2 x 2, true colour, rows from the bottom up: bottom row red, green; top row blue, white (stored blue-green-red)
        QByteArray plain = header(2, 2, 2, 24, 0);
        plain.append("\x00\x00\xff" "\x00\xff\x00" "\xff\x00\x00" "\xff\xff\xff", 12);
        // the same picture packed in runs, 32 bits, rows from the top down
        QByteArray packed = header(10, 2, 2, 32, 0x28);
        packed.append("\x01" "\xff\x00\x00\xff" "\xff\xff\xff\xff", 9);   // two different pixels: blue, white
        packed.append("\x01" "\x00\x00\xff\xff" "\x00\xff\x00\xff", 9);   // two more: red, green
        QByteArray grey = header(11, 4, 1, 8, 0x20);
        grey.append("\x83\x80", 2);                                                // one run: four pixels of mid grey
        const QImage a = modelPicture(plain), b = modelPicture(packed), g = modelPicture(grey);
        const bool right = a.size() == QSize(2, 2) && a.pixel(0, 0) == qRgb(0, 0, 255) && a.pixel(1, 0) == qRgb(255, 255, 255) && a.pixel(0, 1) == qRgb(255, 0, 0) && a.pixel(1, 1) == qRgb(0, 255, 0);
        check(right && b.size() == QSize(2, 2) && a.convertToFormat(QImage::Format_RGB32) == b.convertToFormat(QImage::Format_RGB32) && g.size() == QSize(4, 1) && g.pixel(3, 0) == qRgb(128, 128, 128),
              "TGA pictures: true colour and grey, plain and packed in runs, rows from the bottom or the top");
        check(modelPicture(plain.left(20)).isNull() && modelPicture(QByteArray("not a picture at all")).isNull(), "a TGA cut short, or bytes that are no picture, give no picture");
        put(d + "crate paint.tga", plain);
        put(d + "crate.mtl", "newmtl paint\nmap_Kd crate paint.tga\n");
        put(d + "crate.obj", "mtllib crate.mtl\nusemtl paint\nv 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nf 1/1 2/2 3/3 4/4\n");
        m = load("crate.obj");
        // the picture's bottom-left corner (red) is at the model's bottom-left, its top-right (white) at the top-right
        // (sampled in the middle of each quarter of the model: at the very edges a picture that repeats blends with its other side)
        QVector3D low(-1, -1, -1), high(-1, -1, -1);
        float lowD = 1e9f, highD = 1e9f;
        for (int i = 0; i < corners(m); ++i) {
            const float dl = (pos(m, i) - QVector3D(-0.35f, 0.35f, 0)).length(), dh = (pos(m, i) - QVector3D(0.35f, 1.05f, 0)).length();
            if (dl < lowD) { lowD = dl; low = colour(m, i); }
            if (dh < highD) { highD = dh; high = colour(m, i); }
        }
        check(m.hasColours && (low - QVector3D(1, 0, 0)).length() < 0.05f && (high - QVector3D(1, 1, 1)).length() < 0.05f,
              "OBJ: a TGA picture named by the material file is wrapped the right way up", m.triangles ? QStringLiteral("bottom left %1 %2 %3, top right %4 %5 %6")
              .arg(low.x(), 0, 'f', 2).arg(low.y(), 0, 'f', 2).arg(low.z(), 0, 'f', 2).arg(high.x(), 0, 'f', 2).arg(high.y(), 0, 'f', 2).arg(high.z(), 0, 'f', 2) : err);
    }
    put(d + "broken.obj", "v 0 0 0\nv 1 0 0\nf 1 2 9\n");
    m = load("broken.obj");
    check(m.triangles == 0 && err.contains("missing vertex"), "OBJ: a face that refers to a missing vertex is refused, with the reason", err);
    for (const char* loc : {"de_DE.UTF-8", "fr_FR.UTF-8", "de_DE", "German"}) if (std::setlocale(LC_ALL, loc)) break;
    m = load("cone_y.obj");
    const Mesh p = (put(d + "comma.ply", ply(cone(), 0)), load("comma.ply"));
    check(maxDifference(m, coneY) < 1e-6f && maxDifference(p, coneY) < 1e-4f, "numbers are read the same whatever the system's language (a decimal comma)",
          QString::fromLatin1(std::setlocale(LC_NUMERIC, nullptr)));
    std::setlocale(LC_ALL, "C");

    // ---- PLY
    Mesh flavours[3];
    for (int f = 0; f < 3; ++f) { put(d + QStringLiteral("cone%1.ply").arg(f), ply(cone(), f)); flavours[f] = load(QStringLiteral("cone%1.ply").arg(f)); }
    check(flavours[0].triangles == 48 && maxDifference(flavours[0], coneY) < 1e-4f && maxDifference(flavours[1], coneY) < 1e-5f && maxDifference(flavours[2], coneY) < 1e-5f,
          "PLY: text, binary little-endian and binary big-endian all give the model the OBJ gives",
          QStringLiteral("%1 triangles; differences %2, %3, %4; %5").arg(flavours[0].triangles).arg(maxDifference(flavours[0], coneY)).arg(maxDifference(flavours[1], coneY)).arg(maxDifference(flavours[2], coneY)).arg(err));
    put(d + "cone_z.ply", ply(zUp(cone()), 1));
    m = load("cone_z.ply");
    check(m.up == ModelLibrary::UpZ && upright(m), "PLY: a Z-up model is stood on its base", say(m, err));
    put(d + "colour.ply", ply(cone(), 1, true));
    m = load("colour.ply");
    check(m.hasColours && (colour(m, 0) - QVector3D(1.f, 128 / 255.f, 0.f)).length() < 1e-3f, "PLY: colours", m.triangles ? QStringLiteral("%1 %2 %3").arg(colour(m, 0).x()).arg(colour(m, 0).y()).arg(colour(m, 0).z()) : err);
    put(d + "cube.ply", ply(box(QVector3D(-1, -1, -1), QVector3D(1, 1, 1)), 0));
    m = load("cube.ply");
    check(m.triangles == 12, "PLY: faces of four corners", say(m, err));
    put(d + "strip.ply", "ply\nformat ascii 1.0\nelement vertex 6\nproperty float x\nproperty float y\nproperty float z\nelement tristrips 1\nproperty list int int vertex_indices\nend_header\n"
                         "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 2 0.5\n1 2 0.5\n9 0 1 2 3 -1 2 3 4 5\n");
    m = load("strip.ply");
    check(m.triangles == 4, "PLY: triangle strips, with a restart", say(m, err));
    QByteArray whole = ply(cone(), 1);
    put(d + "extras.ply", "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nelement face 4\nproperty list uchar int vertex_indices\nend_header\n"
                          "0 0 0 255 0 0\n1 0 0\n0 1 0 7\n0 0 1\n3 0 2 1 9 9 9\n3 0 1 3\n\n3 0 3 2 1 1\n3 1 2 3\n");
    m = load("extras.ply");
    check(m.triangles == 4, "PLY (text): more on a line than the header says does not upset the lines after it", say(m, err));
    put(d + "flat_xy.ply", "ply\nformat ascii 1.0\nelement vertex 4\nproperty float x\nproperty float y\nproperty float z\nelement face 2\nproperty list uchar int vertex_index\nend_header\n-4 -4 0\n-4 4 0\n4 -4 0\n4 4 0\n3 0 2 1\n3 1 2 3\n");
    put(d + "flat_xz.obj", "v -4 0 -4\nv -4 0 4\nv 4 0 -4\nv 4 0 4\nf 1 3 2\nf 2 3 4\n");
    m = load("flat_xy.ply");
    const Mesh fz = load("flat_xz.obj");
    check(m.triangles == 2 && std::abs(m.size.y() - 1.4f) < 1e-3f && fz.triangles == 2 && std::abs(fz.size.y() - 1.4f) < 1e-3f, "a flat cut-out is stood on its edge, whichever way it lies in its file",
          QStringLiteral("%1 | %2").arg(say(m, err), fz.triangles ? fz.describe() : QStringLiteral("not loaded")));
    put(d + "cut.ply", whole.left(whole.size() - 40));
    m = load("cut.ply");
    check(m.triangles == 0 && err.contains("cut short"), "PLY: a file cut short is refused, with the reason", err);
    put(d + "nothead.ply", "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\n");
    m = load("nothead.ply");
    check(m.triangles == 0 && err.contains("not a PLY"), "PLY: a file without a complete header is refused", err);
    put(d + "badface.ply", "ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar int vertex_indices\nend_header\n0 0 0\n1 0 0\n0 1 0\n3 0 1 7\n");
    m = load("badface.ply");
    check(m.triangles == 0 && err.contains("missing vertex"), "PLY: a face that refers to a missing vertex is refused", err);

    // ---- point clouds
    Shape ball = sphere(60, 90);   // 5,490 points
    put(d + "points.ply", ply(ball, 1, true, false));
    m = load("points.ply");
    double radial = 0;
    for (int i = 0; i < corners(m); i += 6) {
        const QVector3D c = (pos(m, i) + pos(m, i + 2)) * 0.5f - QVector3D(0, m.size.y() * 0.5f, 0);
        radial += std::abs(QVector3D::dotProduct(nrm(m, i), c.normalized()));
    }
    radial /= std::max(1, corners(m) / 6);
    check(m.points && m.triangles == int(ball.v.size()) * 2 && m.hasColours, "PLY: a file with points only is a point cloud: a small square for each point",
          m.triangles ? QStringLiteral("%1; %2 triangles").arg(m.describe()).arg(m.triangles) : err);
    check(radial > 0.97, "the squares lie in the surface (normals from the neighbouring points)", QStringLiteral("mean alignment %1").arg(radial, 0, 'f', 3));
    Shape many;
    for (int i = 0; i < 300000; ++i) {
        const double t = std::acos(1.0 - 2.0 * (i + 0.5) / 300000.0), ph = i * 2.399963229728653;
        many.v.push_back(QVector3D(float(std::sin(t) * std::cos(ph)), float(std::cos(t)), float(std::sin(t) * std::sin(ph))));
    }
    for (int i = 0; i < 20; ++i) many.v.push_back(QVector3D(50.f + i, -70.f, 30.f * i));   // strays, far from the model
    put(d + "many.ply", ply(many, 1, false, false));
    m = load("many.ply");
    check(m.points && m.triangles <= ModelLibrary::kMaxPoints * 2 && m.triangles >= 200000 && m.sourcePoints == 300020, "a large point cloud is thinned to what can be drawn",
          m.triangles ? m.describe() : err);
    check(std::abs(m.size.x() - m.size.y()) < 0.05f && std::abs(m.size.z() - m.size.y()) < 0.05f, "stray points far from the model do not shrink it to a speck",
          QStringLiteral("%1 x %2 x %3").arg(m.size.x()).arg(m.size.y()).arg(m.size.z()));

    // ---- glTF / GLB
    const float h = std::sqrt(0.5f);
    put(d + "nodes.glb", glbOf(zUp(cone()), QJsonObject{{"rotation", QJsonArray{-h, 0, 0, h}}, {"scale", QJsonArray{7, 7, 7}}, {"translation", QJsonArray{3, 0, -2}}}, QJsonObject(), true));
    m = load("nodes.glb");
    check(m.triangles == 48 && upright(m) && m.up == ModelLibrary::UpY, "GLB: the scene's turn, scale and move are applied (a Z-up mesh set upright by its node)", say(m, err));
    check(m.hasColours && (colour(m, 0) - QVector3D(1, 0, 0)).length() < 1e-3f, "GLB: colours per vertex");
    QJsonObject green{{"materials", QJsonArray{QJsonObject{{"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", QJsonArray{0.0, 0.5, 0.0, 1.0}}}}}}}};
    put(d + "green.glb", glbOf(cone(), QJsonObject(), green));
    m = load("green.glb");
    check(m.hasColours && std::abs(colour(m, 0).y() - std::pow(0.5f, 1 / 2.2f)) < 1e-3f && colour(m, 0).x() < 1e-3f, "GLB: a material's colour (stored linear, shown as on a screen)",
          m.triangles ? QStringLiteral("green %1").arg(colour(m, 0).y()) : err);
    QImage halves(16, 16, QImage::Format_RGB32);
    halves.fill(qRgb(255, 0, 0));
    for (int y = 8; y < 16; ++y) for (int x = 0; x < 16; ++x) halves.setPixel(x, y, qRgb(0, 0, 255));
    QByteArray png;
    { QBuffer buf(&png); buf.open(QIODevice::WriteOnly); halves.save(&buf, "PNG"); }
    const Shape c = cone();
    std::vector<QPointF> uv;
    for (const QVector3D& q : c.v) uv.push_back(QPointF(0.5, q.y() > 1.f ? 0.25 : 0.75));   // the tip in the picture's upper (red) half, the foot in the lower (blue)
    put(d + "pictured.glb", glbOf(c, QJsonObject(), QJsonObject(), false, &uv, png));
    m = load("pictured.glb");
    bool tipRed = m.triangles > 0, footBlue = m.triangles > 0;
    for (int i = 0; i < corners(m); ++i) {
        if (pos(m, i).y() > 1.4f) tipRed = tipRed && colour(m, i).x() > 0.95f && colour(m, i).z() < 0.05f;
        if (pos(m, i).y() < 0.01f) footBlue = footBlue && colour(m, i).z() > 0.95f && colour(m, i).x() < 0.05f;
    }
    check(m.hasColours && tipRed && footBlue, "GLB: a picture wrapped around the model colours it (red tip, blue foot)", say(m, err));
    // The cone has 48 triangles, each from the foot to the tip; the picture changes from blue to red halfway up. Colours at the
    // corners alone would give a smooth blend all the way; with the triangles divided, the change is where the picture has it.
    int sharp = 0, blended = 0;
    for (int i = 0; i < corners(m); ++i) {
        const float y = pos(m, i).y(), r = colour(m, i).x(), b = colour(m, i).z();
        if (pos(m, i).x() * pos(m, i).x() + pos(m, i).z() * pos(m, i).z() < 1e-4f && y < 0.01f) continue;   // (the foot's centre)
        if (y > 0.1f && y < 0.6f) { if (b > 0.9f && r < 0.1f) ++sharp; else ++blended; }
        if (y > 0.9f && y < 1.4f) { if (r > 0.9f && b < 0.1f) ++sharp; else ++blended; }
    }
    check(m.triangles > 48 * 100 && m.sourceTriangles == 48 && sharp > 1000 && blended == 0, "a model of few triangles is divided, so the picture shows between its corners too",
          QStringLiteral("%1 triangles drawn for 48; %2 samples away from the change are pure blue or red, %3 are not").arg(m.triangles).arg(sharp).arg(blended));
    {   // no cracks: every divided side has the same points from both triangles that share it
        QHash<QString, int> seen;
        for (int i = 0; i < corners(m); ++i) { const QVector3D q = pos(m, i); ++seen[QStringLiteral("%1 %2 %3").arg(q.x(), 0, 'g', 9).arg(q.y(), 0, 'g', 9).arg(q.z(), 0, 'g', 9)]; }
        int lone = 0;
        // A point inside a triangle belongs to 6 small triangles; a point on a side to 3 from each of the two triangles that share
        // the side, 6 in all, if both put it in exactly the same place (3 and 3 apart if not); the cone's own corners to 4 or more.
        for (auto it = seen.begin(); it != seen.end(); ++it) if (it.value() < 4) ++lone;
        check(lone == 0, "the divided triangles meet exactly (no cracks along the original edges)", QStringLiteral("%1 points that only one of two neighbours uses").arg(lone));
    }
    {   // a GLB whose first chunk is not padded to a multiple of four bytes (seen in files from the wild)
        QByteArray g = glbOf(cone(), QJsonObject());
        const quint32 jsonLen = qFromLittleEndian<quint32>(g.constData() + 12);
        int spaces = 0;
        while (spaces < 3 && g[20 + int(jsonLen) - 1 - spaces] == ' ') ++spaces;
        if (spaces == 0) {   // none to drop: lengthen the document by one space, which leaves it unpadded
            g.insert(20 + int(jsonLen), ' ');
            qToLittleEndian<quint32>(jsonLen + 1, g.data() + 12);
        } else {
            g.remove(20 + int(jsonLen) - 1, 1);
            qToLittleEndian<quint32>(jsonLen - 1, g.data() + 12);
        }
        qToLittleEndian<quint32>(quint32(g.size()), g.data() + 8);
        put(d + "unpadded.glb", g);
        m = load("unpadded.glb");
        check(m.triangles == 48 && upright(m), "GLB: a first chunk that is not padded to four bytes is read all the same", say(m, err));
    }
    put(d + "draco.glb", glb(QJsonObject{{"asset", QJsonObject{{"version", "2.0"}}}, {"extensionsRequired", QJsonArray{"KHR_draco_mesh_compression"}}}, QByteArray()));
    m = load("draco.glb");
    check(m.triangles == 0 && err.contains("Draco"), "GLB: a Draco-compressed file is refused, with the reason", err);
    put(d + "meshopt.glb", glb(QJsonObject{{"asset", QJsonObject{{"version", "2.0"}}}, {"extensionsRequired", QJsonArray{"KHR_meshopt_compression"}}}, QByteArray()));
    m = load("meshopt.glb");
    check(m.triangles == 0 && err.contains("meshopt"), "GLB: and a meshopt-compressed one", err);
    put(d + "junk.glb", "glTF\x02\0\0\0 not really");
    m = load("junk.glb");
    check(m.triangles == 0 && !err.isEmpty(), "GLB: a damaged file is refused", err);

    // ---- FBX
    put(d + "cone_z.fbx", fbxText(zUp(cone()), 2));
    m = load("cone_z.fbx");
    check(m.triangles == 48 && upright(m) && m.up == ModelLibrary::UpY, "FBX (text): a file that says Z is up comes out upright", say(m, err));
    check(m.hasColours && (colour(m, 0) - QVector3D(0.8f, 0.1f, 0.1f)).length() < 1e-3f, "FBX: its material's colour",
          m.triangles ? QStringLiteral("%1 %2 %3").arg(colour(m, 0).x()).arg(colour(m, 0).y()).arg(colour(m, 0).z()) : err);
    put(d + "cone_y.fbx", fbxText(cone(), 1));
    m = load("cone_y.fbx");
    check(m.triangles == 48 && upright(m), "FBX: and one that says Y is up", say(m, err));
    put(d + "junk.fbx", "Kaydara FBX Binary  \0\x1a\0 nothing more");
    m = load("junk.fbx");
    check(m.triangles == 0 && err.contains("FBX"), "FBX: a damaged file is refused, with the reason", err);

    // ---- normals
    put(d + "cube.stl", stlBinary(box(QVector3D(-1, -1, -1), QVector3D(1, 1, 1))));
    m = load("cube.stl");
    bool crisp = m.triangles == 12;
    for (int i = 0; i < corners(m); ++i) { const QVector3D n = nrm(m, i); crisp = crisp && std::max({std::abs(n.x()), std::abs(n.y()), std::abs(n.z())}) > 0.999f; }
    check(crisp, "a box keeps its edges: every corner's normal is its own face's");
    m = load("ball.stl");
    float least = 1.f;
    for (int i = 0; i < corners(m); ++i) least = std::min(least, QVector3D::dotProduct(nrm(m, i), (pos(m, i) - QVector3D(0, m.size.y() * 0.5f, 0)).normalized()));
    check(m.triangles > 0 && least > 0.98f, "a round shape is smooth: every normal points away from the middle", QStringLiteral("least alignment %1").arg(least, 0, 'f', 3));

    // ---- too many triangles to draw
    const Shape dense = sphere(500, 502);   // 500,996 triangles
    put(d + "dense.stl", stlBinary(dense));
    m = load("dense.stl");
    float off = 0.f;
    for (int i = 0; i < corners(m); ++i) off = std::max(off, std::abs((pos(m, i) - QVector3D(0, m.size.y() * 0.5f, 0)).length() - m.size.y() * 0.5f));
    check(m.sourceTriangles == 500996 && m.triangles <= ModelLibrary::kMaxTriangles && m.triangles >= 150000, "a model with more triangles than can be drawn is simplified, not skipped",
          m.triangles ? m.describe() : err);
    check(m.triangles > 0 && off < 0.01f, "and keeps its shape", QStringLiteral("farthest from the true surface: %1 of a 0.7 radius").arg(off, 0, 'f', 4));
    err.clear();
    m = ModelLibrary::load(d + "dense.stl", &err, ModelLibrary::UpAuto, true);
    const Mesh lc = ModelLibrary::load(d + "many.ply", &err, ModelLibrary::UpAuto, true);
    const Mesh lp = ModelLibrary::load(d + "pictured.glb", &err, ModelLibrary::UpAuto, true);
    check(m.triangles > 40000 && m.triangles <= ModelLibrary::kLightTriangles && lc.triangles <= ModelLibrary::kLightPoints * 2 && lc.triangles > 100000 && lp.triangles > 48 && lp.triangles <= 40000,
          "without a graphics card the limits are lower (fewer triangles, points and divisions)",
          QStringLiteral("%1 triangles; %2 points; the pictured cone %3 triangles").arg(m.triangles).arg(lc.triangles / 2).arg(lp.triangles));
    check(coneY.describe().contains("48") && coneY.describe().contains("Y up") && coneY.describe().startsWith("cone_y.obj"), "each model is described in a line", coneY.describe());

    std::printf("\n%s\n", fails ? "model checks FAILED" : "All model checks passed");
    return fails ? 1 : 0;
}
