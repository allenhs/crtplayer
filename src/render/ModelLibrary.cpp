#include "ModelLibrary.h"
#include "ModelRaw.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QMutexLocker>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace {
using Up = ModelLibrary::Up;

// A flat side counts when it covers this much of the model's footprint on that side, and
// lies within this much of the model's extent of the side's plane.
const double kFlatShare = 0.06;
const float kFlatSlab = 0.005f;

// The file's point p, with the file's `up` axis turned to +Y (a rotation: nothing is mirrored).
inline QVector3D turned(Up up, const QVector3D& p)
{
    switch (up) {
    case ModelLibrary::UpZ: return QVector3D(p.x(), p.z(), -p.y());
    case ModelLibrary::UpX: return QVector3D(-p.y(), p.x(), p.z());
    case ModelLibrary::UpNegY: return QVector3D(-p.x(), -p.y(), p.z());
    case ModelLibrary::UpNegZ: return QVector3D(p.x(), -p.z(), p.y());
    case ModelLibrary::UpNegX: return QVector3D(p.y(), -p.x(), p.z());
    default: return p;
    }
}

void bounds(const QVector<QVector3D>& v, QVector3D& lo, QVector3D& hi)
{
    lo = QVector3D(1e30f, 1e30f, 1e30f);
    hi = QVector3D(-1e30f, -1e30f, -1e30f);
    for (const QVector3D& p : v) {
        lo = QVector3D(std::min(lo.x(), p.x()), std::min(lo.y(), p.y()), std::min(lo.z(), p.z()));
        hi = QVector3D(std::max(hi.x(), p.x()), std::max(hi.y(), p.y()), std::max(hi.z(), p.z()));
    }
}

// How much of each side of its bounding box the model lies flat against, as a share of
// that side: -x +x -y +y -z +z. A statue's base gives a large share on one side only.
std::array<double, 6> flatShares(const RawModel& raw, const QVector<float>& dot)
{
    std::array<double, 6> area{};
    QVector3D lo, hi;
    bounds(raw.v, lo, hi);
    const QVector3D ext = hi - lo;
    float lower[3], upper[3];
    for (int a = 0; a < 3; ++a) { lower[a] = lo[a] + ext[a] * kFlatSlab; upper[a] = hi[a] - ext[a] * kFlatSlab; }
    if (raw.points) {
        for (int i = 0; i < raw.v.size(); ++i)
            for (int a = 0; a < 3; ++a) {
                const double d = 4.0 * dot[i] * dot[i];
                if (raw.v[i][a] <= lower[a]) area[a * 2] += d;
                if (raw.v[i][a] >= upper[a]) area[a * 2 + 1] += d;
            }
    } else {
        for (int t = 0; t + 2 < raw.idx.size(); t += 3) {
            const QVector3D& A = raw.v[raw.idx[t]], &B = raw.v[raw.idx[t + 1]], &C = raw.v[raw.idx[t + 2]];
            for (int a = 0; a < 3; ++a) {
                const bool low = A[a] <= lower[a] && B[a] <= lower[a] && C[a] <= lower[a];
                const bool high = A[a] >= upper[a] && B[a] >= upper[a] && C[a] >= upper[a];
                if (!low && !high) continue;
                const double tri = 0.5 * double(QVector3D::crossProduct(B - A, C - A).length());
                if (low) area[a * 2] += tri;
                if (high) area[a * 2 + 1] += tri;
            }
        }
    }
    std::array<double, 6> share{};
    for (int a = 0; a < 3; ++a) {
        const double side = double(ext[(a + 1) % 3]) * double(ext[(a + 2) % 3]);
        if (side <= 0.0) continue;
        share[a * 2] = std::min(1.0, area[a * 2] / side);
        share[a * 2 + 1] = std::min(1.0, area[a * 2 + 1] / side);
    }
    return share;
}

// ---- point clouds
struct Sym3 { double xx = 0, xy = 0, xz = 0, yy = 0, yz = 0, zz = 0; };
// The direction a patch of points varies least in (the surface's normal): the eigenvector
// of the smallest eigenvalue, by Jacobi rotations.
QVector3D leastDirection(const Sym3& s)
{
    double a[3][3] = {{s.xx, s.xy, s.xz}, {s.xy, s.yy, s.yz}, {s.xz, s.yz, s.zz}};
    double v[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int sweep = 0; sweep < 12; ++sweep) {
        const double off = std::abs(a[0][1]) + std::abs(a[0][2]) + std::abs(a[1][2]);
        if (off < 1e-18 * (std::abs(a[0][0]) + std::abs(a[1][1]) + std::abs(a[2][2]) + 1e-300)) break;
        for (int p = 0; p < 2; ++p)
            for (int q = p + 1; q < 3; ++q) {
                if (std::abs(a[p][q]) < 1e-300) continue;
                const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
                const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), sn = t * c;
                for (int k = 0; k < 3; ++k) { const double x = a[k][p], y = a[k][q]; a[k][p] = c * x - sn * y; a[k][q] = sn * x + c * y; }
                for (int k = 0; k < 3; ++k) { const double x = a[p][k], y = a[q][k]; a[p][k] = c * x - sn * y; a[q][k] = sn * x + c * y; }
                for (int k = 0; k < 3; ++k) { const double x = v[k][p], y = v[k][q]; v[k][p] = c * x - sn * y; v[k][q] = sn * x + c * y; }
            }
    }
    int m = 0;
    if (a[1][1] < a[m][m]) m = 1;
    if (a[2][2] < a[m][m]) m = 2;
    return QVector3D(float(v[0][m]), float(v[1][m]), float(v[2][m]));
}

