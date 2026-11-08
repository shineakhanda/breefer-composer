#include "engine/Effects.h"
#include "engine/PluginHost.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace bc::fx {
namespace {

Param makeParam(const char* id, const char* name, double value, double minV, double maxV)
{
    Param p;
    p.id = QString::fromLatin1(id);
    p.name = QString::fromLatin1(name);
    p.value = value;
    p.minValue = minV;
    p.maxValue = maxV;
    return p;
}

EffectInstance makeEffect(const char* type, const char* name,
                          std::initializer_list<Param> params)
{
    EffectInstance e;
    e.id = makeId(QStringLiteral("fx"));
    e.type = QString::fromLatin1(type);
    e.name = QString::fromLatin1(name);
    for (const auto& p : params) e.params.push_back(p);
    return e;
}

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

} // namespace

EffectInstance makeMotion()
{
    return makeEffect(kMotion, "Motion",
                      {
                          makeParam("position.x", "Position X", 0.0, -4000, 4000),
                          makeParam("position.y", "Position Y", 0.0, -4000, 4000),
                          makeParam("scale", "Scale", 100.0, 1, 500),
                          makeParam("rotation", "Rotation", 0.0, -360, 360),
                      });
}

EffectInstance makeOpacity()
{
    return makeEffect(kOpacity, "Opacity",
                      {makeParam("amount", "Opacity", 100.0, 0, 100)});
}

EffectInstance makeLumetri()
{
    return makeEffect(kLumetri, "Lumetri Color",
                      {
                          makeParam("exposure", "Exposure", 0.0, -5, 5),
                          makeParam("contrast", "Contrast", 0.0, -100, 100),
                          makeParam("highlights", "Highlights", 0.0, -100, 100),
                          makeParam("shadows", "Shadows", 0.0, -100, 100),
                          makeParam("whites", "Whites", 0.0, -100, 100),
                          makeParam("blacks", "Blacks", 0.0, -100, 100),
                          makeParam("temperature", "Temperature", 0.0, -100, 100),
                          makeParam("tint", "Tint", 0.0, -100, 100),
                          makeParam("saturation", "Saturation", 100.0, 0, 200),
                          makeParam("vibrance", "Vibrance", 0.0, -100, 100),
                      });
}

EffectInstance makeBlur()
{
    return makeEffect(kBlur, "Gaussian Blur",
                      {makeParam("radius", "Blurriness", 0.0, 0, 60)});
}

EffectInstance makeMask()
{
    return makeEffect(kMask, "Mask (Shape)",
                      {
                          makeParam("position.x", "Position X", 0.0, -4000, 4000),
                          makeParam("position.y", "Position Y", 0.0, -4000, 4000),
                          makeParam("width", "Width", 55.0, 2, 100),
                          makeParam("height", "Height", 55.0, 2, 100),
                          makeParam("feather", "Feather", 20.0, 0, 200),
                          makeParam("roundness", "Roundness", 100.0, 0, 100),
                          makeParam("opacity", "Mask Opacity", 100.0, 0, 100),
                      });
}

EffectInstance makeSharpen()
{
    return makeEffect(kSharpen, "Sharpen", {makeParam("amount", "Amount", 0.0, 0, 100)});
}

EffectInstance makeCrop()
{
    return makeEffect(kCrop, "Crop",
                      {
                          makeParam("left", "Left", 0.0, 0, 100),
                          makeParam("top", "Top", 0.0, 0, 100),
                          makeParam("right", "Right", 0.0, 0, 100),
                          makeParam("bottom", "Bottom", 0.0, 0, 100),
                      });
}

EffectInstance makeFlip()
{
    return makeEffect(kFlip, "Flip",
                      {
                          makeParam("horizontal", "Horizontal (0/1)", 1.0, 0, 1),
                          makeParam("vertical", "Vertical (0/1)", 0.0, 0, 1),
                      });
}

EffectInstance makeVignette()
{
    return makeEffect(kVignette, "Vignette",
                      {
                          makeParam("amount", "Amount", 40.0, 0, 100),
                          makeParam("size", "Size", 60.0, 10, 100),
                      });
}

