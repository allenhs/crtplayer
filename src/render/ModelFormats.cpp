// Readers for the 3D model formats of the 90s CG room: OBJ (with its MTL colours and
// textures), STL, PLY, glTF / GLB and FBX (through ufbx). Each fills a RawModel.
#include "ModelRaw.h"
#include "ModelLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix4x4>
#include <QQuaternion>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <functional>

#include "ufbx.h"

namespace {
const QVector3D kGrey(0.8f, 0.8f, 0.8f);

// The file's bytes: mapped when possible, read otherwise.
struct FileBytes {
    QFile file;
    QByteArray owned;
    const char* begin = nullptr;
    const char* end = nullptr;
    bool open(const QString& path)
    {
        file.setFileName(path);
        if (!file.open(QIODevice::ReadOnly)) return false;
        const qint64 n = file.size();
        uchar* m = n > 0 ? file.map(0, n) : nullptr;
        if (m) { begin = reinterpret_cast<const char*>(m); end = begin + n; }
        else { owned = file.readAll(); begin = owned.constData(); end = begin + owned.size(); }
        return true;
    }
    qint64 size() const { return end - begin; }
};

inline bool blank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

// The next number at p, never reading past end. False at the end, or when what is there
// is not a number (that word is skipped). Independent of the system's language settings.
bool number(const char*& p, const char* end, double& out)
{
    while (p < end && blank(*p)) ++p;
    if (p >= end) return false;
    const char* q = *p == '+' ? p + 1 : p;
    const auto r = std::from_chars(q, end, out);
    if (r.ec == std::errc()) { p = r.ptr; return true; }
    if (r.ec == std::errc::result_out_of_range) { p = r.ptr; out = 0.0; return true; }
    while (p < end && !blank(*p)) ++p;
    return false;
}

bool integer(const char*& p, const char* end, qint64& out)
{
    const char* q = p;
    bool neg = false;
    if (q < end && (*q == '-' || *q == '+')) { neg = *q == '-'; ++q; }
    if (q >= end || *q < '0' || *q > '9') return false;
    qint64 v = 0;
    while (q < end && *q >= '0' && *q <= '9') { v = v * 10 + (*q - '0'); if (v > (qint64(1) << 40)) return false; ++q; }
    out = neg ? -v : v;
    p = q;
    return true;
}

// A picture wrapped around a model: kept at a moderate size (it is sampled at the
// model's corners and at points in between, not drawn pixel for pixel). Returns its
// number among the model's pictures, or -1.
int addTexture(RawModel& raw, const QImage& src)
{
    if (src.isNull() || raw.textures.size() >= 64) return -1;
    QImage s = src;
    if (std::max(s.width(), s.height()) > 1024) s = s.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    raw.textures.append(s.convertToFormat(QImage::Format_RGB32));
    return int(raw.textures.size()) - 1;
}

// TGA: true colour, grey or with a colour table; plain or run-length packed.
QImage tgaPicture(const QByteArray& d)
{
    if (d.size() < 18) return QImage();
    const uchar* h = reinterpret_cast<const uchar*>(d.constData());
    const int idLen = h[0], mapType = h[1], type = h[2], mapLen = h[5] | (h[6] << 8), mapBits = h[7];
    const int w = h[12] | (h[13] << 8), ht = h[14] | (h[15] << 8), bits = h[16], desc = h[17];
    const bool rle = type >= 9;
    const int kind = type & 7;   // 1 colour table, 2 true colour, 3 grey
    if ((kind != 1 && kind != 2 && kind != 3) || (type != kind && type != kind + 8) || w <= 0 || ht <= 0 || w > 16384 || ht > 16384) return QImage();
    if ((kind == 2 && bits != 15 && bits != 16 && bits != 24 && bits != 32) || (kind != 2 && bits != 8)) return QImage();
    if (kind == 1 && (mapType != 1 || (mapBits != 24 && mapBits != 32) || mapLen <= 0)) return QImage();
    const int mapBytes = mapType == 1 ? mapLen * ((mapBits + 7) / 8) : 0, px = (bits + 7) / 8;
    const qint64 start = qint64(18) + idLen + mapBytes;
    if (start > d.size() || (kind == 1 && mapBits != 24 && mapBits != 32)) return QImage();
    const uchar* map = h + 18 + idLen;
    const uchar* p = h + start;
    const uchar* end = h + d.size();
    if (!rle && qint64(w) * ht * px > end - p) return QImage();
    QImage img(w, ht, QImage::Format_RGB32);
    if (img.isNull()) return QImage();
    img.fill(Qt::black);
    auto colour = [&](const uchar* q) -> QRgb {
        if (kind == 3) return qRgb(q[0], q[0], q[0]);
        if (kind == 1) { const int i = q[0]; if (i >= mapLen) return qRgb(0, 0, 0); const uchar* m = map + i * (mapBits / 8); return qRgb(m[2], m[1], m[0]); }
        if (px == 2) { const int v = q[0] | (q[1] << 8); return qRgb(((v >> 10) & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, (v & 31) * 255 / 31); }
        return qRgb(q[2], q[1], q[0]);
    };
    const qint64 total = qint64(w) * ht;
    qint64 i = 0;
    auto put = [&](QRgb c) {
        const int x = int(i % w), y = int(i / w);
        img.setPixel(desc & 0x10 ? w - 1 - x : x, desc & 0x20 ? y : ht - 1 - y, c);
        ++i;
    };
    if (!rle) { for (; i < total; p += px) put(colour(p)); return img; }
    while (i < total && p < end) {
        const int head = *p++, count = (head & 127) + 1;
        if (head & 128) {
            if (end - p < px) break;
            const QRgb c = colour(p);
            p += px;
            for (int k = 0; k < count && i < total; ++k) put(c);
        } else {
            for (int k = 0; k < count && i < total; ++k) { if (end - p < px) return img; put(colour(p)); p += px; }
        }
    }
    return img;
}

// A texture file named by a model, looked for next to the model.
QImage pictureFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() > (256 << 20)) return QImage();
    return modelPicture(f.readAll());
}
QImage findTexture(const QString& modelDir, QString name)
{
    name.replace('\\', '/');
    name = name.trimmed();
    if (name.isEmpty()) return QImage();
    const QString base = QFileInfo(name).fileName();
    QStringList tries;
    if (QFileInfo(name).isRelative()) tries << modelDir + '/' + name;
    else tries << name;
    tries << modelDir + '/' + base << modelDir + "/textures/" + base << modelDir + "/Textures/" + base;
    for (const QString& t : tries) {
        if (!QFileInfo(t).isFile()) continue;
        const QImage img = pictureFile(t);
        if (!img.isNull()) return img;
    }
    for (const QFileInfo& fi : QDir(modelDir).entryInfoList(QDir::Files))   // the same name in other capitals
        if (fi.fileName().compare(base, Qt::CaseInsensitive) == 0) { const QImage img = pictureFile(fi.absoluteFilePath()); if (!img.isNull()) return img; }
    return QImage();
}

