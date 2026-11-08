# Adobe Premiere Pro compatibility

**Short answer on plugins:** Breefer Composer **cannot load Adobe Premiere Pro plugins**
(`.prm`, `.aex`), Adobe audio plugins, or Adobe panels. It *can* exchange **projects and
timelines** with Premiere, export **LUTs** Premiere can read, and run its own **open plugin
format** (and React/Remotion graphics). The tables below are exact.

Legend: ✅ supported · 🟡 partial / best effort · ❌ not supported · 🛠 planned/possible

---

## 1. Premiere Pro plugins and extensions

| Type | File | Supported? | Why / what to do |
|---|---|---|---|
| Premiere video **effects** | `.prm` (Premiere SDK) | ❌ | Compiled against Adobe's proprietary host interface; the plugin calls back into Premiere's internals. Only Adobe's applications implement that host. |
| After Effects effects used in Premiere | `.aex` (AE SDK) | ❌ | Same reason. Most "Premiere compatible" effect packs (Red Giant/Maxon, Sapphire, Beauty Box, Twixtor…) are `.aex`/`.prm`. |
| Premiere **transitions** | `.prm` | ❌ | Same. Breefer has its own transitions (dissolve, dip to black/white, wipe, push, iris). |
| **Audio effects** | VST2 / VST3 / AU / AAX | ❌ | Breefer has no audio-effect plugin API yet (🛠 VST3 hosting is a candidate). Built in: clip gain, Volume keyframes, track fader/pan, crossfades. |
| Format importers/exporters | `.prm` (e.g. RED, BRAW) | ❌ | Media is decoded by FFmpeg only. Transcode camera-raw formats first (e.g. to ProRes/DNxHR) or use proxies from the camera software. |
| Panels / extensions | CEP / UXP (`.zxp`), e.g. Motion Bro, Frame.io panel | ❌ | Tied to Adobe's extension runtime. |
| Scripts | ExtendScript `.jsx` | ❌ | Breefer is scripted through its [plain-JSON project files](FILE_FORMATS.md) and the headless `--render-frame` mode. |
| **OpenFX** effects | `.ofx` | ❌ 🛠 | Not loaded today. Premiere itself does not use OFX, but many vendors also ship OFX builds for DaVinci Resolve/Nuke/Natron; an OFX bridge is a possible future addition. |
| Motion Graphics Templates | `.mogrt` | ❌ | Replaced by **Remotion/React compositions** — see [REMOTION.md](REMOTION.md). |
| Presets | `.prfpset`, effect presets | ❌ | — |
| **Breefer native plugins** | `.dll` / `.so` / `.dylib` | ✅ | The supported plugin type. See the [Plugin Guide](PLUGINS.md). |
| **React / Remotion plugins** | Remotion project / bundle | ✅ (needs Node.js) | See [REMOTION.md](REMOTION.md). |

**Why not emulate the Adobe host?** The Premiere/After Effects SDKs are Adobe-owned, the host
side is not published, and re-implementing it would both be legally risky and technically
open-ended (hundreds of callback suites). No third-party editor loads these plugins.

### How to keep using a favorite Premiere plugin

1. **Bake it in Premiere.** Apply the effect there, export that section to ProRes/DNxHR (or
   use *Render and Replace*), and import the result into Breefer. The look is preserved
   exactly; it just isn't editable afterward.
2. **Use an equivalent.** Check section 4 for built-ins, or install a Breefer plugin that
   offers the same look (the sample plugins add Posterize, RGB Split, Film Grain, Color
   Tint, Pixelate).
