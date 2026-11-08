# Breefer Composer documentation

| Document | Read it if you want to… |
|---|---|
| [User Guide](USER_GUIDE.md) | learn the editor: interface, editing, effects, audio, color, 3D, export |
| [Keyboard Shortcuts](SHORTCUTS.md) | look up a key |
| [How It Works](HOW_IT_WORKS.md) | understand the architecture: threads, timeline model, render and audio pipelines |
| [Plugin Guide](PLUGINS.md) | write a native effect plugin (C, C++, Rust) |
| [Remotion & React](REMOTION.md) | use React compositions as clips, or ship a React plugin |
| [Premiere Pro Compatibility](PREMIERE_COMPATIBILITY.md) | find out exactly which Premiere files, plugins and features work |
| [File Formats](FILE_FORMATS.md) | read or generate `.bcproj` project files, or script the editor |

Other files in the repository root: [README](../README.md) (build instructions),
[CONTRIBUTING](../CONTRIBUTING.md), [LICENSE](../LICENSE),
[THIRD_PARTY_NOTICES](../THIRD_PARTY_NOTICES.md).

## The five-minute version

1. **Import** media (Ctrl+I) or drag files from Explorer/Finder onto the timeline.
2. **Double-click** a clip in the Project panel to open it in the Source monitor; press
   `I`/`O` to mark a range, then `,` (insert) or `.` (overwrite).
3. **Trim** with the Selection tool (drag clip edges), cut with `Ctrl+K`, ripple-delete with
   `Shift+Delete`. `Ctrl+Z` undoes everything.
4. **Polish**: drop effects and transitions from the Effects panel, animate them in
   Effect Controls, grade in Lumetri Color, mix audio in the Audio Track Mixer.
5. **Export** with `Ctrl+M`.
