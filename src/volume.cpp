#include "volume.h"

#include <windows.h>
#include <shellapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ks {

namespace {

// PKEY_Device_FriendlyName, spelled out: its header wants more of the
// SDK than the lean build includes.
const PROPERTYKEY kFriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

std::string narrow(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// Listener hears of changes from elsewhere (the volume keys, the flyout,
// another default device), on Windows' threads: it only raises a flag and
// wakes the dashboard, which looks again on its own thread.
class Listener : public IAudioEndpointVolumeCallback, public IMMNotificationClient {
public:
    explicit Listener(std::function<void()> wake) : wake_(std::move(wake)) {}
    std::atomic<bool> deviceChanged{false};

    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IAudioEndpointVolumeCallback))
            *out = static_cast<IAudioEndpointVolumeCallback*>(this);
        else if (iid == __uuidof(IMMNotificationClient))
            *out = static_cast<IMMNotificationClient*>(this);
        else
            return *out = nullptr, E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA) override {
        if (wake_) wake_();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) {
            deviceChanged = true;
            if (wake_) wake_();
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::function<void()> wake_;
    std::atomic<ULONG> refs_{1};
};

// appName is what the mixer calls a process: its exe's description
// ("Firefox", "Spotify"), else the exe's name without ".exe".
std::string appName(DWORD pid) {
    std::wstring path(MAX_PATH * 2, L'\0');
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return {};
    DWORD n = DWORD(path.size());
    bool ok = QueryFullProcessImageNameW(h, 0, path.data(), &n);
    CloseHandle(h);
    if (!ok) return {};
    path.resize(n);
    DWORD dummy = 0, size = GetFileVersionInfoSizeW(path.c_str(), &dummy);
    if (size) {
        std::vector<char> info(size);
        struct Lang {
            WORD lang, page;
        }* langs = nullptr;
        UINT len = 0;
        if (GetFileVersionInfoW(path.c_str(), 0, size, info.data()) &&
            VerQueryValueW(info.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&langs), &len) && len >= sizeof(Lang)) {
            wchar_t key[64];
            swprintf(key, 64, L"\\StringFileInfo\\%04x%04x\\FileDescription", langs[0].lang, langs[0].page);
            wchar_t* desc = nullptr;
            if (VerQueryValueW(info.data(), key, reinterpret_cast<void**>(&desc), &len) && len > 1 && desc[0])
                return narrow(desc);
        }
    }
    size_t slash = path.find_last_of(L"\\/");
    std::wstring file = path.substr(slash + 1);
    if (file.size() > 4 && _wcsicmp(file.c_str() + file.size() - 4, L".exe") == 0) file.resize(file.size() - 4);
    return narrow(file.c_str());
}

}  // namespace

struct Volume::Impl {
    ComPtr<IMMDevice> device;
    // The mixer's apps, each with the volume controls of its sessions.
    std::vector<std::pair<std::string, std::vector<ComPtr<ISimpleAudioVolume>>>> apps;
    DWORD appsRead = 0;
    bool appsEver = false;

    void readApps() {
        DWORD now = GetTickCount();
        if (appsEver && now - appsRead < 500) return;
        appsRead = now, appsEver = true;
        apps.clear();
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        if (!device || FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) ||
            FAILED(manager->GetSessionEnumerator(&sessions)))
            return;
        int n = 0;
        sessions->GetCount(&n);
        std::map<std::string, size_t> at;
        for (int i = 0; i < n; i++) {
            ComPtr<IAudioSessionControl> c;
            ComPtr<IAudioSessionControl2> c2;
            ComPtr<ISimpleAudioVolume> v;
            if (FAILED(sessions->GetSession(i, &c)) || FAILED(c.As(&c2)) || FAILED(c.As(&v))) continue;
            AudioSessionState state;
            if (SUCCEEDED(c->GetState(&state)) && state == AudioSessionStateExpired) continue;
            std::string name;
            if (c2->IsSystemSoundsSession() == S_OK) {
                name = "System sounds";
            } else {
                DWORD pid = 0;
                c2->GetProcessId(&pid);
                name = appName(pid);
            }
            if (name.empty()) continue;
            auto it = at.find(name);
            if (it == at.end()) {
                at[name] = apps.size();
                apps.push_back({name, {v}});
            } else {
                apps[it->second].second.push_back(v);
            }
        }
    }

    ComPtr<IMMDeviceEnumerator> devices;
    ComPtr<IAudioEndpointVolume> endpoint;
    std::string name;
    Listener* listener = nullptr;
    DWORD tried = 0;  // when the speakers were last looked for, in ticks

    // open finds the default speakers, at most every few seconds while
    // there are none.
    void open() {
        if (!devices) return;
        DWORD now = GetTickCount();
        if (endpoint && !listener->deviceChanged) return;
        if (!endpoint && tried && now - tried < 3000 && !listener->deviceChanged) return;
        tried = now;
        listener->deviceChanged = false;
        close();
        appsEver = false;
        if (FAILED(devices->GetDefaultAudioEndpoint(eRender, eConsole, &device))) return;
        if (FAILED(device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpoint))) return;
        endpoint->RegisterControlChangeNotify(listener);
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR) name = narrow(v.pwszVal);
            PropVariantClear(&v);
        }
    }
    void close() {
        if (endpoint) endpoint->UnregisterControlChangeNotify(listener);
        endpoint.Reset();
        device.Reset();
        apps.clear();
        name.clear();
    }
};

