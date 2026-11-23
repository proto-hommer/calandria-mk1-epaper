#pragma once

#include "configuration.h"

/*

Identity beacon for CalandriApp: lets the app recognise Calandria MK1 epaper nodes on the mesh.

The official HardwareModel enum has no entry for this board (we report PRIVATE_HW), so we announce it on
a private port. The official apps ignore unknown ports; any app connected to a node that hears this
packet receives it through the normal FromRadio stream.

Port:    CALANDRIA_PORT (323 = PRIVATE_APP + 67), primary channel, broadcast, no ack.
Payload: UTF-8 text, fields separated by ';'
         CALANDRIA/1;model=Calandria MK1 epaper;fw=<firmware version>;env=<pio env>
When:    5 min after boot, then every 6 h, skipped while the channel is busy. A request on this port
         with want_response is answered directly (at most once a minute, never to relayed broadcasts).

*/

#include "MeshService.h"
#include "SinglePortModule.h"
#include "airtime.h"
#include "concurrency/OSThread.h"
#include "mesh/Throttle.h"

#include <cstdio>
#include <cstring>

constexpr meshtastic_PortNum CALANDRIA_PORT = (meshtastic_PortNum)(meshtastic_PortNum_PRIVATE_APP + 67);
constexpr const char *CALANDRIA_MODEL = "Calandria MK1 epaper";

class CalandriaBeacon : public SinglePortModule, private concurrency::OSThread
{
  public:
    CalandriaBeacon() : SinglePortModule("CalandriaBeacon", CALANDRIA_PORT), concurrency::OSThread("CalandriaBeacon")
    {
        setIntervalFromNow(FIRST_BEACON_MS);
    }

  protected:
    int32_t runOnce() override
    {
        if (airTime && !airTime->isTxAllowedChannelUtil(true)) {
            LOG_DEBUG("CalandriaBeacon: channel busy, retry later");
            return RETRY_MS;
        }
        meshtastic_MeshPacket *p = allocIdentity();
        if (p) {
            p->to = NODENUM_BROADCAST;
            p->channel = 0; // primary
            p->want_ack = false;
            p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
            LOG_INFO("CalandriaBeacon: announce %.*s", p->decoded.payload.size, p->decoded.payload.bytes);
            service->sendToMesh(p, RX_SRC_LOCAL, false);
        }
        return PERIOD_MS;
    }

    // Direct answer to an app asking "what are you?" on our port
    meshtastic_MeshPacket *allocReply() override
    {
        if (isMultiHopBroadcastRequest())
            return nullptr;
        if (lastReplyMs && Throttle::isWithinTimespanMs(lastReplyMs, REPLY_MIN_INTERVAL_MS))
            return nullptr;
        lastReplyMs = millis();
        return allocIdentity();
    }

  private:
    static constexpr uint32_t FIRST_BEACON_MS = 5 * 60 * 1000UL;
    static constexpr uint32_t PERIOD_MS = 6 * 60 * 60 * 1000UL;
    static constexpr uint32_t RETRY_MS = 10 * 60 * 1000UL;
    static constexpr uint32_t REPLY_MIN_INTERVAL_MS = 60 * 1000UL;

    uint32_t lastReplyMs = 0;

    meshtastic_MeshPacket *allocIdentity()
    {
        meshtastic_MeshPacket *p = allocDataPacket();
        if (!p)
            return nullptr;
        int n = snprintf((char *)p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                         "CALANDRIA/1;model=%s;fw=%s;env=%s", CALANDRIA_MODEL, optstr(APP_VERSION), optstr(APP_ENV));
        p->decoded.payload.size = (n > 0 && (size_t)n < sizeof(p->decoded.payload.bytes)) ? n : 0;
        return p;
    }
};
