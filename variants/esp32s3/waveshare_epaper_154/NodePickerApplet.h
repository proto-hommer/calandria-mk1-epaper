#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

NODOS: the card itself is the list of nodes ("Últimos oídos"); a node's card is the thing inside it.

  LIST   : every node, last heard first: short name, how long ago, hops, distance and direction (when
           both positions are known). Lost nodes are struck through ("perdido · 2 h"), same rule as
           CalandriApp: a client after 1 h unheard, a ROUTER / ROUTER_LATE after 13 h (they send their
           own info every 12 h). The last row opens the Radar (the 10 nearest).
  CARD   : four boxes (Oído, Enlace, Distancia, Batería) and the actions: Mandarle un mensaje (keyboard /
           canned, MenuApplet::composeTo), Medir señal, Rastrearlo (the compass of TrackApplet),
           Marcar / Quitar favorito (NodeDB::set_favorite, no admin packet) and Olvidar nodo (removed
           from the NodeDB without a reboot, after a second click).
  RESULT : Medir señal. A traceroute (sent by StatusApplet, one at a time): IDA (us -> node) and VUELTA
           (node -> us) with the SNR of every hop, the number of hops, how good the link is and its
           weakest hop. "Midiendo…" while waiting, "Sin respuesta" after the timeout. Click measures again.
BOOT goes back one level (RESULT -> CARD -> LIST); from the list, InkHudNavigator goes back to Inicio.

The list is rebuilt from the NodeDB every NODE_PICKER_REFRESH_MS while on screen, keeping the cursor on
the same node, and only redrawn when what it shows changed.

*/

#include "CalandriaUI.h"

#include "AppletNames.h"
#include "MenuAppletInclude.h"
#include "NavReturn.h"
#include "RadarApplet.h"
#include "StatusApplet.h"
#include "TrackApplet.h"

#include "NodeDB.h"
#include "concurrency/OSThread.h"

#ifndef NODE_PICKER_REFRESH_MS
#define NODE_PICKER_REFRESH_MS (2 * 60 * 1000UL)
#endif
#define NODE_PICKER_TRACE_POLL_MS (3 * 1000UL)

#include <algorithm>
#include <climits>
#include <cstdio>
#include <string>
#include <vector>

namespace NicheGraphics::InkHUD
{

class NodePickerApplet : public CalandriaUI<Applet>, public concurrency::OSThread
{
  public:
    NodePickerApplet() : concurrency::OSThread("NodePickerApplet") { OSThread::disable(); }

    // Entered from Inicio: the list, on the most recently heard node
    void enter()
    {
        rebuild();
        cursor = 0;
        top = 0;
        setLevel(LIST);
    }

    void onActivate() override
    {
        setLevel(level);
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(NODE_PICKER_REFRESH_MS);
    }

    void onDeactivate() override { OSThread::disable(); }

    void onRender(bool full) override
    {
        (void)full;
        if (nodeDB->getNumMeshNodes() != builtFrom) { // nodes forgotten ("Limpiar fantasmas")
            rebuild();
            if (level != LIST && !nodeDB->getMeshNode(selected))
                setLevel(LIST);
        }
        if (level == CARD)
            renderCard();
        else if (level == RESULT)
            renderResult();
        else
            renderList();
    }

    void onButtonShortPress() override
    {
        if (level == LIST) {
            if (cursor == nodes.size()) { // the Radar row
                openRadar();
                return;
            }
            if (cursor > nodes.size())
                return;
            selected = nodes[cursor];
            action = 0;
            notice.clear();
            setLevel(CARD);
            requestUpdate(EInk::UpdateTypes::FULL);
        } else if (level == CARD) {
            runAction();
        } else {
            measure(); // again
        }
    }

