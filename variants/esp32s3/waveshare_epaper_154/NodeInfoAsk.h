#pragma once

#include "configuration.h"

/*

"Pedir datos": ask a node for its NodeInfo (name, role, public key) from the MK1.

Sends OUR NodeInfo to it as a DM with want_response (NodeInfoModule::sendOurNodeInfo), on the channel
we hear it on. NODEINFO_APP is never PKI-encrypted, so it goes out even without the node's key; the
node answers with its own. The same packet also fixes "no tiene mi clave" (PKI_UNKNOWN_PUBKEY): it
carries our key.

At most once a minute per node (airtime), and NodeInfoModule itself allows one user-triggered NodeInfo
a minute (shorterTimeout). Never automatic: from a node's card, or from a "sin su clave" pop-up
(DeliveryWatcher offers it, InkHudNavigator acts on the click).

*/

#include "NodeDB.h"
#include "modules/NodeInfoModule.h"

#include <Arduino.h>
#include <map>

#ifndef NODEINFO_ASK_INTERVAL_MS
#define NODEINFO_ASK_INTERVAL_MS (60 * 1000UL)
#endif
#define NODEINFO_ASK_OFFER_MS (30 * 1000UL) // how long a pop-up's "clic pide datos" stays valid

namespace CalandriaAsk
{

enum Result : uint8_t { SENT, WAIT, FAILED };

inline std::map<NodeNum, uint32_t> &lastAsked()
{
    static std::map<NodeNum, uint32_t> m;
    return m;
}

inline Result request(NodeNum num)
{
    if (!nodeInfoModule || !num || num == nodeDB->getNodeNum())
        return FAILED;
    auto &last = lastAsked();
    auto it = last.find(num);
    if (it != last.end() && millis() - it->second < NODEINFO_ASK_INTERVAL_MS)
        return WAIT;
    // false: NodeInfoModule's own one-a-minute limit, or a busy channel
    if (!nodeInfoModule->sendOurNodeInfo(num, true, nodeDB->getMeshNodeChannel(num), true))
        return FAILED;
    last[num] = millis();
    LOG_INFO("Pedir datos: NodeInfo request to 0x%08x", num);
    return SENT;
}

inline const char *resultText(Result r)
{
    switch (r) {
    case SENT:
        return "Datos pedidos";
    case WAIT:
        return "Esperá un minuto para volver a pedir";
    default:
        return "No salió: probá en un minuto";
    }
}

// The node a "sin su clave" pop-up offers to ask (0 = none)
inline NodeNum &offered()
{
    static NodeNum n = 0;
    return n;
}
inline uint32_t &offeredAt()
{
    static uint32_t t = 0;
    return t;
}

inline void offer(NodeNum num)
{
    offered() = num;
    offeredAt() = millis();
}

inline void clearOffer() { offered() = 0; }

// The node to ask, if a pop-up offered it recently; clears the offer
inline NodeNum takeOffer()
{
    const NodeNum n = offered();
    offered() = 0;
    return n && millis() - offeredAt() < NODEINFO_ASK_OFFER_MS ? n : 0;
}

} // namespace CalandriaAsk
