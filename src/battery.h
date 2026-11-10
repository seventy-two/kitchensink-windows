// Reads wireless devices' batteries with Squeak (squeak.exe -battery), for
// the server's battery monitor, which says when one has finished charging.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ks {

// The monitor kind the readings go to.
inline constexpr const char* kBatteryKind = "battery";

class BatteryWatcher {
public:
    explicit BatteryWatcher(std::function<void()> wake);
    ~BatteryWatcher();

    // Want says whether the server has a battery monitor; readings are only
    // taken while it does, every kEvery seconds.
    void Want(bool want);
    // Take gives the latest reading (Squeak's JSON, {"devices":[...]}) if
    // there's one not yet taken.
    bool Take(std::string& json);

    static constexpr int kEvery = 120;

private:
    void run();
    std::function<void()> wake_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool want_ = false, stop_ = false, fresh_ = false;
    std::string latest_;
    std::thread thread_;
};

}  // namespace ks
