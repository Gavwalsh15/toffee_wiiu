# Roadmap

## Current focus

Easier setup: a reproducible build, prebuilt releases, and clear diagnostics, so a new user can get
a stream running without help.

## Under consideration

None of these are committed to a release.

| Feature | What it would offer |
| --- | --- |
| Covered-window capture | Capture a window that is minimised or has other windows on top of it. |
| Rotated display capture | Capture displays set to a rotated orientation. |
| Virtual display refresh rate | Pick the extra monitor's refresh rate as well as its size. |
| Audio/video sync | A delay setting to line up sound and picture. |
| Frame-push API | Let an emulator hand Toffee its GamePad picture directly (a C library, or a pipe served by `toffee-cli`) instead of capturing its window. Needs the stream code split out of the Qt controller first. A program linking the library would have to be GPL-compatible, as Toffee and x264 are GPL. |
| GamePad camera and microphone | Receive them on the PC. |

## Have a feature in mind?

[Open a feature request](https://github.com/Gavwalsh15/toffee_wiiu/issues/new/choose) and say how
you would use it, or add your use case to an existing one.