EffectInstance makeVolume()
{
    return makeEffect(kVolume, "Volume", {makeParam("level", "Level (dB)", 0.0, -60, 15)});
}

EffectInstance makeBasic3D()
{
    return makeEffect(kBasic3D, "Basic 3D",
                      {
                          makeParam("swivel", "Swivel", 0.0, -180, 180),
                          makeParam("tilt", "Tilt", 0.0, -180, 180),
                          makeParam("distance", "Distance to Image", 0.0, -90, 500),
                      });
}

EffectInstance makeModel3D()
{
    return makeEffect(kModel3D, "3D Model",
                      {
                          makeParam("rotateX", "Rotation X", -15.0, -720, 720),
                          makeParam("rotateY", "Rotation Y", 30.0, -720, 720),
                          makeParam("rotateZ", "Rotation Z", 0.0, -720, 720),
                          makeParam("scale", "Scale", 100.0, 1, 1000),
                          makeParam("distance", "Camera Distance", 3.0, 1.2, 50),
                          makeParam("fov", "Field of View", 45.0, 10, 120),
                          makeParam("lightAzimuth", "Light Azimuth", 45.0, -180, 180),
                          makeParam("lightElevation", "Light Elevation", 40.0, -90, 90),
                          makeParam("ambient", "Ambient", 25.0, 0, 100),
                          makeParam("red", "Color Red", 200.0, 0, 255),
                          makeParam("green", "Color Green", 200.0, 0, 255),
                          makeParam("blue", "Color Blue", 210.0, 0, 255),
                          makeParam("wireframe", "Wireframe (0/1)", 0.0, 0, 1),
                      });
}

bool makeByType(const QString& type, EffectInstance& out)
{
    if (type == QLatin1String(kMotion)) out = makeMotion();
    else if (type == QLatin1String(kOpacity)) out = makeOpacity();
    else if (type == QLatin1String(kLumetri)) out = makeLumetri();
    else if (type == QLatin1String(kBlur)) out = makeBlur();
    else if (type == QLatin1String(kSharpen)) out = makeSharpen();
    else if (type == QLatin1String(kMask)) out = makeMask();
    else if (type == QLatin1String(kCrop)) out = makeCrop();
    else if (type == QLatin1String(kFlip)) out = makeFlip();
    else if (type == QLatin1String(kVignette)) out = makeVignette();
    else if (type == QLatin1String(kVolume)) out = makeVolume();
    else if (type == QLatin1String(kBasic3D)) out = makeBasic3D();
    else if (type == QLatin1String(kModel3D)) out = makeModel3D();
    else if (const PluginHost::Effect* p = PluginHost::instance().find(type)) {
        out = EffectInstance{};
        out.id = makeId(QStringLiteral("fx"));
        out.type = p->type;
        out.name = p->name;
        out.params = p->params;
    } else return false;
    return true;
}

void ensureAudioStack(Clip& clip)
{
    for (const auto& e : clip.effects)
        if (e.type == QLatin1String(kVolume)) return;
    clip.effects.push_back(makeVolume());
}

void applyCropFlip(QImage& image, const Clip& clip, Tick local)
{
    const double l = paramValue(clip, kCrop, "left", local, 0.0);
    const double t = paramValue(clip, kCrop, "top", local, 0.0);
    const double r = paramValue(clip, kCrop, "right", local, 0.0);
    const double b = paramValue(clip, kCrop, "bottom", local, 0.0);
    const bool flipH = paramValue(clip, kFlip, "horizontal", local, 0.0) >= 0.5;
    const bool flipV = paramValue(clip, kFlip, "vertical", local, 0.0) >= 0.5;

    if (flipH || flipV) image = image.flipped((flipH ? Qt::Horizontal : Qt::Orientations{}) |
                                              (flipV ? Qt::Vertical : Qt::Orientations{}));

    if (l <= 0 && t <= 0 && r <= 0 && b <= 0) return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied && image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    const int x0 = static_cast<int>(w * std::clamp(l, 0.0, 100.0) / 100.0);
    const int y0 = static_cast<int>(h * std::clamp(t, 0.0, 100.0) / 100.0);
    const int x1 = w - static_cast<int>(w * std::clamp(r, 0.0, 100.0) / 100.0);
    const int y1 = h - static_cast<int>(h * std::clamp(b, 0.0, 100.0) / 100.0);
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        const bool rowOut = y < y0 || y >= y1;
        for (int x = 0; x < w; ++x)
            if (rowOut || x < x0 || x >= x1) line[x] = 0;
    }
}

