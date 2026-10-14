// Runs watchgrab (installed beside the dashboard) for the watchlist's
// download buttons, and follows its state file.
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "grab.h"

namespace ks {

class Grabber {
public:
    // wake is called from a background thread when a grab finishes.
    explicit Grabber(std::function<void()> wake);

    // Has says whether app is installed here; only "watchgrab" is known.
    bool Has(const std::string& app);
    // Get is a film's standing: a grab run from here, else watchgrab's own
    // state.
    GrabState Get(const std::string& slug);
    // Start runs `watchgrab -grab slug` in the background.
    void Start(const std::string& slug);

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};

}  // namespace ks
