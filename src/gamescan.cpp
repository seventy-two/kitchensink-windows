// The games menu's portable part: reading the stores' records of what's
// installed. The Windows side (games.cpp) finds the files and registry keys.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>

#include <nlohmann/json.hpp>

#include "gamescan.h"

namespace ks {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// vdfToken reads the next quoted string, unquoted word or brace from text
// at i, skipping space and // comments. It's false at the end.
bool vdfToken(const std::string& text, size_t& i, std::string& tok, bool& quoted) {
    for (;;) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) i++;
        if (i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') i++;
            continue;
        }
        break;
    }
    if (i >= text.size()) return false;
    tok.clear();
    quoted = false;
    if (text[i] == '{' || text[i] == '}') {
        tok = text[i++];
        return true;
    }
    if (text[i] == '"') {
        quoted = true;
        for (i++; i < text.size() && text[i] != '"'; i++) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                char n = text[++i];
                tok += n == 'n' ? '\n' : n == 't' ? '\t' : n;
            } else {
                tok += text[i];
            }
        }
        i++;  // the closing quote
        return true;
    }
    while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i])) && text[i] != '{' && text[i] != '}' &&
           text[i] != '"')
        tok += text[i++];
    return true;
}

void vdfInto(const std::string& text, size_t& i, VdfNode& node) {
    std::string key, tok;
    bool quoted = false;
    while (vdfToken(text, i, key, quoted)) {
        if (key == "}" && !quoted) return;
        if (!vdfToken(text, i, tok, quoted)) return;
        if (tok == "{" && !quoted) {
            vdfInto(text, i, node.children[lower(key)]);
        } else {
            node.values[lower(key)] = tok;
        }
    }
}

// cleanName drops the trademark signs some stores put in names.
std::string cleanName(std::string s) {
    for (const char* mark : {"\xE2\x84\xA2", "\xC2\xAE", "\xC2\xA9"}) {
        for (size_t at; (at = s.find(mark)) != std::string::npos;) s.erase(at, std::char_traits<char>::length(mark));
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    return s;
}

// sortKey is the name as it sorts: lower case, without a leading "the ".
std::string sortKey(const std::string& name) {
    std::string k = lower(name);
    if (k.rfind("the ", 0) == 0) k = k.substr(4);
    return k;
}

std::string trimSlash(std::string s) {
    while (!s.empty() && (s.back() == '\\' || s.back() == '/')) s.pop_back();
    return s;
}

}  // namespace

const char* GameStoreName(GameStore s) {
    switch (s) {
        case GameStore::Steam: return "Steam";
        case GameStore::GOG: return "GOG";
        case GameStore::Epic: return "Epic";
        case GameStore::EA: return "EA app";
        case GameStore::Ubisoft: return "Ubisoft Connect";
        case GameStore::Xbox: return "Xbox";
    }
    return "";
}

std::string VdfNode::Value(const std::string& key, const std::string& fallback) const {
    auto it = values.find(lower(key));
    return it == values.end() ? fallback : it->second;
}

const VdfNode& VdfNode::Child(const std::string& key) const {
    static const VdfNode empty;
    auto it = children.find(lower(key));
    return it == children.end() ? empty : it->second;
}

VdfNode ParseVdf(const std::string& text) {
    VdfNode root;
    size_t i = 0;
    vdfInto(text, i, root);
    return root;
}

std::vector<std::string> SteamLibraries(const std::string& libraryfolders) {
    std::vector<std::string> out;
    VdfNode root = ParseVdf(libraryfolders);
    for (const auto& [key, lib] : root.Child("libraryfolders").children) {
        std::string path = trimSlash(lib.Value("path"));
        if (!path.empty() && std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
    }
    return out;
}

bool SteamGame(const std::string& acf, const std::string& library, Game& out) {
    VdfNode root = ParseVdf(acf);
    const VdfNode& st = root.Child("AppState");
    std::string id = st.Value("appid"), name = st.Value("name"), dir = st.Value("installdir");
    if (id.empty() || name.empty() || dir.empty()) return false;
    // StateFlags 4 is fully installed (more bits join it while it updates).
    if (!(std::atoi(st.Value("StateFlags", "0").c_str()) & 4)) return false;
    std::string n = lower(name);
    for (const char* tool : {"redistributable", "proton", "steam linux runtime", "steamvr", "sdk", "dedicated server",
                             "steamworks common"})
        if (contains(n, tool)) return false;
    out = Game{};
    out.name = name;
    out.store = GameStore::Steam;
    out.id = id;
    out.dir = lower(trimSlash(library) + "\\steamapps\\common\\" + dir);
    out.open = "steam://rungameid/" + id;
    out.lastPlayed = std::atoll(st.Value("LastPlayed", "0").c_str());
    return true;
}

bool EpicGame(const std::string& item, Game& out) {
    auto j = nlohmann::json::parse(item, nullptr, false);
    if (!j.is_object()) return false;
    auto str = [&](const char* key) { return j.contains(key) && j[key].is_string() ? j[key].get<std::string>() : ""; };
    if (j.value("bIsIncompleteInstall", false)) return false;
    std::string app = str("AppName"), main = str("MainGameAppName");
    if (app.empty() || (!main.empty() && main != app)) return false;  // DLC
    if (j.contains("AppCategories") && j["AppCategories"].is_array()) {
        bool game = false;
        for (const auto& c : j["AppCategories"])
            if (c.is_string() && c.get<std::string>() == "games") game = true;
        if (!game) return false;
    }
    std::string dir = trimSlash(str("InstallLocation"));
    if (dir.empty() || str("DisplayName").empty()) return false;
    out = Game{};
    out.name = str("DisplayName");
    out.store = GameStore::Epic;
    out.id = app;
    out.dir = lower(dir);
    out.open = "com.epicgames.launcher://apps/" + str("CatalogNamespace") + "%3A" + str("CatalogItemId") + "%3A" + app +
               "?action=launch&silent=true";
    std::string exe = str("LaunchExecutable");
    if (!exe.empty()) out.icon = dir + "\\" + exe;
    return true;
}

std::string EAOffer(const std::vector<std::string>& entries) {
    for (const auto& e : entries)
        if (e.rfind("base-", 0) == 0 && e.size() > 5) return e.substr(5);
    return "";
}

void SortGames(std::vector<Game>& games) {
    for (auto& g : games) g.name = cleanName(g.name);
    std::stable_sort(games.begin(), games.end(),
                     [](const Game& a, const Game& b) { return sortKey(a.name) < sortKey(b.name); });
    std::set<std::string> seen;
    games.erase(std::remove_if(games.begin(), games.end(),
                               [&](const Game& g) { return g.name.empty() || !seen.insert(sortKey(g.name)).second; }),
                games.end());
}

bool GameOwns(const Game& g, const std::string& exe, const std::string& family) {
    if (!g.family.empty()) return family == g.family;
    return !g.dir.empty() && exe.size() > g.dir.size() && exe.compare(0, g.dir.size(), g.dir) == 0 &&
           (exe[g.dir.size()] == '\\' || exe[g.dir.size()] == '/');
}

}  // namespace ks
