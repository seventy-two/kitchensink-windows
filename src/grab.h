// What the watchlist's download buttons show: each film's standing with
// watchgrab (a separate app that fetches films), from its state file and
// from grabs run from here. Portable; grabber.cpp runs the app.
#pragma once

#include <string>
#include <unordered_map>

#include "model.h"

namespace ks {

struct GrabState {
    enum Kind { None, Running, Grabbed, Have, NoRelease, NoIMDb, Failed };
    Kind kind = None;
    std::string message;
};

// ParseWatchgrabState reads watchgrab's state.json: each film it has dealt
// with, by Letterboxd slug. Films it hasn't are left out.
std::unordered_map<std::string, GrabState> ParseWatchgrabState(const std::string& json);

// ParseGrabResult reads what `watchgrab -grab` printed: one line of JSON,
// {"slug","status","message"}.
GrabState ParseGrabResult(const std::string& output);

// GrabView is how a film's button looks.
struct GrabView {
    enum Icon { Download, Busy, Done, Warning };
    Icon icon = Download;
    Tone tone = Tone::None;
    std::string tooltip;
    bool clickable = true;
};

GrabView ViewGrab(const GrabState& s);

}  // namespace ks
