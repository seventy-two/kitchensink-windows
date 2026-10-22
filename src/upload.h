// Sends monitors the local files they ask for (a snapshot's upload), such
// as RuneLite's saved farming data for the farming panel.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "model.h"
#include "net.h"

namespace ks {

class Uploader {
public:
    // Check looks at each monitor's file every few seconds and, when its
    // wanted lines have changed since last sent (or haven't been sent
    // since the app started), sends them.
    void Check(const std::vector<Snapshot>& snapshots, Client& client);

private:
    struct Seen {
        std::string path;
        uint64_t written = 0;  // the file's last-write time
        std::string sent;      // the lines last sent
    };
    std::map<std::string, Seen> seen_;  // by monitor
    uint64_t lastCheck_ = 0;
};

}  // namespace ks