inline float toLinear(float c) { return std::pow(std::clamp(c, 0.f, 1.f), 2.2f); }
inline float toDisplay(float c) { return std::pow(std::clamp(c, 0.f, 1.f), 1.f / 2.2f); }
inline QVector3D toLinear(const QVector3D& c) { return QVector3D(toLinear(c.x()), toLinear(c.y()), toLinear(c.z())); }
inline QVector3D toDisplay(const QVector3D& c) { return QVector3D(toDisplay(c.x()), toDisplay(c.y()), toDisplay(c.z())); }

QString tooMany() { return QStringLiteral("too many triangles (more than %1 million)").arg(ModelLibrary::kMaxFileTriangles / 1000000); }

// ---------------------------------------------------------------- OBJ
struct ObjMaterial {
    QVector3D kd = kGrey;
    int tex = -1;
};

void loadMtl(const QString& path, RawModel& raw, QHash<QByteArray, int>& names, QVector<ObjMaterial>& mats)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QString dir = QFileInfo(path).absolutePath();
    int cur = -1;
    while (!f.atEnd()) {
        const QByteArray line = f.readLine().trimmed();
        if (line.startsWith("newmtl")) {
            const QByteArray name = line.mid(6).trimmed();
            cur = mats.size();
            mats.append(ObjMaterial());
            names.insert(name, cur);
        } else if (cur >= 0 && line.startsWith("Kd") && line.size() > 2 && blank(line[2])) {
            const char* p = line.constData() + 2;
            const char* e = line.constData() + line.size();
            double c[3] = {0.8, 0.8, 0.8};
            if (number(p, e, c[0])) { if (!number(p, e, c[1])) c[1] = c[2] = c[0]; else if (!number(p, e, c[2])) c[2] = c[1]; }
            mats[cur].kd = QVector3D(float(c[0]), float(c[1]), float(c[2]));
        } else if (cur >= 0 && line.startsWith("map_Kd") && line.size() > 6 && blank(line[6])) {
            // options first ("-s 1 1 1", "-clamp on", ...), then the file name (which may hold spaces)
            static const QHash<QByteArray, int> args = {{"-blendu", 1}, {"-blendv", 1}, {"-clamp", 1}, {"-cc", 1}, {"-imfchan", 1}, {"-texres", 1},
                                                         {"-bm", 1}, {"-boost", 1}, {"-type", 1}, {"-mm", 2}, {"-o", 3}, {"-s", 3}, {"-t", 3}};
            QList<QByteArray> t = line.mid(6).simplified().split(' ');
            while (!t.isEmpty() && args.contains(t.first())) { const int n = args.value(t.first()); for (int i = 0; i <= n && !t.isEmpty(); ++i) t.removeFirst(); }
            mats[cur].tex = addTexture(raw, findTexture(dir, QString::fromUtf8(t.join(' '))));
        }
    }
}

bool loadObj(const QString& path, RawModel& raw, QString* error)
{
    FileBytes d;
    if (!d.open(path)) { *error = QStringLiteral("cannot open"); return false; }
    const QString dir = QFileInfo(path).absolutePath();
    QVector<QVector3D> vn;
    QVector<float> vt;                       // u v pairs
    QHash<QByteArray, int> matNames;
    QVector<ObjMaterial> mats;
    int mat = -1;
    bool fileColours = false, normals = true;
    QVector<qint64> fv, ft, fn;
    QVector<QVector3D> cc;                   // corner colours from the materials
    QVector<float> uvs;
    QVector<int> triTex;
    const char* p = d.begin;
    while (p < d.end) {
        const char* le = static_cast<const char*>(std::memchr(p, '\n', size_t(d.end - p)));
        if (!le) le = d.end;
        const char* q = p;
        p = le < d.end ? le + 1 : d.end;
        while (q < le && (*q == ' ' || *q == '\t')) ++q;
        if (le - q < 2) continue;
        if (q[0] == 'v' && (q[1] == ' ' || q[1] == '\t')) {
            q += 2;
            double x[6];
            int n = 0;
            while (n < 6 && number(q, le, x[n])) ++n;
            if (n < 3) continue;
            raw.v.append(QVector3D(float(x[0]), float(x[1]), float(x[2])));
            if (n >= 6) { raw.vc.append(QVector3D(float(x[3]), float(x[4]), float(x[5]))); fileColours = true; }
            else raw.vc.append(kGrey);
        } else if (q[0] == 'v' && q[1] == 'n' && le - q > 2 && blank(q[2])) {
            q += 3;
            double x[3];
            if (number(q, le, x[0]) && number(q, le, x[1]) && number(q, le, x[2])) vn.append(QVector3D(float(x[0]), float(x[1]), float(x[2])));
            else vn.append(QVector3D());
        } else if (q[0] == 'v' && q[1] == 't' && le - q > 2 && blank(q[2])) {
            q += 3;
            double x[2] = {0.0, 0.0};
            if (number(q, le, x[0])) number(q, le, x[1]);
            vt << float(x[0]) << float(x[1]);
        } else if (q[0] == 'f' && (q[1] == ' ' || q[1] == '\t')) {
            q += 2;
            fv.clear(); ft.clear(); fn.clear();
            while (q < le) {
                while (q < le && blank(*q)) ++q;
                if (q >= le) break;
                qint64 a = 0, t = 0, n = 0;
                if (!integer(q, le, a)) { while (q < le && !blank(*q)) ++q; continue; }
                if (q < le && *q == '/') {
                    ++q;
                    integer(q, le, t);
                    if (q < le && *q == '/') { ++q; integer(q, le, n); }
                }
                while (q < le && !blank(*q)) ++q;
                fv.append(a < 0 ? raw.v.size() + a : a - 1);
                ft.append(t < 0 ? vt.size() / 2 + t : t - 1);
                fn.append(n < 0 ? vn.size() + n : n - 1);
            }
            for (int i = 1; i + 1 < fv.size(); ++i) {   // a fan of triangles
                const int corner[3] = {0, i, i + 1};
                const int tex = mat >= 0 ? mats[mat].tex : -1;
                if (!mats.isEmpty()) triTex.append(tex);
                for (int k : corner) {
                    const qint64 a = fv[k];
                    if (a < 0 || a >= raw.v.size()) { *error = QStringLiteral("face refers to a missing vertex"); return false; }
                    raw.idx.append(quint32(a));
                    const qint64 n = fn[k];
                    if (n >= 0 && n < vn.size()) raw.cn.append(vn[n]); else { raw.cn.append(QVector3D()); normals = false; }
                    if (!mats.isEmpty()) {
                        const qint64 t = ft[k];
                        cc.append(mat >= 0 ? mats[mat].kd : kGrey);
                        if (tex >= 0 && t >= 0 && t < vt.size() / 2) uvs << vt[t * 2] << 1.f - vt[t * 2 + 1];
                        else uvs << 0.f << 0.f;
                    }
                }
            }
            if (raw.idx.size() / 3 > ModelLibrary::kMaxFileTriangles) { *error = tooMany(); return false; }
        } else if (le - q > 7 && std::memcmp(q, "mtllib", 6) == 0 && blank(q[6])) {
            const QString rest = QString::fromUtf8(QByteArray(q + 7, int(le - q - 7)).trimmed());
            if (QFileInfo(dir + '/' + rest).isFile()) loadMtl(dir + '/' + rest, raw, matNames, mats);   // one name, perhaps with spaces
            else for (const QString& one : rest.split(' ', Qt::SkipEmptyParts)) loadMtl(dir + '/' + one, raw, matNames, mats);
        } else if (le - q > 7 && std::memcmp(q, "usemtl", 6) == 0 && blank(q[6])) {
            mat = matNames.value(QByteArray(q + 7, int(le - q - 7)).trimmed(), -1);
        }
    }
    if (!normals) raw.cn.clear();
    if (!fileColours) raw.vc.clear();
    if (!fileColours && cc.size() == raw.idx.size()) {   // (a face before the first mtllib leaves them out of step)
        raw.cc = cc;
        if (!raw.textures.isEmpty()) { raw.uv = uvs; raw.triTexture = triTex; }
    }
    if (raw.triTexture.isEmpty()) raw.textures.clear();
    if (raw.idx.isEmpty()) { *error = QStringLiteral("no faces"); return false; }
    return true;
}

