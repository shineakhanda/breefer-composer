# Breefer Composer

A free, open-source non-linear video editor for Windows, macOS and Linux, built to stay
responsive on modest hardware (dual-core / 4-thread, 8 GB RAM, integrated graphics).

![Breefer Composer](docs/screenshot.png)

This repository currently contains a **working Windows MVP**: a dockable Qt 6 workspace,
a multi-track timeline with real trimming, FFmpeg decode and compositing, audio playback,
undo/redo, project persistence, and timeline interchange export.

---

## Interface (Premiere Pro layout)

- Premiere's Editing workspace on one screen, tuned for **1366x768**: Source / Effect Controls /
  Lumetri / Mixer / Scopes group, Program monitor, Project / Effects / History / Render Queue
  group, Tools panel, Timeline with sequence tabs, Audio Meters.
- Rounded panels on a black gutter, blue outline on the focused panel, panel menus,
  **` (backtick) maximizes the panel under the cursor**, double-click a tab to maximize.
- Custom vector icon set (no emoji, no font glyphs) for tools, transport and timeline.
- Header bar with Import / Edit / Export, Toggle Proxies and Quick Export.
- Effect Controls with stopwatches, keyframe navigation, keyframe lanes and blue scrubbable
  values (drag; Shift = x10, Ctrl = fine; click to type).
- Track headers: source patching (V1/A1), lock, track targeting, sync lock, eye / M / S.
- Monitors: Fit/zoom, playback resolution Full to 1/16, safe margins, loop, mini timeline.

## Low-end hardware

- D3D11VA hardware decoding (Intel / AMD / NVIDIA iGPUs) for H.264, HEVC, VP9, AV1 with
  software fallback; only displayed frames are copied back from the GPU.
- Default playback resolution 1/2 (1/4 on dual-thread CPUs) with automatic drop/recover.
- Thumbnails decode on one low-priority background thread; the timeline repaints only the
  playhead columns during playback; Effect Controls updates values in place.
- Automatic 540p MJPEG proxies for anything above 1080p.

## What works today (v0.2)

**Editing (Premiere-style)**
- Linked video/audio clips (Alt-click selects one side, Ctrl+L links/unlinks).
- Full tool palette: Selection (V), Track Select Forward (A), Ripple (B), Rolling (N),
  Rate Stretch (R), Razor (C), Slip (Y), Slide (U), Hand (H), Zoom (Z).
- Multi-select, marquee select, live trim/move preview in the Program monitor.
- Overwrite and insert moves (Ctrl+drag); lane moves mirror linked audio.
- Trims are limited by real media handles; ripple edits respect **sync lock** on every track.
- Three-point editing from the Source monitor: In/Out, Insert (,) / Overwrite (.),
  Lift (;) / Extract ('), Q/W ripple trims, Add Edit (Ctrl+K / Ctrl+Shift+K).
- Copy / cut / paste / paste-insert, duplicate, nudge (Alt+arrows), enable/disable,
  speed/duration with reverse and ripple, clip labels and rename.
- Track targeting, sync lock, lock, mute, solo, resizable track heights, add/delete tracks.
- Sequence markers, In/Out range, Match Frame (F), J/K/L shuttle, loop playback.
- Drag media from the Project panel, the Source monitor, or Explorer onto the timeline.
- Whole-sequence undo/redo with a **History** panel.

**Effects, titles, audio**
- Transitions: Cross Dissolve, Dip to Black/White, Wipe, Push, Iris; constant-power audio
  crossfades. Ctrl+D / Ctrl+Shift+D apply defaults; drag a transition to resize it.
- Motion, Opacity, Lumetri Color, Crop, Flip, Vignette, Gaussian Blur, Sharpen, Shape Mask.
- Titles (font, stroke, shadow, background box, lower-third preset) and Color Mattes.
- Clip gain via the yellow rubber band, Volume effect, Audio Track Mixer (fader, pan,
  mute/solo). Audio is the master clock during playback.

**Export**
- H.264 and HEVC (NVENC / Quick Sync / AMF hardware first, software fallback), ProRes 422,
  DNxHR HQ, VP9, AV1, WAV and AAC audio-only.
- Presets: YouTube 1080p/4K/720p, Vertical 9:16, Square 1:1, 4:5, masters, audio.
- In/Out range export, batch **Render Queue**, Export Frame (Ctrl+Shift+E).

**Projects and interchange**
- `.bcproj` JSON (v2, auto-migrates v1), auto-save with crash recovery, recent projects,
  unsaved-changes prompts, offline-media detection and relinking.
- Import **and** export: OpenTimelineIO, FCP 7 XML (Premiere/Resolve), CMX 3600 EDL
  (FCPXML is export-only).
- Multiple sequences with presets (including vertical and square).

**Tested:** `build/breefer_tests.exe` runs 123 headless checks on the edit engine,
transitions, timecode, project files and interchange round-trips.

## Keyboard shortcuts

Press **F1** in the app for the full list. They follow Premiere Pro defaults
(J/K/L, I/O, `,` `.` `;` `'`, Q/W, Ctrl+K, Ctrl+D, and the tool letters above).

