#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

InkHUD applet: a scrollable, selectable list of mesh nodes.

Encoder has a single axis, so this uses InkHUD's grab/release pattern (same as the
UserAppletInputExample): encoder short-press enters "browse" mode. While browsing, turning
moves the cursor, short-press opens the keyboard to DM the selected node (handed off to
MenuApplet::composeTo), and BOOT (EXIT) or encoder long-press leaves the list. While not
browsing, turning changes applet as usual, so you are never trapped in the list.

The list is rebuilt from NodeDB every NODE_PICKER_REFRESH_MS (default 2 min) while the applet is
on screen, keeping the cursor on the same node. It only redraws if the list actually changed.

*/

#include "graphics/niche/InkHUD/Applet.h"
#include "MenuAppletInclude.h"
#include "graphics/niche/InkHUD/InkHUD.h"

#include "NodeDB.h"
#include "concurrency/OSThread.h"

#ifndef NODE_PICKER_REFRESH_MS
#define NODE_PICKER_REFRESH_MS (2 * 60 * 1000UL)
#endif

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace NicheGraphics::InkHUD
{

class NodePickerApplet : public Applet, public concurrency::OSThread
{
  public:
    NodePickerApplet() : concurrency::OSThread("NodePickerApplet") { OSThread::disable(); }

    void onActivate() override
    {
        setGrabbed(false);
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(NODE_PICKER_REFRESH_MS);
    }

    void onDeactivate() override { OSThread::disable(); }

    void onForeground() override
    {
        setGrabbed(false);
        rebuild();
        cursor = 0;
        scrollOffset = 0;
    }

    void onRender(bool full) override
    {
        (void)full;
        setFont(fontSmall);
        drawHeader(headerText());

        const int16_t top = getHeaderHeight() + 2;
        if (nodes.empty()) {
            printAt(X(0.5f), Y(0.5f), "No nodes yet", CENTER, MIDDLE);
            return;
        }

        const uint16_t rowH = fontSmall.lineHeight() + 2;
        uint16_t rows = (height() - top) / rowH;
        if (rows < 1)
            rows = 1;

        // Keep the cursor on screen
        if (cursor < scrollOffset)
            scrollOffset = cursor;
        if (cursor >= scrollOffset + rows)
            scrollOffset = cursor - rows + 1;

        for (uint16_t r = 0; r < rows && (scrollOffset + r) < nodes.size(); r++) {
            const uint16_t idx = scrollOffset + r;
            const int16_t y = top + r * rowH;
            const bool sel = grabbed && idx == cursor;
            if (sel)
                drawSelection(y, rowH);
            setTextColor(BLACK);
            // Right column: hops ("d" = heard directly, "2s" = 2 hops) + how long ago
            std::string age = ageFor(nodes[idx]);
            const std::string hops = hopsFor(nodes[idx]);
            if (!hops.empty())
                age = age.empty() ? hops : hops + " " + age;
            const uint16_t ageW = age.empty() ? 0 : getTextWidth(age) + 4;
            setCrop(0, y, width() - ageW, rowH); // long names never run into the age column
            printAt(2, y + 1, labelFor(nodes[idx]));
            resetCrop();
            if (!age.empty())
                printAt(width() - 2, y + 1, age, RIGHT, TOP);

            // Dotted line under the last "active" node (heard within the menu's Recents window):
            // replaces the separate Heard / Recents List applets
            if (idx + 1 < nodes.size() && isActive(nodes[idx]) && !isActive(nodes[idx + 1]))
                for (int16_t x = 0; x < width(); x += 3)
                    drawPixel(x, y + rowH - 1, BLACK);
        }

        drawScrollHints(scrollOffset > 0, (scrollOffset + rows) < nodes.size());
    }

    void onButtonShortPress() override
    {
        if (grabbed) {
            compose();
        } else {
            setGrabbed(true);
            requestUpdate(EInk::UpdateTypes::FAST);
        }
    }

    void onButtonLongPress() override { leaveList(); }
    void onExitShort() override { leaveList(); }

    void onNavUp() override
    {
        if (cursor > 0) {
            cursor--;
            requestUpdate(navUpdateType(), false);
        }
    }

    void onNavDown() override
    {
        if (cursor + 1 < nodes.size()) {
            cursor++;
            requestUpdate(navUpdateType(), false);
        }
    }

  protected:
    // Periodic refresh. Only while on screen: onForeground() rebuilds anyway when we come back.
    int32_t runOnce() override
    {
        if (isForeground()) {
            const NodeNum selected = cursor < nodes.size() ? nodes[cursor] : 0;
            const std::string before = signature();
            rebuild();
            for (uint16_t i = 0; i < nodes.size(); i++)
                if (nodes[i] == selected)
                    cursor = i; // keep the highlight on the same node
            if (signature() != before)
                requestUpdate(EInk::UpdateTypes::FAST); // e-paper: redraw only on a visible change
        }
        return NODE_PICKER_REFRESH_MS;
    }

  private:

    // E-paper: a black highlight bar, inverted on every step, ghosts badly with partial refreshes.
    // Use a double outline instead, and do a FULL refresh when the move scrolls to a new page
    // (every row changes); moves inside the page stay FAST.
    void drawSelection(int16_t y, uint16_t rowH)
    {
        drawRect(0, y, width(), rowH, BLACK);
        drawRect(1, y + 1, width() - 2, rowH - 2, BLACK);
    }

    uint16_t listRows()
    {
        const int16_t top = getHeaderHeight() + 2;
        const uint16_t rowH = fontSmall.lineHeight() + 2;
        const uint16_t rows = (height() - top) / rowH;
        return rows ? rows : 1;
    }

    EInk::UpdateTypes navUpdateType()
    {
        const bool pageTurn = cursor < scrollOffset || cursor >= scrollOffset + listRows();
        return pageTurn ? EInk::UpdateTypes::FULL : EInk::UpdateTypes::FAST;
    }
    std::vector<NodeNum> nodes;
    uint16_t cursor = 0;
    uint16_t scrollOffset = 0;
    bool grabbed = false;

    // Short-press is always ours (enter list / DM). Nav, long-press and EXIT only while browsing,
    // so outside the list turning changes applet and BOOT/long-press keep their InkHUD roles.
    void setGrabbed(bool g)
    {
        grabbed = g;
        setInputsSubscribed(BUTTON_SHORT, true);
        setInputsSubscribed(NAV_UP | NAV_DOWN | BUTTON_LONG | EXIT_SHORT, g);
    }

    void leaveList()
    {
        if (!grabbed)
            return;
        setGrabbed(false);
        requestUpdate(EInk::UpdateTypes::FULL); // clear any ghosting left by the list
    }

    void rebuild()
    {
        nodes.clear();
        const NodeNum me = nodeDB->getNodeNum();
        const uint32_t count = nodeDB->getNumMeshNodes();
        for (uint32_t i = 0; i < count; i++) {
            meshtastic_NodeInfoLite *node = nodeDB->getMeshNodeByIndex(i);
            if (!node || node->num == me)
                continue;
            nodes.push_back(node->num);
        }
        // Most recently heard first; never-heard (last_heard 0) at the end
        std::stable_sort(nodes.begin(), nodes.end(), [](NodeNum a, NodeNum b) {
            const meshtastic_NodeInfoLite *na = nodeDB->getMeshNode(a), *nb = nodeDB->getMeshNode(b);
            return (na ? na->last_heard : 0) > (nb ? nb->last_heard : 0);
        });
        if (cursor >= nodes.size())
            cursor = nodes.empty() ? 0 : (uint16_t)(nodes.size() - 1);
    }

    // Coarse "heard N ago": minutes < 1h, hours < 1 day, then days. Coarse on purpose, so the
    // 2-minute refresh only repaints the panel when a label actually changes.
    std::string ageFor(NodeNum num)
    {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
        if (!node || !node->last_heard)
            return "";
        const uint32_t s = sinceLastSeen(node);
        char buf[8];
        if (s < 3600)
            snprintf(buf, sizeof(buf), "%um", (unsigned)(s / 60));
        else if (s < 86400)
            snprintf(buf, sizeof(buf), "%uh", (unsigned)(s / 3600));
        else
            snprintf(buf, sizeof(buf), "%ud", (unsigned)(s / 86400));
        return buf;
    }

    // What the screen shows (order + ages): redraw only when this changes
    std::string signature()
    {
        std::string sig;
        for (NodeNum n : nodes)
            sig += std::to_string(n) + ageFor(n) + hopsFor(n) + (isActive(n) ? "a" : "") + ";";
        return sig;
    }

    // Same "active" notion as the stock Recents List: heard within settings->recentlyActiveSeconds
    // (set from the menu). Shown as a count in the header and a divider in the list.
    bool isActive(NodeNum num)
    {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
        return node && node->last_heard && sinceLastSeen(node) <= settings->recentlyActiveSeconds;
    }

    std::string headerText()
    {
        uint16_t active = 0;
        for (NodeNum n : nodes)
            if (isActive(n))
                active++;
        char buf[32];
        snprintf(buf, sizeof(buf), "Nodes%s %u (%u act)", grabbed ? " *" : "", (unsigned)nodes.size(), (unsigned)active);
        return buf;
    }

    std::string hopsFor(NodeNum num)
    {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
        if (!node || !node->has_hops_away)
            return "";
        if (node->hops_away == 0)
            return "d";
        char buf[6];
        snprintf(buf, sizeof(buf), "%us", (unsigned)node->hops_away);
        return buf;
    }

    std::string labelFor(NodeNum num)
    {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
        if (node && nodeInfoLiteHasUser(node) && node->long_name[0])
            return parse(node->long_name);
        return hexifyNodeNum(num);
    }

    void compose()
    {
        if (cursor >= nodes.size())
            return;
        const NodeNum dest = nodes[cursor];
        const ChannelIndex channel = nodeDB->getMeshNodeChannel(dest);
        inkhud->openMenu(); // borrow this tile, bring the menu forward
        MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
        if (menu)
            menu->composeTo(dest, channel);
    }
};

} // namespace NicheGraphics::InkHUD

#endif
