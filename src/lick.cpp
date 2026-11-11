#include "lick.h"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdlib>
#include <string>

namespace ks {

namespace {

// The apps' message for hiding and showing.
constexpr UINT WM_APP_TOGGLE = WM_APP + 2;

std::wstring exePath(const wchar_t* name) {
    const wchar_t* v = _wgetenv(L"LOCALAPPDATA");
    return std::wstring(v ? v : L"") + L"\\Programs\\" + name + L"\\" + name + L".exe";
}

double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

void launch(const wchar_t* name, const wchar_t* args) {
    std::wstring exe = exePath(name);
    ShellExecuteW(nullptr, L"open", exe.c_str(), args, nullptr, SW_SHOWNORMAL);
}

}  // namespace

bool HiddenApp::Installed() {
    double t = seconds();
    if (t - checked_ > 5) {
        checked_ = t;
        installed_ = GetFileAttributesW(exePath(name_).c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    return installed_;
}

bool HiddenApp::Running() const { return FindWindowW(name_, nullptr) != nullptr; }

bool HiddenApp::Shown() const {
    HWND h = FindWindowW(name_, nullptr);
    return h && IsWindowVisible(h);
}

void HiddenApp::Start() {
    if (Installed() && !Running()) launch(name_, L"-background");
}

void HiddenApp::Raise() const {
    HWND h = FindWindowW(name_, nullptr);
    if (h && IsWindowVisible(h))
        SetWindowPos(h, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void HiddenApp::Toggle() {
    HWND h = FindWindowW(name_, nullptr);
    if (!h) {
        if (Installed()) launch(name_, args_);
        return;
    }
    // So it can come to the front when it shows.
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    AllowSetForegroundWindow(pid);
    PostMessageW(h, WM_APP_TOGGLE, 0, 0);
}

}  // namespace ks
