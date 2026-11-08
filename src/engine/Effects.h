#pragma once

#include "core/Project.h"

#include <QImage>

namespace bc::fx {

// Effect type ids stored in EffectInstance::type.
inline constexpr const char* kMotion = "motion";
inline constexpr const char* kOpacity = "opacity";
inline constexpr const char* kLumetri = "lumetri";
inline constexpr const char* kBlur = "blur";
inline constexpr const char* kSharpen = "sharpen";
inline constexpr const char* kMask = "mask";
inline constexpr const char* kCrop = "crop";
inline constexpr const char* kFlip = "flip";
inline constexpr const char* kVignette = "vignette";
inline constexpr const char* kVolume = "volume";
inline constexpr const char* kBasic3D = "basic3d";
inline constexpr const char* kModel3D = "model3d";

EffectInstance makeMotion();
EffectInstance makeOpacity();
EffectInstance makeLumetri();
EffectInstance makeBlur();
EffectInstance makeMask();
EffectInstance makeSharpen();
EffectInstance makeCrop();
EffectInstance makeFlip();
EffectInstance makeVignette();
EffectInstance makeVolume();
EffectInstance makeBasic3D();
EffectInstance makeModel3D();

// Factory by type id; returns false for unknown types.
bool makeByType(const QString& type, EffectInstance& out);

// Adds the stack an audio clip needs (Volume).
void ensureAudioStack(Clip& clip);

// Crop insets in percent (0..100) and flips.
void applyCropFlip(QImage& image, const Clip& clip, Tick localTime);
void applyVignette(QImage& image, double amount, double size);

// True when the clip carries an enabled shape mask.
bool hasMask(const Clip& clip);

// Premiere's intrinsic video effects (Motion, Opacity), always present.
void ensureIntrinsics(Clip& clip);

// Adds the standard stack a video clip needs (Motion, Opacity, Lumetri).
void ensureStandardStack(Clip& clip);

const Param* findParam(const Clip& clip, const char* effectType, const char* paramId);
Param* findParam(Clip& clip, const char* effectType, const char* paramId);
double paramValue(const Clip& clip, const char* effectType, const char* paramId,
                  Tick localTime, double fallback);

struct Transform {
    double scale = 100.0;      // percent
    double rotation = 0.0;     // degrees
    double x = 0.0;            // pixels, relative to centre
    double y = 0.0;
};

Transform transformFor(const Clip& clip, Tick localTime);

// Colour grade in place (Lumetri Basic Correction approximation).
void applyColor(QImage& image, const Clip& clip, Tick localTime);

// Bakes the clip's Lumetri grade at `localTime` into a 3D LUT (.cube, Adobe/Resolve format).
bool exportCube(const Clip& clip, Tick localTime, const QString& path, int size = 33, QString* error = nullptr);

// Separable box blur, radius in pixels.
void applyBlur(QImage& image, double radius);

// Unsharp mask.
void applySharpen(QImage& image, double amount);

} // namespace bc::fx
