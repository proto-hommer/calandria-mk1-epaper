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
#include "ConversationApplet.h"
#include "DeliveryWatcher.h"
#include "EncoderInput.h"
#include "HubApplet.h"
#include "NodePickerApplet.h"
#include "RadarApplet.h"
#include "StatusApplet.h"
#include "TimezoneWatcher.h"
#include "TrackApplet.h"
#include "WaypointCompassApplet.h"

#include "NodeDB.h"
#include "mesh/Channels.h"

#include <cstdio>
#include <cstring>
#include <string>

// Menu name of a channel applet: "Canal 1: Amigos (privado)". "público" = the well-known default key,
// "privado" = your own key. Read once at boot (fonts must already be set: the menu prints names
// without decoding UTF-8, so we encode them for the font here).
static const char *channelAppletName(uint8_t index)
{
    using namespace NicheGraphics;
    static std::string names[2];
    const meshtastic_Channel &ch = channels.getByIndex(index);
    const char *kind = ch.role == meshtastic_Channel_Role_DISABLED ? "sin usar"
                       : channels.isDefaultChannel(index)          ? "público"
                                                                   : "privado";
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%u: %s (%s)", AppletNames::CHANNEL_PREFIX, index, channels.getName(index), kind);
    names[index] = InkHUD::Applet::fontSmall.decodeUTF8(buf);
    return names[index].c_str();
}

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

// Two-level navigation (HubApplet: Inicio -> card -> applet), enforced on every boot because the
// menu (or a settings reset) can change it:
//  - one tile showing Inicio at boot: Inicio, its cards and BOOT-to-go-back assume a single tile;
//  - everything the cards open is active (a switched-off applet would be a dead end);
//  - Heard, Recents List and Favorites Map are off: Nodes and Positions already show the same.
//    They stay registered: removing them would shift the saved indexes.
static void arrangeForHub(NicheGraphics::InkHUD::InkHUD *inkhud)
{
    using namespace NicheGraphics;
    auto &s = inkhud->persistence->settings;
    s.userTiles.maxCount = 1; // also hides "Layout" from the menu
    s.userTiles.count = 1;
    s.userTiles.focused = 0;
    for (size_t i = 0; i < inkhud->userApplets.size(); i++) {
        const char *n = inkhud->userApplets[i]->name;
        if (!n)
            continue;
        const bool off = !strcmp(n, AppletNames::HEARD) || !strcmp(n, AppletNames::RECENTS) ||
                         !strcmp(n, AppletNames::FAVORITES_MAP);
        s.userApplets.active[i] = !off;
        if (off)
            s.userApplets.autoshow[i] = false;
        if (!strcmp(n, AppletNames::HOME))
            s.userTiles.displayedUserApplet[0] = (uint8_t)i;
    }
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

    inkhud->persistence->settings.userTiles.maxCount = 1; // Inicio needs one tile (arrangeForHub)
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;
    // One tile only (BOOT long goes to Inicio): "Next Tile" has nothing to do
    inkhud->persistence->settings.optionalMenuItems.nextTile = false;

    // ORDER MATTERS: saved settings (active / autoshow / tile assignment) are stored by applet
    // INDEX. Never insert an applet in the middle -- it shifts every applet after it onto
    // another one's saved flags. New applets go at the END.
    inkhud->addApplet(AppletNames::ALL_MESSAGES, new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet(AppletNames::DMS, new InkHUD::DMApplet);
    inkhud->addApplet(channelAppletName(0), new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet(channelAppletName(1), new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet(AppletNames::RADAR, new InkHUD::RadarApplet, true);
    inkhud->addApplet(AppletNames::WAYPOINTS, new InkHUD::WaypointCompassApplet); // same slot: stock list + compass
    inkhud->addApplet(AppletNames::FAVORITES_MAP, new InkHUD::FavoritesMapApplet);
    inkhud->addApplet(AppletNames::RECENTS, new InkHUD::RecentsListApplet);
    inkhud->addApplet(AppletNames::HEARD, new InkHUD::HeardApplet, true, false, 0);
    inkhud->addApplet(AppletNames::NODES, new InkHUD::NodePickerApplet, true); // index 9 (added with the encoder)
    inkhud->addApplet(AppletNames::POSITIONS, new InkHUD::PositionsApplet, true); // index 10: map of all nodes
    inkhud->addApplet(AppletNames::STATUS, new InkHUD::StatusApplet, true);       // index 11: sensors / battery / radio / GPS
    inkhud->addApplet(AppletNames::HOME, new InkHUD::HubApplet, true);            // index 12: Inicio, the 4 cards
    inkhud->addApplet(AppletNames::CONVERSATIONS, new InkHUD::ConversationApplet, true); // 13: MENSAJES
    inkhud->addApplet(AppletNames::TRACK, new InkHUD::TrackApplet, true);                 // 14: RUMBO compass / position

    // Physical CardKB (I2C 0x5F, on the touch connector's SDA/SCL), like the Wio Tracker L1.
    // Found by the boot I2C scan; with it attached the on-screen keyboard is never shown.
    Inputs::CardKbInput::getInstance()->detect();

    // Settings we enforce (joystick UI for the encoder, migrations, the Inicio layout) must be SAVED
    // before begin(): begin() calls loadSettings() again, which replaces what is in memory with what
    // is in flash. So: load, change, save if anything changed, then begin() reloads the same values.
    // This must run AFTER addApplet() and maxCount: if nothing is stored (or the stored version is
    // old), loadSettings() keeps the current settings (applet defaults included), so the file we save
    // is complete instead of "every applet inactive".
    inkhud->persistence->loadSettings();
    const InkHUD::Persistence::Settings loaded = inkhud->persistence->settings;
    inkhud->persistence->settings.joystick.enabled = true; // the encoder drives nav
    inkhud->persistence->settings.joystick.aligned = true; // skip AlignStick: no 4-way to align
    inkhud->persistence->settings.joystick.alignment = 0;
    migrateAppletSettings(inkhud);
    arrangeForHub(inkhud);
    const auto &now = inkhud->persistence->settings; // compared field by field: no struct padding
    if (memcmp(&loaded.userTiles, &now.userTiles, sizeof(now.userTiles)) ||
        memcmp(&loaded.userApplets, &now.userApplets, sizeof(now.userApplets)) ||
        loaded.joystick.enabled != now.joystick.enabled || loaded.joystick.aligned != now.joystick.aligned ||
        loaded.joystick.alignment != now.joystick.alignment)
        inkhud->persistence->saveSettings();

    inkhud->begin();

    // Which applets run: the channel applets store channel messages (MENSAJES reads them)
    {
        std::string active;
        for (size_t i = 0; i < inkhud->userApplets.size(); i++)
            if (inkhud->userApplets[i]->isActive())
                active += std::to_string(i) + ",";
        LOG_INFO("InkHUD active applets: %s", active.c_str());
    }

    // "No entregado" pop-up when one of our messages is NAKed (needs the Notification applet, so after begin)
    new InkHUD::DeliveryWatcher();
    // Timezone set from the phone at runtime (no reboot): redraw the clocks
    new InkHUD::TimezoneWatcher();

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
