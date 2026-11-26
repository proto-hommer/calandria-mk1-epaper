#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

ESTADO: the card itself. Four boxes and a list.

  Boxes: Batería (% and "enchufada · cargando" / "enchufada · llena" / "a batería"), Canal (channel
         utilisation, our TX airtime), Ambiente (the on-board SHTC3, I2C 0x70, read directly: the
         telemetry module keeps its reading in a protected method) and Firmware (our version, with a tick
         when it is the latest CalandriApp told us about, or "hay X" when there is a newer one).
  List:  Ajustes, WiFi (state only for now), Bluetooth · PIN, Apagar.
  Ajustes: Ahorro de energía, Girar pantalla, Hora local, Limpiar fantasmas, Reiniciar, and the full
         InkHUD menu for anything else. Group, radio and role are set from the app.
Actions that reboot or power off ask for a second click.

Also the traceroute service used by Nodes -> a node -> "Medir señal" (traceTo): one at a time, with a
cooldown, user-triggered only. The whole RouteDiscovery of the reply is kept for the result screen.

Redraws only when what it shows changes; refreshes every STATUS_REFRESH_MS while on screen.

*/

#include "CalandriaUI.h"

#include "CalandriaBeacon.h"
#include "MenuAppletInclude.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerStatus.h"
#include "RTC.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "UptimeClock.h"
#include "airtime.h"
#include "concurrency/OSThread.h"
#include "main.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"

#include <Wire.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef STATUS_REFRESH_MS
#define STATUS_REFRESH_MS (30 * 1000UL)
#endif
#define STATUS_TRACE_TIMEOUT_MS (90 * 1000UL) // multi-hop round trips are slow
#define STATUS_TRACE_COOLDOWN_MS (30 * 1000UL)
#define STATUS_SHTC3_ADDR 0x70

namespace NicheGraphics::InkHUD
{

class StatusApplet : public CalandriaUI<Applet>, public SinglePortModule, public concurrency::OSThread
{
  public:
    StatusApplet()
        : SinglePortModule("StatusApplet", meshtastic_PortNum_TRACEROUTE_APP), concurrency::OSThread("StatusApplet")
    {
        OSThread::disable();
    }

    // Entered from Inicio: the boxes and the list, first row
    void enter()
    {
        cursor = 0;
        confirm = -1;
        notice.clear();
        setPage(MAIN);
    }

    void onActivate() override
    {
        setPage(page);
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(STATUS_REFRESH_MS);
    }

    void onDeactivate() override { OSThread::disable(); }

    void onForeground() override
    {
        readSensor();
        lastSignature.clear();
    }

    void onRender(bool full) override
    {
        (void)full;
        lastSignature = signature();
        if (page == MAIN)
            renderMain();
        else
            renderList();
    }

