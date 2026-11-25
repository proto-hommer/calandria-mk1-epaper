#pragma once

#include "configuration.h"

/*

Identity beacon for CalandriApp: lets the app recognise Calandria MK1 epaper nodes on the mesh.

The official HardwareModel enum has no entry for this board (we report PRIVATE_HW), so we announce it on
a private port. The official apps ignore unknown ports; any app connected to a node that hears this
packet receives it through the normal FromRadio stream.

Port:    CALANDRIA_PORT (323 = PRIVATE_APP + 67), primary channel, broadcast, no ack.
Payload: UTF-8 text, fields separated by ';'
         CALANDRIA/1;model=Calandria MK1 epaper;fw=<firmware version>;env=<pio env>;ver=<CALANDRIA_VERSION>
         (contract with CalandriApp: new fields go at the end, never reorder)
When:    5 min after boot, then every 6 h, skipped while the channel is busy. A request on this port
         with want_response is answered directly: always when it comes from our own phone (to the local
         node), otherwise at most once a minute and never to relayed broadcasts.

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
// The Calandria's own semantic version (alpha: 0.x). APP_VERSION stays Meshtastic's (2.8.3.<hash>): apps use it
// to know which features the firmware has. ws154.1 .. ws154.5 = 0.1.0 .. 0.5.0
constexpr const char *CALANDRIA_VERSION = "0.6.0";

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

    // Direct answer to an app asking "what are you?" on our port.
    // From our own phone (request to the LOCAL node, from == 0): always answer, it never goes on air.
    // The app asks on every connection, and a throttled request would get a NO_RESPONSE NAK instead.
    // From the mesh: never to relayed broadcasts, at most once a minute.
    meshtastic_MeshPacket *allocReply() override
    {
        if (currentRequest && currentRequest->from == 0)
            return allocIdentity();
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
                         "CALANDRIA/1;model=%s;fw=%s;env=%s;ver=%s", CALANDRIA_MODEL, optstr(APP_VERSION), optstr(APP_ENV),
                         CALANDRIA_VERSION);
        p->decoded.payload.size = (n > 0 && (size_t)n < sizeof(p->decoded.payload.bytes)) ? n : 0;
        return p;
    }
};
