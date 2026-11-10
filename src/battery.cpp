#include "battery.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

namespace ks {

namespace {

std::wstring env(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    return v ? v : L"";
}

// read runs squeak.exe -battery into a temporary file and returns what it
// wrote, or "" if it isn't installed or found nothing.
std::string read() {
    std::wstring exe = env(L"LOCALAPPDATA") + L"\\Programs\\squeak\\squeak.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) return "";
    wchar_t dir[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, dir)) return "";
    std::wstring out = std::wstring(dir) + L"kitchensink-battery.json";
    DeleteFileW(out.c_str());

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" -battery " + out;
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, nullptr, &si, &pi))
        return "";
    // Squeak gives up after 30s with no answer from the devices.
    if (WaitForSingleObject(pi.hProcess, 45000) != WAIT_OBJECT_0) TerminateProcess(pi.hProcess, 1);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (code != 0) return "";

    std::string text;
    if (FILE* f = _wfopen(out.c_str(), L"rb")) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 65536) text.append(buf, n);
        fclose(f);
    }
    DeleteFileW(out.c_str());
    return text;
}

}  // namespace

BatteryWatcher::BatteryWatcher(std::function<void()> wake) : wake_(std::move(wake)), thread_([this] { run(); }) {}

BatteryWatcher::~BatteryWatcher() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

void BatteryWatcher::Want(bool want) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (want_ == want) return;
        want_ = want;
    }
    cv_.notify_all();
}

bool BatteryWatcher::Take(std::string& json) {
    std::lock_guard<std::mutex> lock(mu_);
    if (!fresh_) return false;
    fresh_ = false;
    json = latest_;
    return true;
}

void BatteryWatcher::run() {
    std::unique_lock<std::mutex> lock(mu_);
    for (;;) {
        cv_.wait(lock, [this] { return stop_ || want_; });
        if (stop_) return;
        lock.unlock();
        std::string got = read();
        lock.lock();
        if (stop_) return;
        if (!got.empty()) {
            latest_ = std::move(got);
            fresh_ = true;
            lock.unlock();
            wake_();
            lock.lock();
        }
        cv_.wait_for(lock, std::chrono::seconds(kEvery), [this] { return stop_; });
        if (stop_) return;
    }
}

}  // namespace ks
