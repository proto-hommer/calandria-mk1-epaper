#pragma once

// Upstream MenuApplet.h has no include guard. Both NodePickerApplet.h and InkHudNavigator.h need
// it, so include it only through this wrapper (avoids "redefinition of class MenuApplet" without
// touching the shared file).
#include "graphics/niche/InkHUD/Applets/System/Menu/MenuApplet.h"
