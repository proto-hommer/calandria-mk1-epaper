#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

InkHUD applet: a scrollable, selectable list of mesh nodes.

Encoder has a single axis, so this uses InkHUD's grab/release pattern (same as the
UserAppletInputExample): a long-press toggles "browse" mode. While browsing, turning
moves the cursor and a short-press opens the keyboard to DM the selected node (handed
off to MenuApplet::composeTo). While not browsing, turning changes applet as usual, so
you are never trapped in the list.

*/

#include "graphics/niche/InkHUD/Applet.h"
#include "graphics/niche/InkHUD/Applets/System/Menu/MenuApplet.h"
#include "graphics/niche/InkHUD/InkHUD.h"

#include "NodeDB.h"

#include <string>
#include <vector>

namespace NicheGraphics::InkHUD
{

class NodePickerApplet : public Applet
{
  public:
    void onActivate() override { setGrabbed(false); }

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
        drawHeader(grabbed ? "Nodes *" : "Nodes");

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
                fillRect(0, y, width(), rowH, BLACK);
            setTextColor(sel ? WHITE : BLACK);
            printAt(2, y + 1, labelFor(nodes[idx]));
        }

        drawScrollHints(scrollOffset > 0, (scrollOffset + rows) < nodes.size());
    }

    void onButtonLongPress() override
    {
        setGrabbed(!grabbed);
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    void onButtonShortPress() override
    {
        if (grabbed)
            compose();
    }

    void onNavUp() override
    {
        if (cursor > 0) {
            cursor--;
            requestUpdate(EInk::UpdateTypes::FAST, false);
        }
    }

    void onNavDown() override
    {
        if (cursor + 1 < nodes.size()) {
            cursor++;
            requestUpdate(EInk::UpdateTypes::FAST, false);
        }
    }

  private:
    std::vector<NodeNum> nodes;
    uint16_t cursor = 0;
    uint16_t scrollOffset = 0;
    bool grabbed = false;

    // Grab the nav + select inputs only while browsing; always keep long-press to toggle.
    void setGrabbed(bool g)
    {
        grabbed = g;
        setInputsSubscribed(NAV_UP | NAV_DOWN | BUTTON_SHORT, g);
        setInputsSubscribed(BUTTON_LONG, true);
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
        if (cursor >= nodes.size())
            cursor = nodes.empty() ? 0 : (uint16_t)(nodes.size() - 1);
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
