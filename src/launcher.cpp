#include "launcher.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwctype>

#include "images.h"
#include "winicon.h"

using Microsoft::WRL::ComPtr;

namespace ks {

namespace {

double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring lower(std::wstring s) {
    for (auto& c : s) c = wchar_t(std::towlower(c));
    return s;
}

// imagePath is a process's exe, lower case, or empty if it can't be read.
std::wstring imagePath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH * 2];
    DWORD n = DWORD(std::size(buf));
    std::wstring path = QueryFullProcessImageNameW(h, 0, buf, &n) ? lower(std::wstring(buf, n)) : L"";
    CloseHandle(h);
    return path;
}

// iconPNG encodes the exe's icon at size pixels as a PNG, for the image
// cache, or returns empty.
std::string iconPNG(const std::wstring& exe, int size) {
    HICON icon = nullptr;
    if (FAILED(SHDefExtractIconW(exe.c_str(), 0, 0, &icon, nullptr, MAKELONG(size, 0))) || !icon) return "";
    std::string out;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateBitmapFromHICON(icon, &bitmap)) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->WriteSource(bitmap.Get(), nullptr)) && SUCCEEDED(frame->Commit()) &&
        SUCCEEDED(encoder->Commit())) {
        HGLOBAL mem = nullptr;
        if (SUCCEEDED(GetHGlobalFromStream(stream.Get(), &mem))) {
            STATSTG st{};
            stream->Stat(&st, STATFLAG_NONAME);
            if (void* p = GlobalLock(mem)) {
                out.assign(static_cast<const char*>(p), size_t(st.cbSize.QuadPart));
                GlobalUnlock(mem);
            }
        }
    }
    DestroyIcon(icon);
    return out;
}

struct Find {
    std::wstring path;
    HWND found = nullptr;
};

// mainWindow finds the exe's visible top-level window, if it has one.
HWND mainWindow(const std::wstring& exe) {
    Find f{lower(exe)};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* f = reinterpret_cast<Find*>(lp);
            if (!IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            if (imagePath(pid) != f->path) return TRUE;
            f->found = h;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&f));
    return f.found;
}

}  // namespace

void Launcher::Set(const std::vector<std::string>& apps) {
    all_.clear();
    const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
    for (const auto& a : apps) {
        if (a.empty()) continue;
        LaunchApp app;
        std::wstring w = widen(a);
        if (w.size() > 4 && lower(w.substr(w.size() - 4)) == L".exe") {
            app.path = w;
            size_t slash = w.find_last_of(L"\\/");
            app.name = narrow(w.substr(slash == std::wstring::npos ? 0 : slash + 1, w.size() - 4 - (slash + 1)));
        } else {
            app.path = std::wstring(local ? local : L"") + L"\\Programs\\" + w + L"\\" + w + L".exe";
            app.name = a;
        }
        all_.push_back(app);
    }
    std::sort(all_.begin(), all_.end(), [](const LaunchApp& a, const LaunchApp& b) {
        return lower(widen(a.name)) < lower(widen(b.name));
    });
    checked_ = -1e9;
}

const std::vector<LaunchApp>& Launcher::Installed() {
    double t = seconds();
    if (t - checked_ > 5) {
        checked_ = t;
        installed_.clear();
        for (const auto& a : all_)
            if (GetFileAttributesW(a.path.c_str()) != INVALID_FILE_ATTRIBUTES) installed_.push_back(a);
    }
    return installed_;
}

std::string Launcher::Icon(const LaunchApp& app, ImageCache& images, int size) {
    auto it = icons_.find(app.path);
    if (it == icons_.end()) it = icons_.emplace(app.path, iconPNG(app.path, size)).first;
    if (it->second.empty()) return "";
    std::string key = "app-icon:" + narrow(app.path);
    // Kept, as the cache may drop it among the thumbnails.
    if (!images.Has(key)) images.Provide(key, it->second);
    return key;
}

bool Launcher::Running(const LaunchApp& app) {
    double t = seconds();
    if (t - runningChecked_ > 1) {
        runningChecked_ = t;
        running_.clear();
        // Only processes named like one of the apps are looked at closely.
        std::vector<std::wstring> names;
        for (const auto& a : installed_) names.push_back(lower(a.path.substr(a.path.find_last_of(L"\\/") + 1)));
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W pe{sizeof(pe)};
            for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
                if (std::find(names.begin(), names.end(), lower(pe.szExeFile)) != names.end())
                    running_.push_back(imagePath(pe.th32ProcessID));
            CloseHandle(snap);
        }
    }
    return std::find(running_.begin(), running_.end(), lower(app.path)) != running_.end();
}