// ---------------------------------------------------------------- STL
bool loadStl(const QString& path, RawModel& raw, QString* error)
{
    FileBytes d;
    if (!d.open(path)) { *error = QStringLiteral("cannot open"); return false; }
    if (d.size() >= 84) {   // binary: 80-byte header, count, 50 bytes per triangle
        const quint32 n = qFromLittleEndian<quint32>(d.begin + 80);
        if (qint64(84) + qint64(n) * 50 == d.size()) {
            if (n > quint32(ModelLibrary::kMaxFileTriangles)) { *error = tooMany(); return false; }
            if (n == 0) { *error = QStringLiteral("no triangles"); return false; }
            raw.v.resize(int(n) * 3);
            raw.idx.resize(int(n) * 3);
            const char* p = d.begin + 84;
            for (quint32 i = 0; i < n; ++i, p += 50)
                for (int k = 0; k < 3; ++k) {
                    const char* c = p + 12 + k * 12;
                    raw.v[int(i) * 3 + k] = QVector3D(qFromLittleEndian<float>(c), qFromLittleEndian<float>(c + 4), qFromLittleEndian<float>(c + 8));
                    raw.idx[int(i) * 3 + k] = i * 3 + quint32(k);
                }
            return true;
        }
    }
    // text: "vertex x y z" lines, three per facet
    const char* p = d.begin;
    while (p < d.end) {
        const char* le = static_cast<const char*>(std::memchr(p, '\n', size_t(d.end - p)));
        if (!le) le = d.end;
        const char* q = p;
        p = le < d.end ? le + 1 : d.end;
        while (q < le && blank(*q)) ++q;
        if (le - q < 7 || std::memcmp(q, "vertex", 6) != 0) continue;
        q += 6;
        double x[3];
        if (number(q, le, x[0]) && number(q, le, x[1]) && number(q, le, x[2])) raw.v.append(QVector3D(float(x[0]), float(x[1]), float(x[2])));
        if (raw.v.size() / 3 > ModelLibrary::kMaxFileTriangles) { *error = tooMany(); return false; }
    }
    raw.v.resize(raw.v.size() / 3 * 3);
    if (raw.v.isEmpty()) { *error = QStringLiteral("not an STL model"); return false; }
    raw.idx.resize(raw.v.size());
    for (int i = 0; i < raw.idx.size(); ++i) raw.idx[i] = quint32(i);
    return true;
}

// ---------------------------------------------------------------- PLY
struct PlyProp {
    QByteArray name;
    int type = 0;        // index into kPlySize
    bool list = false;
    int countType = 0;
};
struct PlyElem {
    QByteArray name;
    qint64 count = 0;
    QVector<PlyProp> props;
};
const int kPlySize[8] = {1, 1, 2, 2, 4, 4, 4, 8};
int plyType(const QByteArray& t)
{
    static const QHash<QByteArray, int> types = {{"char", 0}, {"int8", 0}, {"uchar", 1}, {"uint8", 1}, {"short", 2}, {"int16", 2},
                                                  {"ushort", 3}, {"uint16", 3}, {"int", 4}, {"int32", 4}, {"uint", 5}, {"uint32", 5},
                                                  {"float", 6}, {"float32", 6}, {"double", 7}, {"float64", 7}};
    return types.value(t, -1);
}
struct PlyBody {
    const char* p;
    const char* end;        // of the file; in a text file, of the line being read
    const char* fileEnd;
    bool ascii, swap;
    bool ok = true;
    // A text file holds one vertex or face per line (anything further on the line is not ours).
    void beginRow()
    {
        if (!ascii) return;
        while (p < fileEnd && blank(*p)) ++p;
        const char* le = static_cast<const char*>(std::memchr(p, '\n', size_t(fileEnd - p)));
        end = le ? le : fileEnd;
    }
    void endRow()
    {
        if (!ascii) return;
        p = end < fileEnd ? end + 1 : fileEnd;
        end = fileEnd;
    }
    static double decode(const char* q, int type, bool swap)
    {
        uchar b[8];
        const int n = kPlySize[type];
        std::memcpy(b, q, size_t(n));
        if (swap) std::reverse(b, b + n);
        switch (type) {
        case 0: return qint8(b[0]);
        case 1: return b[0];
        case 2: { qint16 v; std::memcpy(&v, b, 2); return v; }
        case 3: { quint16 v; std::memcpy(&v, b, 2); return v; }
        case 4: { qint32 v; std::memcpy(&v, b, 4); return v; }
        case 5: { quint32 v; std::memcpy(&v, b, 4); return v; }
        case 6: { float v; std::memcpy(&v, b, 4); return v; }
        default: { double v; std::memcpy(&v, b, 8); return v; }
        }
    }
    double num(int type)
    {
        if (ascii) {
            double v = 0.0;
            if (!number(p, end, v)) ok = false;
            return v;
        }
        const int n = kPlySize[type];
        if (end - p < n) { ok = false; p = end; return 0.0; }
        const double v = decode(p, type, swap);
        p += n;
        return v;
    }
};

