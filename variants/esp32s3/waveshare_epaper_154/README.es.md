# Meshtastic en Waveshare ESP32-S3-ePaper-1.54 + Wio-SX1262

*[Read in English](README.md)*

Variante propia de Meshtastic para la **Waveshare ESP32-S3-ePaper-1.54** (V2, sin touch) con un
módulo LoRa **Seeed Wio-SX1262 for XIAO** cableado a mano. Usa InkHUD en el e-paper de 1,54" y
suma un encoder rotativo EC11 opcional, con interfaz propia: lista de nodos, mensajes directos,
respuestas, teclado en pantalla, brújula de waypoints, radar de nodos cercanos y pantalla de estado.

> Es un build comunitario no oficial, sin soporte de Meshtastic ni de Waveshare.

## Hardware

| Pieza | Notas |
|---|---|
| Waveshare ESP32-S3-ePaper-1.54 **V2, sin touch** | ESP32-S3-PICO-1-N8R8 (8 MB flash, 8 MB PSRAM), e-paper 200×200 |
| Seeed **Wio-SX1262 for XIAO** | TCXO de 1,8 V por DIO3, DIO2 como RF switch |
| Antena LoRa para tu región | **Conectala siempre antes de energizar.** Transmitir sin antena puede dañar el SX1262 |
| Cable fino (sirve el esmaltado) | Para cablear la radio |
| Batería LiPo (opcional) | En el conector de batería de la placa |
| Encoder rotativo EC11 con pulsador (opcional) | 20 pasos por vuelta |

La ranura microSD **no se usa**: sus pads llevan las señales de la radio. Dejala vacía. El codec de
audio ES8311 tampoco se usa. Para el encoder, desoldá el codec y cableá el encoder a tres de los
pads que deja libres.

Esquemático de la placa: [wiki de Waveshare](https://www.waveshare.com/wiki/ESP32-S3-ePaper-1.54).

## Cableado

### Módulo LoRa (obligatorio)

| Señal del SX1262 | GPIO del ESP32-S3 | Dónde está en la placa |
|---|---|---|
| SCK | 1 | header J1 |
| NSS / CS | 2 | header J1 |
| MOSI | 43 | header J1 (TXD) |
| MISO | 44 | header J1 (RXD) |
| RF_SW (RXEN) | 3 | header J1 |
| DIO1 | 39 | pad de la microSD (CLK) |
| BUSY | 40 | pad de la microSD (D0) |
| RESET | 41 | pad de la microSD (CMD) |
| 3V3 / GND | — | header J1 |

Para ubicar cada señal en el módulo, mirá el pinout del Wio-SX1262 que publica Seeed.

### Encoder rotativo (opcional)

| Pata del encoder | GPIO | Pad del ES8311 (codec retirado) |
|---|---|---|
| A | 15 | 6 (I2S_SCLK) |
| B | 38 | 8 (I2S_LRCK) |
| Pulsador | 14 | 2 (I2S_MCLK) |
| Común + segunda pata del pulsador | GND | — |

Se usan los pull-ups internos, así que no hacen falta resistencias. Si el giro va al revés,
intercambiá `ENCODER_PIN_A` y `ENCODER_PIN_B` en `variant.h`.

> ⛔ **No uses GPIO19 ni GPIO20**, aunque el header y la etiqueta de la placa los muestren como
> GPIO. Son las líneas D− / D+ del USB, y el USB es la única forma de flashear esta placa (UART0 lo
> usa la radio). Si los usás, la placa queda sin forma práctica de reflashear.

## Flashear un release ya compilado

Bajá `firmware-waveshare-epaper-154-inkhud-*.factory.bin` de la página de
[Releases](https://github.com/proto-hommer/mesh-paper-154/releases).

1. Poné la placa en modo descarga: **mantené BOOT apretado mientras enchufás el USB**.
2. Flasheá con [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`):

   ```sh
   esptool --chip esp32s3 --port COM5 write-flash 0x0 firmware-waveshare-epaper-154-inkhud-X.Y.Z.factory.bin
   ```

   Cambiá `COM5` por tu puerto (`/dev/ttyACM0` en Linux). También podés usar
   [ESP Tool](https://espressif.github.io/esptool-js/) desde el navegador, con la dirección `0x0`.
3. Desenchufá y volvé a enchufar la placa, emparejala con la app de Meshtastic y **configurá la
   región LoRa** antes que nada.

Para actualizar más adelante sin borrar la configuración, flasheá el `.bin` que no es factory en
`0x10000`.

## Compilar desde el código

Hace falta [PlatformIO](https://platformio.org/). Desde la raíz del repo:

```sh
pio run -e waveshare-epaper-154-inkhud
```

| Entorno | Qué es |
|---|---|
| `waveshare-epaper-154-inkhud` | Build normal, con InkHUD en el e-paper |
| `waveshare-epaper-154-headless` | Solo radio, sin pantalla. Sirve para aislar problemas |

El resultado queda en `.pio/build/<entorno>/`. La consola serie va por el USB nativo (CDC).

## Uso

Con el botón **BOOT** solo ya se puede usar el nodo, así que el encoder es opcional.

| Control | Acción |
|---|---|
| BOOT corto | Abrir/cerrar el menú. En el teclado: **enviar** |
| BOOT largo | Siguiente tile. En el teclado: cancelar |
| Girar el encoder | Cambiar de applet. Dentro de una lista, menú o el teclado: subir/bajar |
| Encoder corto | Entrar / elegir. Sobre un mensaje: responderlo |
| Encoder largo | Volver / entrar y salir del modo scroll en los mensajes |

Applets que agrega esta variante:

- **Nodes:** ordenada por último escuchado, con saltos y antigüedad. Cada nodo tiene una ficha con
  *Mensaje directo*, *Medir señal* (traceroute) y *Radar*.
- **Waypoints:** elegís un waypoint y ves la distancia y el rumbo. La brújula va **con el norte
  arriba**: la placa no tiene magnetómetro, así que no puede saber hacia dónde mirás.
- **Radar:** los 10 nodos más cercanos a escala, con una flecha al más cercano o al que fijaste.
- **Estado:** temperatura y humedad (SHTC3), batería, hora, vecinos directos, uso del canal y
  tiempo de aire.

Tu propia posición la toma del celular por Bluetooth.

### A batería

La placa tiene un latch de alimentación por hardware. Para prenderla a batería, **mantené PWR
apretado 1 o 2 segundos**, hasta que el firmware tome el control. Para apagarla, mantené PWR 3
segundos o usá *Save & Shut Down* en el menú. Con el USB enchufado anda siempre.

## Estado

Funcionando y en uso diario: radio, e-paper, InkHUD, botón BOOT, encoder y los applets nuevos.

Sin verificar del todo:

- Prender y apagar a batería, y el deep sleep con el latch.
- La lectura de batería no está calibrada.
- El GPS (NEO-6M en el conector del touch, GPIO21/7) está planeado pero todavía no tiene soporte.

Se aceptan issues y pull requests.

## Licencia

Basado en el [firmware de Meshtastic](https://github.com/meshtastic/firmware), con licencia GPL-3.0.
Esta variante se distribuye bajo la misma licencia.
