#include "openwindows.h"

#include <appmodel.h>
#include <dwmapi.h>
#include <propkey.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <set>

#include "net.h"
#include "platform.h"
#include "winicon.h"
#include "winutil.h"

namespace ks {

namespace {

double seconds() { return TickMs() / 1000.0; }

// process gives a window's exe (lower case) and package family.
void process(HWND h, PinWindow& m) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    m.exe = Narrow(LowerPath(ExePath(pid)));  // lower case, as the launcher's pins
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    UINT32 len = 0;
    if (GetPackageFamilyName(p, &len, nullptr) == ERROR_INSUFFICIENT_BUFFER && len) {
        std::wstring fam(len, L'\0');
        if (GetPackageFamilyName(p, &len, fam.data()) == ERROR_SUCCESS) {
            fam.resize(len ? len - 1 : 0);
            m.family = Narrow(fam);
        }
    }
    CloseHandle(p);
}

// appID is a window's AppUserModelID, which Store apps' frames carry.
std::wstring appID(HWND h) {
    std::wstring out;
    IPropertyStore* ps = nullptr;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(h, IID_PPV_ARGS(&ps)))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(ps->GetValue(PKEY_AppUserModel_ID, &v)) && v.vt == VT_LPWSTR && v.pwszVal) out = v.pwszVal;
        PropVariantClear(&v);
        ps->Release();
    }
    return out;
}

// windowIcon asks the window for its small icon, giving up on a busy one.
HICON windowIcon(HWND h) {
    for (WPARAM which : {WPARAM(ICON_SMALL2), WPARAM(ICON_SMALL), WPARAM(ICON_BIG)}) {
        DWORD_PTR r = 0;
        if (SendMessageTimeoutW(h, WM_GETICON, which, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &r) && r) return HICON(r);
    }
    if (HICON i = HICON(GetClassLongPtrW(h, GCLP_HICONSM))) return i;
    return HICON(GetClassLongPtrW(h, GCLP_HICON));
}

constexpr int kIconSize = 32;

}  // namespace

OpenWindows::OpenWindows(NativeWindow self, std::function<void()> wake)
    : self_(self), wake_(std::move(wake)), thread_([this] { run(); }) {
    RegisterShellHookWindow(self_);
}

