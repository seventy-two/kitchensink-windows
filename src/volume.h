// The default speakers' volume and mute, for the side column's slider, as
// the volume flyout sets them. Windows only; unavailable elsewhere.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ks {

// AppVolume is one app's share of the speakers, as the volume mixer shows
// it: its sessions, grouped by app.
struct AppVolume {
    std::string name;  // the app's description (e.g. "Firefox"), or "System sounds"
    int value = 0;     // 0 to 100
    bool muted = false;
};

class Volume {
public:
    // wake is called (from another thread) when something else changes
    // the volume or the default speakers.
    explicit Volume(std::function<void()> wake);
    ~Volume();

    bool Available();
    int Value();  // 0 to 100
    bool Muted();
    std::string Device();  // the speakers' name, as Sound settings shows it
    void Set(int value);
    void SetMuted(bool muted);

    // Apps lists the apps with sound on the speakers (looked at again every
    // half second), and SetApp / SetAppMuted change one by name.
    std::vector<AppVolume> Apps();
    void SetApp(const std::string& name, int value);
    void SetAppMuted(const std::string& name, bool muted);

    // OpenDevices opens the classic Sound panel (to pick or disable
    // speakers), OpenSettings Windows' Sound settings.
    static void OpenDevices();
    static void OpenSettings();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ks
