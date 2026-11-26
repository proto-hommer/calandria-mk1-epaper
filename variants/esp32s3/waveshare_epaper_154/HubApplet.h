#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

"Inicio": the top of a two-level navigation. Four cards (MENSAJES, NODOS, RUMBO, ESTADO), each with an
icon, a one-line summary and a counter. Clicking a card opens it:

  MENSAJES -> ConversationApplet (its list of conversations IS the card)
  NODOS    -> NodePickerApplet   (its list of nodes)
  ESTADO   -> StatusApplet       (its boxes and list)
  RUMBO    -> a list drawn here: Rastrear un nodo, Waypoints, Mapa del grupo, Mi posición

InkHudNavigator brings us back: BOOT from an applet goes back one level (back()), holding BOOT returns
HOME (goHome). A card applet is reset when entered from here (enter()), never when it merely comes
back to the foreground (after the menu, the keyboard, a child applet...).

E-paper: the selection is drawn inverted (black bar, white text). Moving it is a FAST refresh (no
flashing); entering or leaving a card is FULL, which also clears any grey left by the bar. HOME is redrawn
on its own only when something on it changes (counters, summaries, the clock's minute), checked every
HUB_REFRESH_MS.

*/

#include "CalandriaUI.h"

#include "AppletNames.h"
#include "ConversationApplet.h"
#include "NavReturn.h"
#include "NodePickerApplet.h"
#include "OwnPosition.h"
#include "StatusApplet.h"
#include "TrackApplet.h"
#include "WaypointCompassApplet.h"

#include "MessageStore.h"
#include "NodeDB.h"
#include "Observer.h"
#include "PowerStatus.h"
#include "RTC.h"
#include "WaypointStore.h"
#include "concurrency/OSThread.h"
#include "modules/TextMessageModule.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef HUB_REFRESH_MS
#define HUB_REFRESH_MS (30 * 1000UL)
#endif

namespace NicheGraphics::InkHUD
{

class HubApplet : public CalandriaUI<Applet>, public concurrency::OSThread
{
  public:
    enum Card : uint8_t { MESSAGES, NODES, ROUTE, STATUS, CARDS };

    HubApplet() : concurrency::OSThread("HubApplet")
    {
        instance = this;
        OSThread::disable();
    }

    static HubApplet *get() { return instance; }

    void onActivate() override
    {
        textMessageObserver.observe(textMessageModule);
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(HUB_REFRESH_MS);
        setLevel(level);
    }

    void onDeactivate() override
    {
        textMessageObserver.unobserve(textMessageModule);
        OSThread::disable();
    }

    void onForeground() override { lastSignature = signature(); }

    // ---------- Called by InkHudNavigator ----------

    // Hold BOOT: back to the four cards
    void goHome()
    {
        NavReturn::clear();
        setLevel(HOME);
        showSelf();
    }

    // BOOT in an applet at its own top level: back to whoever opened it
    void back(Applet *a)
    {
        if (a && a == NavReturn::child && NavReturn::parent) { // e.g. Radar -> the node list
            Applet *p = NavReturn::parent;
            NavReturn::clear();
            show(p);
            return;
        }
        NavReturn::clear();
        for (uint8_t c = 0; c < CARDS; c++)
            if (a && a == cardApplet((Card)c)) { // a card itself: back to Inicio, on that card
                cursor = c;
                goHome();
                return;
            }
        for (uint8_t i = 0; i < ROUTE_ITEMS; i++) // something inside RUMBO
            if (a && a == routeApplet(i)) {
                cursor = ROUTE;
                item = i;
                setLevel(CARD);
                showSelf();
                return;
            }
        goHome();
    }

    // Bring an applet back as it was (no reset)
    void show(Applet *a)
    {
        const int i = indexOf(a);
        if (i < 0)
            return;
        ensureActive((uint8_t)i);
        inkhud->showApplet((uint8_t)i);
    }

    // ---------- InkHUD ----------

    void onRender(bool full) override
    {
        (void)full;
        lastSignature = signature();
        if (level == HOME)
            renderHome();
        else
            renderRoute();
    }

    void onButtonShortPress() override
    {
        if (level == HOME)
            openCard((Card)cursor);
        else
            openRouteItem(item);
    }

    // RUMBO list only (subscribed there): back to HOME
    void onButtonLongPress() override { onExitShort(); }
    void onExitShort() override
    {
        cursor = ROUTE;
        setLevel(HOME);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    void onNavUp() override { move(-1); }
    void onNavDown() override { move(1); }

  protected:
    // Redraw HOME only when what it shows has changed
    int32_t runOnce() override
    {
        if (isForeground() && level == HOME && signature() != lastSignature)
            requestUpdate(EInk::UpdateTypes::FAST);
        return HUB_REFRESH_MS;
    }

  private:
    enum Level : uint8_t { HOME, CARD };
    static constexpr uint8_t ROUTE_ITEMS = 4;

    static inline HubApplet *instance = nullptr;

    Level level = HOME;
    uint8_t cursor = 0; // HOME: which card
    uint8_t item = 0;   // RUMBO: which entry
    std::string lastSignature;

    static const char *cardName(Card c)
    {
        static const char *names[CARDS] = {"MENSAJES", "NODOS", "RUMBO", "ESTADO"};
        return names[c];
    }

    // ---------- Applets behind the cards ----------
    int indexOf(Applet *a)
    {
        for (size_t n = 0; n < inkhud->userApplets.size(); n++)
            if (inkhud->userApplets[n] == a)
                return (int)n;
        return -1;
    }

    Applet *byName(const char *name)
    {
        for (Applet *a : inkhud->userApplets)
            if (a && a->name && strcmp(a->name, name) == 0)
                return a;
        return nullptr;
    }

    Applet *cardApplet(Card c)
    {
        switch (c) {
        case MESSAGES:
            return byName(AppletNames::CONVERSATIONS);
        case NODES:
            return byName(AppletNames::NODES);
        case STATUS:
            return byName(AppletNames::STATUS);
        default:
            return nullptr; // RUMBO is a list drawn here
        }
    }

    static const char *routeLabel(uint8_t i)
    {
        static const char *labels[ROUTE_ITEMS] = {"Rastrear un nodo", "Waypoints", "Mapa del grupo", "Mi posición"};
        return labels[i];
    }

    Applet *routeApplet(uint8_t i)
    {
        switch (i) {
        case 1:
            return byName(AppletNames::WAYPOINTS);
        case 2:
            return byName(AppletNames::POSITIONS);
        default:
            return byName(AppletNames::TRACK); // track a node / my position
        }
    }

    void setLevel(Level l)
    {
        level = l;
        setInputsSubscribed(BUTTON_SHORT | NAV_UP | NAV_DOWN, true);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, l == CARD); // at HOME, BOOT is the navigator's (menu)
    }

    void move(int dir)
    {
        const uint8_t n = level == HOME ? (uint8_t)CARDS : ROUTE_ITEMS;
        uint8_t &pos = level == HOME ? cursor : item;
        const int next = (int)pos + dir;
        if (next < 0 || next >= n)
            return;
        pos = (uint8_t)next;
        requestUpdate(EInk::UpdateTypes::FAST); // no flashing; a level change (FULL) cleans up
    }

    void showSelf()
    {
        if (isForeground()) {
            requestUpdate(EInk::UpdateTypes::FULL);
            return;
        }
        const int i = indexOf(this);
        if (i < 0)
            return;
        ensureActive((uint8_t)i);
        inkhud->showApplet((uint8_t)i);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    // An applet switched off from the menu's "Applets" page would make its card a dead end
    void ensureActive(uint8_t index)
    {
        Applet *a = inkhud->userApplets[index];
        if (a->isActive())
            return;
        settings->userApplets.active[index] = true;
        a->activate();
    }

    void showFresh(Applet *a)
    {
        NavReturn::clear();
        inkhud->showApplet((uint8_t)indexOf(a));
        inkhud->forceUpdate(EInk::UpdateTypes::FULL);
    }

    // Entering resets the card's applet to its first screen
    void openCard(Card c)
    {
        if (c == ROUTE) {
            item = 0;
            setLevel(CARD);
            requestUpdate(EInk::UpdateTypes::FULL);
            return;
        }
        Applet *a = cardApplet(c);
        if (!a)
            return;
        ensureActive((uint8_t)indexOf(a));
        if (c == MESSAGES)
            static_cast<ConversationApplet *>(a)->enter();
        else if (c == NODES)
            static_cast<NodePickerApplet *>(a)->enter();
        else
            static_cast<StatusApplet *>(a)->enter();
        showFresh(a);
    }

    void openRouteItem(uint8_t i)
    {
        Applet *a = routeApplet(i);
        if (!a)
            return;
        ensureActive((uint8_t)indexOf(a));
        if (i == 0 || i == 3)
            static_cast<TrackApplet *>(a)->enter(i == 0 ? TrackApplet::TRACK : TrackApplet::MINE);
        else if (i == 1)
            static_cast<WaypointCompassApplet *>(a)->enter();
        showFresh(a);
    }

    // ---------- Data for the cards ----------
    int onReceiveTextMessage(const meshtastic_MeshPacket *p)
    {
        (void)p;
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST); // counters / last message
        return 0;
    }
    CallbackObserver<HubApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<HubApplet, const meshtastic_MeshPacket *>(this, &HubApplet::onReceiveTextMessage);

    // Summary line and right-hand counter of a card
    void cardInfo(Card c, std::string &summary, std::string &counter)
    {
        switch (c) {
        case MESSAGES: {
            const auto &all = messageStore.getLiveMessages();
            if (all.empty())
                summary = "Sin mensajes";
            else {
                const StoredMessage &m = all.back();
                const std::string who = m.sender == nodeDB->getNodeNum() ? std::string("Vos") : shortNameOf(m.sender);
                summary = who + ": " + parse(MessageStore::getText(m));
            }
            const uint16_t unread = ConversationApplet::totalUnread();
            counter = unread ? std::to_string(unread) : "";
            break;
        }
        case NODES: {
            const NodeNum me = nodeDB->getNodeNum();
            unsigned total = 0, active = 0;
            uint32_t newest = UINT32_MAX;
            for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
                const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
                if (!n || n->num == me)
                    continue;
                total++;
                if (!n->last_heard)
                    continue;
                const uint32_t s = sinceLastSeen(n);
                newest = std::min(newest, s);
                if (s <= 3600) // heard within the hour: not lost (as a client)
                    active++;
            }
            if (!total)
                summary = parse("Sin nodos todavía");
            else if (newest == UINT32_MAX)
                summary = fmt("%u en la lista", total);
            else
                summary =
                    parse(fmt("%u activos · último ", active) + (newest < 60 ? std::string("recién") : "hace " + ago(newest)));
            counter = std::to_string(total);
            break;
        }
        case ROUTE: {
            const size_t wps = waypointStore.getWaypoints().size();
            summary = parse(fmt("Rastrear · %u waypoint%s", (unsigned)wps, wps == 1 ? "" : "s") +
                            (OwnPosition::have() ? "" : " · sin posición"));
            counter = std::to_string(wps);
            break;
        }
        default: {
            summary = charging() ? "Cargando" : plugged() ? "Enchufada" : "A batería";
            summary += config.network.wifi_enabled ? " · WiFi prendido" : " · WiFi apagado";
            summary = parse(summary);
            if (powerStatus && powerStatus->getHasBattery())
                counter = std::to_string(powerStatus->getBatteryChargePercent()) + "%";
            else
                counter = plugged() ? "USB" : "--";
            break;
        }
        }
    }

    std::string clock() { return getValidTime(RTCQuality::RTCQualityDevice) ? getTimeString() : std::string("--:--"); }

    std::string batteryText()
    {
        if (powerStatus && powerStatus->getHasBattery())
            return std::to_string(powerStatus->getBatteryChargePercent()) + "%";
        return "";
    }

    // Everything HOME shows: redraw only when this changes
    std::string signature()
    {
        std::string s = clock() + batteryText() + (plugged() ? "+" : "");
        for (uint8_t c = 0; c < CARDS; c++) {
            std::string a, b;
            cardInfo((Card)c, a, b);
            s += "|" + a + "|" + b;
        }
        return s;
    }

    // ---------- Drawing ----------

    // 16 px pictograms, drawn with primitives so they invert with the row
    void drawIcon(Card c, int16_t x, int16_t cy, uint16_t color)
    {
        const int16_t y = cy - 8;
        switch (c) {
        case MESSAGES: // an envelope
            drawRect(x, y + 3, 16, 11, color);
            drawLine(x, y + 3, x + 8, y + 9, color);
            drawLine(x + 8, y + 9, x + 15, y + 3, color);
            break;
        case NODES: // three linked dots
            drawLine(x + 3, y + 12, x + 8, y + 3, color);
            drawLine(x + 8, y + 3, x + 13, y + 12, color);
            drawLine(x + 3, y + 12, x + 13, y + 12, color);
            fillCircle(x + 3, y + 12, 3, color);
            fillCircle(x + 8, y + 3, 3, color);
            fillCircle(x + 13, y + 12, 3, color);
            break;
        case ROUTE: // a compass
            drawCircle(x + 8, y + 8, 7, color);
            fillTriangle(x + 8, y + 2, x + 5, y + 9, x + 11, y + 9, color);
            drawLine(x + 8, y + 9, x + 8, y + 13, color);
            break;
        default: // a battery
            drawRect(x, y + 4, 14, 9, color);
            fillRect(x + 14, y + 6, 2, 5, color);
            fillRect(x + 2, y + 6, 7, 5, color);
            break;
        }
    }

    void renderHome()
    {
        const std::string me = owner.short_name[0] ? parse(owner.short_name) : std::string("Calandria");
        std::string right = clock();
        const std::string bat = batteryText();
        if (!bat.empty())
            right = bat + "  " + right;
        drawBar(me, right, false, plugged());

        const int16_t top = barHeight() + 1;
        const uint16_t rowH = (height() - top) / CARDS;
        for (uint8_t c = 0; c < CARDS; c++) {
            const int16_t y = top + c * rowH;
            const bool sel = c == cursor;
            if (!sel && c > 0)
                dottedLine(y);
            const uint16_t fg = beginRow(y, rowH, sel);

            std::string summary, counter;
            cardInfo((Card)c, summary, counter);
            drawIcon((Card)c, 4, y + rowH / 2 - 5, fg);

            // Counter on the right; unread messages in a circle
            setFont(fontMedium);
            if (!counter.empty()) {
                const int16_t right = width() - 4;
                const int16_t cy = y + rowH / 2 - 5;
                if (c == MESSAGES) {
                    const int16_t rad = std::max<int16_t>(9, getTextWidth(counter) / 2 + 4);
                    fillCircle(right - rad, cy, rad, fg);
                    setTextColor(sel ? BLACK : WHITE);
                    printAt(right - rad, cy, counter, CENTER, MIDDLE);
                    setTextColor(fg);
                } else {
                    printAt(right, cy, counter, RIGHT, MIDDLE);
                }
            }

            const int16_t textX = 26;
            printAt(textX, y + 3, cardName((Card)c));
            setFont(fontSmall);
            setCrop(textX, y, width() - textX - 4, rowH); // long summaries never run off the row
            printAt(textX, y + 4 + fontMedium.lineHeight(), summary);
            resetCrop();
            setTextColor(BLACK);
        }
    }

    void renderRoute()
    {
        drawBar("RUMBO", "", true);
        const int16_t top = barHeight() + 4;
        setFont(fontMedium);
        const uint16_t rowH = fontMedium.lineHeight() + 10;
        for (uint8_t i = 0; i < ROUTE_ITEMS; i++) {
            const int16_t y = top + i * rowH;
            const uint16_t fg = beginRow(y, rowH, i == item);
            setCrop(0, y, width() - 14, rowH);
            printAt(6, y + rowH / 2, parse(routeLabel(i)), LEFT, MIDDLE);
            resetCrop();
            drawChevron(width() - 9, y + rowH / 2, fg);
            setTextColor(BLACK);
        }

        // No GPS on the MK1: the position comes from the phone
        setFont(fontSmall);
        const int16_t y = top + ROUTE_ITEMS * rowH + 6;
        if (y + fontSmall.lineHeight() < height()) {
            setCrop(0, y, width(), height() - y);
            printWrapped(4, y, width() - 8, parse(OwnPosition::statusLine()));
            resetCrop();
        }
    }
};

} // namespace NicheGraphics::InkHUD

#endif
