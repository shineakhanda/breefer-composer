# File formats and scripting

## The `.bcproj` project file

A project is a single UTF-8 **JSON** file, written atomically (a crash can't leave half a
file). Current version: **2**. Older files load and are upgraded in memory.

All times are integer **ticks**: 254 016 000 000 per second (see
[HOW_IT_WORKS § Time](HOW_IT_WORKS.md#4-time)). Examples: 1 s = `254016000000`;
one frame at 25 fps = `10160640000`; one frame at 29.97 fps = `8475667200`.

```jsonc
{
  "format": "breefer-project",
  "version": 2,
  "id": "proj_...", "name": "My Film",
  "bins": ["Interviews", "Interviews/Day 1"],        // Project-panel folders
  "media": [ /* MediaRef */ ],
  "sequences": [ /* Sequence */ ]
}
```

### MediaRef
```jsonc
{ "id": "media_1", "path": "C:/footage/a.mp4", "name": "a.mp4",
  "duration": 2540160000000, "width": 3840, "height": 2160,
  "rateNum": 25, "rateDen": 1, "sampleRate": 48000, "channels": 2,
  "hasVideo": true, "hasAudio": true, "codec": "h264",
  "proxyPath": "",                                   // filled once a proxy exists
  "bin": "Interviews/Day 1",                         // optional
  "remotionEntry": "", "remotionComposition": "", "remotionProps": "" }  // optional, Remotion renders
```
Still images have `duration: 0`. `path` may be absolute; if the file is missing the media is
shown offline and can be relinked.

### Sequence
```jsonc
{ "id": "seq_1", "name": "Sequence 01", "width": 1920, "height": 1080,
  "rateNum": 25, "rateDen": 1,
  "patchVideo": 1, "patchAudio": 1,                  // source-patch lanes (0 = off)
  "in": -1, "out": -1,                               // sequence In/Out, -1 = unset
  "markers": [ {"id":"mk_1","time":0,"duration":0,"name":"","comment":"","color":3} ],
  "tracks": [ /* Track, top-first: V3, V2, V1, A1, A2, A3 */ ] }
```

### Track
```jsonc
{ "id": "vt_1", "name": "V1", "kind": "video",       // "video" | "audio"
  "muted": false, "solo": false, "locked": false,
  "syncLock": true, "targeted": true,
  "volumeDb": 0.0, "pan": 0.0,                       // audio tracks
  "height": 0,                                       // 0 = default
  "clips": [ /* Clip, sorted by start, non-overlapping */ ] }
```
**Invariant:** clips on one track are sorted by `start` and must not overlap; the loader
repairs violations, and the editor rejects edits that would create them.

### Clip
```jsonc
{ "id": "clip_1", "mediaId": "media_1",
  "start": 0, "duration": 254016000000, "sourceIn": 0,
  "speed": 1.0, "reverse": false, "enabled": true, "muted": false,
  "label": "", "labelColor": -1, "linkId": "link_1", "gainDb": 0.0,
  "generator": 0,                                    // see below
  "in":  {"type": 1, "duration": 6350400000},        // optional head transition
  "out": {"type": 0, "duration": 0},                 // optional tail transition
  "effects": [ /* EffectInstance */ ] }
```
`generator`: `0` media, `1` title (`"title": {...}`), `2` color matte (`"matte": {"argb":"#ff204080"}`),
`3` nested sequence (`"nested": "<sequence id>"`), `4` adjustment layer,
`5` 3D model (`"model": "<path to .obj>"`). Generators have an empty `mediaId`.
Transition `type`: `1` cross dissolve, `2` dip to black, `3` dip to white, `4` wipe left,
`5` wipe right, `6` push, `7` iris. A head transition on a clip that touches its predecessor is
centered on the cut.

### EffectInstance, Param, Keyframe
```jsonc
{ "id": "fx_1", "type": "motion", "name": "Motion", "enabled": true,
  "params": [ { "id": "scale", "name": "Scale", "value": 100, "min": 1, "max": 500,
                "keys": [ {"time": 0, "value": 100}, {"time": 127008000000, "value": 150, "interp": 2} ] } ] }
```
`keys[].time` is relative to the **clip start**. `interp` (optional): `0` linear (default),
`1` hold, `2` ease.

Built-in effect `type`s and their parameter ids:

| type | parameter ids |
|---|---|
| `motion` | `position.x`, `position.y` (px of sequence), `scale` (%), `rotation` (°) |
| `opacity` | `amount` (0–100) |
| `lumetri` | `exposure`, `contrast`, `highlights`, `shadows`, `whites`, `blacks`, `temperature`, `tint`, `saturation` (100 = neutral), `vibrance` |
| `blur` | `radius` |
| `sharpen` | `amount` |
| `vignette` | `amount`, `size` |
| `crop` | `left`, `top`, `right`, `bottom` (%) |
| `flip` | `horizontal`, `vertical` (0/1) |
| `mask` | `position.x`, `position.y`, `width`, `height`, `feather`, `roundness`, `opacity` |
| `basic3d` | `swivel`, `tilt`, `distance` |
| `model3d` | `rotateX/Y/Z`, `scale`, `distance`, `fov`, `lightAzimuth`, `lightElevation`, `ambient`, `red`, `green`, `blue`, `wireframe` |
| `volume` | `level` (dB) |
| `plugin:<effect id>` | the parameter ids the plugin declares |

### Title (`generator: 1`)
`text`, `font`, `size` (pixels at the sequence's frame size), `bold`, `italic`, `color`, `align`
(0 left, 1 center, 2 right), `x`, `y` (0–1 anchor), `background`, `backgroundColor`,
`shadow`, `outline`, `outlineColor` — colors are `{"argb": "#aarrggbb"}`.

## Scripting and automation

There is no scripting runtime; automation is done on the file and on the command line:

```bash
# generate or edit a project with any language, then render a frame headlessly:
breefer --render-frame project.bcproj <seconds> out.png [width]
```
The repository's `tests/data/make_features.py` shows how to generate a project with nests,
3D, plugins and an adjustment layer. Render frames from the **first sequence** of the file.

## Other formats Breefer reads and writes

| Format | Where | Notes |
|---|---|---|
| FCP 7 XML (xmeml v4) | File › Import / Export | see [PREMIERE_COMPATIBILITY](PREMIERE_COMPATIBILITY.md) |
| CMX 3600 EDL | File › Import / Export | |
| OpenTimelineIO | File › Import / Export | |
| FCPXML 1.9 | Export only | |
| Adobe `.prproj` | Import only | |
| `.cube` 3D LUT (33³) | Lumetri › Export .cube | |
| Media | anything FFmpeg decodes | |
| Rendered files | mp4, mov, mkv, webm, wav, m4a; PNG/JPEG/TIFF frames | |
