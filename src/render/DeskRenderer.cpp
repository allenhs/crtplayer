#include "DeskRenderer.h"
#include <QOpenGLContext>

#include <QtMath>
#include <algorithm>

namespace {

struct Ring {
    QVector<QPointF> p;   // outline points
    QVector<QPointF> n;   // outward 2D normals
};

// Rounded rectangle outline with a fixed point count (4 * (seg + 1)), so any two
// rings can be joined point-to-point.
Ring roundedRect(double cx, double cy, double hw, double hh, double r, int seg = 10)
{
    r = std::clamp(r, 0.002, std::min(hw, hh) - 1e-4);
    const QPointF centres[4] = {{cx + hw - r, cy + hh - r}, {cx - hw + r, cy + hh - r},
                                {cx - hw + r, cy - hh + r}, {cx + hw - r, cy - hh + r}};
    Ring ring;
    for (int c = 0; c < 4; ++c) {
        for (int i = 0; i <= seg; ++i) {
            const double a = (c * 90.0 + 90.0 * i / seg) * M_PI / 180.0;
            const QPointF n(std::cos(a), std::sin(a));
            ring.p << centres[c] + n * r;
            ring.n << n;
        }
    }
    return ring;
}

QVector<QPointF> convexHull(QVector<QPointF> pts)
{
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    if (pts.size() < 3) return pts;
    auto cross = [](const QPointF& o, const QPointF& a, const QPointF& b) {
        return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
    };
    QVector<QPointF> h(2 * pts.size());
    int k = 0;
    for (int i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    for (int i = pts.size() - 2, t = k + 1; i >= 0; --i) {
        while (k >= t && cross(h[k - 2], h[k - 1], pts[i]) <= 0) --k;
        h[k++] = pts[i];
    }
    h.resize(k - 1);
    return h;
}

} // namespace

bool DeskRenderer::initialize(QString* error)
{
    if (!QOpenGLContext::currentContext() || QOpenGLContext::currentContext()->format().version() < qMakePair(3, 3)) {
        if (error) *error = QStringLiteral("CRT Player needs OpenGL 3.3 or newer.");
        return false;
    }
    initializeOpenGLFunctions();
    if (!m_prog.addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/desk.vert")) ||
        !m_prog.addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/desk.frag")) ||
        !m_prog.link()) {
        if (error) *error = QStringLiteral("Desk shader failed to build:\n") + m_prog.log();
        return false;
    }
    m_vao.create();
    m_vbo.create();
    // Room backdrop: a full-viewport quad (the shader casts a ray per pixel).
    if (!m_backdrop.addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/quad.vert")) ||
        !m_backdrop.addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/scene_desk.frag")) ||
        !m_backdrop.link()) {
        if (error) *error = QStringLiteral("Scene shader failed to build:\n") + m_backdrop.log();
        return false;
    }
    for (auto [prog, frag] : {std::pair<QOpenGLShaderProgram*, const char*>{&m_theater, ":/shaders/theater.frag"},
                              std::pair<QOpenGLShaderProgram*, const char*>{&m_theaterFront, ":/shaders/theater_front.frag"},
                              std::pair<QOpenGLShaderProgram*, const char*>{&m_cg, ":/shaders/cg90.frag"},
                              std::pair<QOpenGLShaderProgram*, const char*>{&m_flare, ":/shaders/flare.frag"}}) {
        if (!prog->addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/quad.vert")) ||
            !prog->addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, QString::fromLatin1(frag)) || !prog->link()) {
            if (error) *error = QStringLiteral("Theater shader failed to build:\n") + prog->log();
            return false;
        }
    }
    if (!m_model.addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/model.vert")) ||
        !m_model.addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/model.frag")) || !m_model.link()) {
        if (error) *error = QStringLiteral("Model shader failed to build:\n") + m_model.log();
        return false;
    }
    {   // a unit box (-0.5..0.5) for the plinths
        QVector<float> v;
        const float n[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (auto& f : n) {
            const QVector3D N(f[0], f[1], f[2]);
            const QVector3D U = std::abs(N.y()) > 0.5f ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
            const QVector3D R = QVector3D::crossProduct(U, N);
            const QVector3D c = N * 0.5f, a = R * 0.5f, b = U * 0.5f;
            const QVector3D q[6] = {c - a - b, c + a - b, c + a + b, c - a - b, c + a + b, c - a + b};
            for (const QVector3D& p : q) v << p.x() << p.y() << p.z() << N.x() << N.y() << N.z() << 0.9f << 0.9f << 0.9f;
        }
        m_plinth = upload(v);
    }
    if (!m_haze.addCacheableShaderFromSourceFile(QOpenGLShader::Vertex, QStringLiteral(":/shaders/quad.vert")) ||
        !m_haze.addCacheableShaderFromSourceFile(QOpenGLShader::Fragment, QStringLiteral(":/shaders/haze.frag")) ||
        !m_haze.link()) {
        if (error) *error = QStringLiteral("Haze shader failed to build:\n") + m_haze.log();
        return false;
    }
    m_quadVao.create();
    m_quadVao.bind();
    m_quadVbo.create();
    m_quadVbo.bind();
    const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    m_quadVbo.allocate(quad, sizeof quad);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    m_quadVao.release();
    m_quadVbo.release();
    m_initialized = true;
    m_dirty = true;
    return true;
}

void DeskRenderer::destroy()
{
    if (!m_initialized) return;
    m_vbo.destroy();
    m_vao.destroy();
    m_initialized = false;
}

void DeskRenderer::setLayout(float aspect, bool pivot, int cabinet, bool wallMount)
{
    aspect = std::max(1.0f, aspect);
    cabinet = std::clamp(cabinet, 0, 7);
    if (std::abs(aspect - m_aspect) < 1e-3f && pivot == m_pivot && cabinet == m_cabinet && wallMount == m_wallMount &&
        !m_verts.isEmpty()) return;
    m_aspect = aspect;
    m_pivot = pivot;
    m_cabinet = cabinet;
    m_wallMount = wallMount;
    m_dirty = true;
}

void DeskRenderer::addTri(const Vertex& a, const Vertex& b, const Vertex& c)
{
    m_verts << a << b << c;
}

void DeskRenderer::build()
{
    m_verts.clear();
    const float A = m_aspect;
    const float gw = A * 0.5f, gh = 0.5f;          // glass half size
    const bool panel = m_cabinet == FlatPanel;
    // Bezel margins and glass: chunky 90s set, or a slim panel with thin bezels.
    if (panel) { m_marginSide = 0.03f; m_marginTop = 0.03f; m_marginBottom = 0.045f; m_glassRadius = 0.004f; m_glassZ = -0.004f; }
    else if (m_cabinet == WoodConsole)  { m_marginSide = 0.20f; m_marginTop = 0.15f; m_marginBottom = 0.48f; m_glassRadius = 0.07f; m_glassZ = -0.05f; }
    else if (m_cabinet == PvmMonitor)   { m_marginSide = 0.07f; m_marginTop = 0.06f; m_marginBottom = 0.20f; m_glassRadius = 0.04f; m_glassZ = -0.035f; }
    else if (m_cabinet == BeigeMonitor) { m_marginSide = 0.10f; m_marginTop = 0.09f; m_marginBottom = 0.14f; m_glassRadius = 0.05f; m_glassZ = -0.04f; }
    else if (m_cabinet == TheaterScreen) { m_marginSide = 0.f; m_marginTop = 0.f; m_marginBottom = 0.f; m_glassRadius = 0.f; m_glassZ = 0.f; }
    else if (m_cabinet == ArcadeCabinet) { m_marginSide = 0.f; m_marginTop = 0.f; m_marginBottom = 0.f; m_glassRadius = 0.025f; m_glassZ = -0.07f; }
    else       { m_marginSide = 0.13f; m_marginTop = 0.12f; m_marginBottom = 0.30f;  m_glassRadius = 0.06f;  m_glassZ = -0.045f; }
    const bool theater = m_cabinet == TheaterScreen;
    const bool arcade = m_cabinet == ArcadeCabinet;
    const float rg = m_glassRadius;
    const float lip = panel ? 0.003f : 0.02f;
    const float ms = m_marginSide, mt = m_marginTop, mb = m_marginBottom;

    auto vtx = [](const QPointF& p, float z, const QVector3D& n, int mat) {
        return Vertex{float(p.x()), float(p.y()), z, n.x(), n.y(), n.z(), float(mat)};
    };
    // Joins two rings (a at za, b at zb); normals from the rings' 2D normals,
    // made perpendicular to the loft direction. sign = +1 faces outward, -1 inward.
    auto loft = [&](const Ring& a, float za, const Ring& b, float zb, int mat, float sign) {
        const int n = a.p.size();
        QVector<QVector3D> na(n), nb(n);
        for (int i = 0; i < n; ++i) {
            QVector3D d(float(b.p[i].x() - a.p[i].x()), float(b.p[i].y() - a.p[i].y()), zb - za);
            d.normalize();
            QVector3D n3(float(a.n[i].x()) * sign, float(a.n[i].y()) * sign, 0.f);
            QVector3D nn = (n3 - QVector3D::dotProduct(n3, d) * d);
            if (nn.lengthSquared() < 1e-8f) nn = QVector3D(0, 0, zb < za ? 1.f : -1.f);
            na[i] = nb[i] = nn.normalized();
        }
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            const Vertex a0 = vtx(a.p[i], za, na[i], mat), a1 = vtx(a.p[j], za, na[j], mat);
            const Vertex b0 = vtx(b.p[i], zb, nb[i], mat), b1 = vtx(b.p[j], zb, nb[j], mat);
            addTri(a0, a1, b1);
            addTri(a0, b1, b0);
        }
    };
    auto face = [&](const Ring& outer, const Ring& inner, float z, const QVector3D& nrm, int mat) {
        const int n = outer.p.size();
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            addTri(vtx(outer.p[i], z, nrm, mat), vtx(outer.p[j], z, nrm, mat), vtx(inner.p[j], z, nrm, mat));
            addTri(vtx(outer.p[i], z, nrm, mat), vtx(inner.p[j], z, nrm, mat), vtx(inner.p[i], z, nrm, mat));
        }
    };
    auto cap = [&](const Ring& r, float z, const QPointF& c, const QVector3D& nrm, int mat) {
        const int n = r.p.size();
        for (int i = 0; i < n; ++i)
            addTri(vtx(c, z, nrm, mat), vtx(r.p[i], z, nrm, mat), vtx(r.p[(i + 1) % n], z, nrm, mat));
    };

    // Front bezel: outer outline -> opening, then the tunnel down to the glass.
    const float fcy = (mt - mb) * 0.5f;
    const float fhw = gw + ms, fhh = gh + (mt + mb) * 0.5f;
    const Ring front = roundedRect(0, fcy, fhw, fhh, panel ? 0.015 : m_cabinet == PvmMonitor ? 0.025 : m_cabinet == WoodConsole ? 0.04 : 0.07);
    const Ring opening = roundedRect(0, 0, gw + lip, gh + lip, rg + lip);
    const Ring glassRim = roundedRect(0, 0, gw, gh, rg);
    if (!theater && !arcade) {
        face(front, opening, 0.f, QVector3D(0, 0, 1), 1);
        loft(opening, 0.f, glassRim, m_glassZ, 2, -1.f);
    }

    // Stand: built upright in the rolled frame, so that a pivoted (portrait) panel
    // keeps its stand on the floor. Roll(+90) maps (x,y) -> (-y,x); its inverse is
    // (x,y) -> (y,-x).
    auto toModel = [this](const QVector3D& v) { return m_pivot ? QVector3D(v.y(), -v.x(), v.z()) : v; };
    float quadMat = 0.f;   // material for quads (8 = the wall mount's dark metal)
    auto quad = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D d, QVector3D n) {
        const QVector3D ma = toModel(a), mb2 = toModel(b), mc = toModel(c), md = toModel(d), mn = toModel(n);
        auto V = [&](const QVector3D& p) { return Vertex{p.x(), p.y(), p.z(), mn.x(), mn.y(), mn.z(), quadMat}; };
        addTri(V(ma), V(mb2), V(mc));
        addTri(V(ma), V(mc), V(md));
    };
    auto box = [&](float x0, float x1, float y0, float y1, float z0, float z1) {
        quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, {0, 0, 1});
        quad({x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {0, 0, -1});
        quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, {-1, 0, 0});
        quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {1, 0, 0});
        quad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, {0, 1, 0});
        quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, {0, -1, 0});
    };

    // A round knob on the front face (model space, rolls with the set).
    // An 80s TV knob on the front face (model space, rolls with the set): a thin metal
    // flange (mat 7), a slightly tapered fluted body (mat 5), a bevel, and a brushed cap
    // with an indicator line (mat 6). Profile rings are lofted around the axis.
    auto knob = [&](float cx, float cy, float r, float h) {
        const int n = 40;
        struct P { float rad, z; int mat; };
        // (radius, height) profile from the cabinet outwards; each segment has one material.
        const P prof[] = {{r * 1.28f, 0.f, 7}, {r * 1.28f, 0.004f, 7}, {r, 0.004f, 5},
                          {r * 0.93f, h - 0.009f, 5}, {r * 0.80f, h, 5}};
        auto V = [&](float rad, float z, float a, const QVector3D& nn, int mat) {
            return Vertex{cx + rad * std::cos(a), cy + rad * std::sin(a), z, nn.x(), nn.y(), nn.z(), float(mat)};
        };
        for (int s = 0; s + 1 < int(sizeof(prof) / sizeof(prof[0])); ++s) {
            const P a = prof[s], b = prof[s + 1];
            const int mat = (s == 1) ? 7 : b.mat;
            for (int i = 0; i < n; ++i) {
                const float a0 = 2.f * float(M_PI) * i / n, a1 = 2.f * float(M_PI) * (i + 1) / n;
                auto nrmAt = [&](float ang) {
                    // perpendicular to the profile segment, pointing away from the axis
                    const float dr = b.rad - a.rad, dz = b.z - a.z;
                    QVector3D nn(std::cos(ang) * dz, std::sin(ang) * dz, -dr);
                    if (nn.lengthSquared() < 1e-10f) nn = QVector3D(0, 0, 1);
                    return nn.normalized();
                };
                addTri(V(a.rad, a.z, a0, nrmAt(a0), mat), V(a.rad, a.z, a1, nrmAt(a1), mat), V(b.rad, b.z, a1, nrmAt(a1), mat));
                addTri(V(a.rad, a.z, a0, nrmAt(a0), mat), V(b.rad, b.z, a1, nrmAt(a1), mat), V(b.rad, b.z, a0, nrmAt(a0), mat));
            }
        }
        const float capR = r * 0.80f;
        for (int i = 0; i < n; ++i) {
            const float a0 = 2.f * float(M_PI) * i / n, a1 = 2.f * float(M_PI) * (i + 1) / n;
            addTri(Vertex{cx, cy, h, 0, 0, 1, 6.f}, V(capR, h, a0, {0, 0, 1}, 6), V(capR, h, a1, {0, 0, 1}, 6));
        }
    };
    // A leg that tapers towards the floor, built upright (stays on the floor when pivoted).
    auto leg = [&](float x, float z, float yTop, float yBottom, float top, float bottom) {
        const float t = top * 0.5f, b = bottom * 0.5f;
        const QVector3D T[4] = {{x - t, yTop, z + t}, {x + t, yTop, z + t}, {x + t, yTop, z - t}, {x - t, yTop, z - t}};
        const QVector3D B[4] = {{x - b, yBottom, z + b}, {x + b, yBottom, z + b}, {x + b, yBottom, z - b}, {x - b, yBottom, z - b}};
        const QVector3D side[4] = {{0, 0, 1}, {1, 0, 0}, {0, 0, -1}, {-1, 0, 0}};
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            quad(B[i], B[j], T[j], T[i], side[i]);
        }
        quad(B[3], B[2], B[1], B[0], {0, -1, 0});
    };
    // Horizontal slab on the floor (feet, bases), built upright via quad().
    auto slab = [&](const Ring& foot, float y0, float y1, float cz) {
        const int n = foot.p.size();
        for (int i = 0; i < n; ++i) {
            const int j = (i + 1) % n;
            const QVector3D a0(float(foot.p[i].x()), y1, cz + float(foot.p[i].y()));
            const QVector3D a1(float(foot.p[j].x()), y1, cz + float(foot.p[j].y()));
            const QVector3D b0(a0.x(), y0, a0.z()), b1(a1.x(), y0, a1.z());
            const QVector3D c(0, y1, cz), cb(0, y0, cz);
            const QVector3D side(float(foot.n[i].x()), 0, float(foot.n[i].y()));
            quad(c, c, a1, a0, {0, 1, 0});
            quad(cb, cb, b0, b1, {0, -1, 0});
            quad(a0, a1, b1, b0, side);
        }
    };
    const float uprightHalfH = m_pivot ? fhw : fhh;
    const float uprightCentreY = m_pivot ? 0.f : fcy;
    const float uprightBottom = uprightCentreY - uprightHalfH;

    if (theater) {
        // The movie theater: only the screen; the scene draws the stage and room around it.
    } else if (arcade) {
        buildArcade();
    } else if (!panel && (m_cabinet == WoodConsole || m_cabinet == PvmMonitor)) {
        // Boxy cabinets: a straight body with only a slight taper.
        const float depth = m_cabinet == WoodConsole ? -0.95f : -0.80f;
        const float r = m_cabinet == WoodConsole ? 0.04f : 0.02f;
        const Ring bevel = roundedRect(0, fcy, fhw + 0.01, fhh + 0.01, r + 0.01);
        // The console is a straight wooden box (flat underside for the legs); the PVM tapers a little.
        const Ring back = m_cabinet == WoodConsole ? bevel : roundedRect(0, fcy, fhw * 0.92, fhh * 0.94, r);
        loft(front, 0.f, bevel, -0.03f, 0, 1.f);
        loft(bevel, -0.03f, back, depth, 0, 1.f);
        cap(back, depth, QPointF(0, fcy), QVector3D(0, 0, -1), 0);
        if (m_cabinet == WoodConsole) {
            // Two knobs on the lower right, four tapered legs under the flat bottom.
            const float by = -0.5f - mb * 0.5f;
            knob(gw + ms - 0.16f, by + 0.09f, 0.055f, 0.045f);
            knob(gw + ms - 0.16f, by - 0.09f, 0.045f, 0.040f);
            const float legH = 0.22f;
            const float bodyBottom = uprightBottom - 0.01f;     // the box's underside (bevel outline)
            const float floorY = bodyBottom - legH;
            const float halfW = m_pivot ? fhh + 0.01f : fhw + 0.01f;
            const float lx = std::max(halfW - 0.09f, 0.15f);
            if (!m_wallMount)
                for (float sx : {-1.f, 1.f})
                    for (float z : {-0.10f, depth + 0.10f})
                        leg(sx * lx, z, bodyBottom + 0.05f, floorY, 0.075f, 0.048f);   // top sinks into the body
        }
    } else if (!panel) {
        // Rounded front lip, straight body, then the taper to the tube housing.
        const Ring bevel = roundedRect(0, fcy, fhw + 0.012, fhh + 0.012, 0.08);
        const Ring body = roundedRect(0, fcy, fhw + 0.012, fhh + 0.012, 0.08);
        const Ring bodyBack = roundedRect(0, fcy - 0.01, fhw, fhh - 0.01, 0.08);
        const bool beige = m_cabinet == BeigeMonitor;
        const float rearHw = beige ? gw * 0.50f + 0.08f : gw * 0.62f + 0.12f, rearHh = beige ? 0.36f : 0.42f;
        const Ring rear = roundedRect(0, fcy - 0.06, rearHw, rearHh, 0.14);
        loft(front, 0.f, bevel, -0.035f, 0, 1.f);
        loft(body, -0.035f, bodyBack, beige ? -0.35f : -0.55f, 0, 1.f);
        loft(bodyBack, beige ? -0.35f : -0.55f, rear, -1.05f, 0, 1.f);
        cap(rear, -1.05f, QPointF(0, fcy - 0.06), QVector3D(0, 0, -1), 0);
        if (beige && !m_wallMount) {
            // Tilt-swivel base: a short neck on a round foot.
            const float floorY = uprightBottom - 0.10f;
            box(-0.09f, 0.09f, floorY + 0.02f, uprightBottom + 0.12f, -0.55f, -0.30f);
            slab(roundedRect(0, 0, 0.32, 0.30, 0.29), floorY, floorY + 0.025f, -0.42f);
        }
    } else {
        // Slim panel: a thin frame, then a shallow rear housing for the electronics.
        const Ring frameBack = roundedRect(0, fcy, fhw, fhh, 0.015);
        const Ring rear = roundedRect(0, fcy - 0.05, gw * 0.72f, gh * 0.68f, 0.05);
        loft(front, 0.f, frameBack, -0.03f, 0, 1.f);
        face(frameBack, rear, -0.03f, QVector3D(0, 0, -1), 0);   // back of the frame
        const Ring rearFront = roundedRect(0, fcy - 0.05, gw * 0.72f, gh * 0.68f, 0.05);
        const Ring rearBack = roundedRect(0, fcy - 0.05, gw * 0.66f, gh * 0.62f, 0.05);
        loft(rearFront, -0.03f, rearBack, -0.10f, 0, 1.f);
        cap(rearBack, -0.10f, QPointF(0, fcy - 0.05), QVector3D(0, 0, -1), 0);

        if (!m_wallMount) {
            const float bottom = uprightBottom;
            const float floorY = bottom - 0.17f;
            box(-0.045f, 0.045f, floorY + 0.02f, bottom + 0.30f, -0.095f, -0.065f);          // neck
            slab(roundedRect(0, 0, 0.30, 0.15, 0.06), floorY, floorY + 0.02f, -0.08f);   // base
        }
    }

    if (m_wallMount && !theater && !arcade) {   // (an arcade cabinet stands against the wall instead)
        quadMat = 8.f;   // the mount is dark brushed metal, whatever the cabinet is made of
        // The rear of what has been built so far (the cabinet itself).
        float rearZ = 0.f;
        for (const Vertex& v : m_verts) rearZ = std::min(rearZ, v.pz);
        if (panel) {
            // Slim flat-panel bracket: a short arm from the rear housing to a wall plate.
            const float cy = m_pivot ? 0.f : fcy - 0.05f;
            box(-0.06f, 0.06f, cy - 0.06f, cy + 0.06f, rearZ - 0.06f, rearZ + 0.01f);
            box(-0.16f, 0.16f, cy - 0.12f, cy + 0.12f, rearZ - 0.075f, rearZ - 0.06f);
            m_wallZ = rearZ - 0.075f;
        } else {
            // CRT on a hotel-style articulating wall arm: a tray under the cabinet, an arm
            // from the tray's back to a plate on the wall.
            const float halfW = (m_pivot ? fhh : fhw) * 0.85f;
            const float trayTop = uprightBottom - 0.005f, trayBottom = trayTop - 0.035f;
            box(-halfW, halfW, trayBottom, trayTop, rearZ * 0.9f, -0.12f);                      // tray
            box(-halfW, -halfW + 0.03f, trayTop, trayTop + 0.08f, rearZ * 0.9f, -0.12f);        // side lips
            box(halfW - 0.03f, halfW, trayTop, trayTop + 0.08f, rearZ * 0.9f, -0.12f);
            box(-0.06f, 0.06f, trayBottom - 0.07f, trayBottom, rearZ - 0.14f, rearZ * 0.9f + 0.05f);   // arm
            box(-0.14f, 0.14f, trayBottom - 0.22f, trayBottom + 0.10f, rearZ - 0.155f, rearZ - 0.14f); // wall plate
            m_wallZ = rearZ - 0.155f;
        }
        quadMat = 0.f;
    }

    // Glass grid (the bulge is applied in the vertex shader from the current curvature).
    const int nx = 56, ny = 42;
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            auto gv = [&](int ii, int jj) {
                return Vertex{-gw + 2.f * gw * ii / nx, -gh + 2.f * gh * jj / ny, m_glassZ, 0, 0, 1, 3.f};
            };
            addTri(gv(i, j), gv(i + 1, j), gv(i + 1, j + 1));
            addTri(gv(i, j), gv(i + 1, j + 1), gv(i, j + 1));
        }
    }

    // Bounds (model space, before any pivot roll), from the actual geometry.
    m_bmin = QVector3D(1e9f, 1e9f, 1e9f);
    m_bmax = QVector3D(-1e9f, -1e9f, -1e9f);
    for (const Vertex& v : m_verts) {
        m_bmin = QVector3D(std::min(m_bmin.x(), v.px), std::min(m_bmin.y(), v.py), std::min(m_bmin.z(), v.pz));
        m_bmax = QVector3D(std::max(m_bmax.x(), v.px), std::max(m_bmax.y(), v.py), std::max(m_bmax.z(), v.pz));
    }

    // Shadow quad: unit square, expanded in the vertex shader onto the floor plane.
    m_shadowFirst = m_verts.size();
    const Vertex s00{0, 0, 0, 0, 1, 0, 4.f}, s10{1, 0, 0, 0, 1, 0, 4.f}, s11{1, 0, 1, 0, 1, 0, 4.f}, s01{0, 0, 1, 0, 1, 0, 4.f};
    addTri(s00, s10, s11);
    addTri(s00, s11, s01);
    m_shadowCount = 6;

    m_vao.bind();
    m_vbo.bind();
    m_vbo.allocate(m_verts.constData(), int(m_verts.size() * sizeof(Vertex)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(6 * sizeof(float)));
    m_vbo.release();
    m_vao.release();
    m_dirty = false;
}

