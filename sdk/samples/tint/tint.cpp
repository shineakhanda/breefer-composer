// Sample Breefer plugin in C++: "Color Tint" and "Pixelate".
//
// Shows the C++ way to satisfy the C ABI: put everything the host calls inside
// extern "C", keep state out of globals (render() is called from several threads),
// and avoid exceptions escaping into the host.
#include "breefer_plugin.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace {

constexpr std::array<BreeferParamDesc, 4> kTintParams{{
    {"red", "Red", 0.0f, 255.0f, 255.0f},
    {"green", "Green", 0.0f, 255.0f, 140.0f},
    {"blue", "Blue", 0.0f, 255.0f, 40.0f},
    {"amount", "Amount (%)", 0.0f, 100.0f, 35.0f},
}};
constexpr std::array<BreeferParamDesc, 1> kPixelateParams{{
    // Percent of frame height, so the look is identical in the half-resolution preview and in export.
    {"size", "Block Size (% of height)", 0.2f, 25.0f, 2.5f},
}};

constexpr std::array<BreeferEffectDesc, 2> kEffects{{
    {BREEFER_PLUGIN_API_VERSION, "org.breefer.sample.tint", "Color Tint (C++)", "Color Correction",
     static_cast<int>(kTintParams.size()), kTintParams.data()},
    {BREEFER_PLUGIN_API_VERSION, "org.breefer.sample.pixelate", "Pixelate (C++)", "Stylize",
     static_cast<int>(kPixelateParams.size()), kPixelateParams.data()},
}};

inline uint8_t clamp8(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); }

// Pixels are BGRA, premultiplied. Tinting a premultiplied pixel means scaling the
// tint by the pixel's own alpha so transparent areas stay transparent.
void tint(BreeferImage& img, const float* p)
{
    const float amount = std::clamp(p[3] / 100.0f, 0.0f, 1.0f);
    const float tintBgr[3] = {p[2], p[1], p[0]};
    for (int y = 0; y < img.height; ++y) {
        uint8_t* px = img.pixels + static_cast<size_t>(y) * img.stride;
        for (int x = 0; x < img.width; ++x, px += 4) {
            const float alpha = px[3] / 255.0f;
            for (int c = 0; c < 3; ++c)
                px[c] = clamp8(px[c] * (1.0f - amount) + tintBgr[c] * alpha * amount);
        }
    }
}

void pixelate(BreeferImage& img, float sizeF)
{
    const int size = std::max(1, static_cast<int>(sizeF / 100.0f * static_cast<float>(img.height)));
    if (size == 1) return;
    for (int by = 0; by < img.height; by += size) {
        for (int bx = 0; bx < img.width; bx += size) {
            // Sample the block's centre and fill the block with it.
            const int cx = std::min(bx + size / 2, img.width - 1);
            const int cy = std::min(by + size / 2, img.height - 1);
            const uint8_t* src = img.pixels + static_cast<size_t>(cy) * img.stride + static_cast<size_t>(cx) * 4;
            const uint8_t color[4] = {src[0], src[1], src[2], src[3]};
            const int ye = std::min(by + size, img.height);
            const int xe = std::min(bx + size, img.width);
            for (int y = by; y < ye; ++y) {
                uint8_t* row = img.pixels + static_cast<size_t>(y) * img.stride;
                for (int x = bx; x < xe; ++x) {
                    uint8_t* dst = row + static_cast<size_t>(x) * 4;
                    dst[0] = color[0]; dst[1] = color[1]; dst[2] = color[2]; dst[3] = color[3];
                }
            }
        }
    }
}

} // namespace

extern "C" {

BREEFER_EXPORT int breefer_plugin_count(void) { return static_cast<int>(kEffects.size()); }

BREEFER_EXPORT const BreeferEffectDesc* breefer_plugin_describe(int index)
{
    return (index >= 0 && index < static_cast<int>(kEffects.size())) ? &kEffects[static_cast<size_t>(index)] : nullptr;
}

BREEFER_EXPORT int breefer_plugin_render(int index, BreeferImage* image, const float* params, double /*timeSeconds*/)
{
    if (!image || !image->pixels || !params) return 1;
    try {
        switch (index) {
        case 0: tint(*image, params); return 0;
        case 1: pixelate(*image, params[0]); return 0;
        default: return 1;
        }
    } catch (...) {
        return 2;   // never let an exception cross the C boundary
    }
}

} // extern "C"
