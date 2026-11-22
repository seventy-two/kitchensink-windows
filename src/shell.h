// The rules behind the dashboard's stand-ins for the Windows taskbar (open
// windows, pinned apps and the tray), kept free of Windows headers so they
// can be tested anywhere.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ks {

// WindowTraits is what decides whether a top-level window has a taskbar
// button.
struct WindowTraits {
    bool visible = false;
    bool toolWindow = false;  // WS_EX_TOOLWINDOW
    bool appWindow = false;   // WS_EX_APPWINDOW
    bool owned = false;       // has an owner window
    bool cloaked = false;     // hidden by DWM: a suspended Store app, or on another virtual desktop
    bool hasTitle = false;
    bool ours = false;        // the dashboard itself
};

// ShowsOnTaskbar follows the taskbar's own rules: visible, uncloaked and
// titled, and either a plain unowned window or one that asks for a button
// (WS_EX_APPWINDOW). Tool windows don't get one unless they ask. The
// dashboard leaves itself out, as it's where the list is.
inline bool ShowsOnTaskbar(const WindowTraits& w) {
    if (!w.visible || w.cloaked || !w.hasTitle || w.ours) return false;
    if (w.appWindow) return true;
    return !w.toolWindow && !w.owned;
}

// HiddenTrayIcon says whether an icon is one of Explorer's own that the
// dashboard shows its own way instead, so leaves out of the Tray card:
// the volume (ID 100 on one of Explorer's ATL windows) and the network
// (its "Network Flyout" window).
inline bool HiddenTrayIcon(bool explorers, unsigned id, const std::string& ownerClass, const std::string& ownerTitle) {
    if (!explorers) return false;
    if (ownerTitle == "Network Flyout") return true;
    return id == 100 && ownerClass.compare(0, 4, "ATL:") == 0;
}

// Mouse messages, as Windows numbers them, for the tray's callbacks.
namespace mouse {
constexpr unsigned Move = 0x0200, LeftDown = 0x0201, LeftUp = 0x0202, LeftDouble = 0x0203;
constexpr unsigned RightDown = 0x0204, RightUp = 0x0205, RightDouble = 0x0206;
constexpr unsigned ContextMenu = 0x007B;  // WM_CONTEXTMENU
constexpr unsigned Select = 0x0400;       // NIN_SELECT (WM_USER)
}  // namespace mouse

// TrayMessage is one callback message to a tray icon's window.
struct TrayMessage {
    uint64_t wParam = 0;
    int64_t lParam = 0;
};

enum class TrayButton { Left, Right };
enum class TrayPress { Down, Double, Up };

// TrayMessages lists the callbacks Explorer sends a tray icon for a press,
// in its icon's format. Version 4 icons (NOTIFYICON_VERSION_4) get the
// message in lParam's low word with the icon's ID in its high word, the
// pointer's screen position in wParam, and NIN_SELECT or WM_CONTEXTMENU
// after the button comes up. Older ones get the ID in wParam and the
// message in lParam. The pointer's position is the dashboard's, so menus
// open beside the click.
inline std::vector<TrayMessage> TrayMessages(int version, unsigned id, TrayButton b, TrayPress p, int x, int y) {
    bool left = b == TrayButton::Left;
    std::vector<unsigned> msgs;
    switch (p) {
    case TrayPress::Down: msgs = {mouse::Move, left ? mouse::LeftDown : mouse::RightDown}; break;
    case TrayPress::Double: msgs = {left ? mouse::LeftDouble : mouse::RightDouble}; break;
    case TrayPress::Up:
        msgs = {left ? mouse::LeftUp : mouse::RightUp};
        if (version >= 4) msgs.push_back(left ? mouse::Select : mouse::ContextMenu);
        break;
    }
    std::vector<TrayMessage> out;
    for (unsigned m : msgs) {
        TrayMessage t;
        if (version >= 4) {
            t.wParam = (uint64_t(uint16_t(int16_t(y))) << 16) | uint16_t(int16_t(x));
            t.lParam = int64_t((uint64_t(id & 0xFFFF) << 16) | m);
        } else {
            t.wParam = id;
            t.lParam = m;
        }
        out.push_back(t);
    }
    return out;
}

// A pinned app is a shortcut (a .lnk, by path or by its name among the
// taskbar's pins), a Store app by its app ID ("family!App"), or File
// Explorer.
enum class PinKind { Shortcut, AppID, Explorer };

// PinKindOf says which a "pins" entry in config.json is.
inline PinKind PinKindOf(const std::string& spec) {
    std::string s;
    for (char c : spec) s += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    if (s == "explorer" || s == "file explorer" || s == "file explorer.lnk") return PinKind::Explorer;
    bool lnk = s.size() > 4 && s.compare(s.size() - 4, 4, ".lnk") == 0;
    if (!lnk && s.find('!') != std::string::npos) return PinKind::AppID;
    return PinKind::Shortcut;
}

