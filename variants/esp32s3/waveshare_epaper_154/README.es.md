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
suma un encoder rotativo EC11, con interfaz propia: un Inicio con cuatro carátulas (mensajes, nodos,
rumbo y estado), conversaciones, medición de señal, brújulas, radar de nodos cercanos y WiFi.

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
| Encoder rotativo EC11 con pulsador | 20 pasos por vuelta |

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

### Encoder rotativo

| Pata del encoder | GPIO | Pad del ES8311 (codec retirado) |
|---|---|---|
| A | 15 | 6 (I2S_SCLK) |
| B | 38 | 8 (I2S_LRCK) |
| Pulsador | 14 | 2 (I2S_MCLK) |
| Común + segunda pata del pulsador | GND | — |

Hace falta para moverse por el Inicio y las carátulas: solo con BOOT se llega al menú de InkHUD.
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

El nodo muestra su versión en *ESTADO → Firmware*. Con CalandriApp conectada (1.8.5 o posterior)
dice "al día", "hay X.Y.Z" cuando hay un release más nuevo (se lo dice la app) o "de prueba" si el
nodo tiene una versión más nueva que la publicada. La hora pasa a la zona horaria del celular
enseguida, sin reiniciar.

### Lo nuevo de la 0.9.0

- **Mensajes directos:** los que mandás desde el MK1 (respuesta rápida o teclado) se ven en la
  conversación y pasan de "enviando" a "entregado" (o "no entregado") con el ACK. El historial guarda
  50 mensajes en vez de 20, así un canal con mucho tráfico no se lleva los directos.
- **WiFi:** se prende y se apaga desde *ESTADO → WiFi*, con salida de emergencia (ver [WiFi](#wifi)).

La 0.7.0 trajo la navegación por carátulas, y la 0.8.0, arreglos sobre ella.

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

El MK1 arranca en **Inicio**: cuatro carátulas, cada una con un resumen y un contador. Hace falta
el encoder para moverse. Solo con BOOT se llega al menú de InkHUD.

| Gesto | Hace |
|---|---|
| Girar el encoder | Moverse por la lista o el contenido |
| Clic del encoder | Entrar / elegir |
| BOOT | Volver un nivel. En el Inicio: el menú completo de InkHUD. En el teclado: **enviar** |
| Mantener BOOT | Volver al Inicio desde cualquier lado. En el teclado: cancelar |
| Mantener el encoder | Lo mismo que BOOT, salvo donde la pantalla tiene su propia acción |

- **MENSAJES:** las conversaciones: *Grupo* (canal 0), *Público* (canal 1), los demás canales en
  uso y un directo por persona, con ● si hay sin leer. Clic abre la conversación (remitente, hora,
  saltos; en los tuyos "✓ entregado"). Clic sobre un mensaje: responder con una respuesta rápida o
  con el teclado. Un mensaje nuevo aparece solo; BOOT te vuelve a donde estabas.
- **NODOS:** todos los nodos, el último oído arriba, con distancia, rumbo y saltos. Los perdidos
  van tachados (1 h sin oírlos; 13 h si son ROUTER). Al final, el **Radar** con los 10 más cerca.
  La ficha de cada nodo tiene *Mandarle un mensaje*, *Medir señal* (traceroute: ida y vuelta con el
  SNR de cada tramo y el más flojo), *Rastrearlo* (brújula), *Marcar favorito* y *Olvidar nodo*.
- **RUMBO:** *Rastrear un nodo*, *Waypoints* (del más cerca al más lejos), *Mapa del grupo* y
  *Mi posición*. La brújula va **con el norte arriba**: la placa no tiene magnetómetro, así que no
  puede saber hacia dónde mirás. Tu posición la toma del celular por Bluetooth.
- **ESTADO:** batería, uso del canal, temperatura y humedad (SHTC3) y versión. Abajo: *Ajustes*
  (ahorro de energía, girar pantalla, hora local, limpiar fantasmas, reiniciar, menú completo),
  *WiFi*, *Bluetooth · PIN* y *Apagar*.
- **Nodos fantasma:** al reflashear o cambiar la placa, un nodo puede cambiar de número y el viejo
  queda en la lista con el mismo nombre. *ESTADO → Ajustes → Limpiar fantasmas* olvida los
  duplicados más viejos (mismo nombre largo, oídos antes) y los que no se oyen hace más de 7 días,
  sin reiniciar. Los favoritos y los ignorados no se tocan. Es el mismo criterio que *Limpiar
  fantasmas* de CalandriApp.

### WiFi

Sirve para dejar el MK1 en casa o en una base fija con conexión. **Mientras el WiFi está prendido,
el Bluetooth queda apagado** (en el ESP32 no andan juntos), así que la app no se puede conectar por
Bluetooth.

- La red y la clave se cargan desde la app. Con la perilla no se escriben.
- *ESTADO → WiFi* muestra la red, si está conectado (con la IP) y la hora por internet. *Prender
  WiFi* pide confirmación. Prender o apagar el WiFi reinicia el nodo. Sin una red cargada no se
  puede prender.
- **Salida de emergencia**, para que nunca quede sin WiFi y sin Bluetooth:
  - Si a los 3 minutos de arrancar el WiFi no conectó, el Inicio muestra "WiFi sin conexión".
    Un clic lo apaga y vuelve el Bluetooth.
  - Sin mirar la pantalla: **mantené apretada la perilla del encoder mientras lo prendés o lo
    enchufás**. Arranca con el WiFi apagado y el Bluetooth andando. (BOOT no sirve: apretado al
    enchufar pone la placa en modo de grabación.)
- Actualizar con el web flasher anda igual con el WiFi prendido: es por USB.

### A batería

La placa tiene un latch de alimentación por hardware. Para prenderla a batería, **mantené PWR
apretado 1 o 2 segundos**, hasta que el firmware tome el control. Para apagarla, mantené PWR 3
segundos o usá *ESTADO → Apagar*. Con el USB enchufado anda siempre.

## Estado

Funcionando y en uso diario: radio, e-paper, InkHUD, botón BOOT, encoder, el Inicio con las
carátulas y los mensajes de canal.

Sin probar en la placa todavía: los mensajes directos de la 0.9.0 y el WiFi.

Sin verificar del todo:

- Prender y apagar a batería, y el deep sleep con el latch.
- La lectura de batería no está calibrada.
- El GPS (NEO-6M en el conector del touch, GPIO21/7) está planeado pero todavía no tiene soporte.

Se aceptan issues y pull requests.

## Licencia

Basado en el [firmware de Meshtastic](https://github.com/meshtastic/firmware), con licencia GPL-3.0.
Esta variante se distribuye bajo la misma licencia.