OpenWindows::~OpenWindows() {
    DeregisterShellHookWindow(self_);
    {
        std::lock_guard<std::mutex> l(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

std::vector<OpenWindow> OpenWindows::List() {
    std::lock_guard<std::mutex> l(mu_);
    return list_;
}

void OpenWindows::ShellEvent(unsigned code, uintptr_t w) {
    // HSHELL_FLASH is HSHELL_REDRAW with the high bit; activations (and
    // the "rude" ones of full-screen apps) stop a window flashing.
    if (code == (HSHELL_REDRAW | HSHELL_HIGHBIT)) flashing_.insert(w);
    if (code == HSHELL_WINDOWACTIVATED || code == HSHELL_RUDEAPPACTIVATED) {
        flashing_.erase(w);
        if (w && HWND(w) != self_) lastActive_[w] = seconds();
    }
    if (code == HSHELL_WINDOWDESTROYED) flashing_.erase(w), lastActive_.erase(w);
    {
        std::lock_guard<std::mutex> l(mu_);
        dirty_ = true;
    }
    cv_.notify_all();
}

uintptr_t OpenWindows::Front() const {
    if (HWND fg = GetForegroundWindow()) return uintptr_t(fg);
    // A click on the dashboard can leave no window in front for a moment
    // (it doesn't take the front itself), so then it's the one activated
    // last, as the taskbar remembers it, unless that's since been
    // minimised or hidden.
    uintptr_t best = 0;
    double when = -1;
    for (const auto& [w, t] : lastActive_)
        if (t > when) when = t, best = w;
    if (best && IsWindow(HWND(best)) && IsWindowVisible(HWND(best)) && !IsIconic(HWND(best))) return best;
    return 0;
}

void OpenWindows::Click(uintptr_t w) {
    HWND h = HWND(w);
    if (w == Front() && !IsIconic(h)) Minimise(w);
    else Show(w);
}

void OpenWindows::Show(uintptr_t w) {
    HWND h = HWND(w);
    if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
    // The dashboard had the click, so Windows lets it hand over the front.
    SetForegroundWindow(h);
    flashing_.erase(w);
    lastActive_[w] = seconds();
}

void OpenWindows::Minimise(uintptr_t w) { PostMessageW(HWND(w), WM_SYSCOMMAND, SC_MINIMIZE, 0); }

void OpenWindows::Maximise(uintptr_t w) {
    PostMessageW(HWND(w), WM_SYSCOMMAND, SC_MAXIMIZE, 0);
    SetForegroundWindow(HWND(w));
}

void OpenWindows::Close(uintptr_t w) { PostMessageW(HWND(w), WM_SYSCOMMAND, SC_CLOSE, 0); }

void OpenWindows::run() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<uintptr_t> order;  // first seen first
    // What doesn't change about a window (its process and class), and its
    // icon key, looked for again every few seconds.
    struct Known {
        PinWindow match;
        std::wstring aumid;
        std::string icon;
        double iconAt = -1e9;
    };
    std::map<uintptr_t, Known> known;
    std::map<std::string, bool> made;  // icon keys tried
    double scanned = -1e9;
    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        // Titles change without the shell saying (or saying late), so look
        // again every second anyway; a burst of shell events makes one look.
        cv_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_ || dirty_; });
        if (stop_) break;
        if (double wait = 0.25 - (seconds() - scanned); wait > 0) {
            cv_.wait_for(lock, std::chrono::duration<double>(wait), [this] { return stop_; });
            if (stop_) break;
        }
        dirty_ = false;
        scanned = seconds();
        lock.unlock();

        struct Found {
            HWND h;
            std::wstring title;
        };
        std::vector<Found> found;
        struct Ctx {
            HWND self;
            std::vector<Found>* out;
        } ctx{self_, &found};
        EnumWindows(
            [](HWND h, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<Ctx*>(lp);
                LONG ex = GetWindowLongW(h, GWL_EXSTYLE);
                DWORD cloaked = 0;
                DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
                wchar_t title[512];
                int n = GetWindowTextW(h, title, 512);
                WindowTraits t;
                t.visible = IsWindowVisible(h);
                t.toolWindow = ex & WS_EX_TOOLWINDOW;
                t.appWindow = ex & WS_EX_APPWINDOW;
                t.owned = GetWindow(h, GW_OWNER) != nullptr;
                t.cloaked = cloaked != 0;
                t.hasTitle = n > 0;
                t.ours = h == c->self;
                if (ShowsOnTaskbar(t)) c->out->push_back({h, std::wstring(title, n)});
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&ctx));

        // Keep the order windows were first seen in; new ones go last.
        std::vector<uintptr_t> now;
        for (const auto& f : found) now.push_back(uintptr_t(f.h));
        order.erase(std::remove_if(order.begin(), order.end(),
                                   [&](uintptr_t w) { return std::find(now.begin(), now.end(), w) == now.end(); }),
                    order.end());
        for (uintptr_t w : now)
            if (std::find(order.begin(), order.end(), w) == order.end()) order.push_back(w);
        for (auto it = known.begin(); it != known.end();)
            it = std::find(order.begin(), order.end(), it->first) == order.end() ? known.erase(it) : std::next(it);

        std::vector<OpenWindow> list;
        for (uintptr_t w : order) {
            HWND h = HWND(w);
            auto f = std::find_if(found.begin(), found.end(), [&](const Found& x) { return x.h == h; });
            OpenWindow o;
            o.id = w;
            o.title = Narrow(f->title);
            o.minimised = IsIconic(h);
            auto [it, fresh] = known.try_emplace(w);
            Known& k = it->second;
            if (fresh) {
                wchar_t cls[128] = L"";
                GetClassNameW(h, cls, 128);
                k.match.cls = Narrow(cls);
                process(h, k.match);
                // A Store app's frame belongs to ApplicationFrameHost: its
                // app ID says which app it is, for the icon and pins.
                if (k.match.cls == "ApplicationFrameWindow") k.aumid = appID(h);
                if (!k.aumid.empty() && k.match.family.empty()) k.match.family = PinFamily(Narrow(k.aumid));
            }
            o.match = k.match;

            // The icon: the window's own, else the Store app's or exe's.
            // Asked again every few seconds, as some apps change it.
            if (scanned - k.iconAt >= 5) {
                k.iconAt = scanned;
                HICON hi = k.aumid.empty() ? windowIcon(h) : nullptr;
                char key[64];
                std::snprintf(key, sizeof key, "win-icon:%llx:%llx", (unsigned long long)w, (unsigned long long)uintptr_t(hi));
                std::string ik = hi ? key : !k.aumid.empty() ? "app-id-icon:" + Narrow(k.aumid) : "exe-icon:" + k.match.exe;
                if (!made.count(ik)) {
                    std::string png = hi ? IconFromHandle(hi, kIconSize)
                                     : !k.aumid.empty() ? IconFromAppID(k.aumid, kIconSize)
                                                        : IconFromShell(Widen(k.match.exe), kIconSize);
                    made[ik] = !png.empty();
                    if (!png.empty()) icons_.Put(ik, std::move(png));
                }
                k.icon = made[ik] ? ik : "";
            }
            o.icon = k.icon;
            list.push_back(std::move(o));
        }

        // Icons no window shows any more go, so the store doesn't grow
        // with every window ever opened.
        std::set<std::string> used;
        for (const auto& [w, k] : known) used.insert(k.icon);
        for (auto it = made.begin(); it != made.end();)
            it = used.count(it->first) ? std::next(it) : made.erase(it);
        icons_.Keep(used);

        lock.lock();
        bool changed = list.size() != list_.size();
        for (size_t i = 0; !changed && i < list.size(); i++)
            changed = list[i].id != list_[i].id || list[i].title != list_[i].title || list[i].icon != list_[i].icon ||
                      list[i].minimised != list_[i].minimised;
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
