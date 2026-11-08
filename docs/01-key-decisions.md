# 01 — Key Decisions

Decisions below are committed, not tentative. Each states the choice, the reason, and what I rejected and why. The design target is a dual-core/4-thread machine with 8 GB RAM and integrated graphics — every choice is made against that budget, not against a hypothetical workstation.

---

## Decision Summary

| Area | Decision | One-line justification | Rejected alternative |
|---|---|---|---|
| Language | **C++20** | FFmpeg, Qt, and every NLE interchange SDK are C/C++ native; zero-cost FFI, direct `AVFrame`/`AVPacket` lifetimes, largest pool of editors who already read NLE source | Rust — better safety story, but every media dependency needs an FFI shim and the borrow checker fights the shared-graph-dirty-region model |
| GUI | **Qt 6 Widgets** (not QML) | Docking, menus, dense tables, and 60 Hz repainting are solved problems in Widgets; no QML scene-graph/JS runtime on a 4-thread CPU budget | Dear ImGui — fast to prototype, but docking, i18n, accessibility, and high-DPI are DIY forever; QML — adds a JS engine and per-frame scenegraph costs |
| Render backend | **Custom thin RHI over D3D11 + Metal + Vulkan** | ~3 kLOC of `create_texture`/`draw_quad`/`present`; no dependency, no lock-in; one shader source compiled to DXBC/Metal/MSL/SPIR-V | Qt RHI (couples to Qt's release cadence), bgfx (heavy build, opinionated, stale), wgpu (Rust), raw OpenGL (deprecated on macOS) |
| Audio | **miniaudio** (single-header, MIT) + own mixer | Cross-platform device backend for free; mixer is ours because NLE audio needs sample-accurate keyframe ramps, not a game-engine graph | PortAudio (threading/device quirks), RtAudio (thin, more glue), SDL2 audio (pulls SDL), JUCE (GPL/commercial, heavy) |
| Project format | **SQLite 3** (`.bcproj`), one table per entity | ACID saves, crash-safe, partial edits without rewriting the file, queryable for recovery; schema is small and migratable | JSON — whole-file rewrite on every save, no crash safety at 10k clips; proprietary binary — write it ourselves once for no gain; XML — same rewrite problem, slower parse |
| Interchange internal model | **Own canonical model** + adapters | Every format is lossy in different places; importing into "FCP7's model" makes FCPXML and OTIO second-class | Adopt OTIO as the internal model — it is a library for *exchange*, adds a dependency to the hot path, no persistent identity for clips |
| Threading model | **Shared immutable `TimelineSnapshot` + command queue + lock-free SPSC ring buffers** | UI thread never locks with render; workers read a frozen snapshot; audio consumes a pre-filled lock-free ring | Global mutex — priority inversion with the audio callback; actor-per-track — too much coordination for 4 threads |
| Proxy codec | **ProRes Proxy** (macOS/Linux) and **DNxHR LB** (Windows) — both intra-frame, 8-bit 4:2:2 | Intra-frame = seek-friendly, no GOP to lose on scrub; 720p ProRes Proxy is ~30 Mbps and decodes 5–8× faster than 4K H.264 on 4 threads | MJPEG — smallest decode cost but 4:2:2 8-bit with weak quality at 720p and no standard container story; H.264 all-I — patent-encumbered, slower to encode |
| Undo/redo | **Command pattern with explicit `UndoStack`** | Edits are semantic and small; commands carry a stable clip ID and can be coalesced (drag/trim produce one entry), which persistent data structures make awkward | Immutable snapshots — copy-on-write of a 10k-clip tree per keystroke is too much churn for 8 GB |
| Job scheduling | **Fixed thread pool (2–3 workers) + priority queue** | Predictable: playback work steals a high-priority slot from background jobs | Per-task threads — oversubscribes 4 hardware threads; work-stealing pool — complexity for no gain at this size |

---

## Why C++20 and not Rust

The honest answer is dependency gravity. FFmpeg is C with manual `av_frame_unref`/`av_packet_unref` lifetimes and a callback-driven `AVIOContext`. Qt is C++. OpenFX is C. LibAAF is C. In C++ these are direct calls; in Rust each one becomes a crate wrapping `bindgen` output with `unsafe` blocks and a wrapper layer — hundreds of unsafe boundaries where a bug in shape or lifetime is exactly the class of bug the language was chosen to prevent. You lose the safety benefit at the boundary and keep the ergonomics cost.

The second reason is contributor pool. NLE engineering is a small niche; most practitioners come from C++/Qt/FFmpeg shops. Rust NLE contributors exist, but they are a rounding error next to the C++ pool.

Mitigations since we give up Rust's guarantees: `-fsanitize=address,undefined` in CI, `std::span`/`std::string_view` discipline, `unique_ptr` ownership at FFmpeg boundaries with RAII deleters, `gsl::not_null`, and clang-tidy `cppcoreguidelines-*`. The audio thread is the only place where a mistake is unrecoverable, and it is hand-audited in review.

## Why Qt 6 Widgets and not QML

A pro NLE is a panel-heavy desktop app: dock widgets, splitters, context menus, dense property grids, tree views, drag-and-drop. Widgets ship all of that. QML ships a scene graph and a JavaScript engine, then expects you to build docking yourself or buy KDAB's Advanced Docking System. On 4 threads, a QML runtime + JS GC competing with video decode is the wrong trade. Widgets' downsides — animation and custom-shader content — are irrelevant since video frames go to a `QOpenGLWidget`/native child surface, not through the widget paint path.

Use Widgets for chrome, a native render surface for monitors, and only use `QPainter` for overlays (safe areas, masks, scopes) where it is fast enough.

## Why a custom RHI

The requirements are narrow: quad rasterization with YUV→RGB shader, a handful of blend/composite shaders, offscreen render targets, and readback for export. That is four entry points, not a graphics engine. Qt RHI ties texture lifetimes to Qt's version; bgfx adds a large build and an opinionated render-thread model; wgpu is Rust. A 2–4 kLOC `rhi` module with three backends, one shader source compiled to four targets via a build step, and a null backend for tests is the smallest thing that satisfies Vulkan 1.1 / Metal 2 / D3D11. OpenGL is a disabled fallback, not a target, because macOS deprecated it.

## Why SQLite for the project file

An NLE project is a relational graph: projects → sequences → tracks → clips → keyframes, plus media references and effect instances. SQLite gives us ACID saves, crash safety via WAL, partial updates (moving one clip rewrites one row), and `sqlite3_backup` for autosave snapshots. A 10 000-clip timeline is ~30–60 MB in SQLite; rewriting that as JSON on every save is not acceptable. We ship the amalgamation (public domain) so there is no dependency on a system library.

Interchange formats are *also* written out through the same canonical model — no format owns the internal representation.

## Memory budget (8 GB machine, ~3.5 GB to the app)

Target: leave 4 GB for the OS, browser, and other apps; the app must never exceed 3.5 GB or the box swaps and playback stutters.

| Consumer | Budget | Notes |
|---|---|---|
| Frame cache (decoded CPU frames) | **1400 MB** | ~1.4 GB; at 720p proxy YUV420 8-bit (1.3 MB/frame) ≈ 1000 frames ≈ 40 s at 24 fps; at 1080p full-res (3.1 MB/frame) ≈ 450 frames |
| GPU textures (decoded + composite targets) | **600 MB** | 2× ping-pong composite targets at 1080p RGBA (8 MB each), ~48 texture-pooled frames at 720p RGBA (3.7 MB each), plus scopes/overlay RTs |
| Decode buffers / FFmpeg internals | **250 MB** | ~6 concurrent decoders × 8–12 packets + ref frames; 4K H.264 needs the most |
| Audio | **80 MB** | 48 kHz float stereo, 8 s ring per stream ≈ 3 MB; 10 streams ≈ 30 MB; mixer + resamplers |
| UI (Qt, fonts, thumbnails) | **400 MB** | Qt baseline ~120 MB; bin thumbnails capped at 128 MB LRU |
| Model + undo stack + SQLite page cache | **250 MB** | Page cache capped at 64 MB; undo capped at 500 entries |
| JIT / code / stacks | **150 MB** | Shader compiler, SPIR-V, thread stacks (3 × 8 MB) |
| **Headroom** | **~370 MB** | Reserve for one 4K frame, export buffers, OS growth |
| **Total** | **3.5 GB** | Hard cap enforced by the cache manager; exceeding triggers eviction, not allocation failure |

Eviction rule: the cache manager owns the global budget. LRU by last *use* with two protected tiers — pins held by the currently-playing head (next ~120 frames) and the composite queue. A reservation over budget evicts from the unprotected tier first; if nothing is evictable it fails the request and the engine drops the frame rather than stalling the audio clock.

---

**Committed.** Continue to Step 2 for the component diagram, thread inventory, and playback degradation policy.