// PinFamily is the package family an app ID belongs to: the part before
// its "!".
inline std::string PinFamily(const std::string& appID) { return appID.substr(0, appID.find('!')); }

// IsTerminalApp says whether an app ID is Windows Terminal (or its
// Preview), which the title bar draws rather than showing its logo.
inline bool IsTerminalApp(const std::string& appID) {
    return appID.compare(0, 25, "Microsoft.WindowsTerminal") == 0 && appID.find('!') != std::string::npos;
}

// PinWindow is what a window is matched to a pin by.
struct PinWindow {
    std::string exe;     // the process's path, lower case
    std::string family;  // its package family, for Store apps; else empty
    std::string cls;     // the window's class
};

// PinMatches says whether a window belongs to a pin: File Explorer's
// folder windows, a Store app's by package family, else by exe (target,
// lower case).
inline bool PinMatches(PinKind kind, const std::string& target, const PinWindow& w) {
    switch (kind) {
    case PinKind::Explorer: return w.cls == "CabinetWClass";
    case PinKind::AppID: {
        // The family is the app ID up to its "!", compared in place: this
        // runs for every pin and window each frame.
        size_t n = std::min(target.find('!'), target.size());
        if (w.family.empty() || w.family.size() != n) return false;
        for (size_t i = 0; i < n; i++)
            if (std::tolower((unsigned char)target[i]) != std::tolower((unsigned char)w.family[i])) return false;
        return true;
    }
    case PinKind::Shortcut: return !target.empty() && w.exe == target;
    }
    return false;
}

// MenuGroup is one of the apps menu's groups: a heading over some apps.
struct MenuGroup {
    std::string name;
    std::vector<std::string> apps;
};

// MenuSections lays out the apps menu's own apps (names, in the order
// shown, private ones already left out if they're hidden) under headings:
// those in no group first, under "Apps" (no heading if they're all there
// is), then each group in turn with its apps in its own order. Private
// apps in no group go under "Private": where a group of that name stands,
// else after the rest.
inline std::vector<MenuGroup> MenuSections(const std::vector<std::string>& names, const std::vector<MenuGroup>& groups,
                                           const std::vector<std::string>& privates, bool pinsAbove) {
    auto has = [](const std::vector<std::string>& v, const std::string& s) {
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    auto grouped = [&](const std::string& n) {
        for (const auto& g : groups)
            if (has(g.apps, n)) return true;
        return false;
    };
    std::vector<std::string> loose, priv;
    for (const auto& n : names)
        if (!grouped(n)) (has(privates, n) ? priv : loose).push_back(n);
    std::vector<MenuGroup> out;
    bool privateGroup = false;
    if (!loose.empty()) out.push_back({pinsAbove || !groups.empty() || !priv.empty() ? "Apps" : "", loose});
    for (const auto& g : groups) {
        MenuGroup s{g.name, {}};
        for (const auto& n : g.apps)
            if (has(names, n)) s.apps.push_back(n);
        if (g.name == "Private") {
            privateGroup = true;
            s.apps.insert(s.apps.end(), priv.begin(), priv.end());
        }
        if (!s.apps.empty()) out.push_back(std::move(s));
    }
    if (!privateGroup && !priv.empty()) out.push_back({"Private", priv});
    return out;
}

// PinWindows lists the windows that belong to a pin, in list order.
template <typename Windows>
std::vector<uintptr_t> PinWindows(PinKind kind, const std::string& target, const Windows& windows) {
    std::vector<uintptr_t> out;
    for (const auto& w : windows)
        if (PinMatches(kind, target, w.match)) out.push_back(w.id);
    return out;
}

// PinTarget picks which of a pin's windows a click brings up, given them
// in list order, the one in front (0 for none of them) and when each was
// last active. In front already: the next one along (cycling), or 0 to
// minimise it when it's the only one. Otherwise the one used last.
inline uintptr_t PinTarget(const std::vector<uintptr_t>& windows, uintptr_t front,
                           const std::map<uintptr_t, double>& lastActive) {
    if (windows.empty()) return 0;
    for (size_t i = 0; i < windows.size(); i++)
        if (windows[i] == front) return windows.size() == 1 ? 0 : windows[(i + 1) % windows.size()];
    uintptr_t best = windows[0];
    double when = -1;
    for (uintptr_t w : windows) {
        auto it = lastActive.find(w);
        double t = it == lastActive.end() ? -1 : it->second;
        if (t > when) when = t, best = w;
    }
    return best;
}

}  // namespace ks
