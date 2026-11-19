#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Consumer layer: translates abstract InputEvents into InkHUD actions.

This is the only place that knows about InkHUD. Input sources (encoder, future
joystick, I2C expander, ...) never call InkHUD directly: they only emit events
through dispatch(). Adding a source means writing a source class and registering
it in nicheGraphics.h; this navigator does not change.

*/

#include "InputEvent.h"

#include <cstring>

#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::Inputs
{

class InkHudNavigator
{
  public:
    static InkHudNavigator *getInstance()
    {
        static InkHudNavigator instance;
        return &instance;
    }

    void dispatch(InputEvent event, bool longPress = false)
    {
        InkHUD::InkHUD *h = InkHUD::InkHUD::getInstance();
        switch (event) {
        case InputEvent::UP:
            rotate(h, false);
            break;
        case InputEvent::DOWN:
            rotate(h, true);
            break;
        case InputEvent::LEFT:
            h->navLeft();
            break;
        case InputEvent::RIGHT:
            h->navRight();
            break;
        case InputEvent::SELECT:
            longPress ? h->longpress() : h->shortpress();
            break;
        case InputEvent::BACK:
            back(h, longPress);
            break;
        }
    }

  private:
    InkHudNavigator() = default;

    // UP/DOWN have no InkHUD fallback at the top level, so choose contextually: when a
    // system applet (menu, keyboard, ...) or a NAV-subscribed applet is active, move the
    // selection; otherwise switch applet. LEFT/RIGHT go straight to InkHUD, which already
    // does the right thing (grid keyboard, and prev/next applet at the top level).
    void rotate(InkHUD::InkHUD *h, bool down)
    {
        if (navConsumerActive(h, down))
            down ? h->navDown() : h->navUp();
        else
            down ? h->nextApplet() : h->prevApplet();
    }

    bool navConsumerActive(InkHUD::InkHUD *h, bool down)
    {
        for (InkHUD::SystemApplet *sa : h->systemApplets)
            if (sa->handleInput)
                return true;
        InkHUD::Applet *active = h->getActiveApplet();
        InkHUD::Applet::InputMask want = down ? InkHUD::Applet::NAV_DOWN : InkHUD::Applet::NAV_UP;
        return active && active->isInputSubscribed(want);
    }

    // BACK is the BOOT-button role: inside the keyboard it sends (or cancels on long press),
    // inside the menu it closes, and at the top level it opens the menu.
    void back(InkHUD::InkHUD *h, bool longPress)
    {
        if (foreground(h, "Keyboard")) {
            longPress ? h->freeTextCancel() : h->freeTextDone();
            h->closeKeyboard();
        } else if (longPress) {
            h->exitShort();
        } else if (foreground(h, "Menu")) {
            h->exitShort();
        } else {
            h->openMenu();
        }
    }

    // Note: InkHUD::getSystemApplet() asserts if the name is missing, so look it up ourselves
    // and treat "not present" (e.g. no keyboard) as "not in the foreground".
    bool foreground(InkHUD::InkHUD *h, const char *name)
    {
        for (InkHUD::SystemApplet *sa : h->systemApplets)
            if (sa->name && strcmp(sa->name, name) == 0)
                return sa->isForeground();
        return false;
    }
};

} // namespace NicheGraphics::Inputs

#endif