DeskRenderer::ArcadeLayout DeskRenderer::arcadeLayout() const
{
    ArcadeLayout l;
    const float gw = m_aspect * 0.5f, gh = 0.5f;
    l.uw = m_pivot ? gh : gw;                     // the screen's half size, upright
    l.uh = m_pivot ? gw : gh;
    l.cw = std::max(l.uw, 0.62f) + 0.30f;         // the side panels' outer faces
    l.ci = l.cw - 0.04f;                          // between the side panels
    l.floorY = -l.uh - 2.0f;
    return l;
}

namespace {
// Ear clipping for a simple polygon (counter-clockwise), for the side panels.
QVector<int> triangulate(const QVector<QPointF>& p)
{
    QVector<int> idx(p.size()), out;
    for (int i = 0; i < p.size(); ++i) idx[i] = i;
    auto cross = [](QPointF a, QPointF b, QPointF c) { return (b.x() - a.x()) * (c.y() - a.y()) - (b.y() - a.y()) * (c.x() - a.x()); };
    int guard = 0;
    while (idx.size() > 3 && guard++ < 1000) {
        bool clipped = false;
        for (int i = 0; i < idx.size(); ++i) {
            const int a = idx[(i + idx.size() - 1) % idx.size()], b = idx[i], c = idx[(i + 1) % idx.size()];
            if (cross(p[a], p[b], p[c]) <= 1e-9) continue;   // reflex corner
            bool inside = false;
            for (int j : idx) {
                if (j == a || j == b || j == c) continue;
                if (cross(p[a], p[b], p[j]) > 0 && cross(p[b], p[c], p[j]) > 0 && cross(p[c], p[a], p[j]) > 0) { inside = true; break; }
            }
            if (inside) continue;
            out << a << b << c;
            idx.removeAt(i);
            clipped = true;
            break;
        }
        if (!clipped) break;
    }
    if (idx.size() == 3) out << idx[0] << idx[1] << idx[2];
    return out;
}
} // namespace

