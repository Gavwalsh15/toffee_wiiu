# Architecture

Toffee is a Windows PC app, an x264 helper process, and ESP32-C5 firmware. The PC
app owns capture, encoding, delivery and the interface. The firmware owns
pairing, Wi-Fi, and the GamePad's video, audio and input ports.

## Source Layout

| Folder | Responsibility |
| --- | --- |
| `src/app` | Application start-up and configuration |
| `src/stream` | Stream session: capture, frame preparation, encoding, delivery, reconnect |
| `src/video` | Display and window capture, and the Interactive demo scene |
| `src/encoder` | x264 helper process that produces GamePad-compatible pictures |
| `src/audio` | WASAPI loopback capture and gain |
| `src/transport` | USB serial writes and the write queue |
| `src/inputs` | GamePad input decoding and motion, touch and mouse output, and the virtual Xbox controller |
| `src/display` | The GamePad as an extra monitor: virtual display driver install and control |
| `src/firmware` | Firmware flashing from inside the app |
| `src/cli` | `toffee-cli`, the windowless command-line front end |
| `src/dsu` | The DSU server that shares GamePad input with any DSU client on the PC |
| `src/logger` | Session logs |
| `src/ui/qml` | The interface |
| `src/common` | Code shared by the PC and the firmware (video profile, wire limits, DRH parser, ADPCM), and small Windows helpers |
| `firmware/src/app` | Firmware start-up order |
| `firmware/src/transport` | USB record reception and parsing |
| `firmware/src/video` | Video delivery to the GamePad |
| `firmware/src/audio` | Paced audio delivery and the rumble flag |
| `firmware/src/gamepad` | GamePad control, input reception and forwarding |
| `firmware/src/network` | Wi-Fi access point and pairing |
| `third_party`, `patches` | Pinned x264 and ESP-IDF submodules, and our patches to them |
| `cmake`, `tools` | Build logic and the scripts behind `build.ps1` |

`Toffee.exe` and `toffee-cli.exe` share one static library, `toffee_core`: the stream, capture,
audio, transport, input, DSU and logging code. It does not depend on QML; the app registers the
QML-facing classes (`StreamController`, `InputController`, `DsuServer`) in `src/app/qml_types.h`.
The app adds the interface, the flasher, the virtual display and the scene preview.

### The stream code

`StreamController` (`src/stream`) is the one object the interface talks to. It decides what the
bridge's reports mean and runs the start/stop sequence; everything with a job of its own is a
separate type that the controller owns:

| Type | What it holds |
| --- | --- |
| `BridgeLink` | The serial port, what the bridge has reported (`Feedback`, with its `RecoveryPolicy`), and how long it has been quiet. It knows nothing about streaming. |
| `CaptureModel`, `FramePreparer` | The scene, the chosen display or window, the preview picture and the freeze state, and the worker that turns a capture into a raw frame |
| `EncoderSession` | The helper process |
| `RumbleDriver` | Game rumble strength into GamePad pulses (in `stream_controller.h`) |
| `SessionLifecycle`, `FramePipeline`, `DeliveryTracker`, `PictureSizeControl` | The small state classes the tick consults, together in `stream_state.h` |

The controller's own code is split by job across three files:

| File | What it holds |
| --- | --- |
| `stream_controller.cpp` | Construction, wiring between the parts, the status line |
| `stream_session.cpp` | Connect, start and stop, audio and rumble, the reconnect retries, finding the encoder |
| `stream_delivery.cpp` | The 2 ms tick that prepares, encodes and sends each picture, and handles the bridge's reports |

New behaviour that has its own state belongs in its own type like these rather than in
`StreamController`.

## Runtime Pieces

`Toffee.exe` embeds the bridge firmware, the encoder helper and the two driver
installers, and unpacks them when needed.

The encoder helper is a separate process. The app sends it raw frames on standard
input and reads encoded pictures from standard output, so a helper crash cannot
take the app down, and the GPL-licensed x264 code stays in its own program.

The ESP32-C5 firmware runs on the bridge. It talks to the PC over USB
Serial/JTAG and to the GamePad over 5 GHz Wi-Fi.

## Start-up Flow

Firmware (`firmware/src/app/application.cpp`):

1. Start memory diagnostics, then bring up Wi-Fi.
2. Start the USB receiver, the video sender, the audio sender and the input
   forwarder.
3. Pair with the GamePad, then start the GamePad control task.

PC app: open the serial port, wait for the bridge to report that the GamePad is
ready, then start the encoder and send a begin record.

## Stream Pipeline

1. **Capture.** A display is captured with Windows desktop duplication. The
   display is chosen by its Windows device name, so identical monitors work. A
   window is captured by cropping its display to the window's rectangle, which
   means the window must be visible. Either way the picture is scaled on the GPU
   to the GamePad's 864x480 profile.
2. **Encode.** The helper (x264, constant quantizer) produces one picture per
   frame. Later pictures depend on earlier ones, so pictures stay ordered. The PC
   may replace raw frames when it falls behind. An IDR picture restarts the
   sequence after a loss.
3. **Deliver.** Pictures and audio chunks are sent to the ESP32-C5 as framed
   records over USB: a `TVU1` magic, record type, sequence, length and CRC32.
   Types are begin, video picture, end, ADPCM audio, vibrate and pair reset.
   The bridge reports each picture done, and the PC keeps at most three in
   flight.
4. **Send.** The firmware sends video to the GamePad's video port and audio to
   its audio port. Large pictures are spread over several milliseconds so Wi-Fi
   buffers are not exhausted. The rumble flag rides in the audio packet header.
5. **Return path.** The GamePad's input reports (buttons, sticks, touch, motion)
   reach the firmware, which forwards them to the PC over USB.

Firmware send completion and bridge acknowledgements describe local delivery,
not confirmed display on the GamePad.

## Wire Definitions

The shared video profile, wire limits and record types are defined in `src/common/`. Both
directions use the same framing (`common/record.h`): a magic, the record type, a sequence, a
length and a CRC-32 of the payload.

- **PC to bridge** (`TVU1`): begin, video picture, end, ADPCM audio, vibrate and pair reset
  (`common/wire_limits.h`). The firmware parses them in `firmware/src/transport/record_parser.h`
  and the PC builds them in `src/stream/protocol.cpp`.
- **Bridge to PC** (`TVB1`, `common/bridge_record.h`): hello, state, picture done, stream ready,
  IDR request and sent, error, and the GamePad's input reports. The firmware sends them through
  `firmware/src/transport/bridge_link.cpp`, and `src/stream/bridge_decoder.cpp` splits them from
  the firmware's log text on the same USB stream. The bridge sends hello and state every second,
  so the PC learns the firmware's protocol version and whether the GamePad is ready without
  asking, and an app that finds no hello (an older firmware) tells the user to flash the new one.
  A record that is damaged is skipped without losing the ones after it.

Change a record's meaning, and bump `BRIDGE_PROTOCOL_VERSION`.

## Tests

`.uild.ps1 -Test` (or `cmake --build build/pc --target check`) runs `tests/bridge_protocol_test.cpp`.
It covers the USB framing in both directions, the firmware's parser and the firmware's picture
ring, with no Qt and no hardware. Streaming behaviour is checked on a bridge and GamePad.

To build it, see [building.md](building.md).
