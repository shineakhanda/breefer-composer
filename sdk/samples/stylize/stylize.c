/* Sample Breefer plugin: Posterize, Glitch RGB split and Film Grain.
 * Build: part of the main CMake build (target breefer_sample_plugins). */
#include "breefer_plugin.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const BreeferParamDesc kPosterizeParams[] = {
    {"levels", "Levels", 2.0f, 32.0f, 6.0f},
};
static const BreeferParamDesc kSplitParams[] = {
    {"offset", "Offset (px)", 0.0f, 60.0f, 8.0f},
    {"angle", "Angle", -180.0f, 180.0f, 0.0f},
};
static const BreeferParamDesc kGrainParams[] = {
    {"amount", "Amount", 0.0f, 100.0f, 20.0f},
};

static const BreeferEffectDesc kEffects[] = {
    {BREEFER_PLUGIN_API_VERSION, "org.breefer.sample.posterize", "Posterize", "Stylize", 1, kPosterizeParams},
    {BREEFER_PLUGIN_API_VERSION, "org.breefer.sample.rgbsplit", "RGB Split (Glitch)", "Stylize", 2, kSplitParams},
    {BREEFER_PLUGIN_API_VERSION, "org.breefer.sample.grain", "Film Grain", "Noise & Grain", 1, kGrainParams},
};

BREEFER_EXPORT int breefer_plugin_count(void) { return 3; }

BREEFER_EXPORT const BreeferEffectDesc* breefer_plugin_describe(int index)
{
    return (index >= 0 && index < 3) ? &kEffects[index] : 0;
}

static unsigned char clamp8(int v) { return (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void posterize(BreeferImage* img, float levelsF)
{
    const int levels = levelsF < 2 ? 2 : (int)levelsF;
    for (int y = 0; y < img->height; ++y) {
        unsigned char* p = img->pixels + (size_t)y * img->stride;
        for (int x = 0; x < img->width; ++x, p += 4)
            for (int c = 0; c < 3; ++c) {
                const int q = (p[c] * (levels - 1) + 127) / 255;
                p[c] = clamp8(q * 255 / (levels - 1));
            }
    }
}

static void rgbSplit(BreeferImage* img, float offset, float angle)
{
    const size_t bytes = (size_t)img->stride * img->height;
    unsigned char* copy = (unsigned char*)malloc(bytes);
    if (!copy) return;
    memcpy(copy, img->pixels, bytes);
    const double a = angle * 3.14159265358979 / 180.0;
    const int dx = (int)lround(cos(a) * offset), dy = (int)lround(sin(a) * offset);
    for (int y = 0; y < img->height; ++y) {
        unsigned char* p = img->pixels + (size_t)y * img->stride;
        for (int x = 0; x < img->width; ++x, p += 4) {
            int rx = x + dx, ry = y + dy, bx = x - dx, by = y - dy;
            if (rx < 0) rx = 0; if (rx >= img->width) rx = img->width - 1;
            if (ry < 0) ry = 0; if (ry >= img->height) ry = img->height - 1;
            if (bx < 0) bx = 0; if (bx >= img->width) bx = img->width - 1;
            if (by < 0) by = 0; if (by >= img->height) by = img->height - 1;
            p[2] = copy[(size_t)ry * img->stride + rx * 4 + 2];   /* red from +offset  */
            p[0] = copy[(size_t)by * img->stride + bx * 4 + 0];   /* blue from -offset */
        }
    }
    free(copy);
}

static void grain(BreeferImage* img, float amount, double t)
{
    uint32_t seed = (uint32_t)(t * 1000.0) * 2654435761u + 1u;
    const int strength = (int)(amount * 0.6f);
    for (int y = 0; y < img->height; ++y) {
        unsigned char* p = img->pixels + (size_t)y * img->stride;
        for (int x = 0; x < img->width; ++x, p += 4) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;   /* xorshift, reentrant */
            const int n = (int)(seed % (2 * strength + 1)) - strength;
            for (int c = 0; c < 3; ++c) p[c] = clamp8(p[c] + n);
        }
    }
}

BREEFER_EXPORT int breefer_plugin_render(int index, BreeferImage* image, const float* params, double timeSeconds)
{
    if (!image || !image->pixels) return 1;
    switch (index) {
    case 0: posterize(image, params[0]); return 0;
    case 1: rgbSplit(image, params[0], params[1]); return 0;
    case 2: grain(image, params[0], timeSeconds); return 0;
    default: return 1;
    }
}
