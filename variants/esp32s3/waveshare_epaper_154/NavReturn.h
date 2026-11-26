#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

#include "graphics/niche/InkHUD/Applet.h"

namespace NicheGraphics::InkHUD
{

// "BOOT goes back to whoever opened me": set by an applet that shows another one from inside it
// (Nodes -> Radar, a node's card -> its compass), read by InkHudNavigator. The opener keeps its own
// state (Calandria applets reset only when entered from Inicio), so going back resumes it.
struct NavReturn {
    static inline Applet *child = nullptr;
    static inline Applet *parent = nullptr;

    static void set(Applet *opened, Applet *opener)
    {
        child = opened;
        parent = opener;
    }
    static void clear() { child = parent = nullptr; }
};

} // namespace NicheGraphics::InkHUD

#endif