3. **Ask the vendor** for a Breefer/OpenFX build, or write one yourself if you own the
   source — [Plugin Guide § Porting](PLUGINS.md#9-porting-an-adobe-after-effects--premiere-effect).

---

## 2. Projects and timelines

| Format | Import | Export | Notes |
|---|---|---|---|
| Premiere Pro project `.prproj` | 🟡 | ❌ | See section 3 |
| **Final Cut Pro 7 XML** (`.xml`, xmeml v4) | ✅ | ✅ | **Best route to and from Premiere**: *File › Export › Final Cut Pro XML* in Premiere; *File › Import* the result there. Carries tracks, clip positions, in/out points, names, enabled state, link groups, cross-dissolves, file paths, frame rate/size. |
| CMX 3600 EDL | ✅ | ✅ | Cuts and dissolves only; assumes 25 fps unless the EDL says drop-frame |
| OpenTimelineIO `.otio` | ✅ | ✅ | Clips, gaps, speed (LinearTimeWarp), transitions on import; export writes explicit gaps |
| FCPXML (Final Cut Pro X) | ❌ | ✅ | Import isn't implemented |
| AAF | ❌ | ❌ | Needs a separate library; not implemented |

What does **not** survive any interchange (because the formats can't carry it or Breefer
doesn't map it yet): effect parameters and keyframes (except cross-dissolves), Lumetri looks,
titles/graphics, nests, adjustment layers, audio levels and pan, markers, speed ramps.
Re-create these in the destination.

## 3. Opening a `.prproj`

*File › Import Premiere Pro Project / Timeline…* (or drag the file onto the timeline).

`.prproj` is an **undocumented, version-dependent**, gzip-compressed XML file. Breefer
decompresses it with its own decoder and follows the object graph
`Sequence → track groups → tracks → track items → sub-clips → clips → media`.

**Verification status, honestly:** the importer is modelled on the structure commonly
documented by the community for Premiere Pro CC-era projects, and is verified only against a
**generated test file** of that structure (`tests/data/sample.prproj`) — it has *not* been
tested against real files from every Premiere version. Treat it as best effort; **please try
it on your real projects and report failures** with a (small) sample if you can.

| Imported | Not imported |
|---|---|
| Every sequence (name, frame size, frame rate) | Effects, keyframes, Lumetri, masks |
| Video and audio tracks, in order | Transitions (counted and reported) |
| Clip positions, durations, in-points | Titles/graphics (Legacy Titles, Essential Graphics), nested sequences, adjustment layers (reported as skipped) |
| Playback speed and reverse | Audio levels, pan, audio effects |
| Linked video/audio pairs | Markers, bins/folders, labels, proxies |
| Media file paths (relinkable if moved) | Multicam, captions, Dynamic Link comps |

If a project fails to import, or imports empty: in Premiere use *File › Export › Final Cut
Pro XML* and import the XML — it is far more complete and stable.

Exporting `.prproj` is not supported: Adobe's format is undocumented and Premiere refuses
files it did not write. Premiere opens Breefer's FCP 7 XML export.

## 4. Premiere features and their Breefer equivalents

### Effects and transitions

| Premiere | Breefer |
|---|---|
| Motion (Position, Scale, Rotation) | ✅ Motion — uniform scale; no anchor-point or anti-flicker controls |
| Opacity | ✅ Opacity — **no blend modes** |
| Lumetri Color — Basic Correction | ✅ exposure, contrast, highlights, shadows, whites, blacks, temperature, tint, saturation, vibrance |
| Lumetri — Creative, Curves, Color Wheels & Match, HSL Secondary, Vignette tab | ❌ (a separate **Vignette** effect exists); no LUT *import*; **.cube export ✅** |
| Lumetri Scopes | ✅ Waveform (luma), RGB Parade, Vectorscope |
| Crop | ✅ percent only |
| Horizontal / Vertical Flip | ✅ |
| Gaussian Blur | 🟡 separable box-blur approximation |
| Sharpen (unsharp) | ✅ |
| Basic 3D | ✅ swivel / tilt / distance |
| Opacity masks (ellipse, rectangle) | 🟡 one shape mask per clip (feather, roundness, opacity); no Bezier/pen masks, no mask tracking |
| Cross Dissolve, Dip to Black, Dip to White | ✅ |
| Wipe, Push, Iris Round | ✅ (one variant each) |
| Film Dissolve, Morph Cut, Slide, Zoom, Whip, 3D Motion… | ❌ |
| Warp Stabilizer, Ultra Key, Optical Flow, Time Remapping / speed ramps, Auto Reframe, noise reduction | ❌ (constant speed/reverse ✅) |
| Constant Power audio transition | ✅ (Constant Gain, Exponential Fade ❌) |
| Volume, Channel Volume | ✅ Volume (dB, keyframeable); clip gain ✅ |
| Parametric EQ, Compressor, DeNoise, Essential Sound, Dynamic Link to Audition | ❌ |
| Text tool / Essential Graphics | 🟡 Title dialog (font, fill, stroke, shadow, box, alignment); no responsive design or templates |
| Adjustment Layer | ✅ |
| Nested sequences | ✅ (double-click to open) |
| Multi-camera editing | ❌ |
| Proxies (ingest/attach) | ✅ automatic 540p MJPEG proxies + toggle |

### Editing workflow (what matches Premiere)

| Premiere | Breefer |
|---|---|
| Source and Program monitors, Insert `,` / Overwrite `.`, Lift `;` / Extract `'` | ✅ |
| Source patching and track targeting | ✅ (V1/A1 boxes and track-label buttons) |
| Sync lock, track lock, solo/mute, linked selection, snapping | ✅ |
| Selection, Track Select Forward, Ripple, Rolling, Rate Stretch, Razor, Slip, Slide, Hand, Zoom tools | ✅ (Track Select **Backward**, Pen ❌) |
| Q / W ripple trim, Match Frame, Add Edit (all tracks), J/K/L | ✅ |
| Numeric nudge (`+12` Enter), Alt+arrows | ✅ |
| Markers (sequence), In/Out | ✅ — clip markers ❌; marker colors/comments are stored but have no UI yet |
| Project panel bins, icon/list view, search | ✅ |
| Effect Controls: stopwatch, keyframe navigation, hot-text scrubbing | ✅ — no Bezier/graph editor |
| Workspaces | ✅ Editing, Assembly, Color, Effects, Audio |
| History panel | ✅ |
| Essential Sound, Captions, Speech-to-Text, Text-Based Editing | ❌ |
| Team Projects, Productions, Dynamic Link, Creative Cloud Libraries | ❌ |

### Export

| Premiere / Media Encoder | Breefer |
|---|---|
| H.264, HEVC | ✅ (hardware encoders first, software fallback) |
| ProRes 422, DNxHR | ✅ |
| WebM VP9, AV1, WAV, AAC | ✅ |
| Presets: YouTube, vertical/square social | ✅ |
| Queue | ✅ (built-in Render Queue; Media Encoder not needed) |
| Render at Maximum Depth (32-bit float), HDR, Dolby Vision, closed captions burn-in | ❌ 8-bit Rec.709 pipeline |

## 5. Practical migration checklist

1. In Premiere: finish picture lock, *File › Export › Final Cut Pro XML*. Consolidate or
   keep media paths unchanged.
2. In Breefer: *File › Import Premiere Pro Project / Timeline…* → choose the XML. If media is
   offline, right-click it in the Project panel → *Link Media…*.
3. Re-apply effects that don't transfer; bake plugin-heavy shots in Premiere to ProRes first.
4. Color: Breefer can **export** `.cube` LUTs but cannot **import** them yet (🛠 planned), so
   grades made in Premiere's Lumetri must be re-created with Breefer's Lumetri Color, or baked
   into the footage before import.
5. To return to Premiere: *File › Export › Final Cut Pro 7 XML…* in Breefer, then
   *File › Import* in Premiere.