bool loadPly(const QString& path, RawModel& raw, QString* error)
{
    FileBytes d;
    if (!d.open(path)) { *error = QStringLiteral("cannot open"); return false; }
    if (d.size() < 4 || std::memcmp(d.begin, "ply", 3) != 0) { *error = QStringLiteral("not a PLY model"); return false; }
    // the header: text lines up to "end_header"
    int format = -1;   // 0 text, 1 binary little-endian, 2 binary big-endian
    QVector<PlyElem> elems;
    const char* p = d.begin;
    bool ended = false;
    while (p < d.end && !ended) {
        const char* le = static_cast<const char*>(std::memchr(p, '\n', size_t(d.end - p)));
        if (!le) break;
        const QList<QByteArray> t = QByteArray(p, int(le - p)).simplified().split(' ');
        p = le + 1;
        if (t.isEmpty()) continue;
        if (t[0] == "format" && t.size() >= 2) format = t[1] == "ascii" ? 0 : t[1] == "binary_little_endian" ? 1 : t[1] == "binary_big_endian" ? 2 : -1;
        else if (t[0] == "element" && t.size() >= 3) { PlyElem e; e.name = t[1]; e.count = t[2].toLongLong(); elems.append(e); }
        else if (t[0] == "property" && !elems.isEmpty()) {
            PlyProp pr;
            if (t.size() >= 5 && t[1] == "list") { pr.list = true; pr.countType = plyType(t[2]); pr.type = plyType(t[3]); pr.name = t[4]; }
            else if (t.size() >= 3) { pr.type = plyType(t[1]); pr.name = t[2]; }
            else { *error = QStringLiteral("not a PLY model"); return false; }
            if (pr.type < 0 || pr.countType < 0) { *error = QStringLiteral("a PLY property of an unknown kind"); return false; }
            elems.last().props.append(pr);
        } else if (t[0] == "end_header") ended = true;
    }
    if (!ended || format < 0) { *error = QStringLiteral("not a PLY model"); return false; }
    PlyBody b{p, d.end, d.end, format == 0, format != 0 && (format == 2) != (QSysInfo::ByteOrder == QSysInfo::BigEndian)};
    bool faces = false;
    for (const PlyElem& e : elems) if ((e.name == "face" || e.name == "tristrips") && e.count > 0) faces = true;
    bool colours = false, normals = false;
    QVector<qint64> poly;
    for (const PlyElem& e : elems) {
        if (e.count < 0) { *error = QStringLiteral("not a PLY model"); return false; }
        bool lists = false;
        int stride = 0;
        for (const PlyProp& pr : e.props) { lists = lists || pr.list; stride += kPlySize[pr.type]; }
        if (e.name == "vertex") {
            // which property is what: 0-2 position, 3-5 normal, 6-8 colour, 9-11 a splat's base colour
            QVector<int> role(e.props.size(), -1);
            QVector<int> offset(e.props.size(), 0);
            double colourScale = 1.0;
            bool has[12] = {};
            for (int i = 0, off = 0; i < e.props.size(); ++i) {
                static const QHash<QByteArray, int> roles = {{"x", 0}, {"y", 1}, {"z", 2}, {"nx", 3}, {"ny", 4}, {"nz", 5},
                    {"red", 6}, {"green", 7}, {"blue", 8}, {"r", 6}, {"g", 7}, {"b", 8},
                    {"diffuse_red", 6}, {"diffuse_green", 7}, {"diffuse_blue", 8}, {"f_dc_0", 9}, {"f_dc_1", 10}, {"f_dc_2", 11}};
                const PlyProp& pr = e.props[i];
                offset[i] = off;
                off += kPlySize[pr.type];
                const int r = pr.list ? -1 : roles.value(pr.name.toLower(), -1);
                if (r < 0 || has[r]) continue;
                role[i] = r;
                has[r] = true;
                if (r >= 6 && r <= 8) colourScale = pr.type == 1 ? 1.0 / 255.0 : pr.type == 3 ? 1.0 / 65535.0 : 1.0;
            }
            if (!has[0] || !has[1] || !has[2]) { *error = QStringLiteral("no vertex positions"); return false; }
            const bool rgb = has[6] && has[7] && has[8], splat = !rgb && has[9] && has[10] && has[11];
            normals = has[3] && has[4] && has[5];
            colours = rgb || splat;
            // A point cloud is thinned as it is read; a mesh needs every vertex.
            const qint64 step = faces ? 1 : std::max<qint64>(1, (e.count + ModelLibrary::kMaxPoints - 1) / ModelLibrary::kMaxPoints);
            if (faces && e.count > qint64(3) * ModelLibrary::kMaxFileTriangles) { *error = tooMany(); return false; }
            if (!faces) raw.filePoints = e.count;
            const qint64 room = b.ascii ? (b.end - b.p) / 6 + 1 : (b.end - b.p) / std::max(1, stride) + 1;
            const int keep = int(std::min(e.count, room) / step + 1);
            raw.v.reserve(keep);
            if (normals) raw.vn.reserve(keep);
            if (colours) raw.vc.reserve(keep);
            for (qint64 i = 0; i < e.count && b.ok; ++i) {
                double val[12] = {};
                b.beginRow();
                if (!b.ascii && !lists) {   // fixed-size records: only the wanted values are read
                    if (b.end - b.p < stride) { b.ok = false; break; }
                    if (i % step == 0) for (int k = 0; k < e.props.size(); ++k) if (role[k] >= 0) val[role[k]] = PlyBody::decode(b.p + offset[k], e.props[k].type, b.swap);
                    b.p += stride;
                } else {
                    for (int k = 0; k < e.props.size() && b.ok; ++k) {
                        const PlyProp& pr = e.props[k];
                        if (pr.list) { const qint64 n = qint64(b.num(pr.countType)); for (qint64 j = 0; j < n && b.ok; ++j) b.num(pr.type); }
                        else { const double x = b.num(pr.type); if (role[k] >= 0) val[role[k]] = x; }
                    }
                }
                b.endRow();
                if (!b.ok || i % step != 0) continue;
                raw.v.append(QVector3D(float(val[0]), float(val[1]), float(val[2])));
                if (normals) raw.vn.append(QVector3D(float(val[3]), float(val[4]), float(val[5])));
                if (rgb) raw.vc.append(QVector3D(float(val[6] * colourScale), float(val[7] * colourScale), float(val[8] * colourScale)));
                else if (splat) raw.vc.append(QVector3D(float(std::clamp(0.5 + 0.2820948 * val[9], 0.0, 1.0)), float(std::clamp(0.5 + 0.2820948 * val[10], 0.0, 1.0)),
                                                        float(std::clamp(0.5 + 0.2820948 * val[11], 0.0, 1.0))));
            }
        } else if (e.name == "face" || e.name == "tristrips") {
            const bool strips = e.name == "tristrips";
            int want = -1;
            for (int k = 0; k < e.props.size(); ++k) if (e.props[k].list && (e.props[k].name == "vertex_indices" || e.props[k].name == "vertex_index")) want = k;
            for (int k = 0; k < e.props.size() && want < 0; ++k) if (e.props[k].list) want = k;
            for (qint64 i = 0; i < e.count && b.ok; ++i) {
                poly.clear();
                b.beginRow();
                for (int k = 0; k < e.props.size() && b.ok; ++k) {
                    const PlyProp& pr = e.props[k];
                    if (!pr.list) { b.num(pr.type); continue; }
                    const qint64 n = qint64(b.num(pr.countType));
                    if (n < 0 || (!b.ascii && n * kPlySize[pr.type] > b.end - b.p)) { b.ok = false; break; }
                    for (qint64 j = 0; j < n && b.ok; ++j) { const double x = b.num(pr.type); if (k == want) poly.append(qint64(x)); }
                }
                b.endRow();
                if (!b.ok) break;
                if (strips) {   // a run of triangles; a negative index starts a new run
                    for (int j = 0, run = 0; j < poly.size(); ++j) {
                        if (poly[j] < 0) { run = 0; continue; }
                        if (++run < 3) continue;
                        const bool odd = run % 2 == 0;
                        raw.idx << quint32(poly[j - 2]) << quint32(poly[odd ? j : j - 1]) << quint32(poly[odd ? j - 1 : j]);
                    }
                } else {
                    for (int j = 1; j + 1 < poly.size(); ++j) raw.idx << quint32(poly[0]) << quint32(poly[j]) << quint32(poly[j + 1]);
                    for (qint64 x : poly) if (x < 0) { *error = QStringLiteral("face refers to a missing vertex"); return false; }
                }
                if (raw.idx.size() / 3 > ModelLibrary::kMaxFileTriangles) { *error = tooMany(); return false; }
            }
        } else if (!b.ascii && !lists) {
            if ((b.end - b.p) / std::max(1, stride) < e.count) b.ok = false; else b.p += e.count * stride;
        } else {
            for (qint64 i = 0; i < e.count && b.ok; ++i) {
                b.beginRow();
                for (const PlyProp& pr : e.props) {
                    if (pr.list) { const qint64 n = qint64(b.num(pr.countType)); for (qint64 j = 0; j < n && b.ok; ++j) b.num(pr.type); }
                    else b.num(pr.type);
                    if (!b.ok) break;
                }
                b.endRow();
            }
        }
        if (!b.ok) { *error = QStringLiteral("the file is damaged or cut short"); return false; }
    }
    for (quint32 i : raw.idx) if (i >= quint32(raw.v.size())) { *error = QStringLiteral("face refers to a missing vertex"); return false; }
    if (raw.v.isEmpty()) { *error = QStringLiteral("no vertices"); return false; }
    if (raw.idx.isEmpty()) {
        if (faces) { *error = QStringLiteral("no faces"); return false; }
        raw.points = true;
    }
    return true;
}

