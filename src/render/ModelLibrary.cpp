#include "ModelLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutexLocker>
#include <QTextStream>
#include <QVector3D>
#include <cmath>
#include <cstring>

namespace {
struct Raw {
    QVector<QVector3D> pos;       // one per corner (3 per triangle)
    QVector<QVector3D> nrm;       // empty = compute
    QVector<QVector3D> col;       // empty = none
};

bool loadObj(const QString& path, Raw& raw, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { *error = QStringLiteral("cannot open"); return false; }
    QVector<QVector3D> v, vn, vc;
    bool colours = false, normals = true;
    QTextStream in(&f);
    QVector<int> vi, ni;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("v "))) {
            const auto t = line.split(' ', Qt::SkipEmptyParts);
            if (t.size() < 4) continue;
            v.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
            if (t.size() >= 7) { vc.append(QVector3D(t[4].toFloat(), t[5].toFloat(), t[6].toFloat())); colours = true; }
            else vc.append(QVector3D(0.8f, 0.8f, 0.8f));
        } else if (line.startsWith(QLatin1String("vn "))) {
            const auto t = line.split(' ', Qt::SkipEmptyParts);
            if (t.size() >= 4) vn.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
        } else if (line.startsWith(QLatin1String("f "))) {
            const auto t = line.split(' ', Qt::SkipEmptyParts);
            vi.clear(); ni.clear();
            for (int i = 1; i < t.size(); ++i) {
                const auto parts = t[i].split('/');
                int a = parts[0].toInt();
                if (a < 0) a = v.size() + a + 1;
                vi.append(a - 1);
                int n = parts.size() >= 3 && !parts[2].isEmpty() ? parts[2].toInt() : 0;
                if (n < 0) n = vn.size() + n + 1;
                ni.append(n - 1);
            }
            for (int i = 1; i + 1 < vi.size(); ++i) {   // a fan of triangles
                const int idx[3] = {0, i, i + 1};
                for (int k : idx) {
                    const int a = vi[k];
                    if (a < 0 || a >= v.size()) { *error = QStringLiteral("face refers to a missing vertex"); return false; }
                    raw.pos.append(v[a]);
                    raw.col.append(vc[a]);
                    const int n = ni[k];
                    if (n >= 0 && n < vn.size()) raw.nrm.append(vn[n]); else normals = false;
                }
            }
            if (raw.pos.size() / 3 > ModelLibrary::kMaxTriangles) { *error = QStringLiteral("too many triangles"); return false; }
        }
    }
    if (!normals) raw.nrm.clear();
    if (!colours) raw.col.clear();
    if (raw.pos.isEmpty()) { *error = QStringLiteral("no faces"); return false; }
    return true;
}

bool loadStl(const QString& path, Raw& raw, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { *error = QStringLiteral("cannot open"); return false; }
    const QByteArray data = f.readAll();
    if (data.size() >= 84) {   // binary: 80-byte header, count, 50 bytes per triangle
        quint32 n = 0;
        std::memcpy(&n, data.constData() + 80, 4);
        if (qint64(84) + qint64(n) * 50 == data.size()) {
            if (n > quint32(ModelLibrary::kMaxTriangles)) { *error = QStringLiteral("too many triangles"); return false; }
            const char* p = data.constData() + 84;
            for (quint32 i = 0; i < n; ++i, p += 50) {
                float fv[12];
                std::memcpy(fv, p, 48);
                for (int k = 0; k < 3; ++k) raw.pos.append(QVector3D(fv[3 + k * 3], fv[4 + k * 3], fv[5 + k * 3]));
            }
            return n > 0 || (*error = QStringLiteral("no triangles"), false);
        }
    }
    // text: "vertex x y z" lines, three per facet
    QTextStream in(data);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("vertex"))) {
            const auto t = line.split(' ', Qt::SkipEmptyParts);
            if (t.size() >= 4) raw.pos.append(QVector3D(t[1].toFloat(), t[2].toFloat(), t[3].toFloat()));
        }
    }
    raw.pos.resize(raw.pos.size() / 3 * 3);
    if (raw.pos.isEmpty()) { *error = QStringLiteral("not an STL model"); return false; }
    if (raw.pos.size() / 3 > ModelLibrary::kMaxTriangles) { *error = QStringLiteral("too many triangles"); return false; }
    return true;
}
} // namespace

ModelLibrary::ModelLibrary(QObject* parent) : QObject(parent) {}
ModelLibrary::~ModelLibrary()
{
    ++m_ticket;
    if (m_worker.joinable()) m_worker.join();
}

