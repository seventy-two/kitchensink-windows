#include "images.h"

#ifdef _WIN32
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#else
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#include "stb/stb_image.h"
#endif

#include <algorithm>

#include "net.h"

namespace ks {

namespace {

#ifndef _WIN32
// The few OpenGL calls textures need, looked up through SDL from whichever
// GL the window's context uses (EGL on Wayland), so nothing links libGL.
struct Gl {
    void(APIENTRY* GenTextures)(GLsizei, GLuint*);
    void(APIENTRY* DeleteTextures)(GLsizei, const GLuint*);
    void(APIENTRY* BindTexture)(GLenum, GLuint);
    void(APIENTRY* TexParameteri)(GLenum, GLenum, GLint);
    void(APIENTRY* PixelStorei)(GLenum, GLint);
    void(APIENTRY* TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
};

const Gl& gl() {
    static const Gl fns = [] {
        Gl f;
        f.GenTextures = (decltype(f.GenTextures))SDL_GL_GetProcAddress("glGenTextures");
        f.DeleteTextures = (decltype(f.DeleteTextures))SDL_GL_GetProcAddress("glDeleteTextures");
        f.BindTexture = (decltype(f.BindTexture))SDL_GL_GetProcAddress("glBindTexture");
        f.TexParameteri = (decltype(f.TexParameteri))SDL_GL_GetProcAddress("glTexParameteri");
        f.PixelStorei = (decltype(f.PixelStorei))SDL_GL_GetProcAddress("glPixelStorei");
        f.TexImage2D = (decltype(f.TexImage2D))SDL_GL_GetProcAddress("glTexImage2D");
        return f;
    }();
    return fns;
}
#endif

void release(Texture& t) {
#ifdef _WIN32
    if (t.srv) t.srv->Release();
#else
    if (t.srv) {
        GLuint name = GLuint(t.srv);
        gl().DeleteTextures(1, &name);
    }
#endif
    t.srv = {};
}

// Thumbnails are scaled down to at most this wide when decoded.
constexpr unsigned kMaxWidth = 480;
constexpr size_t kMaxEntries = 200;

#ifdef _WIN32
bool decode(IWICImagingFactory* wic, const std::string& bytes, UINT& w, UINT& h, std::vector<unsigned char>& rgba) {
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(wic->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory((BYTE*)bytes.data(), DWORD(bytes.size()))) ||
        FAILED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&w, &h)) || w == 0 || h == 0)
        return false;

    ComPtr<IWICBitmapSource> source = frame;
    if (w > kMaxWidth) {
        ComPtr<IWICBitmapScaler> scaler;
        UINT sh = std::max<UINT>(1, h * kMaxWidth / w);
        if (FAILED(wic->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(frame.Get(), kMaxWidth, sh, WICBitmapInterpolationModeFant)))
            return false;
        source = scaler;
        w = kMaxWidth;
        h = sh;
    }
    ComPtr<IWICFormatConverter> conv;
    if (FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(source.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)))
        return false;
    rgba.resize(size_t(w) * h * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, UINT(rgba.size()), rgba.data()));
}
#else
// decode reads a PNG, JPEG, GIF (first frame) or BMP into RGBA, scaled
// down to at most kMaxWidth across with a box filter.
bool decode(const std::string& bytes, unsigned& w, unsigned& h, std::vector<unsigned char>& rgba) {
    int iw = 0, ih = 0, n = 0;
    unsigned char* px = stbi_load_from_memory((const stbi_uc*)bytes.data(), int(bytes.size()), &iw, &ih, &n, 4);
    if (!px || iw <= 0 || ih <= 0) {
        if (px) stbi_image_free(px);
        return false;
    }
    if (unsigned(iw) <= kMaxWidth) {
        w = iw;
        h = ih;
        rgba.assign(px, px + size_t(iw) * ih * 4);
        stbi_image_free(px);
        return true;
    }
    w = kMaxWidth;
    h = std::max<unsigned>(1, unsigned(ih) * kMaxWidth / unsigned(iw));
    rgba.assign(size_t(w) * h * 4, 0);
    for (unsigned y = 0; y < h; y++) {
        unsigned y0 = y * ih / h, y1 = std::max(y0 + 1, (y + 1) * ih / h);
        for (unsigned x = 0; x < w; x++) {
            unsigned x0 = x * iw / w, x1 = std::max(x0 + 1, (x + 1) * iw / w);
            unsigned sum[4] = {0, 0, 0, 0}, count = 0;
            for (unsigned sy = y0; sy < y1; sy++)
                for (unsigned sx = x0; sx < x1; sx++, count++)
                    for (int c = 0; c < 4; c++) sum[c] += px[(size_t(sy) * iw + sx) * 4 + c];
            for (int c = 0; c < 4; c++) rgba[(size_t(y) * w + x) * 4 + c] = (unsigned char)(sum[c] / count);
        }
    }
    stbi_image_free(px);
    return true;
}

