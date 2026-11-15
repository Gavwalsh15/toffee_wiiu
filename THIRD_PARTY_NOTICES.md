# Third-Party Notices

Toffee is licensed under the GNU General Public License, version 3 (see
[LICENSE](LICENSE)). It uses the following third-party software, which keeps its
own license.

| Software | Used for | License |
| --- | --- | --- |
| [x264](https://www.videolan.org/developers/x264.html), via the [`drc-x264`](https://github.com/opencma/drc-x264) fork | Encoding GamePad-compatible video in the encoder helper | GNU GPL v2 or later |
| [Qt 6](https://www.qt.io/) (static build: Core, Gui, Quick, Quick Controls 2, Serial Port) | The interface and serial port access | GNU LGPL v3 |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Firmware framework for the ESP32-C5 | Apache License 2.0 |
| [esptool](https://github.com/espressif/esptool) | Flashing the firmware from the app (Espressif's prebuilt `esptool.exe`) | GNU GPL v2 or later |
| [ViGEmClient](https://github.com/nefarius/ViGEmClient) | Presenting the GamePad as a virtual Xbox 360 controller | MIT |
| [ViGEmBus](https://github.com/nefarius/ViGEmBus) | The driver the virtual controller needs. Its installer is embedded in the app and runs only when the user asks | BSD 3-clause |
| [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver) | The extra monitor for the GamePad. The signed driver is embedded in the app and installed only when the user asks | MIT |
| libdrc (protocol reference) | GamePad packet formats | BSD 2-clause, Copyright (c) 2013 Mema Hacking |
| vanilla (protocol reference) | GamePad protocol behaviour | GNU GPL v2 |

## Notes

- The Qt libraries are linked statically. As the GPL-3.0 release of Toffee ships
  with its full source, you can rebuild the app against a modified Qt with the
  steps in [docs/building.md](docs/building.md). The Qt build is
  made from the official source by `tools/build-qt-static.ps1`.
- `libdrc` and `vanilla` were read as protocol references. Where any of their
  code appears in this repository, it keeps its original license and copyright
  notice.
- "Wii U" and "GamePad" are trademarks of Nintendo. Toffee is not affiliated
  with or endorsed by Nintendo and contains no Nintendo software or keys.

The x264, Qt, ESP-IDF, ViGEmClient, ViGEmBus and Virtual Display Driver license texts are in [licenses/](licenses). The esptool license comes
with `esptool.exe` in the firmware bundle, so each release's firmware zip includes it
(`esptool-LICENSE.txt`).
