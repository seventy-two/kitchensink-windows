#include "tray.h"

#include <commctrl.h>

#include <chrono>
#include <iterator>
#include <cstring>
#include <set>

#include "net.h"
#include "winicon.h"
#include "winutil.h"

namespace ks {

namespace {

// Explorer's record of an icon, which a tray button's dwData points to (as
// Windows 10 lays it out, 64-bit).
struct TrayData {
    uint64_t window;
    uint32_t id, message;
    uint32_t reserved, version;
    uint64_t icon;
};
static_assert(sizeof(TrayData) == 32, "Explorer's layout");

HWND shownToolbar() {
    HWND t = FindWindowW(L"Shell_TrayWnd", nullptr);
    t = FindWindowExW(t, nullptr, L"TrayNotifyWnd", nullptr);
    t = FindWindowExW(t, nullptr, L"SysPager", nullptr);
    return FindWindowExW(t, nullptr, L"ToolbarWindow32", nullptr);
}

HWND overflowToolbar() {
    return FindWindowExW(FindWindowW(L"NotifyIconOverflowWindow", nullptr), nullptr, L"ToolbarWindow32", nullptr);
}

bool ask(HWND tb, UINT msg, WPARAM w, LPARAM l, DWORD_PTR* r) {
    return SendMessageTimeoutW(tb, msg, w, l, SMTO_ABORTIFHUNG | SMTO_BLOCK, 200, r) != 0;
}

// read lists a tray toolbar's icons, reading Explorer's memory.
void read(HWND tb, DWORD self, std::vector<TrayIcon>& out, std::vector<uint64_t>& handles) {
    if (!tb) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(tb, &pid);
    HANDLE p = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!p) return;
    constexpr SIZE_T kBuf = 4096;
    void* mem = VirtualAllocEx(p, nullptr, kBuf, MEM_COMMIT, PAGE_READWRITE);
    DWORD_PTR n = 0;
    if (mem && ask(tb, TB_BUTTONCOUNT, 0, 0, &n)) {
        for (int i = 0; i < int(n) && i < 200; i++) {
            TBBUTTON b{};
            DWORD_PTR ok = 0;
            if (!ask(tb, TB_GETBUTTON, i, LPARAM(mem), &ok) || !ok) continue;
            if (!ReadProcessMemory(p, mem, &b, sizeof b, nullptr)) continue;
            if (b.fsState & TBSTATE_HIDDEN) continue;
            TrayData d{};
            if (!b.dwData || !ReadProcessMemory(p, reinterpret_cast<void*>(b.dwData), &d, sizeof d, nullptr)) continue;
            TrayIcon t;
            t.window = uintptr_t(d.window);
            t.id = d.id;
            t.message = d.message;
            t.version = int(d.version);
            DWORD owner = 0;
            GetWindowThreadProcessId(HWND(t.window), &owner);
            t.pid = owner;
            if (!t.window || !IsWindow(HWND(t.window)) || owner == self) continue;
            wchar_t cls[64] = L"", title[64] = L"";
            if (owner == pid) {  // only Explorer's own icons are looked at
                GetClassNameW(HWND(t.window), cls, 64);
                GetWindowTextW(HWND(t.window), title, 64);
            }
            if (HiddenTrayIcon(owner == pid, t.id, Narrow(cls), Narrow(title))) continue;
            DWORD_PTR len = 0;
            if (ask(tb, TB_GETBUTTONTEXTW, b.idCommand, LPARAM(mem), &len) && len > 0 && len < kBuf / 2 - 1) {
                std::wstring tip(len + 1, L'\0');
                if (ReadProcessMemory(p, mem, tip.data(), (len + 1) * 2, nullptr)) {
                    tip.resize(len);
                    t.tip = Narrow(tip);
                }
            }
            handles.push_back(d.icon);
            out.push_back(std::move(t));
        }
    }
    if (mem) VirtualFreeEx(p, mem, 0, MEM_RELEASE);
    CloseHandle(p);
}

constexpr int kIconSize = 32;

}  // namespace

Tray::Tray(NativeWindow self, std::function<void()> wake)
    : self_(self), wake_(std::move(wake)), thread_([this] { run(); }) {}

Tray::~Tray() {
    {
        std::lock_guard<std::mutex> l(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

std::vector<TrayIcon> Tray::Icons() {
    std::lock_guard<std::mutex> l(mu_);
    return list_;
}

void Tray::Refresh() {
    {
        std::lock_guard<std::mutex> l(mu_);
        dirty_ = true;
    }
    cv_.notify_all();
}

void Tray::Press(const TrayIcon& t, TrayButton b, TrayPress p, int x, int y) {
    if (!t.message || !IsWindow(HWND(t.window))) return;
    // The app may want the front for its menu or window; the dashboard had
    // the click, so it can let it.
    if (p == TrayPress::Up) AllowSetForegroundWindow(t.pid);
    for (const TrayMessage& m : TrayMessages(t.version, t.id, b, p, x, y))
        PostMessageW(HWND(t.window), t.message, WPARAM(m.wParam), LPARAM(m.lParam));
    Refresh();
}

void Tray::run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);  // for the shell's icons
    DWORD self = GetCurrentProcessId();
    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        cv_.wait_for(lock, std::chrono::seconds(2), [this] { return stop_ || dirty_; });
        if (stop_) break;
        dirty_ = false;
        lock.unlock();

        std::vector<TrayIcon> list;
        std::vector<uint64_t> handles;
        read(shownToolbar(), self, list, handles);
        read(overflowToolbar(), self, list, handles);
        for (size_t i = 0; i < list.size(); i++) {
            // Keyed by handle: an app changing its icon (a new badge) sets a
            // new one.
            char key[48];
            std::snprintf(key, sizeof key, "tray-icon:%llx", (unsigned long long)handles[i]);
            if (!icons_.Holds(key)) {
                std::string png = IconFromHandle(HICON(uintptr_t(handles[i])), kIconSize);
                // Some apps free their icon once the tray has its own copy,
                // leaving a dead handle here: their exe's icon will do.
                if (png.empty()) {
                    std::wstring exe = ExePath(list[i].pid);
                    if (!exe.empty()) png = IconFromShell(exe, kIconSize);
                }
                icons_.Put(key, std::move(png));
            }
            list[i].icon = key;
        }
        // Old handles' icons go (a badge or meter makes a new one each
        // change), so the store doesn't grow.
        std::set<std::string> used;
        for (const auto& t : list) used.insert(t.icon);
        icons_.Keep(used);

        lock.lock();
        bool changed = list.size() != list_.size();
        for (size_t i = 0; !changed && i < list.size(); i++)
            changed = list[i].window != list_[i].window || list[i].id != list_[i].id || list[i].tip != list_[i].tip ||
                      list[i].icon != list_[i].icon;
        if (changed) {
            list_ = std::move(list);
            lock.unlock();
            wake_();
            lock.lock();
        }
    }
    lock.unlock();
    CoUninitialize();
}

}  // namespace ks