    void onButtonShortPress() override { choose(); }
    void onButtonLongPress() override { onExitShort(); } // sub-pages only (subscribed there)
    void onExitShort() override
    {
        if (page == MAIN)
            return;
        const Page from = page;
        cursor = from == BLUETOOTH ? 2 : from == TIMEZONE ? 2 : 0; // back on the row that opened it
        confirm = -1;
        notice.clear();
        setPage(from == TIMEZONE ? SETTINGS : MAIN);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    void onNavUp() override { move(-1); }
    void onNavDown() override { move(1); }

    // ---------- Traceroute service (Nodes -> "Medir señal") ----------
    enum TraceState : uint8_t { TRACE_IDLE, TRACE_PENDING, TRACE_FAILED, TRACE_DONE };

    // Seconds until another traceroute is allowed (0 = now)
    uint32_t traceWaitSeconds()
    {
        if (tracePending && millis() - traceSentMs <= STATUS_TRACE_TIMEOUT_MS)
            return (STATUS_TRACE_TIMEOUT_MS - (millis() - traceSentMs)) / 1000 + 1;
        if (traceSentMs && millis() - traceSentMs < STATUS_TRACE_COOLDOWN_MS)
            return (STATUS_TRACE_COOLDOWN_MS - (millis() - traceSentMs)) / 1000 + 1;
        return 0;
    }

    // Send a traceroute to any node. False if one is already running / cooling down.
    bool traceTo(NodeNum target)
    {
        if (!target || target == nodeDB->getNodeNum() || traceWaitSeconds())
            return false; // airtime: one at a time, with a cooldown
        meshtastic_MeshPacket *p = router->allocForSending();
        if (!p)
            return false;
        p->to = target;
        p->channel = nodeDB->getMeshNodeChannel(target);
        p->decoded.portnum = meshtastic_PortNum_TRACEROUTE_APP;
        p->decoded.want_response = true;
        p->decoded.payload.size = 0; // empty RouteDiscovery: the route is filled in on the way
        traceTarget = target;
        traceId = p->id;
        traceSentMs = millis();
        tracePending = true;
        traceFailed = false;
        haveRoute = false;
        LOG_INFO("Estado: traceroute to 0x%08x", target);
        service->sendToMesh(p, RX_SRC_LOCAL, false);
        return true;
    }

    TraceState traceStateFor(NodeNum target)
    {
        if (target != traceTarget)
            return TRACE_IDLE;
        if (tracePending && millis() - traceSentMs > STATUS_TRACE_TIMEOUT_MS) {
            tracePending = false;
            traceFailed = true;
        }
        if (tracePending)
            return TRACE_PENDING;
        if (traceFailed)
            return TRACE_FAILED;
        return haveRoute ? TRACE_DONE : TRACE_IDLE;
    }

    // The reply of the last traceroute: route / snr_towards (to the target, SNR x4) and route_back /
    // snr_back (back to us). rxSnr = how we heard the reply's last hop (in case snr_back lacks it).
    const meshtastic_RouteDiscovery &lastRoute() const { return route; }
    float lastRxSnr() const { return rxSnr; }

  protected:
    int32_t runOnce() override
    {
        if (isForeground() && page == MAIN) {
            readSensor();
            if (signature() != lastSignature)
                requestUpdate(EInk::UpdateTypes::FAST); // e-paper: only on a visible change
        }
        return STATUS_REFRESH_MS;
    }

    // The reply to our traceroute request
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        if (!tracePending || getFrom(&mp) != traceTarget || mp.decoded.request_id != traceId)
            return ProcessMessage::CONTINUE;
        meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
        if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_RouteDiscovery_msg, &rd))
            return ProcessMessage::CONTINUE;
        route = rd;
        rxSnr = mp.rx_snr;
        haveRoute = true;
        tracePending = false;
        traceFailed = false;
        return ProcessMessage::CONTINUE; // the stock TraceRouteModule still gets it
    }

  private:
    enum Page : uint8_t { MAIN, SETTINGS, TIMEZONE, BLUETOOTH };
    Page page = MAIN;
    uint8_t cursor = 0;
    int8_t confirm = -1; // row waiting for its confirming second click
    std::string notice;
    std::string lastSignature;

    // SHTC3
    bool haveSensor = false;
    const char *sensorErr = "sin lectura"; // which step failed: logged
    float tempC = NAN, humidity = NAN;

    // Traceroute
    bool tracePending = false, traceFailed = false, haveRoute = false;
    NodeNum traceTarget = 0;
    PacketId traceId = 0;
    uint32_t traceSentMs = 0;
    meshtastic_RouteDiscovery route = meshtastic_RouteDiscovery_init_zero;
    float rxSnr = 0;

    // ---------- Pages ----------
    struct TzOption {
        const char *label;
        const char *tz;
    };
    static const TzOption *tzOptions(uint8_t *count)
    {
        static const TzOption opts[] = {
            {"Argentina · Uruguay (-3)", "<-03>3"},
            {"Paraguay · Bolivia (-4)", "<-04>4"},
            {"Chile", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"},
            {"Perú · Colombia (-5)", "<-05>5"},
            {"México (-6)", "CST6"},
            {"España", "CET-1CEST,M3.5.0,M10.5.0/3"},
            {"UTC", "UTC0"},
        };
        *count = sizeof(opts) / sizeof(opts[0]);
        return opts;
    }

    uint8_t rowCount()
    {
        uint8_t n = 0;
        switch (page) {
        case MAIN:
            return 4;
        case SETTINGS:
            return 6;
        case TIMEZONE:
            tzOptions(&n);
            return n;
        default:
            return 0;
        }
    }

    void setPage(Page p)
    {
        page = p;
        setInputsSubscribed(BUTTON_SHORT | NAV_UP | NAV_DOWN, true);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, p != MAIN);
    }

    void move(int dir)
    {
        const int next = (int)cursor + dir;
        if (next < 0 || next >= rowCount())
            return;
        cursor = (uint8_t)next;
        confirm = -1;
        notice.clear();
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    void open(Page p)
    {
        cursor = 0;
        if (p == TIMEZONE) { // start on the current one
            uint8_t n;
            const TzOption *o = tzOptions(&n);
            for (uint8_t i = 0; i < n; i++)
                if (!strcmp(o[i].tz, config.device.tzdef))
                    cursor = i;
        }
        confirm = -1;
        notice.clear();
        setPage(p);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    // Second click on the same row confirms
    bool confirmed(const char *ask)
    {
        if (confirm == (int8_t)cursor)
            return true;
        confirm = (int8_t)cursor;
        notice = ask;
        requestUpdate(EInk::UpdateTypes::FAST);
        return false;
    }

    void say(const std::string &s)
    {
        notice = s;
        confirm = -1;
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    void choose()
    {
        if (page == MAIN) {
            switch (cursor) {
            case 0:
                open(SETTINGS);
                break;
            case 1:
                say(config.network.wifi_enabled ? "WiFi: se configura desde la app" : "WiFi: llega en la 0.8.0");
                break;
            case 2:
                open(BLUETOOTH);
                break;
            default:
                if (confirmed("Clic de nuevo: apagar")) {
                    LOG_INFO("Estado: shutting down");
                    shutdownAtMsec = Time::skipZero(Time::getMillis());
                }
                break;
            }
        } else if (page == SETTINGS) {
            switch (cursor) {
            case 0:
                if (confirmed("Clic de nuevo: reinicia")) {
                    config.power.is_power_saving = !config.power.is_power_saving;
                    nodeDB->saveToDisk(SEGMENT_CONFIG);
                    inkhud->notifyApplyingChanges();
                    rebootAtMsec = Time::timerEndsAtMillis(DEFAULT_REBOOT_SECONDS * 1000);
                }
                break;
            case 1:
                inkhud->rotate();
                break;
            case 2:
                open(TIMEZONE);
                break;
            case 3: {
                const size_t n = MenuApplet::forgetGhostNodes();
                say(n ? fmt("Fantasmas: %u borrados", (unsigned)n) : std::string("Sin fantasmas"));
                break;
            }
            case 4:
                if (confirmed("Clic de nuevo: reiniciar")) {
                    inkhud->notifyApplyingChanges();
                    rebootAtMsec = Time::timerEndsAtMillis(1000);
                }
                break;
            default:
                inkhud->openMenu();
                break;
            }
        } else if (page == TIMEZONE) {
            uint8_t n;
            const TzOption *o = tzOptions(&n);
            const uint8_t chosen = cursor < n ? cursor : 0;
            setTimezone(o[chosen].tz);
            cursor = 2;
            setPage(SETTINGS);
            notice = std::string("Hora local: ") + o[chosen].label;
            requestUpdate(EInk::UpdateTypes::FULL);
        }
    }

    // Same as a timezone set from the app (AdminModule): applied now, saved, clocks redrawn
    // (TimezoneWatcher listens to configChanged)
    static void setTimezone(const char *tz)
    {
        strncpy(config.device.tzdef, tz, sizeof(config.device.tzdef) - 1);
        config.device.tzdef[sizeof(config.device.tzdef) - 1] = '\0';
        setenv("TZ", tz, 1);
        tzset();
        nodeDB->saveToDisk(SEGMENT_CONFIG);
        service->reloadConfig(SEGMENT_CONFIG);
    }

    std::string tzLabel()
    {
        if (!config.device.tzdef[0])
            return "UTC";
        uint8_t n;
        const TzOption *o = tzOptions(&n);
        for (uint8_t i = 0; i < n; i++)
            if (!strcmp(o[i].tz, config.device.tzdef))
                return o[i].label;
        return config.device.tzdef; // set from the app
    }

    // ---------- Drawing ----------
    std::string batteryValue()
    {
        if (powerStatus && powerStatus->getHasBattery())
            return std::to_string(powerStatus->getBatteryChargePercent()) + "%";
        return plugged() ? "USB" : "--";
    }

    std::string batteryState()
    {
        if (!plugged())
            return "a batería";
        return charging() ? "enchufada · cargando" : "enchufada · llena";
    }

    std::string signature()
    {
        std::string s = batteryValue() + batteryState();
        if (airTime)
            s += fmt("%.0f/%.1f", airTime->channelUtilizationPercent(), airTime->utilizationTXPercent());
        s += haveSensor ? fmt("%.1f/%.0f", tempC, humidity) : std::string("x");
        s += std::to_string(CalandriaLatest::standing()) + CalandriaLatest::versionLabel();
        s += getValidTime(RTCQuality::RTCQualityDevice) ? getTimeString() : std::string("");
        return s;
    }

    uint16_t textWidthMedium(const std::string &s)
    {
        setFont(fontMedium);
        const uint16_t w = getTextWidth(s);
        setFont(fontSmall);
        return w;
    }

    void renderMain()
    {
        drawBar("ESTADO", getValidTime(RTCQuality::RTCQualityDevice) ? getTimeString() : std::string(""), true);

        const int16_t top = barHeight() + 3;
        const uint16_t gap = 3;
        const uint16_t bw = (width() - 3 * gap) / 2;
        const uint16_t bh = fontMedium.lineHeight() + 2 * fontSmall.lineHeight() + 8;
        const int16_t x0 = gap, x1 = gap * 2 + bw, y0 = top, y1 = top + bh + gap;
        const int16_t valueMid = 3 + fontSmall.lineHeight() + fontMedium.lineHeight() / 2;

        const std::string bat = batteryValue();
        drawBox(x0, y0, bw, bh, parse("Batería"), bat, parse(batteryState()));
        if (plugged())
            drawBolt(x0 + 10 + textWidthMedium(bat), y0 + valueMid, BLACK);

        drawBox(x1, y0, bw, bh, "Canal", airTime ? fmt("%.0f%%", airTime->channelUtilizationPercent()) : std::string("--"),
                airTime ? fmt("aire TX %.1f%%", airTime->utilizationTXPercent()) : std::string(""));

        drawBox(x0, y1, bw, bh, "Ambiente", haveSensor ? parse(fmt("%.1f°C", tempC)) : std::string("--"),
                haveSensor ? fmt("humedad %.0f%%", humidity) : std::string("sin sensor"));

        const CalandriaLatest::Standing st = CalandriaLatest::standing();
        std::string fwSub;
        if (st == CalandriaLatest::OUTDATED)
            fwSub = std::string("hay ") + CalandriaLatest::versionLabel();
        else if (st == CalandriaLatest::CURRENT)
            fwSub = parse("al día");
        else if (st == CalandriaLatest::AHEAD)
            fwSub = "de prueba"; // newer than the published release
        drawBox(x1, y1, bw, bh, "Firmware", CALANDRIA_VERSION, fwSub);
        if (st == CalandriaLatest::CURRENT)
            drawCheck(x1 + 8 + textWidthMedium(CALANDRIA_VERSION), y1 + valueMid, BLACK);

        static const char *const labels[4] = {"Ajustes", "WiFi", "Bluetooth · PIN", "Apagar"};
        const std::string right[4] = {"", config.network.wifi_enabled ? parse("SÍ") : std::string("NO"),
                                      config.bluetooth.enabled ? parse("SÍ") : std::string("NO"), ""};
        drawRows(y1 + bh + gap + 1, labels, right, 4);
    }

    bool rowOpensPage(uint8_t i)
    {
        if (page == MAIN)
            return i == 0 || i == 2;
        return page == SETTINGS && (i == 2 || i == 5);
    }

    // Rows of a list; chevrons on the rows that open a page; the notice at the bottom
    void drawRows(int16_t y, const char *const *labels, const std::string *right, uint8_t n)
    {
        setFont(fontSmall);
        const uint16_t rowH = fontSmall.lineHeight() + 6;
        const uint16_t avail = height() - y - (notice.empty() ? 0 : fontSmall.lineHeight() + 4);
        const uint8_t rows = std::max<uint8_t>(1, avail / rowH);
        const uint8_t first = cursor >= rows ? cursor - rows + 1 : 0;
        for (uint8_t r = 0; r < rows && first + r < n; r++) {
            const uint8_t i = first + r;
            const int16_t ry = y + r * rowH;
            const uint16_t fg = beginRow(ry, rowH, i == cursor);
            int16_t rx = width() - 4;
            if (rowOpensPage(i)) {
                drawChevron(width() - 9, ry + rowH / 2, fg);
                rx -= 12;
            }
            uint16_t rightW = 0;
            if (right && !right[i].empty()) {
                printAt(rx, ry + rowH / 2, right[i], RIGHT, MIDDLE);
                rightW = getTextWidth(right[i]) + 6;
            }
            setCrop(0, ry, rx - rightW, rowH);
            printAt(6, ry + rowH / 2, parse(labels[i]), LEFT, MIDDLE);
            resetCrop();
            setTextColor(BLACK);
        }
        drawScrollHints(first > 0, first + rows < n);
        if (!notice.empty())
            printAt(4, height() - 2, parse(notice), LEFT, BOTTOM);
    }

    void renderList()
    {
        if (page == BLUETOOTH) {
            renderBluetooth();
            return;
        }
        if (page == SETTINGS) {
            drawBar("AJUSTES", "", true);
            static const char *const labels[6] = {"Ahorro de energía", "Girar pantalla",    "Hora local",
                                                  "Limpiar fantasmas", "Reiniciar",         "Menú completo"};
            const std::string right[6] = {config.power.is_power_saving ? parse("SÍ") : std::string("NO"), "", parse(tzLabel()),
                                          "", "", ""};
            drawRows(barHeight() + 3, labels, right, 6);
            return;
        }
        drawBar("HORA LOCAL", "", true);
        uint8_t n;
        const TzOption *o = tzOptions(&n);
        const char *labels[8];
        std::string right[8];
        for (uint8_t i = 0; i < n && i < 8; i++) {
            labels[i] = o[i].label;
            right[i] = strcmp(o[i].tz, config.device.tzdef) ? "" : "actual";
        }
        drawRows(barHeight() + 3, labels, right, n < 8 ? n : 8);
    }

    void renderBluetooth()
    {
        drawBar("BLUETOOTH", "", true);
        setFont(fontSmall);
        const uint16_t lh = fontSmall.lineHeight() + 4;
        int16_t y = barHeight() + 6;
        printAt(4, y, std::string("Bluetooth: ") + (config.bluetooth.enabled ? "prendido" : "apagado"));
        y += lh;
        if (config.network.wifi_enabled) {
            printAt(4, y, "Apagado mientras haya WiFi");
            y += lh;
        }
        std::string pin;
        switch (config.bluetooth.mode) {
        case meshtastic_Config_BluetoothConfig_PairingMode_FIXED_PIN:
            pin = fmt("PIN: %06lu (fijo)", (unsigned long)config.bluetooth.fixed_pin);
            break;
        case meshtastic_Config_BluetoothConfig_PairingMode_NO_PIN:
            pin = "Sin PIN";
            break;
        default:
            pin = "PIN: aparece en pantalla al vincular";
            break;
        }
        printWrapped(4, y, width() - 8, parse(pin));
        y += lh * 2;
        printWrapped(4, y, width() - 8, parse("El modo y el PIN se cambian desde la app."));
    }

    // ---------- SHTC3 (datasheet: wake 0x3517, measure T-first no-stretch 0x7866, sleep 0xB098) ----------
    static uint8_t crc8(const uint8_t *d)
    {
        uint8_t crc = 0xFF;
        for (uint8_t i = 0; i < 2; i++) {
            crc ^= d[i];
            for (uint8_t b = 0; b < 8; b++)
                crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
        return crc;
    }

    static bool shtcCommand(uint16_t cmd)
    {
        Wire.beginTransmission(STATUS_SHTC3_ADDR);
        Wire.write(cmd >> 8);
        Wire.write(cmd & 0xFF);
        return Wire.endTransmission() == 0;
    }

    void readSensor()
    {
        haveSensor = false;
        // Wake (also tells us the chip is there). The first command after sleep can be NACKed while
        // the chip powers up its interface, so retry once.
        if (!shtcCommand(0x3517)) {
            delay(1);
            if (!shtcCommand(0x3517))
                return sensorFail("no responde en 0x70");
        }
        delayMicroseconds(300); // wake-up time: max 240 us
        if (!shtcCommand(0x7866)) // measure, T first, no clock stretching, normal mode
            return sensorFail("rechazó la medición");
        delay(15); // max 12.1 ms in normal mode
        uint8_t b[6];
        if (Wire.requestFrom((uint8_t)STATUS_SHTC3_ADDR, (uint8_t)6) != 6)
            return sensorFail("lectura incompleta");
        for (uint8_t &v : b)
            v = Wire.read();
        shtcCommand(0xB098); // back to sleep
        if (crc8(b) != b[2] || crc8(b + 3) != b[5])
            return sensorFail("error de CRC");
        tempC = -45.0f + 175.0f * (float)((b[0] << 8) | b[1]) / 65536.0f;
        humidity = 100.0f * (float)((b[3] << 8) | b[4]) / 65536.0f;
        haveSensor = true;
    }

    void sensorFail(const char *why)
    {
        if (sensorErr != why)
            LOG_WARN("Estado: SHTC3 %s", why);
        sensorErr = why;
    }
};

} // namespace NicheGraphics::InkHUD

#endif