    // CARD / RESULT only (subscribed there)
    void onButtonLongPress() override { onExitShort(); }
    void onExitShort() override
    {
        notice.clear();
        setLevel(level == RESULT ? CARD : LIST);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    void onNavUp() override { move(-1); }
    void onNavDown() override { move(1); }

  protected:
    int32_t runOnce() override
    {
        if (!isForeground())
            return NODE_PICKER_REFRESH_MS;
        if (level == RESULT) { // waiting for the traceroute
            StatusApplet *st = status();
            const uint8_t state = st ? (uint8_t)st->traceStateFor(selected) : (uint8_t)0;
            if (state != shownTraceState)
                requestUpdate(EInk::UpdateTypes::FAST);
            return state == StatusApplet::TRACE_PENDING ? NODE_PICKER_TRACE_POLL_MS : NODE_PICKER_REFRESH_MS;
        }
        if (level == CARD) {
            requestUpdate(EInk::UpdateTypes::FAST); // "hace N" changes
        } else {
            const NodeNum keep = cursor < nodes.size() ? nodes[cursor] : 0;
            const std::string before = signature();
            rebuild();
            for (uint16_t i = 0; i < nodes.size(); i++)
                if (nodes[i] == keep)
                    cursor = i; // keep the selection on the same node
            if (signature() != before)
                requestUpdate(EInk::UpdateTypes::FAST);
        }
        return NODE_PICKER_REFRESH_MS;
    }

  private:
    enum Level : uint8_t { LIST, CARD, RESULT };
    static constexpr uint8_t ACTIONS = 5;
    static constexpr const char *CONFIRM_FORGET = "Clic de nuevo: olvidar";

    Level level = LIST;
    std::vector<NodeNum> nodes;
    uint16_t cursor = 0, top = 0; // cursor == nodes.size(): the Radar row
    NodeNum selected = 0;
    uint8_t action = 0;
    std::string notice;
    size_t builtFrom = 0;
    uint8_t shownTraceState = 0;

    void setLevel(Level l)
    {
        level = l;
        setInputsSubscribed(BUTTON_SHORT, true);
        setInputsSubscribed(NAV_UP | NAV_DOWN, l != RESULT);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, l != LIST);
        if (l == RESULT)
            OSThread::setIntervalFromNow(NODE_PICKER_TRACE_POLL_MS);
    }

