// The Windows client's PC-specific parts, left out on Linux: the "This PC"
// and "Now playing" panels, the apps menu, Lick of the Day, watchgrab's
// download buttons, uploading RuneLite's files and Squeak's battery
// readings. Each is here as the
// do-nothing version the dashboard treats as absent.
#include "battery.h"
#include "grabber.h"
#include "launcher.h"
#include "lick.h"
#include "media.h"
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
void MediaWatcher::Command(MediaCommand) {}
void MediaWatcher::run() {}

void Launcher::Set(const std::vector<std::string>&) {}
const std::vector<LaunchApp>& Launcher::Installed() { return installed_; }
std::string Launcher::Icon(const LaunchApp&, ImageCache&, int) { return {}; }
bool Launcher::Running(const LaunchApp&) { return false; }
void Launcher::Open(const LaunchApp&) const {}

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

struct Grabber::Shared {};
Grabber::Grabber(std::function<void()>) {}
bool Grabber::Has(const std::string&) { return false; }
GrabState Grabber::Get(const std::string&) { return {}; }
void Grabber::Start(const std::string&) {}

void Uploader::Check(const std::vector<Snapshot>&, Client&) {}

}  // namespace ks
