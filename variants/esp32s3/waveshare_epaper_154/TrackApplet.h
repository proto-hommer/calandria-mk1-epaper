#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

RUMBO > "Rastrear un nodo" and "Mi posición" (one applet, two modes, so it takes one slot).

  TRACK : a big arrow to a node, NORTH UP (no magnetometer: we know the bearing to the node, never which
          way the user faces), distance and bearing ("NO 293°"). Nodes with a known position, nearest
          first; turning changes node, click re-measures. track(num) starts on a given node (from its card).
  MINE  : our own coordinates and how long ago the phone sent them (OwnPosition.h).

*/

#include "CalandriaUI.h"

#include "OwnPosition.h"
#include "RadarApplet.h"

#include "NodeDB.h"
#include "concurrency/OSThread.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#ifndef TRACK_REFRESH_MS
#define TRACK_REFRESH_MS (30 * 1000UL)
#endif

namespace NicheGraphics::InkHUD
{

class TrackApplet : public CalandriaUI<Applet>, public concurrency::OSThread
{
  public:
    TrackApplet() : concurrency::OSThread("TrackApplet") { OSThread::disable(); }

    enum Mode : uint8_t { TRACK, MINE };

    // From RUMBO: nearest node first / our position
    void enter(Mode m)
    {
        mode = m;
        target = 0;
        rebuild();
        subscribe();
    }

    // From a node's card: start on that node
    void track(NodeNum num)
    {
        mode = TRACK;
        target = num;
        rebuild();
        subscribe();
    }

    void onActivate() override
    {
        subscribe();
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(TRACK_REFRESH_MS);
    }
    void onDeactivate() override { OSThread::disable(); }

    void onRender(bool full) override
    {
        (void)full;
        lastSignature = signature();
        if (mode == MINE)
            renderMine();
        else
            renderTrack();
    }

    void onButtonShortPress() override // re-measure now
    {
        rebuild();
        requestUpdate(EInk::UpdateTypes::FULL);
    }
    void onNavUp() override { step(-1); }
    void onNavDown() override { step(1); }

  protected:
    int32_t runOnce() override
    {
        if (isForeground()) {
            if (mode == TRACK)
                rebuild();
            if (signature() != lastSignature)
                requestUpdate(EInk::UpdateTypes::FAST);
        }
        return TRACK_REFRESH_MS;
    }

  private:
    Mode mode = TRACK;
    NodeNum target = 0;
    std::vector<NodeNum> nodes; // with a position, nearest first
    std::string lastSignature;

    void subscribe()
    {
        setInputsSubscribed(BUTTON_SHORT, mode == TRACK);
        setInputsSubscribed(NAV_UP | NAV_DOWN, mode == TRACK);
    }

    void rebuild()
    {
        nodes.clear();
        const NodeNum me = nodeDB->getNodeNum();
        std::vector<std::pair<float, NodeNum>> found;
        for (size_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
            const meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            float meters, bearing;
            if (n && n->num != me && RadarApplet::locate(n->num, &meters, &bearing))
                found.push_back({meters, n->num});
        }
        std::sort(found.begin(), found.end());
        for (const auto &f : found)
            nodes.push_back(f.second);
        if (!target && !nodes.empty())
            target = nodes.front();
    }

    int indexOfTarget()
    {
        for (size_t i = 0; i < nodes.size(); i++)
            if (nodes[i] == target)
                return (int)i;
        return -1;
    }