    void move(int dir)
    {
        if (level == CARD) {
            const int next = (int)action + dir;
            if (next < 0 || next >= ACTIONS)
                return;
            action = (uint8_t)next;
            notice.clear();
        } else {
            const int next = (int)cursor + dir;
            if (next < 0 || next > (int)nodes.size()) // nodes + the Radar row
                return;
            cursor = (uint16_t)next;
        }
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    // ---------- Data ----------
    void rebuild()
    {
        nodes.clear();
        const NodeNum me = nodeDB->getNodeNum();
        builtFrom = nodeDB->getNumMeshNodes();
        for (uint32_t i = 0; i < builtFrom; i++) {
            const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            if (n && n->num != me)
                nodes.push_back(n->num);
        }
        // Most recently heard first; never heard (last_heard 0) at the end
        std::stable_sort(nodes.begin(), nodes.end(), [](NodeNum a, NodeNum b) {
            const meshtastic_NodeInfoLite *na = nodeDB->getMeshNode(a), *nb = nodeDB->getMeshNode(b);
            return (na ? na->last_heard : 0) > (nb ? nb->last_heard : 0);
        });
        if (cursor > nodes.size())
            cursor = nodes.size();
    }

    static uint32_t heardAgo(const meshtastic_NodeInfoLite *n) { return n && n->last_heard ? sinceLastSeen(n) : UINT32_MAX; }

    // CalandriApp's rule: 1 h for a client, 13 h for a router (they announce themselves every 12 h)
    static bool isLost(const meshtastic_NodeInfoLite *n)
    {
        const uint32_t s = heardAgo(n);
        if (s == UINT32_MAX)
            return true;
        const bool router =
            n->role == meshtastic_Config_DeviceConfig_Role_ROUTER || n->role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE;
        return s > (router ? 13UL : 1UL) * 3600UL;
    }

    std::string hopsShort(const meshtastic_NodeInfoLite *n)
    {
        if (!n || !n->has_hops_away)
            return "";
        return n->hops_away == 0 ? std::string("d") : fmt("%us", (unsigned)n->hops_away);
    }

    std::string hopsLong(const meshtastic_NodeInfoLite *n)
    {
        if (!n || !n->has_hops_away)
            return "?";
        return n->hops_away == 0 ? std::string("directo") : fmt("%u salto%s", (unsigned)n->hops_away, n->hops_away == 1 ? "" : "s");
    }

    std::string whereTo(NodeNum num)
    {
        float meters, bearing;
        if (!RadarApplet::locate(num, &meters, &bearing))
            return "";
        return localizeDistance((uint32_t)meters) + " " + cardinal(bearingDegrees(bearing));
    }

    std::string signature()
    {
        std::string s;
        for (NodeNum n : nodes) {
            const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(n);
            const uint32_t a = heardAgo(node);
            s += std::to_string(n) + (a == UINT32_MAX ? std::string("-") : ago(a)) + hopsShort(node) + whereTo(n) + ";";
        }
        return s;
    }

    StatusApplet *status() { return findApplet<StatusApplet>(AppletNames::STATUS, nullptr); }

    template <class T> T *findApplet(const char *name, uint8_t *index)
    {
        for (size_t i = 0; i < inkhud->userApplets.size(); i++) {
            Applet *a = inkhud->userApplets[i];
            if (a && a->name && strcmp(a->name, name) == 0) {
                if (index)
                    *index = (uint8_t)i;
                return static_cast<T *>(a);
            }
        }
        return nullptr;
    }

    // Show another applet; BOOT there comes back here, as we were
    template <class T> T *openChild(const char *name)
    {
        uint8_t index = 0;
        T *a = findApplet<T>(name, &index);
        if (!a)
            return nullptr;
        if (!a->isActive()) {
            settings->userApplets.active[index] = true;
            a->activate();
        }
        NavReturn::set(a, this);
        inkhud->showApplet(index);
        return a;
    }

    void openRadar()
    {
        RadarApplet *radar = findApplet<RadarApplet>(AppletNames::RADAR, nullptr);
        if (radar)
            radar->setTarget(0); // the nearest
        openChild<RadarApplet>(AppletNames::RADAR);
    }

    // ---------- Actions ----------
    void runAction()
    {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(selected);
        if (!n) {
            setLevel(LIST);
            requestUpdate(EInk::UpdateTypes::FULL);
            return;
        }
        switch (action) {
        case 0: { // message: canned or keyboard, straight to this node
            const ChannelIndex channel = nodeDB->getMeshNodeChannel(selected);
            inkhud->openMenu();
            MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
            if (menu)
                menu->composeTo(selected, channel);
            return;
        }
        case 1:
            measure();
            return;
        case 2: {
            float m, b;
            if (!RadarApplet::haveOwnPosition())
                notice = "Sin posición propia";
            else if (!RadarApplet::locate(selected, &m, &b))
                notice = "Ese nodo no tiene posición";
            else if (TrackApplet *t = findApplet<TrackApplet>(AppletNames::TRACK, nullptr)) {
                t->track(selected);
                openChild<TrackApplet>(AppletNames::TRACK);
                return;
            }
            break;
        }
        case 3: {
            const bool fav = nodeInfoLiteIsFavorite(n);
            if (nodeDB->set_favorite(!fav, selected))
                notice = fav ? "Quitado de favoritos" : "Marcado como favorito";
            else
                notice = "No se pudo: límite de favoritos";
            break;
        }
        default:
            if (notice != CONFIRM_FORGET) {
                notice = CONFIRM_FORGET;
                break;
            }
            nodeDB->removeNodeByNum(selected);
            notice.clear();
            rebuild();
            setLevel(LIST);
            requestUpdate(EInk::UpdateTypes::FULL);
            return;
        }
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    void measure()
    {
        StatusApplet *st = status();
        if (!st) {
            notice = "Sin applet Estado";
        } else if (const uint32_t wait = st->traceWaitSeconds()) {
            notice = fmt("Esperá %lus", (unsigned long)wait); // one at a time, with a cooldown
        } else if (st->traceTo(selected)) {
            notice.clear();
            setLevel(RESULT);
            requestUpdate(EInk::UpdateTypes::FULL);
            return;
        } else {
            notice = "No se pudo enviar";
        }
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    // ---------- Drawing ----------
    void renderList()
    {
        drawBar("NODOS", fmt("%u", (unsigned)nodes.size()), true);
        setFont(fontSmall);
        const int16_t y0 = barHeight() + 1;
        const uint16_t rowH = fontSmall.lineHeight() + 5;
        const uint16_t rows = std::max<uint16_t>(1, (height() - y0) / rowH);
        const uint16_t total = nodes.size() + 1; // + Radar
        if (cursor < top)
            top = cursor;
        if (cursor >= top + rows)
            top = cursor - rows + 1;

        for (uint16_t r = 0; r < rows && top + r < total; r++) {
            const uint16_t i = top + r;
            const int16_t y = y0 + r * rowH;
            const uint16_t fg = beginRow(y, rowH, i == cursor);
            const int16_t mid = y + rowH / 2;
            if (i == nodes.size()) {
                printAt(6, mid, parse("Radar: los 10 más cerca"), LEFT, MIDDLE);
                drawChevron(width() - 9, mid, fg);
                setTextColor(BLACK);
                continue;
            }
            const meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(nodes[i]);
            const bool lost = isLost(n);
            const uint32_t a = heardAgo(n);

            // Right: distance + direction, hops
            std::string right = whereTo(nodes[i]);
            const std::string hops = hopsShort(n);
            if (!hops.empty())
                right += (right.empty() ? "" : "  ") + hops;
            uint16_t rightW = 0;
            if (!right.empty()) {
                printAt(width() - 3, mid, right, RIGHT, MIDDLE);
                rightW = getTextWidth(right) + 6;
            }

            // Left: short name (faux bold), then how long ago
            const std::string name = shortNameOf(nodes[i]);
            setCrop(0, y, width() - rightW, rowH);
            printAt(4, mid, name, LEFT, MIDDLE);
            printAt(5, mid, name, LEFT, MIDDLE);
            const std::string when = parse(a == UINT32_MAX ? std::string("nunca") : lost ? "perdido · " + ago(a) : ago(a));
            printAt(42, mid, when, LEFT, MIDDLE);
            resetCrop();
            if (lost) // struck through
                drawLine(3, mid, std::min<int16_t>(width() - rightW, 42 + getTextWidth(when)) + 1, mid, fg);
            setTextColor(BLACK);
        }
        drawScrollHints(top > 0, top + rows < total);
    }

    void renderCard()
    {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(selected);
        drawBar(longNameOf(selected), shortNameOf(selected), true);

        const int16_t y0 = barHeight() + 3;
        const uint16_t gap = 3;
        const uint16_t bw = (width() - 3 * gap) / 2;
        const uint16_t bh = fontSmall.lineHeight() + fontMedium.lineHeight() + 6;
        const int16_t x0 = gap, x1 = 2 * gap + bw, y1 = y0 + bh + gap;

        const uint32_t a = heardAgo(n);
        drawBox(x0, y0, bw, bh, parse("Oído"),
                a == UINT32_MAX ? std::string("nunca") : parse(a < 60 ? std::string("recién") : "hace " + ago(a)), "");
        std::string link = hopsLong(n);
        if (n && n->has_hops_away && n->hops_away == 0)
            link += fmt(" %.0fdB", n->snr);
        drawBox(x1, y0, bw, bh, "Enlace", link, "");
        const std::string where = whereTo(selected);
        drawBox(x0, y1, bw, bh, "Distancia", where.empty() ? std::string("--") : where, "");
        meshtastic_DeviceMetrics dm = meshtastic_DeviceMetrics_init_zero;
        std::string bat = "--";
        if (nodeDB->copyNodeTelemetry(selected, dm) && dm.has_battery_level)
            bat = dm.battery_level > 100 ? std::string("enchufado") : fmt("%lu%%", (unsigned long)dm.battery_level);
        drawBox(x1, y1, bw, bh, parse("Batería"), bat, "");

        const bool fav = n && nodeInfoLiteIsFavorite(n);
        const char *labels[ACTIONS] = {"Mandarle un mensaje", "Medir señal", "Rastrearlo (brújula)",
                                       fav ? "Quitar favorito" : "Marcar favorito", "Olvidar nodo"};
        setFont(fontSmall);
        const uint16_t rowH = fontSmall.lineHeight() + 5;
        int16_t y = y1 + bh + gap + 1;
        for (uint8_t i = 0; i < ACTIONS; i++) {
            const uint16_t fg = beginRow(y, rowH, i == action);
            printAt(6, y + rowH / 2, parse(labels[i]), LEFT, MIDDLE);
            if (i == 2)
                drawChevron(width() - 9, y + rowH / 2, fg);
            setTextColor(BLACK);
            y += rowH;
        }
        if (!notice.empty())
            printAt(4, height() - 2, parse(notice), LEFT, BOTTOM);
    }

    // ---------- Medir señal ----------
    struct Hop {
        NodeNum from, to;
        int8_t snrQ4; // INT8_MIN = unknown
    };

    // The hops of one direction. towards: us -> route[] -> target, SNR as heard by the receiving end.
    // back: target -> route_back[] -> us; our own SNR may be missing (we are the last hop): use rxSnr.
    std::vector<Hop> hops(bool towards)
    {
        StatusApplet *st = status();
        const meshtastic_RouteDiscovery &r = st->lastRoute();
        std::vector<NodeNum> path;
        const NodeNum me = nodeDB->getNodeNum();
        path.push_back(towards ? me : selected);
        const uint32_t *mid = towards ? r.route : r.route_back;
        const pb_size_t count = towards ? r.route_count : r.route_back_count;
        for (pb_size_t i = 0; i < count; i++)
            path.push_back(mid[i]);
        path.push_back(towards ? selected : me);
        const int8_t *snr = towards ? r.snr_towards : r.snr_back;
        const pb_size_t snrCount = towards ? r.snr_towards_count : r.snr_back_count;
        std::vector<Hop> out;
        for (size_t i = 0; i + 1 < path.size(); i++) {
            int8_t q = i < snrCount ? snr[i] : INT8_MIN;
            if (!towards && i + 2 == path.size() && i >= snrCount)
                q = (int8_t)std::clamp<long>(lroundf(st->lastRxSnr() * 4), -127, 127);
            out.push_back({path[i], path[i + 1], q});
        }
        return out;
    }

    std::string snrText(int8_t q) { return q == INT8_MIN ? std::string("?") : fmt("%+.0f dB", q / 4.0f); }

    // "VOS -3 dB » BASE -8 dB » JUAN"
    std::string chain(const std::vector<Hop> &h)
    {
        std::string s;
        for (const Hop &x : h)
            s += shortNameOf(x.from) + " " + snrText(x.snrQ4) + " » ";
        if (!h.empty())
            s += shortNameOf(h.back().to);
        return s;
    }

    static const char *quality(float worstDb) { return worstDb >= -5 ? "bueno" : worstDb >= -12 ? "justo" : "flojo"; }

    void renderResult()
    {
        StatusApplet *st = status();
        drawBar(parse("MEDIR SEÑAL"), shortNameOf(selected), true);
        setFont(fontSmall);
        const uint8_t state = st ? (uint8_t)st->traceStateFor(selected) : (uint8_t)StatusApplet::TRACE_FAILED;
        shownTraceState = state;
        if (state == StatusApplet::TRACE_PENDING) {
            setFont(fontMedium);
            printAt(X(0.5f), Y(0.45f), parse("Midiendo…"), CENTER, MIDDLE);
            setFont(fontSmall);
            printAt(X(0.5f), Y(0.45f) + fontMedium.lineHeight(), "puede tardar hasta 90 s", CENTER, MIDDLE);
            return;
        }
        if (state != StatusApplet::TRACE_DONE) {
            setFont(fontMedium);
            printAt(X(0.5f), Y(0.45f), "Sin respuesta", CENTER, MIDDLE);
            setFont(fontSmall);
            printAt(X(0.5f), Y(0.45f) + fontMedium.lineHeight(), parse(notice.empty() ? std::string("Clic: medir de nuevo") : notice),
                    CENTER, MIDDLE);
            return;
        }

        const std::vector<Hop> out = hops(true), back = hops(false);
        int16_t y = barHeight() + 4;
        const uint16_t lh = fontSmall.lineHeight() + 2;
        auto section = [&](const char *title, const std::vector<Hop> &h) {
            printAt(4, y, title);
            printAt(5, y, title); // faux bold
            y += lh;
            const std::string c = parse(chain(h));
            printWrapped(8, y, width() - 12, c);
            y += getWrappedTextHeight(8, width() - 12, c) + 4;
        };
        section("IDA", out);
        section("VUELTA", back);
        dottedLine(y);
        y += 4;

        // Summary: hops, how good, and the weakest hop of either direction
        Hop worst = {0, 0, INT8_MIN};
        for (const std::vector<Hop> *dir : {&out, &back})
            for (const Hop &h : *dir)
                if (h.snrQ4 != INT8_MIN && (worst.snrQ4 == INT8_MIN || h.snrQ4 < worst.snrQ4))
                    worst = h;
        const unsigned count = out.size();
        std::string summary = fmt("%u salto%s", count, count == 1 ? "" : "s");
        if (worst.snrQ4 != INT8_MIN)
            summary += std::string(" · enlace ") + quality(worst.snrQ4 / 4.0f);
        setFont(fontMedium);
        printAt(4, y, parse(summary));
        y += fontMedium.lineHeight() + 3;
        setFont(fontSmall);
        if (worst.snrQ4 != INT8_MIN && (out.size() + back.size()) > 2)
            printWrapped(4, y, width() - 8,
                         parse("Más flojo: " + shortNameOf(worst.from) + " » " + shortNameOf(worst.to) + " " + snrText(worst.snrQ4)));
        printAt(4, height() - 2, parse(notice.empty() ? std::string("Clic: medir de nuevo") : notice), LEFT, BOTTOM);
    }
};

} // namespace NicheGraphics::InkHUD

#endif
