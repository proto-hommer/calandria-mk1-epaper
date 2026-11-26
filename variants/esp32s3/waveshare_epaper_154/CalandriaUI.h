#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shared look of the Calandria screens, as a mixin over any InkHUD applet (Applet, WaypointListApplet...):

  - a black title bar: "< TITLE" on the left, one datum on the right, stopping short of InkHUD's
    battery icon (a system applet that draws itself in the top-right corner);
  - rows drawn inverted when selected (black bar, white text);
  - small pictograms drawn with primitives, so they invert with the row;
  - "hace cuánto", node names, cardinal points.

E-paper: moving a selection is a FAST refresh (no flashing); a FULL one belongs to level changes.

*/

#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "graphics/niche/InkHUD/Persistence.h"
#include "graphics/niche/InkHUD/SystemApplet.h"
#include "graphics/niche/InkHUD/Tile.h"

#include "NodeDB.h"
#include "PowerStatus.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>

namespace NicheGraphics::InkHUD
{

template <class Base> class CalandriaUI : public Base
{
  protected:
    uint16_t barHeight() { return Applet::getHeaderHeight(); }

    // Right edge of the title bar: InkHUD's battery icon draws itself past it
    int16_t barRight()
    {
        if (!this->settings->optionalFeatures.batteryIcon)
            return this->width();
        Tile *t = ((Applet *)this->inkhud->getSystemApplet("BatteryIcon"))->getTile();
        return t ? t->getLeft() - 3 : this->width();
    }

    // Black title bar. backArrow: there is a level above ("<"). bolt: plugged in.
    void drawBar(const std::string &left, const std::string &right = "", bool backArrow = true, bool bolt = false)
    {
        const int16_t r = barRight();
        const uint16_t h = barHeight();
        this->fillRect(0, 0, r, h, BLACK);
        this->setFont(Applet::fontSmall);
        this->setTextColor(WHITE);
        int16_t x = 3;
        if (backArrow) {
            const int16_t cy = h / 2;
            for (int k = 0; k < 2; k++) {
                this->drawLine(x + 4 + k, cy - 4, x + k, cy, WHITE);
                this->drawLine(x + k, cy, x + 4 + k, cy + 4, WHITE);
            }
            x += 10;
        }
        int16_t rx = r - 3;
        if (!right.empty()) {
            this->printAt(rx, h / 2, right, Applet::RIGHT, Applet::MIDDLE);
            rx -= this->getTextWidth(right) + 4;
        }
        if (bolt) {
            drawBolt(rx - 4, h / 2, WHITE);
            rx -= 10;
        }
        this->setCrop(0, 0, rx > x ? rx - 2 : x + 1, h);
        this->printAt(x, h / 2, left, Applet::LEFT, Applet::MIDDLE);
        this->resetCrop();
        this->setTextColor(BLACK);
    }

    // A row's background; returns the color to draw its contents with
    uint16_t beginRow(int16_t y, uint16_t h, bool selected)
    {
        if (selected)
            this->fillRect(0, y, this->width(), h, BLACK);
        const uint16_t fg = selected ? WHITE : BLACK;
        this->setTextColor(fg);
        return fg;
    }

    void dottedLine(int16_t y)
    {
        for (int16_t x = 0; x < this->width(); x += 2)
            this->drawPixel(x, y, BLACK);
    }

    // ">" at the right end of a row: opens something
    void drawChevron(int16_t cx, int16_t cy, uint16_t color)
    {
        this->drawLine(cx, cy - 4, cx + 4, cy, color);
        this->drawLine(cx + 4, cy, cx, cy + 4, color);
    }

    void drawBolt(int16_t cx, int16_t cy, uint16_t color)
    {
        this->fillTriangle(cx + 1, cy - 5, cx - 3, cy + 1, cx, cy + 1, color);
        this->fillTriangle(cx - 1, cy + 5, cx + 3, cy - 1, cx, cy - 1, color);
    }

    void drawCheck(int16_t x, int16_t cy, uint16_t color)
    {
        for (int k = 0; k < 2; k++) {
            this->drawLine(x, cy + k, x + 3, cy + 3 + k, color);
            this->drawLine(x + 3, cy + 3 + k, x + 9, cy - 3 + k, color);
        }
    }

    // Arrow from the center towards a bearing (radians, clockwise from north; north is up)
    void drawArrow(int16_t cx, int16_t cy, int16_t len, float bearing, uint16_t color = BLACK)
    {
        const float s = sinf(bearing), c = cosf(bearing);
        auto px = [&](float along, float side) { return (int16_t)lroundf(cx + along * s + side * c); };
        auto py = [&](float along, float side) { return (int16_t)lroundf(cy - along * c + side * s); };
        const float head = len * 0.35f, half = len * 0.22f;
        for (int8_t o = -1; o <= 1; o++)
            this->drawLine(px(-len * 0.6f, o), py(-len * 0.6f, o), px(len - head, o), py(len - head, o), color);
        this->fillTriangle(px(len, 0), py(len, 0), px(len - head, -half), py(len - head, -half), px(len - head, half),
                           py(len - head, half), color);
        this->fillCircle(cx, cy, 2, color);
    }

    // Framed box: small title, a big value and a small line under it
    void drawBox(int16_t x, int16_t y, uint16_t w, uint16_t h, const std::string &title, const std::string &value,
                 const std::string &sub)
    {
        this->drawRect(x, y, w, h, BLACK);
        this->setTextColor(BLACK);
        this->setCrop(x + 1, y + 1, w - 2, h - 2);
        this->setFont(Applet::fontSmall);
        this->printAt(x + 4, y + 2, title);
        this->setFont(Applet::fontMedium);
        this->printAt(x + 4, y + 3 + Applet::fontSmall.lineHeight(), value);
        this->setFont(Applet::fontSmall);
        if (!sub.empty())
            this->printAt(x + 4, y + h - 2, sub, Applet::LEFT, Applet::BOTTOM);
        this->resetCrop();
    }

    // ---------- Text ----------

    __attribute__((format(printf, 1, 2))) static std::string fmt(const char *f, ...)
    {
        char buf[96];
        va_list ap;
        va_start(ap, f);
        vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        return buf;
    }

    // "recién", "5 min", "3 h", "2 d"
    static std::string ago(uint32_t s)
    {
        if (s < 60)
            return "recién";
        if (s < 3600)
            return fmt("%lu min", (unsigned long)(s / 60));
        if (s < 86400)
            return fmt("%lu h", (unsigned long)(s / 3600));
        return fmt("%lu d", (unsigned long)(s / 86400));
    }

    // Seconds since an epoch timestamp, or UINT32_MAX if unknown
    static uint32_t secondsSince(uint32_t epoch)
    {
        const uint32_t now = getValidTime(RTCQuality::RTCQualityDevice);
        if (!now || !epoch || epoch > now + 60)
            return UINT32_MAX;
        return epoch > now ? 0 : now - epoch;
    }

    static const char *cardinal(int deg)
    {
        static const char *names[] = {"N", "NE", "E", "SE", "S", "SO", "O", "NO"};
        deg = ((deg % 360) + 360) % 360;
        return names[((deg + 22) / 45) % 8];
    }

    static int bearingDegrees(float bearingRad)
    {
        int deg = (int)lroundf(bearingRad * 180.0f / (float)M_PI);
        return ((deg % 360) + 360) % 360;
    }

    std::string shortNameOf(NodeNum num)
    {
        if (num == nodeDB->getNodeNum())
            return "VOS";
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(num);
        if (n && nodeInfoLiteHasUser(n) && n->short_name[0])
            return this->parseShortName(n);
        return this->hexifyNodeNum(num);
    }

    std::string longNameOf(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(num);
        if (n && nodeInfoLiteHasUser(n) && n->long_name[0])
            return this->parse(n->long_name);
        return shortNameOf(num);
    }

    // Our own power, as Estado and Inicio show it
    static bool plugged() { return powerStatus && powerStatus->getHasUSB(); }
    static bool charging() { return powerStatus && powerStatus->getIsCharging(); }
};

} // namespace NicheGraphics::InkHUD

#endif
