#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Input source: a hand-wired EC11 rotary encoder.

Decodes the A/B quadrature in a GPIO ISR and emits abstract UP/DOWN events to
InkHudNavigator from the OSThread (never from the ISR). The encoder push-button
and BOOT are wired separately (in nicheGraphics.h) and emit SELECT / BACK, so
this source only owns the rotation.

It knows nothing about InkHUD: swapping in a different source (5-way joystick,
I2C expander) is a new class plus a line in nicheGraphics.h, with no change to
the navigator.

Define ENCODER_SIM to inject events over the USB serial console without hardware.

*/

#include "InkHudNavigator.h"
#include "InputEvent.h"

#include "concurrency/OSThread.h"

#include <atomic>

namespace NicheGraphics::Inputs
{

// Quadrature transition table: index is (previous<<2 | current) Gray code, value is the step direction.
static DRAM_ATTR int8_t ENC_QUAD_TABLE[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};

class EncoderInput : protected concurrency::OSThread
{
  public:
    static EncoderInput *getInstance()
    {
        static EncoderInput instance;
        return &instance;
    }

    // Wire the GPIOs and start polling. Call after InkHUD::begin().
    void begin()
    {
        nav = InkHudNavigator::getInstance();

        pinMode(ENCODER_PIN_A, INPUT_PULLUP);
        pinMode(ENCODER_PIN_B, INPUT_PULLUP);
        quadState = readQuad();

        attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), isr, CHANGE);
        attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), isr, CHANGE);

        setInterval(POLL_MS);
        enabled = true;
    }

  protected:
    int32_t runOnce() override
    {
#ifdef ENCODER_SIM
        drainSerialSim();
#endif
        // Pull whatever the ISR decoded since last time (clockwise = DOWN, counter = UP).
        // exchange() reads and clears in one step: an edge between a read and a clear is never lost.
        int8_t s = (int8_t)pendingSteps.exchange(0);
        if (s != 0) {
            accumSteps += s;
            lastStepMs = millis();
        }

        // Defer delivery only while the keyboard is foreground: the e-paper is too slow to
        // redraw every key, so wait for the turning to settle, then move in one burst (InkHUD
        // coalesces the redraws into one). Everywhere else, deliver immediately for a responsive feel.
        if (accumSteps != 0) {
            const bool defer = nav->keyboardForeground() && (millis() - lastStepMs) < KEYBOARD_COALESCE_MS;
            if (!defer) {
                int8_t d = accumSteps;
                accumSteps = 0;
                while (d > 0) {
                    nav->dispatch(InputEvent::DOWN);
                    d--;
                }
                while (d < 0) {
                    nav->dispatch(InputEvent::UP);
                    d++;
                }
            }
        }
        return POLL_MS;
    }

  private:
    static constexpr uint32_t POLL_MS = 20;
    static constexpr uint32_t KEYBOARD_COALESCE_MS = ENCODER_KEYBOARD_COALESCE_MS;

    EncoderInput() : OSThread("EncoderInput") {}

    InkHudNavigator *nav = nullptr;
    uint8_t quadState = 0b11; // last (A<<1 | B); rest state is 0b11 (both pulled up)
    int8_t quadAccum = 0;     // sub-step accumulator between detents
    std::atomic<int32_t> pendingSteps{0}; // written by the ISR, drained by runOnce (lock-free on S3)

    int8_t accumSteps = 0;    // steps waiting to be delivered (coalesced for the keyboard)
    uint32_t lastStepMs = 0;  // millis() of the most recent step, for the coalesce window

    static uint8_t readQuad() { return (digitalRead(ENCODER_PIN_A) << 1) | digitalRead(ENCODER_PIN_B); }

    static void IRAM_ATTR isr() { getInstance()->onEdge(); }

    void IRAM_ATTR onEdge()
    {
        uint8_t s = readQuad();
        quadState = ((quadState << 2) | s) & 0x0F;
        quadAccum += ENC_QUAD_TABLE[quadState];
        // A detent completes when we land back on the rest state (both high).
        if (s == 0b11) {
            if (quadAccum >= 2)
                pendingSteps.fetch_add(1);
            else if (quadAccum <= -2)
                pendingSteps.fetch_sub(1);
            quadAccum = 0;
        }
    }

#ifdef ENCODER_SIM
    // Test-only: inject events from the USB serial console.
    //   d / .  DOWN      a / ,  UP        e  RIGHT      q  LEFT
    //   s / space / enter  SELECT         l  SELECT (long)
    //   m      BACK (BOOT short)          x  BACK (long)
    void drainSerialSim()
    {
        while (Serial.available()) {
            switch (Serial.read()) {
            case 'd':
            case '.':
                nav->dispatch(InputEvent::DOWN);
                break;
            case 'a':
            case ',':
                nav->dispatch(InputEvent::UP);
                break;
            case 'e':
                nav->dispatch(InputEvent::RIGHT);
                break;
            case 'q':
                nav->dispatch(InputEvent::LEFT);
                break;
            case 's':
            case ' ':
            case '\r':
            case '\n':
                nav->dispatch(InputEvent::SELECT);
                break;
            case 'l':
                nav->dispatch(InputEvent::SELECT, true);
                break;
            case 'm':
                nav->dispatch(InputEvent::BACK);
                break;
            case 'x':
                nav->dispatch(InputEvent::BACK, true);
                break;
            default:
                break;
            }
        }
    }
#endif
};

} // namespace NicheGraphics::Inputs

#endif