// A point cloud: stray points dropped, thinned to what can be drawn, and for every point
// the size of its dot (from how close its neighbours are) and a normal (from the file,
// or from the neighbours).
void preparePoints(RawModel& raw, QVector<float>& dot, int maxPoints)
{
    auto keep = [&raw](const QVector<int>& which) {
        QVector<QVector3D> v, n, c;
        v.reserve(which.size());
        for (int i : which) { v.append(raw.v[i]); if (!raw.vn.isEmpty()) n.append(raw.vn[i]); if (!raw.vc.isEmpty()) c.append(raw.vc[i]); }
        raw.v = v; raw.vn = n; raw.vc = c;
    };
    if (raw.vn.size() != raw.v.size()) raw.vn.clear();
    if (raw.vc.size() != raw.v.size()) raw.vc.clear();
    if (raw.v.size() > maxPoints) {
        QVector<int> which;
        for (int i = 0; i < maxPoints; ++i) which.append(int(qint64(i) * raw.v.size() / maxPoints));
        keep(which);
    }
    if (raw.v.size() >= 200) {   // strays: beyond the middle 98% of the points by half its width again
        float lo[3], hi[3];
        QVector<float> axis(raw.v.size());
        for (int a = 0; a < 3; ++a) {
            for (int i = 0; i < raw.v.size(); ++i) axis[i] = raw.v[i][a];
            const int k = raw.v.size() / 100;
            std::nth_element(axis.begin(), axis.begin() + k, axis.end());
            const float p1 = axis[k];
            std::nth_element(axis.begin(), axis.end() - 1 - k, axis.end());
            const float p99 = axis[axis.size() - 1 - k];
            lo[a] = p1 - (p99 - p1) * 0.5f;
            hi[a] = p99 + (p99 - p1) * 0.5f;
        }
        QVector<int> which;
        for (int i = 0; i < raw.v.size(); ++i) {
            const QVector3D& p = raw.v[i];
            if (p.x() >= lo[0] && p.x() <= hi[0] && p.y() >= lo[1] && p.y() <= hi[1] && p.z() >= lo[2] && p.z() <= hi[2]) which.append(i);
        }
        if (which.size() < raw.v.size() && which.size() >= 3) keep(which);
    }
    const int n = raw.v.size();
    bool fileNormals = raw.vn.size() == n;
    if (fileNormals) {
        int zero = 0;
        for (const QVector3D& x : raw.vn) if (x.lengthSquared() < 1e-12f) ++zero;
        fileNormals = zero * 100 < n;
    }
    if (!fileNormals) raw.vn.fill(QVector3D(), n);
    // a grid of cells about two point spacings wide; each point looks at its 27 cells
    QVector3D lo, hi;
    bounds(raw.v, lo, hi);
    const QVector3D ext = hi - lo;
    const double surface = std::max(1e-30, double(ext.x()) * ext.y() + double(ext.x()) * ext.z() + double(ext.y()) * ext.z());
    const float cell = std::max(float(2.5 * std::sqrt(surface / std::max(1, n))), std::max({ext.x(), ext.y(), ext.z()}) / 1000.f + 1e-30f);
    auto cellOf = [&](const QVector3D& p, int a) { return std::clamp(int((p[a] - lo[a]) / cell), 0, 2047); };
    auto key = [](int x, int y, int z) { return (quint64(x) << 42) | (quint64(y) << 21) | quint64(z); };
    QVector<QPair<quint64, int>> order(n);
    for (int i = 0; i < n; ++i) order[i] = {key(cellOf(raw.v[i], 0), cellOf(raw.v[i], 1), cellOf(raw.v[i], 2)), i};
    std::sort(order.begin(), order.end());
    QHash<quint64, QPair<int, int>> cells;   // first, count
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && order[j].first == order[i].first) ++j;
        cells.insert(order[i].first, {i, j - i});
        i = j;
    }
    dot.fill(0.f, n);
    QVector<QPair<float, int>> neigh;
    const QVector3D centre = (lo + hi) * 0.5f;
    for (int i = 0; i < n; ++i) {
        const QVector3D& p = raw.v[i];
        neigh.clear();
        const int cx = cellOf(p, 0), cy = cellOf(p, 1), cz = cellOf(p, 2);
        for (int x = std::max(0, cx - 1); x <= cx + 1; ++x)
            for (int y = std::max(0, cy - 1); y <= cy + 1; ++y)
                for (int z = std::max(0, cz - 1); z <= cz + 1; ++z) {
                    const auto it = cells.constFind(key(x, y, z));
                    if (it == cells.constEnd()) continue;
                    const int count = std::min(it->second, 64);
                    for (int k = 0; k < count; ++k) {
                        const int j = order[it->first + k].second;
                        const float d2 = (raw.v[j] - p).lengthSquared();
                        if (j != i && d2 > 0.f) neigh.append({d2, j});
                    }
                }
        const int k = std::min<int>(10, neigh.size());
        std::partial_sort(neigh.begin(), neigh.begin() + k, neigh.end());
        if (k >= 1) dot[i] = 0.85f * std::sqrt(neigh[std::min(3, k - 1)].first);
        if (fileNormals) continue;
        QVector3D nrm = p - centre;
        if (k >= 3) {
            QVector3D mean = p;
            for (int j = 0; j < k; ++j) mean += raw.v[neigh[j].second];
            mean /= float(k + 1);
            Sym3 s;
            auto add = [&s, &mean](const QVector3D& q) { const QVector3D e = q - mean; s.xx += double(e.x()) * e.x(); s.xy += double(e.x()) * e.y(); s.xz += double(e.x()) * e.z();
                                                           s.yy += double(e.y()) * e.y(); s.yz += double(e.y()) * e.z(); s.zz += double(e.z()) * e.z(); };
            add(p);
            for (int j = 0; j < k; ++j) add(raw.v[neigh[j].second]);
            nrm = leastDirection(s);
        }
        raw.vn[i] = nrm;
    }
    // dot sizes: within reach of the usual one (a lone point gets the usual size)
    QVector<float> sorted;
    for (float d : dot) if (d > 0.f) sorted.append(d);
    float usual = std::max({ext.x(), ext.y(), ext.z()}) * 0.01f;
    if (!sorted.isEmpty()) { std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end()); usual = sorted[sorted.size() / 2]; }
    for (float& d : dot) d = d > 0.f ? std::clamp(d, usual * 0.5f, usual * 2.5f) : usual;
}

