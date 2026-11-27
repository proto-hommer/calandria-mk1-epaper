#pragma once

#include "configuration.h"

/*

Our own position on a board without GPS: it comes from the phone (or a fixed position), and
`localPosition` lives in RAM, so after a reboot it is zero until the phone sends a new one.
The NodeDB keeps the last one in flash: restoreOwnPosition() copies it back into
`localPosition` at boot so Radar / Nodes / Positions keep working.

It is assigned directly, NOT with NodeDB::setLocalPosition(): that would mark it "fresh since
boot", and PositionModule only broadcasts fresh positions. A restored one is shown, never sent.

*/

#include "NodeDB.h"
#include "RTC.h"
#include "TypeConversions.h"

#include <cstdio>
#include <string>

namespace OwnPosition
{

inline bool have()
{
    return localPosition.latitude_i != 0 || localPosition.longitude_i != 0;
}

// lateInitVariant(): NodeDB is loaded, nothing has drawn yet
inline void restore()
{
    if (have() || !nodeDB)
        return;
    meshtastic_PositionLite saved;
    if (!nodeDB->copyNodePosition(nodeDB->getNodeNum(), saved) || (saved.latitude_i == 0 && saved.longitude_i == 0))
        return;
    localPosition = TypeConversions::ConvertToPosition(saved);
    LOG_INFO("Own position restored from NodeDB: lat=%d lon=%d time=%u", saved.latitude_i, saved.longitude_i, saved.time);
}

// "3 h", "40 s"; empty if the position has no timestamp or we don't know the time
inline std::string age()
{
    const uint32_t now = getValidTime(RTCQualityDevice);
    const uint32_t t = localPosition.timestamp ? localPosition.timestamp : localPosition.time;
    if (!now || !t || t > now)
        return "";
    const uint32_t s = now - t;
    char buf[16];
    if (s < 60)
        snprintf(buf, sizeof(buf), "%lu s", (unsigned long)s);
    else if (s < 3600)
        snprintf(buf, sizeof(buf), "%lu min", (unsigned long)(s / 60));
    else if (s < 2 * 86400UL)
        snprintf(buf, sizeof(buf), "%lu h", (unsigned long)(s / 3600));
    else
        snprintf(buf, sizeof(buf), "%lu d", (unsigned long)(s / 86400));
    return buf;
}

// One line for "Estado": where our position comes from and how old it is
inline std::string statusLine()
{
    if (!have())
        return "Sin posición: conectá el celu";
    const std::string a = age();
    if (config.position.fixed_position)
        return "Posición fija";
    if (nodeDB && nodeDB->hasLocalPositionSinceBoot())
        return a.empty() ? "Posición: del celu" : "Posición: del celu hace " + a;
    return a.empty() ? "Posición guardada" : "Posición guardada, hace " + a;
}

} // namespace OwnPosition
