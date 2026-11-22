// The default speakers' volume and mute, for the side column's slider, as
// the volume flyout sets them. Windows only; unavailable elsewhere.
#pragma once

#include <functional>
#include <memory>
#include <string>

namespace ks {

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ks
