// Icons kept as PNGs for the image cache, for the taskbar's stand-ins.
#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>

#include "images.h"

namespace ks {

// IconStore keeps icons' PNGs by key and hands them to the image cache
// whenever it hasn't them (the cache drops the least used among the
// thumbnails). Put may be called from any thread; Get on the UI thread.
class IconStore {
public:
    void Put(const std::string& key, std::string png) {
        std::lock_guard<std::mutex> l(mu_);
        png_[key] = std::move(png);
    }
    bool Holds(const std::string& key) {
        std::lock_guard<std::mutex> l(mu_);
        return png_.count(key) > 0;
    }
    // Keep drops every icon but those named.
    void Keep(const std::set<std::string>& keys) {
        std::lock_guard<std::mutex> l(mu_);
        for (auto it = png_.begin(); it != png_.end();) it = keys.count(it->first) ? std::next(it) : png_.erase(it);
    }
    const Texture* Get(const std::string& key, ImageCache& images) {
        if (key.empty()) return nullptr;
        if (!images.Has(key)) {
            std::string png;
            {
                std::lock_guard<std::mutex> l(mu_);
                auto it = png_.find(key);
                if (it == png_.end() || it->second.empty()) return nullptr;
                png = it->second;
            }
            images.Provide(key, std::move(png));
        }
        return images.Get(key);
    }

private:
    std::mutex mu_;
    std::map<std::string, std::string> png_;
};

}  // namespace ks
