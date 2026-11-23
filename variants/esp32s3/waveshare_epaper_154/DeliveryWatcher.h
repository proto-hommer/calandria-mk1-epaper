#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Pops up "No entregado" when a packet we sent comes back with a routing error (NAK).

Listens to ROUTING_APP packets addressed to us that answer one of ours (request_id set) and carry an
error: NAKs from the destination (e.g. it could not decrypt a PKI DM), from relays, and the ones our
own router generates (MAX_RETRANSMIT, missing public key). Covers messages sent from the e-paper and
from the phone alike. Successful ACKs are ignored.

*/

#include "MeshModule.h"
#include "NodeDB.h"
#include "graphics/niche/InkHUD/Applets/System/Notification/NotificationApplet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"

#include <cstdio>
#include <cstring>

namespace NicheGraphics::InkHUD
{

class DeliveryWatcher : public MeshModule
{
  public:
    DeliveryWatcher() : MeshModule("DeliveryWatcher")
    {
        loopbackOk = true; // NAKs our own router raises arrive as local packets
    }

  protected:
    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        return p->decoded.portnum == meshtastic_PortNum_ROUTING_APP && p->decoded.request_id != 0 && isToUs(p);
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        meshtastic_Routing r = meshtastic_Routing_init_zero;
        if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Routing_msg, &r))
            return ProcessMessage::CONTINUE;
        if (r.which_variant != meshtastic_Routing_error_reason_tag || r.error_reason == meshtastic_Routing_Error_NONE)
            return ProcessMessage::CONTINUE;

        char line[48];
        const NodeNum from = getFrom(&mp);
        if (from == nodeDB->getNodeNum())
            snprintf(line, sizeof(line), "No entregado: %s", reason(r.error_reason));
        else
            snprintf(line, sizeof(line), "No entregado: %s (%s)", shortName(from).c_str(), reason(r.error_reason));
        LOG_INFO("Delivery failed for 0x%08x: error %d from 0x%08x", mp.decoded.request_id, r.error_reason, from);

        NotificationApplet *n = notificationApplet();
        if (n)
            n->showText(line);
        return ProcessMessage::CONTINUE; // RoutingModule and the phone still get the NAK
    }

  private:
    static const char *reason(meshtastic_Routing_Error e)
    {
        switch (e) {
        case meshtastic_Routing_Error_NO_CHANNEL: // a PKI DM the destination could not decrypt
        case meshtastic_Routing_Error_PKI_FAILED:
            return "clave";
        case meshtastic_Routing_Error_PKI_UNKNOWN_PUBKEY:
            return "no tiene mi clave";
        case meshtastic_Routing_Error_PKI_SEND_FAIL_PUBLIC_KEY:
            return "sin su clave";
        case meshtastic_Routing_Error_NO_ROUTE:
            return "sin ruta";
        case meshtastic_Routing_Error_TIMEOUT:
        case meshtastic_Routing_Error_MAX_RETRANSMIT:
        case meshtastic_Routing_Error_NO_RESPONSE:
            return "sin respuesta";
        case meshtastic_Routing_Error_TOO_LARGE:
            return "muy largo";
        case meshtastic_Routing_Error_DUTY_CYCLE_LIMIT:
        case meshtastic_Routing_Error_RATE_LIMIT_EXCEEDED:
            return "límite de envío";
        case meshtastic_Routing_Error_NO_INTERFACE:
            return "sin radio";
        default:
            return "rechazado";
        }
    }

    static std::string shortName(NodeNum n)
    {
        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(n);
        if (node && nodeInfoLiteHasUser(node) && node->short_name[0])
            return node->short_name;
        char hex[8];
        snprintf(hex, sizeof(hex), "%04x", (unsigned)(n & 0xffff));
        return hex;
    }

    // InkHUD::getSystemApplet() asserts when the name is missing: look it up ourselves
    static NotificationApplet *notificationApplet()
    {
        for (SystemApplet *sa : InkHUD::getInstance()->systemApplets)
            if (sa->name && strcmp(sa->name, "Notification") == 0)
                return static_cast<NotificationApplet *>(sa);
        return nullptr;
    }
};

} // namespace NicheGraphics::InkHUD

#endif