// ---- pictures wrapped around a model
// The model is drawn with a colour at each triangle corner, so a picture is sampled
// there; a model of few triangles is divided into smaller ones first, so that the
// picture still shows. Each sample is taken from a copy of the picture reduced to about
// one pixel per sample (a sharper copy would sparkle).
const int kTexturedTriangles = 150000;   // divide up to about this many
struct Picture {
    QVector<QImage> level;   // the picture, then halves of it
    explicit Picture(const QImage& img)
    {
        level.append(img);
        while (std::max(level.last().width(), level.last().height()) > 1)
            level.append(level.last().scaled(std::max(1, level.last().width() / 2), std::max(1, level.last().height() / 2), Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }
    QVector3D at(float u, float v, float texels) const   // texels: how many of the picture's pixels one sample stands for
    {
        if (!(u == u) || !(v == v) || std::abs(u) > 1e6f || std::abs(v) > 1e6f) u = v = 0.f;
        const int l = std::clamp(int(std::floor(std::log2(std::max(texels, 1.f)) + 0.5f)), 0, int(level.size()) - 1);
        const QImage& im = level[l];
        const int w = im.width(), h = im.height();
        const float x = (u - std::floor(u)) * w - 0.5f, y = (v - std::floor(v)) * h - 0.5f;
        const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        const float fx = x - x0, fy = y - y0;
        auto px = [&](int xi, int yi) {
            const QRgb c = reinterpret_cast<const QRgb*>(im.constScanLine(((yi % h) + h) % h))[((xi % w) + w) % w];
            return QVector3D(qRed(c), qGreen(c), qBlue(c));
        };
        return ((px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) + (px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx) * fy) / 255.f;
    }
};
inline QVector3D toLinear(const QVector3D& c) { return QVector3D(std::pow(std::max(c.x(), 0.f), 2.2f), std::pow(std::max(c.y(), 0.f), 2.2f), std::pow(std::max(c.z(), 0.f), 2.2f)); }
inline QVector3D toDisplay(const QVector3D& c)
{
    return QVector3D(std::pow(std::clamp(c.x(), 0.f, 1.f), 1 / 2.2f), std::pow(std::clamp(c.y(), 0.f, 1.f), 1 / 2.2f), std::pow(std::clamp(c.z(), 0.f, 1.f), 1 / 2.2f));
}
struct Painter {
    const RawModel& raw;
    QVector<Picture> pictures;
    explicit Painter(const RawModel& r) : raw(r) { for (const QImage& img : r.textures) pictures.append(Picture(img)); }
    bool textured(int tri) const { return raw.triTexture[tri] >= 0 && raw.triTexture[tri] < pictures.size(); }
    // The longest side of the triangle, in the picture's pixels.
    float span(int tri) const
    {
        const QImage& im = pictures[raw.triTexture[tri]].level[0];
        float longest = 0.f;
        for (int k = 0; k < 3; ++k) {
            const int a = tri * 3 + k, b = tri * 3 + (k + 1) % 3;
            longest = std::max(longest, std::hypot((raw.uv[a * 2] - raw.uv[b * 2]) * im.width(), (raw.uv[a * 2 + 1] - raw.uv[b * 2 + 1]) * im.height()));
        }
        return longest;
    }
    QVector3D colour(int tri, float u, float v, const QVector3D& mul, float texels) const
    {
        const QVector3D c = pictures[raw.triTexture[tri]].at(u, v, texels);
        return raw.cmul.isEmpty() ? c : toDisplay(toLinear(c) * mul);
    }
};
// The point k of n steps along the side from p to q: the same bits from whichever
// triangle's side it is asked for, so that neighbours divided alike leave no cracks.
inline QVector3D sidePoint(const QVector3D& p, const QVector3D& q, int k, int n)
{
    if (k <= 0) return p;
    if (k >= n) return q;
    const bool swap = q.x() < p.x() || (q.x() == p.x() && (q.y() < p.y() || (q.y() == p.y() && q.z() < p.z())));
    return swap ? q + (p - q) * (float(n - k) / n) : p + (q - p) * (float(k) / n);
}

// ---- large meshes
// Fewer triangles by merging the vertices that share a cell of a grid over the model
// (their average takes their place); triangles that collapse or repeat are dropped.
int clusterMesh(const RawModel& raw, int cellsAcross, const QVector3D& lo, float longest, RawModel* out)
{
    const float scale = cellsAcross / longest;
    auto cell = [&](const QVector3D& p) {
        const int x = std::clamp(int((p.x() - lo.x()) * scale), 0, cellsAcross), y = std::clamp(int((p.y() - lo.y()) * scale), 0, cellsAcross),
                  z = std::clamp(int((p.z() - lo.z()) * scale), 0, cellsAcross);
        return (quint64(x) << 42) | (quint64(y) << 21) | quint64(z);
    };
    QHash<quint64, quint32> ids;
    ids.reserve(raw.v.size() / 4);
    QVector<quint32> to(raw.v.size());
    for (int i = 0; i < raw.v.size(); ++i) {
        const quint64 k = cell(raw.v[i]);
        auto it = ids.constFind(k);
        if (it == ids.constEnd()) it = ids.insert(k, quint32(ids.size()));
        to[i] = *it;
    }
    const int n = ids.size();
    QSet<quint64> seen;
    QVector<quint32> idx;
    const bool pack = n < (1 << 21);
    for (int t = 0; t + 2 < raw.idx.size(); t += 3) {
        const quint32 a = to[raw.idx[t]], b = to[raw.idx[t + 1]], c = to[raw.idx[t + 2]];
        if (a == b || b == c || a == c) continue;
        if (pack) {
            quint32 s[3] = {a, b, c};
            std::sort(s, s + 3);
            const quint64 k = (quint64(s[0]) << 42) | (quint64(s[1]) << 21) | quint64(s[2]);
            if (seen.contains(k)) continue;
            seen.insert(k);
        }
        idx << a << b << c;
    }
    if (!out) return idx.size() / 3;
    QVector<QVector3D> sum(n), col(n);
    QVector<float> count(n, 0.f), colCount(n, 0.f);
    for (int i = 0; i < raw.v.size(); ++i) { sum[to[i]] += raw.v[i]; count[to[i]] += 1.f; }
    const bool perCorner = raw.cc.size() == raw.idx.size() && !raw.cc.isEmpty(), perVertex = !perCorner && raw.vc.size() == raw.v.size() && !raw.vc.isEmpty();
    if (perCorner) for (int i = 0; i < raw.idx.size(); ++i) { col[to[raw.idx[i]]] += raw.cc[i]; colCount[to[raw.idx[i]]] += 1.f; }
    if (perVertex) for (int i = 0; i < raw.v.size(); ++i) { col[to[i]] += raw.vc[i]; colCount[to[i]] += 1.f; }
    out->v.resize(n);
    for (int i = 0; i < n; ++i) out->v[i] = sum[i] / std::max(1.f, count[i]);
    out->vc.clear();
    if (perCorner || perVertex) { out->vc.resize(n); for (int i = 0; i < n; ++i) out->vc[i] = colCount[i] > 0.f ? col[i] / colCount[i] : QVector3D(0.8f, 0.8f, 0.8f); }
    out->idx = idx;
    out->vn.clear(); out->cn.clear(); out->cc.clear();
    return idx.size() / 3;
}

void simplify(RawModel& raw, int target)
{
    QVector3D lo, hi;
    bounds(raw.v, lo, hi);
    const float longest = std::max({hi.x() - lo.x(), hi.y() - lo.y(), hi.z() - lo.z(), 1e-30f});
    // The count grows about with the square of the grid's fineness: aim a little under the limit.
    int cells = 192, best = 0;
    for (int tries = 0; tries < 10; ++tries) {
        const int n = clusterMesh(raw, cells, lo, longest, nullptr);
        int next;
        if (n <= target) {
            best = std::max(best, cells);
            if (n > target * 0.6 || cells >= 2000) break;
            next = std::min(2000, std::max(cells + 1, int(cells * std::sqrt(0.85 * target / std::max(1, n)))));
        } else next = std::max(8, std::min(cells - 1, int(cells * std::sqrt(0.85 * target / n))));
        if (best && next <= best) break;
        cells = next;
    }
    if (!best) {
        while (cells > 8 && clusterMesh(raw, cells, lo, longest, nullptr) > target) cells = cells * 3 / 4;
        best = std::max(8, cells);
    }
    RawModel out;
    out.points = false;
    clusterMesh(raw, best, lo, longest, &out);
    raw.v = out.v; raw.vc = out.vc; raw.idx = out.idx;
    raw.vn.clear(); raw.cn.clear(); raw.cc.clear();
}

// ---- normals
struct PosKey {
    quint32 a, b, c;
    bool operator==(const PosKey& o) const { return a == o.a && b == o.b && c == o.c; }
};
inline size_t qHash(const PosKey& k, size_t seed = 0) { return qHashMulti(seed, k.a, k.b, k.c); }

// One normal per triangle corner: the faces around the corner's point are averaged
// (by area), leaving out those across a sharp edge (more than 60 degrees), so that
// round shapes come out smooth and boxes keep their edges.
QVector<QVector3D> cornerNormals(const RawModel& raw)
{
    const int nt = raw.idx.size() / 3;
    QVector<quint32> weld(raw.v.size());
    {
        QHash<PosKey, quint32> ids;
        ids.reserve(raw.v.size());
        for (int i = 0; i < raw.v.size(); ++i) {
            const float f[3] = {raw.v[i].x() + 0.f, raw.v[i].y() + 0.f, raw.v[i].z() + 0.f};   // -0 becomes 0
            PosKey k;
            std::memcpy(&k, f, 12);
            auto it = ids.constFind(k);
            if (it == ids.constEnd()) it = ids.insert(k, quint32(ids.size()));
            weld[i] = *it;
        }
    }
    int nw = 0;
    for (quint32 w : weld) nw = std::max(nw, int(w) + 1);
    QVector<QVector3D> face(nt), unit(nt);
    QVector<int> first(nw + 1, 0);
    for (int t = 0; t < nt; ++t) {
        const QVector3D& A = raw.v[raw.idx[t * 3]];
        face[t] = QVector3D::crossProduct(raw.v[raw.idx[t * 3 + 1]] - A, raw.v[raw.idx[t * 3 + 2]] - A);
        unit[t] = face[t].lengthSquared() > 0.f ? face[t].normalized() : QVector3D();
        for (int k = 0; k < 3; ++k) ++first[weld[raw.idx[t * 3 + k]] + 1];
    }
    for (int i = 0; i < nw; ++i) first[i + 1] += first[i];
    QVector<int> around(nt * 3), fill = first;
    for (int t = 0; t < nt; ++t) for (int k = 0; k < 3; ++k) around[fill[weld[raw.idx[t * 3 + k]]]++] = t;
    QVector<QVector3D> whole(nw);   // every face around a point (for points with very many faces)
    for (int t = 0; t < nt; ++t) for (int k = 0; k < 3; ++k) whole[weld[raw.idx[t * 3 + k]]] += face[t];
    const float crease = 0.5f;   // cos 60
    QVector<QVector3D> out(nt * 3);
    for (int t = 0; t < nt; ++t)
        for (int k = 0; k < 3; ++k) {
            const quint32 w = weld[raw.idx[t * 3 + k]];
            QVector3D sum;
            if (first[w + 1] - first[w] > 256) sum = whole[w];
            else for (int j = first[w]; j < first[w + 1]; ++j) if (QVector3D::dotProduct(unit[t], unit[around[j]]) >= crease) sum += face[around[j]];
            if (sum.lengthSquared() < 1e-30f) sum = unit[t];
            out[t * 3 + k] = sum.lengthSquared() > 0.f ? sum.normalized() : QVector3D(0.f, 1.f, 0.f);
        }
    return out;
}
} // namespace

QString ModelLibrary::upName(Up up)
{
    static const char* names[] = {"auto", "Y", "Z", "X", "-Y", "-Z", "-X"};
    return QString::fromLatin1(names[std::clamp(int(up), 0, 6)]);
}

ModelLibrary::Up ModelLibrary::upFromName(const QString& name)
{
    const QString n = name.trimmed().toUpper();
    for (int i = 1; i <= 6; ++i) if (n == upName(Up(i)) || n == QLatin1Char('+') + upName(Up(i))) return Up(i);
    return UpAuto;
}

QString ModelLibrary::Mesh::describe() const
{
    const QLocale loc;
    QString t = name + QStringLiteral(": ");
    if (points) {
        t += QObject::tr("%1 points").arg(loc.toString(triangles / 2));
        if (sourcePoints > triangles / 2) t += QObject::tr(" of %1").arg(loc.toString(sourcePoints));
    } else {
        t += QObject::tr("%1 triangles").arg(loc.toString(std::min(triangles, sourceTriangles)));   // (a pictured model is drawn with more than its file holds)
        if (sourceTriangles > triangles) t += QObject::tr(" (simplified from %1)").arg(loc.toString(sourceTriangles));
    }
    const QString why = upWhy == 0 ? QObject::tr("stands on its flat base") : upWhy == 3 ? QObject::tr("by its flat top")
                      : upWhy == 2 ? QObject::tr("chosen") : upWhy == 4 ? QObject::tr("as the file says") : QObject::tr("as such files usually are");
    return t + QObject::tr(", %1 up (%2)").arg(upName(up), why);
}

ModelLibrary::ModelLibrary(QObject* parent) : QObject(parent) {}
ModelLibrary::~ModelLibrary()
{
    ++m_ticket;
    if (m_worker.joinable()) m_worker.join();
}

ModelLibrary::Mesh ModelLibrary::load(const QString& path, QString* error, Up wanted, bool light)
{
    const int maxTriangles = light ? kLightTriangles : kMaxTriangles, maxPoints = light ? kLightPoints : kMaxPoints;
    const int texturedTriangles = light ? kTexturedTriangles / 5 : kTexturedTriangles;
    RawModel raw;
    Mesh m;
    const QString ext = QFileInfo(path).suffix().toLower();
    const bool ok = ext == "obj" ? loadObjModel(path, raw, error) : ext == "ply" ? loadPlyModel(path, raw, error)
                  : ext == "glb" || ext == "gltf" ? loadGltfModel(path, raw, error) : ext == "fbx" ? loadFbxModel(path, raw, error)
                  : loadStlModel(path, raw, error);
    if (!ok) return m;
    for (const QVector3D& p : raw.v)
        if (!(std::abs(p.x()) < 1e18f && std::abs(p.y()) < 1e18f && std::abs(p.z()) < 1e18f)) { *error = QStringLiteral("the file holds numbers that are not positions"); return m; }
    m.points = raw.points;
    m.sourceTriangles = raw.idx.size() / 3;
    m.sourcePoints = raw.points ? int(std::max<qint64>(raw.filePoints, raw.v.size())) : 0;
    QVector<float> dot;
    if (raw.points) {
        preparePoints(raw, dot, maxPoints);
        if (raw.v.size() < 3) { *error = QStringLiteral("no faces"); return m; }
    }

    // Which way is up. FBX and glTF files say so themselves (their readers have already
    // turned them Y-up). The others do not: STL models are usually Z-up (3D printing),
    // OBJ and PLY Y-up; but a model with a flat base (or top) on the other of the two
    // axes, and none on the usual one, is stood on that.
    const bool declared = ext == "fbx" || ext == "glb" || ext == "gltf";
    const Up usual = ext == "stl" ? UpZ : UpY, other = usual == UpY ? UpZ : UpY;
    const std::array<double, 6> flat = flatShares(raw, dot);
    auto along = [&flat](Up u) { return u == UpY ? std::max(flat[2], flat[3]) : std::max(flat[4], flat[5]); };
    auto under = [&flat](Up u) { return u == UpY ? flat[2] : u == UpZ ? flat[4] : u == UpX ? flat[0] : u == UpNegY ? flat[3] : u == UpNegZ ? flat[5] : flat[1]; };
    // (A model with no height along an axis, a flat cut-out, cannot stand along it.)
    QVector3D lo0, hi0;
    bounds(raw.v, lo0, hi0);
    const QVector3D ext0 = hi0 - lo0;
    const float longest0 = std::max({ext0.x(), ext0.y(), ext0.z()});
    auto tall = [&](Up u) { return (u == UpY ? ext0.y() : ext0.z()) > longest0 * 1e-5f; };
    if (wanted != UpAuto) { m.up = wanted; m.upWhy = 2; }
    else if (!declared && !tall(usual) && tall(other)) { m.up = other; m.upWhy = 1; }
    else if (!declared && tall(other) && along(other) >= kFlatShare && along(usual) < along(other) / 3.0) { m.up = other; m.upWhy = under(other) >= kFlatShare ? 0 : 3; }
    else { m.up = usual; m.upWhy = under(usual) >= kFlatShare ? 0 : declared ? 4 : 1; }
    m.flatBase = float(under(m.up));
    if (m.up != UpY) {
        for (QVector3D& p : raw.v) p = turned(m.up, p);
        for (QVector3D& n : raw.vn) n = turned(m.up, n);
        for (QVector3D& n : raw.cn) n = turned(m.up, n);
    }

    // One position, normal and colour per triangle corner.
    QVector<QVector3D> pos, nrm, col;
    if (raw.points) {
        const int n = raw.v.size();
        m.hasColours = raw.vc.size() == n;
        pos.reserve(n * 6); nrm.reserve(n * 6);
        for (int i = 0; i < n; ++i) {   // a small square in the surface, as two triangles
            QVector3D nn = raw.vn[i].lengthSquared() > 1e-20f ? raw.vn[i].normalized() : QVector3D(0.f, 1.f, 0.f);
            const QVector3D side = std::abs(nn.y()) < 0.8f ? QVector3D(0.f, 1.f, 0.f) : QVector3D(1.f, 0.f, 0.f);
            const QVector3D a = QVector3D::crossProduct(nn, side).normalized() * dot[i], b = QVector3D::crossProduct(nn, a);
            const QVector3D& p = raw.v[i];
            const QVector3D q[4] = {p - a - b, p + a - b, p + a + b, p - a + b};
            for (int k : {0, 1, 2, 0, 2, 3}) { pos.append(q[k]); nrm.append(nn); if (m.hasColours) col.append(raw.vc[i]); }
        }
    } else {
        bool textured = !raw.textures.isEmpty() && raw.triTexture.size() == raw.idx.size() / 3 && raw.uv.size() == raw.idx.size() * 2
                        && (raw.cmul.isEmpty() || raw.cmul.size() == raw.idx.size());
        if (textured && raw.cc.size() != raw.idx.size()) raw.cc.fill(QVector3D(0.8f, 0.8f, 0.8f), raw.idx.size());
        if (textured && raw.idx.size() / 3 > maxTriangles) {   // too many triangles: the pictures become corner colours first
            const Painter paint(raw);
            for (int t = 0; t < raw.idx.size() / 3; ++t) {
                if (!paint.textured(t)) continue;
                const float texels = paint.span(t);
                for (int k = 0; k < 3; ++k) { const int i = t * 3 + k; raw.cc[i] = paint.colour(t, raw.uv[i * 2], raw.uv[i * 2 + 1], raw.cmul.isEmpty() ? QVector3D() : raw.cmul[i], texels); }
            }
            textured = false;
        }
        if (raw.idx.size() / 3 > maxTriangles) simplify(raw, maxTriangles);
        const int corners = raw.idx.size();
        if (corners < 3) { *error = QStringLiteral("no faces"); return m; }
        // the file's normals, unless some are missing
        bool fileNormals = raw.cn.size() == corners || (raw.cn.isEmpty() && raw.vn.size() == raw.v.size());
        if (fileNormals) {
            if (raw.cn.size() == corners) { for (const QVector3D& n : raw.cn) if (n.lengthSquared() < 1e-12f) { fileNormals = false; break; } }
            else for (quint32 i : raw.idx) if (raw.vn[i].lengthSquared() < 1e-12f) { fileNormals = false; break; }
        }
        if (!fileNormals) nrm = cornerNormals(raw);
        else if (raw.cn.size() == corners) { nrm = raw.cn; for (QVector3D& n : nrm) n.normalize(); }
        else { nrm.resize(corners); for (int i = 0; i < corners; ++i) nrm[i] = raw.vn[raw.idx[i]].normalized(); }
        pos.resize(corners);
        for (int i = 0; i < corners; ++i) pos[i] = raw.v[raw.idx[i]];
        if (raw.cc.size() == corners) col = raw.cc;
        else if (raw.vc.size() == raw.v.size() && !raw.vc.isEmpty()) { col.resize(corners); for (int i = 0; i < corners; ++i) col[i] = raw.vc[raw.idx[i]]; }
        m.hasColours = !col.isEmpty();
        if (textured) {
            // Every triangle is divided alike, n steps along each side (n = 1: only its corners are sampled).
            const Painter paint(raw);
            const int tris = corners / 3;
            const int n = std::clamp(int(std::sqrt(double(texturedTriangles) / tris)), 1, 64);
            QVector<QVector3D> p2, n2, c2;
            p2.reserve(tris * n * n * 3); n2.reserve(tris * n * n * 3); c2.reserve(tris * n * n * 3);
            QVector<QVector3D> gp((n + 1) * (n + 2) / 2), gn(gp.size()), gc(gp.size());
            auto at = [n](int i, int j) { return j * (n + 1) - j * (j - 1) / 2 + i; };   // row j holds n + 1 - j points
            for (int t = 0; t < tris; ++t) {
                const int a = t * 3, b = a + 1, c = a + 2;
                const bool pic = paint.textured(t);
                const float texels = pic ? paint.span(t) / n : 1.f;
                for (int j = 0; j <= n; ++j)
                    for (int i = 0; i + j <= n; ++i) {
                        const float wb = float(i) / n, wc = float(j) / n, wa = 1.f - wb - wc;
                        const int g = at(i, j);
                        gp[g] = j == 0 ? sidePoint(pos[a], pos[b], i, n) : i == 0 ? sidePoint(pos[a], pos[c], j, n) : i + j == n ? sidePoint(pos[b], pos[c], j, n)
                              : pos[a] * wa + pos[b] * wb + pos[c] * wc;
                        const QVector3D nn = nrm[a] * wa + nrm[b] * wb + nrm[c] * wc;
                        gn[g] = nn.lengthSquared() > 1e-12f ? nn.normalized() : nrm[a];
                        if (pic) {
                            const QVector3D mul = raw.cmul.isEmpty() ? QVector3D() : raw.cmul[a] * wa + raw.cmul[b] * wb + raw.cmul[c] * wc;
                            gc[g] = paint.colour(t, raw.uv[a * 2] * wa + raw.uv[b * 2] * wb + raw.uv[c * 2] * wc,
                                                 raw.uv[a * 2 + 1] * wa + raw.uv[b * 2 + 1] * wb + raw.uv[c * 2 + 1] * wc, mul, texels);
                        } else gc[g] = col[a] * wa + col[b] * wb + col[c] * wc;
                    }
                auto three = [&](int g0, int g1, int g2) { for (int g : {g0, g1, g2}) { p2.append(gp[g]); n2.append(gn[g]); c2.append(gc[g]); } };
                for (int j = 0; j < n; ++j)
                    for (int i = 0; i + j < n; ++i) {
                        three(at(i, j), at(i + 1, j), at(i, j + 1));
                        if (i + j < n - 1) three(at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
                    }
            }
            pos = p2; nrm = n2; col = c2;
            m.hasColours = true;
        }
    }

    // Centre it, stand it on its base, scale to statue height (and no wider than 1.4).
    QVector3D lo, hi;
    bounds(pos, lo, hi);
    const QVector3D size = hi - lo;
    if (!(size.y() > 0.f)) { *error = QStringLiteral("flat model"); return m; }
    const float scale = std::min(kHeight / size.y(), 1.4f / std::max(1e-6f, std::max(size.x(), size.z())));
    const QVector3D base((lo.x() + hi.x()) * 0.5f, lo.y(), (lo.z() + hi.z()) * 0.5f);
    m.name = QFileInfo(path).fileName();
    m.triangles = pos.size() / 3;
    m.size = size * scale;
    m.vertices.reserve(pos.size() * 9);
    for (int i = 0; i < pos.size(); ++i) {
        const QVector3D p = (pos[i] - base) * scale, n = nrm[i];
        const QVector3D c = m.hasColours ? col[i] : QVector3D(0.8f, 0.8f, 0.8f);
        m.vertices << p.x() << p.y() << p.z() << n.x() << n.y() << n.z() << c.x() << c.y() << c.z();
    }
    return m;
}

void ModelLibrary::setFolder(const QString& folder, Up up)
{
    if (folder == m_folder && up == m_up && m_light == m_lightLoaded && (m_loading || m_generation > 0)) return;
    m_folder = folder;
    m_up = up;
    m_lightLoaded = m_light;
    const int ticket = ++m_ticket;
    if (m_worker.joinable()) m_worker.join();
    if (folder.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        m_meshes.clear(); m_skipped.clear(); ++m_generation;
        emit loaded();
        return;
    }
    m_loading = true;
    m_worker = std::thread([this, folder, up, ticket, light = m_light] {
        QVector<Mesh> meshes;
        QStringList skipped;
        const QFileInfoList files = QDir(folder).entryInfoList({"*.obj", "*.stl", "*.ply", "*.glb", "*.gltf", "*.fbx"}, QDir::Files, QDir::Name);
        for (const QFileInfo& fi : files) {
            if (m_ticket != ticket) return;   // a newer folder was chosen
            if (meshes.size() >= kMaxModels) { skipped << fi.fileName() + QStringLiteral(": more than 6 models"); continue; }
            QString err;
            Mesh m = load(fi.absoluteFilePath(), &err, up, light);
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

QStringList ModelLibrary::described() const
{
    QMutexLocker lock(&m_mutex);
    QStringList out;
    for (const Mesh& m : m_meshes) out << m.describe();
    return out;
}
