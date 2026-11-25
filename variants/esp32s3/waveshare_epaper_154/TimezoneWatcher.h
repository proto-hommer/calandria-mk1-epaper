#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Redraws InkHUD when the timezone changes at runtime (CalandriApp sends the phone's on every connection,
in config.device.tzdef). AdminModule applies TZ right away; this only refreshes the clock in the header,
the message times and Estado. Hooked on MeshService::configChanged, which fires after a config save.

*/

#include "MeshService.h"
#include "Observer.h"
#include "graphics/niche/InkHUD/InkHUD.h"

#include <cstring>

namespace NicheGraphics::InkHUD
{

class TimezoneWatcher
{
  public:
    TimezoneWatcher()
    {
        remember();
        configObserver.observe(&service->configChanged);
    }

  private:
    char lastTz[sizeof(config.device.tzdef)] = "";

    void remember()
    {
        strncpy(lastTz, config.device.tzdef, sizeof(lastTz) - 1);
        lastTz[sizeof(lastTz) - 1] = '\0';
    }

    int onConfigChanged(void *)
    {
        if (strcmp(lastTz, config.device.tzdef) != 0) {
            remember();
            LOG_INFO("Timezone now %s: redraw", config.device.tzdef);
            InkHUD::getInstance()->forceUpdate(Drivers::EInk::UpdateTypes::FULL, true);
        }
        return 0;
    }

    CallbackObserver<TimezoneWatcher, void *> configObserver =
        CallbackObserver<TimezoneWatcher, void *>(this, &TimezoneWatcher::onConfigChanged);
};

} // namespace NicheGraphics::InkHUD

#endif
