// The open windows the taskbar would have buttons for, for the side
// column's Windows card and the pinned apps: listed on a thread of its own
// (asking a window for its icon can wait on a busy app), refreshed when the
// shell says something changed.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "platform.h"
#include "iconstore.h"
#include "shell.h"

namespace ks {

struct OpenWindow {
    uintptr_t id = 0;   // the window handle
    std::string title;
    std::string icon;   // its key in the icon store; empty for none
    PinWindow match;    // exe, package family and class, for pins
    bool minimised = false;
};

class OpenWindows {
public:
    // wake is called from the thread when the list changes.
    OpenWindows(NativeWindow self, std::function<void()> wake);
    ~OpenWindows();

    // List is the windows, in the order they first showed up (as the
    // taskbar keeps them).
    std::vector<OpenWindow> List();
    // ShellEvent takes the shell's word (RegisterShellHookWindow) that a
    // window came, went, changed, was activated or is flashing.
    void ShellEvent(unsigned code, uintptr_t window);
    // Front is the window in front, if it's one of the list's.
    uintptr_t Front() const;
    bool Flashing(uintptr_t w) const { return flashing_.count(w) > 0; }
    // LastActive is when each window was last brought to the front (a
    // seconds clock).
    const std::map<uintptr_t, double>& LastActive() const { return lastActive_; }

    // Click does what a taskbar button does: bring the window up, or
    // minimise it if it's already in front.
    void Click(uintptr_t w);
    void Show(uintptr_t w);      // restore if minimised, and bring to front
    void Minimise(uintptr_t w);
    void Maximise(uintptr_t w);
    void Close(uintptr_t w);     // as its close button

    IconStore& Icons() { return icons_; }

private:
    void run();
    NativeWindow self_;
    std::function<void()> wake_;
    IconStore icons_;
    std::set<uintptr_t> flashing_;
    std::map<uintptr_t, double> lastActive_;

    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false, dirty_ = true;
    std::vector<OpenWindow> list_;
    std::thread thread_;
};

}  // namespace ks
