#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

InkHUD applet: the stock Waypoints list, plus "pick one -> compass" for a single-axis encoder.

Subclasses WaypointListApplet so it keeps the same slot in the applet list (saved settings are by
index) and the menu's "Remove Waypoint" page keeps working (asWaypointListApplet()).

Opened from Inicio > RUMBO:
  PICK    : the waypoints, nearest first, with their distance; turning moves the selection, click
            opens the compass.
  LIST    : the stock list, only while there are no waypoints (its "no waypoints" screen).
  COMPASS : arrow to the waypoint, NORTH UP (no magnetometer: we know the bearing to the target,
            never which way the user faces). Distance + bearing in degrees.
            Encoder short = re-measure now (full refresh). Entering re-measures too.
BOOT short or encoder long closes the compass; from PICK / LIST they reach InkHudNavigator, which goes
back to the RUMBO card.

*/

#include "graphics/niche/InkHUD/Applets/User/Waypoints/WaypointListApplet.h"

#include "CalandriaUI.h"

#include "GeoCoord.h"
#include "NodeDB.h"
#include "RTC.h"
#include "WaypointStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace NicheGraphics::InkHUD
{

class WaypointCompassApplet : public CalandriaUI<WaypointListApplet>
{
  public:
    // Entered from RUMBO: the list, nearest first
    void enter()
    {
        cursor = 0;
        scroll = 0;
        sortByDistance();
        setLevel(topLevel());
    }

    void onActivate() override
    {
        WaypointListApplet::onActivate();
        setLevel(topLevel());
    }

    void onForeground() override { WaypointListApplet::onForeground(); }

    void onRender(bool full) override
    {
        if (level == LIST) {
            WaypointListApplet::onRender(full);
            return;
        }
        const StoredWaypoint *wp = selected();
        if (!wp) { // waypoint list changed under us (expired / deleted)
            setLevel(LIST);
            WaypointListApplet::onRender(full);
            return;
        }
        if (level == PICK)
            renderPick();
        else
            renderCompass(wp->waypoint);
    }

    void onButtonShortPress() override
    {
        if (waypointStore.getWaypoints().empty())
            return;
        if (level == LIST) {
            cursor = 0;
            sortByDistance();
            setLevel(PICK);
            requestUpdate(EInk::UpdateTypes::FAST);
        } else if (level == PICK) {
            setLevel(COMPASS);
            requestUpdate(EInk::UpdateTypes::FULL); // fresh measurement + clean panel
        } else {
            requestUpdate(EInk::UpdateTypes::FULL); // re-measure now
        }
    }

    void onButtonLongPress() override { back(); }
    void onExitShort() override { back(); }

    void onNavUp() override
    {
        if (level == PICK && cursor > 0) {
            cursor--;
            requestUpdate(EInk::UpdateTypes::FAST);
        }
    }

    void onNavDown() override
    {
        if (level == PICK && cursor + 1 < waypointStore.getWaypoints().size()) {
            cursor++;
            requestUpdate(EInk::UpdateTypes::FAST);
        }
    }

  private:

    enum Level { LIST, PICK, COMPASS };
    Level level = LIST;
    uint16_t cursor = 0;
    uint16_t scroll = 0;
    std::vector<uint16_t> order; // indexes into the store, nearest first

    Level topLevel() { return waypointStore.getWaypoints().empty() ? LIST : PICK; }

    // Short press is always ours (enter / select / re-measure), nav while picking. Long press and
    // EXIT only on the compass: from PICK / LIST they belong to InkHudNavigator (back to RUMBO).
    void setLevel(Level l)
    {
        level = l;
        setInputsSubscribed(BUTTON_SHORT, true);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, l == COMPASS);
        setInputsSubscribed(NAV_UP | NAV_DOWN, l == PICK);
    }

    void back()
    {
        if (level != COMPASS)
            return;
        setLevel(PICK);
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    // Distance from us, or -1 if either position is unknown
    float distanceTo(const meshtastic_Waypoint &w)
    {
        meshtastic_PositionLite own = meshtastic_PositionLite_init_zero;
        if (!w.has_latitude_i || !w.has_longitude_i || !ownPosition(own))
            return -1;
        return GeoCoord::latLongToMeter(own.latitude_i * 1e-7, own.longitude_i * 1e-7, w.latitude_i * 1e-7,
                                        w.longitude_i * 1e-7);
    }

    // Nearest first; without a distance, at the end in store order
    void sortByDistance()
    {
        const auto &wps = waypointStore.getWaypoints();
        std::vector<std::pair<float, uint16_t>> d;
        for (uint16_t i = 0; i < wps.size(); i++) {
            const float m = distanceTo(wps.at(i).waypoint);
            d.push_back({m < 0 ? 1e12f + i : m, i});
        }
        std::sort(d.begin(), d.end());
        order.clear();
        for (const auto &x : d)
            order.push_back(x.second);
    }

    const StoredWaypoint *selected()
    {
        const auto &wps = waypointStore.getWaypoints();
        if (wps.empty())
            return nullptr;
        if (order.size() != wps.size())
            sortByDistance(); // added / expired under us
        if (cursor >= wps.size())
            cursor = wps.size() - 1;
        return &wps.at(order[cursor]);
    }

    // Same source as the stock list: our node's position in NodeDB (GPS, phone, or fixed).
    bool ownPosition(meshtastic_PositionLite &out)
    {
        return nodeDB->copyNodePosition(nodeDB->getNodeNum(), out) && (out.latitude_i != 0 || out.longitude_i != 0);
    }

    std::string nameOf(const meshtastic_Waypoint &w)
    {
        if (w.name[0])
            return parse(w.name);
        char buf[20];
        snprintf(buf, sizeof(buf), "Waypoint 0x%x", (unsigned)w.id);
        return buf;
    }

    void renderPick()
    {
        const auto &wps = waypointStore.getWaypoints();
        if (order.size() != wps.size())
            sortByDistance();
        drawBar("WAYPOINTS", std::to_string(wps.size()), true);
        setFont(fontSmall);
        const int16_t top = barHeight() + 1;
        const uint16_t rowH = fontSmall.lineHeight() + 6;
        uint16_t rows = std::max<uint16_t>(1, (height() - top) / rowH);
        if (cursor < scroll)
            scroll = cursor;
        if (cursor >= scroll + rows)
            scroll = cursor - rows + 1;

        for (uint16_t r = 0; r < rows && scroll + r < wps.size(); r++) {
            const uint16_t i = scroll + r;
            const int16_t y = top + r * rowH;
            const uint16_t fg = beginRow(y, rowH, i == cursor);
            const meshtastic_Waypoint &w = wps.at(order[i]).waypoint;
            const float m = distanceTo(w);
            uint16_t rightW = 0;
            if (m >= 0) {
                const std::string d = localizeDistance((uint32_t)lroundf(m));
                printAt(width() - 4, y + rowH / 2, d, RIGHT, MIDDLE);
                rightW = getTextWidth(d) + 8;
            }
            setCrop(0, y, width() - rightW, rowH);
            printAt(6, y + rowH / 2, nameOf(w), LEFT, MIDDLE);
            resetCrop();
            (void)fg;
            setTextColor(BLACK);
        }
        drawScrollHints(scroll > 0, scroll + rows < wps.size());
    }

    void renderCompass(const meshtastic_Waypoint &w)
    {
        setFont(fontSmall);
        drawBar(nameOf(w), "", true);

        const int16_t top = getHeaderHeight();
        const int16_t textH = fontMedium.lineHeight() + 2 * fontSmall.lineHeight() + 8;
        const int16_t areaH = height() - top - textH;
        const int16_t cx = width() / 2;
        const int16_t cy = top + areaH / 2;
        const int16_t r = std::max<int16_t>(10, std::min<int16_t>(width(), areaH) / 2 - 8);

        // Dial: north is always up
        drawCircle(cx, cy, r, BLACK);
        setFont(fontSmall);
        printAt(cx, cy - r - 1, "N", CENTER, BOTTOM);

        meshtastic_PositionLite own = meshtastic_PositionLite_init_zero;
        const bool haveWp = w.has_latitude_i && w.has_longitude_i;
        const bool haveOwn = ownPosition(own);
        const int16_t lineY = top + areaH + 2;

        if (!haveWp || !haveOwn) {
            setFont(fontMedium);
            printAt(cx, lineY, parse(haveWp ? "Sin posición" : "Sin coordenadas"), CENTER, TOP);
            setFont(fontSmall);
            printAt(cx, lineY + fontMedium.lineHeight() + 2, parse(haveWp ? "falta GPS / celular" : "waypoint sin ubicación"),
                    CENTER, TOP);
            return;
        }

        const double lat1 = own.latitude_i * 1e-7, lon1 = own.longitude_i * 1e-7;
        const double lat2 = w.latitude_i * 1e-7, lon2 = w.longitude_i * 1e-7;
        const float meters = GeoCoord::latLongToMeter(lat1, lon1, lat2, lon2);
        float bearing = GeoCoord::bearing(lat1, lon1, lat2, lon2); // radians, clockwise from north
        int deg = (int)lroundf(bearing * 180.0f / (float)M_PI);
        deg = ((deg % 360) + 360) % 360;

        drawArrow(cx, cy, r - 4, bearing);

        char buf[24];
        snprintf(buf, sizeof(buf), "%d° %s  (norte arriba)", deg, cardinal(deg));
        setFont(fontMedium);
        printAt(cx, lineY, localizeDistance((uint32_t)lroundf(std::max(0.0f, meters))), CENTER, TOP);
        setFont(fontSmall);
        printAt(cx, lineY + fontMedium.lineHeight() + 2, parse(buf), CENTER, TOP);
        // Our position comes from the phone (no GPS yet) and may be old: say how old, so a stale
        // arrow isn't trusted blindly.
        printAt(cx, lineY + fontMedium.lineHeight() + fontSmall.lineHeight() + 4, parse(positionAge(own)), CENTER, TOP);
    }

    // Arrow from the center toward `bearing` (screen y grows downward, so north = -y)
    void drawArrow(int16_t cx, int16_t cy, int16_t len, float bearing)
    {
        const float s = sinf(bearing), c = cosf(bearing);
        auto px = [&](float along, float side) { return (int16_t)lroundf(cx + along * s + side * c); };
        auto py = [&](float along, float side) { return (int16_t)lroundf(cy - along * c + side * s); };

        const float head = len * 0.35f, half = len * 0.22f;
        // shaft (3 px wide) from the tail to the base of the head
        for (int8_t o = -1; o <= 1; o++)
            drawLine(px(-len * 0.6f, o), py(-len * 0.6f, o), px(len - head, o), py(len - head, o), BLACK);
        // head
        fillTriangle(px(len, 0), py(len, 0), px(len - head, -half), py(len - head, -half), px(len - head, half),
                     py(len - head, half), BLACK);
        fillCircle(cx, cy, 2, BLACK);
    }

    std::string positionAge(const meshtastic_PositionLite &p)
    {
        const uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
        if (!p.time || !now)
            return "posición: hora desconocida";
        const uint32_t s = now > p.time ? now - p.time : 0;
        char buf[40];
        if (s < 60)
            snprintf(buf, sizeof(buf), "posición recién tomada");
        else if (s < 3600)
            snprintf(buf, sizeof(buf), "posición de hace %u min", (unsigned)(s / 60));
        else if (s < 86400)
            snprintf(buf, sizeof(buf), "posición de hace %u h", (unsigned)(s / 3600));
        else
            snprintf(buf, sizeof(buf), "posición de hace %u d", (unsigned)(s / 86400));
        return buf;
    }

    static const char *cardinal(int deg)
    {
        static const char *names[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
        return names[((deg + 22) / 45) % 8];
    }
};

} // namespace NicheGraphics::InkHUD

#endif
