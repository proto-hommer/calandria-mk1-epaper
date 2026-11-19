#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

namespace NicheGraphics::Inputs
{

// Abstract navigation events. Input sources emit these; InkHudNavigator maps them to InkHUD.
// LEFT/RIGHT belong to the vocabulary even though the rotary encoder never emits them: a
// future 5-way joystick (or an I2C GPIO expander) needs them for the grid keyboard.
enum class InputEvent {
    UP,
    DOWN,
    LEFT,
    RIGHT,
    SELECT,
    BACK,
};

} // namespace NicheGraphics::Inputs

#endif