    void step(int dir)
    {
        if (nodes.empty())
            return;
        int i = indexOfTarget();
        i = i < 0 ? 0 : (i + dir + (int)nodes.size()) % (int)nodes.size(); // wraps around
        target = nodes[i];
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    std::string signature()
    {
        if (mode == MINE)
            return OwnPosition::statusLine() + std::to_string(localPosition.latitude_i) + std::to_string(localPosition.longitude_i);
        float meters = 0, bearing = 0;
        RadarApplet::locate(target, &meters, &bearing);
        return std::to_string(target) + fmt("%.0f/%d", meters / 10, bearingDegrees(bearing)); // 10 m steps
    }

    // How old the node's position is
    std::string positionAge(NodeNum num)
    {
        meshtastic_PositionLite p;
        if (!nodeDB->copyNodePosition(num, p) || !p.time)
            return "";
        const uint32_t s = secondsSince(p.time);
        return s == UINT32_MAX ? "" : "su posición: hace " + ago(s);
    }

    void renderTrack()
    {
        const int idx = indexOfTarget();
        drawBar(idx >= 0 ? "RASTREAR " + shortNameOf(target) : std::string("RASTREAR"),
                idx >= 0 ? fmt("%d/%u", idx + 1, (unsigned)nodes.size()) : std::string(""), true);
        const int16_t top = barHeight();
        setFont(fontSmall);
        if (!RadarApplet::haveOwnPosition()) {
            printAt(X(0.5f), Y(0.45f), parse("Sin posición propia"), CENTER, MIDDLE);
            printAt(X(0.5f), Y(0.45f) + fontSmall.lineHeight() + 4, parse("conectá el celu con GPS"), CENTER, MIDDLE);
            return;
        }
        float meters, bearing;
        if (idx < 0 || !RadarApplet::locate(target, &meters, &bearing)) {
            printAt(X(0.5f), Y(0.5f), parse(nodes.empty() ? "Ningún nodo con posición" : "Ese nodo no tiene posición"), CENTER,
                    MIDDLE);
            return;
        }

        // Name, then the dial, then distance + bearing
        setFont(fontMedium);
        setCrop(0, top, width(), fontMedium.lineHeight() + 4);
        printAt(X(0.5f), top + 2, longNameOf(target), CENTER, TOP);
        resetCrop();
        const int16_t dialTop = top + fontMedium.lineHeight() + 6;
        const int16_t textH = fontMedium.lineHeight() + 2 * fontSmall.lineHeight() + 8;
        const int16_t areaH = height() - dialTop - textH;
        const int16_t cx = width() / 2, cy = dialTop + areaH / 2;
        const int16_t r = std::max<int16_t>(10, std::min<int16_t>(width(), areaH) / 2 - 6);
        drawCircle(cx, cy, r, BLACK);
        setFont(fontSmall);
        printAt(cx, cy - r - 1, "N", CENTER, BOTTOM);
        drawArrow(cx, cy, r - 4, bearing);

        const int deg = bearingDegrees(bearing);
        int16_t y = dialTop + areaH + 2;
        setFont(fontMedium);
        printAt(cx, y, localizeDistance((uint32_t)lroundf(std::max(0.0f, meters))) + "   " + fmt("%s %d", cardinal(deg), deg) +
                           parse("°"),
                CENTER, TOP);
        y += fontMedium.lineHeight() + 2;
        setFont(fontSmall);
        printAt(cx, y, "norte arriba", CENTER, TOP);
        y += fontSmall.lineHeight() + 2;
        const std::string age = positionAge(target);
        if (!age.empty())
            printAt(cx, y, parse(age), CENTER, TOP);
    }

    void renderMine()
    {
        drawBar(parse("MI POSICIÓN"), "", true);
        setFont(fontSmall);
        const uint16_t lh = fontMedium.lineHeight() + 4;
        int16_t y = barHeight() + 8;
        if (!OwnPosition::have()) {
            printWrapped(4, y, width() - 8, parse("Sin posición: el MK1 no tiene GPS. Conectá el celu con CalandriApp."));
            return;
        }
        setFont(fontSmall);
        printAt(4, y, "Latitud");
        setFont(fontMedium);
        printAt(width() - 4, y, fmt("%.6f", localPosition.latitude_i * 1e-7), RIGHT, TOP);
        y += lh;
        setFont(fontSmall);
        printAt(4, y, "Longitud");
        setFont(fontMedium);
        printAt(width() - 4, y, fmt("%.6f", localPosition.longitude_i * 1e-7), RIGHT, TOP);
        y += lh;
        if (localPosition.altitude) {
            setFont(fontSmall);
            printAt(4, y, "Altura");
            setFont(fontMedium);
            printAt(width() - 4, y, fmt("%ld m", (long)localPosition.altitude), RIGHT, TOP);
            y += lh;
        }
        y += 4;
        dottedLine(y);
        y += 6;
        setFont(fontSmall);
        printWrapped(4, y, width() - 8, parse(OwnPosition::statusLine()));
    }
};

} // namespace NicheGraphics::InkHUD

#endif
