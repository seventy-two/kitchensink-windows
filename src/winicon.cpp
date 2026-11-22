#include "winicon.h"

#include <appmodel.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <functional>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace ks {

namespace {

// png encodes top-down BGRA pixels (straight alpha).
std::string png(const std::vector<BYTE>& bgra, int w, int h) {
    std::string out;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppBGRA, w * 4, UINT(bgra.size()),
                                              const_cast<BYTE*>(bgra.data()), &bitmap)) &&
        SUCCEEDED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
        SUCCEEDED(frame->SetSize(w, h)) && SUCCEEDED(frame->SetPixelFormat(&format)) &&
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
    return out;
}

// both makes the colour PNG and a grey one, the grey keeping a little of
// the icon's light and shade.
IconPNGs both(std::vector<BYTE>& bgra, int w, int h) {
    IconPNGs out;
    out.colour = png(bgra, w, h);
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        int lum = (bgra[i] * 11 + bgra[i + 1] * 59 + bgra[i + 2] * 30) / 100;
        // Squeezed towards the middle so dark and light icons look alike.
        BYTE g = BYTE(std::clamp(128 + (lum - 128) * 6 / 10, 0, 255));
        bgra[i] = bgra[i + 1] = bgra[i + 2] = g;
    }
    out.grey = png(bgra, w, h);
    return out;
}

// pixels reads a 32-bit bitmap top-down. Without an alpha channel (all
// zero), mask (if given) says what's see-through.
bool pixels(HBITMAP colour, HBITMAP mask, int& w, int& h, std::vector<BYTE>& out, bool premultiplied) {
    BITMAP bm{};
    if (!GetObjectW(colour, sizeof bm, &bm)) return false;
    w = bm.bmWidth, h = std::abs(bm.bmHeight);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return false;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    out.assign(size_t(w) * h * 4, 0);
    HDC dc = GetDC(nullptr);
    bool ok = GetDIBits(dc, colour, 0, h, out.data(), &bi, DIB_RGB_COLORS) == h;
    bool alpha = false;
    for (size_t i = 3; i < out.size(); i += 4)
        if (out[i]) alpha = true;
    if (ok && !alpha) {
        std::vector<BYTE> m(size_t(w) * h * 4, 0);
        bool haveMask = mask && GetDIBits(dc, mask, 0, h, m.data(), &bi, DIB_RGB_COLORS) == h;
        for (size_t i = 0; i < out.size(); i += 4) out[i + 3] = haveMask && m[i] ? 0 : 255;
        premultiplied = false;
    }
    ReleaseDC(nullptr, dc);
    if (ok && premultiplied)
        for (size_t i = 0; i < out.size(); i += 4) {
            BYTE a = out[i + 3];
            if (a && a < 255)
                for (int k = 0; k < 3; k++) out[i + k] = BYTE(std::min(255, out[i + k] * 255 / a));
        }
    return ok;
}

// pngFile decodes a PNG file to top-down BGRA.
bool pngFile(const std::wstring& path, int& w, int& h, std::vector<BYTE>& out) {
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmapDecoder> dec;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapSource> bgra;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) ||
        FAILED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
        FAILED(dec->GetFrame(0, &frame)) || FAILED(WICConvertBitmapSource(GUID_WICPixelFormat32bppBGRA, frame.Get(), &bgra)))
        return false;
    UINT uw = 0, uh = 0;
    bgra->GetSize(&uw, &uh);
    if (!uw || !uh || uw > 1024 || uh > 1024) return false;
    w = int(uw), h = int(uh);
    out.assign(size_t(w) * h * 4, 0);
    return SUCCEEDED(bgra->CopyPixels(nullptr, w * 4, UINT(out.size()), out.data()));
}

// packageDirs lists where a package family's packages are installed.
std::vector<std::wstring> packageDirs(const std::wstring& family) {
    std::vector<std::wstring> out;
    UINT32 count = 0, len = 0;
    if (FindPackagesByPackageFamily(family.c_str(), PACKAGE_FILTER_HEAD, &count, nullptr, &len, nullptr, nullptr) !=
            ERROR_INSUFFICIENT_BUFFER || !count)
        return out;
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buf(len);
    if (FindPackagesByPackageFamily(family.c_str(), PACKAGE_FILTER_HEAD, &count, names.data(), &len, buf.data(), nullptr) !=
        ERROR_SUCCESS)
        return out;
    for (UINT32 i = 0; i < count; i++) {
        UINT32 n = 0;
        if (GetPackagePathByFullName(names[i], &n, nullptr) != ERROR_INSUFFICIENT_BUFFER) continue;
        std::wstring path(n, L'\0');
        if (GetPackagePathByFullName(names[i], &n, path.data()) != ERROR_SUCCESS) continue;
        path.resize(wcslen(path.c_str()));
        out.push_back(path);
    }
    return out;
}

