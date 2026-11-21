#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/niche/InkHUD/InkHUD.h"

#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Positions/PositionsApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Waypoints/WaypointListApplet.h"

#include "graphics/niche/Drivers/EInk/GDEY0154D67.h"
#include "graphics/niche/Inputs/CardKbInput.h"
#include "graphics/niche/Inputs/TwoButton.h"
#include "graphics/niche/Utils/FlashData.h"

#include "AppletNames.h"
#include "EncoderInput.h"
#include "NodePickerApplet.h"
#include "StatusApplet.h"
#include "WaypointCompassApplet.h"

#include "NodeDB.h"
#include "SinglePortModule.h"
#include "gps/GeoCoord.h"
#include "mesh/Throttle.h"

namespace NicheGraphics::InkHUD
{

// North-up bearing to the nearest node. This board has no compass, so the ring does not follow rotation.
class RadarApplet : public Applet, public SinglePortModule
{
  public:
    RadarApplet() : SinglePortModule("RadarApplet", meshtastic_PortNum_POSITION_APP) {}

    void onRender(bool full) override
    {
        (void)full;
        setFont(fontSmall);
        drawHeader("Radar");

        const bool haveUs = nodeDB && (localPosition.latitude_i != 0 || localPosition.longitude_i != 0);
        if (!haveUs) {
            printAt(X(0.5f), Y(0.45f), "No GPS", CENTER, MIDDLE);
            printAt(X(0.5f), Y(0.62f), "Share phone location", CENTER, MIDDLE);
            return;
        }

        const double ourLat = localPosition.latitude_i * 1e-7;
        const double ourLon = localPosition.longitude_i * 1e-7;
        NodeNum bestNum = 0;
        double bestLat = 0;
        double bestLon = 0;
        float bestMeters = 0;
        bool found = false;
        for (NodeNum num : nodeDB->snapshotPositionNodeNums(nodeDB->getNodeNum())) {
            meshtastic_PositionLite pos;
            if (!nodeDB->copyNodePosition(num, pos))
                continue;
            if (pos.latitude_i == 0 && pos.longitude_i == 0)
                continue;
            const double lat = pos.latitude_i * 1e-7;
            const double lon = pos.longitude_i * 1e-7;
            const float meters = GeoCoord::latLongToMeter(ourLat, ourLon, lat, lon);
            if (!found || meters < bestMeters) {
                found = true;
                bestMeters = meters;
                bestNum = num;
                bestLat = lat;
                bestLon = lon;
            }
        }

        const int16_t footer = fontSmall.lineHeight() * 2 + 6;
        const int16_t bodyTop = getHeaderHeight() + 4;
        int16_t bodyH = height() - bodyTop - footer;
        if (bodyH < 32)
            bodyH = 32;
        int16_t radius = width() / 2;
        if (bodyH / 2 < radius)
            radius = bodyH / 2;
        radius -= 8;
        if (radius < 16)
            radius = 16;
        const int16_t cx = width() / 2;
        const int16_t cy = bodyTop + (bodyH / 2);

        drawCircle(cx, cy, radius, BLACK);
        drawCircle(cx, cy, radius - 1, BLACK);
        printAt(cx, cy - radius + fontSmall.lineHeight(), "N", CENTER, MIDDLE);
        fillCircle(cx, cy, 2, BLACK);

        if (!found) {
            printAt(X(0.5f), height() - 2, "No nodes", CENTER, BOTTOM);
            return;
        }

        if (bestMeters >= 5)
            drawArrow(cx, cy, (int16_t)(radius * 0.7f), GeoCoord::bearing(ourLat, ourLon, bestLat, bestLon));

        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(bestNum);
        const std::string name = node ? parseShortName(node) : hexifyNodeNum(bestNum);
        printAt(X(0.5f), height() - fontSmall.lineHeight() - 2, name, CENTER, BOTTOM);
        printAt(X(0.5f), height() - 1, bestMeters < 5 ? "Here" : localizeDistance((uint32_t)bestMeters), CENTER, BOTTOM);
    }

  protected:
    void onActivate() override { loopbackOk = true; }
    void onDeactivate() override { loopbackOk = false; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        (void)mp;
        if (!isForeground() || Throttle::isWithinTimespanMs(lastRefreshMs, 4000))
            return ProcessMessage::CONTINUE;
        lastRefreshMs = millis();
        requestUpdate(Drivers::EInk::UpdateTypes::FAST);
        return ProcessMessage::CONTINUE;
    }