// ---------------------------------------------------------------- glTF / GLB
int gltfCompSize(int comp) { return comp == 5120 || comp == 5121 ? 1 : comp == 5122 || comp == 5123 ? 2 : comp == 5125 || comp == 5126 ? 4 : 0; }
struct GltfAccessor {
    const uchar* p = nullptr;
    int stride = 0, comp = 0, n = 0;
    bool norm = false;
    qint64 count = 0;
    double get(qint64 i, int c) const
    {
        const uchar* q = p + i * stride + c * gltfCompSize(comp);
        switch (comp) {
        case 5120: { const qint8 v = qint8(*q); return norm ? std::max(v / 127.0, -1.0) : v; }
        case 5121: return norm ? *q / 255.0 : *q;
        case 5122: { const qint16 v = qFromLittleEndian<qint16>(q); return norm ? std::max(v / 32767.0, -1.0) : v; }
        case 5123: { const quint16 v = qFromLittleEndian<quint16>(q); return norm ? v / 65535.0 : v; }
        case 5125: return qFromLittleEndian<quint32>(q);
        default: return qFromLittleEndian<float>(q);
        }
    }
};

bool loadGltf(const QString& path, RawModel& raw, QString* error)
{
    FileBytes d;
    if (!d.open(path)) { *error = QStringLiteral("cannot open"); return false; }
    const QString dir = QFileInfo(path).absolutePath();
    QByteArray json, glbBin;
    if (d.size() >= 12 && std::memcmp(d.begin, "glTF", 4) == 0) {   // GLB: a header, then chunks (JSON, then binary)
        const char* p = d.begin + 12;
        while (d.end - p >= 8) {
            const quint32 len = qFromLittleEndian<quint32>(p), type = qFromLittleEndian<quint32>(p + 4);
            p += 8;
            if (qint64(len) > d.end - p) { *error = QStringLiteral("the file is damaged or cut short"); return false; }
            if (type == 0x4E4F534Au && json.isEmpty()) json = QByteArray::fromRawData(p, int(len));
            else if (type == 0x004E4942u && glbBin.isEmpty()) glbBin = QByteArray::fromRawData(p, int(len));
            p += len;   // (the length includes any padding)
        }
    } else json = QByteArray::fromRawData(d.begin, int(d.size()));
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty() || !root.contains("asset")) { *error = QStringLiteral("not a glTF model"); return false; }
    for (const QJsonValue& x : root["extensionsRequired"].toArray()) {
        if (x.toString() == "KHR_draco_mesh_compression") { *error = QStringLiteral("Draco-compressed (not supported; export it without compression)"); return false; }
        if (x.toString() == "EXT_meshopt_compression" || x.toString() == "KHR_meshopt_compression") { *error = QStringLiteral("meshopt-compressed (not supported; export it without compression)"); return false; }
    }
    auto uriBytes = [&dir](const QString& uri) -> QByteArray {
        if (uri.startsWith(QLatin1String("data:"))) {
            const int comma = uri.indexOf(',');
            return comma < 0 ? QByteArray() : QByteArray::fromBase64(uri.mid(comma + 1).toLatin1());
        }
        QFile f(dir + '/' + QUrl::fromPercentEncoding(uri.toUtf8()));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };
    QVector<QByteArray> buffers;
    for (const QJsonValue& x : root["buffers"].toArray()) {
        const QString uri = x.toObject()["uri"].toString();
        buffers.append(uri.isEmpty() ? glbBin : uriBytes(uri));
    }
    const QJsonArray views = root["bufferViews"].toArray(), accessors = root["accessors"].toArray(), meshes = root["meshes"].toArray(),
                     nodes = root["nodes"].toArray(), materials = root["materials"].toArray(), textures = root["textures"].toArray(),
                     images = root["images"].toArray();
    auto viewBytes = [&](int bv) -> QByteArray {
        if (bv < 0 || bv >= views.size()) return QByteArray();
        const QJsonObject v = views[bv].toObject();
        const int b = v["buffer"].toInt(-1);
        const qint64 off = qint64(v["byteOffset"].toDouble()), len = qint64(v["byteLength"].toDouble());
        const QVector<QByteArray>& all = buffers;
        if (b < 0 || b >= all.size() || off < 0 || len < 0 || off + len > all[b].size()) return QByteArray();
        return QByteArray::fromRawData(all[b].constData() + off, int(len));
    };
    bool meshopt = false;
    auto accessor = [&](int i, GltfAccessor& a) -> bool {
        if (i < 0 || i >= accessors.size()) return false;
        const QJsonObject o = accessors[i].toObject();
        const int bv = o["bufferView"].toInt(-1);
        if (bv < 0 || bv >= views.size()) return false;
        const QJsonObject v = views[bv].toObject();
        const QJsonObject vx = v["extensions"].toObject();
        if (vx.contains("EXT_meshopt_compression") || vx.contains("KHR_meshopt_compression")) { meshopt = true; return false; }
        const QByteArray bytes = viewBytes(bv);
        a.comp = o["componentType"].toInt();
        a.norm = o["normalized"].toBool();
        a.count = qint64(o["count"].toDouble());
        const QString t = o["type"].toString();
        a.n = t == "SCALAR" ? 1 : t == "VEC2" ? 2 : t == "VEC3" ? 3 : t == "VEC4" ? 4 : 0;
        const int es = gltfCompSize(a.comp) * a.n;
        if (es == 0 || a.count <= 0) return false;
        a.stride = std::max(v["byteStride"].toInt(0), es);
        const qint64 off = qint64(o["byteOffset"].toDouble());
        if (off < 0 || off + (a.count - 1) * a.stride + es > bytes.size()) return false;
        a.p = reinterpret_cast<const uchar*>(bytes.constData()) + off;
        return true;
    };
    // materials: a base colour (linear) and perhaps a picture
    struct Material { QVector3D factor{1.f, 1.f, 1.f}; int image = -1; bool coloured = false; };
    QVector<Material> mats;
    for (const QJsonValue& x : materials) {
        Material m;
        const QJsonObject o = x.toObject();
        QJsonObject pbr = o["pbrMetallicRoughness"].toObject();
        QString factor = QStringLiteral("baseColorFactor"), tex = QStringLiteral("baseColorTexture");
        const QJsonObject old = o["extensions"].toObject()["KHR_materials_pbrSpecularGlossiness"].toObject();
        if (!old.isEmpty()) { pbr = old; factor = QStringLiteral("diffuseFactor"); tex = QStringLiteral("diffuseTexture"); }
        const QJsonArray f = pbr[factor].toArray();
        if (f.size() >= 3) { m.factor = QVector3D(float(f[0].toDouble()), float(f[1].toDouble()), float(f[2].toDouble())); m.coloured = true; }
        const QJsonObject t = pbr[tex].toObject();
        if (t.contains("index")) {
            const int ti = t["index"].toInt(-1);
            if (ti >= 0 && ti < textures.size()) m.image = textures[ti].toObject()["source"].toInt(-1);
        }
        mats.append(m);
    }
    QHash<int, int> pictures;   // image -> its number among the model's pictures (-1: could not be read)
    auto picture = [&](int image) -> int {
        if (image < 0 || image >= images.size()) return -1;
        auto it = pictures.find(image);
        if (it == pictures.end()) {
            const QJsonObject o = images[image].toObject();
            QImage img;
            if (o.contains("bufferView")) img = modelPicture(viewBytes(o["bufferView"].toInt(-1)));
            else if (o["uri"].toString().startsWith(QLatin1String("data:"))) img = modelPicture(uriBytes(o["uri"].toString()));
            else img = findTexture(dir, QUrl::fromPercentEncoding(o["uri"].toString().toUtf8()));
            it = pictures.insert(image, addTexture(raw, img));
        }
        return *it;
    };
    RawModel pts;                 // the file's points, used when it holds no triangles
    bool anyColour = false, draco = false, failed = false;
    auto addMesh = [&](int mi, const QMatrix4x4& world) {
        if (mi < 0 || mi >= meshes.size()) return;
        const QMatrix3x3 nm = world.normalMatrix();
        for (const QJsonValue& pv : meshes[mi].toObject()["primitives"].toArray()) {
            const QJsonObject prim = pv.toObject();
            if (prim["extensions"].toObject().contains("KHR_draco_mesh_compression")) { draco = true; continue; }
            const int mode = prim["mode"].toInt(4);
            if (mode != 0 && mode != 4 && mode != 5 && mode != 6) continue;   // lines
            const QJsonObject attr = prim["attributes"].toObject();
            GltfAccessor pos, nrm, col, uv, ind;
            if (!accessor(attr["POSITION"].toInt(-1), pos) || pos.n != 3) continue;
            const bool hasN = accessor(attr["NORMAL"].toInt(-1), nrm) && nrm.n == 3 && nrm.count >= pos.count;
            const bool hasC = accessor(attr["COLOR_0"].toInt(-1), col) && col.n >= 3 && col.count >= pos.count;
            const bool hasUv = accessor(attr["TEXCOORD_0"].toInt(-1), uv) && uv.n == 2 && uv.count >= pos.count;
            const int mat = prim["material"].toInt(-1);
            const Material m = mat >= 0 && mat < mats.size() ? mats[mat] : Material();
            const int tex = hasUv && mode != 0 ? picture(m.image) : -1;
            if (hasC || tex >= 0 || m.coloured) anyColour = true;
            QVector<QVector3D> mul;   // per vertex of this primitive: the material's colour times the vertex's (linear)
            mul.reserve(int(pos.count));
            RawModel& out = mode == 0 ? pts : raw;
            if (qint64(out.v.size()) + pos.count > qint64(3) * ModelLibrary::kMaxFileTriangles) { failed = true; return; }
            const quint32 base = quint32(out.v.size());
            for (qint64 i = 0; i < pos.count; ++i) {
                out.v.append(world.map(QVector3D(float(pos.get(i, 0)), float(pos.get(i, 1)), float(pos.get(i, 2)))));
                QVector3D n;
                if (hasN) {
                    const float x = float(nrm.get(i, 0)), y = float(nrm.get(i, 1)), z = float(nrm.get(i, 2));
                    n = QVector3D(nm(0, 0) * x + nm(0, 1) * y + nm(0, 2) * z, nm(1, 0) * x + nm(1, 1) * y + nm(1, 2) * z, nm(2, 0) * x + nm(2, 1) * y + nm(2, 2) * z);
                }
                out.vn.append(n);
                QVector3D c = m.factor;   // linear
                if (hasC) c *= QVector3D(float(col.get(i, 0)), float(col.get(i, 1)), float(col.get(i, 2)));
                mul.append(c);
                if (mode == 0) out.vc.append(hasC || m.coloured ? toDisplay(c) : kGrey);
            }
            if (mode == 0) continue;
            const bool hasI = accessor(prim["indices"].toInt(-1), ind) && ind.n == 1;
            const qint64 n = hasI ? ind.count : pos.count;
            auto at = [&](qint64 i) -> qint64 { return hasI ? qint64(ind.get(i, 0)) : i; };
            auto tri = [&](qint64 a, qint64 b, qint64 c) {
                if (a < 0 || b < 0 || c < 0 || a >= pos.count || b >= pos.count || c >= pos.count) return;
                raw.idx << base + quint32(a) << base + quint32(b) << base + quint32(c);
                raw.triTexture.append(tex);
                for (qint64 i : {a, b, c}) {
                    raw.cmul.append(mul[int(i)]);
                    raw.cc.append(hasC || m.coloured ? toDisplay(mul[int(i)]) : kGrey);
                    if (tex >= 0) raw.uv << float(uv.get(i, 0)) << float(uv.get(i, 1)); else raw.uv << 0.f << 0.f;
                }
            };
            if (mode == 4) for (qint64 i = 0; i + 2 < n; i += 3) tri(at(i), at(i + 1), at(i + 2));
            else if (mode == 5) for (qint64 i = 0; i + 2 < n; ++i) { if (i % 2) tri(at(i + 1), at(i), at(i + 2)); else tri(at(i), at(i + 1), at(i + 2)); }
            else for (qint64 i = 1; i + 1 < n; ++i) tri(at(0), at(i), at(i + 1));
            if (raw.idx.size() / 3 > ModelLibrary::kMaxFileTriangles) { failed = true; return; }
        }
    };
    std::function<void(int, const QMatrix4x4&, int)> visit = [&](int ni, const QMatrix4x4& parent, int depth) {
        if (ni < 0 || ni >= nodes.size() || depth > 64 || failed) return;
        const QJsonObject node = nodes[ni].toObject();
        QMatrix4x4 local;
        const QJsonArray mx = node["matrix"].toArray();
        if (mx.size() == 16) {
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) local(r, c) = float(mx[c * 4 + r].toDouble());
        } else {
            const QJsonArray t = node["translation"].toArray(), r = node["rotation"].toArray(), s = node["scale"].toArray();
            if (t.size() == 3) local.translate(float(t[0].toDouble()), float(t[1].toDouble()), float(t[2].toDouble()));
            if (r.size() == 4) local.rotate(QQuaternion(float(r[3].toDouble()), float(r[0].toDouble()), float(r[1].toDouble()), float(r[2].toDouble())).normalized());
            if (s.size() == 3) local.scale(float(s[0].toDouble()), float(s[1].toDouble()), float(s[2].toDouble()));
        }
        const QMatrix4x4 world = parent * local;
        if (node.contains("mesh")) addMesh(node["mesh"].toInt(-1), world);
        for (const QJsonValue& c : node["children"].toArray()) visit(c.toInt(-1), world, depth + 1);
    };
    const QJsonArray scenes = root["scenes"].toArray();
    if (!scenes.isEmpty()) {
        const int s = std::clamp(root["scene"].toInt(0), 0, int(scenes.size()) - 1);
        for (const QJsonValue& n : scenes[s].toObject()["nodes"].toArray()) visit(n.toInt(-1), QMatrix4x4(), 0);
    } else if (!nodes.isEmpty()) {
        QVector<bool> child(nodes.size(), false);
        for (const QJsonValue& n : nodes) for (const QJsonValue& c : n.toObject()["children"].toArray()) { const int i = c.toInt(-1); if (i >= 0 && i < child.size()) child[i] = true; }
        for (int i = 0; i < nodes.size(); ++i) if (!child[i]) visit(i, QMatrix4x4(), 0);
    } else for (int i = 0; i < meshes.size(); ++i) addMesh(i, QMatrix4x4());
    if (failed) { *error = tooMany(); return false; }
    if (raw.idx.isEmpty() && !pts.v.isEmpty()) { raw = pts; raw.points = true; raw.filePoints = raw.v.size(); if (!anyColour) raw.vc.clear(); }
    if (raw.v.isEmpty() || (raw.idx.isEmpty() && !raw.points)) {
        *error = draco ? QStringLiteral("Draco-compressed (not supported; export it without compression)")
               : meshopt ? QStringLiteral("meshopt-compressed (not supported; export it without compression)") : QStringLiteral("no faces");
        return false;
    }
    if (!raw.points) {
        raw.vc.clear();
        if (!anyColour) { raw.cc.clear(); raw.cmul.clear(); }
        if (raw.textures.isEmpty() || !anyColour) { raw.textures.clear(); raw.triTexture.clear(); raw.uv.clear(); raw.cmul.clear(); }
    }
    return true;
}

