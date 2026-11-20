#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

InkHUD applet: the stock Waypoints list, plus "pick one -> compass" for a single-axis encoder.

Subclasses WaypointListApplet so it keeps the same slot in the applet list (saved settings are by
index) and the menu's "Remove Waypoint" page keeps working (asWaypointListApplet()).

Three levels:
  LIST    : stock list, untouched. Turning changes applet (we drop the base's NAV grab).
  PICK    : "Waypoints *": turning moves a cursor, encoder short opens the compass.
  COMPASS : arrow to the waypoint, NORTH UP (no magnetometer: we know the bearing to the target,
            never which way the user faces). Distance + bearing in degrees.
            Encoder short = re-measure now (full refresh). Entering re-measures too.
BOOT short or encoder long goes back one level.

*/

#include "graphics/niche/InkHUD/Applets/User/Waypoints/WaypointListApplet.h"

#include "GeoCoord.h"
#include "NodeDB.h"
#include "RTC.h"
#include "WaypointStore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace NicheGraphics::InkHUD
{

class WaypointCompassApplet : public WaypointListApplet
{
  public:
    void onActivate() override
    {
        WaypointListApplet::onActivate();
        setLevel(LIST);
    }

    void onForeground() override
    {
        WaypointListApplet::onForeground();
        setLevel(LIST);
    }

    void onRender(bool full) override
    {
        if (level == LIST) {
            WaypointListApplet::onRender(full);
            return;
        }
        const StoredWaypoint *wp = selected();
        if (!wp) { // waypoint list changed under us (expired / deleted)
            level = LIST;
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
            requestUpdate(EInk::UpdateTypes::FAST, false);
        }
    }

    void onNavDown() override
    {
        if (level == PICK && cursor + 1 < waypointStore.getWaypoints().size()) {
            cursor++;
            requestUpdate(EInk::UpdateTypes::FAST, false);
        }
    }

  private:
    enum Level { LIST, PICK, COMPASS };
    Level level = LIST;
    uint16_t cursor = 0;
    uint16_t scroll = 0;

    // Short press is always ours (enter / select / re-measure). Nav, long press and EXIT only
    // below LIST, so on the stock list turning changes applet and BOOT opens the menu.
    void setLevel(Level l)
    {
        level = l;
        setInputsSubscribed(BUTTON_SHORT, true);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, l != LIST);
        setInputsSubscribed(NAV_UP | NAV_DOWN, l == PICK);
    }

    void back()
    {
        if (level == LIST)
            return;
        setLevel(level == COMPASS ? PICK : LIST);
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    const StoredWaypoint *selected()
    {
        const auto &wps = waypointStore.getWaypoints();
        if (wps.empty())
            return nullptr;
        if (cursor >= wps.size())
            cursor = wps.size() - 1;
        return &wps.at(cursor);
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
        setFont(fontSmall);
        drawHeader("Waypoints *");

        const int16_t top = getHeaderHeight() + 2;
        const uint16_t rowH = fontSmall.lineHeight() + 2;
        uint16_t rows = (height() - top) / rowH;
        if (rows < 1)
            rows = 1;
        if (cursor < scroll)
            scroll = cursor;
        if (cursor >= scroll + rows)
            scroll = cursor - rows + 1;

        for (uint16_t r = 0; r < rows && scroll + r < wps.size(); r++) {
            const uint16_t i = scroll + r;
            const int16_t y = top + r * rowH;
            const bool sel = i == cursor;
            if (sel)
                fillRect(0, y, width(), rowH, BLACK);
            setTextColor(sel ? WHITE : BLACK);
            printAt(2, y + 1, nameOf(wps.at(i).waypoint));
        }
        setTextColor(BLACK);
        drawScrollHints(scroll > 0, scroll + rows < wps.size());
    }

    void renderCompass(const meshtastic_Waypoint &w)
    {
        setFont(fontSmall);
        drawHeader(nameOf(w));

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
