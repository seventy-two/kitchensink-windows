// platform.h for Windows.
#include "platform.h"

#include <thread>

namespace ks {

uint64_t TickMs() { return GetTickCount64(); }

bool WindowMaximised(NativeWindow w) { return IsZoomed(w) != 0; }
bool WindowActive(NativeWindow w) { return GetForegroundWindow() == w; }
void MinimiseWindow(NativeWindow w) { ShowWindow(w, SW_MINIMIZE); }
void ToggleMaximise(NativeWindow w) { ShowWindow(w, IsZoomed(w) ? SW_RESTORE : SW_MAXIMIZE); }
void CloseMainWindow(NativeWindow w) { PostMessageW(w, WM_CLOSE, 0, 0); }

bool MouseOverWindow(NativeWindow w) {
    POINT p;
    if (!GetCursorPos(&p)) return false;
    HWND under = WindowFromPoint(p);
    return under && GetAncestor(under, GA_ROOT) == w;
}

void DisplaysOff() {
    std::thread([] {
        Sleep(800);
        PostMessageW(HWND_BROADCAST, WM_SYSCOMMAND, SC_MONITORPOWER, 2);
    }).detach();
}

bool CanTurnDisplaysOff() { return true; }

}  // namespace ks
