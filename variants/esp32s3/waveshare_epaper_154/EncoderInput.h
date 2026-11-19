#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

EC11 rotary encoder -> InkHUD navigation (variant-local input source).

InkHUD excludes the InputBroker, so this does not go through the standard
Meshtastic encoder support. The A/B quadrature is decoded in a GPIO ISR; the
decoded steps are turned into InkHUD nav calls from the OSThread (main loop),
never from the ISR. The encoder push-button and the BOOT button are handled by
the shared TwoButton input.

Rotation is context aware (chosen with InkHUD's public API, no shared edits):
  - a system applet (menu, keyboard, ...) is consuming input,  OR the foreground
    user applet subscribes to NAV  -> turn = navDown / navUp
  - otherwise (plain applet view)                              -> turn = next / prev applet

Buttons:
  - encoder press: short = select (shortpress), long = longpress (keyboard caps)
  - BOOT:          keyboard -> send / cancel, menu -> close, elsewhere -> open menu

Define ENCODER_SIM to drive it over the USB serial console without hardware.

*/

#include "concurrency/OSThread.h"
#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

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
        inkhud = InkHUD::InkHUD::getInstance();

        pinMode(ENCODER_PIN_A, INPUT_PULLUP);
        pinMode(ENCODER_PIN_B, INPUT_PULLUP);
        quadState = readQuad();

        attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_A), isr, CHANGE);
        attachInterrupt(digitalPinToInterrupt(ENCODER_PIN_B), isr, CHANGE);

        setInterval(POLL_MS);
        enabled = true;
    }

    // Button handlers, wired to the shared TwoButton in nicheGraphics.h.
    // Also reused by the serial simulator.
    void pressShort() { inkhud->shortpress(); }
    void pressLong() { inkhud->longpress(); }

    void bootShort()
    {
        if (keyboardForeground()) {
            inkhud->freeTextDone(); // BOOT = send inside the keyboard
            inkhud->closeKeyboard();
        } else if (menuForeground()) {
            inkhud->exitShort(); // BOOT = close the menu
        } else {
            inkhud->openMenu(); // BOOT = open the menu
        }
    }

    void bootLong()
    {
        if (keyboardForeground()) {
            inkhud->freeTextCancel(); // long BOOT = discard the draft
            inkhud->closeKeyboard();
        } else {
            inkhud->exitShort(); // long BOOT = back / close
        }
    }

    // Clockwise = next / down, counter-clockwise = previous / up.
    void rotate(bool clockwise)
    {
        if (navConsumerActive(clockwise))
            clockwise ? inkhud->navDown() : inkhud->navUp();
        else
            clockwise ? inkhud->nextApplet() : inkhud->prevApplet();
    }

  protected:
    int32_t runOnce() override
    {
#ifdef ENCODER_SIM
        drainSerialSim();
#endif
        // Consume the steps the ISR decoded since last time.
        int8_t steps = pendingSteps;
        pendingSteps = 0;
        while (steps > 0) {
            rotate(true);
            steps--;
        }
        while (steps < 0) {
            rotate(false);
            steps++;
        }
        return POLL_MS;
    }

  private:
    static constexpr uint32_t POLL_MS = 20;

    EncoderInput() : OSThread("EncoderInput") {}

    InkHUD::InkHUD *inkhud = nullptr;
    uint8_t quadState = 0b11; // last (A<<1 | B); rest state is 0b11 (both pulled up)
    int8_t quadAccum = 0;     // sub-step accumulator between detents
    volatile int8_t pendingSteps = 0;

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
                pendingSteps++;
            else if (quadAccum <= -2)
                pendingSteps--;
            quadAccum = 0;
        }
    }

    // Mirror of Events.cpp: is any system applet currently consuming input?
    bool systemAppletHandlingInput()
    {
        for (InkHUD::SystemApplet *sa : inkhud->systemApplets)
            if (sa->handleInput)
                return true;
        return false;
    }

    bool navConsumerActive(bool clockwise)
    {
        if (systemAppletHandlingInput())
            return true;
        InkHUD::Applet *active = inkhud->getActiveApplet();
        InkHUD::Applet::InputMask want = clockwise ? InkHUD::Applet::NAV_DOWN : InkHUD::Applet::NAV_UP;
        return active && active->isInputSubscribed(want);
    }

    bool systemAppletForeground(const char *name)
    {
        InkHUD::SystemApplet *sa = inkhud->getSystemApplet(name);
        return sa && sa->isForeground();
    }
    bool keyboardForeground() { return systemAppletForeground("Keyboard"); }
    bool menuForeground() { return systemAppletForeground("Menu"); }

#ifdef ENCODER_SIM
    // Test-only: inject encoder events from the USB serial console.
    //   d / .  turn clockwise (next / down)      a / ,  turn counter-clockwise (prev / up)
    //   s / space / enter  encoder press         l      encoder long-press
    //   m      BOOT short (menu)                  x      BOOT long (back)
    void drainSerialSim()
    {
        while (Serial.available()) {
            switch (Serial.read()) {
            case 'd':
            case '.':
                rotate(true);
                break;
            case 'a':
            case ',':
                rotate(false);
                break;
            case 's':
            case ' ':
            case '\r':
            case '\n':
                pressShort();
                break;
            case 'l':
                pressLong();
                break;
            case 'm':
                bootShort();
                break;
            case 'x':
                bootLong();
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
