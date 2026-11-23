// The games menu in the title bar: the games installed from Steam, GOG,
// Epic, the EA app, Ubisoft Connect and the Xbox app, found where each
// store records them (as GeForce Experience and Playnite do), and started
// through their store.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gamescan.h"
#include "iconstore.h"

namespace ks {

class ImageCache;

class Games {
public:
    ~Games();
    // SetWake gives what to call when a scan has finished.
    void SetWake(std::function<void()> wake) { wake_ = std::move(wake); }
    // SetSources takes config.json's "gameFolders" (folders of games from
    // no store, one game to a folder inside) and "gameShortcuts" (each a
    // shortcut's file name among the taskbar's pinned ones, or a full path
    // to a .lnk or exe), looking again if they've changed.
    // names are config.json's "gameNames": a name for a game, by the name
    // it was found as.
    void SetSources(const std::vector<std::string>& folders, const std::vector<std::string>& shortcuts,
                    const std::map<std::string, std::string>& names);
    // Scan looks for the installed games again on a thread of its own,
    // unless it looked within the last minute (or is looking now).
    void Scan(bool force = false);
    // List is the games found so far.
    std::vector<Game> List();
    size_t Count();
    // Icon is the game's icon, or null.
    const Texture* Icon(const Game& g, ImageCache& images);
    // Start starts the game through its store.
    void Start(const Game& g) const;

private:
    void run();
    std::mutex mu_;
    std::vector<Game> games_;
    std::vector<std::string> folders_, shortcuts_;
    std::map<std::string, std::string> names_;
    std::thread thread_;
    std::atomic<bool> busy_{false};
    double scanned_ = -1e9;
    IconStore icons_;
    std::function<void()> wake_;
};

}  // namespace ks