void applyVignette(QImage& image, double amount, double size)
{
    if (amount <= 0.0) return;
    if (image.format() != QImage::Format_ARGB32 && image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    const double cx = w * 0.5;
    const double cy = h * 0.5;
    const double inner = std::clamp(size / 100.0, 0.1, 1.0);
    const double strength = std::clamp(amount / 100.0, 0.0, 1.0);
    for (int y = 0; y < h; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        const double dy = (y - cy) / cy;
        for (int x = 0; x < w; ++x) {
            const double dx = (x - cx) / cx;
            const double d = std::sqrt(dx * dx + dy * dy) / 1.4142;
            if (d <= inner) continue;
            const double f = 1.0 - strength * std::clamp((d - inner) / (1.0 - inner + 1e-6), 0.0, 1.0);
            const QRgb p = line[x];
            line[x] = qRgba(static_cast<int>(qRed(p) * f), static_cast<int>(qGreen(p) * f),
                            static_cast<int>(qBlue(p) * f), qAlpha(p));
        }
    }
}

bool hasMask(const Clip& clip)
{
    for (const auto& e : clip.effects)
        if (e.enabled && e.type == QLatin1String(kMask)) return true;
    return false;
}

void ensureIntrinsics(Clip& clip)
{
    bool motion = false;
    bool opacity = false;
    for (const auto& e : clip.effects) {
        motion = motion || e.type == QLatin1String(kMotion);
        opacity = opacity || e.type == QLatin1String(kOpacity);
    }
    if (!opacity) clip.effects.prepend(makeOpacity());
    if (!motion) clip.effects.prepend(makeMotion());
}

void ensureStandardStack(Clip& clip)
{
    auto has = [&](const char* type) {
        for (const auto& e : clip.effects)
            if (e.type == QLatin1String(type)) return true;
        return false;
    };

    if (!has(kMotion)) clip.effects.push_back(makeMotion());
    if (!has(kOpacity)) clip.effects.push_back(makeOpacity());
    if (!has(kLumetri)) clip.effects.push_back(makeLumetri());
}

const Param* findParam(const Clip& clip, const char* effectType, const char* paramId)
{
    for (const auto& e : clip.effects) {
        if (!e.enabled || e.type != QLatin1String(effectType)) continue;
        if (const Param* p = e.param(QString::fromLatin1(paramId))) return p;
    }
    return nullptr;
}

Param* findParam(Clip& clip, const char* effectType, const char* paramId)
{
    for (auto& e : clip.effects) {
        if (e.type != QLatin1String(effectType)) continue;
        if (Param* p = e.param(QString::fromLatin1(paramId))) return p;
    }
    return nullptr;
}

double paramValue(const Clip& clip, const char* effectType, const char* paramId,
                  Tick localTime, double fallback)
{
    if (const Param* p = findParam(clip, effectType, paramId))
        return p->evaluate(localTime);
    return fallback;
}

Transform transformFor(const Clip& clip, Tick localTime)
{
    Transform t;
    t.x = paramValue(clip, kMotion, "position.x", localTime, 0.0);
    t.y = paramValue(clip, kMotion, "position.y", localTime, 0.0);
    t.scale = paramValue(clip, kMotion, "scale", localTime, 100.0) / 100.0;
    t.rotation = paramValue(clip, kMotion, "rotation", localTime, 0.0);
    return t;
}

void applyColor(QImage& image, const Clip& clip, Tick localTime)
{
    bool hasColor = false;
    for (const auto& e : clip.effects) {
        if (e.enabled && e.type == QLatin1String(kLumetri)) {
            hasColor = true;
            break;
        }
    }
    if (!hasColor) return;

    const double exposure = paramValue(clip, kLumetri, "exposure", localTime, 0.0);
    const double contrast = paramValue(clip, kLumetri, "contrast", localTime, 0.0) / 100.0;
    const double highlights = paramValue(clip, kLumetri, "highlights", localTime, 0.0) / 100.0;
    const double shadows = paramValue(clip, kLumetri, "shadows", localTime, 0.0) / 100.0;
    const double whites = paramValue(clip, kLumetri, "whites", localTime, 0.0) / 100.0;
    const double blacks = paramValue(clip, kLumetri, "blacks", localTime, 0.0) / 100.0;
    const double temperature = paramValue(clip, kLumetri, "temperature", localTime, 0.0) / 100.0;
    const double tint = paramValue(clip, kLumetri, "tint", localTime, 0.0) / 100.0;
    const double saturation = paramValue(clip, kLumetri, "saturation", localTime, 100.0) / 100.0;
    const double vibrance = paramValue(clip, kLumetri, "vibrance", localTime, 0.0) / 100.0;

    const bool neutral = std::abs(exposure) < 1e-6 && std::abs(contrast) < 1e-6
                         && std::abs(highlights) < 1e-6 && std::abs(shadows) < 1e-6
                         && std::abs(whites) < 1e-6 && std::abs(blacks) < 1e-6
                         && std::abs(temperature) < 1e-6 && std::abs(tint) < 1e-6
                         && std::abs(saturation - 1.0) < 1e-6 && std::abs(vibrance) < 1e-6;
    if (neutral) return;

    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    const double gain = std::pow(2.0, exposure);
    const double contrastFactor = (contrast >= 0.0)
                                      ? 1.0 + contrast * 1.6
                                      : 1.0 + contrast * 0.75;
    const double lift = blacks * 0.18;
    const double whiteGain = 1.0 + whites * 0.35;
    const double tempR = 1.0 + temperature * 0.25;
    const double tempB = 1.0 - temperature * 0.25;
    const double tintG = 1.0 + tint * 0.16;
    const double tintRB = 1.0 - tint * 0.08;

    const int width = image.width();
    const int height = image.height();

    for (int y = 0; y < height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb px = line[x];
            double r = qRed(px) / 255.0;
            double g = qGreen(px) / 255.0;
            double b = qBlue(px) / 255.0;

            r *= tempR * tintRB;
            g *= tintG;
            b *= tempB * tintRB;

            r *= gain;
            g *= gain;
            b *= gain;

            // Tone shaping: lift blacks, roll whites, then contrast around 0.5.
            auto shape = [&](double v) {
                v = v * whiteGain + lift;
                const double shadowWeight = (1.0 - clamp01(v)) * (1.0 - clamp01(v));
                const double highWeight = clamp01(v) * clamp01(v);
                v += shadows * shadowWeight * 0.35;
                v += highlights * highWeight * 0.35;
                v = (v - 0.5) * contrastFactor + 0.5;
                return v;
            };
            r = shape(r);
            g = shape(g);
            b = shape(b);

            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            double sat = saturation;
            if (std::abs(vibrance) > 1e-6) {
                const double currentSat = std::max(0.0, std::min(1.0,
                    std::max({r, g, b}) - std::min({r, g, b})));
                sat += vibrance * (1.0 - currentSat);
            }
            r = luma + (r - luma) * sat;
            g = luma + (g - luma) * sat;
            b = luma + (b - luma) * sat;

            line[x] = qRgb(static_cast<int>(clamp01(r) * 255.0 + 0.5),
                           static_cast<int>(clamp01(g) * 255.0 + 0.5),
                           static_cast<int>(clamp01(b) * 255.0 + 0.5));
        }
    }
}

