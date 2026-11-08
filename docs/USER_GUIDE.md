# User Guide

This guide covers every part of Breefer Composer. For key bindings see
[SHORTCUTS.md](SHORTCUTS.md); for what happens under the hood see
[HOW_IT_WORKS.md](HOW_IT_WORKS.md).

**Contents**
1. [Installing and starting](#1-installing-and-starting)
2. [The interface](#2-the-interface)
3. [Projects and sequences](#3-projects-and-sequences)
4. [Importing media and working with proxies](#4-importing-media-and-working-with-proxies)
5. [Editing](#5-editing)
6. [Transitions](#6-transitions)
7. [Effects and keyframes](#7-effects-and-keyframes)
8. [Titles, mattes, adjustment layers and nests](#8-titles-mattes-adjustment-layers-and-nests)
9. [Color: Lumetri and scopes](#9-color-lumetri-and-scopes)
10. [Audio](#10-audio)
11. [3D](#11-3d)
12. [React / Remotion and plugins](#12-react--remotion-and-plugins)
13. [Exporting](#13-exporting)
14. [Moving projects to and from other editors](#14-moving-projects-to-and-from-other-editors)
15. [Performance and low-end hardware](#15-performance-and-low-end-hardware)
16. [Preferences, autosave and where files live](#16-preferences-autosave-and-where-files-live)
17. [Troubleshooting](#17-troubleshooting)

---

## 1. Installing and starting

**Windows** — run the installer, or unzip the ZIP and start `breefer.exe`.
**Linux** — make the AppImage executable (`chmod +x Breefer_Composer-*.AppImage`) and run it.
**macOS** — open the DMG and drag *Breefer Composer* to Applications (Apple Silicon).
**From source** — see the [README](../README.md#building-from-source).

On launch the Home dialog lets you create a project or open a recent one. Passing a project
path on the command line opens it directly: `breefer my-film.bcproj`.

System requirements: any x86-64 CPU with SSE2 (2nd-generation Core i3/i5 or newer, any AMD
64-bit CPU) or Apple Silicon; 4 GB RAM minimum, 8 GB recommended; OpenGL is **not** required
(the interface is software-composited Qt Widgets). A screen of 1366×768 is enough.

## 2. The interface

The Editing workspace mirrors Premiere Pro:

```
┌─ header: Home · status ·········· Import | Edit | Export ··· proxies · quick export ─┐
├───────────────────────────────────────┬──────────────────────────────────────────────┤
│ Source │ Effect Controls │ Lumetri     │ Program monitor                              │
│ Color │ Audio Track Mixer │ Scopes     │                                              │
├───────────────────────┬──┬─────────────┴───────────────────────────────────────┬─────┤
│ Project │ Effects │   │T │ Timeline (one tab per sequence)                      │ Aud │
│ History │ Render Queue  │o │                                                      │ io  │
│                       │ls│                                                      │ me  │
└───────────────────────┴──┴──────────────────────────────────────────────────────┴─ter ┘
```

- **Every panel is a dock.** Drag a panel by its title bar (or the thin strip above Tools, the
  Timeline and the meters) to move it: drop it on another panel to tab it there, on an edge to split
  beside it, or outside the window to float it (also on a second monitor). Drag the dark gutters to
  resize. Double-click a floating panel's title to put it back. The panel menu (three lines, top right
  of each panel) has *Undock Panel*, *Close Panel* and *Maximize Frame*; closed panels come back from the
  *Window* menu. The panel you last clicked gets a blue outline.
- **Workspaces** (*Window › Workspaces*, `Alt+Shift+1…5`): Editing, Assembly, Color, Effects, Audio.
  *Save Current Layout as Workspace…* stores your own arrangement under a name; *Lock Panel Layout* stops
  accidental drags. Your layout is remembered between sessions; if a saved layout is ever damaged the
  default one is restored automatically.
- **Maximize** the panel under the cursor with `` ` `` (backtick) or by double-clicking its
  tab; press again to restore.
- **Workspaces** (*Window › Workspaces*, `Alt+Shift+1…5`): Editing, Assembly (big Project
  panel), Color (Lumetri + Scopes), Effects, Audio (mixer). *Reset Layout* restores sizes.
- **Window menu** brings any panel to the front.
- **Header bar**: the home button opens recent projects; *Import* opens the import dialog,
  *Export* opens Export Settings; the proxy icon toggles proxy playback; the arrow icon is
  Quick Export.

### Source and Program monitors

Both have a timecode row (current time left, duration right), a **zoom** menu (Fit … 200%), a
**playback resolution** menu (Full, 1/2, 1/4, 1/8, 1/16), a settings menu (safe margins, loop),
a mini timeline with In/Out and markers, and a button row: add marker, mark in/out, go to
in/out, step back, play/stop, step forward, then **Insert/Overwrite** (Source) or
**Lift/Extract** (Program), and export frame.

Lower the playback resolution if playback stutters; it only affects the preview, never export.

### Timeline

- **Track headers**, left to right: *source patch* box (blue = the track that receives
  Source-monitor edits), **lock**, **track target** (V1/A1 label; blue = targeted by
  keyboard commands such as Add Edit and Go to Edit), **sync lock** (chain: ripple edits
  elsewhere keep this track in sync), **eye** (video: output on/off) or **M** / **S** (audio
  mute / solo). Right-click a header to rename, add or delete tracks, or change height.
- **Clip colors** follow Premiere's label colors (video Iris, audio Forest, graphics Lavender,
  mattes Mango). Right-click › *Label* to change; the `fx` badge turns yellow when a clip
  has non-default effects.
- **Header buttons above the track list**: snap (magnet), linked selection, add marker, and a
  display-settings menu (thumbnails, waveforms, minimize/expand tracks).
- **Ruler**: click or drag to scrub. In/Out shows as a light range. Double-click a marker
  to rename it.

## 3. Projects and sequences

A **project** (`.bcproj`) holds your media list, bins and sequences. A **sequence** is a
timeline with a frame size, frame rate and tracks (V3/V2/V1 above A1/A2/A3 by default).

- *File › New Project…* (`Ctrl+Alt+N`), *Open* (`Ctrl+O`), *Save* (`Ctrl+S`).
- *File › New Sequence…* (`Ctrl+N`) offers presets including 1080p/4K at 23.976–60 fps,
  vertical 9:16, square 1:1 and portrait 4:5. *Sequence › Sequence Settings…* changes the
  active one.
- Each sequence is a tab above the timeline; double-click a sequence in the Project panel to
  open it. Sequences can be **nested** into other sequences (section 8).
- The first clip you place in an empty sequence can offer to **match the sequence to the
  clip's size and frame rate** (*Clip Mismatch Warning*; "Always do this" is remembered, and
  configurable in Preferences).
- **Timebase**: positions are exact integers, so 23.976, 29.97 and 59.94 fps never drift.
  Timecode is non-drop-frame.

## 4. Importing media and working with proxies

**Import** with `Ctrl+I`, *Import Folder…*, or drag files from your file manager onto the
Project panel or the timeline. Supported: anything FFmpeg can decode (MP4, MOV, MKV, AVI, WebM,
MXF, MTS, WMV, MPEG; WAV, MP3, AAC, FLAC, OGG, AIFF; PNG, JPG, TIFF, BMP, WebP, GIF stills).

**Project panel** — search at the top; list/icon view, thumbnail size, Import, New Item
(sequence, title, color matte, adjustment layer, 3D model, Remotion), **New Bin** and *Clear*
along the bottom. Double-click a bin to open it, `..` goes up; right-click media for *Move to
Bin*, *Open in Source Monitor*, *Insert/Overwrite*, *Link Media…* (relink), *Reveal in
Explorer*, *Remove from Project*.

**Offline media** — if a file has moved, clips show *Media Offline*; right-click the media and
choose **Link Media…** to point at the new location.

**Proxies** — media taller than 1080 pixels (720 in Low-end mode) gets a 540p intra-frame
MJPEG proxy generated in the background; the status line shows progress. Playback and
scrubbing then decode the cheap proxy. The proxy toggle in the header switches between
proxies and originals. **Export always uses the originals.** Proxies live in the cache
folder (*Preferences › Media cache folder*, default: your OS cache directory).

## 5. Editing

### Basic workflow

1. Double-click a clip in the Project panel → it opens in the **Source** monitor.
2. Mark `I`/`O`.
3. Choose destination tracks by clicking the **source patch** boxes (V1/A1 by default).
4. `,` inserts, `.` overwrites at the playhead — or at the sequence In point, if one is set
   (three-point editing). Dragging from the Source monitor or the Project panel onto the
   timeline also works; hold `Ctrl` to insert instead of overwrite.

### Selecting and moving

Click to select; `Shift`/`Ctrl`-click to add; drag a box on empty space to marquee-select.
Video and audio from the same file are **linked**: selecting one selects the other
(`Alt`-click for one side; `Ctrl+L` links or unlinks; the chain button turns linked
selection off globally). Drag to move; clips snap to edits, markers, the playhead and In/Out
(`S` toggles). Moves never create overlaps: dropping over another clip **overwrites** it
(trimming what it lands on); `Ctrl` while dragging **inserts** and ripples.
Drag vertically to change tracks (video to video, audio to audio; linked partners follow).
`Alt+←/→` and `+12 Enter` nudge precisely.

### Trimming

| Tool | Drag a clip edge to… |
|---|---|
| Selection (`V`) | trim one clip; hold `Ctrl` to ripple |
| Ripple Edit (`B`) | trim and shift everything later (yellow edge) |
| Rolling Edit (`N`) | move the cut between two clips without changing total length (red) |
| Rate Stretch (`R`) | change the clip's speed so it fills the new length |
| Slip (`Y`) | drag inside a clip: change which part of the source shows, same position and length |
| Slide (`U`) | drag inside a clip: move it, and trim the neighbours to compensate |

Trims stop at the end of the source media (no black frames) and at neighbours. Linked clips
trim together. The status line shows the live delta and new duration while you drag.
**Sync lock** decides which other tracks shift on a ripple; **lock** protects a track
completely.

### Cutting and removing

`Ctrl+K` splits (selected clips, else clips under the playhead on targeted tracks);
`Ctrl+Shift+K` splits every track; the Razor tool (`C`) cuts where you click.
`Delete` clears, `Shift+Delete` ripple-deletes (closes the gap). **Lift** (`;`) and
**Extract** (`'`) remove the In–Out range. `Q`/`W` ripple-trim the previous/next edit to the
playhead. Right-click an empty gap › *Ripple Delete Gap*, or *Sequence › Close Gaps*.

### Speed and reverse

`Ctrl+R` sets speed (5%–2000%), reverse, and whether trailing clips ripple. Speed badges
show on the clip name. Audio is resampled (no pitch correction); reversed clips are silent.

### Markers, enable/disable, labels

`M` adds a marker (shown on the ruler and the monitor's mini timeline). `Shift+E` disables a
clip without deleting it. Right-click › *Rename…* or *Label*.

### Undo

Every edit is one undo step, including multi-track ripples and whole drags. The **History**
panel lists steps; click one to jump there.

## 6. Transitions

Select clips (or park the playhead near a cut on a targeted track) and press `Ctrl+D`
(video) / `Ctrl+Shift+D` (audio), or double-click a transition in the **Effects** panel.
Available: Cross Dissolve, Dip to Black, Dip to White, Wipe Left/Right, Push, Iris Round, and
a constant-power audio crossfade.

- At a cut where two clips touch, the transition is **centered** on the cut (both clips
  need enough media beyond their ends; shorter transitions are clamped to the clip length).
- At a clip's head or tail with nothing adjacent it becomes a fade from/to nothing.
- **Drag** a transition's edge to resize it; **double-click** to enter a duration in frames;
  **right-click** to change type or clear it. `Ctrl+D` / `Ctrl+Shift+D` use *Preferences ›
  Default transition duration* (25 frames by default); applying a transition from the Effects
  panel uses one second.

## 7. Effects and keyframes

Apply effects by selecting clips and **double-clicking** an effect in the Effects panel (or
use the search field). Effects stack in the order added; reorder or remove them in
Effect Controls (right-click the effect's header).

| Category | Effects |
|---|---|
| Transform | Motion (position, scale, rotation), Opacity, Crop, Horizontal Flip |
| Color Correction | Lumetri Color |
| Stylize | Vignette |
| Blur & Sharpen | Gaussian Blur, Sharpen |
| Masking | Mask (Shape) — ellipse/rounded rectangle with feather and opacity |
| Perspective | Basic 3D (swivel, tilt, distance) |
| 3D | 3D Model (on OBJ clips) |
| Audio Effects | Volume |
| Plugins | anything installed through the [plugin API](PLUGINS.md) |

**Effect Controls** works like Premiere's:

- **Twirl** (▸) shows or hides an effect; the **fx** button bypasses it; the circular arrow
  resets it.
- Each parameter has a **stopwatch**: click it to start animating (a keyframe is created
  at the playhead). Move the playhead and change the value to add more keyframes. The
  diamond buttons jump to the previous/next keyframe or add/remove one; the lane at the
  right shows keyframes across the clip; click it to jump.
- **Values are blue "hot text"**: drag left/right to scrub (`Shift` ×10, `Ctrl` fine) or
  click to type a number. Dragging updates the Program monitor live, and the whole drag is
  one undo step.
- Turning the stopwatch off deletes the keyframes (after confirmation).

Opacity and Motion exist on every video clip. Interpolation between keyframes is linear
(a hold/ease option exists in the file format).

## 8. Titles, mattes, adjustment layers and nests

- **Title** (`Ctrl+T` or `T`): font, size, bold/italic, fill, stroke, shadow, background box,
  alignment and position, with a live preview; *Lower third* and *Centered* presets.
  Double-click the title on the timeline to edit it again.
- **Color Matte**: a solid color generator.
- **Adjustment Layer** (*File › New › Adjustment Layer*): place it above other clips and apply
  effects to it; they affect everything on lower tracks for the length of the layer. Its
  Opacity blends the result.
- **Nest** (select clips, *Clip › Nest…* or right-click): moves the clips into a new sequence and
  replaces them with a single nested clip that keeps the same timing, including its audio.
  **Double-click** the nested clip to open the inner sequence; changes inside appear in the
  outer one. Nests can be nested (up to 4 levels deep).

## 9. Color: Lumetri and scopes

Open the **Lumetri Color** tab (or the Color workspace) with a video clip selected.

- **Look** presets (Cinematic, Warm Film, Cool Noir, Black & White) set starting values.
- **Basic Correction**: Exposure, Contrast, Highlights, Shadows, Whites, Blacks, Temperature,
  Tint, Saturation, Vibrance, Hue Shift and Faded Film — all keyframeable.
- **Color Wheels** (as per-channel sliders): Shadows, Midtones and Highlights each have Red, Green and
  Blue offsets (lift / gamma / gain). Push Shadows Blue and Highlights Red for the classic teal-and-orange
  look. Grades keep a clip's transparency and run the same way in the preview, the exported file and the
  exported `.cube`.
- **Export .cube** saves the current grade as a 3D LUT (33³, Adobe/Resolve format) that you
  can load in Premiere, DaVinci Resolve, OBS and most editors. *(LUT import is not
  implemented yet.)*

**Lumetri Scopes** (tab in the first group; panel menu switches mode): Waveform (luma),
RGB Parade and Vectorscope, computed from the Program monitor. Keep luma between 0 and 100
for broadcast-safe SDR; skin tones sit on the line toward the top-left of the vectorscope.

## 10. Audio

- **Clip gain**: drag the yellow line on an audio clip, or use *Clip Gain* in Effect Controls
  (−60 to +24 dB).
- **Volume** effect: keyframe level for fades and ducking.
- **Fades and crossfades**: audio transitions (section 6) are constant-power.
- **Audio Track Mixer**: one strip per audio track with fader (−60 to +12 dB), pan, mute
  and solo. Mixer changes affect playback and export identically.
- **Meters** at the far right of the bottom row show the master output; red means clipping.
  The output is soft-limited to avoid harsh digital clipping while you work.
- **Waveforms** draw on audio clips (display option in the timeline settings menu). Audio is
  the master clock during playback, so picture follows sound.

## 11. 3D

- **Basic 3D** (Effects › Perspective): swivel, tilt and distance with real perspective — for
  flip-ins, card turns and 2.5D parallax. Keyframe the parameters.
- **3D Model** (*File › New › 3D Model (OBJ)…*): imports a Wavefront OBJ (triangles, quads and
  n-gons; positive or relative indices) and places it as a clip. The model is automatically
  centered and scaled to fit; animate in Effect Controls: rotation X/Y/Z, scale, camera
  distance, field of view, light azimuth/elevation, ambient, color and wireframe.
  Composite it over video by placing it on a higher track; blur, Lumetri, opacity, masks and
  plugins work on it like on any clip.

Limitations: flat-shaded CPU rendering, no textures/materials, no glTF, no camera-and-light
objects in the timeline. It is meant for logos, product spins and graphic elements.

## 12. React / Remotion and plugins

- **[Remotion & React](REMOTION.md)**: render React compositions with your own props to
  ProRes 4444 with alpha and drop them on the timeline; re-render after editing the code.
- **[Plugins](PLUGINS.md)**: install native effect plugins; *Edit › Plugins…* shows what
  loaded.

## 13. Exporting

**File › Export › Media…** (`Ctrl+M`) opens Export Settings.

| Setting | Notes |
|---|---|
| Preset | Match Sequence, YouTube 1080p / 4K (HEVC) / 720p, Vertical 9:16, Square 1:1, 4:5, ProRes 422, DNxHR HQ, WebM VP9, AV1, WAV, AAC audio-only |
| Format | mp4, mov, mkv, webm, wav, m4a |
| Video codec | H.264, HEVC, ProRes 422, DNxHR HQ, VP9, AV1 — only codecs your FFmpeg build can encode are listed |
| Frame size | any size; the image is scaled from the sequence |
| Bitrate | target kbps (ProRes/DNxHR ignore it) |
| Hardware encoding | tries NVIDIA NVENC, Intel Quick Sync, AMD AMF, then Media Foundation; falls back to a software encoder automatically |
| Audio | AAC, PCM 16-bit or Opus; 192 kbps default |
| Range | entire sequence or the sequence In/Out |

Click **Export** (or **Queue**; both add the job to the queue, and jobs run one after
another). The **Render Queue** panel shows progress, which encoder was chosen, and finished
files (double-click to open the folder). Export runs on its own thread; you can keep editing,
but the render uses a snapshot taken when you pressed Export.

Other exports: **Export Frame** (`Ctrl+Shift+E`, PNG/JPEG/TIFF from the Program monitor at
full quality), **Lumetri LUT** (section 9), and timelines (next section).

Note on patents/licences: H.264, HEVC and AAC may need licences where you distribute
software that encodes them; see [THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md).

## 14. Moving projects to and from other editors

| To/from | How |
|---|---|
| Premiere Pro → Breefer | *File › Import Premiere Pro Project…* with a `.prproj` (best effort), or export **Final Cut Pro XML** from Premiere and import the `.xml` (fuller transfer) |
| Breefer → Premiere Pro | *File › Export › Final Cut Pro 7 XML…*; in Premiere, *File › Import* the `.xml` |
| DaVinci Resolve / Avid / others | FCP 7 XML, CMX 3600 EDL or OpenTimelineIO |
| Final Cut Pro X | export FCPXML (import of FCPXML is not supported) |

Details and exact lists of what transfers are in
[PREMIERE_COMPATIBILITY.md](PREMIERE_COMPATIBILITY.md).

## 15. Performance and low-end hardware

Breefer is tuned for dual-core/4-thread CPUs (2nd–4th gen Core i3/i5, early Ryzen/APUs)
with integrated graphics and 4–8 GB of RAM.

**Low-end mode** (*Edit › Preferences*; switched on automatically on first run when the
CPU has 4 threads or fewer): 1/4 playback resolution, proxies for anything above 720p,
timeline thumbnails off.

**Tips**
- Let proxies finish (status line) before judging playback; keep the proxy toggle on.
- Set the monitor's *Playback Resolution* to 1/4 or 1/8 while editing; Full is for checking.
- Hardware decoding (D3D11VA on Windows, VA-API on Linux, VideoToolbox on macOS) is on by
  default for H.264/HEVC/VP9/AV1 files 1280 pixels wide or more and falls back to software if the GPU
  can't decode a file. Turn it off in Preferences if you see glitches.
- Transitions and stacked effects cost CPU: reduce Blur radius, avoid several 3D layers, and
  keep Lumetri neutral on clips you don't grade.
- Put the cache folder on an SSD if you have one (Preferences).
- **Memory governor.** Every 3 seconds the app compares its frame cache with the RAM that is really
  free: the cache is sized to roughly a sixth of free memory (48 MB to 1 GB; at most 128 MB in low-end
  mode), and if the system drops below about 7 % free RAM (never under 256 MB) the preview caches are
  released immediately and the status bar says so. This is what keeps a 4 GB machine from swapping.
- **Graphics cards.** The preview is composited on the CPU, so a missing, old or misbehaving GPU can
  never break it. GPUs are used for hardware *decoding* (NVIDIA, AMD and Intel through D3D11VA, VA-API or
  VideoToolbox) and hardware *encoding* on export (NVENC, AMD AMF, Intel Quick Sync, VideoToolbox, as
  far as your FFmpeg build includes them). If the GPU decoder returns a bad frame the app switches to
  software decoding by itself and reopens the clip. *Help › System Information* lists what was
  detected on your machine.
- **Portrait (9:16) and small screens.** The monitors follow the sequence's own aspect ratio, shrink
  their controls on short panels, and dialogs are kept inside the screen. The window can be as small
  as 800 × 520.
- Memory is bounded: the Source and Program monitors each cache at most 256 MB of decoded
  frames (another 256 MB while exporting). Measured on Windows: about 90 MB idle and
  about 160 MB peak while playing a short 4K clip through its proxy; long timelines with many
  different clips fill the caches and use more, up to roughly 600 MB plus the decoders.
- Playback drops frames rather than slowing the audio, and lowers the preview resolution by
  itself when it can't keep up (never above the resolution you chose).

## 16. Preferences, autosave and where files live

*Edit › Preferences…* (`Ctrl+,`): media cache folder, auto-save interval (default 5 min),
default transition duration, low-end mode, hardware decoding, and what to do when the first
clip doesn't match the sequence.

| What | Where |
|---|---|
| Project | the `.bcproj` you choose |
| Auto-save | `<project>.bcproj.autosave`, written while the project has unsaved changes; offered for recovery when you reopen a project whose autosave is newer; removed on save/close |
| Proxies | `<cache folder>/proxies/*.avi` |
| Settings | Windows registry `HKCU\Software\Breefer\Composer`; macOS `~/Library/Preferences/com.Breefer.Composer.plist`; Linux `~/.config/Breefer/Composer.conf` |
| User plugins | Windows `%APPDATA%\Breefer\Breefer Composer\plugins`; macOS `~/Library/Application Support/Breefer/Breefer Composer/plugins`; Linux `~/.local/share/Breefer/Breefer Composer/plugins` |
| Remotion renders | `Remotion Renders/` next to the project |

The window title shows `*` while there are unsaved changes; you are asked before closing.

**Crash protection.** A project that has never been saved is auto-saved to the app's `recovery`
folder; after an unexpected exit the next start offers it back. A crash also writes
`crash-<date>.txt` and a running `session.log` into the `logs` folder (*Help › System Information ›
Open Log Folder*) — please attach the newest crash report to a bug report. Errors raised while
rendering a frame or exporting are caught and reported instead of closing the editor.

## 17. Troubleshooting

| Problem | Try |
|---|---|
| **Media Offline** | Right-click the media in the Project panel › *Link Media…* |
| Playback stutters | Lower playback resolution; wait for proxies; enable Low-end mode; close other apps |
| No sound | Check track mute/solo, the mixer fader, clip *Enable*, and your OS output device; reversed clips are silent |
| Black frames at a clip end | The clip reaches the end of its media; trim it or slip it |
| Export fails or hardware encoder error | Untick *Hardware encoding* or pick H.264; the Render Queue log names the encoder that failed |
| Export is slow | CPU rendering; lower the frame size, drop heavy effects, or use a hardware encoder |
| A plugin does not appear | *Edit › Plugins…* shows load errors; see the [Plugin Guide](PLUGINS.md#troubleshooting) |
| Remotion says Node.js not found | Install Node.js 18+ and restart Breefer so `PATH` updates |
| Imported `.prproj` is incomplete | Expected; use Premiere's FCP XML export ([details](PREMIERE_COMPATIBILITY.md)) |
| Window layout is odd | *Window › Workspaces › Reset Layout* |
| Crash or freeze | Reopen the project and accept the autosave recovery; please report with steps |
