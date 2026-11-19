#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "graphics/niche/InkHUD/InkHUD.h"

#include "graphics/niche/InkHUD/Applets/User/AllMessage/AllMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/DM/DMApplet.h"
#include "graphics/niche/InkHUD/Applets/User/FavoritesMap/FavoritesMapApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Heard/HeardApplet.h"
#include "graphics/niche/InkHUD/Applets/User/RecentsList/RecentsListApplet.h"
#include "graphics/niche/InkHUD/Applets/User/ThreadedMessage/ThreadedMessageApplet.h"
#include "graphics/niche/InkHUD/Applets/User/Waypoints/WaypointListApplet.h"

#include "graphics/niche/Drivers/EInk/GDEY0154D67.h"
#include "graphics/niche/Inputs/TwoButton.h"

#include "EncoderInput.h"

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

    inkhud->persistence->settings.userTiles.maxCount = 2;
    inkhud->persistence->settings.optionalFeatures.batteryIcon = true;

    // Rotary encoder navigates InkHUD as a joystick (up/down + select).
    // aligned=true skips the AlignStick screen: an encoder has no 4-way to align.
    inkhud->persistence->settings.joystick.enabled = true;
    inkhud->persistence->settings.joystick.aligned = true;
    inkhud->persistence->settings.joystick.alignment = 0;

    inkhud->addApplet("All Messages", new InkHUD::AllMessageApplet, true, true);
    inkhud->addApplet("DMs", new InkHUD::DMApplet);
    inkhud->addApplet("Channel 0", new InkHUD::ThreadedMessageApplet(0));
    inkhud->addApplet("Channel 1", new InkHUD::ThreadedMessageApplet(1));
    inkhud->addApplet("Radar", new InkHUD::RadarApplet, true);
    inkhud->addApplet("Waypoints", new InkHUD::WaypointListApplet);
    inkhud->addApplet("Favorites Map", new InkHUD::FavoritesMapApplet);
    inkhud->addApplet("Recents List", new InkHUD::RecentsListApplet);
    inkhud->addApplet("Heard", new InkHUD::HeardApplet, true, false, 0);

    inkhud->begin();

    // Button #0 = BOOT (menu / back anchor), #1 = encoder push (select).
    // All logic lives in EncoderInput so BOOT and the encoder stay in sync.
    Inputs::EncoderInput *encoder = Inputs::EncoderInput::getInstance();
    Inputs::TwoButton *buttons = Inputs::TwoButton::getInstance();
    buttons->setWiring(0, Inputs::TwoButton::getUserButtonPin(), true);
    buttons->setHandlerShortPress(0, [encoder]() { encoder->bootShort(); });
    buttons->setHandlerLongPress(0, [encoder]() { encoder->bootLong(); });
    buttons->setWiring(1, ENCODER_PIN_PRESS, true);
    buttons->setHandlerShortPress(1, [encoder]() { encoder->pressShort(); });
    buttons->setHandlerLongPress(1, [encoder]() { encoder->pressLong(); });
    buttons->start();

    // Rotary encoder quadrature (A/B) -> contextual InkHUD navigation.
    encoder->begin();
}

#endif
