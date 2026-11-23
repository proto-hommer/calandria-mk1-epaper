#pragma once

// Single source for user-applet names. They are shown in the menu AND used by InkHudNavigator to
// recognise message applets (reply), so a rename here can't silently break the navigator.
// Channel applets are named at boot as CHANNEL_PREFIX + index + ": <name> (<kind>)", e.g.
// "Canal 1: Amigos (privado)" (channelAppletName() in nicheGraphics.h); the navigator only relies on
// the prefix and the index right after it.
namespace NicheGraphics::AppletNames
{
constexpr const char *ALL_MESSAGES = "All Messages";
constexpr const char *DMS = "DMs";
constexpr const char *CHANNEL_PREFIX = "Canal ";
constexpr unsigned CHANNEL_PREFIX_LEN = 6;
constexpr const char *RADAR = "Radar";
constexpr const char *WAYPOINTS = "Waypoints";
constexpr const char *FAVORITES_MAP = "Favorites Map";
constexpr const char *RECENTS = "Recents List";
constexpr const char *HEARD = "Heard";
constexpr const char *NODES = "Nodes";
constexpr const char *POSITIONS = "Positions";
constexpr const char *STATUS = "Estado";
} // namespace NicheGraphics::AppletNames