// unplatedLogo finds the package's small unplated logo nearest size (the
// smallest at or above it, else the largest), looking a few folders deep.
std::wstring unplatedLogo(const std::wstring& dir, int size) {
    std::wstring best;
    int bestSize = 0;
    int seen = 0;
    std::function<void(const std::wstring&, int)> walk = [&](const std::wstring& d, int depth) {
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW((d + L"\\*").c_str(), &fd);
        if (f == INVALID_HANDLE_VALUE) return;
        do {
            if (++seen > 5000) break;
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (depth < 3) walk(d + L"\\" + name, depth + 1);
                continue;
            }
            std::wstring low = name;
            for (auto& c : low) c = wchar_t(std::towlower(c));
            if (low.find(L"square44x44logo") == std::wstring::npos || low.find(L"altform-unplated") == std::wstring::npos ||
                low.size() < 4 || low.compare(low.size() - 4, 4, L".png") != 0)
                continue;
            size_t at = low.find(L"targetsize-");
            if (at == std::wstring::npos) continue;
            int n = _wtoi(low.c_str() + at + 11);
            if (n <= 0) continue;
            bool better = best.empty() || (bestSize < size ? n > bestSize : (n >= size && n < bestSize));
            if (better) best = d + L"\\" + name, bestSize = n;
        } while (FindNextFileW(f, &fd));
        FindClose(f);
    };
    walk(dir, 0);
    return best;
}

}  // namespace

IconPNGs IconFromAppID(const std::wstring& appID, int size) {
    std::wstring family = appID.substr(0, appID.find(L'!'));
    for (const auto& dir : packageDirs(family)) {
        std::wstring logo = unplatedLogo(dir, size);
        int w = 0, h = 0;
        std::vector<BYTE> px;
        if (!logo.empty() && pngFile(logo, w, h, px)) return both(px, w, h);
    }
    return IconFromShell(L"shell:AppsFolder\\" + appID, size);
}

IconPNGs IconFromHandle(HICON icon, int size) {
    if (!icon) return {};
    // Drawn at the size wanted, so Windows picks the best of its images.
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = size;
    bi.bmiHeader.biHeight = -size;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib) {
        DeleteDC(dc);
        return {};
    }
    HGDIOBJ old = SelectObject(dc, dib);
    bool drawn = DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    SelectObject(dc, old);
    DeleteDC(dc);
    IconPNGs out;
    if (drawn) {
        std::vector<BYTE> px(static_cast<BYTE*>(bits), static_cast<BYTE*>(bits) + size_t(size) * size * 4);
        bool alpha = false;
        for (size_t i = 3; i < px.size(); i += 4)
            if (px[i]) alpha = true;
        if (alpha) {
            // DrawIconEx leaves premultiplied pixels.
            for (size_t i = 0; i < px.size(); i += 4) {
                BYTE a = px[i + 3];
                if (a && a < 255)
                    for (int k = 0; k < 3; k++) px[i + k] = BYTE(std::min(255, px[i + k] * 255 / a));
            }
            out = both(px, size, size);
        } else {
            // An old icon without alpha: read its own bitmaps and mask.
            ICONINFO ii{};
            if (GetIconInfo(icon, &ii)) {
                int w = 0, h = 0;
                if (ii.hbmColor && pixels(ii.hbmColor, ii.hbmMask, w, h, px, false)) out = both(px, w, h);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
            }
        }
    }
    DeleteObject(dib);
    return out;
}

IconPNGs IconFromShell(const std::wstring& name, int size) {
    ComPtr<IShellItemImageFactory> factory;
    if (FAILED(SHCreateItemFromParsingName(name.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return {};
    HBITMAP bmp = nullptr;
    if (FAILED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) || !bmp) return {};
    int w = 0, h = 0;
    std::vector<BYTE> px;
    IconPNGs out;
    // The shell's images come premultiplied.
    if (pixels(bmp, nullptr, w, h, px, true)) out = both(px, w, h);
    DeleteObject(bmp);
    return out;
}

}  // namespace ks
