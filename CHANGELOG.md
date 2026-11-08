# Changelog

## 0.4.0

- **Movable panels.** Every panel is now a dock: drag, tab, split, float (also on a second monitor),
  close and reopen from the Window menu. Saved layouts, named workspaces, a lock option and automatic
  repair of a damaged layout.
- **Program/Source monitor fixes.** The picture follows the sequence's real aspect ratio (9:16, 1:1,
  4:5 ...) and no longer overlaps the transport controls on short panels.
- **Small screens.** Window minimum 800 x 520, dialogs clamped to the screen, compact monitor controls.
- **Crash protection.** Native crash reports, unclean-exit detection, exceptions in render/export
  threads and event handlers are contained, unsaved projects are auto-saved for recovery.
- **Memory governor.** Frame cache sized from the free RAM and released under memory pressure.
- **Hardware.** GPU decode failures fall back to software automatically; VideoToolbox encoders added;
  *Help > System Information* shows what was detected.
- **Colour grading.** Rewritten as a table-driven, multi-threaded pipeline that keeps transparency;
  new Hue Shift, Faded Film and Shadows/Midtones/Highlights colour wheels.
- Windows installer and portable zip are attached to releases.

## 0.3.0

Premiere `.prproj` import, plugin API, Remotion/React bridge, 3D, nested sequences, Linux/macOS CI.