bool exportCube(const Clip& clip, Tick localTime, const QString& path, int size, QString* error)
{
    size = std::clamp(size, 2, 65);
    // Run every lattice colour through the real grade, one pixel each.
    QImage lattice(size * size * size, 1, QImage::Format_ARGB32);
    auto* px = reinterpret_cast<QRgb*>(lattice.scanLine(0));
    int i = 0;
    for (int b = 0; b < size; ++b)
        for (int g = 0; g < size; ++g)
            for (int r = 0; r < size; ++r)
                px[i++] = qRgb(r * 255 / (size - 1), g * 255 / (size - 1), b * 255 / (size - 1));
    applyColor(lattice, clip, localTime);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) *error = file.errorString();
        return false;
    }
    QTextStream out(&file);
    out << "TITLE \"Breefer Composer Lumetri\"\n";
    out << "LUT_3D_SIZE " << size << "\n";
    out << "DOMAIN_MIN 0.0 0.0 0.0\nDOMAIN_MAX 1.0 1.0 1.0\n";
    const auto* res = reinterpret_cast<const QRgb*>(lattice.constScanLine(0));
    for (int k = 0; k < size * size * size; ++k)
        out << QString::number(qRed(res[k]) / 255.0, 'f', 6) << ' ' << QString::number(qGreen(res[k]) / 255.0, 'f', 6)
            << ' ' << QString::number(qBlue(res[k]) / 255.0, 'f', 6) << '\n';
    return true;
}