// The upright arcade cabinet, built in the upright frame (like the stands): a pivoted,
// portrait picture turns only the monitor, the cabinet stays standing. Materials:
// 0 laminate body, 2 screen tunnel, 9 side art, 10 marquee, 11 control panel, 12 joystick
// ball, 14 coin door, 15 T-molding, 16 speaker grille, 17 chrome, 18 bezel, 20-27 buttons.
void DeskRenderer::buildArcade()
{
    const ArcadeLayout L = arcadeLayout();
    const float U = L.uh, Yf = L.floorY, ci = L.ci, cw = L.cw;
    auto toModel = [this](const QVector3D& v) { return m_pivot ? QVector3D(v.y(), -v.x(), v.z()) : v; };
    auto tri = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D na, QVector3D nb, QVector3D nc, int mat) {
        auto V = [&](const QVector3D& p, const QVector3D& n) { const QVector3D mp = toModel(p), mn = toModel(n);
                                                               return Vertex{mp.x(), mp.y(), mp.z(), mn.x(), mn.y(), mn.z(), float(mat)}; };
        addTri(V(a, na), V(b, nb), V(c, nc));
    };
    auto quad = [&](QVector3D a, QVector3D b, QVector3D c, QVector3D d, int mat) {   // normal from the winding
        QVector3D n = QVector3D::crossProduct(b - a, c - a).normalized();
        tri(a, b, c, n, n, n, mat);
        tri(a, c, d, n, n, n, mat);
    };
    // A strip across the cabinet (x from -w to +w) between two points of the profile (z, y).
    auto across = [&](float w, QPointF p0, QPointF p1, int mat) {
        quad({-w, float(p0.y()), float(p0.x())}, {w, float(p0.y()), float(p0.x())},
             {w, float(p1.y()), float(p1.x())}, {-w, float(p1.y()), float(p1.x())}, mat);
    };
    auto box = [&](float x0, float x1, float y0, float y1, float z0, float z1, int mat) {
        quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, mat);
        quad({x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, mat);
        quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, mat);
        quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, mat);
        quad({x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, mat);
    };

    // --- side panels: the cabinet's silhouette (z, y), extruded; the edges carry T-molding
    const QVector<QPointF> prof = {
        {-1.60, Yf}, {-0.08, Yf}, {-0.08, Yf + 0.30}, {0.06, Yf + 0.34}, {0.06, -U - 0.62},
        {1.00, -U - 0.46}, {1.00, -U - 0.32}, {0.24, -U - 0.12}, {0.10, -U - 0.10}, {0.10, U + 0.14},
        {0.40, U + 0.26}, {0.44, U + 0.72}, {-1.25, U + 0.72}, {-1.60, U + 0.10}};
    // counter-clockwise when seen from +x with (z, y) as (right, up)
    QVector<QPointF> ccw = prof;
    double area = 0;
    for (int i = 0; i < ccw.size(); ++i) { const QPointF a = ccw[i], b = ccw[(i + 1) % ccw.size()]; area += a.x() * b.y() - b.x() * a.y(); }
    if (area < 0) std::reverse(ccw.begin(), ccw.end());
    const QVector<int> tris = triangulate(ccw);
    for (float side : {-1.f, 1.f}) {
        for (float x : {side * cw, side * ci}) {
            const bool outer = std::abs(x) > ci + 1e-4f;
            const QVector3D n(outer ? side : -side, 0, 0);
            for (int t = 0; t + 2 < tris.size(); t += 3) {
                QVector3D a(x, float(ccw[tris[t]].y()), float(ccw[tris[t]].x()));
                QVector3D b(x, float(ccw[tris[t + 1]].y()), float(ccw[tris[t + 1]].x()));
                QVector3D c(x, float(ccw[tris[t + 2]].y()), float(ccw[tris[t + 2]].x()));
                if (n.x() > 0) std::swap(b, c);   // face outwards
                tri(a, b, c, n, n, n, outer ? 9 : 0);
            }
        }
        for (int i = 0; i < ccw.size(); ++i) {   // the panel's edge: T-molding
            const QPointF a = ccw[i], b = ccw[(i + 1) % ccw.size()];
            QVector3D p0(side * ci, float(a.y()), float(a.x())), p1(side * ci, float(b.y()), float(b.x()));
            QVector3D p2(side * cw, float(b.y()), float(b.x())), p3(side * cw, float(a.y()), float(a.x()));
            const QVector3D edge(0, float(b.y() - a.y()), float(b.x() - a.x()));
            QVector3D n = QVector3D::crossProduct(QVector3D(1, 0, 0), edge).normalized();   // outward for a ccw outline
            n = QVector3D(0, n.y(), n.z());
            tri(p0, p1, p2, n, n, n, 15);
            tri(p0, p2, p3, n, n, n, 15);
        }
    }

    // --- the front, between the side panels (slightly inset from their edges)
    const float w = ci;
    across(w, {-0.10, Yf}, {-0.10, Yf + 0.30}, 0);                  // kick plate
    across(w, {-0.10, Yf + 0.30}, {0.04, Yf + 0.33}, 0);
    across(w, {0.04, Yf + 0.33}, {0.04, -U - 0.62}, 0);              // lower front
    box(-0.30f, 0.30f, Yf + 0.60f, Yf + 1.30f, 0.03f, 0.07f, 14);    // coin door
    across(w, {0.04, -U - 0.62}, {0.97, -U - 0.47}, 0);              // under the control panel
    across(w, {0.97, -U - 0.47}, {0.97, -U - 0.33}, 15);             // its front edge (molding)
    across(w, {0.97, -U - 0.33}, {0.22, -U - 0.13}, 11);             // the control panel
    across(w, {0.22, -U - 0.13}, {0.0, -U - 0.14}, 0);
    // the bezel around the screen, and the tunnel back to the glass
    const float bw = L.uw, bh = L.uh;
    const float yb = -U - 0.14f, yt = U + 0.12f;
    quad({-w, yb, 0}, {w, yb, 0}, {w, -bh, 0}, {-w, -bh, 0}, 18);
    quad({-w, bh, 0}, {w, bh, 0}, {w, yt, 0}, {-w, yt, 0}, 18);
    quad({-w, -bh, 0}, {-bw, -bh, 0}, {-bw, bh, 0}, {-w, bh, 0}, 18);
    quad({bw, -bh, 0}, {w, -bh, 0}, {w, bh, 0}, {bw, bh, 0}, 18);
    const float gz = m_glassZ;
    quad({-bw, -bh, 0}, {-bw, -bh, gz}, {bw, -bh, gz}, {bw, -bh, 0}, 2);
    quad({-bw, bh, gz}, {-bw, bh, 0}, {bw, bh, 0}, {bw, bh, gz}, 2);
    quad({-bw, -bh, gz}, {-bw, -bh, 0}, {-bw, bh, 0}, {-bw, bh, gz}, 2);
    quad({bw, -bh, 0}, {bw, -bh, gz}, {bw, bh, gz}, {bw, bh, 0}, 2);
    across(w, {0.0, U + 0.12}, {0.38, U + 0.25}, 16);                // speaker panel under the marquee
    across(w, {0.38, U + 0.25}, {0.42, U + 0.66}, 10);               // the marquee
    across(w, {0.42, U + 0.66}, {0.42, U + 0.70}, 0);
    across(w, {0.42, U + 0.70}, {-1.23, U + 0.70}, 0);               // top
    across(w, {-1.23, U + 0.70}, {-1.58, U + 0.10}, 0);              // back
    across(w, {-1.58, U + 0.10}, {-1.58, Yf}, 0);

    // --- controls on the control panel
    const QVector3D A(0, -U - 0.33f, 0.97f), B(0, -U - 0.13f, 0.22f);
    const QVector3D along = (B - A).normalized();                     // front to back
    const QVector3D up = QVector3D::crossProduct(QVector3D(1, 0, 0), along).normalized();   // the panel's normal
    auto at = [&](float x, float s) { return QVector3D(x, A.y() + (B.y() - A.y()) * s, A.z() + (B.z() - A.z()) * s); };
    auto cylinder = [&](QVector3D c, float r, float h, int sideMat, int topMat) {
        const int n = 22;
        const QVector3D e1(1, 0, 0), e2 = QVector3D::crossProduct(up, e1).normalized();
        for (int i = 0; i < n; ++i) {
            const float a0 = 2.f * float(M_PI) * i / n, a1 = 2.f * float(M_PI) * (i + 1) / n;
            const QVector3D d0 = e1 * std::cos(a0) + e2 * std::sin(a0), d1 = e1 * std::cos(a1) + e2 * std::sin(a1);
            const QVector3D b0 = c + d0 * r, b1 = c + d1 * r, t0 = b0 + up * h, t1 = b1 + up * h;
            tri(b0, b1, t1, d0, d1, d1, sideMat);
            tri(b0, t1, t0, d0, d1, d0, sideMat);
            tri(c + up * h, t0, t1, up, up, up, topMat);
        }
    };
    auto sphere = [&](QVector3D c, float r, int mat) {
        const int nu = 18, nv = 10;
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                auto P = [&](int ii, int jj) {
                    const float th = float(M_PI) * jj / nv, ph = 2.f * float(M_PI) * ii / nu;
                    return QVector3D(std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph));
                };
                const QVector3D p00 = P(i, j), p10 = P(i + 1, j), p11 = P(i + 1, j + 1), p01 = P(i, j + 1);
                tri(c + p00 * r, c + p11 * r, c + p10 * r, p00, p11, p10, mat);
                tri(c + p00 * r, c + p01 * r, c + p11 * r, p00, p01, p11, mat);
            }
    };
    const bool twoPlayers = ci > 0.85f;
    const float players[2] = {twoPlayers ? -ci * 0.5f : 0.f, ci * 0.5f};
    for (int pl = 0; pl < (twoPlayers ? 2 : 1); ++pl) {
        const float pc = players[pl] - 0.06f;
        // joystick: a base, a chrome shaft and a ball top
        const QVector3D j = at(pc - 0.20f, 0.46f);
        cylinder(j, 0.055f, 0.006f, 17, 17);
        cylinder(j, 0.011f, 0.15f, 17, 17);
        sphere(j + up * 0.18f, 0.042f, 12);
        // six buttons in two gently curved rows, and a start button
        for (int row = 0; row < 2; ++row)
            for (int k = 0; k < 3; ++k) {
                const float x = pc + 0.02f + k * 0.12f, s = 0.36f + row * 0.22f - (k == 1 ? 0.03f : 0.f);
                cylinder(at(x, s), 0.034f, 0.018f, 20 + ((row * 3 + k + pl * 2) % 8), 20 + ((row * 3 + k + pl * 2) % 8));
            }
        cylinder(at(pc + 0.14f, 0.86f), 0.022f, 0.012f, 26, 26);   // start
    }

    // In the wall scene the cabinet stands against the wall.
    float rearZ = 0.f;
    for (const Vertex& v : m_verts) rearZ = std::min(rearZ, v.pz);
    m_wallZ = rearZ - 0.03f;
}

