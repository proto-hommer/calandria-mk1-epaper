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
    }

    // Used by EncoderInput to decide when to defer the on-screen keyboard redraw.
    bool keyboardForeground() { return foreground(InkHUD::InkHUD::getInstance(), "Keyboard"); }

  private:
    InkHudNavigator() = default;

    // Stock InkHUD applets (Heard, Recents, messages, ...) subscribe NAV permanently, to scroll.
    // With one axis that would trap the encoder: turning would only ever scroll. So for those
    // "stock scrollers" the turn changes applet, until the user enters SCROLL MODE:
    //   messages (All/DMs/Channel N): encoder short = reply, encoder long = toggle scroll mode
    //   other stock scrollers:        encoder short = toggle scroll mode
    //   BOOT short also leaves scroll mode.
    // Our own applets (Nodes, Waypoints) subscribe BUTTON_SHORT and grab NAV only while
    // browsing, so they are handled by plain subscription.
    // Scroll mode has no on-screen marker (stock applets draw their own header), so it ends by
    // itself after NAV_SCROLL_TIMEOUT_MS without turning: you can't stay "lost" in it.
    InkHUD::Applet *scrollApplet = nullptr; // stock applet currently in scroll mode, if any
    uint32_t scrollActivityMs = 0;

    void setScroll(InkHUD::Applet *a)
    {
        scrollApplet = a;
        scrollActivityMs = millis();
    }

    void rotate(InkHUD::InkHUD *h, bool down)
    {
        if (navConsumerActive(h, down)) {
            scrollActivityMs = millis(); // keep scroll mode alive while turning
            down ? h->navDown() : h->navUp();
        } else {
            scrollApplet = nullptr;
            down ? h->nextApplet() : h->prevApplet();
        }
    }

    bool systemOwnsInput(InkHUD::InkHUD *h)
    {
        for (InkHUD::SystemApplet *sa : h->systemApplets)
            if (sa->handleInput)
                return true;
        return false;
    }

    bool navConsumerActive(InkHUD::InkHUD *h, bool down)
    {
        if (systemOwnsInput(h))
            return true;
        InkHUD::Applet *active = h->getActiveApplet();
        InkHUD::Applet::InputMask want = down ? InkHUD::Applet::NAV_DOWN : InkHUD::Applet::NAV_UP;
        if (!active || !active->isInputSubscribed(want))
            return false;
        if (isStockScroller(active))
            return inScrollMode(h);
        return true;
    }

    // Subscribes NAV but has no action of its own on the button
    bool isStockScroller(InkHUD::Applet *a)
    {
        return (a->isInputSubscribed(InkHUD::Applet::NAV_UP) || a->isInputSubscribed(InkHUD::Applet::NAV_DOWN)) &&
               !a->isInputSubscribed(InkHUD::Applet::BUTTON_SHORT);
    }

    bool inScrollMode(InkHUD::InkHUD *h)
    {
        InkHUD::Applet *active = h->getActiveApplet();
        if (scrollApplet && scrollApplet != active)
            scrollApplet = nullptr; // applet changed under us (autoshow, menu, ...)
        if (scrollApplet && millis() - scrollActivityMs > NAV_SCROLL_TIMEOUT_MS)
            scrollApplet = nullptr; // idle: back to "turn = change applet"
        return scrollApplet != nullptr;
    }

    void select(InkHUD::InkHUD *h, bool longPress)
    {
        InkHUD::Applet *active = h->getActiveApplet();
        if (systemOwnsInput(h) || !active || !isStockScroller(active)) {
            longPress ? h->longpress() : h->shortpress();
            return;
        }
        inScrollMode(h); // drop a stale scroll target
        const bool message = isMessageApplet(active);
        if (message && !longPress) {
            if (!reply(h, active))
                h->shortpress(); // nothing to reply to: InkHUD default (menu)
            return;
        }
        if (message || !longPress) { // toggle scroll mode
            setScroll(scrollApplet == active ? nullptr : active);
            return;
        }
        h->longpress(); // long press on a non-message stock applet: InkHUD default (menu)
    }

    static bool nameIs(InkHUD::Applet *a, const char *n) { return a->name && strcmp(a->name, n) == 0; }

    static bool isMessageApplet(InkHUD::Applet *a)
    {
        using namespace AppletNames;
        return nameIs(a, ALL_MESSAGES) || nameIs(a, DMS) ||
               (a->name && strncmp(a->name, CHANNEL_PREFIX, CHANNEL_PREFIX_LEN) == 0);
    }

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
            channel = (uint8_t)atoi(a->name + AppletNames::CHANNEL_PREFIX_LEN); // "Channel N"
        }
        scrollApplet = nullptr;
        h->openMenu();
        InkHUD::MenuApplet *menu = (InkHUD::MenuApplet *)h->getSystemApplet("Menu");
        if (!menu)
            return false;
        menu->composeTo(dest, channel);
        return true;
    }

    // BACK is the BOOT-button role ("menu / back"), distinct from SELECT (the applet's action):
    //   keyboard:           short = send, long = cancel
    //   long anywhere else: next tile (also closes the menu)
    //   menu open:          close it
    //   applet wants EXIT:  back out of it (e.g. leave the Nodes list)
    //   otherwise:          open the menu
    void back(InkHUD::InkHUD *h, bool longPress)
    {
        if (foreground(h, "Keyboard")) {
            longPress ? h->freeTextCancel() : h->freeTextDone();
            h->closeKeyboard();
        } else if (longPress) {
            h->nextTile();
        } else if (foreground(h, "Menu")) {
            h->exitShort();
        } else if (activeWants(h, InkHUD::Applet::EXIT_SHORT)) {
            h->exitShort();
        } else if (inScrollMode(h)) {
            scrollApplet = nullptr; // leave scroll mode instead of opening the menu
        } else {
            h->openMenu();
        }
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
