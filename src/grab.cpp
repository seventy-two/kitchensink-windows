#include "grab.h"

#include <nlohmann/json.hpp>

namespace ks {

namespace {

std::string field(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : "";
}

// state turns one of watchgrab's statuses into a GrabState.
GrabState state(const std::string& status, const std::string& message) {
    GrabState s;
    s.message = message;
    if (status == "grabbed") s.kind = GrabState::Grabbed;
    else if (status == "have") s.kind = GrabState::Have;
    else if (status == "none") s.kind = GrabState::NoRelease;
    else if (status == "noimdb") s.kind = GrabState::NoIMDb;
    else if (status == "nospace" || status == "error") s.kind = GrabState::Failed;
    else s.kind = GrabState::None;
    return s;
}

}  // namespace

std::unordered_map<std::string, GrabState> ParseWatchgrabState(const std::string& json) {
    std::unordered_map<std::string, GrabState> out;
    auto j = nlohmann::json::parse(json, nullptr, false);
    if (!j.is_object()) return out;
    auto films = j.find("films");
    if (films == j.end() || !films->is_object()) return out;
    for (auto it = films->begin(); it != films->end(); ++it) {
        if (!it->is_object()) continue;
        std::string status = field(*it, "status"), message;
        if (status == "grabbed") message = "Added " + field(*it, "torrent");
        else if (status == "have") message = "Already in the library or qBittorrent";
        else if (status == "none") message = "No acceptable release when last looked for";
        else if (status == "noimdb") message = "Letterboxd has no IMDb ID for it, so it can't be searched for";
        GrabState s = state(status, message);
        if (s.kind != GrabState::None) out[it.key()] = s;
    }
    return out;
}

GrabState ParseGrabResult(const std::string& output) {
    // The last line that's JSON: anything before it is noise.
    size_t end = output.size();
    while (end > 0) {
        size_t start = output.rfind('\n', end - 1);
        start = start == std::string::npos ? 0 : start + 1;
        std::string line = output.substr(start, end - start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_object()) {
            GrabState s = state(field(j, "status"), field(j, "message"));
            if (s.kind == GrabState::None) s = {GrabState::Failed, "watchgrab said: " + line};
            return s;
        }
        if (start == 0) break;
        end = start - 1;
    }
    return {GrabState::Failed, output.empty() ? "watchgrab printed nothing" : "watchgrab said: " + output};
}

GrabView ViewGrab(const GrabState& s) {
    GrabView v;
    switch (s.kind) {
    case GrabState::None:
        v.tooltip = "Download with watchgrab";
        break;
    case GrabState::Running:
        v = {GrabView::Busy, Tone::Muted, "Looking for a release…", false};
        break;
    case GrabState::Grabbed:
        v = {GrabView::Done, Tone::Good, s.message, false};
        break;
    case GrabState::Have:
        v = {GrabView::Done, Tone::Muted, s.message, false};
        break;
    case GrabState::NoRelease:
        v = {GrabView::Download, Tone::Warn, s.message + ". Click to look again.", true};
        break;
    case GrabState::NoIMDb:
        v = {GrabView::Warning, Tone::Muted, s.message, false};
        break;
    case GrabState::Failed:
        v = {GrabView::Warning, Tone::Bad, s.message + ". Click to try again.", true};
        break;
    }
    return v;
}

}  // namespace ks