// ---- transforms -----------------------------------------------------------------
// world = RotYawPitch * T(-pivotPoint) * Roll * model
// pivotPoint and the shadow live in "rolled" space (after the optional 90° roll).

static QMatrix4x4 rollMatrix(bool pivot)
{
    QMatrix4x4 r;
    if (pivot) r.rotate(90.f, 0, 0, 1);
    return r;
}

static QMatrix4x4 yawPitchMatrix(const DeskRenderer::Pose& p)
{
    QMatrix4x4 m;
    m.rotate(p.pitch, 1, 0, 0);
    m.rotate(p.yaw, 0, 1, 0);
    return m;
}

QMatrix4x4 DeskRenderer::modelMatrix(const Pose& p) const
{
    QMatrix4x4 t;
    t.translate(-p.pivotPoint);
    return yawPitchMatrix(p) * t * rollMatrix(p.pivot);
}

QMatrix4x4 DeskRenderer::viewProjection(const Pose& p, const QSize& vp) const
{
    QMatrix4x4 proj;
    proj.perspective(fovY(), float(vp.width()) / std::max(1, vp.height()), 0.05f, 60.f);
    QMatrix4x4 shift;   // lens shift: moves the image without perspective skew
    shift(0, 3) = float(p.lensShift.x());
    shift(1, 3) = float(p.lensShift.y());
    QMatrix4x4 view;
    view.translate(0, 0, -p.distance);
    return shift * proj * view;
}

