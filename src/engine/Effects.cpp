#include <QThread>
#include <thread>
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
                          makeParam("hue", "Hue Shift", 0.0, -180, 180),
                          makeParam("fade", "Faded Film", 0.0, 0, 100),
                          makeParam("lift.r", "Shadows Red", 0.0, -100, 100),
                          makeParam("lift.g", "Shadows Green", 0.0, -100, 100),
                          makeParam("lift.b", "Shadows Blue", 0.0, -100, 100),
                          makeParam("gamma.r", "Midtones Red", 0.0, -100, 100),
                          makeParam("gamma.g", "Midtones Green", 0.0, -100, 100),
                          makeParam("gamma.b", "Midtones Blue", 0.0, -100, 100),
                          makeParam("gain.r", "Highlights Red", 0.0, -100, 100),
                          makeParam("gain.g", "Highlights Green", 0.0, -100, 100),
                          makeParam("gain.b", "Highlights Blue", 0.0, -100, 100),
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

// Projects saved before the colour wheels existed carry a Lumetri effect without the new
// parameters; add them (at their neutral values) so the panel and the renderer see a full set.
static void upgradeLumetri(Clip& clip)
{
    for (auto& e : clip.effects) {
        if (e.type != QLatin1String(kLumetri)) continue;
        const EffectInstance full = makeLumetri();
        for (const Param& p : full.params)
            if (!e.param(p.id)) e.params.push_back(p);
    }
}

