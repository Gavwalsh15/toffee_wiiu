# Using Toffee from other programs

Two ways in besides the window: `toffee-cli` for scripts and launchers, and a DSU server that
shares the GamePad's input with any DSU client, such as an emulator.

## toffee-cli

`dist\toffee-cli.exe` is built beside `Toffee.exe`. It prints to the console and stops cleanly on
Ctrl+C (the GamePad is told the stream ended).

```
toffee-cli ports                       list serial ports, marking the likely bridge
toffee-cli sources                     list the displays and windows that can be streamed
toffee-cli stream [options]            stream until stopped
toffee-cli input  [options]            print the GamePad's input as it arrives
toffee-cli pair   [--port <name>]      make the bridge forget its GamePad, print the new pairing code
                                       and wait until a GamePad pairs
toffee-cli dsu    [options]            serve the GamePad's input over DSU, no streaming
```

`toffee-cli help` lists the commands, `toffee-cli <command> --help` a command's options, and
`toffee-cli version` the version. `ports` and `sources` take `--json`, which prints one JSON array.

`stream`, `input`, `dsu` and `pair` connect to the bridge and share these options (`pair` takes only
the first two):

| Option | Meaning |
| --- | --- |
| `--port <name>` | The bridge's serial port. By default the port that looks like the ESP32-C5. |
| `--json` | Print events as JSON lines. |
| `--dsu`, `--dsu-port <port>` | Also serve input over DSU (below). Default port 26760. |

### stream

| Option | Meaning |
| --- | --- |
| `--display <n or name>` | Stream a display, by the number `sources` shows or part of its name. Default 0. |
| `--window <title>` | Stream the first window whose title contains the text, waiting for it to appear. |
| `--no-audio`, `--audio-gain <x>` | Turn PC audio off, or set its boost (0.5 to 4, default 2). |
| `--touch-mouse` | The GamePad's touch screen moves the mouse. |
| `--virtual-pad`, `--xbox-layout` | Show the GamePad to Windows as an Xbox 360 controller (needs ViGEmBus). |
| `--source-wait <seconds>` | How long to wait for `--window`. Default 30. |
| `--seconds <n>` | Stop after this long. |

For example, streaming an emulator's window while its motion and touch input comes from the GamePad:

```
toffee-cli stream --window "Cemu" --dsu
```

### Output and exit codes

With `--json`, every line is an object with an `event` (`connecting`, `ready`, `waiting`,
`streaming`, `status`, `input`, `dsu`, `pair`, `pairing`, `paired`, `stopped`) and a `message`.
`status` also carries `streaming`, `ready` and `dsuClients`; `input` carries `buttons`, `axes`,
`accel`, `gyro`, `touch`, `touchX` and `touchY`. `pairing` comes whenever the bridge waits for a new
GamePad, with the symbols to enter as `code` (for example `"spade heart diamond club"`).

| Code | Meaning |
| --- | --- |
| 0 | Finished normally |
| 1 | Failed (the reason is on stderr) |
| 2 | Bad command line |
| 3 | The bridge was not found |
| 4 | The display or window was not found |

## Input over DSU

Toffee runs a DSU server (the protocol Cemuhook introduced) that any DSU client can read: Cemu and
other emulators, or your own program.

1. Turn on **Settings → Input sharing**, or start `toffee-cli` with `--dsu`.
2. In the client, add a DSU server at `127.0.0.1`, port `26760`. In Cemu this is **Options → Input
   settings → DSU client**.
3. Map the buttons as for any controller.

How the GamePad appears:

- One controller in slot 0, with motion. It shows as disconnected until the GamePad is paired.
- The server only listens on `127.0.0.1`, so clients must run on the same PC and nothing else on your network can read your input.
- Face buttons follow their position: right is Circle, bottom is Cross, top is Triangle, left is
  Square. ZL and ZR are L2 and R2; `+` is Options and `-` is Share.
- The touch screen is scaled to a DualShock 4 touch pad.
- Accelerometer values are in g and the gyro in degrees per second, in the GamePad's own axis order.
  **Not yet checked against a real DSU client:** if tilting the GamePad moves the view the wrong
  way, please report which axes.

`toffee-cli dsu --simulate` serves a fake controller (circling left stick, face buttons pressed in
turn) for trying a client's setup without a GamePad.