QPointF DeskRenderer::project(const QVector3D& mp, const Pose& p, const QSize& vp) const
{
    const QVector4D c = viewProjection(p, vp) * modelMatrix(p) * QVector4D(mp, 1.f);
    const float w = std::max(1e-5f, c.w());
    return QPointF((c.x() / w * 0.5 + 0.5) * vp.width(), (0.5 - c.y() / w * 0.5) * vp.height());
}

float DeskRenderer::cameraClearance(const Pose& p, float floorDrop) const
{
    const QMatrix4x4 roll = rollMatrix(p.pivot);
    float floorY = 1e9f;
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = roll.map(QVector3D((i & 1) ? m_bmax.x() : m_bmin.x(), (i & 2) ? m_bmax.y() : m_bmin.y(),
                                               (i & 4) ? m_bmax.z() : m_bmin.z()));
        floorY = std::min(floorY, c.y());
    }
    QMatrix4x4 t;
    t.translate(-p.pivotPoint);
    const QMatrix4x4 sm = yawPitchMatrix(p) * t;
    const QVector3D point = sm.map(QVector3D(0, floorY - floorDrop, 0));
    const QVector3D normal = sm.mapVector(QVector3D(0, 1, 0)).normalized();
    return QVector3D::dotProduct(QVector3D(0, 0, p.distance) - point, normal);
}

DeskRenderer::GpuModel DeskRenderer::upload(const QVector<float>& v)
{
    GpuModel g;
    g.vao = new QOpenGLVertexArrayObject;
    g.vao->create();
    g.vao->bind();
    g.vbo = new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    g.vbo->create();
    g.vbo->bind();
    g.vbo->allocate(v.constData(), int(v.size() * sizeof(float)));
    for (int a = 0; a < 3; ++a) {
        glEnableVertexAttribArray(a);
        glVertexAttribPointer(a, 3, GL_FLOAT, GL_FALSE, 9 * sizeof(float), reinterpret_cast<void*>(a * 3 * sizeof(float)));
    }
    g.vao->release();
    g.vbo->release();
    g.count = int(v.size() / 9);
    for (int i = 0; i + 8 < v.size(); i += 9) { g.halfW = std::max(g.halfW, std::abs(v[i])); g.halfD = std::max(g.halfD, std::abs(v[i + 2])); }
    return g;
}

void DeskRenderer::setModels(const QVector<ModelLibrary::Mesh>& meshes)
{
    for (GpuModel& g : m_models) { delete g.vao; if (g.vbo) { g.vbo->destroy(); delete g.vbo; } }
    m_models.clear();
    for (const auto& m : meshes) {
        GpuModel g = upload(m.vertices);
        g.halfW = 0.f; g.halfD = 0.f;
        g.points = m.points;
        for (int i = 0; i + 8 < m.vertices.size(); i += 9) { g.halfW = std::max(g.halfW, std::abs(m.vertices[i])); g.halfD = std::max(g.halfD, std::abs(m.vertices[i + 2])); }
        m_models.append(g);
    }
}

void DeskRenderer::drawModels(int mode, const Frame& f, const QMatrix4x4& vp, const QMatrix4x4& floorToWorld,
                              const QVector3D& c, const QVector3D& sunF, const QVector3D& camF, const QVector3D& tvF, float glassLod)
{
    // Six places around the set; each statue on a small marble plinth, turned a little towards the set.
    static const float places[6][2] = {{-3.0f, -1.9f}, {3.0f, -1.9f}, {-3.7f, 0.7f}, {3.7f, 0.7f}, {-1.7f, -3.3f}, {1.7f, -3.3f}};
    static const QVector3D tints[6] = {{0.95f, 0.2f, 0.25f}, {0.15f, 0.55f, 1.0f}, {1.0f, 0.8f, 0.1f},
                                       {0.2f, 0.85f, 0.4f}, {0.75f, 0.3f, 0.95f}, {1.0f, 0.5f, 0.15f}};
    const float plinthH = 0.35f;
    const QVector3D skyTop = f.cgPalette == 1 ? QVector3D(0.22f, 0.05f, 0.32f) : f.cgPalette == 2 ? QVector3D(0.f, 0.f, 0.01f) : QVector3D(0.02f, 0.07f, 0.22f);
    const QVector3D skyHor = f.cgPalette == 1 ? QVector3D(1.f, 0.48f, 0.2f) : f.cgPalette == 2 ? QVector3D(0.05f, 0.06f, 0.14f) : QVector3D(0.18f, 0.62f, 0.66f);
    const QVector3D sunCol = f.cgPalette == 1 ? QVector3D(1.3f, 0.75f, 0.45f) : f.cgPalette == 2 ? QVector3D(0.85f, 0.9f, 1.1f) : QVector3D(1.15f, 1.1f, 1.f);
    const QVector3D amb = f.cgPalette == 1 ? QVector3D(0.28f, 0.14f, 0.22f) : f.cgPalette == 2 ? QVector3D(0.03f, 0.035f, 0.06f) : QVector3D(0.10f, 0.22f, 0.28f);
    m_model.bind();
    m_model.setUniformValue("uFloorToWorld", floorToWorld);
    m_model.setUniformValue("uVP", vp);
    m_model.setUniformValue("uMode", mode);
    m_model.setUniformValue("uSunF", sunF);
    m_model.setUniformValue("uSunColour", sunCol);
    m_model.setUniformValue("uAmbient", amb);
    m_model.setUniformValue("uSkyTop", skyTop);
    m_model.setUniformValue("uSkyHorizon", skyHor);
    m_model.setUniformValue("uCamF", camF);
    m_model.setUniformValue("uTvF", tvF);
    m_model.setUniformValue("uGlassTex", 0);
    m_model.setUniformValue("uHasGlass", f.glassTexture != 0);
    m_model.setUniformValue("uGlassLod", glassLod);
    for (int i = 0; i < m_models.size() && i < 6; ++i) {
        const GpuModel& g = m_models[i];
        const QVector3D at(c.x() + places[i][0], 0.f, c.z() + places[i][1]);
        const float face = qRadiansToDegrees(std::atan2(c.x() - at.x(), 4.0f)) * 0.5f;
        QMatrix4x4 place;
        place.translate(at);
        place.rotate(face + f.modelTurn + f.modelSpread * i, 0, 1, 0);
        QMatrix4x4 statue = place;
        statue.translate(0.f, plinthH, 0.f);
        m_model.setUniformValue("uPlace", statue);
        m_model.setUniformValue("uFinish", f.modelFinish);
        m_model.setUniformValue("uTint", tints[i]);
        m_model.setUniformValue("uMirror", g.points ? 0.05f : 0.16f);   // a cloud's dots overlap about three deep
        g.vao->bind();
        glDrawArrays(GL_TRIANGLES, 0, g.count);
        g.vao->release();
        QMatrix4x4 plinth = place;   // the plinth, a little wider than the statue
        plinth.scale(std::max(0.35f, g.halfW * 2.3f), plinthH, std::max(0.35f, g.halfD * 2.3f));
        plinth.translate(0.f, 0.5f, 0.f);
        m_model.setUniformValue("uPlace", plinth);
        m_model.setUniformValue("uFinish", 5);
        m_model.setUniformValue("uMirror", 0.16f);
        m_plinth.vao->bind();
        glDrawArrays(GL_TRIANGLES, 0, m_plinth.count);
        m_plinth.vao->release();
    }
    m_model.release();
}