  private:
    uint32_t lastRefreshMs = 0;

    // along is toward the target, across is to the right. Bearing 0 is north, up the screen.
    void drawArrow(int16_t cx, int16_t cy, int16_t size, float bearing)
    {
        const float s = sin(bearing);
        const float c = cos(bearing);
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

// One-time changes to SAVED applet settings, versioned in our own flash file so that afterwards the
// user's menu choices are respected. Applets are looked up by name, never by hardcoded index.
//  v1: "Nodes" absorbs Heard + Recents List (sorted by last heard, active count + divider), so hide
//      those two; turn on the applets added at the end (Nodes, Positions, Estado), which saved
//      settings from older firmware have as inactive; a tile showing Heard/Recents now shows Nodes.
static void migrateAppletSettings(NicheGraphics::InkHUD::InkHUD *inkhud)
{
    using namespace NicheGraphics;
    struct VariantState {
        uint8_t version = 0;
    } state;
    constexpr uint8_t CURRENT = 1;
    FlashData<VariantState>::load(&state, "wsepd154");
    if (state.version >= CURRENT)
        return;

    auto indexOf = [inkhud](const char *name) -> int {
        for (size_t i = 0; i < inkhud->userApplets.size(); i++)
            if (inkhud->userApplets[i]->name && strcmp(inkhud->userApplets[i]->name, name) == 0)
                return (int)i;
        return -1;
    };
    auto &s = inkhud->persistence->settings;
    const int nodes = indexOf(AppletNames::NODES);
    for (const char *off : {AppletNames::HEARD, AppletNames::RECENTS}) {
        const int i = indexOf(off);
        if (i < 0)
            continue;
        s.userApplets.active[i] = false;
        s.userApplets.autoshow[i] = false;
        for (uint8_t &shown : s.userTiles.displayedUserApplet)
            if (shown == i && nodes >= 0)
                shown = (uint8_t)nodes;
    }
    for (const char *on : {AppletNames::NODES, AppletNames::POSITIONS, AppletNames::STATUS}) {
        const int i = indexOf(on);
        if (i >= 0)
            s.userApplets.active[i] = true;
    }
    inkhud->persistence->saveSettings();
    state.version = CURRENT;
    FlashData<VariantState>::save(&state, "wsepd154");
}

// Heard and Recents List duplicate "Nodes" (same nodes, same order, less info). The one-time v1
// migration hid them, but they can come back (menu, or a settings reset back to the defaults), so
// keep them off on every boot. They stay registered: removing them would shift the saved indexes.
static void hideRedundantApplets(NicheGraphics::InkHUD::InkHUD *inkhud)
{
    using namespace NicheGraphics;
    auto &s = inkhud->persistence->settings;
    int nodes = -1;
    for (size_t i = 0; i < inkhud->userApplets.size(); i++)
        if (inkhud->userApplets[i]->name && strcmp(inkhud->userApplets[i]->name, AppletNames::NODES) == 0)
            nodes = (int)i;
    for (size_t i = 0; i < inkhud->userApplets.size(); i++) {
        const char *n = inkhud->userApplets[i]->name;
        if (!n || (strcmp(n, AppletNames::HEARD) != 0 && strcmp(n, AppletNames::RECENTS) != 0))
            continue;
        s.userApplets.active[i] = false;
        s.userApplets.autoshow[i] = false;
        for (uint8_t &shown : s.userTiles.displayedUserApplet)
            if (shown == i && nodes >= 0)
                shown = (uint8_t)nodes;
    }
    if (nodes >= 0)
        s.userApplets.active[nodes] = true;
}

void setupNicheGraphics()
{
    using namespace NicheGraphics;

    // Drive the panel rail before any SPI. Active low, the panel refreshes.
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, EINK_EN_ACTIVE);
    delay(10);

    // Panel on its own HSPI. The radio keeps the default SPI.
    SPIClass *hspi = new SPIClass(HSPI);
    hspi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);

    Drivers::EInk *driver = new Drivers::GDEY0154D67;
    driver->begin(hspi, PIN_EINK_DC, PIN_EINK_CS, PIN_EINK_BUSY, PIN_EINK_RES);

    InkHUD::InkHUD *inkhud = InkHUD::InkHUD::getInstance();
    inkhud->setDriver(driver);
    // Same FAST/FULL budget as the T-Echo, which uses this panel.
    inkhud->setDisplayResilience(20, 1.5);

