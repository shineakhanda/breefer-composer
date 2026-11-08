#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <QVector>
#include <memory>

namespace bc {

// A triangle mesh loaded from Wavefront OBJ, centred and scaled to fit a unit
// sphere. Thread-safe cache: models are shared by the render and export threads.
struct Mesh {
    struct Tri { int a, b, c; };
    QVector<float> positions;   // xyz triples
    QVector<Tri> triangles;
    QString error;

    bool valid() const { return !triangles.isEmpty(); }
    static std::shared_ptr<const Mesh> load(const QString& path);
};

struct RenderParams3D {
    double rotateX = -15.0;      // degrees
    double rotateY = 30.0;
    double rotateZ = 0.0;
    double scale = 100.0;        // percent of frame height
    double distance = 3.0;       // camera distance in model units
    double fov = 45.0;           // degrees
    double lightAzimuth = 45.0;  // degrees
    double lightElevation = 40.0;
    double ambient = 25.0;       // percent
    QColor color = QColor(200, 200, 210);
    bool wireframe = false;
    double offsetX = 0.0;        // pixels, frame centre relative
    double offsetY = 0.0;
};

// Software renderer: back-face culling, painter's-order depth sort, flat
// Lambert shading. Output is transparent where nothing is drawn.
QImage renderMesh(const Mesh& mesh, const RenderParams3D& params, int width, int height);

} // namespace bc
