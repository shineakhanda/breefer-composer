# Contributing

Thanks for helping! A few ground rules keep the editor fast and the codebase sane.

## Before you start

- Open an issue for anything larger than a bug fix so the design can be agreed first.
- Keep the **core** (`src/core`, `src/io`) free of GUI and FFmpeg code; it is unit-tested
  headlessly. New edit operations belong in `src/core/EditOps.*` with tests in
  `tests/test_core.cpp`.
- The target machine is a 2nd–4th gen Core i5 with integrated graphics. Nothing may block
  the UI thread or the audio callback; decode and render on worker threads.

## Build and test

See the README for per-platform builds, then:

```bash
cmake --build build && ./build/breefer_tests
```

All tests must pass. CI builds Windows, Linux and macOS on every pull request.

## Style

- C++20, Qt 6. 4-space indent, braces on the same line, `m_` members, `camelCase`.
- Prefer small, explicit code over clever code; comment the *why*.
- UI text in `tr()`. No emoji or font glyphs as icons — add a vector icon to
  `src/ui/Icons.cpp`.

## Licence

By contributing you agree your work is released under GPL-3.0-or-later. Plugin SDK headers
in `sdk/include` are MIT so third-party plugins can use any licence.