QVector3D DeskRenderer::cameraInScene(const Pose& p) const
{
    const QMatrix4x4 roll = rollMatrix(p.pivot);
    float floorY = 1e9f;
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = roll.map(QVector3D((i & 1) ? m_bmax.x() : m_bmin.x(), (i & 2) ? m_bmax.y() : m_bmin.y(),
                                               (i & 4) ? m_bmax.z() : m_bmin.z()));
        floorY = std::min(floorY, c.y());
    }
    QMatrix4x4 t;
    t.translate(-p.pivotPoint);
    const QMatrix4x4 sm = yawPitchMatrix(p) * t;
    const QVector3D origin = sm.map(QVector3D(0, floorY, 0));
    const QVector3D X = sm.mapVector(QVector3D(1, 0, 0)).normalized(), Y = sm.mapVector(QVector3D(0, 1, 0)).normalized(),
                    Z = sm.mapVector(QVector3D(0, 0, 1)).normalized();
    const QVector3D d = QVector3D(0, 0, p.distance) - origin;
    return QVector3D(QVector3D::dotProduct(d, X), QVector3D::dotProduct(d, Y), QVector3D::dotProduct(d, Z));
}

bool DeskRenderer::cameraAboveFloor(const Pose& p) const
{
    // Floor plane: y = the rolled cabinet's lowest point, in the shadow's (unrolled) frame.
    const QMatrix4x4 roll = rollMatrix(p.pivot);
    float floorY = 1e9f;
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = roll.map(QVector3D((i & 1) ? m_bmax.x() : m_bmin.x(), (i & 2) ? m_bmax.y() : m_bmin.y(),
                                               (i & 4) ? m_bmax.z() : m_bmin.z()));
        floorY = std::min(floorY, c.y());
    }
    QMatrix4x4 t;
    t.translate(-p.pivotPoint);
    const QMatrix4x4 sm = yawPitchMatrix(p) * t;
    const QVector3D point = sm.map(QVector3D(0, floorY, 0));
    const QVector3D normal = sm.mapVector(QVector3D(0, 1, 0)).normalized();
    return QVector3D::dotProduct(QVector3D(0, 0, p.distance) - point, normal) > 0.f;
}

QPolygonF DeskRenderer::silhouette(const Pose& p, const QSize& vp) const
{
    QVector<QPointF> pts;
    for (int i = 0; i < 8; ++i) {
        const QVector3D c((i & 1) ? m_bmax.x() : m_bmin.x(), (i & 2) ? m_bmax.y() : m_bmin.y(),
                          (i & 4) ? m_bmax.z() : m_bmin.z());
        pts << project(c, p, vp);
    }
    // Contact shadow reaches a little beyond the base.
    const QMatrix4x4 vpM = viewProjection(p, vp);
    QMatrix4x4 t;
    t.translate(-p.pivotPoint);
    const QMatrix4x4 sm = yawPitchMatrix(p) * t;
    const QMatrix4x4 roll = rollMatrix(p.pivot);
    QVector3D rmin(1e9, 1e9, 1e9), rmax(-1e9, -1e9, -1e9);
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = roll.map(QVector3D((i & 1) ? m_bmax.x() : m_bmin.x(), (i & 2) ? m_bmax.y() : m_bmin.y(),
                                               (i & 4) ? m_bmax.z() : m_bmin.z()));
        rmin = QVector3D(std::min(rmin.x(), c.x()), std::min(rmin.y(), c.y()), std::min(rmin.z(), c.z()));
        rmax = QVector3D(std::max(rmax.x(), c.x()), std::max(rmax.y(), c.y()), std::max(rmax.z(), c.z()));
    }
    // Far enough to contain the visible part of the soft shadow: on X11 the window
    // mask also clips drawing, so a tighter outline would cut the shadow off.
    const float e = 0.40f;
    for (int i = 0; i < 4 && cameraAboveFloor(p); ++i) {   // from below, the shadow is not drawn
        const QVector3D c((i & 1) ? rmax.x() + e : rmin.x() - e, rmin.y(), (i & 2) ? rmax.z() + e : rmin.z() - e);
        const QVector4D cl = vpM * sm * QVector4D(c, 1.f);
        const float w = std::max(1e-5f, cl.w());
        pts << QPointF((cl.x() / w * 0.5 + 0.5) * vp.width(), (0.5 - cl.y() / w * 0.5) * vp.height());
    }
    return QPolygonF(convexHull(pts));
}

QRectF DeskRenderer::glassRect(const Pose& p, const QSize& vp) const
{
    const float gw = m_aspect * 0.5f;
    QPolygonF poly;
    for (auto c : {QVector3D(-gw, -0.5f, m_glassZ), QVector3D(gw, -0.5f, m_glassZ),
                   QVector3D(gw, 0.5f, m_glassZ), QVector3D(-gw, 0.5f, m_glassZ)})
        poly << project(c, p, vp);
    return poly.boundingRect();
}

// ---- drawing ------------------------------------------------------------------

