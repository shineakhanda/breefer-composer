#include "engine/Model3D.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QPainter>
#include <QPolygonF>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace bc {
namespace {

constexpr double kDeg = 3.14159265358979323846 / 180.0;

struct V3 {
    double x, y, z;
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
};
V3 cross(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
double dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 normalized(const V3& v)
{
    const double l = std::sqrt(dot(v, v));
    return l > 1e-12 ? V3{v.x / l, v.y / l, v.z / l} : V3{0, 0, 1};
}

} // namespace

std::shared_ptr<const Mesh> Mesh::load(const QString& path)
{
    static QMutex mutex;
    static QHash<QString, std::shared_ptr<const Mesh>> cache;
    QMutexLocker lock(&mutex);
    if (auto it = cache.find(path); it != cache.end()) return it.value();

    auto mesh = std::make_shared<Mesh>();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        mesh->error = file.errorString();
        cache.insert(path, mesh);
        return mesh;
    }
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1String("v "))) {
            const QStringList p = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() >= 3) mesh->positions << p[0].toFloat() << p[1].toFloat() << p[2].toFloat();
        } else if (line.startsWith(QLatin1String("f "))) {
            const QStringList p = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            QVector<int> idx;
            const int count = mesh->positions.size() / 3;
            for (const QString& token : p) {
                int v = token.section(QLatin1Char('/'), 0, 0).toInt();
                if (v < 0) v = count + v + 1;   // relative indices
                if (v >= 1 && v <= count) idx << v - 1;
            }
            for (int i = 1; i + 1 < idx.size(); ++i) mesh->triangles.push_back({idx[0], idx[i], idx[i + 1]});
        }
    }
    if (mesh->triangles.isEmpty()) mesh->error = QStringLiteral("No faces in OBJ file");

    // Centre and scale to a unit sphere so any model frames the same way.
    if (!mesh->positions.isEmpty()) {
        double mn[3] = {1e300, 1e300, 1e300};
        double mx[3] = {-1e300, -1e300, -1e300};
        for (int i = 0; i < mesh->positions.size(); i += 3)
            for (int k = 0; k < 3; ++k) {
                mn[k] = std::min(mn[k], static_cast<double>(mesh->positions[i + k]));
                mx[k] = std::max(mx[k], static_cast<double>(mesh->positions[i + k]));
            }
        const double c[3] = {(mn[0] + mx[0]) / 2, (mn[1] + mx[1]) / 2, (mn[2] + mx[2]) / 2};
        double r = 1e-9;
        for (int i = 0; i < mesh->positions.size(); i += 3) {
            const double dx = mesh->positions[i] - c[0], dy = mesh->positions[i + 1] - c[1], dz = mesh->positions[i + 2] - c[2];
            r = std::max(r, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
        for (int i = 0; i < mesh->positions.size(); i += 3)
            for (int k = 0; k < 3; ++k)
                mesh->positions[i + k] = static_cast<float>((mesh->positions[i + k] - c[k]) / r);
    }
    cache.insert(path, mesh);
    return mesh;
}

QImage renderMesh(const Mesh& mesh, const RenderParams3D& p, int width, int height)
{
    QImage image(std::max(2, width), std::max(2, height), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (!mesh.valid()) return image;

    // Rotation matrix (Z * Y * X).
    const double ax = p.rotateX * kDeg, ay = p.rotateY * kDeg, az = p.rotateZ * kDeg;
    const double cx = std::cos(ax), sx = std::sin(ax), cy = std::cos(ay), sy = std::sin(ay), cz = std::cos(az), sz = std::sin(az);
    const double m[3][3] = {
        {cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz},
        {cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz},
        {-sy, sx * cy, cx * cy},
    };
    const double s = std::max(0.01, p.scale / 100.0);
    const double dist = std::max(1.2, p.distance);
    const double focal = (height * 0.5) / std::tan(std::clamp(p.fov, 10.0, 120.0) * kDeg * 0.5);

    const int n = mesh.positions.size() / 3;
    QVector<V3> world(n);
    QVector<QPointF> screen(n);
    for (int i = 0; i < n; ++i) {
        const double x = mesh.positions[i * 3] * s, y = mesh.positions[i * 3 + 1] * s, z = mesh.positions[i * 3 + 2] * s;
        V3 w{m[0][0] * x + m[0][1] * y + m[0][2] * z, m[1][0] * x + m[1][1] * y + m[1][2] * z,
             m[2][0] * x + m[2][1] * y + m[2][2] * z};
        world[i] = w;
        const double depth = dist - w.z;
        const double k = focal / std::max(0.05, depth);
        screen[i] = QPointF(width * 0.5 + p.offsetX + w.x * k, height * 0.5 + p.offsetY - w.y * k);
    }

    const V3 light = normalized({std::cos(p.lightElevation * kDeg) * std::sin(p.lightAzimuth * kDeg),
                                 std::sin(p.lightElevation * kDeg),
                                 std::cos(p.lightElevation * kDeg) * std::cos(p.lightAzimuth * kDeg)});
    const double ambient = std::clamp(p.ambient / 100.0, 0.0, 1.0);

    struct Face { int tri; double depth; double shade; };
    QVector<Face> faces;
    faces.reserve(mesh.triangles.size());
    for (int t = 0; t < mesh.triangles.size(); ++t) {
        const auto& tri = mesh.triangles[t];
        const V3 normal = normalized(cross(world[tri.b] - world[tri.a], world[tri.c] - world[tri.a]));
        // Camera looks down -z from +dist: cull faces pointing away.
        const V3 toCamera = normalized(V3{0, 0, dist} - world[tri.a]);
        const double facing = dot(normal, toCamera);
        if (!p.wireframe && facing <= 0) continue;
        const double diffuse = std::max(0.0, dot(normal, light));
        faces.push_back({t, (world[tri.a].z + world[tri.b].z + world[tri.c].z) / 3.0,
                         ambient + (1.0 - ambient) * diffuse});
    }
    std::sort(faces.begin(), faces.end(), [](const Face& a, const Face& b) { return a.depth < b.depth; });

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Face& f : faces) {
        const auto& tri = mesh.triangles[f.tri];
        QPolygonF poly;
        poly << screen[tri.a] << screen[tri.b] << screen[tri.c];
        if (p.wireframe) {
            painter.setPen(QPen(p.color, 1.0));
            painter.setBrush(Qt::NoBrush);
        } else {
            const QColor c = QColor::fromRgbF(std::min(1.0, p.color.redF() * f.shade), std::min(1.0, p.color.greenF() * f.shade),
                                              std::min(1.0, p.color.blueF() * f.shade));
            painter.setPen(QPen(c, 0.6));   // hairline seals cracks between triangles
            painter.setBrush(c);
        }
        painter.drawPolygon(poly);
    }
    painter.end();
    return image;
}

} // namespace bc