#endif

}  // namespace

ImageCache::ImageCache(GpuDevice device, std::function<void()> wake)
    : device_(device), wake_(std::move(wake)), thread_(&ImageCache::run, this) {}

ImageCache::~ImageCache() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    thread_.join();
    for (auto& [url, e] : entries_) release(e.tex);
}

const Texture* ImageCache::Get(const std::string& url) {
    if (url.empty()) return nullptr;
    auto it = entries_.find(url);
    if (it == entries_.end()) {
        evict();
        entries_[url].lastUsed = ++tick_;
        {
            std::lock_guard<std::mutex> lock(mu_);
            queue_.push_back(url);
        }
        cv_.notify_one();
        return nullptr;
    }
    it->second.lastUsed = ++tick_;
    return it->second.tex.srv ? &it->second.tex : nullptr;
}

void ImageCache::evict() {
    while (entries_.size() >= kMaxEntries) {
        auto oldest = std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
            // Never evict one still loading; its result would be lost.
            if (a.second.done != b.second.done) return a.second.done;
            return a.second.lastUsed < b.second.lastUsed;
        });
        if (!oldest->second.done) return;
        release(oldest->second.tex);
        entries_.erase(oldest);
    }
}

void ImageCache::Provide(const std::string& key, std::string bytes) {
    std::lock_guard<std::mutex> lock(mu_);
    provided_[key] = std::move(bytes);
}

void ImageCache::Upload() {
    std::vector<Decoded> ready;
    {
        std::lock_guard<std::mutex> lock(mu_);
        ready.swap(decoded_);
    }
    for (auto& d : ready) {
        auto it = entries_.find(d.url);
        if (it == entries_.end()) continue;
        it->second.done = true;
        if (d.rgba.empty()) continue;

#ifdef _WIN32
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = d.width;
        desc.Height = d.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{d.rgba.data(), UINT(d.width * 4), 0};
        ComPtr<ID3D11Texture2D> tex;
        if (FAILED(device_->CreateTexture2D(&desc, &data, &tex))) continue;
        ID3D11ShaderResourceView* srv = nullptr;
        if (FAILED(device_->CreateShaderResourceView(tex.Get(), nullptr, &srv))) continue;
        it->second.tex = {srv, d.width, d.height};
#else
        GLuint name = 0;
        gl().GenTextures(1, &name);
        if (!name) continue;
        gl().BindTexture(GL_TEXTURE_2D, name);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl().TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl().PixelStorei(GL_UNPACK_ALIGNMENT, 4);
        gl().PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        gl().TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, d.width, d.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, d.rgba.data());
        it->second.tex.srv = name;
        it->second.tex.width = d.width;
        it->second.tex.height = d.height;
#endif
    }
}

void ImageCache::run() {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
    {
#ifdef _WIN32
        ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
#endif
        Http http;
        for (;;) {
            std::string url;
            HttpResult r;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [&] { return stop_ || !queue_.empty(); });
                if (stop_) break;
                url = std::move(queue_.front());
                queue_.pop_front();
                // Bytes supplied by Provide stand in for a download.
                if (auto it = provided_.find(url); it != provided_.end()) {
                    r.status = 200;
                    r.body = std::move(it->second);
                    provided_.erase(it);
                }
            }
            Decoded d{url};
            if (r.status == 0 && url.rfind("http", 0) == 0) r = http.Request("GET", url, "", 8 << 20);
#ifdef _WIN32
            UINT w = 0, h = 0;
            if (wic && r.ok() && decode(wic.Get(), r.body, w, h, d.rgba)) {
#else
            unsigned w = 0, h = 0;
            if (r.ok() && decode(r.body, w, h, d.rgba)) {
#endif
                d.width = int(w);
                d.height = int(h);
            } else {
                d.rgba.clear();
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                decoded_.push_back(std::move(d));
            }
            wake_();
        }
    }
#ifdef _WIN32
    CoUninitialize();
#endif
}

}  // namespace ks
