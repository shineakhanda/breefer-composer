# Plugin Guide

Breefer loads **native effect plugins**: shared libraries (`.dll`, `.so`, `.dylib`) that
export a small C interface. Any language that can produce a C-ABI shared library works —
C, C++, Rust, Zig, Go (cgo), Nim, Odin…

> **Adobe Premiere Pro plugins (`.prm`, `.aex`) cannot be loaded.** See
> [PREMIERE_COMPATIBILITY.md](PREMIERE_COMPATIBILITY.md) for exactly what is and is not
> supported and how to carry your work across.

**Contents**
1. [What a plugin can and cannot do](#1-what-a-plugin-can-and-cannot-do)
2. [The API reference](#2-the-api-reference)
3. [Your first plugin in C](#3-your-first-plugin-in-c)
4. [Building](#4-building)
5. [Installing and testing](#5-installing-and-testing)
6. [Writing plugins in C++](#6-writing-plugins-in-c)
7. [Writing plugins in Rust](#7-writing-plugins-in-rust)
8. [Design rules and performance](#8-design-rules-and-performance)
9. [Porting an Adobe After Effects / Premiere effect](#9-porting-an-adobe-after-effects--premiere-effect)
10. [Distributing a plugin](#10-distributing-a-plugin)
11. [Troubleshooting](#troubleshooting)
12. [Roadmap: API v2, OpenFX and audio plugins](#12-roadmap)

## 1. What a plugin can and cannot do

| | API v1 |
|---|---|
| Kind | **Video effect**: receives one clip's image, modifies it in place |
| Pixels | 8-bit **BGRA, premultiplied alpha**, top row first |
| Parameters | any number of **float** parameters with min / max / default; shown as blue scrubbable values in Effect Controls; **keyframeable** (values arrive already interpolated) |
| Time | seconds since the start of the *clip* |
| Threads | may be called concurrently from preview and export threads |
| Categories | free-text; effects are grouped under that heading in the Effects panel |
| Not yet | transitions, audio effects, generators, multi-input effects, GPU, 16/32-bit float, custom UI, point/color/choice/checkbox parameter types, frame-scale info |

Where it runs in the pipeline: after the built-in Lumetri/Blur/Sharpen/Vignette and before
Crop/Flip; then the clip's Motion/Basic 3D transform, mask and opacity are applied to your
output. Plugins therefore see the clip's **own** picture (not the whole timeline canvas), at
the **current preview resolution** — see [rule 3](#8-design-rules-and-performance).

## 2. The API reference

The whole interface is one header, [`sdk/include/breefer_plugin.h`](../sdk/include/breefer_plugin.h)
(MIT licensed, so plugins can use any licence).

```c
#define BREEFER_PLUGIN_API_VERSION 1

typedef struct BreeferParamDesc {
    const char* id;        // stable identifier, saved in projects: "levels"
    const char* name;      // label in Effect Controls
    float minimum, maximum, defaultValue;
} BreeferParamDesc;

typedef struct BreeferEffectDesc {
    int apiVersion;                 // must be BREEFER_PLUGIN_API_VERSION
    const char* id;                 // globally unique, reverse-DNS: "com.acme.posterize"
    const char* name;               // "Posterize"
    const char* category;           // "Stylize"
    int paramCount;
    const BreeferParamDesc* params; // paramCount entries, stay valid while loaded
} BreeferEffectDesc;

typedef struct BreeferImage {
    int width, height;
    int stride;                     // bytes per row (may exceed width*4)
    unsigned char* pixels;          // BGRA premultiplied, modified in place
} BreeferImage;

int breefer_plugin_count(void);
const BreeferEffectDesc* breefer_plugin_describe(int index);
int breefer_plugin_render(int index, BreeferImage* image, const float* params, double timeSeconds);
```

### `breefer_plugin_count`
How many effects this library provides (one library may provide several).

### `breefer_plugin_describe(index)`
Return the description of effect `index` (0 … count−1), or `NULL`. The returned memory
(and all strings in it) must stay valid until the library unloads — use `static` data.
Effects whose `apiVersion` is not 1 are rejected with a message in *Edit › Plugins…*.

### `breefer_plugin_render(index, image, params, timeSeconds)`
Process the image in place.

- `params` has `paramCount` floats **in the order of your declaration**, already evaluated
  at this moment from the clip's keyframes. Never read past `paramCount`.
- `timeSeconds` is seconds since the start of the **clip** (not the sequence) — use it for
  time-varying effects (grain seed, wipes). It is the same for every pixel of a frame.
- `image->stride` is the byte distance between rows. Always use it; do not assume
  `width * 4`.
- Pixels are **premultiplied**: `B,G,R` are already multiplied by `A/255`. Operations that
  mix colors must respect this (see the Color Tint sample). Opaque video has `A = 255`.
- Return **0** on success. Any other value marks the frame as failed: the host keeps whatever
  is in the buffer and moves on, so don't leave a half-written image if you can fail —
  validate first, write after.
- **Threading**: calls can overlap (preview and export render at the same time). No global
  mutable state, no static scratch buffers; allocate per call or use thread-local storage.
- Never keep `image` or `pixels` after returning. Never throw/panic across the boundary.

### Identity and compatibility

- `id` (effect) and `params[].id` identify things inside saved projects. **Never change them
  once released**, or old projects lose their settings. Display names may change.
- A project that uses a plugin you don't have opens normally; the effect is listed in Effect
  Controls with its saved values but is skipped when rendering.
- API versioning: the host accepts `apiVersion == 1`. Future host versions will continue to
  load v1 plugins unchanged.

## 3. Your first plugin in C

A complete "Invert" plugin with one parameter (amount):

```c
// invert.c
#include "breefer_plugin.h"   // also provides size_t

static const BreeferParamDesc kParams[] = {
    {"amount", "Amount (%)", 0.0f, 100.0f, 100.0f},
};
static const BreeferEffectDesc kEffect = {
    BREEFER_PLUGIN_API_VERSION, "com.example.invert", "Invert Amount", "Color Correction",
    1, kParams,
};

BREEFER_EXPORT int breefer_plugin_count(void) { return 1; }

BREEFER_EXPORT const BreeferEffectDesc* breefer_plugin_describe(int index)
{
    return index == 0 ? &kEffect : 0;
}

BREEFER_EXPORT int breefer_plugin_render(int index, BreeferImage* img, const float* params, double t)
{
    (void)t;
    if (index != 0 || !img || !img->pixels) return 1;
    const int amount = (int)(params[0] * 2.55f);           // 0..255
    for (int y = 0; y < img->height; ++y) {
        unsigned char* p = img->pixels + (size_t)y * img->stride;
        for (int x = 0; x < img->width; ++x, p += 4) {
            const int a = p[3];                             // premultiplied alpha
            for (int c = 0; c < 3; ++c) {
                const int inverted = a - p[c];              // invert within the alpha range
                p[c] = (unsigned char)(p[c] + (inverted - p[c]) * amount / 255);
            }
        }
    }
    return 0;
}
```

Parameters appear in Effect Controls exactly as declared: *Amount (%)* with a blue scrub
value, a stopwatch for animation, and reset buttons — you write no UI code.

## 4. Building

You only need a C or C++ compiler and the header. Hide all symbols except the three exports
(`BREEFER_EXPORT` does this) and link the runtime statically so the plugin has no extra DLL
dependencies.

**Windows (MinGW-w64)**
```powershell
gcc -O2 -shared -I sdk/include invert.c -o invert.dll -static-libgcc
# C++:
g++ -O2 -shared -I sdk/include tint.cpp -o tint.dll -static-libgcc -static-libstdc++
```
**Windows (MSVC)**
```bat
cl /O2 /LD /I sdk\include invert.c /Fe:invert.dll
```
**Linux**
```bash
gcc -O2 -shared -fPIC -fvisibility=hidden -I sdk/include invert.c -o libinvert.so -lm
```
**macOS**
```bash
clang -O2 -dynamiclib -fvisibility=hidden -I sdk/include invert.c -o libinvert.dylib
```

Build for the same CPU architecture as the editor (x86-64 on Windows/Linux, arm64 on Apple
Silicon builds). The C ABI means your compiler does **not** need to match the editor's.

**With CMake** (this is how the bundled samples are built — see the repository
`CMakeLists.txt`):
```cmake
add_library(my_plugin MODULE my_plugin.c)
target_include_directories(my_plugin PRIVATE path/to/sdk/include)
set_target_properties(my_plugin PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden)
```

## 5. Installing and testing

Put the library in any plugin folder:

| Location | Path |
|---|---|
| Next to the app | `<install dir>/plugins` |
| Per user — Windows | `%APPDATA%\Breefer\Breefer Composer\plugins` |
| Per user — macOS | `~/Library/Application Support/Breefer/Breefer Composer/plugins` |
| Per user — Linux | `~/.local/share/Breefer/Breefer Composer/plugins` |
| Anywhere | folders listed in the `BREEFER_PLUGIN_PATH` environment variable (separated by `;` on Windows, `:` elsewhere) |

Then **Edit › Plugins…**: it lists every effect that loaded (with its library), every
error, a **Rescan** button and a button that opens your user plugin folder. After a rescan,
the Effects panel shows your effects under **PLUGINS**; search for yours, select a clip and
double-click.

**Headless test** — render a frame of a project that uses your effect, without the GUI
(great for CI and for checking pixel math):

```bash
breefer --render-frame test.bcproj 0.5 out.png 640
```

A minimal clip entry in `test.bcproj` (see [FILE_FORMATS.md](FILE_FORMATS.md)):

```json
{"id":"c1","mediaId":"m_demo","start":0,"duration":254016000000,"sourceIn":0,"speed":1,
 "effects":[{"id":"e1","type":"plugin:com.example.invert","name":"Invert","enabled":true,
             "params":[{"id":"amount","name":"Amount (%)","value":100,"min":0,"max":100,"keys":[]}]}]}
```

The effect `type` is always `plugin:` + your effect `id`.

## 6. Writing plugins in C++

Satisfy the C ABI with `extern "C"` and keep the rest ordinary C++. The bundled sample
[`sdk/samples/tint/tint.cpp`](../sdk/samples/tint/tint.cpp) provides **Color Tint** and
**Pixelate** and is built and tested with the app. Pattern:

```cpp
#include "breefer_plugin.h"
#include <array>

namespace {
constexpr std::array<BreeferParamDesc, 1> kParams{{{"size", "Block Size (% of height)", 0.2f, 25.0f, 2.5f}}};
constexpr BreeferEffectDesc kEffect{BREEFER_PLUGIN_API_VERSION, "com.example.px", "Pixelate", "Stylize",
                                    1, kParams.data()};
}

extern "C" {
BREEFER_EXPORT int breefer_plugin_count(void) { return 1; }
BREEFER_EXPORT const BreeferEffectDesc* breefer_plugin_describe(int i) { return i == 0 ? &kEffect : nullptr; }
BREEFER_EXPORT int breefer_plugin_render(int i, BreeferImage* img, const float* p, double) {
    try { /* … your code … */ return 0; } catch (...) { return 2; }
}
}
```

Tips: link `libstdc++`/`libgcc` statically on MinGW; don't let exceptions escape; avoid
global constructors that do real work (the library is loaded during start-up scanning).

## 7. Writing plugins in Rust

> This example follows the ABI exactly but is **not compiled in this repository's CI**;
> if you find a problem please open an issue.

`Cargo.toml`:
```toml
[lib]
crate-type = ["cdylib"]
```

`src/lib.rs`:
```rust
use std::os::raw::{c_char, c_int};

#[repr(C)] pub struct ParamDesc { id: *const c_char, name: *const c_char, min: f32, max: f32, default: f32 }
#[repr(C)] pub struct EffectDesc { api_version: c_int, id: *const c_char, name: *const c_char,
                                   category: *const c_char, param_count: c_int, params: *const ParamDesc }
#[repr(C)] pub struct Image { width: c_int, height: c_int, stride: c_int, pixels: *mut u8 }

// Raw pointers are not Sync; these statics are immutable and point at 'static data.
struct Wrapper<T>(T);
unsafe impl<T> Sync for Wrapper<T> {}

static PARAMS: Wrapper<[ParamDesc; 1]> = Wrapper([ParamDesc {
    id: b"gain\0".as_ptr() as _, name: b"Gain (%)\0".as_ptr() as _, min: 0.0, max: 300.0, default: 100.0 }]);
static EFFECT: Wrapper<EffectDesc> = Wrapper(EffectDesc {
    api_version: 1, id: b"com.example.gain\0".as_ptr() as _, name: b"Gain\0".as_ptr() as _,
    category: b"Color Correction\0".as_ptr() as _, param_count: 1, params: PARAMS.0.as_ptr() });

#[no_mangle] pub extern "C" fn breefer_plugin_count() -> c_int { 1 }

#[no_mangle] pub extern "C" fn breefer_plugin_describe(i: c_int) -> *const EffectDesc {
    if i == 0 { &EFFECT.0 } else { std::ptr::null() }
}

#[no_mangle] pub unsafe extern "C" fn breefer_plugin_render(
    i: c_int, img: *mut Image, params: *const f32, _t: f64) -> c_int {
    if i != 0 || img.is_null() || params.is_null() { return 1; }
    let result = std::panic::catch_unwind(|| {
        let img = &*img;
        let gain = *params / 100.0;
        for y in 0..img.height as usize {
            let row = std::slice::from_raw_parts_mut(img.pixels.add(y * img.stride as usize), img.width as usize * 4);
            for px in row.chunks_exact_mut(4) {
                for c in 0..3 { px[c] = (px[c] as f32 * gain).min(px[3] as f32) as u8; } // stay ≤ alpha
            }
        }
    });
    if result.is_ok() { 0 } else { 2 }
}
```
Build with `cargo build --release` and copy `target/release/<name>.dll|.so|.dylib` to a plugin
folder. `catch_unwind` keeps panics from crossing the FFI boundary.

## 8. Design rules and performance

1. **Be reentrant.** No mutable globals or shared scratch memory. Allocate inside the call,
   or use thread-local storage.
2. **Respect `stride`.** Rows may be padded.
3. **Express sizes relative to the frame, not in pixels.** The editor renders previews at a
   reduced resolution (the *Playback Resolution* menu: 1/2, 1/4 …) and exports at full
   size. A blur radius or block size in pixels would look different in preview and export.
   Use "% of width/height" parameters (as the samples do) and derive pixels from
   `image->width/height` in every call. *(API v2 will also pass the preview scale.)*
4. **Mind premultiplied alpha.** For effects that add color, scale it by the pixel's alpha;
   for effects that move pixels, copy all four channels together.
5. **Never write outside `[0, width) × [0, height)`.** Clamp neighbor lookups at the edges.
6. **Target the slowest machine.** The audience includes 2nd–4th gen dual-core CPUs.
   - A 1080p frame is 2 million pixels: per-pixel `pow`, `exp` or `sin` in the inner loop is
     expensive; precompute lookup tables per call (256-entry tables for per-channel curves).
   - Process row by row with simple integer math; let the compiler auto-vectorize
     (`-O2`; do **not** require AVX — compile for baseline x86-64 or provide a runtime
     dispatch).
   - Early-out when the parameters make the effect a no-op (amount 0).
   - Don't spawn threads of your own; the host already renders on worker threads.
7. **Be deterministic for a given `(params, timeSeconds, image)`** so previews and exports
   match and frames can be cached. For noise, seed from `timeSeconds`.
8. **Fail soft.** Return non-zero instead of crashing; the host will skip your effect for
   that frame.

## 9. Porting an Adobe After Effects / Premiere effect

If you *own the source* of an effect built with the Adobe After Effects/Premiere SDK, its
pixel code can usually be moved over, but the host glue must be rewritten:

| Adobe SDK concept | Breefer v1 |
|---|---|
| `PF_ParamDef` float/slider/angle params | `BreeferParamDesc` float with min/max/default (angles in degrees as floats) |
| Checkbox / popup / color / point params | not available — encode as a 0/1 or index slider, or split into several floats (e.g. R, G, B) |
| `PF_Cmd_RENDER` / `SmartRender` | `breefer_plugin_render` |
| 8-bit ARGB | 8-bit **BGRA premultiplied** (note the channel order) |
| 16/32-bit float bit depths | not available |
| `PF_Cmd_GLOBAL_SETUP` / param setup | `breefer_plugin_describe` |
| Layer params / checking out other layers | not available |
| Custom UI, GPU (CUDA/OpenCL/Metal) paths | not available |
| `in_data->current_time` | `timeSeconds` (clip-relative) |

For plugins you do **not** own, a vendor must provide a Breefer (or future OpenFX) build;
there is no way to load their Adobe binaries. For a finished look you can render in
Premiere to a high-quality intermediate and import that.

## 10. Distributing a plugin

- Ship one library per platform/architecture (`windows-x64`, `linux-x64`, `macos-arm64`).
- Use a stable reverse-DNS **effect id** and **param ids** and keep them forever.
- Include a licence. The SDK header is MIT; your plugin can be proprietary or open.
- Mention the minimum editor version you tested with.
- Test: open *Edit › Plugins…*, apply, keyframe a parameter, scrub, save and reopen the
  project, export a short range, and run `--render-frame` to confirm identical pixels.

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| Plugin missing from the Effects panel | Not in a plugin folder, wrong CPU architecture, or load error — see *Edit › Plugins…* (errors are listed) and press **Rescan** |
| "not a Breefer plugin (missing exports)" | The three functions aren't exported: use `BREEFER_EXPORT`, `extern "C"` in C++, `#[no_mangle]` in Rust; check with `nm -g lib.so` / `dumpbin /exports x.dll` |
| "The specified module could not be found" (Windows) | Your DLL depends on another DLL (MinGW runtime, MSVC runtime). Link statically (`-static-libgcc -static-libstdc++`, `/MT`) |
| "unsupported API version" | `apiVersion` isn't 1 |
| Effect looks different in preview and export | You used pixel units; make sizes proportional to the frame |
| Colors wrong / halos on transparent areas | You treated pixels as straight alpha; they are premultiplied |
| Crash when scrubbing | Out-of-range access; use `stride`, clamp at edges; build with `-fsanitize=address` and run `--render-frame` |
| Parameter changes not applied | The effect declares fewer params than the project stores; keep param order and ids stable |

## 12. Roadmap

Planned for API v2 (v1 plugins keep working): preview scale factor, more parameter types
(bool, color, choice, point), transitions and generators, a "needs alpha" hint and optional
16-bit float pixels. An **OpenFX** bridge (so OFX effect plugins work unchanged) and **VST3**
audio-effect hosting are candidates; contributions are welcome — see
[CONTRIBUTING.md](../CONTRIBUTING.md).
