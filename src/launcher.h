// The apps menu in the title bar: the user's own Windows apps, listed in
// config.json's "apps", started or brought to the front from it.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "iconstore.h"
#include "shell.h"

namespace ks {

class ImageCache;

struct LaunchApp {
    std::string name;   // shown in the menu
    std::wstring path;  // the exe
};

// PinnedApp is one of the title bar's pinned apps, from config.json's "pins".
struct PinnedApp {
    std::string spec;    // as in config.json
    std::string name;    // shown in its tooltip
    PinKind kind = PinKind::Shortcut;
    std::string target;  // exe (lower case) for a shortcut, app ID for a Store app
    std::wstring open;   // what starts it: the shortcut, shell:AppsFolder\<app ID>, or explorer.exe
    std::wstring args;
    std::string icon;    // its key in the pin icons; "<key>:grey" for the grey one
};

class Launcher {
public:
    // Set takes config.json's "apps": each a name, found at
    // %LOCALAPPDATA%\Programs\<name>\<name>.exe, or a full path to an exe.
    // They're listed in A-Z order.
    void Set(const std::vector<std::string>& apps);
    // Installed lists the apps whose exe is there (looked for again every
    // few seconds).
    const std::vector<LaunchApp>& Installed();
    // Icon returns the key the app's icon is in the image cache under,
    // handing the cache the icon first if it hasn't it (empty if it has
    // none).
    std::string Icon(const LaunchApp& app, ImageCache& images, int size);
    // Running reports whether the app is running (looked at again every
    // second).
    bool Running(const LaunchApp& app);
    // Open brings the app's window to the front, or starts it.
    void Open(const LaunchApp& app) const;

    // SetPins takes config.json's "pins": each a shortcut (a .lnk's path,
    // or its file name among the taskbar's pinned ones), a Store app's app
    // ID ("family!App"), or "explorer" for File Explorer (This PC). Kept in
    // order; ones that can't be found are left out.
    void SetPins(const std::vector<std::string>& pins);
    const std::vector<PinnedApp>& Pins() const { return pins_; }
    // PinIcon is the pin's icon, in colour or grey, or null.
    const Texture* PinIcon(const PinnedApp& pin, bool grey, ImageCache& images);
    // Start starts a new copy of the pin's app (letting it to the front).
    void Start(const PinnedApp& pin) const;

private:
    std::vector<PinnedApp> pins_;
    IconStore pinIcons_;
    std::vector<LaunchApp> all_, installed_;
    double checked_ = -1e9;
    std::map<std::wstring, std::string> icons_;  // PNGs by exe, empty for none
    std::vector<std::wstring> running_;  // exe paths, lower case
    double runningChecked_ = -1e9;
};

}  // namespace ks
