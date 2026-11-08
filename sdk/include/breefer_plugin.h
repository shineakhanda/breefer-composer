/*
 * Breefer Composer native effect plugin API (C ABI, version 1).
 *
 * Build a shared library (.dll / .so / .dylib) that exports the three
 * functions below and drop it into one of:
 *   <install dir>/plugins
 *   Windows: %APPDATA%/Breefer/Composer/plugins
 *   macOS:   ~/Library/Application Support/Breefer/Composer/plugins
 *   Linux:   ~/.local/share/Breefer/Composer/plugins
 *
 * Effects appear in the Effects panel under their category; parameters show in
 * Effect Controls with stopwatches and are keyframeable.
 *
 * Rules:
 *  - breefer_plugin_render may be called from several threads at once
 *    (preview and export): keep it reentrant, no global mutable state.
 *  - Pixels are 8-bit BGRA, premultiplied alpha, top row first.
 *  - Never keep the pixel pointer after render returns.
 *
 * Licence: this header is MIT-licensed so plugins may use any licence.
 */
#ifndef BREEFER_PLUGIN_H
#define BREEFER_PLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#define BREEFER_PLUGIN_API_VERSION 1

#if defined(_WIN32)
#define BREEFER_EXPORT __declspec(dllexport)
#else
#define BREEFER_EXPORT __attribute__((visibility("default")))
#endif

typedef struct BreeferParamDesc {
    const char* id;          /* stable identifier, e.g. "levels" */
    const char* name;        /* label in Effect Controls */
    float minimum;
    float maximum;
    float defaultValue;
} BreeferParamDesc;

typedef struct BreeferEffectDesc {
    int apiVersion;          /* BREEFER_PLUGIN_API_VERSION */
    const char* id;          /* globally unique, reverse-DNS: "com.example.posterize" */
    const char* name;        /* "Posterize" */
    const char* category;    /* "Stylize" */
    int paramCount;
    const BreeferParamDesc* params;
} BreeferEffectDesc;

typedef struct BreeferImage {
    int width;
    int height;
    int stride;              /* bytes per row */
    unsigned char* pixels;   /* BGRA premultiplied, modified in place */
} BreeferImage;

/* Number of effects this library provides. */
BREEFER_EXPORT int breefer_plugin_count(void);

/* Description of effect `index` (0 .. count-1). Must stay valid while loaded. */
BREEFER_EXPORT const BreeferEffectDesc* breefer_plugin_describe(int index);

/* Process `image` in place. `params` holds paramCount values in declaration
 * order, already keyframe-evaluated. Return 0 on success. */
BREEFER_EXPORT int breefer_plugin_render(int index, BreeferImage* image, const float* params, double timeSeconds);

#ifdef __cplusplus
}
#endif

#endif /* BREEFER_PLUGIN_H */
