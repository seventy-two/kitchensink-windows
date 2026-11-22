#include "volume.h"

#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include <atomic>
#include <cmath>

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

}  // namespace

struct Volume::Impl {
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
        ComPtr<IMMDevice> device;
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

}  // namespace ks
