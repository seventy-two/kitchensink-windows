#include "brightness.h"

#include <windows.h>
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace ks {

namespace {

constexpr BYTE kLuminance = 0x10;  // VCP code for brightness

// Physical is the monitors behind one HMONITOR (usually one), opened for
// DDC/CI; they're closed when it goes.
struct Physical {
    HMONITOR monitor = nullptr;
    std::vector<PHYSICAL_MONITOR> handles;
    ~Physical() { close(); }
    void close() {
        if (!handles.empty()) DestroyPhysicalMonitors(DWORD(handles.size()), handles.data());
        handles.clear();
        monitor = nullptr;
    }
    void open(HMONITOR m) {
        close();
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(m, &n) || n == 0) return;
        handles.resize(n);
        if (!GetPhysicalMonitorsFromHMONITOR(m, n, handles.data())) {
            handles.clear();
            return;
        }
        monitor = m;
    }
};

// read gives the first physical monitor's brightness as 0-100, or -1.
int read(const Physical& p) {
    for (const auto& h : p.handles) {
        DWORD cur = 0, max = 0;
        if (GetVCPFeatureAndVCPFeatureReply(h.hPhysicalMonitor, kLuminance, nullptr, &cur, &max) && max > 0)
            return int(std::lround(100.0 * std::min(cur, max) / max));
    }
    return -1;
}

// write sets every physical monitor behind it (a monitor's maximum is
// nearly always 100, but scaled in case it isn't).
void write(const Physical& p, int value) {
    for (const auto& h : p.handles) {
        DWORD cur = 0, max = 100;
        if (!GetVCPFeatureAndVCPFeatureReply(h.hPhysicalMonitor, kLuminance, nullptr, &cur, &max) || max == 0) max = 100;
        SetVCPFeature(h.hPhysicalMonitor, kLuminance, DWORD(std::lround(value * max / 100.0)));
    }
}

}  // namespace

Brightness::Brightness(NativeWindow window, std::function<void()> wake)
    : window_(window), wake_(std::move(wake)), thread_([this] { run(); }) {}

Brightness::~Brightness() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

bool Brightness::Available() {
    std::lock_guard<std::mutex> lock(mu_);
    return available_;
}

int Brightness::Value() {
    std::lock_guard<std::mutex> lock(mu_);
    return value_;
}

void Brightness::Set(int value) {
    value = std::clamp(value, 0, 100);
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (value == value_ && want_ < 0) return;
        value_ = want_ = value;
    }
    cv_.notify_all();
}

void Brightness::run() {
    Physical p;
    auto next = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        cv_.wait_until(lock, next, [&] { return stop_ || want_ >= 0; });
        if (stop_) break;
        int want = want_;
        want_ = -1;
        lock.unlock();

        HMONITOR m = MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST);
        bool moved = m != p.monitor;
        if (moved) p.open(m);
        int got = -1;
        if (want >= 0 && !moved) {
            write(p, want);
        } else {
            // Due a reading (or the window's on another monitor now, where
            // what was asked for may not suit).
            got = read(p);
            next = std::chrono::steady_clock::now() + std::chrono::seconds(kEvery);
        }

        lock.lock();
        if (got >= 0 || want < 0) {
            bool changed = available_ != (got >= 0) || (got >= 0 && want_ < 0 && value_ != got);
            available_ = got >= 0;
            if (got >= 0 && want_ < 0) value_ = got;
            if (changed && wake_) {
                lock.unlock();
                wake_();
                lock.lock();
            }
        }
    }
}

}  // namespace ks