## Building (Windows, MinGW)

Tested with **Qt 6.12.0 (mingw_64)**, **MinGW 13.1**, **FFmpeg 9.0.1 (LGPL shared)**,
CMake 3.31 and Ninja.

1. Install Qt 6.12 with the *MSVC or MinGW 64-bit* kit and the matching compiler.
2. Get an **LGPL** shared FFmpeg build (headers + `*.dll.a` + DLLs), for example
   `ffmpeg-master-latest-win64-lgpl-shared` from BtbN's builds, and place it at
   `.toolchain/ffmpeg/ffmpeg-master-latest-win64-lgpl-shared/`.
   The path is configurable: `-DFFMPEG_ROOT=<path>`.
3. Build:

```powershell
.\build.ps1            # configure + build
.\build.ps1 -Run       # build and launch
```

or manually:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_C_COMPILER=C:/Qt/Tools/mingw1310_64/bin/gcc.exe `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1310_64/bin/g++.exe `
  -DQT_ROOT=C:/Qt/6.12.0/mingw_64
cmake --build build
```

The build stages Qt (`windeployqt`), the platform plugin and the FFmpeg DLLs next to
`breefer.exe`, so the output directory is self-contained.

A demo project is included: `build\breefer.exe assets\demo.bcproj`.

---

## Architecture

```
src/
  core/     Rational timebase, project graph, command-pattern undo, Document
  media/    FFmpeg probe + video/audio decoders, frame cache, miniaudio output
  engine/   Compositor (render thread) + PlaybackEngine (clock, drop policy)
  io/       Project JSON persistence, EDL / FCP7 / FCPXML / OTIO adapters
  ui/       Qt Widgets: MainWindow, Timeline, Monitors, MediaBin, EffectControls, Meters
```

Key invariants:

- **Time** is integer ticks at 254 016 000 000 Hz, which divides exactly by every common
  frame rate including 24000/1001, 30000/1001 and 60000/1001.
- **Tracks** keep clips sorted and non-overlapping, so "what is active at time *t*" is a
  binary search (`O(log n)`) rather than an interval tree.
- **The UI thread never decodes.** A `RenderWorker` owns the compositor, every decoder and
  the frame cache; the UI only posts the newest requested timestamp and receives finished
  images through a queued signal.
- **The audio callback never allocates or locks.** It copies from a lock-free SPSC ring
  buffer; decoding and mixing happen on a separate feeder thread.
- **Edits are commands.** A track's clip vector is snapshotted before and after a gesture,
  which makes move/trim/split/ripple correct without bespoke inverse operations.

Design notes and the full rationale live in `docs/`.

---

## Known limitations (honest list)

- Rendering is CPU (QPainter); no GPU compositor yet, so heavy effect stacks export slowly at 4K.
- No nested sequences, adjustment layers, multicam, keyframe graph editor or Bezier masks.
- Reverse clips are silent; audio at non-100% speed is resampled without pitch correction.
- No `.prproj` or AAF import; use Premiere's FCP 7 XML or OTIO export instead.
- No AI features (captions, silence detection) yet.
- Windows is the only tested platform.

## Roadmap

1. Proxy generation (ProRes Proxy / DNxHR LB) + proxy/original switching.
2. Audio as the master clock; per-clip gain keyframes.
3. Color correction + transitions in the compositor.
4. Cache-aware background render and export.
5. OpenTimelineIO **import**, then AAF via LibAAF.
6. ONNX Runtime / whisper.cpp sidecar for captions and silence detection (opt-in).
