#include "openwindows.h"

#include <appmodel.h>
#include <dwmapi.h>
#include <propkey.h>
#include <propsys.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <chrono>
#include <cwctype>

#include "winicon.h"

namespace ks {

namespace {

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), w.data(), n);
    return w;
}

// lowerW lower-cases a path as the launcher does, so pins match.
std::wstring lowerW(std::wstring s) {
    for (auto& c : s) c = wchar_t(std::towlower(c));
    return s;
}

double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// process gives a window's exe (lower case) and package family.
void process(HWND h, PinWindow& m) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return;
    wchar_t buf[MAX_PATH];
    DWORD n = MAX_PATH;
    if (QueryFullProcessImageNameW(p, 0, buf, &n)) m.exe = narrow(lowerW(std::wstring(buf, n)));
    UINT32 len = 0;
    if (GetPackageFamilyName(p, &len, nullptr) == ERROR_INSUFFICIENT_BUFFER && len) {
        std::wstring fam(len, L'\0');
        if (GetPackageFamilyName(p, &len, fam.data()) == ERROR_SUCCESS) {
            fam.resize(len ? len - 1 : 0);
            m.family = narrow(fam);
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
    std::vector<uintptr_t> order;            // first seen first
    std::map<uintptr_t, std::string> icons;  // each window's icon key
    std::map<std::string, bool> made;        // icon keys tried
    std::unique_lock<std::mutex> lock(mu_);
    while (!stop_) {
        // Titles change without the shell saying (or saying late), so look
        // again every second anyway.
        cv_.wait_for(lock, std::chrono::seconds(1), [this] { return stop_ || dirty_; });
        if (stop_) break;
        dirty_ = false;
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
        for (auto it = icons.begin(); it != icons.end();)
            it = std::find(order.begin(), order.end(), it->first) == order.end() ? icons.erase(it) : std::next(it);

        std::vector<OpenWindow> list;
        for (uintptr_t w : order) {
            HWND h = HWND(w);
            auto f = std::find_if(found.begin(), found.end(), [&](const Found& x) { return x.h == h; });
            OpenWindow o;
            o.id = w;
            o.title = narrow(f->title);
            o.minimised = IsIconic(h);
            wchar_t cls[128] = L"";
            GetClassNameW(h, cls, 128);
            o.match.cls = narrow(cls);
            process(h, o.match);
            // A Store app's frame belongs to ApplicationFrameHost: its app ID
            // says which app it is, for the icon and pins.
            std::wstring aumid = o.match.cls == "ApplicationFrameWindow" ? appID(h) : L"";
            if (!aumid.empty() && o.match.family.empty()) o.match.family = narrow(aumid.substr(0, aumid.find(L'!')));

            // The icon: the window's own, else the Store app's or exe's.
            HICON hi = aumid.empty() ? windowIcon(h) : nullptr;
            char key[64];
            std::snprintf(key, sizeof key, "win-icon:%llx:%llx", (unsigned long long)w, (unsigned long long)uintptr_t(hi));
            std::string k = hi ? key : !aumid.empty() ? "app-id-icon:" + narrow(aumid) : "exe-icon:" + o.match.exe;
            if (!made.count(k)) {
                IconPNGs png = hi ? IconFromHandle(hi, kIconSize)
                               : !aumid.empty() ? IconFromAppID(aumid, kIconSize)
                                                : IconFromShell(widen(o.match.exe), kIconSize);
                made[k] = !png.empty();
                if (!png.empty()) icons_.Put(k, std::move(png.colour));
            }
            if (made[k]) o.icon = k;
            list.push_back(std::move(o));
        }

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