void ensureIntrinsics(Clip& clip)
{
    upgradeLumetri(clip);
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

    upgradeLumetri(clip);
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
    if (!hasColor || image.isNull()) return;

    auto value = [&](const char* id, double fallback) { return paramValue(clip, kLumetri, id, localTime, fallback); };
    const double exposure = value("exposure", 0.0);
    const double contrast = value("contrast", 0.0) / 100.0;
    const double highlights = value("highlights", 0.0) / 100.0;
    const double shadows = value("shadows", 0.0) / 100.0;
    const double whites = value("whites", 0.0) / 100.0;
    const double blacks = value("blacks", 0.0) / 100.0;
    const double temperature = value("temperature", 0.0) / 100.0;
    const double tint = value("tint", 0.0) / 100.0;
    const double saturation = value("saturation", 100.0) / 100.0;
    const double vibrance = value("vibrance", 0.0) / 100.0;
    const double hueDegrees = value("hue", 0.0);
    const double fade = value("fade", 0.0) / 100.0;
    static const char* kLift[3] = {"lift.r", "lift.g", "lift.b"};
    static const char* kGamma[3] = {"gamma.r", "gamma.g", "gamma.b"};
    static const char* kGain[3] = {"gain.r", "gain.g", "gain.b"};
    double lift[3], gamma[3], gain[3];
    bool wheelsNeutral = true;
    for (int k = 0; k < 3; ++k) {
        const double l = value(kLift[k], 0.0), g = value(kGamma[k], 0.0), n = value(kGain[k], 0.0);
        wheelsNeutral = wheelsNeutral && std::abs(l) < 1e-6 && std::abs(g) < 1e-6 && std::abs(n) < 1e-6;
        lift[k] = l / 400.0;                      // +-0.25 black offset
        gamma[k] = std::pow(2.0, g / 100.0);      // midtone power
        gain[k] = 1.0 + n / 200.0;                // 0.5x .. 1.5x
    }

    const bool toneNeutral = std::abs(exposure) < 1e-6 && std::abs(contrast) < 1e-6 && std::abs(highlights) < 1e-6
                             && std::abs(shadows) < 1e-6 && std::abs(whites) < 1e-6 && std::abs(blacks) < 1e-6
                             && std::abs(temperature) < 1e-6 && std::abs(tint) < 1e-6 && std::abs(fade) < 1e-6
                             && wheelsNeutral;
    const bool matrixNeutral = std::abs(saturation - 1.0) < 1e-6 && std::abs(hueDegrees) < 1e-6;
    const bool vibranceNeutral = std::abs(vibrance) < 1e-6;
    if (toneNeutral && matrixNeutral && vibranceNeutral) return;

    // Everything that acts on one channel at a time is folded into three 256-entry tables, so the
    // per-pixel cost is three lookups however many sliders are moved.
    const double wb[3] = {(1.0 + temperature * 0.25) * (1.0 - tint * 0.08), 1.0 + tint * 0.16,
                          (1.0 - temperature * 0.25) * (1.0 - tint * 0.08)};
    const double gainExposure = std::pow(2.0, exposure);
    const double contrastFactor = contrast >= 0.0 ? 1.0 + contrast * 1.6 : 1.0 + contrast * 0.75;
    const double whiteGain = 1.0 + whites * 0.35;
    const double blackLift = blacks * 0.18;

    uint8_t table[3][256];
    for (int k = 0; k < 3; ++k) {
        for (int i = 0; i < 256; ++i) {
            double v = i / 255.0;
            if (!toneNeutral) {
                v *= wb[k] * gainExposure;
                v = std::max(0.0, v * gain[k] + lift[k]);
                if (gamma[k] != 1.0) v = std::pow(v, 1.0 / gamma[k]);
                v = v * whiteGain + blackLift;
                const double c = clamp01(v);
                v += shadows * (1.0 - c) * (1.0 - c) * 0.35;
                v += highlights * c * c * 0.35;
                v = (v - 0.5) * contrastFactor + 0.5;
                if (fade > 0.0) v = v * (1.0 - fade * 0.3) + fade * 0.12;
            }
            table[k][i] = static_cast<uint8_t>(clamp01(v) * 255.0 + 0.5);
        }
    }

    // Saturation and hue rotation are linear, so they share one 3x3 matrix (fixed point, 1/1024).
    int m[9] = {1024, 0, 0, 0, 1024, 0, 0, 0, 1024};
    if (!matrixNeutral) {
        const double lr = 0.2126, lg = 0.7152, lb = 0.0722;
        const double a = hueDegrees * 3.14159265358979323846 / 180.0;
        const double cs = std::cos(a), sn = std::sin(a);
        const double hue[9] = {lr + cs * (1 - lr) - sn * lr,        lg - cs * lg - sn * lg,           lb - cs * lb + sn * (1 - lb),
                               lr - cs * lr + sn * 0.143,           lg + cs * (1 - lg) + sn * 0.140,  lb - cs * lb - sn * 0.283,
                               lr - cs * lr - sn * (1 - lr),        lg - cs * lg + sn * lg,           lb + cs * (1 - lb) + sn * lb};
        const double sat[9] = {lr + (1 - lr) * saturation, lg - lg * saturation,         lb - lb * saturation,
                               lr - lr * saturation,       lg + (1 - lg) * saturation,   lb - lb * saturation,
                               lr - lr * saturation,       lg - lg * saturation,         lb + (1 - lb) * saturation};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                double sum = 0.0;
                for (int k = 0; k < 3; ++k) sum += sat[r * 3 + k] * hue[k * 3 + c];
                m[r * 3 + c] = static_cast<int>(std::lround(sum * 1024.0));
            }
    }
    const bool useMatrix = !matrixNeutral;
    const bool useVibrance = !vibranceNeutral;
    const int vibranceQ = static_cast<int>(std::lround(vibrance * 256.0));

    const QImage::Format fmt = image.format();
    if (fmt != QImage::Format_ARGB32 && fmt != QImage::Format_ARGB32_Premultiplied && fmt != QImage::Format_RGB32)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const bool premultiplied = image.format() == QImage::Format_ARGB32_Premultiplied;
    image.detach();

    auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    auto processRows = [&](int y0, int y1) {
        const int width = image.width();
        for (int y = y0; y < y1; ++y) {
            QRgb* line = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const QRgb px = line[x];
                const int a = qAlpha(px);
                if (a == 0) continue;
                int r = qRed(px), g = qGreen(px), b = qBlue(px);
                const bool unpremultiply = premultiplied && a < 255;
                if (unpremultiply) {
                    r = std::min(255, r * 255 / a);
                    g = std::min(255, g * 255 / a);
                    b = std::min(255, b * 255 / a);
                }
                r = table[0][r];
                g = table[1][g];
                b = table[2][b];
                if (useMatrix) {
                    const int nr = (m[0] * r + m[1] * g + m[2] * b + 512) >> 10;
                    const int ng = (m[3] * r + m[4] * g + m[5] * b + 512) >> 10;
                    const int nb = (m[6] * r + m[7] * g + m[8] * b + 512) >> 10;
                    r = clamp8(nr);
                    g = clamp8(ng);
                    b = clamp8(nb);
                }
                if (useVibrance) {
                    // Boost muted colours more than vivid ones.
                    const int hi = std::max({r, g, b}), lo = std::min({r, g, b});
                    const int factor = 256 + vibranceQ * (255 - (hi - lo)) / 255;
                    const int luma = (r * 54 + g * 183 + b * 19) >> 8;
                    r = clamp8(luma + (((r - luma) * factor) >> 8));
                    g = clamp8(luma + (((g - luma) * factor) >> 8));
                    b = clamp8(luma + (((b - luma) * factor) >> 8));
                }
                if (unpremultiply) {
                    r = r * a / 255;
                    g = g * a / 255;
                    b = b * a / 255;
                }
                line[x] = qRgba(r, g, b, a);
            }
        }
    };

    const int height = image.height();
    const int cores = std::min(4, std::max(1, QThread::idealThreadCount() - 1));
    if (cores <= 1 || static_cast<qint64>(image.width()) * height < 200000) {
        processRows(0, height);
        return;
    }
    std::vector<std::thread> workers;
    const int band = (height + cores - 1) / cores;
    for (int i = 1; i < cores; ++i) {
        const int y0 = i * band, y1 = std::min(height, y0 + band);
        if (y0 < y1) workers.emplace_back(processRows, y0, y1);
    }
    processRows(0, std::min(height, band));
    for (auto& t : workers) t.join();
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
