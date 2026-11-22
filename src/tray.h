// The notification area's icons, read from Explorer's own tray (which
// stays there, hidden, with the taskbar), for the side column's Tray card.
// Clicks go to each icon's app as Explorer would send them. Windows only;
// empty elsewhere.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform.h"
#include "iconstore.h"
#include "shell.h"

namespace ks {

struct TrayIcon {
    uintptr_t window = 0;  // the app's window that takes the icon's messages
    unsigned id = 0, message = 0;
    int version = 0;       // NOTIFYICON_VERSION_4 or older
    unsigned pid = 0;      // the app's process
    std::string tip;       // the tooltip
    std::string icon;      // its key in the icon store
};

class Tray {
public:
    // wake is called from the thread when the icons change.
    Tray(NativeWindow self, std::function<void()> wake);
    ~Tray();

    std::vector<TrayIcon> Icons();
    // Refresh reads the tray again soon (e.g. Explorer restarted).
    void Refresh();
    // Press sends an icon a mouse press at the pointer's screen position.
    void Press(const TrayIcon& icon, TrayButton b, TrayPress p, int x, int y);

    IconStore& Store() { return icons_; }

private:
    void run();
    NativeWindow self_;
    std::function<void()> wake_;
    IconStore icons_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool stop_ = false, dirty_ = true;
    std::vector<TrayIcon> list_;
    std::thread thread_;
};

}  // namespace ks
