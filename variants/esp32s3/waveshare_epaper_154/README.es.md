<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="img/calandriapp-logo-oscuro.svg">
    <source media="(prefers-color-scheme: light)" srcset="img/calandriapp-logo-claro.svg">
    <img alt="CalandriApp" src="img/calandriapp-logo-claro.svg" width="200">
  </picture>
</p>

# Calandria MK1 epaper: Meshtastic en Waveshare ESP32-S3-ePaper-1.54 + Wio-SX1262

*[Read in English](README.md)*

Variante propia de Meshtastic para la **Waveshare ESP32-S3-ePaper-1.54** (V2, sin touch) con un
módulo LoRa **Seeed Wio-SX1262 for XIAO** cableado a mano. Usa InkHUD en el e-paper de 1,54" y
suma un encoder rotativo EC11 opcional, con interfaz propia: lista de nodos, mensajes directos,
respuestas, teclado en pantalla, brújula de waypoints, radar de nodos cercanos y pantalla de estado.

> Es un build comunitario no oficial, sin soporte de Meshtastic ni de Waveshare.
>
> **Alpha, en desarrollo.** La radio, la pantalla y el encoder andan y se usan todos los días, pero
> todavía es un prototipo y hay partes sin probar (sobre todo a batería). Cuando estén el **módulo GPS**
> y la **batería 18650 integrada con su TP4056**, se va a diseñar una **carcasa impresa en 3D**.
> App compañera: [CalandriApp](https://github.com/proto-hommer/calandriapp).

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

## Versiones

El Calandria tiene su propio número de versión, aparte del de Meshtastic. El firmware está hecho
sobre **Meshtastic 2.8.3**, y eso es lo que el nodo les informa a las apps. La versión del
Calandria es semántica: **0.x** mientras está en alpha, sube el último número con los arreglos y
el del medio con funciones nuevas, y llega a 1.0.0 cuando deje de ser alpha. Los releases se
etiquetan `calandria-X.Y.Z`.

Los primeros releases usaban otro esquema. Sus tags quedan como están:

| Tag viejo | Calandria |
|---|---|
| `v2.8.3-ws154.1` | 0.1.0 |
| `v2.8.3-ws154.2` | 0.2.0 |
| `v2.8.3-ws154.3` | 0.3.0 |
| `v2.8.3-ws154.4` | 0.4.0 |
| `v2.8.3-ws154.5` | 0.5.0 |

El nodo muestra su versión en *Estado*. Con CalandriApp conectada (1.8.5 o posterior), *Estado*
también muestra "Firmware nuevo: X.Y.Z" cuando hay un release más nuevo (el nodo no tiene
internet: se lo dice la app), y la hora pasa a la zona horaria del celular enseguida, sin reiniciar.

### Lo nuevo de la 0.6.0

- **Versión propia:** la versión del Calandria se ve en *Estado* y viaja en el beacon de
  CalandriApp (`ver=`).
- **Aviso de firmware nuevo:** *Estado* muestra "Firmware nuevo: X.Y.Z" cuando CalandriApp avisa
  que hay un release más nuevo.
- **Zona horaria al instante:** la zona horaria del celular se aplica apenas la manda la app, sin
  reiniciar.
- **Limpiar fantasmas:** respeta favoritos e ignorados, y escribe la NodeDB en flash una sola vez.

## Flashear un release ya compilado

**Recomendado: el [web flasher](https://proto-hommer.github.io/calandria-flasher/)** (Chrome o Edge
en una computadora, con el cable USB). Siempre instala el último release. *Actualizar* conserva la
configuración; *Instalación limpia* borra todo.

Para flashear a mano, bajá
`firmware-calandria-mk1-epaper-*.factory.bin` de la página de
[Releases](https://github.com/proto-hommer/calandria-mk1-epaper/releases).

1. Poné la placa en modo descarga: **mantené BOOT apretado mientras enchufás el USB**.
2. Flasheá con [esptool](https://docs.espressif.com/projects/esptool/) (`pip install esptool`):

   ```sh
   esptool --chip esp32s3 --port COM5 write-flash 0x0 firmware-calandria-mk1-epaper-X.Y.Z.factory.bin
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
pio run -e calandria-mk1-epaper
```

| Entorno | Qué es |
|---|---|
| `calandria-mk1-epaper` | Build normal, con InkHUD en el e-paper |
| `calandria-mk1-epaper-headless` | Solo radio, sin pantalla. Sirve para aislar problemas |

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
  *Mensaje directo*, *Medir señal* (traceroute), *Radar* y *Olvidar nodo* (pulsando dos veces, sin
  reiniciar).
- **Nodos fantasma:** al reflashear o cambiar la placa, un nodo puede cambiar de número y el viejo
  queda en la lista con el mismo nombre. *Menú → Node Config → Reset NodeDB → Limpiar fantasmas*
  olvida los duplicados más viejos (mismo nombre largo, oídos antes) y los que no se oyen hace más
  de 7 días, sin reiniciar. Los favoritos y los ignorados no se tocan. Es el mismo criterio que *Limpiar fantasmas* de CalandriApp.
- **Waypoints:** elegís un waypoint y ves la distancia y el rumbo. La brújula va **con el norte
  arriba**: la placa no tiene magnetómetro, así que no puede saber hacia dónde mirás.
- **Radar:** los 10 nodos más cercanos a escala, con una flecha al más cercano o al que fijaste.
- **Estado:** temperatura y humedad (SHTC3), batería, hora, vecinos directos, uso del canal,
  tiempo de aire, la versión del Calandria y si hay una más nueva.

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
