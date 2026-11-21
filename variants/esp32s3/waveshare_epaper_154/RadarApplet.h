#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

InkHUD applet "Radar": where the nodes are, NORTH UP. This board has no magnetometer, so the ring
never follows the user's heading (we know the bearing to a node, not which way the user faces).

  - Dots: the RADAR_MAX_DOTS nearest nodes with a known position, placed by bearing and distance
    (linear scale; the ring is the farthest plotted node, printed in the footer).
  - Arrow: points at the TARGET. By default the target is the nearest node; from Nodes -> a node ->
    "Radar" it is locked onto that node (header "Radar: NAME"). Encoder short-press while locked goes
    back to "nearest".

Redraws when a position packet arrives (throttled) and every RADAR_REFRESH_MS while on screen, because
OUR position comes from the phone (not a mesh packet) and changes while moving. It only redraws if
what is shown changed (target, distance, 10-degree bearing step, dot count).

*/

#include "graphics/niche/InkHUD/Applet.h"

#include "NodeDB.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "gps/GeoCoord.h"
#include "mesh/Throttle.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef RADAR_REFRESH_MS
#define RADAR_REFRESH_MS (20 * 1000UL)
#endif
#define RADAR_MAX_DOTS 10

namespace NicheGraphics::InkHUD
{

class RadarApplet : public Applet, public SinglePortModule, public concurrency::OSThread
{
  public:
    RadarApplet() : SinglePortModule("RadarApplet", meshtastic_PortNum_POSITION_APP), concurrency::OSThread("RadarApplet")
    {
        OSThread::disable();
    }

