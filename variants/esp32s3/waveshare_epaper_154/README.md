# Meshtastic on Waveshare ESP32-S3-ePaper-1.54 + Wio-SX1262

*[Leer en español](README.es.md)*

A custom Meshtastic variant for the **Waveshare ESP32-S3-ePaper-1.54** (V2, no touch) with a
**Seeed Wio-SX1262 for XIAO** LoRa module wired by hand. It uses InkHUD on the 1.54" e-paper and
adds an optional EC11 rotary encoder with its own UI: node list, direct messages, replies, an
on-screen keyboard, a waypoint compass, a radar of nearby nodes and a status screen.

> This is an unofficial community build, not supported by Meshtastic or Waveshare.

## Hardware

| Part | Notes |
|---|---|
| Waveshare ESP32-S3-ePaper-1.54 **V2, no touch** | ESP32-S3-PICO-1-N8R8 (8 MB flash, 8 MB PSRAM), 200×200 e-paper |
| Seeed **Wio-SX1262 for XIAO** | 1.8 V TCXO on DIO3, DIO2 as RF switch |
| LoRa antenna for your region | **Always connect it before powering on.** Transmitting without an antenna can damage the SX1262 |
| Thin wire (enamelled wire works) | For the radio wiring |
| LiPo battery (optional) | Board's battery connector |
| EC11 rotary encoder with push button (optional) | 20 detents per turn |

The microSD slot is **not used**: its pads carry the radio signals. Leave it empty. The ES8311
audio codec is not used either. For the encoder, desolder the codec and wire the encoder to three
of the pads it leaves free.

Board schematic: [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54).

## Wiring

### LoRa module (required)

| SX1262 signal | ESP32-S3 GPIO | Where on the board |
|---|---|---|
| SCK | 1 | header J1 |
| NSS / CS | 2 | header J1 |
| MOSI | 43 | header J1 (TXD) |
| MISO | 44 | header J1 (RXD) |
| RF_SW (RXEN) | 3 | header J1 |
| DIO1 | 39 | microSD pad (CLK) |
| BUSY | 40 | microSD pad (D0) |
| RESET | 41 | microSD pad (CMD) |
| 3V3 / GND | — | header J1 |

Check the Wio-SX1262 pinout from Seeed to find each signal on the module.

### Rotary encoder (optional)

| Encoder pin | GPIO | ES8311 pad (codec removed) |
|---|---|---|
| A | 15 | 6 (I2S_SCLK) |
| B | 38 | 8 (I2S_LRCK) |
| Push button | 14 | 2 (I2S_MCLK) |
| Common + second button pin | GND | — |

Internal pull-ups are used, so no resistors are needed. If turning goes the wrong way, swap
`ENCODER_PIN_A` and `ENCODER_PIN_B` in `variant.h`.

> ⛔ **Do not use GPIO19 or GPIO20**, even though the header and the board label show them as
> GPIOs. They are the USB D− / D+ lines, and USB is the only way to flash this board (UART0 is taken
> by the radio). Using them leaves the board with no practical way to reflash.

## Flashing a prebuilt release

Download `firmware-waveshare-epaper-154-inkhud-*.factory.bin` from the
[Releases](https://github.com/proto-hommer/mesh-paper-154/releases) page.

1. Put the board in download mode: **hold BOOT while you plug in the USB cable**.
2. Flash with [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`):

   ```sh
   esptool --chip esp32s3 --port COM5 write-flash 0x0 firmware-waveshare-epaper-154-inkhud-X.Y.Z.factory.bin
   ```

   Replace `COM5` with your port (`/dev/ttyACM0` on Linux). Or use the browser-based
   [ESP Tool](https://espressif.github.io/esptool-js/) with address `0x0`.
3. Unplug and plug the board back in, then pair it with the Meshtastic app and **set your LoRa
   region** before anything else.

To update later without erasing settings, flash the non-factory `.bin` at `0x10000`.

## Building from source

Requires [PlatformIO](https://platformio.org/). From the repository root:

```sh
pio run -e waveshare-epaper-154-inkhud
```

| Environment | What it is |
|---|---|
| `waveshare-epaper-154-inkhud` | Normal build, with InkHUD on the e-paper |
| `waveshare-epaper-154-headless` | Radio only, no display. Useful to isolate problems |

Output goes to `.pio/build/<env>/`. The serial console is on native USB CDC.

## Usage

The **BOOT** button alone is enough to use the node, so the encoder is optional.

| Control | Action |
|---|---|
| BOOT short | Open/close the menu. In the keyboard: **send** |
| BOOT long | Next tile. In the keyboard: cancel |
| Encoder turn | Change applet. Inside a list, menu or the keyboard: move up/down |
| Encoder short press | Enter / select. On a message: reply to it |
| Encoder long press | Go back / toggle scroll mode on message lists |

Applets added by this variant:

- **Nodes:** sorted by last heard, with hop count and age. Each node has a card with *Direct
  message*, *Measure signal* (traceroute) and *Radar*.
- **Waypoints:** pick a waypoint to see its distance and bearing. The compass is **north-up**:
  the board has no magnetometer, so it cannot know which way you are facing.
- **Radar:** the 10 nearest nodes drawn to scale, with an arrow to the nearest one or to the one
  you pinned.
- **Status:** temperature/humidity (SHTC3), battery, time, direct neighbours, channel
  utilisation and airtime.

Your own position comes from the phone over Bluetooth.

### Battery power

The board has a hardware power latch. To power on from battery, **hold PWR for 1–2 seconds**
until the firmware takes over. To power off, hold PWR for 3 seconds or use *Save & Shut Down* in
the menu. On USB power the board runs regardless.

## Status

Working and in daily use: radio, e-paper, InkHUD, the BOOT button, the rotary encoder and the new applets.

Not yet fully verified:

- Battery power-on/off and deep sleep with the latch.
- Battery voltage reading is not calibrated.
- GPS (NEO-6M on the touch connector, GPIO21/7) is planned but not yet supported.

Issues and pull requests are welcome.

## License

Based on [Meshtastic firmware](https://github.com/meshtastic/firmware), licensed under GPL-3.0.
This variant is distributed under the same license.