ModelLibrary::Mesh ModelLibrary::load(const QString& path, QString* error)
{
    Raw raw;
    const QString ext = QFileInfo(path).suffix().toLower();
    const bool ok = ext == "obj" ? loadObj(path, raw, error) : loadStl(path, raw, error);
    Mesh m;
    if (!ok) return m;
    // STL models are usually Z-up (3D printing); OBJ is Y-up.
    if (ext == "stl")
        for (QVector3D& p : raw.pos) p = QVector3D(p.x(), p.z(), -p.y());
    if (ext == "stl" && !raw.nrm.isEmpty())
        for (QVector3D& n : raw.nrm) n = QVector3D(n.x(), n.z(), -n.y());
    // Smooth normals when the file has none (shared positions averaged).
    if (raw.nrm.size() != raw.pos.size()) {
        QHash<QString, QVector3D> acc;
        auto key = [](const QVector3D& p) { return QStringLiteral("%1,%2,%3").arg(p.x(), 0, 'g', 7).arg(p.y(), 0, 'g', 7).arg(p.z(), 0, 'g', 7); };
        QVector<QVector3D> faceN(raw.pos.size() / 3);
        for (int t = 0; t < faceN.size(); ++t) {
            faceN[t] = QVector3D::crossProduct(raw.pos[t * 3 + 1] - raw.pos[t * 3], raw.pos[t * 3 + 2] - raw.pos[t * 3]);
            for (int k = 0; k < 3; ++k) acc[key(raw.pos[t * 3 + k])] += faceN[t];
        }
        raw.nrm.resize(raw.pos.size());
        for (int i = 0; i < raw.pos.size(); ++i) {
            QVector3D n = acc.value(key(raw.pos[i]));
            if (n.lengthSquared() < 1e-20f) n = faceN[i / 3];
            raw.nrm[i] = n.normalized();
        }
    }
    // Centre it, stand it on its base, scale to statue height (and no wider than 1.4).
    QVector3D lo(1e30f, 1e30f, 1e30f), hi(-1e30f, -1e30f, -1e30f);
    for (const QVector3D& p : raw.pos) { lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
                                         hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z())); }
    const QVector3D size = hi - lo;
    if (size.y() <= 0.f) { *error = QStringLiteral("flat model"); return m; }
    const float scale = std::min(kHeight / size.y(), 1.4f / std::max(1e-6f, std::max(size.x(), size.z())));
    const QVector3D base((lo.x() + hi.x()) * 0.5f, lo.y(), (lo.z() + hi.z()) * 0.5f);
    m.name = QFileInfo(path).fileName();
    m.triangles = raw.pos.size() / 3;
    m.hasColours = !raw.col.isEmpty();
    m.vertices.reserve(raw.pos.size() * 9);
    for (int i = 0; i < raw.pos.size(); ++i) {
        const QVector3D p = (raw.pos[i] - base) * scale, n = raw.nrm[i];
        const QVector3D c = m.hasColours ? raw.col[i] : QVector3D(0.8f, 0.8f, 0.8f);
        m.vertices << p.x() << p.y() << p.z() << n.x() << n.y() << n.z() << c.x() << c.y() << c.z();
    }
    return m;
}

void ModelLibrary::setFolder(const QString& folder)
{
    if (folder == m_folder && (m_loading || m_generation > 0)) return;
    m_folder = folder;
    const int ticket = ++m_ticket;
    if (m_worker.joinable()) m_worker.join();
    if (folder.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_meshes.clear(); m_skipped.clear(); ++m_generation;
        emit loaded();
        return;
    }
    m_loading = true;
    m_worker = std::thread([this, folder, ticket] {
        QVector<Mesh> meshes;
        QStringList skipped;
        const QFileInfoList files = QDir(folder).entryInfoList({"*.obj", "*.OBJ", "*.stl", "*.STL"}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files) {
            if (m_ticket != ticket) return;   // a newer folder was chosen
            if (meshes.size() >= kMaxModels) { skipped << fi.fileName() + QStringLiteral(": more than 6 models"); continue; }
            QString err;
            Mesh m = load(fi.absoluteFilePath(), &err);
            if (m.triangles > 0) meshes.append(std::move(m));
            else skipped << fi.fileName() + QStringLiteral(": ") + err;
        }
        {
            QMutexLocker lock(&m_mutex);
            if (m_ticket != ticket) return;
            m_meshes = std::move(meshes);
            m_skipped = skipped;
            ++m_generation;
        }
        m_loading = false;
        QMetaObject::invokeMethod(this, &ModelLibrary::loaded, Qt::QueuedConnection);
    });
}

QVector<ModelLibrary::Mesh> ModelLibrary::meshes(int* generation) const
{
    QMutexLocker lock(&m_mutex);
    if (generation) *generation = m_generation;
    return m_meshes;
}

QStringList ModelLibrary::skipped() const
{
    QMutexLocker lock(&m_mutex);
    return m_skipped;
}
