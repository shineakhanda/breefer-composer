# React and Remotion

Breefer renders [Remotion](https://www.remotion.dev) compositions — video written in React —
and places them on the timeline as clips with transparency.

Requirements: **Node.js 18+** on `PATH`. The first render downloads Remotion's headless
Chrome automatically.

## Three ways to bring React in

| Source | What it is | How Breefer uses it |
|---|---|---|
| Remotion project | A folder whose `package.json` depends on `remotion` / `@remotion/cli` | `npx remotion render [entry] <composition>` |
| Pre-built bundle ("React binary plugin") | Output of `npx remotion bundle` (`index.html` + `bundle.js`); no source needed | `npx remotion render <bundle> <composition>` |
| React plugin | A Remotion project with a `"breefer"` section in `package.json` | Same as a project, with defaults pre-filled |

React plugin manifest example:

```json
{
  "name": "lower-third-pro",
  "dependencies": { "remotion": "^4.0.0", "@remotion/cli": "^4.0.0", "react": "^18" },
  "breefer": {
    "composition": "LowerThird",
    "entry": "src/index.ts",
    "defaultProps": { "name": "Jane Doe", "title": "Director", "accent": "#378ef0" }
  }
}
```

## Workflow

1. *File › New › Remotion / React Composition…* (Ctrl+Alt+R), or *New Item* in the Project panel.
2. Pick the folder, press **List** to load composition ids, edit the props JSON.
3. **Render & Import** renders ProRes 4444 with alpha into `Remotion Renders/` next to the
   project and drops the clip on V2 at the playhead (bin "Remotion").
4. Change the React code, then right-click the clip in the Project panel →
   **Re-render Remotion Composition**; every use on the timeline updates.
5. **Open Remotion Studio** launches the live React editor for the project.