    InkHUD::Applet::fontLarge = FREESANS_12PT;
    InkHUD::Applet::fontMedium = FREESANS_9PT;
    InkHUD::Applet::fontSmall = FREESANS_6PT;

    // One encoder axis: the on-screen keyboard steps through keys linearly instead of 2D.
    inkhud->singleAxisKeyboard = true;

    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;
    // BOOT long already switches tile (like the joystick boards): keep the menu one item shorter
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    // ORDER MATTERS: saved settings (active / autoshow / tile assignment) are stored by applet
    // INDEX. Never insert an applet in the middle -- it shifts every applet after it onto
    // another one's saved flags. New applets go at the END.
    inkhud->addApplet(AppletNames::ALL_MESSAGES, new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet(AppletNames::DMS, new InkHUD::DMApplet);
    inkhud->addApplet(AppletNames::CHANNEL_0, new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet(AppletNames::CHANNEL_1, new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet(AppletNames::RADAR, new InkHUD::RadarApplet, true);
    inkhud->addApplet(AppletNames::WAYPOINTS, new InkHUD::WaypointCompassApplet); // same slot: stock list + compass
    inkhud->addApplet(AppletNames::FAVORITES_MAP, new InkHUD::FavoritesMapApplet);
    inkhud->addApplet(AppletNames::RECENTS, new InkHUD::RecentsListApplet);
    inkhud->addApplet(AppletNames::HEARD, new InkHUD::HeardApplet, true, false, 0);
    inkhud->addApplet(AppletNames::NODES, new InkHUD::NodePickerApplet, true); // index 9 (added with the encoder)
    inkhud->addApplet(AppletNames::POSITIONS, new InkHUD::PositionsApplet, true); // index 10: map of all nodes
    inkhud->addApplet(AppletNames::STATUS, new InkHUD::StatusApplet, true);       // index 11: sensors / battery / radio / GPS

    // Physical CardKB (I2C 0x5F, on the touch connector's SDA/SCL), like the Wio Tracker L1.
    // Found by the boot I2C scan; with it attached the on-screen keyboard is never shown.
    Inputs::CardKbInput::getInstance()->detect();

    // The encoder is the only input, so the joystick UI must always be on. begin() calls
    // loadSettings() and would overwrite a plain default with the stored value, so load here,
    // force it on, and persist once. This must run AFTER addApplet() and maxCount: if nothing is
    // stored, loadSettings() keeps the current settings (applet defaults included), so the file
    // we save is complete instead of "every applet inactive".
    inkhud->persistence->loadSettings();
    if (!inkhud->persistence->settings.joystick.enabled || !inkhud->persistence->settings.joystick.aligned) {
        inkhud->persistence->settings.joystick.enabled = true;  // encoder drives nav
        inkhud->persistence->settings.joystick.aligned = true;  // skip AlignStick: no 4-way to align
        inkhud->persistence->settings.joystick.alignment = 0;
        inkhud->persistence->saveSettings();
    }
    migrateAppletSettings(inkhud);
    hideRedundantApplets(inkhud);

    inkhud->begin();

    // Input layer: sources emit abstract events, InkHudNavigator maps them to InkHUD.
    // Registering a source is this + its begin(); the navigator never changes.
    Inputs::InkHudNavigator *nav = Inputs::InkHudNavigator::getInstance();

    // Button source: #0 = BOOT (BACK), #1 = encoder push (SELECT).
    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance();
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin(), true);
    buttons->setHandlerShortPress(0, [nav]() { nav->dispatch(Inputs::InputEvent::BACK); });
    buttons->setHandlerLongPress(0, [nav]() { nav->dispatch(Inputs::InputEvent::BACK, true); });
    buttons->setWiring(1, ENCODER_PIN_PRESS, true);
    buttons->setHandlerShortPress(1, [nav]() { nav->dispatch(Inputs::InputEvent::SELECT); });
    buttons->setHandlerLongPress(1, [nav]() { nav->dispatch(Inputs::InputEvent::SELECT, true); });
    buttons->start();

    // Rotary encoder source: A/B quadrature -> UP/DOWN events.
    Inputs::EncoderInput::getInstance()->begin();

    Inputs::CardKbInput::getInstance()->begin(); // no-op unless a CardKB was detected
}

#endif
