#pragma once

// Waveshare ESP32-S3-ePaper-1.54, no touch. Wio-SX1262 is hand-wired.
// The microSD pads are the radio. Nothing here may start an SD card.

#define HAS_GPS 0
#define GPS_DEFAULT_NOT_PRESENT 1

#define USE_SX1262

#define LORA_SCK 1
#define LORA_MISO 44
#define LORA_MOSI 43
#define LORA_CS 2
#define LORA_RESET 41
#define LORA_DIO1 39

#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY 40
#define SX126X_RESET LORA_RESET
// DIO2 is the chip RF switch. RXEN is the RF_SW wire on GPIO3. DIO3 is the 1.8 V TCXO.
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_RXEN 3
#define SX126X_TXEN RADIOLIB_NC
#define SX126X_DIO3_TCXO_VOLTAGE 1.8

#define PIN_EINK_CS 11
#define PIN_EINK_DC 10
#define PIN_EINK_RES 9
#define PIN_EINK_BUSY 8
#define PIN_EINK_SCLK 12
#define PIN_EINK_MOSI 13
#define PIN_EINK_EN 6
// PMOS on the panel rail, active low. The panel refreshes with GPIO6 held low.
#define EINK_EN_ACTIVE LOW

#define BUTTON_PIN 0
#define BUTTON_NEED_PULLUP

// EC11 rotary encoder, soldered to the pads of the removed ES8311 codec (unused I2S lines
// SCLK/LRCK/MCLK); audio is disabled so nothing else drives them. All active low,
// read with the internal pull-ups. Never move these to GPIO19/20 (USB D-/D+).
#define ENCODER_PIN_A 15
#define ENCODER_PIN_B 38
#define ENCODER_PIN_PRESS 14

// On-screen keyboard: accumulate encoder turns and redraw only this long after the last
// click, so the slow e-paper refreshes once per burst instead of once per key.
#define ENCODER_KEYBOARD_COALESCE_MS 200

#define I2C_SDA 47
#define I2C_SCL 48
#define PCF85063_RTC 0x51
#define HAS_RTC 1

// GPIO4 is ADC1 channel 3. Divider is 200K/200K.
#define BATTERY_PIN 4
#define ADC_CHANNEL ADC_CHANNEL_3
#define ADC_MULTIPLIER 2.0
// No VBUS sense pin (the ETA6098 STAT only drives the LED). Without this, "USB / charging" is
// guessed as battery > 4.20 V, which a full, calibrated cell (target 4.19 V) crosses on noise:
// "charging" forever, on battery too. Ask the native USB port instead (sees a USB host: a PC or
// phone; a dumb wall charger reads as unplugged).
#define USB_HOST_PWR_DETECT

// Battery power latch (CLAUDE.md section 4). HIGH keeps VBAT connected to VSYS after PWR is
// released; LOW is a real power-off. Driven + hold-latched in variant.cpp.
#define BAT_CONTROL_PIN 17
// PWR button, active LOW (10K pull-up to 3V3). Read only, not used yet.
#define BAT_KEY_PIN 18