// ---------------------------------------------------------------- FBX (ufbx)
bool loadFbx(const QString& path, RawModel& raw, QString* error)
{
    FileBytes d;
    if (!d.open(path)) { *error = QStringLiteral("cannot open"); return false; }
    const QString dir = QFileInfo(path).absolutePath();
    ufbx_load_opts opts = {};
    opts.target_axes = ufbx_axes_right_handed_y_up;   // the file says which way is up
    opts.space_conversion = UFBX_SPACE_CONVERSION_TRANSFORM_ROOT;
    opts.generate_missing_normals = true;
    opts.ignore_animation = true;
    ufbx_error err;
    ufbx_scene* scene = ufbx_load_memory(d.begin, size_t(d.size()), &opts, &err);
    if (!scene) {
        *error = QStringLiteral("not an FBX model the player can read (%1)").arg(QString::fromUtf8(err.description.data, int(err.description.length)));
        return false;
    }
    struct Free { ufbx_scene* s; ~Free() { ufbx_free_scene(s); } } freeScene{scene};
    QHash<const ufbx_texture*, int> pictures;   // -> its number among the model's pictures (-1: could not be read)
    auto picture = [&](const ufbx_texture* t) -> int {
        if (!t) return -1;
        auto it = pictures.find(t);
        if (it == pictures.end()) {
            QImage img;
            if (t->content.size > 0 && t->content.size < (size_t(256) << 20)) img = modelPicture(QByteArray::fromRawData(static_cast<const char*>(t->content.data), int(t->content.size)));
            for (const ufbx_string& name : {t->relative_filename, t->filename, t->absolute_filename})
                if (img.isNull() && name.length > 0) img = findTexture(dir, QString::fromUtf8(name.data, int(name.length)));
            it = pictures.insert(t, addTexture(raw, img));
        }
        return *it;
    };
    size_t total = 0;
    for (size_t i = 0; i < scene->meshes.count; ++i) total += scene->meshes.data[i]->num_triangles * std::max<size_t>(1, scene->meshes.data[i]->instances.count);
    if (total > size_t(ModelLibrary::kMaxFileTriangles)) { *error = tooMany(); return false; }
    bool anyColour = false;
    std::vector<uint32_t> tri;
    for (int pass = 0; pass < 2 && raw.idx.isEmpty(); ++pass) {   // hidden parts only when nothing else is there
        for (size_t mi = 0; mi < scene->meshes.count; ++mi) {
            const ufbx_mesh* mesh = scene->meshes.data[mi];
            if (mesh->num_triangles == 0) continue;
            tri.resize(mesh->max_face_triangles * 3);
            for (size_t ni = 0; ni < mesh->instances.count; ++ni) {
                const ufbx_node* node = mesh->instances.data[ni];
                if (pass == 0 && !node->visible) continue;
                const ufbx_matrix nmx = ufbx_matrix_for_normals(&node->geometry_to_world);
                const quint32 base = quint32(raw.v.size());
                for (size_t i = 0; i < mesh->vertices.count; ++i) {
                    const ufbx_vec3 p = ufbx_transform_position(&node->geometry_to_world, mesh->vertices.data[i]);
                    raw.v.append(QVector3D(float(p.x), float(p.y), float(p.z)));
                }
                for (size_t fi = 0; fi < mesh->faces.count; ++fi) {
                    const ufbx_face face = mesh->faces.data[fi];
                    if (face.num_indices < 3) continue;
                    // this face's colour: its material's picture, the mesh's vertex colours, or the material's colour
                    const ufbx_material* mat = nullptr;
                    if (fi < mesh->face_material.count) {
                        const uint32_t m = mesh->face_material.data[fi];
                        mat = m < node->materials.count ? node->materials.data[m] : m < mesh->materials.count ? mesh->materials.data[m] : nullptr;
                    }
                    int tex = -1;
                    QVector3D flat = kGrey;
                    bool flatColour = false;
                    if (mat) {
                        const ufbx_material_map& map = mat->pbr.base_color.has_value || mat->pbr.base_color.texture ? mat->pbr.base_color : mat->fbx.diffuse_color;
                        if (mesh->vertex_uv.exists) tex = picture(map.texture);
                        if (map.has_value) { flat = QVector3D(float(map.value_vec3.x), float(map.value_vec3.y), float(map.value_vec3.z)); flatColour = true; }
                    }
                    const uint32_t n = ufbx_triangulate_face(tri.data(), tri.size(), mesh, face);
                    for (uint32_t k = 0; k < n; ++k) raw.triTexture.append(tex);
                    for (uint32_t k = 0; k < n * 3; ++k) {
                        const uint32_t ix = tri[k];
                        raw.idx.append(base + mesh->vertex_indices.data[ix]);
                        if (mesh->vertex_normal.exists) {
                            const ufbx_vec3 nv = ufbx_transform_direction(&nmx, ufbx_get_vertex_vec3(&mesh->vertex_normal, ix));
                            raw.cn.append(QVector3D(float(nv.x), float(nv.y), float(nv.z)));
                        } else raw.cn.append(QVector3D());
                        QVector3D c = flat;
                        if (tex >= 0) { const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh->vertex_uv, ix); raw.uv << float(uv.x) << float(1.0 - uv.y); anyColour = true; }
                        else raw.uv << 0.f << 0.f;
                        if (tex >= 0) {}
                        else if (mesh->vertex_color.exists) { const ufbx_vec4 vc = ufbx_get_vertex_vec4(&mesh->vertex_color, ix); c = QVector3D(float(vc.x), float(vc.y), float(vc.z)); anyColour = true; }
                        else if (flatColour) anyColour = true;
                        raw.cc.append(c);
                    }
                }
            }
        }
    }
    if (raw.idx.isEmpty()) { *error = QStringLiteral("no faces"); return false; }
    if (!anyColour) raw.cc.clear();
    if (raw.textures.isEmpty() || !anyColour) { raw.textures.clear(); raw.triTexture.clear(); raw.uv.clear(); }
    return true;
}
} // namespace

QImage modelPicture(const QByteArray& bytes)
{
    // (TGA files have no signature to go by: Qt is asked first, about the formats that have one.)
    QImage img;
    if (bytes.size() >= 30) {   // (no picture file Qt reads is smaller)
        QBuffer buf(const_cast<QByteArray*>(&bytes));
        buf.open(QIODevice::ReadOnly);
        const QByteArray format = QImageReader::imageFormat(&buf);
        if (!format.isEmpty() && format != "tga") { buf.seek(0); QImageReader r(&buf, format); img = r.read(); }
    }
    return img.isNull() ? tgaPicture(bytes) : img;
}
bool loadObjModel(const QString& path, RawModel& raw, QString* error) { return loadObj(path, raw, error); }
bool loadStlModel(const QString& path, RawModel& raw, QString* error) { return loadStl(path, raw, error); }
bool loadPlyModel(const QString& path, RawModel& raw, QString* error) { return loadPly(path, raw, error); }
bool loadGltfModel(const QString& path, RawModel& raw, QString* error) { return loadGltf(path, raw, error); }
bool loadFbxModel(const QString& path, RawModel& raw, QString* error) { return loadFbx(path, raw, error); }
