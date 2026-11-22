// How fast the network is going, for the side column's Network card:
// the physical adapters' byte counters, read about once a second.
// Windows only; unavailable elsewhere.
#pragma once

#include <string>

#include "netrate.h"

namespace ks {

class NetSpeed {
public:
    // Sample reads the counters if a second has passed since it last did;
    // t is in seconds.
    void Sample(double t);
    // Available says whether any adapter is up.
    bool Available() const { return available_; }
    const NetHistory& History() const { return history_; }
    // Adapters names the adapters counted, e.g. "Ethernet".
    const std::string& Adapters() const { return adapters_; }

private:
    double sampled_ = -1e9;
    bool available_ = false;
    NetHistory history_;
    std::string adapters_;
};

}  // namespace ks
