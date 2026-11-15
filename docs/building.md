# Building

## Requirements

Windows, Git for Windows, Python 3.9+, CMake 3.22+ (a native Windows build, not MSYS2's), Ninja
and the MinGW pinned in `toolchain.json` (13.1). The Qt MinGW toolchain (`C:\Qt\Tools`) provides
the compiler and Ninja. Toffee builds only against the static Qt below; configuring against a
shared Qt or another compiler stops with an error.

```powershell
git clone --recurse-submodules https://github.com/Gavwalsh15/toffee_wiiu.git
cd toffee_wiiu
.\build.ps1
```

This writes `dist\Toffee.exe` and `dist\toffee-cli.exe`. Run it again after changing code; finished
steps are skipped.

| Option | Use |
| --- | --- |
| `-Configuration Debug` | Debug build, includes the maintenance commands |
| `-AppDiagnostics` | Include the maintenance commands (`--stream-check` and friends) in a Release build |
| `-VerboseLogging` | Print start-up and runtime messages to stderr |
| `-QmlLint` | Lint the QML while building |
| `-Test` | Run the host tests (`tests/`) after building |
| `-QtPrefixPath <dir>` | Build against a Qt you already have |
| `-RebuildFirmware` | Rebuild the firmware bundle even if one exists |
| `-Version <v>` | Stamp a release version (`0.1.0`, `v0.1.0-rc1`); it must match `project(VERSION)` |
| `-Fresh` | Start from a clean configure |

For an IDE, open the folder and pick the `pc-release` or `pc-debug` preset in `CMakePresets.json`.
They expect Qt in `C:\qt-static`.

## Versions

`project(toffee VERSION x.y.z)` in `CMakeLists.txt` is the one version number. It is compiled into
both executables (`toffee-cli version`, the Settings page, the start-up log line and the Windows
file properties). Builds that are not releases show `x.y.z-dev`.

To release: bump `project(VERSION)`, commit, then push a tag `vx.y.z` (or `vx.y.z-rc1` for a
prerelease). CI stamps the tag into the build and stops if it does not match the project version.

## Where the pieces come from

Third-party code is a pinned submodule under `third_party/`, with our changes kept as patches in
`patches/` and applied by `cmake/ApplyPatch.cmake`. Only Qt is a prebuilt download.

The toolchain versions that several scripts and workflows need to agree on (Qt, MinGW and the
ESP-IDF build image) are pinned once, in `toolchain.json`.

| Piece | Pinned to | Patch | Built by |
| --- | --- | --- | --- |
| x264 (`third_party/drc-x264`) | commit `7f1eb42` | `drc-x264-gamepad.patch` | `cmake/X264.cmake` |
| ViGEmClient (`third_party/ViGEmClient`) | commit `b66d02d` | none | `cmake/ViGEm.cmake` |
| Virtual Display Driver 25.7.23 | release package, SHA-256 pinned | none | `cmake/DisplayDriver.cmake` (downloaded at configure time) |
| ESP-IDF (`third_party/esp-idf`) | `v5.5.5` | `esp-idf-nintendo-pairing.patch`, `esp-idf-usb-backpressure.patch` | `tools/package-firmware.ps1` (Windows) or `tools/build-firmware.sh` (Linux); `firmware/CMakeLists.txt` applies the patches |
| Static Qt and MinGW | `toolchain.json`, official source | none | `tools/build-qt-static.ps1`, published as a release asset |

- **x264** compiles straight from CMake using the two config headers in `cmake/x264/`. Its SIMD
  assembly needs NASM, which `build.ps1` downloads into `.tools/nasm` unless one is on `PATH`;
  without it the encoder is about six times slower, so the build stops.
- **Firmware** installs the ESP32-C5 toolchain into `.tools/idf-tools` the first time (a large
  download) and builds into `build/firmware`. `tools/package-firmware.ps1` bundles the images, a
  manifest and Espressif's prebuilt `esptool.exe` (pinned by SHA-256) for the app to embed.
- **Static Qt** is what lets Toffee ship as one `Toffee.exe`. `tools/fetch-qt-static.ps1` downloads it
  to `C:\qt-static` and checks its SHA-256 (`tools/qt-static.sha256` can pin one exact build).

### Firmware options

Set these in `firmware/sdkconfig.defaults` and build with `-RebuildFirmware`. The firmware
configuration is regenerated from that file on every build. CI refuses to build release firmware
with the two diagnostics options on, so turn them off again before committing.

| Option | Default | Use |
| --- | --- | --- |
| `CONFIG_ZERO_BOARD` | `y` | Drives the Waveshare C5-Zero's antenna switch. Turn it off for other boards. |
| `CONFIG_CHANNEL` | `36` | The 5 GHz channel (36, 40, 44 or 48) |
| `CONFIG_TOFFEE_MEMORY_DIAGNOSTICS` | off | Log allocation failures and memory headroom |
| `CONFIG_TOFFEE_PROFILE` | off | Log per-task CPU use every five seconds |

## CI and releases

**Build** (`.github/workflows/build.yml`) runs on every push, and on pull requests from forks (a
branch in this repository is already built by its push):

1. `firmware` builds the firmware inside the `espressif/idf` image (pinned by digest in
   `toolchain.json`), or reuses a cached build when nothing that affects it changed.
2. `build` builds the app on Windows against the prebuilt Qt, stamped with the tag's version for a
   release.
3. `release` publishes a GitHub release for a `v*` tag, or when run by hand with a version:
   `toffee-<version>-windows-x64.exe`, `toffee-cli-<version>-windows-x64.exe`, a firmware zip and
   `SHA256SUMS.txt`.

**Static Qt** (`qt-static.yml`) builds and publishes Qt. Run it by hand once, and again with
**replace** ticked after changing the Qt or MinGW version in `toolchain.json` or
`tools/build-qt-static.ps1`.
