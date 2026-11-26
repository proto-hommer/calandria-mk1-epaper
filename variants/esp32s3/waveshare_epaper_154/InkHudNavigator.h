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

#include "AppletNames.h"
#include "HubApplet.h"
#include "InputEvent.h"

#include <Arduino.h>
#include <cstdlib>
#include <cstring>

#include "graphics/niche/InkHUD/Applet.h"
#include "MenuAppletInclude.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "mesh/MeshTypes.h"

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
            select(h, longPress);
            break;
        case InputEvent::BACK:
            back(h, longPress);
            break;
        }
        noteShown(h);
    }

    // Used by EncoderInput to decide when to defer the on-screen keyboard redraw.
    bool keyboardForeground() { return foreground(InkHUD::InkHUD::getInstance(), "Keyboard"); }

  private:
    InkHudNavigator() = default;

    // Two levels, the same gestures everywhere (HubApplet.h is the top):
    //   turn:          move through the list / content of what is on screen (never changes applet)
    //   encoder short: enter / choose (message applets: reply to the latest message)
    //   encoder long:  the applet's own long action if it has one, otherwise the same as BOOT
    //   BOOT short:    back one level: applet -> its card -> Inicio. At Inicio: the InkHUD menu
    //   BOOT long:     back to Inicio from anywhere (keyboard: cancel)
    // Our applets subscribe EXIT_SHORT / BUTTON_LONG only while they have a level of their own to
    // close (a node card, a compass), so BOOT reaches the navigator when they are at their top.
    // A new message pops up on its own (autoshow): BOOT there goes back to where you were.

    // The user applet on screen after the last input. If another one is on screen at the next input,
    // it came up by itself (autoshow).
    InkHUD::Applet *lastShown = nullptr;

    bool isUserApplet(InkHUD::InkHUD *h, InkHUD::Applet *a)
    {
        for (InkHUD::Applet *u : h->userApplets)
            if (u == a)
                return true;
        return false;
    }

    void noteShown(InkHUD::InkHUD *h)
    {
        InkHUD::Applet *a = h->getActiveApplet();
        if (!systemOwnsInput(h) && isUserApplet(h, a))
            lastShown = a;
    }

    void rotate(InkHUD::InkHUD *h, bool down)
    {
        InkHUD::Applet *active = h->getActiveApplet();
        InkHUD::Applet::InputMask want = down ? InkHUD::Applet::NAV_DOWN : InkHUD::Applet::NAV_UP;
        if (!systemOwnsInput(h) && !(active && active->isInputSubscribed(want)))
            return; // nothing to move through here
        down ? h->navDown() : h->navUp();
    }

    bool systemOwnsInput(InkHUD::InkHUD *h)
    {
        for (InkHUD::SystemApplet *sa : h->systemApplets)
            if (sa->handleInput)
                return true;
        return false;
    }

    void select(InkHUD::InkHUD *h, bool longPress)
    {
        InkHUD::Applet *active = h->getActiveApplet();
        if (systemOwnsInput(h) || !active) {
            longPress ? h->longpress() : h->shortpress();
            return;
        }
        if (longPress) {
            if (active->isInputSubscribed(InkHUD::Applet::BUTTON_LONG))
                h->longpress();
            else
                back(h, false);
            return;
        }
        if (active->isInputSubscribed(InkHUD::Applet::BUTTON_SHORT))
            h->shortpress();
        else if (isMessageApplet(active))
            reply(h, active);
        // otherwise nothing to choose here (InkHUD's default would open the menu)
    }

    static bool isMessageApplet(InkHUD::Applet *a)
    {
        using namespace AppletNames;
        return a && a->name &&
               (!strcmp(a->name, ALL_MESSAGES) || !strcmp(a->name, DMS) || !strncmp(a->name, CHANNEL_PREFIX, CHANNEL_PREFIX_LEN));
    }

    static bool nameIs(InkHUD::Applet *a, const char *n) { return a->name && strcmp(a->name, n) == 0; }

    // Reply to the latest message this applet shows: a DM goes back to its sender as a DM, a
    // channel message is answered on that channel. Reuses MenuApplet::composeTo (keyboard + send).
    bool reply(InkHUD::InkHUD *h, InkHUD::Applet *a)
    {
        const auto &latest = h->persistence->latestMessage;
        NodeNum dest;
        uint8_t channel;
        const bool haveDm = latest.dm.sender != 0, haveBc = latest.broadcast.sender != 0;
        const bool all = nameIs(a, AppletNames::ALL_MESSAGES);
        if (nameIs(a, AppletNames::DMS) || (all && haveDm && (!latest.wasBroadcast || !haveBc))) {
            if (!haveDm)
                return false;
            dest = latest.dm.sender;
            channel = latest.dm.channelIndex;
        } else if (all) {
            if (!haveBc)
                return false;
            dest = NODENUM_BROADCAST;
            channel = latest.broadcast.channelIndex;
        } else {
            dest = NODENUM_BROADCAST;
            channel = (uint8_t)atoi(a->name + AppletNames::CHANNEL_PREFIX_LEN); // "Canal N..."
        }
        h->openMenu();
        InkHUD::MenuApplet *menu = (InkHUD::MenuApplet *)h->getSystemApplet("Menu");
        if (!menu)
            return false;
        menu->composeTo(dest, channel);
        return true;
    }

    void back(InkHUD::InkHUD *h, bool longPress)
    {
        InkHUD::HubApplet *hub = InkHUD::HubApplet::get();
        if (foreground(h, "Keyboard")) {
            longPress ? h->freeTextCancel() : h->freeTextDone();
            h->closeKeyboard();
            return;
        }
        if (longPress) {
            if (systemOwnsInput(h))
                h->exitShort(); // close the menu (or whatever system applet is up) first
            if (hub)
                hub->goHome();
            return;
        }
        if (systemOwnsInput(h) || activeWants(h, InkHUD::Applet::EXIT_SHORT)) {
            h->exitShort(); // menu: close it; applet: close its own inner level
            return;
        }
        InkHUD::Applet *active = h->getActiveApplet();
        if (!hub || active == hub) {
            h->openMenu(); // top level: the InkHUD menu (settings, shutdown...)
            return;
        }
        if (lastShown && lastShown != active && isUserApplet(h, lastShown) && lastShown->isActive()) {
            hub->show(lastShown); // it popped up by itself: back to where you were, as you left it
            return;
        }
        hub->back(active);
    }

    bool activeWants(InkHUD::InkHUD *h, InkHUD::Applet::InputMask mask)
    {
        for (InkHUD::SystemApplet *sa : h->systemApplets)
            if (sa->handleInput)
                return false; // a system applet owns input, not the user applet
        InkHUD::Applet *active = h->getActiveApplet();
        return active && active->isInputSubscribed(mask);
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
