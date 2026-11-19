#include "configuration.h"
#include <Arduino.h>
#include <driver/gpio.h>

// Battery power latch (see CLAUDE.md section 4).
// The PWR button connects VBAT to VSYS only while pressed. Driving BAT_CONTROL_PIN HIGH saturates
// T2 and keeps Q4's gate low, so the board stays powered after the button is released.
//
// The pin is also HOLD-latched: the pad keeps its level through deep sleep and software resets
// (esp_restart after a config change). Without the hold, the pin would float during sleep/reset,
// Q4 would open and the board would turn off completely on battery, with no way to wake up.
//
// earlyInitVariant() runs before consoleInit(): NO LOG_* here (it crashes).
void earlyInitVariant()
{
    gpio_hold_dis((gpio_num_t)BAT_CONTROL_PIN); // release a hold left by deep sleep, so we can drive it
    pinMode(BAT_CONTROL_PIN, OUTPUT);
    digitalWrite(BAT_CONTROL_PIN, HIGH);
    gpio_hold_en((gpio_num_t)BAT_CONTROL_PIN);
}

// initDeepSleep() (main.cpp, after earlyInitVariant) releases every RTC pad hold on wake from
// deep sleep. The pad stays HIGH (output register), but re-arm the hold for the next sleep/reset.
void lateInitVariant()
{
    digitalWrite(BAT_CONTROL_PIN, HIGH);
    gpio_hold_en((gpio_num_t)BAT_CONTROL_PIN);
    LOG_INFO("Battery latch: GPIO%d held HIGH", BAT_CONTROL_PIN);
}