void Launcher::Open(const LaunchApp& app) const {
    if (HWND h = mainWindow(app.path)) {
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        SetForegroundWindow(h);
        return;
    }
    // Not showing: start it. Those that keep to one copy show the one
    // already running (e.g. from the tray) instead.
    std::wstring dir = app.path.substr(0, app.path.find_last_of(L"\\/"));
    ShellExecuteW(nullptr, L"open", app.path.c_str(), nullptr, dir.c_str(), SW_SHOWNORMAL);
}

void Launcher::SetPins(const std::vector<std::string>& specs) {
    pins_.clear();
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    std::wstring pinned = std::wstring(appdata ? appdata : L"") +
                          L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\";
    wchar_t windir[MAX_PATH] = L"C:\\Windows";
    GetWindowsDirectoryW(windir, MAX_PATH);
    for (const auto& spec : specs) {
        PinnedApp p;
        p.spec = spec;
        p.kind = PinKindOf(spec);
        std::wstring iconFrom;
        if (p.kind == PinKind::Explorer) {
            p.name = "File Explorer";
            p.open = std::wstring(windir) + L"\\explorer.exe";
            p.args = L"::{20D04FE0-3AEA-1069-A2D8-08002B30309D}";  // This PC
            iconFrom = p.open;
        } else if (p.kind == PinKind::AppID) {
            p.target = spec;
            p.open = L"shell:AppsFolder\\" + widen(spec);
            ComPtr<IShellItem> item;
            if (FAILED(SHCreateItemFromParsingName(p.open.c_str(), nullptr, IID_PPV_ARGS(&item)))) continue;
            PWSTR name = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &name)) && name) {
                p.name = narrow(name);
                CoTaskMemFree(name);
            }
            iconFrom = widen(spec);
        } else {
            std::wstring w = widen(spec);
            std::wstring path = w.find(L'\\') != std::wstring::npos || w.find(L':') != std::wstring::npos ? w : pinned + w;
            if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            size_t slash = path.find_last_of(L"\\/");
            std::wstring file = path.substr(slash + 1);
            if (file.size() > 4) file.resize(file.size() - 4);
            p.name = narrow(file);
            p.open = path;
            ComPtr<IShellLinkW> link;
            ComPtr<IPersistFile> persist;
            wchar_t target[MAX_PATH * 2] = L"";
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
                SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Load(path.c_str(), STGM_READ)))
                link->GetPath(target, int(std::size(target)), nullptr, 0);
            p.target = narrow(lower(target));
            iconFrom = target[0] ? std::wstring(target) : path;
        }
        // The pinned shortcut's icon would carry the shortcut arrow; the
        // target's doesn't.
        p.icon = "pin-icon:" + spec;
        if (!pinIcons_.Holds(p.icon)) {
            IconPNGs png = p.kind == PinKind::AppID ? IconFromAppID(iconFrom, 64) : IconFromShell(iconFrom, 64);
            pinIcons_.Put(p.icon, png.colour);
            pinIcons_.Put(p.icon + ":grey", png.grey);
        }
        pins_.push_back(std::move(p));
    }
}

const Texture* Launcher::PinIcon(const PinnedApp& pin, bool grey, ImageCache& images) {
    return pinIcons_.Get(grey ? pin.icon + ":grey" : pin.icon, images);
}

void Launcher::Start(const PinnedApp& pin) const {
    // The dashboard had the click, so it may hand the front on.
    AllowSetForegroundWindow(ASFW_ANY);
    std::wstring dir;
    if (pin.kind == PinKind::Shortcut && !pin.target.empty()) {
        std::wstring t = widen(pin.target);
        dir = t.substr(0, t.find_last_of(L"\\/"));
    }
    ShellExecuteW(nullptr, L"open", pin.open.c_str(), pin.args.empty() ? nullptr : pin.args.c_str(),
                  dir.empty() ? nullptr : dir.c_str(), SW_SHOWNORMAL);
}

}  // namespace ks