Volume::Volume(std::function<void()> wake) : impl_(std::make_unique<Impl>()) {
    impl_->listener = new Listener(std::move(wake));
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&impl_->devices))))
        impl_->devices->RegisterEndpointNotificationCallback(impl_->listener);
}

Volume::~Volume() {
    impl_->close();
    if (impl_->devices) impl_->devices->UnregisterEndpointNotificationCallback(impl_->listener);
    impl_->listener->Release();
}

bool Volume::Available() {
    impl_->open();
    return impl_->endpoint != nullptr;
}

int Volume::Value() {
    impl_->open();
    float v = 0;
    if (impl_->endpoint) impl_->endpoint->GetMasterVolumeLevelScalar(&v);
    return int(std::lround(v * 100));
}

bool Volume::Muted() {
    impl_->open();
    BOOL m = FALSE;
    if (impl_->endpoint) impl_->endpoint->GetMute(&m);
    return m != FALSE;
}

std::string Volume::Device() {
    impl_->open();
    return impl_->name;
}

void Volume::Set(int value) {
    impl_->open();
    if (!impl_->endpoint) return;
    impl_->endpoint->SetMasterVolumeLevelScalar(float(value) / 100.0f, nullptr);
    // As the flyout does: turning it up unmutes.
    if (value > 0) impl_->endpoint->SetMute(FALSE, nullptr);
}

void Volume::SetMuted(bool muted) {
    impl_->open();
    if (impl_->endpoint) impl_->endpoint->SetMute(muted, nullptr);
}

std::vector<AppVolume> Volume::Apps() {
    impl_->open();
    impl_->readApps();
    std::vector<AppVolume> out;
    for (const auto& [name, controls] : impl_->apps) {
        AppVolume a;
        a.name = name;
        float v = 0;
        BOOL m = FALSE;
        controls[0]->GetMasterVolume(&v);
        controls[0]->GetMute(&m);
        a.value = int(std::lround(v * 100));
        a.muted = m != FALSE;
        out.push_back(a);
    }
    std::sort(out.begin(), out.end(), [](const AppVolume& a, const AppVolume& b) {
        if ((a.name == "System sounds") != (b.name == "System sounds")) return b.name == "System sounds";
        return _stricmp(a.name.c_str(), b.name.c_str()) < 0;
    });
    return out;
}

void Volume::SetApp(const std::string& name, int value) {
    for (auto& [n, controls] : impl_->apps)
        if (n == name)
            for (auto& c : controls) c->SetMasterVolume(float(value) / 100.0f, nullptr);
}

void Volume::SetAppMuted(const std::string& name, bool muted) {
    for (auto& [n, controls] : impl_->apps)
        if (n == name)
            for (auto& c : controls) c->SetMute(muted, nullptr);
}

void Volume::OpenDevices() {
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecuteW(nullptr, L"open", L"control.exe", L"mmsys.cpl,,0", nullptr, SW_SHOWNORMAL);
}

void Volume::OpenSettings() {
    AllowSetForegroundWindow(ASFW_ANY);
    ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace ks
