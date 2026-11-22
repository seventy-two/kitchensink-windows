// Network speeds for the side column's Network card: turning the
// adapters' byte counters into rates, the last couple of minutes of them
// for the graph, and how they're written. No platform headers, so it can
// be tested anywhere.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

namespace ks {

// NetRate is a second's traffic, in bytes a second.
struct NetRate {
    double down = 0, up = 0;
};

// NetHistory takes the byte counters (received and sent, summed over the
// adapters) at times in seconds, and keeps the rates between readings.
class NetHistory {
public:
    // kKeep is how many rates the graph shows: two minutes, read every second.
    static constexpr size_t kKeep = 120;

    // Push adds a reading. The first only sets the baseline; so does one
    // whose counters went backwards (an adapter came or went), or that
    // came too soon to say much.
    void Push(double t, uint64_t in, uint64_t out) {
        if (has_ && in >= in_ && out >= out_ && t - t_ >= 0.25) {
            double dt = t - t_;
            rates_.push_back({double(in - in_) / dt, double(out - out_) / dt});
            while (rates_.size() > kKeep) rates_.pop_front();
        }
        if (!has_ || t - t_ >= 0.25 || in < in_ || out < out_) t_ = t, in_ = in, out_ = out;
        has_ = true;
    }
    const std::deque<NetRate>& Rates() const { return rates_; }
    NetRate Latest() const { return rates_.empty() ? NetRate{} : rates_.back(); }
    // Peak is the highest of either direction in the history.
    double Peak() const {
        double m = 0;
        for (const auto& r : rates_) m = std::max({m, r.down, r.up});
        return m;
    }

private:
    bool has_ = false;
    double t_ = 0;
    uint64_t in_ = 0, out_ = 0;
    std::deque<NetRate> rates_;
};

// kMB is a megabyte, in 1024s as Windows counts.
constexpr double kMB = 1024.0 * 1024.0;

// FormatRate writes bytes a second in MB/s, always, so the line stays
// still while little's moving: "0 MB/s" for nothing much, "0.1 MB/s",
// "12.3 MB/s", "123 MB/s".
inline std::string FormatRate(double bytes) {
    double mb = bytes / kMB;
    char buf[32];
    if (mb < 0.05) std::snprintf(buf, sizeof buf, "0 MB/s");
    else if (mb < 99.95) std::snprintf(buf, sizeof buf, "%.1f MB/s", mb);
    else std::snprintf(buf, sizeof buf, "%.0f MB/s", mb);
    return buf;
}

// GraphTop is the rate the graph's top stands for: the next 1, 2 or 5
// times a power of ten MB/s at or above peak, and at least 1 MB/s so a
// quiet line stays flat rather than filling it with noise.
inline double GraphTop(double peak) {
    double top = std::max(peak / kMB, 1.0);
    double p = std::pow(10.0, std::floor(std::log10(top)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= top * (1 - 1e-9)) return m * p * kMB;
    return 10 * p * kMB;
}

}  // namespace ks
