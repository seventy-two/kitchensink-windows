// The apps menu in the title bar: the user's own Windows apps, listed in
// config.json's "apps", started or brought to the front from it.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace ks {

class ImageCache;

struct LaunchApp {
    std::string name;   // shown in the menu
    std::wstring path;  // the exe
};

class Launcher {
public:
    // Set takes config.json's "apps": each a name, found at
    // %LOCALAPPDATA%\Programs\<name>\<name>.exe, or a full path to an exe.
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

private:
    std::vector<LaunchApp> all_, installed_;
    double checked_ = -1e9;
    std::map<std::wstring, std::string> icons_;  // PNGs by exe, empty for none
    std::vector<std::wstring> running_;  // exe paths, lower case
    double runningChecked_ = -1e9;
};

}  // namespace ks