void applyBlur(QImage& image, double radius)
{
    const int r = static_cast<int>(std::lround(radius));
    if (r <= 0) return;
    if (image.format() != QImage::Format_ARGB32)
        image = image.convertToFormat(QImage::Format_ARGB32);

    const int width = image.width();
    const int height = image.height();
    const int window = r * 2 + 1;

    QImage temp(width, height, QImage::Format_ARGB32);
    std::vector<int> sums(3);

    for (int y = 0; y < height; ++y) {
        const QRgb* src = reinterpret_cast<const QRgb*>(image.scanLine(y));
        QRgb* dst = reinterpret_cast<QRgb*>(temp.scanLine(y));
        sums = {0, 0, 0};
        for (int x = -r; x <= r; ++x) {
            const int cx = std::clamp(x, 0, width - 1);
            sums[0] += qRed(src[cx]);
            sums[1] += qGreen(src[cx]);
            sums[2] += qBlue(src[cx]);
        }
        for (int x = 0; x < width; ++x) {
            dst[x] = qRgb(sums[0] / window, sums[1] / window, sums[2] / window);
            const int addX = std::clamp(x + r + 1, 0, width - 1);
            const int subX = std::clamp(x - r, 0, width - 1);
            sums[0] += qRed(src[addX]) - qRed(src[subX]);
            sums[1] += qGreen(src[addX]) - qGreen(src[subX]);
            sums[2] += qBlue(src[addX]) - qBlue(src[subX]);
        }
    }

    for (int x = 0; x < width; ++x) {
        sums = {0, 0, 0};
        for (int y = -r; y <= r; ++y) {
            const int cy = std::clamp(y, 0, height - 1);
            const QRgb px = reinterpret_cast<const QRgb*>(temp.scanLine(cy))[x];
            sums[0] += qRed(px);
            sums[1] += qGreen(px);
            sums[2] += qBlue(px);
        }
        for (int y = 0; y < height; ++y) {
            QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
            dst[x] = qRgb(sums[0] / window, sums[1] / window, sums[2] / window);
            const int addY = std::clamp(y + r + 1, 0, height - 1);
            const int subY = std::clamp(y - r, 0, height - 1);
            const QRgb addPx = reinterpret_cast<const QRgb*>(temp.scanLine(addY))[x];
            const QRgb subPx = reinterpret_cast<const QRgb*>(temp.scanLine(subY))[x];
            sums[0] += qRed(addPx) - qRed(subPx);
            sums[1] += qGreen(addPx) - qGreen(subPx);
            sums[2] += qBlue(addPx) - qBlue(subPx);
        }
    }
}

void applySharpen(QImage& image, double amount)
{
    if (amount <= 0.0) return;
    QImage blurred = image;
    applyBlur(blurred, 1.5);

    const double strength = std::min(3.0, amount / 50.0);
    for (int y = 0; y < image.height(); ++y) {
        QRgb* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
        const QRgb* src = reinterpret_cast<const QRgb*>(blurred.scanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            auto sharpen = [&](int a, int b) {
                return std::clamp(static_cast<int>(a + (a - b) * strength), 0, 255);
            };
            dst[x] = qRgb(sharpen(qRed(dst[x]), qRed(src[x])),
                          sharpen(qGreen(dst[x]), qGreen(src[x])),
                          sharpen(qBlue(dst[x]), qBlue(src[x])));
        }
    }
}

} // namespace bc::fx
