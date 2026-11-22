// The Windows client's PC-specific parts, left out on Linux: the "This PC"
// and "Now playing" panels, the apps menu and pins, the open windows, tray
// and taskbar, Lick of the Day, watchgrab's
// download buttons, uploading RuneLite's files, Squeak's battery
// readings and the monitor's brightness. Each is here as the
// do-nothing version the dashboard treats as absent.
#include "battery.h"
#include "brightness.h"
#include "grabber.h"
#include "launcher.h"
#include "lick.h"
#include "media.h"
#include "openwindows.h"
#include "taskbar.h"
#include "tray.h"
#include "sysinfo.h"
#include "upload.h"

namespace ks {

LocalStats::LocalStats(std::function<void()> wake) : wake_(std::move(wake)) {}
LocalStats::~LocalStats() = default;
void LocalStats::Want() {}
bool LocalStats::Take(std::vector<Snapshot>&) { return false; }
void LocalStats::run() {}

MediaWatcher::MediaWatcher(std::function<void()> wake, ImageCache* images) : wake_(std::move(wake)), images_(images) {}
MediaWatcher::~MediaWatcher() = default;
void MediaWatcher::Want() {}
bool MediaWatcher::Take(Snapshot&) { return false; }
void MediaWatcher::Command(MediaCommand, const std::string&) {}
void MediaWatcher::run() {}

void Launcher::Set(const std::vector<std::string>&) {}
const std::vector<LaunchApp>& Launcher::Installed() { return installed_; }
std::string Launcher::Icon(const LaunchApp&, ImageCache&, int) { return {}; }
bool Launcher::Running(const LaunchApp&) { return false; }
void Launcher::Open(const LaunchApp&) const {}
void Launcher::SetPins(const std::vector<std::string>&) {}
const Texture* Launcher::PinIcon(const PinnedApp&, bool, ImageCache&) { return nullptr; }
void Launcher::Start(const PinnedApp&) const {}

OpenWindows::OpenWindows(NativeWindow self, std::function<void()> wake) : self_(self), wake_(std::move(wake)) {}
OpenWindows::~OpenWindows() = default;
std::vector<OpenWindow> OpenWindows::List() { return {}; }
void OpenWindows::ShellEvent(unsigned, uintptr_t) {}
uintptr_t OpenWindows::Front() const { return 0; }
void OpenWindows::Click(uintptr_t) {}
void OpenWindows::Show(uintptr_t) {}
void OpenWindows::Minimise(uintptr_t) {}
void OpenWindows::Maximise(uintptr_t) {}
void OpenWindows::Close(uintptr_t) {}
void OpenWindows::run() {}

Tray::Tray(NativeWindow self, std::function<void()> wake) : self_(self), wake_(std::move(wake)) {}
Tray::~Tray() = default;
std::vector<TrayIcon> Tray::Icons() { return {}; }
void Tray::Refresh() {}
void Tray::Press(const TrayIcon&, TrayButton, TrayPress, int, int) {}
void Tray::run() {}

void Taskbar::Hide(bool*) {}
void Taskbar::Restore(bool) {}
void Taskbar::Check() {}

BatteryWatcher::BatteryWatcher(std::function<void()>) {}
BatteryWatcher::~BatteryWatcher() = default;
void BatteryWatcher::Want(bool) {}
bool BatteryWatcher::Take(std::string&) { return false; }
void BatteryWatcher::run() {}

bool HiddenApp::Installed() { return false; }
bool HiddenApp::Running() const { return false; }
bool HiddenApp::Shown() const { return false; }
void HiddenApp::Start() {}
void HiddenApp::Toggle() {}
void HiddenApp::Raise() const {}

Brightness::Brightness(NativeWindow, std::function<void()>) {}
Brightness::~Brightness() = default;
bool Brightness::Available() { return false; }
int Brightness::Value() { return 0; }
void Brightness::Set(int) {}
void Brightness::run() {}

struct Grabber::Shared {};
Grabber::Grabber(std::function<void()>) {}
bool Grabber::Has(const std::string&) { return false; }
GrabState Grabber::Get(const std::string&) { return {}; }
void Grabber::Start(const std::string&) {}

void Uploader::Check(const std::vector<Snapshot>&, Client&) {}

}  // namespace ks
