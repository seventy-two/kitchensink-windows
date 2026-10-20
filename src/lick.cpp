#include "lick.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdlib>
#include <string>

namespace ks {

namespace {

// dailylick's window class, and its messages for hiding and showing.
const wchar_t* kClass = L"dailylick";
constexpr UINT WM_APP_TOGGLE = WM_APP + 2;

std::wstring exePath() {
    const wchar_t* v = _wgetenv(L"LOCALAPPDATA");
    return std::wstring(v ? v : L"") + L"\\Programs\\dailylick\\dailylick.exe";
}

double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void launch(const wchar_t* args) {
    std::wstring exe = exePath();
    ShellExecuteW(nullptr, L"open", exe.c_str(), args, nullptr, SW_SHOWNORMAL);
}

}  // namespace

bool LickOfTheDay::Installed() {
    double t = seconds();
    if (t - checked_ > 5) {
        checked_ = t;
        installed_ = GetFileAttributesW(exePath().c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    return installed_;
}

bool LickOfTheDay::Running() const { return FindWindowW(kClass, nullptr) != nullptr; }

bool LickOfTheDay::Shown() const {
    HWND h = FindWindowW(kClass, nullptr);
    return h && IsWindowVisible(h);
}

void LickOfTheDay::Start() {
    if (Installed() && !Running()) launch(L"-background");
}

void LickOfTheDay::Toggle() {
    HWND h = FindWindowW(kClass, nullptr);
    if (!h) {
        if (Installed()) launch(nullptr);
        return;
    }
    // So it can come to the front when it shows.
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    AllowSetForegroundWindow(pid);
    PostMessageW(h, WM_APP_TOGGLE, 0, 0);
}

}  // namespace ks
