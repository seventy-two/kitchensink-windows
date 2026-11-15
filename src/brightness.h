// The brightness of the monitor the dashboard is on, read and set over
// DDC/CI (VCP code 0x10), as the monitor's own buttons would. DDC/CI is
// slow (tens of milliseconds a call), so it's done on a thread of its own.
#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "platform.h"

namespace ks {

class Brightness {
public:
    // wake is called from the thread when a reading comes in.
    Brightness(NativeWindow window, std::function<void()> wake);
    ~Brightness();

    // Available says whether the monitor answered; until it has, or if it
    // can't, there's nothing to show.
    bool Available();
    // Value is the brightness, 0 to 100: the one last set, else the
    // monitor's.
    int Value();
    // Set asks for a brightness. Only the newest of several asked for in
    // quick succession is sent.
    void Set(int value);

    // How often the monitor is read again, in seconds, in case its own
    // buttons changed it or the window moved to another monitor.
    static constexpr int kEvery = 60;

private:
    void run();
    NativeWindow window_;
    std::function<void()> wake_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false, available_ = false;
    int value_ = 0, want_ = -1;  // want_ is -1 when nothing's waiting to be sent
    std::thread thread_;
};

}  // namespace ks