void DeskRenderer::draw(unsigned targetFbo, const Frame& f)
{
    if (!m_initialized) return;
    if (m_dirty) build();

    glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
    glViewport(0, 0, f.viewport.width(), f.viewport.height());
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glClearColor(0.f, 0.f, 0.f, f.clearAlpha);   // premultiplied black
    glClearDepthf(1.0f);   // (through Qt's function table: plain glClearDepth isn't exported on Windows)
    glDepthMask(GL_TRUE);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_BLEND);

    const Pose& p = f.pose;
    const QMatrix4x4 model = modelMatrix(p);
    QMatrix4x4 t;
    t.translate(-p.pivotPoint);
    const QMatrix4x4 shadowModel = yawPitchMatrix(p) * t;

    // Footprint of the (possibly rolled) cabinet on the floor.
    const QMatrix4x4 roll = rollMatrix(p.pivot);
    QVector3D rmin(1e9, 1e9, 1e9), rmax(-1e9, -1e9, -1e9);
    for (int i = 0; i < 8; ++i) {
        const QVector3D c = roll.map(QVector3D((i & 1) ? m_bmax.x() : m_bmin.x(), (i & 2) ? m_bmax.y() : m_bmin.y(),
                                               (i & 4) ? m_bmax.z() : m_bmin.z()));
        rmin = QVector3D(std::min(rmin.x(), c.x()), std::min(rmin.y(), c.y()), std::min(rmin.z(), c.z()));
        rmax = QVector3D(std::max(rmax.x(), c.x()), std::max(rmax.y(), c.y()), std::max(rmax.z(), c.z()));
    }
    const float pad = 0.75f;   // quad reaches far enough that the exponential falloff is ~0 at its edge

    // The screen as a light source (world space), for the scene and the haze.
    const QVector3D scrC = model.map(QVector3D(0, 0, m_glassZ));
    const QVector3D scrR = model.mapVector(QVector3D(1, 0, 0)).normalized();
    const QVector3D scrU = model.mapVector(QVector3D(0, 1, 0)).normalized();
    const QVector3D scrN = model.mapVector(QVector3D(0, 0, 1)).normalized();
    const float glassLod = f.glassSize.isEmpty() ? 12.f : std::log2(float(std::max(f.glassSize.width(), f.glassSize.height())));
    const QMatrix4x4 vpmT = viewProjection(p, f.viewport);
    auto theaterUniforms = [&](QOpenGLShaderProgram& pr) {
        pr.setUniformValue("uInvVP", vpmT.inverted());
        pr.setUniformValue("uViewport", QVector2D(f.viewport.width(), f.viewport.height()));
        pr.setUniformValue("uCamPos", QVector3D(0, 0, p.distance));
        pr.setUniformValue("uFloorPoint", shadowModel.map(QVector3D(0, rmin.y(), 0)));
        pr.setUniformValue("uFloorX", shadowModel.mapVector(QVector3D(1, 0, 0)).normalized());
        pr.setUniformValue("uFloorNormal", shadowModel.mapVector(QVector3D(0, 1, 0)).normalized());
        pr.setUniformValue("uFloorZ", shadowModel.mapVector(QVector3D(0, 0, 1)).normalized());
        pr.setUniformValue("uAspect", m_aspect);
        pr.setUniformValue("uGlassTex", 0);
        pr.setUniformValue("uHasGlass", f.glassTexture != 0);
        pr.setUniformValue("uGlassLod", glassLod);
        pr.setUniformValue("uHouse", f.house);
        pr.setUniformValue("uFog", f.fog);
        pr.setUniformValue("uFogSteps", f.fogSteps);
        pr.setUniformValue("uTime", f.time);
        pr.setUniformValue("uCurtain", f.curtain);
        pr.setUniformValue("uMask", f.mask);
        pr.setUniformValue("uBeam", f.beam);
        pr.setUniformValue("uMarchRef", f.marchReference);
    };
    if (f.scene == 3) {
        // The theater room behind the screen.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_theater.bind();
        theaterUniforms(m_theater);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f.glassTexture);
        m_quadVao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_quadVao.release();
        m_theater.release();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }
    if (f.scene == 4) {
        // The 90s CG room: ray traced, writing depth, so the set and the objects hide each other.
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_ALWAYS);
        glDepthMask(GL_TRUE);
        m_cg.bind();
        m_cg.setUniformValue("uInvVP", vpmT.inverted());
        m_cg.setUniformValue("uVP", vpmT);
        m_cg.setUniformValue("uViewport", QVector2D(f.viewport.width(), f.viewport.height()));
        m_cg.setUniformValue("uCamPos", QVector3D(0, 0, p.distance));
        m_cg.setUniformValue("uFloorPoint", shadowModel.map(QVector3D(0, rmin.y(), 0)));
        m_cg.setUniformValue("uFloorX", shadowModel.mapVector(QVector3D(1, 0, 0)).normalized());
        m_cg.setUniformValue("uFloorNormal", shadowModel.mapVector(QVector3D(0, 1, 0)).normalized());
        m_cg.setUniformValue("uFloorZ", shadowModel.mapVector(QVector3D(0, 0, 1)).normalized());
        m_cg.setUniformValue("uDrop", f.floorDrop);
        const float by = f.floorDrop - rmin.y();   // model y -> height above the floor
        m_cg.setUniformValue("uSetMin", QVector3D(rmin.x(), rmin.y() + by, rmin.z()));
        m_cg.setUniformValue("uSetMax", QVector3D(rmax.x(), rmax.y() + by, rmax.z()));
        m_cg.setUniformValue("uScreenRect", QVector4D(-m_aspect * 0.5f, m_aspect * 0.5f, -0.5f + by, 0.5f + by));
        m_cg.setUniformValue("uScreenZ", rmax.z());
        m_cg.setUniformValue("uGlassTex", 0);
        m_cg.setUniformValue("uHasGlass", f.glassTexture != 0);
        m_cg.setUniformValue("uGlassLod", glassLod);
        m_cg.setUniformValue("uPalette", f.cgPalette);
        m_cg.setUniformValue("uFloorStyle", f.cgFloor);
        m_cg.setUniformValue("uStand", f.cgStand);
        m_cg.setUniformValue("uObjects", f.cgObjects);
        m_cg.setUniformValue("uObjectSet", f.cgObjectSet);
        m_cg.setUniformValue("uBackground", f.cgBackground);
        m_cg.setUniformValue("uBanding", f.cgBanding);
        m_cg.setUniformValue("uBounces", f.fogSteps <= 8 ? 1 : f.fogSteps <= 16 ? 2 : 3);   // quality
        m_cg.setUniformValue("uShadows", f.fogSteps > 8);
        m_cg.setUniformValue("uTime", f.time);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f.glassTexture);
        m_quadVao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_quadVao.release();
        m_cg.release();
        glDepthFunc(GL_LESS);

        // Your 3D models: their mirror images in the floor, then their shadows, then the models.
        const QVector3D X = shadowModel.mapVector(QVector3D(1, 0, 0)).normalized(), Y = shadowModel.mapVector(QVector3D(0, 1, 0)).normalized(),
                        Z = shadowModel.mapVector(QVector3D(0, 0, 1)).normalized();
        const QVector3D O = shadowModel.map(QVector3D(0, rmin.y(), 0)) - Y * f.floorDrop;
        QMatrix4x4 floorToWorld(X.x(), Y.x(), Z.x(), O.x(), X.y(), Y.y(), Z.y(), O.y(), X.z(), Y.z(), Z.z(), O.z(), 0, 0, 0, 1);
        const QVector3D cam = QVector3D(0, 0, p.distance) - O;
        const QVector3D camF(QVector3D::dotProduct(cam, X), QVector3D::dotProduct(cam, Y), QVector3D::dotProduct(cam, Z));
        const QVector3D sunF = f.cgPalette == 1 ? QVector3D(-0.55f, 0.16f, -0.82f).normalized()
                             : f.cgPalette == 2 ? QVector3D(0.35f, 0.75f, 0.25f).normalized() : QVector3D(0.55f, 0.62f, 0.45f).normalized();
        const QVector3D setC((rmin.x() + rmax.x()) * 0.5f, 0.f, (rmin.z() + rmax.z()) * 0.5f);
        const QVector3D tvF(0.f, by, m_glassZ);
        const bool models = f.models && !m_models.isEmpty();
        if (models) {   // mirror images: added faintly onto the floor's pixels only (the CG pass left their alpha at 0)
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
            drawModels(1, f, vpmT, floorToWorld, setC, sunF, camF, tvF, glassLod);
            glDisable(GL_BLEND);
            glEnable(GL_DEPTH_TEST);
        }
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);   // everything opaque again
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (models) {
            // shadows on the floor: each floor pixel darkened once (stencil)
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-2.f, -2.f);
            glClearStencil(0);
            glClear(GL_STENCIL_BUFFER_BIT);
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(GL_EQUAL, 0, 0xFF);
            glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            drawModels(2, f, vpmT, floorToWorld, setC, sunF, camF, tvF, glassLod);
            glDisable(GL_BLEND);
            glDisable(GL_STENCIL_TEST);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            drawModels(0, f, vpmT, floorToWorld, setC, sunF, camF, tvF, glassLod);   // the models themselves
        }
    }
    if (f.scene == 1 || f.scene == 2) {
        // Scene first, behind everything: the desk lies on the shadow's floor plane.
        const QMatrix4x4 vpm = viewProjection(p, f.viewport);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        m_backdrop.bind();
        m_backdrop.setUniformValue("uInvVP", vpm.inverted());
        m_backdrop.setUniformValue("uViewport", QVector2D(f.viewport.width(), f.viewport.height()));
        m_backdrop.setUniformValue("uCamPos", QVector3D(0, 0, p.distance));
        m_backdrop.setUniformValue("uFloorPoint", shadowModel.map(QVector3D(0, rmin.y() - 0.002f, 0)));
        m_backdrop.setUniformValue("uFloorNormal", shadowModel.mapVector(QVector3D(0, 1, 0)).normalized());
        m_backdrop.setUniformValue("uFloorX", shadowModel.mapVector(QVector3D(1, 0, 0)).normalized());
        m_backdrop.setUniformValue("uFloorZ", shadowModel.mapVector(QVector3D(0, 0, 1)).normalized());
        m_backdrop.setUniformValue("uSetCentre", QVector2D((rmin.x() + rmax.x()) * 0.5f, (rmin.z() + rmax.z()) * 0.5f));
        m_backdrop.setUniformValue("uSetHalf", QVector2D((rmax.x() - rmin.x()) * 0.5f, (rmax.z() - rmin.z()) * 0.5f));
        m_backdrop.setUniformValue("uScreenCentre", scrC);
        m_backdrop.setUniformValue("uScreenRight", scrR);
        m_backdrop.setUniformValue("uScreenUp", scrU);
        m_backdrop.setUniformValue("uScreenNormal", scrN);
        m_backdrop.setUniformValue("uScreenHalf", QVector2D(m_aspect * 0.5f, 0.5f));
        m_backdrop.setUniformValue("uInvModel", model.inverted());
        m_backdrop.setUniformValue("uAspect", m_aspect);
        m_backdrop.setUniformValue("uPivot", p.pivot);
        m_backdrop.setUniformValue("uGlassTex", 0);
        m_backdrop.setUniformValue("uHasGlass", f.glassTexture != 0);
        m_backdrop.setUniformValue("uGlassLod", glassLod);
        m_backdrop.setUniformValue("uMood", f.mood);
        m_backdrop.setUniformValue("uWood", f.wood);
        m_backdrop.setUniformValue("uFog", f.fog);
        m_backdrop.setUniformValue("uFogSteps", f.fogSteps);
        m_backdrop.setUniformValue("uTime", f.time);
        m_backdrop.setUniformValue("uScene", f.scene);
        const bool wall = f.scene == 2;
        const float drop = wall ? f.floorDrop : 0.f;
        m_backdrop.setUniformValue("uFloorDrop", drop);
        m_backdrop.setUniformValue("uWallQ", wall ? m_wallZ : (rmin.z() - 0.55f));
        m_backdrop.setUniformValue("uWallStyle", f.wallStyle);
        m_backdrop.setUniformValue("uRoomHalfW", roomHalfWidth(f.scene));
        m_backdrop.setUniformValue("uCeil", ceilingHeight(f.scene, drop));
        // heights on the wall are measured from the floor (which is `drop` below the set's bottom)
        const float floorY = rmin.y();
        m_backdrop.setUniformValue("uSetRect", QVector4D(rmin.x(), rmax.x(), rmin.y() - floorY + drop, rmax.y() - floorY + drop));
        m_backdrop.setUniformValue("uSetDepth", rmax.z() - m_wallZ);
        // Picture frames beside the set, each taking its picture's shape.
        QVector4D frames[4];
        int n = 0;
        if (wall && f.frameLayout > 0) {
            struct Slot { int side; int row; };   // side -1 left, +1 right; row 0 middle, +1 top, -1 bottom
            QVector<Slot> places;
            if (f.frameLayout == 1) places = {{-1, 0}, {1, 0}};
            else if (f.frameLayout == 2) places = {{-1, 1}, {-1, -1}};
            else if (f.frameLayout == 3) places = {{1, 1}, {1, -1}};
            else places = {{-1, 1}, {-1, -1}, {1, 1}, {1, -1}};
            // A deep set (a CRT sticks out ~1.2 from the wall) would hide pictures close beside it
            // when seen from an angle: the gap grows with how far the set stands off the wall.
            const float kMould = 0.045f, kMat = 0.05f;
            const float gap = (0.30f + 0.45f * std::max(0.f, rmax.z() - m_wallZ - 0.2f)) * f.picSpacing;
            const float vc = 0.f - floorY + drop;   // the screen's centre height above the floor
            int img = 0;
            for (const Slot& sl : places) {
                while (img < 4 && f.frameTex[img] == 0) ++img;
                if (img >= 4) break;
                const float a = std::clamp(f.frameAspect[img], 0.3f, 3.0f);
                const float maxW = (sl.row == 0 ? 0.95f : 0.80f) * f.picSize, maxH = (sl.row == 0 ? 0.80f : 0.46f) * f.picSize;
                float w = maxW, h = w / a;
                if (h > maxH) { h = maxH; w = h * a; }
                const float hw = w * 0.5f + kMat, hh = h * 0.5f + kMat;
                const float u = sl.side < 0 ? rmin.x() - gap - kMould - hw : rmax.x() + gap + kMould + hw;
                const float v = (sl.row == 0 ? vc : vc + sl.row * (0.05f + kMould + hh)) + f.picHeight;
                frames[n] = QVector4D(u, v, hw, hh);
                glActiveTexture(GL_TEXTURE1 + n);
                glBindTexture(GL_TEXTURE_2D, f.frameTex[img]);
                ++n; ++img;
            }
        }
        m_frameCount = n;
        m_backdrop.setUniformValue("uFrameCount", n);
        m_backdrop.setUniformValueArray("uFrame", frames, 4);
        m_backdrop.setUniformValue("uFrameStyle", f.frameStyle);
        m_backdrop.setUniformValue("uFrameTex0", 1);
        m_backdrop.setUniformValue("uFrameTex1", 2);
        m_backdrop.setUniformValue("uFrameTex2", 3);
        m_backdrop.setUniformValue("uFrameTex3", 4);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f.glassTexture);
        m_quadVao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_quadVao.release();
        m_backdrop.release();
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
    }

    m_prog.bind();
    m_prog.setUniformValue("uModel", model);
    m_prog.setUniformValue("uShadowModel", shadowModel);
    m_prog.setUniformValue("uVP", viewProjection(p, f.viewport));
    m_prog.setUniformValue("uCamPos", QVector3D(0, 0, p.distance));
    m_prog.setUniformValue("uAspect", m_aspect);
    m_prog.setUniformValue("uBulge", f.glassBulge);
    m_prog.setUniformValue("uPivot", p.pivot);
    m_prog.setUniformValue("uPicInGlass", QVector4D(f.picInGlass.x(), f.picInGlass.y(), f.picInGlass.width(), f.picInGlass.height()));
    m_prog.setUniformValue("uMargins", QVector4D(m_marginSide, m_marginTop, m_marginBottom, m_glassRadius));
    m_prog.setUniformValue("uCabinet", m_cabinet);
    // The set's own lighting follows the room's mood (the picture itself stays as it is).
    m_prog.setUniformValue("uLightLevel", (f.scene == 0 || f.scene == 4) ? 1.f : (f.mood == 0 ? 1.f : f.mood == 1 ? 0.55f : 0.32f));
    m_prog.setUniformValue("uReveal", f.reveal);
    m_prog.setUniformValue("uShadowRect", QVector4D(rmin.x() - pad, rmin.z() - pad, rmax.x() + pad, rmax.z() + pad));
    m_prog.setUniformValue("uFloorY", rmin.y() - 0.002f);
    m_prog.setUniformValue("uShadowFoot", QVector4D((rmin.x() + rmax.x()) * 0.5f, (rmin.z() + rmax.z()) * 0.5f,
                                                    (rmax.x() - rmin.x()) * 0.5f, (rmax.z() - rmin.z()) * 0.5f));
    m_prog.setUniformValue("uLed", f.ledOn);
    if (m_cabinet == ArcadeCabinet) {
        const ArcadeLayout al = arcadeLayout();
        m_prog.setUniformValue("uArc", QVector4D(al.uw, al.uh, al.ci, al.floorY));
        m_prog.setUniformValue("uArcStyle", f.arcadeArt);
        m_prog.setUniformValue("uMarqueeTex", 5);
        m_prog.setUniformValue("uHasMarquee", f.marqueeTexture != 0);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, f.marqueeTexture);
    }
    m_prog.setUniformValue("uTime", f.time);
    m_prog.setUniformValue("uHasGlass", f.glassTexture != 0);
    m_prog.setUniformValue("uHasBlur", f.blurTexture != 0);
    m_prog.setUniformValue("uGlassTex", 0);
    m_prog.setUniformValue("uBlurTex", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, f.blurTexture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, f.glassTexture);

    m_vao.bind();
    glDrawArrays(GL_TRIANGLES, 0, m_shadowFirst);
    // Shadow after the opaque cabinet: blended (premultiplied), depth-tested, no depth writes.
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    if ((!m_wallMount || m_cabinet == ArcadeCabinet) && m_cabinet != TheaterScreen && f.scene != 4)
        glDrawArrays(GL_TRIANGLES, m_shadowFirst, m_shadowCount);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    m_vao.release();
    m_prog.release();
    glDisable(GL_DEPTH_TEST);

    if (f.scene == 4) {
        // Lens flare, when the sun is in view.
        const QVector3D sunLocal = f.cgPalette == 1 ? QVector3D(-0.55f, 0.16f, -0.82f).normalized()
                                 : f.cgPalette == 2 ? QVector3D(0.35f, 0.75f, 0.25f).normalized() : QVector3D(0.55f, 0.62f, 0.45f).normalized();
        const QVector3D sunWorld = (shadowModel.mapVector(QVector3D(1, 0, 0)).normalized() * sunLocal.x() +
                                    shadowModel.mapVector(QVector3D(0, 1, 0)).normalized() * sunLocal.y() +
                                    shadowModel.mapVector(QVector3D(0, 0, 1)).normalized() * sunLocal.z());
        const QVector4D clip = vpmT * QVector4D(QVector3D(0, 0, p.distance) + sunWorld * 50.f, 1.f);
        float strength = 0.f;
        QVector2D sn;
        if (clip.w() > 0.f) {
            sn = QVector2D(clip.x() / clip.w(), clip.y() / clip.w());
            strength = std::clamp(1.3f - std::max(std::abs(sn.x()), std::abs(sn.y())), 0.f, 1.f);   // fades in as it enters the view
        }
        if (strength > 0.f) {
            glDisable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            m_flare.bind();
            m_flare.setUniformValue("uViewport", QVector2D(f.viewport.width(), f.viewport.height()));
            m_flare.setUniformValue("uSunNdc", sn);
            m_flare.setUniformValue("uStrength", strength);
            m_flare.setUniformValue("uTint", f.cgPalette == 1 ? QVector3D(1.0f, 0.65f, 0.4f) : QVector3D(0.9f, 0.95f, 1.0f));
            m_flare.setUniformValue("uBanding", f.cgBanding);
            m_quadVao.bind();
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            m_quadVao.release();
            m_flare.release();
            glDisable(GL_BLEND);
        }
    }
    if (f.scene == 3) {
        // In front of the screen: proscenium, masking, curtains, seats, step lights, the beam.
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        m_theaterFront.bind();
        theaterUniforms(m_theaterFront);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f.glassTexture);
        m_quadVao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_quadVao.release();
        m_theaterFront.release();
        glDisable(GL_BLEND);
    }
    if ((f.scene == 1 || f.scene == 2) && f.fog > 0.f) {
        // Haze in front of the set: a glow around the screen and a faint veil over everything.
        const QMatrix4x4 vpm = viewProjection(p, f.viewport);
        const QVector3D c = vpm.map(scrC);
        const QVector3D ex = vpm.map(scrC + scrR * (m_aspect * 0.5f)) - c;
        const QVector3D ey = vpm.map(scrC + scrU * 0.5f) - c;
        const QVector3D veil = f.mood == 0 ? QVector3D(0.16f, 0.13f, 0.10f) : f.mood == 1 ? QVector3D(0.05f, 0.06f, 0.09f)
                                                                                         : QVector3D(0.02f, 0.02f, 0.025f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        m_haze.bind();
        m_haze.setUniformValue("uViewport", QVector2D(f.viewport.width(), f.viewport.height()));
        m_haze.setUniformValue("uScreenNdc", QVector2D(c.x(), c.y()));
        m_haze.setUniformValue("uScreenRadius", QVector2D(std::hypot(ex.x(), ey.x()), std::hypot(ex.y(), ey.y())));
        m_haze.setUniformValue("uVeil", veil);
        m_haze.setUniformValue("uFog", f.fog);
        m_haze.setUniformValue("uGlassTex", 0);
        m_haze.setUniformValue("uHasGlass", f.glassTexture != 0);
        m_haze.setUniformValue("uGlassLod", glassLod);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, f.glassTexture);
        m_quadVao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_quadVao.release();
        m_haze.release();
        glDisable(GL_BLEND);
    }
}
