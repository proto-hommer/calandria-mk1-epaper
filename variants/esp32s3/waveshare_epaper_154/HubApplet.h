#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

"Inicio": the top of a two-level navigation. Level HOME shows four cards (MENSAJES, NODOS, RUMBO,
ESTADO), each with an icon, a one-line summary and a counter. Clicking a card opens level CARD: the
list of what lives inside it. Clicking an entry shows that (existing) applet with
InkHUD::showApplet(). InkHudNavigator brings us back: BOOT from an applet returns to the card that
contains it (openCardOf), holding BOOT returns HOME (goHome).

The applets inside the cards are the same ones as before, found by the name they are registered
under (AppletNames.h), so this file does not care about their indexes.

E-paper: the selection is drawn inverted (black bar, white text). Moving it is a FAST refresh (no
flashing); entering or leaving a card is FULL, which also clears any grey left by the bar. HOME is redrawn on its own only when
something on it changes (counters, summaries, the clock's minute), checked every HUB_REFRESH_MS.

*/

#include "graphics/niche/InkHUD/Applet.h"

#include "AppletNames.h"
#include "OwnPosition.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/InkHUD/Tile.h"

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

class HubApplet : public Applet, public concurrency::OSThread
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
        setLevel(HOME);
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
        setLevel(HOME);
        showSelf();
    }

    // BOOT inside an applet: back to the card that contains it (HOME if none does)
    void openCardOf(Applet *a)
    {
        for (uint8_t c = 0; c < CARDS; c++)
            for (uint8_t i = 0; i < itemCount((Card)c); i++)
                if (appletFor((Card)c, i) == a) {
                    card = (Card)c;
                    item = i;
                    setLevel(CARD);
                    showSelf();
                    return;
                }
        goHome();
    }

    bool atHome() { return level == HOME; }

    // Any message applet in use: its messages count as read
    static void markRead()
    {
        if (!unread)
            return;
        unread = 0;
        if (instance && instance->isForeground())
            instance->requestUpdate(EInk::UpdateTypes::FAST);
    }

    static bool isMessageApplet(Applet *a)
    {
        using namespace AppletNames;
        return a && a->name &&
               (strcmp(a->name, ALL_MESSAGES) == 0 || strcmp(a->name, DMS) == 0 ||
                strncmp(a->name, CHANNEL_PREFIX, CHANNEL_PREFIX_LEN) == 0);
    }

    // ---------- InkHUD ----------

    void onRender(bool full) override
    {
        (void)full;
        if (level == HOME)
            renderHome();
        else
            renderCard();
    }

    void onButtonShortPress() override
    {
        if (level == HOME) {
            card = (Card)cursor;
            item = 0;
            setLevel(CARD);
            requestUpdate(EInk::UpdateTypes::FULL);
        } else {
            openItem(card, item);
        }
    }

    // Level CARD only (subscribed there): back to HOME
    void onButtonLongPress() override { onExitShort(); }
    void onExitShort() override
    {
        cursor = card;
        setLevel(HOME);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    void onNavUp() override { move(-1); }
    void onNavDown() override { move(1); }

  protected:
    // Redraw HOME only when what it shows has changed
    int32_t runOnce() override
    {
        if (isForeground() && level == HOME) {
            const std::string s = signature();
            if (s != lastSignature) {
                lastSignature = s;
                requestUpdate(EInk::UpdateTypes::FAST);
            }
        }
        return HUB_REFRESH_MS;
    }

  private:
    enum Level : uint8_t { HOME, CARD };
    enum Kind : uint8_t { APPLET, CHANNEL, MENU };
    struct Item {
        const char *label; // nullptr: use the applet's own name (channels)
        Kind kind;
        const char *applet; // AppletNames::*, or the channel index as text for CHANNEL
    };

    static inline HubApplet *instance = nullptr;
    static inline uint16_t unread = 0;

    Level level = HOME;
    Card card = MESSAGES;
    uint8_t cursor = 0; // HOME: which card
    uint8_t item = 0;   // CARD: which entry
    std::string lastSignature;

    static const char *cardName(Card c)
    {
        static const char *names[CARDS] = {"MENSAJES", "NODOS", "RUMBO", "ESTADO"};
        return names[c];
    }

    // What lives inside each card. Stage 1: the existing applets, unchanged.
    static uint8_t itemCount(Card c)
    {
        static const uint8_t counts[CARDS] = {4, 2, 2, 2};
        return counts[c];
    }

    static const Item &itemAt(Card c, uint8_t i)
    {
        using namespace AppletNames;
        static const Item items[CARDS][4] = {
            {{"Todos los mensajes", APPLET, ALL_MESSAGES}, {"Directos", APPLET, DMS}, {nullptr, CHANNEL, "0"},
             {nullptr, CHANNEL, "1"}},
            {{"Últimos oídos", APPLET, AppletNames::NODES}, {"Radar: los 10 más cerca", APPLET, RADAR}},
            {{"Waypoints", APPLET, WAYPOINTS}, {"Mapa del grupo", APPLET, POSITIONS}},
            {{"Estado", APPLET, AppletNames::STATUS}, {"Ajustes", MENU, nullptr}},
        };
        return items[c][i];
    }

    // The user applet behind an entry, by name (channels: "Canal N...")
    Applet *appletFor(Card c, uint8_t i, uint8_t *index = nullptr)
    {
        const Item &it = itemAt(c, i);
        if (it.kind == MENU)
            return nullptr;
        for (size_t n = 0; n < inkhud->userApplets.size(); n++) {
            Applet *a = inkhud->userApplets[n];
            if (!a || !a->name)
                continue;
            bool match;
            if (it.kind == CHANNEL)
                match = strncmp(a->name, AppletNames::CHANNEL_PREFIX, AppletNames::CHANNEL_PREFIX_LEN) == 0 &&
                        a->name[AppletNames::CHANNEL_PREFIX_LEN] == it.applet[0];
            else
                match = strcmp(a->name, it.applet) == 0;
            if (match) {
                if (index)
                    *index = (uint8_t)n;
                return a;
            }
        }
        return nullptr;
    }

    std::string labelFor(Card c, uint8_t i)
    {
        const Item &it = itemAt(c, i);
        if (it.label)
            return parse(it.label);
        Applet *a = appletFor(c, i);
        return a ? std::string(a->name) : std::string("Canal ") + it.applet; // channel names are already font-encoded
    }

    void setLevel(Level l)
    {
        level = l;
        setInputsSubscribed(BUTTON_SHORT | NAV_UP | NAV_DOWN, true);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, l == CARD); // at HOME, BOOT is the navigator's (menu)
    }

    void move(int dir)
    {
        const uint8_t n = level == HOME ? (uint8_t)CARDS : itemCount(card);
        uint8_t &pos = level == HOME ? cursor : item;
        const int next = (int)pos + dir;
        if (next < 0 || next >= n)
            return;
        pos = (uint8_t)next;
        requestUpdate(EInk::UpdateTypes::FAST); // no flashing; a level change (FULL) cleans up
    }

    void showSelf()
    {
        for (size_t n = 0; n < inkhud->userApplets.size(); n++)
            if (inkhud->userApplets[n] == this) {
                ensureActive((uint8_t)n);
                if (isForeground())
                    requestUpdate(EInk::UpdateTypes::FULL);
                else
                    inkhud->showApplet((uint8_t)n);
                return;
            }
    }

    // An applet switched off from the menu's "Applets" page would make its entry a dead end
    void ensureActive(uint8_t index)
    {
        Applet *a = inkhud->userApplets[index];
        if (a->isActive())
            return;
        settings->userApplets.active[index] = true;
        a->activate();
    }

    void openItem(Card c, uint8_t i)
    {
        if (itemAt(c, i).kind == MENU) {
            inkhud->openMenu();
            return;
        }
        uint8_t index = 0;
        if (!appletFor(c, i, &index))
            return;
        ensureActive(index);
        if (c == MESSAGES)
            markRead();
        inkhud->showApplet(index);
    }

    // ---------- Data for the cards ----------

    int onReceiveTextMessage(const meshtastic_MeshPacket *p)
    {
        if (p && p->from != 0 && p->from != nodeDB->getNodeNum()) {
            unread++;
            if (isForeground())
                requestUpdate(EInk::UpdateTypes::FAST);
        }
        return 0;
    }
    CallbackObserver<HubApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<HubApplet, const meshtastic_MeshPacket *>(this, &HubApplet::onReceiveTextMessage);

    static std::string ago(uint32_t s)
    {
        char buf[16];
        if (s < 60)
            return "recién";
        if (s < 3600)
            snprintf(buf, sizeof(buf), "%lu min", (unsigned long)(s / 60));
        else if (s < 86400)
            snprintf(buf, sizeof(buf), "%lu h", (unsigned long)(s / 3600));
        else
            snprintf(buf, sizeof(buf), "%lu d", (unsigned long)(s / 86400));
        return buf;
    }

    std::string shortNameOf(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(num);
        if (n && nodeInfoLiteHasUser(n) && n->short_name[0])
            return parseShortName(n);
        return hexifyNodeNum(num);
    }

    // Summary line and right-hand counter of a card
    void cardInfo(Card c, std::string &summary, std::string &counter)
    {
        char buf[48];
        switch (c) {
        case MESSAGES: {
            const auto &latest = inkhud->persistence->latestMessage;
            const StoredMessage &m = latest.wasBroadcast ? latest.broadcast : latest.dm;
            if (!m.sender)
                summary = "Sin mensajes";
            else
                summary = shortNameOf(m.sender) + ": " + parse(std::string(MessageStore::getText(m)));
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
                if (s < newest)
                    newest = s;
                if (s <= settings->recentlyActiveSeconds)
                    active++;
            }
            if (!total)
                summary = "Sin nodos todavía";
            else if (newest == UINT32_MAX)
                summary = std::to_string(total) + " en la lista";
            else {
                snprintf(buf, sizeof(buf), "%u activos · último ", active);
                summary = parse(buf) + parse(newest < 60 ? std::string("recién") : "hace " + ago(newest));
            }
            counter = std::to_string(total);
            break;
        }
        case ROUTE: {
            const size_t wps = waypointStore.getWaypoints().size();
            snprintf(buf, sizeof(buf), "%u waypoint%s · ", (unsigned)wps, wps == 1 ? "" : "s");
            summary = parse(buf) + parse(OwnPosition::have() ? "con posición" : "sin posición");
            counter = std::to_string(wps);
            break;
        }
        default: {
            const bool usb = powerStatus && powerStatus->getHasUSB();
            const bool charging = powerStatus && powerStatus->getIsCharging();
            summary = charging ? "Cargando" : usb ? "Enchufada" : "A batería";
            summary += config.network.wifi_enabled ? " · WiFi prendido" : " · WiFi apagado";
            summary = parse(summary);
            if (powerStatus && powerStatus->getHasBattery())
                counter = std::to_string(powerStatus->getBatteryChargePercent()) + "%";
            else
                counter = usb ? "USB" : "--";
            break;
        }
        }
    }

    std::string clock() { return getValidTime(RTCQuality::RTCQualityDevice) ? getTimeString() : std::string("--:--"); }

    // Everything HOME shows: redraw only when this changes
    std::string signature()
    {
        std::string s = clock();
        for (uint8_t c = 0; c < CARDS; c++) {
            std::string a, b;
            cardInfo((Card)c, a, b);
            s += "|" + a + "|" + b;
        }
        return s;
    }

    // ---------- Drawing ----------

    // Right edge of the black title bar: stop short of InkHUD's battery icon, which draws itself there
    int16_t barRight()
    {
        if (!settings->optionalFeatures.batteryIcon)
            return width();
        Tile *t = ((Applet *)inkhud->getSystemApplet("BatteryIcon"))->getTile();
        return t ? t->getLeft() - 3 : width();
    }

    uint16_t barHeight() { return getHeaderHeight(); }

    // Black title bar: left text (with "<" when there is a level above) and a right-hand datum
    void drawBar(const std::string &left, const std::string &right, bool backArrow, bool bolt)
    {
        const int16_t r = barRight();
        const uint16_t h = barHeight();
        fillRect(0, 0, r, h, BLACK);
        setFont(fontSmall);
        setTextColor(WHITE);
        int16_t x = 3;
        if (backArrow) {
            const int16_t cy = h / 2;
            for (int k = 0; k < 2; k++) {
                drawLine(x + 4 + k, cy - 4, x + k, cy, WHITE);
                drawLine(x + k, cy, x + 4 + k, cy + 4, WHITE);
            }
            x += 10;
        }
        printAt(x, h / 2, left, LEFT, MIDDLE);
        int16_t rx = r - 3;
        if (!right.empty()) {
            printAt(rx, h / 2, right, RIGHT, MIDDLE);
            rx -= getTextWidth(right) + 4;
        }
        if (bolt) // plugged in
            drawBolt(rx - 5, h / 2, WHITE);
        setTextColor(BLACK);
    }

    void drawBolt(int16_t cx, int16_t cy, uint16_t color)
    {
        fillTriangle(cx + 1, cy - 5, cx - 3, cy + 1, cx, cy + 1, color);
        fillTriangle(cx - 1, cy + 5, cx + 3, cy - 1, cx, cy - 1, color);
    }

    // 16 px pictograms, drawn with primitives so they invert with the row
    void drawIcon(Card c, int16_t x, int16_t cy, uint16_t color)
    {
        const int16_t y = cy - 8;
        switch (c) {
        case MESSAGES: // speech bubble
            drawRoundRect(x, y + 1, 16, 11, 3, color);
            fillTriangle(x + 3, y + 11, x + 8, y + 11, x + 3, y + 15, color);
            drawLine(x + 4, y + 5, x + 11, y + 5, color);
            drawLine(x + 4, y + 8, x + 9, y + 8, color);
            break;
        case NODES: // three linked nodes
            drawLine(x + 3, y + 12, x + 8, y + 3, color);
            drawLine(x + 8, y + 3, x + 13, y + 12, color);
            drawLine(x + 3, y + 12, x + 13, y + 12, color);
            fillCircle(x + 3, y + 12, 3, color);
            fillCircle(x + 8, y + 3, 3, color);
            fillCircle(x + 13, y + 12, 3, color);
            break;
        case ROUTE: // compass
            drawCircle(x + 8, y + 8, 7, color);
            fillTriangle(x + 8, y + 2, x + 5, y + 9, x + 11, y + 9, color);
            drawLine(x + 8, y + 9, x + 8, y + 13, color);
            break;
        default: // signal / level bars
            fillRect(x + 1, y + 10, 3, 5, color);
            fillRect(x + 6, y + 6, 3, 9, color);
            fillRect(x + 11, y + 2, 3, 13, color);
            break;
        }
    }

    void renderHome()
    {
        const bool usb = powerStatus && powerStatus->getHasUSB();
        std::string me = owner.short_name[0] ? parse(owner.short_name) : std::string("Calandria");
        drawBar(me, clock(), false, usb);

        const int16_t top = barHeight() + 1;
        const uint16_t rowH = (height() - top) / CARDS;
        for (uint8_t c = 0; c < CARDS; c++) {
            const int16_t y = top + c * rowH;
            const bool sel = c == cursor;
            const uint16_t fg = sel ? WHITE : BLACK;
            if (sel)
                fillRect(0, y, width(), rowH, BLACK);
            else if (c > 0)
                for (int16_t x = 0; x < width(); x += 2)
                    drawPixel(x, y, BLACK); // dotted separator

            std::string summary, counter;
            cardInfo((Card)c, summary, counter);

            drawIcon((Card)c, 4, y + rowH / 2 - 5, fg);
            setTextColor(fg);

            // Counter on the right; unread messages in a circle
            int16_t right = width() - 4;
            setFont(fontMedium);
            if (!counter.empty()) {
                const int16_t cy = y + rowH / 2 - 5;
                if (c == MESSAGES) {
                    const int16_t rad = std::max<int16_t>(9, getTextWidth(counter) / 2 + 4);
                    fillCircle(right - rad, cy, rad, fg);
                    setTextColor(sel ? BLACK : WHITE);
                    printAt(right - rad, cy, counter, CENTER, MIDDLE);
                    setTextColor(fg);
                    right -= 2 * rad + 4;
                } else {
                    printAt(right, cy, counter, RIGHT, MIDDLE);
                    right -= getTextWidth(counter) + 6;
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

    void renderCard()
    {
        std::string summary, counter;
        cardInfo(card, summary, counter);
        drawBar(cardName(card), counter, true, false);

        const int16_t top = barHeight() + 4;
        setFont(fontMedium);
        const uint16_t rowH = fontMedium.lineHeight() + 10;
        const uint8_t n = itemCount(card);
        for (uint8_t i = 0; i < n; i++) {
            const int16_t y = top + i * rowH;
            const bool sel = i == item;
            if (sel)
                fillRect(0, y, width(), rowH, BLACK);
            setTextColor(sel ? WHITE : BLACK);
            setCrop(0, y, width() - 14, rowH);
            printAt(6, y + rowH / 2, labelFor(card, i), LEFT, MIDDLE);
            resetCrop();
            // ">" on the right: opens something
            const int16_t cx = width() - 9, cy = y + rowH / 2;
            const uint16_t col = sel ? WHITE : BLACK;
            drawLine(cx, cy - 4, cx + 4, cy, col);
            drawLine(cx + 4, cy, cx, cy + 4, col);
            setTextColor(BLACK);
        }

        // The card's summary under the entries, as a reminder of what is new
        setFont(fontSmall);
        const int16_t y = top + n * rowH + 6;
        if (y + fontSmall.lineHeight() < height()) {
            setCrop(0, y, width(), height() - y);
            printWrapped(4, y, width() - 8, summary);
            resetCrop();
        }
    }
};

} // namespace NicheGraphics::InkHUD

#endif
