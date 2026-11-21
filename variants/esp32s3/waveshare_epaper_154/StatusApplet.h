#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

InkHUD applet "Estado": everything about this node on one page.

  - Ambient: temperature + humidity from the on-board SHTC3 (I2C 0x70), read directly
    (the telemetry module keeps its reading in a protected method).
  - Battery: voltage, %, USB / charging.
  - Clock: RTC time and uptime.
  - Radio: direct neighbours and how well we hear the best one (SNR).
    "How well do they hear US" can't be inferred from what we receive: the only real
    measurement is a traceroute. Encoder short-press sends one to the most recently heard
    direct neighbour (or Nodes -> a node -> "Medir senal" sends one to any node: traceTo()).
    The reply carries the SNR at which each hop received the previous one (snr_towards); [0] is
    the first node that heard us. User-triggered only, with a cooldown, to keep airtime low.
  - Channel: channel utilisation and our own TX airtime (like BaseUI's LoRa screen).
  - GPS: connected? fix? satellites in view (module still pending: shows "no conectado").

Redraws only when the shown text changes; refreshes every STATUS_REFRESH_MS while on screen.

*/

#include "graphics/niche/InkHUD/Applet.h"

#include "GPSStatus.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerStatus.h"
#include "RTC.h"
#include "Router.h"
#include "SinglePortModule.h"
#include "airtime.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"

#include <Wire.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#ifndef STATUS_REFRESH_MS
#define STATUS_REFRESH_MS (30 * 1000UL)
#endif
#define STATUS_TRACE_TIMEOUT_MS (90 * 1000UL) // multi-hop round trips are slow
#define STATUS_TRACE_COOLDOWN_MS (30 * 1000UL)
#define STATUS_SHTC3_ADDR 0x70

namespace NicheGraphics::InkHUD
{

class StatusApplet : public Applet, public SinglePortModule, public concurrency::OSThread
{
  public:
    StatusApplet()
        : SinglePortModule("StatusApplet", meshtastic_PortNum_TRACEROUTE_APP), concurrency::OSThread("StatusApplet")
    {
        OSThread::disable();
    }

    void onActivate() override
    {
        setInputsSubscribed(BUTTON_SHORT, true); // encoder short = measure how we are heard
        OSThread::enabled = true;
        OSThread::setIntervalFromNow(STATUS_REFRESH_MS);
    }

    void onDeactivate() override
    {
        setInputsSubscribed(BUTTON_SHORT, false);
        OSThread::disable();
    }

    void onForeground() override
    {
        readSensor();
        lastSignature.clear();
    }

    void onButtonShortPress() override
    {
        uint16_t count;
        float snr;
        traceTo(bestDirectNeighbour(&count, &snr));
    }

    // Seconds until another traceroute is allowed (0 = now). Shared with the Nodes applet.
    uint32_t traceWaitSeconds()
    {
        if (tracePending)
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
        LOG_INFO("Estado: traceroute to 0x%08x to measure how we are heard", target);
        service->sendToMesh(p, RX_SRC_LOCAL, false);
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST);
        return true;
    }

    void onRender(bool full) override
    {
        (void)full;
        setFont(fontSmall);
        drawHeader("Estado");

        const std::vector<std::string> lines = buildLines();
        lastSignature = join(lines);

        const uint16_t lineH = fontSmall.lineHeight() + 2;
        int16_t y = getHeaderHeight() + 3;
        for (const std::string &l : lines) {
            if (l.empty()) { // section gap: thin dotted rule
                for (int16_t x = 0; x < width(); x += 3)
                    drawPixel(x, y + 1, BLACK);
                y += 4;
                continue;
            }
            printAt(2, y, parse(l));
            y += lineH;
        }
    }

  protected:
    int32_t runOnce() override
    {
        if (tracePending && millis() - traceSentMs > STATUS_TRACE_TIMEOUT_MS) {
            tracePending = false;
            traceFailed = true;
        }
        if (isForeground()) {
            readSensor();
            if (join(buildLines()) != lastSignature)
                requestUpdate(EInk::UpdateTypes::FAST); // e-paper: only on a visible change
        }
        return STATUS_REFRESH_MS;
    }

    // Traceroute reply to our request: snr_towards = how the neighbour heard us
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        if (!tracePending || getFrom(&mp) != traceTarget || mp.decoded.request_id != traceId)
            return ProcessMessage::CONTINUE;

        meshtastic_RouteDiscovery rd = meshtastic_RouteDiscovery_init_zero;
        if (pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_RouteDiscovery_msg, &rd) &&
            rd.snr_towards_count > 0 && rd.snr_towards[0] != INT8_MIN) {
            // [0] = first node that received our request: a relay (route[0]) or the target itself
            heardUsSnr = rd.snr_towards[0] / 4.0f;
            haveHeardUs = true;
        } else {
            haveHeardUs = false;
        }
        traceHops = rd.route_count;
        firstHop = rd.route_count ? rd.route[0] : traceTarget;
        weHearSnr = mp.rx_snr;
        traceResultMs = millis();
        tracePending = false;
        traceFailed = false;
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST);
        return ProcessMessage::CONTINUE; // the stock TraceRouteModule still gets it
    }

  private:
    // SHTC3
    bool haveSensor = false;
    const char *sensorErr = "sin lectura"; // which step failed: shown on screen + logged
    float tempC = NAN, humidity = NAN;

    // Traceroute ("how do they hear us")
    bool tracePending = false, traceFailed = false, haveHeardUs = false;
    NodeNum traceTarget = 0;
    PacketId traceId = 0;
    uint32_t traceSentMs = 0, traceResultMs = 0;
    float heardUsSnr = 0, weHearSnr = 0;
    uint8_t traceHops = 0;  // relays between us and the target (0 = direct)
    NodeNum firstHop = 0;   // who heard our request first

    std::string lastSignature;

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

    // ---------- Radio ----------
    // Most recently heard node at 0 hops: the one most likely to answer a traceroute
    NodeNum bestDirectNeighbour(uint16_t *count, float *bestSnr)
    {
        NodeNum best = 0;
        uint32_t bestHeard = 0;
        *count = 0;
        *bestSnr = -1000;
        const NodeNum me = nodeDB->getNodeNum();
        for (uint32_t i = 0; i < nodeDB->getNumMeshNodes(); i++) {
            meshtastic_NodeInfoLite *n = nodeDB->getMeshNodeByIndex(i);
            if (!n || n->num == me || !n->has_hops_away || n->hops_away != 0 || !n->last_heard)
                continue;
            (*count)++;
            if (n->snr > *bestSnr)
                *bestSnr = n->snr;
            if (n->last_heard > bestHeard) {
                bestHeard = n->last_heard;
                best = n->num;
            }
        }
        return best;
    }

    std::string shortName(NodeNum num)
    {
        meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(num);
        return n ? parseShortName(n) : hexifyNodeNum(num);
    }

    // ---------- Text ----------
    __attribute__((format(printf, 1, 2))) static std::string fmt(const char *f, ...)
    {
        char buf[48];
        va_list ap;
        va_start(ap, f);
        vsnprintf(buf, sizeof(buf), f, ap);
        va_end(ap);
        return buf;
    }

    static std::string ago(uint32_t ms)
    {
        const uint32_t s = ms / 1000;
        return s < 60 ? fmt("%lus", (unsigned long)s) : fmt("%lumin", (unsigned long)(s / 60));
    }

    std::vector<std::string> buildLines()
    {
        std::vector<std::string> l;

        // Ambient
        l.push_back(haveSensor ? fmt("Temp %.1f°C   Hum %.0f%%", tempC, humidity) : std::string("SHTC3: ") + sensorErr);

        // Battery
        if (powerStatus && powerStatus->getHasBattery()) {
            std::string b = fmt("Bat %.2fV %u%%", powerStatus->getBatteryVoltageMv() / 1000.0f,
                                (unsigned)powerStatus->getBatteryChargePercent());
            if (powerStatus->getIsCharging())
                b += " cargando";
            else if (powerStatus->getHasUSB())
                b += " USB";
            l.push_back(b);
        } else {
            l.push_back(powerStatus && powerStatus->getHasUSB() ? "Bat: no detectada (USB)" : "Bat: sin datos");
        }

        // Clock
        const uint32_t up = millis() / 1000;
        std::string clock = getValidTime(RTCQuality::RTCQualityDevice) ? getTimeString() : std::string("--:--");
        l.push_back(fmt("Hora %s  Up %luh%02lum", clock.c_str(), (unsigned long)(up / 3600),
                        (unsigned long)((up / 60) % 60)));
        l.push_back("");

        // Radio
        uint16_t direct;
        float bestSnr;
        const NodeNum target = bestDirectNeighbour(&direct, &bestSnr);
        if (direct)
            l.push_back(fmt("Directos %u  mejor SNR %.1fdB", (unsigned)direct, bestSnr));
        else
            l.push_back("Sin vecinos directos");
        if (airTime)
            l.push_back(fmt("Canal %.0f%%  aire TX %.1f%%", airTime->channelUtilizationPercent(),
                            airTime->utilizationTXPercent()));

        if (tracePending)
            l.push_back("Midiendo con " + shortName(traceTarget) + "...");
        else if (traceFailed)
            l.push_back(shortName(traceTarget) + ": sin respuesta");
        else if (traceResultMs) {
            if (traceHops) // via relays: the first one is who actually heard OUR radio
                l.push_back(fmt("%s: %u salto%s", shortName(traceTarget).c_str(), (unsigned)(traceHops + 1),
                                traceHops ? "s" : ""));
            if (haveHeardUs)
                l.push_back(fmt("%s nos oye %.1fdB", shortName(firstHop).c_str(), heardUsSnr));
            else
                l.push_back(shortName(firstHop) + ": no informa SNR");
            l.push_back(fmt("%s %.1fdB  hace %s", traceHops ? "resp." : "lo oímos", weHearSnr,
                            ago(millis() - traceResultMs).c_str()));
        } else if (target)
            l.push_back("Pulsar: medir cómo nos oyen");
        l.push_back("");

        // GPS
        if (gpsStatus && gpsStatus->getIsConnected())
            l.push_back(fmt("GPS %s  %lu sat", gpsStatus->getHasLock() ? "con fix" : "sin fix",
                            (unsigned long)gpsStatus->getNumSatellites()));
        else
            l.push_back("GPS: no conectado");

        return l;
    }

    static std::string join(const std::vector<std::string> &v)
    {
        std::string s;
        for (const std::string &x : v)
            s += x + "\n";
        return s;
    }
};

} // namespace NicheGraphics::InkHUD

#endif
