// The games menu's portable part: the installed games as the stores
// record them, read from their files (see games.h for the Windows side).
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ks {

// Local is a game from no store: found in one of the game folders, or a
// shortcut named in config.json.
enum class GameStore { Steam, GOG, Epic, EA, Ubisoft, Xbox, Local };

// GameStoreName is the store's name as the menu shows it.
const char* GameStoreName(GameStore s);

struct Game {
    std::string name;
    GameStore store = GameStore::Steam;
    std::string id;      // the store's ID for it (Steam app ID, GOG game ID, ...)
    std::string dir;     // where it's installed, lower case, no trailing slash
    std::string family;  // its package family, for Xbox games
    std::string open;    // what starts it: a URL, an exe or shell:AppsFolder\<app ID>
    std::string args;    // with an exe, its arguments
    std::string workDir; // with an exe, where it runs
    std::string icon;    // a picture or icon file, or an exe, to take its icon from
    int64_t lastPlayed = 0;  // Unix seconds, 0 if the store doesn't say
};

// VdfNode is a parsed Valve KeyValues (VDF) text file, such as Steam's
// libraryfolders.vdf or an appmanifest_*.acf. Keys are lower-cased.
struct VdfNode {
    std::map<std::string, std::string> values;
    std::map<std::string, VdfNode> children;
    // Value returns a value, or fallback.
    std::string Value(const std::string& key, const std::string& fallback = "") const;
    // Child returns a child, or an empty node.
    const VdfNode& Child(const std::string& key) const;
};
VdfNode ParseVdf(const std::string& text);

// SteamLibraries lists the library folders in libraryfolders.vdf.
std::vector<std::string> SteamLibraries(const std::string& libraryfolders);
// SteamGame reads an appmanifest_*.acf from the library at library. It's
// false for what isn't a game to play (redistributables, Proton, SDKs,
// servers) and for a game not yet fully installed.
bool SteamGame(const std::string& acf, const std::string& library, Game& out);
// EpicGame reads one of the Epic launcher's .item manifests. It's false for
// an unfinished install and for what isn't a game (the Unreal Engine, say).
bool EpicGame(const std::string& item, Game& out);
// EAOffer picks the EA app's offer ID from the names in a game's
// InstallData folder ("base-Origin.SFT.50.0001514"), or empty.
std::string EAOffer(const std::vector<std::string>& entries);
// SortGames puts the games in A-Z order (ignoring case and a leading
// "The "), dropping a second copy of a game installed from two stores.
void SortGames(std::vector<Game>& games);
// FolderGameName makes a game's name from its folder's, tidying a
// download's dots, build number, version and group away
// ("Space.Warlord.Baby.Trading.Simulator.Build.22021334").
std::string FolderGameName(const std::string& folder);
// ExeFile is an exe found in a game folder: its path from the folder, its
// size and how many folders down it is.
struct ExeFile {
    std::string path;
    uint64_t size = 0;
    int depth = 0;
};
// PickGameExe picks the folder's game from its exes, or -1 if it has
// none: helpers (uninstallers, crash reporters, anti-cheat, installers,
// launchers, editors, trials) and small exes are passed over; one named
// like the folder ("FortuneMill" in "Fortune Mill", "AOW4" in "Age of
// Wonders 4") wins, then the one nearest the top, then the biggest.
int PickGameExe(const std::string& folder, const std::vector<ExeFile>& exes);
// GameOwns says whether a window's exe (lower case) is one of the game's.
bool GameOwns(const Game& g, const std::string& exe, const std::string& family);

}  // namespace ks
