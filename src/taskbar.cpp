#include "taskbar.h"

#include <windows.h>
#include <shellapi.h>

#include <vector>

namespace ks {

namespace {

// bars lists the taskbars: the main one and each other monitor's.
std::vector<HWND> bars() {
    std::vector<HWND> out;
    if (HWND h = FindWindowW(L"Shell_TrayWnd", nullptr)) out.push_back(h);
    for (HWND h = nullptr; (h = FindWindowExW(nullptr, h, L"Shell_SecondaryTrayWnd", nullptr));) out.push_back(h);
    return out;
}

UINT_PTR state() {
    APPBARDATA ab{sizeof ab};
    ab.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    return UINT_PTR(SHAppBarMessage(ABM_GETSTATE, &ab));
}

void setState(UINT_PTR s) {
    APPBARDATA ab{sizeof ab};
    ab.hWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
    ab.lParam = LPARAM(s);
    SHAppBarMessage(ABM_SETSTATE, &ab);
}

}  // namespace

void Taskbar::Hide(bool* wasAutoHide) {
    std::vector<HWND> all = bars();
    if (all.empty()) return;
    UINT_PTR s = state();
    // Only when it's showing, so a taskbar left auto-hiding by a crash
    // isn't taken as the user's choice.
    if (wasAutoHide && IsWindowVisible(all[0])) *wasAutoHide = (s & ABS_AUTOHIDE) != 0;
    if (!(s & ABS_AUTOHIDE)) setState(s | ABS_AUTOHIDE);
    for (HWND h : all) ShowWindow(h, SW_HIDE);
    hidden_ = true;
}

void Taskbar::Restore(bool autoHide) {
    if (!hidden_) return;
    std::vector<HWND> all = bars();
    UINT_PTR s = state();
    setState(autoHide ? (s | ABS_AUTOHIDE) : (s & ~UINT_PTR(ABS_AUTOHIDE)));
    for (HWND h : all) ShowWindow(h, SW_SHOWNA);
    hidden_ = false;
}

void Taskbar::Check() {
    if (!hidden_) return;
    for (HWND h : bars())
        if (IsWindowVisible(h)) {
            Hide(nullptr);
            return;
        }
    if (!(state() & ABS_AUTOHIDE)) Hide(nullptr);
}

}  // namespace ks
