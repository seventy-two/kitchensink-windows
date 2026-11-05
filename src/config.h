// The dashboard's settings, kept in %APPDATA%\kitchensink\config.json. The
// token is encrypted with DPAPI, so only this Windows user can read it.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include "model.h"

namespace ks {

// OledCare guards OLED screens against burn-in, each part on its own.
struct OledCare {
    bool shift = false;  // move everything round a small circle every three minutes
    bool black = false;  // a true-black theme with dimmer text and fainter borders
    bool dim = false;    // dim after ten minutes without the mouse over the window
    bool swap = false;   // swap the Recent column's side every other hour
};

struct Config {
    std::string url;    // e.g. https://example.com/kitchensink
    std::string token;
    // Events below this priority don't raise a Windows notification.
    Priority minPriority = Priority::Normal;
    bool notify = true;
    OledCare oled;
    // A black cat asleep above the Recent column.
    bool cat = false;
    // Where the landscape under Recent shows the sun and moon from, as
    // "latitude, longitude"; empty for no landscape.
    std::string place;
    // The colour theme: empty to follow Windows' light or dark mode, or
    // "retro", after the Weather Channel's 90s WeatherStar screens.
    std::string theme;
    // Events cleared from the Recent column.
    Model::Cleared cleared;
    // Hides private figures, e.g. a portfolio's value, from the panels.
    bool hidePrivate = false;
    // In the compact layout, monitors opened or closed by hand. The rest
    // open by themselves while there's room.
    std::set<std::string> expanded, collapsed;
    // Panel order, as dragged; empty for the server's config order.
    std::vector<std::string> order;
    // The apps menu in the title bar: each a name, for
    // %LOCALAPPDATA%\Programs\<name>\<name>.exe, or a full path to an exe.
    std::vector<std::string> apps;
    // Apps (named as in "apps") left out of the menu while hideApps is on,
    // toggled from the eye's right-click menu.
    std::vector<std::string> privateApps;
    bool hideApps = false;
    // Dims the window whatever the mouse does, until switched off (the
    // moon in the title bar).
    bool forceDim = false;
    // "" lays out by window size; "ultra" is the compact layout with every
    // closed panel cut to one slim line.
    std::string layout;
    // Panels with something new rise to the top; ones with nothing new for
    // a long while sink, fade and don't open by themselves.
    bool relevance = true;
    // When each monitor last had something new, for the above.
    std::map<std::string, News> news;
};

// LoadConfig reads the settings, leaving defaults for anything missing.
Config LoadConfig();
bool SaveConfig(const Config& c);

// Start with Windows, via the current user's Run key.
bool StartsWithWindows();
void SetStartWithWindows(bool on);

}  // namespace ks
