#pragma once

// Single source for user-applet names. They are shown in the menu AND used by InkHudNavigator to
// recognise message applets (reply), so a rename here can't silently break the navigator.
// "Channel N" is built by prefix: CHANNEL_PREFIX + index.
namespace NicheGraphics::AppletNames
{
constexpr const char *ALL_MESSAGES = "All Messages";
constexpr const char *DMS = "DMs";
constexpr const char *CHANNEL_0 = "Channel 0";
constexpr const char *CHANNEL_1 = "Channel 1";
constexpr const char *CHANNEL_PREFIX = "Channel ";
constexpr unsigned CHANNEL_PREFIX_LEN = 8;
constexpr const char *RADAR = "Radar";
constexpr const char *WAYPOINTS = "Waypoints";
constexpr const char *FAVORITES_MAP = "Favorites Map";
constexpr const char *RECENTS = "Recents List";
constexpr const char *HEARD = "Heard";
constexpr const char *NODES = "Nodes";
} // namespace NicheGraphics::AppletNames
