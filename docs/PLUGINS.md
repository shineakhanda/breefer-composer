# Native effect plugins

Breefer loads effect plugins written against a small, stable C ABI:
[`sdk/include/breefer_plugin.h`](../sdk/include/breefer_plugin.h). Any language that can
export C functions from a shared library works (C, C++, Rust, Zig, Go with cgo, …).

## Where plugins are loaded from

| Location | Path |
|---|---|
| Next to the app | `<install dir>/plugins` |
| Per user (Windows) | `%APPDATA%/Breefer/Composer/plugins` |
| Per user (macOS) | `~/Library/Application Support/Breefer/Composer/plugins` |
| Per user (Linux) | `~/.local/share/Breefer/Composer/plugins` |
| Extra | folders in the `BREEFER_PLUGIN_PATH` environment variable |

*Edit › Plugins…* lists what loaded, shows load errors, rescans and opens the user folder.

## The API in one minute

```c
#include "breefer_plugin.h"

static const BreeferParamDesc params[] = {{"levels", "Levels", 2, 32, 6}};
static const BreeferEffectDesc effect = {
    BREEFER_PLUGIN_API_VERSION, "com.example.posterize", "Posterize", "Stylize", 1, params};

BREEFER_EXPORT int breefer_plugin_count(void) { return 1; }
BREEFER_EXPORT const BreeferEffectDesc* breefer_plugin_describe(int i) { return i == 0 ? &effect : 0; }
BREEFER_EXPORT int breefer_plugin_render(int i, BreeferImage* img, const float* p, double t)
{
    /* img->pixels: BGRA premultiplied, img->stride bytes per row; modify in place */
    return 0;
}
```

- Parameters appear in Effect Controls with stopwatches; values arrive keyframe-evaluated.
- `render` may run on several threads at once (preview and export) — keep it reentrant.
- Effects are stored in projects as `plugin:<id>`; projects open without the plugin
  installed (the effect is simply skipped).

## Sample

[`sdk/samples/stylize/stylize.c`](../sdk/samples/stylize/stylize.c) provides Posterize,
RGB Split (glitch) and Film Grain, and is built with the app (`build/plugins/`).

## Premiere / After Effects plugins

Adobe `.prm`/`.aex` plugins depend on Adobe's proprietary host SDK and cannot be loaded by
any other application. Vendors who ship OpenFX versions (Boris FX, RE:Vision, NewBlue…)
could be supported in future through an OFX bridge; contributions welcome.
