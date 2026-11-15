# Toffee

Stream your PC to a Wii U GamePad.

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/H7G22878U4)

The PC app captures a display or window or streamed frames, encodes it, and
sends it over USB to an ESP32-C5 bridge. The bridge pairs with the GamePad over
Wi-Fi and delivers video, audio and rumble. GamePad buttons, sticks, touch and motion, 
come back to the PC.

## Quick Start

Read [docs/setup.md](docs/setup.md) before installing. You need an ESP32-C5 board
with a USB Serial/JTAG port and a Wii U GamePad.

Board Used for devlopment:
[WaveShare](https://www.waveshare.com/esp32-c5-zero.htm)

Each release contains:

| File | Purpose |
| --- | --- |
| `toffee-<version>-windows-x64.exe` | The PC app. The bridge firmware is bundled inside it |
| `toffee-cli-<version>-windows-x64.exe` | The command line, for scripts and emulator launchers ([docs/framework.md](docs/framework.md)) |
| `toffee-<version>-firmware.zip` | The firmware on its own, for flashing without the app |
| `SHA256SUMS.txt` | Checksums of the files above |

The licenses of the third-party software Toffee uses are in [licenses/](licenses) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Short version:

1. Plug the ESP32-C5 into the PC.
2. Open `Toffee.exe`, go to **Flash ESP** and press **Flash firmware**.
3. Go to **Connect**, pick the bridge's serial port and press **Connect**.
4. Turn on the GamePad. If pairing is requested, enter the symbols Toffee shows.
5. Pick a source (Display capture or Window capture) and press **Start stream**.

## Documentation

- [Setup](docs/setup.md): hardware, flashing, pairing, controls, bug reports
- [Command line and DSU input](docs/framework.md)
- [Architecture](docs/architecture.md)
- [Building](docs/building.md): requirements, options, CI and releases
- [Roadmap](docs/roadmap.md)

## Build

Clone with submodules and run:

```powershell
git clone --recurse-submodules https://github.com/Gavwalsh15/toffee_wiiu.git
cd toffee_wiiu
.\build.ps1
```

It finds your compiler, builds x264 and the firmware from their pinned, patched submodules,
downloads a prebuilt static Qt, builds the app, and writes `dist\Toffee.exe`. See
[docs/building.md](docs/building.md) for the requirements and options.

## License

Copyright (C) 2026 Gavin Walsh.

Toffee is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, version 3. See [LICENSE](LICENSE).

Toffee uses third-party software that keeps its own license; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). "Wii U" and "GamePad" are
trademarks of Nintendo. Toffee is not affiliated with or endorsed by Nintendo.
