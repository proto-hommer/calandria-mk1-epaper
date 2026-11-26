<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="img/calandriapp-logo-oscuro.svg">
    <source media="(prefers-color-scheme: light)" srcset="img/calandriapp-logo-claro.svg">
    <img alt="CalandriApp" src="img/calandriapp-logo-claro.svg" width="200">
  </picture>
</p>

# Calandria MK1 epaper: Meshtastic on Waveshare ESP32-S3-ePaper-1.54 + Wio-SX1262

*[Leer en español](README.es.md)*

A custom Meshtastic variant for the **Waveshare ESP32-S3-ePaper-1.54** (V2, no touch) with a
**Seeed Wio-SX1262 for XIAO** LoRa module wired by hand. It uses InkHUD on the 1.54" e-paper and
adds an EC11 rotary encoder with its own UI: a home screen with four cards (messages, nodes,
heading and status), conversations, signal measurement, compasses, a radar of nearby nodes and WiFi.

> This is an unofficial community build, not supported by Meshtastic or Waveshare.
>
> **Alpha, under development.** Radio, display and encoder work and are used daily, but this is still a
> prototype and parts are untested (mainly on battery). Once the **GPS module** and an integrated
> **18650 battery with its TP4056 charger** are in, a **3D-printed enclosure** will be designed.
> Companion app: [CalandriApp](https://github.com/proto-hommer/calandriapp).

## Hardware

| Part | Notes |
|---|---|
| Waveshare ESP32-S3-ePaper-1.54 **V2, no touch** | ESP32-S3-PICO-1-N8R8 (8 MB flash, 8 MB PSRAM), 200×200 e-paper |
| Seeed **Wio-SX1262 for XIAO** | 1.8 V TCXO on DIO3, DIO2 as RF switch |
| LoRa antenna for your region | **Always connect it before powering on.** Transmitting without an antenna can damage the SX1262 |
| Thin wire (enamelled wire works) | For the radio wiring |
| LiPo battery (optional) | Board's battery connector |
| EC11 rotary encoder with push button | 20 detents per turn |

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

### Rotary encoder

| Encoder pin | GPIO | ES8311 pad (codec removed) |
|---|---|---|
| A | 15 | 6 (I2S_SCLK) |
| B | 38 | 8 (I2S_LRCK) |
| Push button | 14 | 2 (I2S_MCLK) |
| Common + second button pin | GND | — |

It is needed to move around Inicio and its cards: BOOT alone only reaches the InkHUD menu.
Internal pull-ups are used, so no resistors are needed. If turning goes the wrong way, swap
`ENCODER_PIN_A` and `ENCODER_PIN_B` in `variant.h`.

> ⛔ **Do not use GPIO19 or GPIO20**, even though the header and the board label show them as
> GPIOs. They are the USB D− / D+ lines, and USB is the only way to flash this board (UART0 is taken
> by the radio). Using them leaves the board with no practical way to reflash.

## Versions

The Calandria has its own version number, separate from Meshtastic's. The firmware is built on
**Meshtastic 2.8.3**, and that is what the node reports to the apps. The Calandria version
follows semantic versioning: **0.x** while in alpha, a patch bump for fixes, a minor bump for new
features, and 1.0.0 when it leaves alpha. Releases are tagged `calandria-X.Y.Z`.

The first releases used another scheme. Their tags stay as they are:

| Old tag | Calandria |
|---|---|
| `v2.8.3-ws154.1` | 0.1.0 |
| `v2.8.3-ws154.2` | 0.2.0 |
| `v2.8.3-ws154.3` | 0.3.0 |
| `v2.8.3-ws154.4` | 0.4.0 |
| `v2.8.3-ws154.5` | 0.5.0 |

The node shows its version in *ESTADO → Firmware*. With CalandriApp connected (1.8.5 or later) it
reads "al día" (up to date), "hay X.Y.Z" when a newer release is out (the app tells it) or "de
prueba" when the node runs something newer than the published release. The clock switches to the
phone's timezone right away, with no reboot.

### What's new in 0.9.0

- **Direct messages:** the ones you send from the MK1 (canned reply or keyboard) show in the
  conversation and go from "enviando" (sending) to "entregado" (delivered), or "no entregado", with
  the ACK. The history keeps 50 messages instead of 20, so a busy channel no longer pushes DMs out.
- **WiFi:** switched on and off from *ESTADO → WiFi*, with an emergency way out (see [WiFi](#wifi)).

0.7.0 brought the card navigation, and 0.8.0 fixes on top of it.

## Flashing a prebuilt release

**Recommended: the [web flasher](https://proto-hommer.github.io/calandria-flasher/)** (Chrome or
Edge on a computer, USB cable). It always installs the latest release. *Actualizar* (update) keeps
your settings; *Instalación limpia* (clean install) erases everything.

To flash by hand instead, download
`firmware-calandria-mk1-epaper-*.factory.bin` from the
[Releases](https://github.com/proto-hommer/calandria-mk1-epaper/releases) page.

1. Put the board in download mode: **hold BOOT while you plug in the USB cable**.
2. Flash with [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`):

   ```sh
   esptool --chip esp32s3 --port COM5 write-flash 0x0 firmware-calandria-mk1-epaper-X.Y.Z.factory.bin
   ```

   Replace `COM5` with your port (`/dev/ttyACM0` on Linux). Or use the browser-based
   [ESP Tool](https://espressif.github.io/esptool-js/) with address `0x0`.
3. Unplug and plug the board back in, then pair it with the Meshtastic app and **set your LoRa
   region** before anything else.

To update later without erasing settings, flash the non-factory `.bin` at `0x10000`.

## Building from source

Requires [PlatformIO](https://platformio.org/). From the repository root:

```sh
pio run -e calandria-mk1-epaper
```

| Environment | What it is |
|---|---|
| `calandria-mk1-epaper` | Normal build, with InkHUD on the e-paper |
| `calandria-mk1-epaper-headless` | Radio only, no display. Useful to isolate problems |

Output goes to `.pio/build/<env>/`. The serial console is on native USB CDC.

## Usage

The MK1 starts on **Inicio** (home): four cards, each with a summary and a counter. The encoder is
needed to move around. BOOT alone only reaches the InkHUD menu.

| Gesture | Does |
|---|---|
| Turn the encoder | Move through the list or the content |
| Encoder click | Open / choose |
| BOOT | Back one level. On Inicio: the full InkHUD menu. In the keyboard: **send** |
| Hold BOOT | Back to Inicio from anywhere. In the keyboard: cancel |
| Hold the encoder | Same as BOOT, except where a screen has its own action |

- **MENSAJES (messages):** the conversations: *Grupo* (channel 0), *Público* (channel 1), any other
  channel in use, and one per direct message, with ● when unread. Click opens it (sender, time, hops;
  yours show "✓ entregado"). Click a message to reply with a canned message or the keyboard. A new
  message pops up on its own; BOOT takes you back to where you were.
- **NODOS (nodes):** every node, last heard first, with distance, direction and hops. Lost nodes
  are struck through (1 h unheard; 13 h for ROUTERs). At the end, the **Radar** with the 10
  nearest. Each node's card has *message*, *Medir señal* (traceroute: there and back with the SNR of
  every hop and the weakest one), *track it* (compass), *favorite* and *forget*.
- **RUMBO (heading):** track a node, waypoints (nearest first), the group map and your own
  position. The compass is **north-up**: the board has no magnetometer, so it cannot know which way
  you are facing. Your position comes from the phone over Bluetooth.
- **ESTADO (status):** battery, channel use, temperature/humidity (SHTC3) and version. Below:
  *Ajustes* (power saving, rotate screen, local time, clean up ghosts, reboot, full menu), *WiFi*,
  *Bluetooth · PIN* and *Apagar* (power off).
- **Ghost nodes:** after a reflash or a board swap a node can get a new number, and the old one
  stays in the list under the same name. *ESTADO → Ajustes → Limpiar fantasmas* forgets the older
  duplicates (same long name, heard earlier) and nodes not heard for over 7 days, without a reboot.
  Favorites and ignored nodes are kept. Same rule as CalandriApp's *Clean up ghosts*.

### WiFi

For leaving the MK1 at home or at a fixed base with internet. **While WiFi is on, Bluetooth is off**
(on the ESP32 they don't run together), so the app cannot connect over Bluetooth.

- The network and password are loaded from the app; they are not typed with the knob.
- *ESTADO → WiFi* shows the network, whether it is connected (with its IP) and internet time.
  *Prender WiFi* (turn on) asks first. Turning WiFi on or off reboots the node. Without a network
  loaded it cannot be turned on.
- **Emergency way out**, so the node is never left with neither WiFi nor Bluetooth:
  - If WiFi has not connected 3 minutes after boot, Inicio shows "WiFi sin conexión". One click
    turns it off and Bluetooth comes back.
  - Without looking at the screen: **hold the encoder button while you power it on or plug it in**.
    It boots with WiFi off and Bluetooth on. (Not BOOT: held while plugging in, it puts the board in
    download mode.)
- Updating with the web flasher works the same with WiFi on: it goes over USB.

### Battery power

The board has a hardware power latch. To power on from battery, **hold PWR for 1–2 seconds**
until the firmware takes over. To power off, hold PWR for 3 seconds or use *ESTADO → Apagar*. On USB power the board runs regardless.

## Status

Working and in daily use: radio, e-paper, InkHUD, the BOOT button, the rotary encoder, Inicio with
its cards and channel messages.

Not yet tested on the board: the 0.9.0 direct messages and WiFi.

Not yet fully verified:

- Battery power-on/off and deep sleep with the latch.
- Battery voltage reading is not calibrated.
- GPS (NEO-6M on the touch connector, GPIO21/7) is planned but not yet supported.

Issues and pull requests are welcome.

## License

Based on [Meshtastic firmware](https://github.com/meshtastic/firmware), licensed under GPL-3.0.
This variant is distributed under the same license.