    // Lock the arrow onto one node (0 = back to the nearest node)
    void setTarget(NodeNum num)
    {
        target = num;
        setInputsSubscribed(BUTTON_SHORT, target != 0); // short-press = unlock; otherwise InkHUD's default
        lastSignature.clear();
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    // Shared with the Nodes card: distance (m) + bearing (radians) from us to a node
    static bool locate(NodeNum num, float *meters, float *bearing)
    {
        if (!haveOwnPosition())
            return false;
        meshtastic_PositionLite pos;
        if (!nodeDB->copyNodePosition(num, pos) || (pos.latitude_i == 0 && pos.longitude_i == 0))
            return false;
        const double lat0 = localPosition.latitude_i * 1e-7, lon0 = localPosition.longitude_i * 1e-7;
        const double lat = pos.latitude_i * 1e-7, lon = pos.longitude_i * 1e-7;
        *meters = GeoCoord::latLongToMeter(lat0, lon0, lat, lon);
        *bearing = GeoCoord::bearing(lat0, lon0, lat, lon);
        return true;
    }

    static bool haveOwnPosition() { return localPosition.latitude_i != 0 || localPosition.longitude_i != 0; }

    static const char *cardinal(float bearingRad)
    {
        static const char *names[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
        int deg = (int)lroundf(bearingRad * 180.0f / (float)M_PI);
        deg = ((deg % 360) + 360) % 360;
        return names[((deg + 22) / 45) % 8];
    }

    void onRender(bool full) override
    {
        (void)full;
        setFont(fontSmall);
        const Scene sc = scene();
        lastSignature = sc.signature;

        drawHeader(target ? "Radar: " + nameOf(target) : std::string("Radar"));

        if (!haveOwnPosition()) {
            printAt(X(0.5f), Y(0.45f), "Sin GPS", CENTER, MIDDLE);
            printAt(X(0.5f), Y(0.62f), parse("Compartí la ubicación del celu"), CENTER, MIDDLE);
            return;
        }

        const int16_t footer = fontSmall.lineHeight() * 2 + 6;
        const int16_t bodyTop = getHeaderHeight() + 4;
        int16_t bodyH = std::max<int16_t>(32, height() - bodyTop - footer);
        int16_t radius = std::max<int16_t>(16, std::min<int16_t>(width() / 2, bodyH / 2) - 8);
        const int16_t cx = width() / 2;
        const int16_t cy = bodyTop + bodyH / 2;

        drawCircle(cx, cy, radius, BLACK);
        drawCircle(cx, cy, radius - 1, BLACK);
        printAt(cx, cy - radius + fontSmall.lineHeight(), "N", CENTER, MIDDLE);
        fillCircle(cx, cy, 2, BLACK);

        // Other nodes as dots, scaled so the farthest plotted one sits just inside the ring
        const float scale = sc.ringMeters > 0 ? (radius - 5) / sc.ringMeters : 0;
        for (const Blip &b : sc.dots) {
            const float r = std::min<float>(b.meters * scale, radius - 5);
            const int16_t x = cx + (int16_t)(r * sinf(b.bearing));
            const int16_t y = cy - (int16_t)(r * cosf(b.bearing));
            if (b.num == sc.target.num)
                drawCircle(x, y, 4, BLACK); // the target's real position, around its dot
            fillCircle(x, y, 2, BLACK);
        }

        if (!sc.haveTarget) {
            printAt(X(0.5f), height() - 1, target ? nameOf(target) + parse(": sin posición") : "Sin nodos con posición",
                    CENTER, BOTTOM);
            return;
        }
        if (sc.target.meters >= 5)
            drawArrow(cx, cy, (int16_t)(radius * 0.6f), sc.target.bearing);

        // Names are already converted for the font (parseShortName): only parse our own literals
        const std::string where = sc.target.meters < 5 ? parse("  aquí")
                                                        : "  " + localizeDistance((uint32_t)sc.target.meters) + " " +
                                                              cardinal(sc.target.bearing);
        printAt(X(0.5f), height() - fontSmall.lineHeight() - 3, nameOf(sc.target.num) + where, CENTER, BOTTOM);
        char line[48];
        snprintf(line, sizeof(line), "anillo %s  %u nodos", localizeDistance((uint32_t)sc.ringMeters).c_str(),
                 (unsigned)sc.dots.size());
        printAt(X(0.5f), height() - 1, line, CENTER, BOTTOM);
    }

    void onButtonShortPress() override { setTarget(0); }

  protected:
    void onActivate() override
    {
        loopbackOk = true;
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(RADAR_REFRESH_MS);
        setInputsSubscribed(BUTTON_SHORT, target != 0);
    }
    void onDeactivate() override
    {
        loopbackOk = false;
        OSThread::disable();
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        (void)mp;
        if (isForeground() && !Throttle::isWithinTimespanMs(lastRefreshMs, 4000)) {
            lastRefreshMs = millis();
            redrawIfChanged();
        }
        return ProcessMessage::CONTINUE;
    }

    int32_t runOnce() override
    {
        if (isForeground())
            redrawIfChanged(); // our own position (from the phone) moves without any mesh packet
        return RADAR_REFRESH_MS;
    }

  private:
    struct Blip {
        NodeNum num = 0;
        float meters = 0, bearing = 0;
    };
    struct Scene {
        std::vector<Blip> dots;
        Blip target;
        bool haveTarget = false;
        float ringMeters = 0;
        std::string signature;
    };

    NodeNum target = 0; // 0 = nearest
    uint32_t lastRefreshMs = 0;
    std::string lastSignature;

    Scene scene()
    {
        Scene sc;
        if (!haveOwnPosition())
            return sc;
        std::vector<Blip> all;
        for (NodeNum num : nodeDB->snapshotPositionNodeNums(nodeDB->getNodeNum())) {
            Blip b;
            b.num = num;
            if (locate(num, &b.meters, &b.bearing))
                all.push_back(b);
        }
        std::sort(all.begin(), all.end(), [](const Blip &a, const Blip &b) { return a.meters < b.meters; });

        if (target) {
            for (const Blip &b : all)
                if (b.num == target)
                    sc.target = b, sc.haveTarget = true;
        } else if (!all.empty()) {
            sc.target = all.front();
            sc.haveTarget = true;
        }

        for (const Blip &b : all) {
            if (sc.dots.size() >= RADAR_MAX_DOTS)
                break;
            sc.dots.push_back(b);
        }
        // A locked target far away must still be on the radar (and set the scale)
        if (sc.haveTarget && std::none_of(sc.dots.begin(), sc.dots.end(), [&](const Blip &b) { return b.num == sc.target.num; }))
            sc.dots.push_back(sc.target);
        for (const Blip &b : sc.dots)
            sc.ringMeters = std::max(sc.ringMeters, b.meters);

        // What the screen shows, coarsely: distance in 1% steps (min 10 m), bearing in 10-degree steps
        char buf[40];
        snprintf(buf, sizeof(buf), "%08x:%lu:%d:%u", (unsigned)sc.target.num,
                 (unsigned long)(sc.target.meters / std::max(10.0f, sc.target.meters / 100.0f)),
                 (int)(sc.target.bearing * 18.0f / (float)M_PI), (unsigned)sc.dots.size());
        sc.signature = buf;
        return sc;
    }

    void redrawIfChanged()
    {
        if (scene().signature != lastSignature)
            requestUpdate(EInk::UpdateTypes::FAST);
    }

    std::string nameOf(NodeNum num)
    {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
        return node ? parseShortName(node) : hexifyNodeNum(num);
    }

    // along is toward the target, across is to the right. Bearing 0 is north, up the screen.
    void drawArrow(int16_t cx, int16_t cy, int16_t size, float bearing)
    {
        const float s = sinf(bearing);
        const float c = cosf(bearing);
        auto pt = [&](float along, float across, int16_t &ox, int16_t &oy) {
            ox = cx + (int16_t)(across * c + along * s);
            oy = cy + (int16_t)(across * s - along * c);
        };
        int16_t tipX, tipY, leftX, leftY, rightX, rightY, notchX, notchY;
        pt(size, 0, tipX, tipY);
        pt(-size * 0.2f, -size * 0.45f, leftX, leftY);
        pt(-size * 0.2f, size * 0.45f, rightX, rightY);
        pt(size * 0.15f, 0, notchX, notchY);
        fillTriangle(tipX, tipY, leftX, leftY, notchX, notchY, BLACK);
        fillTriangle(tipX, tipY, rightX, rightY, notchX, notchY, BLACK);
    }
};

} // namespace NicheGraphics::InkHUD

#endif
