# Third-party components

| Component | Use | Licence |
|---|---|---|
| [Qt 6](https://www.qt.io) | UI and core libraries (dynamically linked) | LGPL-3.0 |
| [FFmpeg](https://ffmpeg.org) | Decoding, encoding, scaling, resampling (dynamically linked) | LGPL-2.1-or-later (Windows/Linux CI builds); the macOS build uses Homebrew's FFmpeg, which is GPL |
| [miniaudio](https://miniaud.io) | Audio output (`third_party/miniaudio`) | Public domain or MIT-0 |
| [Remotion](https://www.remotion.dev) | Optional; invoked through `npx` if the user installs it | Remotion licence (free for individuals and small teams; companies may need a licence) |

Codec patents: H.264, HEVC and AAC may require patent licences in some countries when you
distribute binaries that encode them. Hardware encoders (NVENC, Quick Sync, AMF, Media
Foundation) use the licences of the GPU/OS vendor.
