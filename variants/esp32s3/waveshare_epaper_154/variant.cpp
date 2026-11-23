#include "configuration.h"
#include "CalandriaBeacon.h"
#include "concurrency/OSThread.h"
#include "main.h"
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
// Power-off: src/sleep.cpp releases the latch (LOW) on "deep sleep forever", which is what
// Power::shutdown() ends in. Both the InkHUD menu "Save & Shut Down" and the PWR long-press below
// go through that same path (banner, save NodeDB / messages / waypoints, then latch off).
//
// earlyInitVariant() runs before consoleInit(): NO LOG_* here (it crashes).
void earlyInitVariant()
{
    gpio_hold_dis((gpio_num_t)BAT_CONTROL_PIN); // release a hold left by deep sleep, so we can drive it
    pinMode(BAT_CONTROL_PIN, OUTPUT);
    digitalWrite(BAT_CONTROL_PIN, HIGH);
    gpio_hold_en((gpio_num_t)BAT_CONTROL_PIN);
}

#ifndef BAT_KEY_LONGPRESS_MS
#define BAT_KEY_LONGPRESS_MS 3000
#endif

// PWR button (BAT_KEY_PIN, active LOW): hold it BAT_KEY_LONGPRESS_MS to shut down.
// The press used to power the board on is still held at boot, so the key must be seen released
// once before a long-press counts.
class PowerKeyThread : public concurrency::OSThread
{
  public:
    PowerKeyThread() : concurrency::OSThread("PowerKey") { pinMode(BAT_KEY_PIN, INPUT); } // 10K external pull-up

  protected:
    int32_t runOnce() override
    {
        const bool pressed = digitalRead(BAT_KEY_PIN) == LOW;
        const uint32_t now = millis();
        if (!pressed) {
            armed = true;
            pressedSince = 0;
        } else if (armed) {
            if (!pressedSince)
                pressedSince = now;
            else if (now - pressedSince >= BAT_KEY_LONGPRESS_MS && !shutdownAtMsec) {
                LOG_INFO("PWR long-press: shutting down");
                shutdownAtMsec = now ? now : 1; // handled by Power::runOnce -> Power::shutdown()
                armed = false;
            }
        }
        return 50;
    }

  private:
    bool armed = false;
    uint32_t pressedSince = 0;
};

// initDeepSleep() (main.cpp, after earlyInitVariant) releases every RTC pad hold on wake from
// deep sleep. The pad stays HIGH (output register), but re-arm the hold for the next sleep/reset.
void lateInitVariant()
{
    digitalWrite(BAT_CONTROL_PIN, HIGH);
    gpio_hold_en((gpio_num_t)BAT_CONTROL_PIN);
    LOG_INFO("Battery latch: GPIO%d held HIGH", BAT_CONTROL_PIN);

    new PowerKeyThread();

    // "I am a Calandria MK1 epaper" on a private port, for CalandriApp (both builds: not a UI feature)
    new CalandriaBeacon();
}
