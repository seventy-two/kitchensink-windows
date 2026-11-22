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

// FormatRate writes bytes a second as "0 B/s", "512 B/s", "12.3 KB/s",
// "123 MB/s": three figures at most, in 1024s as Windows counts.
inline std::string FormatRate(double bytes) {
    static const char* units[] = {"B/s", "KB/s", "MB/s", "GB/s"};
    int u = 0;
    while (bytes >= 999.5 && u < 3) bytes /= 1024, u++;
    char buf[32];
    if (u == 0 || bytes >= 99.95) std::snprintf(buf, sizeof buf, "%.0f %s", bytes, units[u]);
    else std::snprintf(buf, sizeof buf, "%.1f %s", bytes, units[u]);
    return buf;
}

// GraphTop is the rate the graph's top stands for: the next 1, 2 or 5
// times a power of ten at or above peak, and at least 10 KB/s so a quiet
// line doesn't fill it with noise.
inline double GraphTop(double peak) {
    double top = std::max(peak, 10.0 * 1024);
    double p = std::pow(10.0, std::floor(std::log10(top)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= top) return m * p;
    return 10 * p;
}

}  // namespace ks
