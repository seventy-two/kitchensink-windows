// The games menu's Windows side: where each store keeps its records of
// installed games, their icons, and starting them.
#include "games.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wrl/client.h>

#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.Storage.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "net.h"
#include "winicon.h"

namespace ks {

namespace {

double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

std::string readFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string lowerUtf8(const std::wstring& s) {
    std::wstring l = s;
    if (!l.empty()) CharLowerBuffW(l.data(), DWORD(l.size()));
    while (!l.empty() && (l.back() == L'\\' || l.back() == L'/')) l.pop_back();
    return Narrow(l);
}

// regString reads a string value, or empty.
std::wstring regString(HKEY root, const std::wstring& key, const wchar_t* value, REGSAM view = 0) {
    DWORD size = 0;
    DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | (view == KEY_WOW64_64KEY ? RRF_SUBKEY_WOW6464KEY : view == KEY_WOW64_32KEY ? RRF_SUBKEY_WOW6432KEY : 0);
    if (RegGetValueW(root, key.c_str(), value, flags, nullptr, nullptr, &size) != ERROR_SUCCESS || size < 2) return L"";
    std::wstring out(size / sizeof(wchar_t), L'\0');
    if (RegGetValueW(root, key.c_str(), value, flags, nullptr, out.data(), &size) != ERROR_SUCCESS) return L"";
    out.resize(wcsnlen(out.c_str(), out.size()));
    return out;
}

// subkeys lists a key's subkeys' names.
std::vector<std::wstring> subkeys(HKEY root, const std::wstring& key, REGSAM view = 0) {
    std::vector<std::wstring> out;
    HKEY h = nullptr;
    if (RegOpenKeyExW(root, key.c_str(), 0, KEY_READ | view, &h) != ERROR_SUCCESS) return out;
    wchar_t name[256];
    for (DWORD i = 0;; i++) {
        DWORD len = DWORD(std::size(name));
        if (RegEnumKeyExW(h, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.push_back(name);
    }
    RegCloseKey(h);
    return out;
}

// files lists the names matching pattern (e.g. dir\*.acf); dirs for folders.
std::vector<std::wstring> files(const std::wstring& pattern, bool dirs = false) {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        bool isDir = fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY;
        if (isDir == dirs && wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) out.push_back(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

// biggestExe is the largest exe at the top of dir, which is the game
// itself more often than not.
std::wstring biggestExe(const std::wstring& dir) {
    std::wstring best;
    ULONGLONG size = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return best;
    do {
        ULONGLONG s = (ULONGLONG(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        std::wstring n = fd.cFileName;
        CharLowerBuffW(n.data(), DWORD(n.size()));
        bool helper = n.find(L"unins") == 0 || n.find(L"crash") != std::wstring::npos ||
                      n.find(L"anticheat") != std::wstring::npos || n.find(L"redist") != std::wstring::npos;
        if (!helper && s > size) size = s, best = dir + L"\\" + fd.cFileName;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

std::wstring programData() {
    const wchar_t* p = _wgetenv(L"PROGRAMDATA");
    return p ? p : L"C:\\ProgramData";
}

void steam(std::vector<Game>& out) {
    std::wstring root = regString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (root.empty()) return;
    std::replace(root.begin(), root.end(), L'/', L'\\');
    std::vector<std::string> libs = SteamLibraries(readFile(root + L"\\steamapps\\libraryfolders.vdf"));
    if (libs.empty()) libs.push_back(Narrow(root));
    std::wstring cache = root + L"\\appcache\\librarycache\\";
    for (const auto& lib : libs) {
        std::wstring apps = Widen(lib) + L"\\steamapps\\";
        for (const auto& f : files(apps + L"appmanifest_*.acf")) {
            Game g;
            if (!SteamGame(readFile(apps + f), lib, g)) continue;
            // Its small icon: a 32x32 JPEG named by its hash, in its own
            // folder (or <id>_icon.jpg in older Steams).
            std::wstring id = Widen(g.id);
            for (const auto& jpg : files(cache + id + L"\\*.jpg"))
                if (jpg.size() == 44) g.icon = Narrow(cache + id + L"\\" + jpg);
            if (g.icon.empty() && exists(cache + id + L"_icon.jpg")) g.icon = Narrow(cache + id + L"_icon.jpg");
            out.push_back(std::move(g));
        }
    }
}

void gog(std::vector<Game>& out) {
    const std::wstring key = L"SOFTWARE\\WOW6432Node\\GOG.com\\Games";
    for (const auto& sub : subkeys(HKEY_LOCAL_MACHINE, key)) {
        std::wstring k = key + L"\\" + sub;
        auto v = [&](const wchar_t* name) { return regString(HKEY_LOCAL_MACHINE, k, name); };
        std::wstring exe = v(L"exe"), path = v(L"path"), id = v(L"gameID");
        if (exe.empty() || !exists(exe) || !v(L"dependsOn").empty()) continue;  // DLC depends on its game
        if (id.empty()) id = sub;
        Game g;
        g.name = Narrow(v(L"gameName"));
        g.store = GameStore::GOG;
        g.id = Narrow(id);
        g.dir = lowerUtf8(path.empty() ? exe.substr(0, exe.find_last_of(L'\\')) : path);
        g.open = Narrow(exe);
        g.args = Narrow(v(L"launchParam"));
        g.workDir = Narrow(v(L"workingDir"));
        std::wstring ico = path + L"\\goggame-" + id + L".ico";
        g.icon = Narrow(exists(ico) ? ico : exe);
        out.push_back(std::move(g));
    }
}

void epic(std::vector<Game>& out) {
    std::wstring dir = programData() + L"\\Epic\\EpicGamesLauncher\\Data\\Manifests\\";
    for (const auto& f : files(dir + L"*.item")) {
        Game g;
        if (EpicGame(readFile(dir + f), g) && exists(Widen(g.dir))) out.push_back(std::move(g));
    }
}

// eaExe is the game's exe from the EA installer's record of how it's
// launched, passing over the anti-cheat's launcher, else the biggest exe.
std::wstring eaExe(const std::wstring& dir) {
    std::string xml = readFile(dir + L"\\__Installer\\installerdata.xml");
    for (size_t at = 0; (at = xml.find("<filePath>", at)) != std::string::npos;) {
        at += 10;
        size_t end = xml.find("</filePath>", at);
        if (end == std::string::npos) break;
        std::string p = xml.substr(at, end - at);
        size_t close = p.find(']');
        if (close != std::string::npos) p = p.substr(close + 1);
        std::wstring exe = dir + L"\\" + Widen(p);
        std::replace(exe.begin(), exe.end(), L'/', L'\\');
        std::wstring l = exe;
        CharLowerBuffW(l.data(), DWORD(l.size()));
        if (l.find(L"anticheat") == std::wstring::npos && exists(exe)) return exe;
    }
    return biggestExe(dir);
}

void ea(std::vector<Game>& out) {
    std::wstring installData = programData() + L"\\EA Desktop\\InstallData\\";
    std::vector<std::string> seen;
    for (REGSAM view : {REGSAM(KEY_WOW64_64KEY), REGSAM(KEY_WOW64_32KEY)}) {
        const std::wstring key = L"SOFTWARE\\EA Games";
        for (const auto& sub : subkeys(HKEY_LOCAL_MACHINE, key, view)) {
            std::wstring k = key + L"\\" + sub;
            std::wstring dir = regString(HKEY_LOCAL_MACHINE, k, L"Install Dir", view);
            while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
            if (dir.empty() || !exists(dir)) continue;
            std::string l = lowerUtf8(dir);
            if (std::find(seen.begin(), seen.end(), l) != seen.end()) continue;
            seen.push_back(l);
            Game g;
            std::wstring name = regString(HKEY_LOCAL_MACHINE, k, L"DisplayName", view);
            g.name = Narrow(name.empty() ? sub : name);
            g.store = GameStore::EA;
            g.dir = l;
            std::vector<std::string> entries;
            for (const auto& e : files(installData + sub + L"\\*", true)) entries.push_back(Narrow(e));
            g.id = EAOffer(entries);
            std::wstring exe = eaExe(dir);
            g.icon = Narrow(exe);
            if (!g.id.empty()) {
                g.open = "origin2://game/launch?offerIds=" + g.id + "&autoDownload=1";
            } else if (!exe.empty()) {
                g.open = Narrow(exe);
                g.workDir = Narrow(dir);
            } else {
                continue;
            }
            out.push_back(std::move(g));
        }
    }
}

void ubisoft(std::vector<Game>& out) {
    const std::wstring key = L"SOFTWARE\\WOW6432Node\\Ubisoft\\Launcher\\Installs";
    for (const auto& id : subkeys(HKEY_LOCAL_MACHINE, key)) {
        std::wstring dir = regString(HKEY_LOCAL_MACHINE, key + L"\\" + id, L"InstallDir");
        std::replace(dir.begin(), dir.end(), L'/', L'\\');
        while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
        if (dir.empty() || !exists(dir)) continue;
        Game g;
        std::wstring name = regString(HKEY_LOCAL_MACHINE,
                                      L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Uplay Install " + id,
                                      L"DisplayName");
        g.name = Narrow(name.empty() ? dir.substr(dir.find_last_of(L'\\') + 1) : name);
        g.store = GameStore::Ubisoft;
        g.id = Narrow(id);
        g.dir = lowerUtf8(dir);
        g.open = "uplay://launch/" + g.id + "/0";
        g.icon = Narrow(biggestExe(dir));
        out.push_back(std::move(g));
    }
}

// xbox finds the Store packages that are games: those with a
// MicrosoftGame.config (the Game Development Kit's manifest).
void xbox(std::vector<Game>& out) {
    try {
        winrt::Windows::Management::Deployment::PackageManager pm;
        for (const auto& pkg : pm.FindPackagesForUser(L"")) {
            try {
                if (pkg.IsFramework() || pkg.IsResourcePackage() || pkg.IsBundle()) continue;
                std::wstring dir(pkg.InstalledLocation().Path());
                if (!exists(dir + L"\\MicrosoftGame.config")) continue;
                auto entries = pkg.GetAppListEntries();
                if (entries.Size() == 0) continue;
                auto entry = entries.GetAt(0);
                Game g;
                g.name = Narrow(std::wstring(entry.DisplayInfo().DisplayName()));
                g.store = GameStore::Xbox;
                g.id = Narrow(std::wstring(entry.AppUserModelId()));
                g.family = Narrow(std::wstring(pkg.Id().FamilyName()));
                g.dir = lowerUtf8(dir);
                g.open = "shell:AppsFolder\\" + g.id;
                g.icon = g.id;
                out.push_back(std::move(g));
            } catch (...) {
            }
        }
    } catch (...) {
    }
}

// exes lists the exes in dir and below it, down to depth folders,
// passing over folders that hold no game's exe and giving up after a few
// hundred folders.
void exes(const std::wstring& dir, const std::wstring& rel, int depth, int& budget, std::vector<ExeFile>& out) {
    if (--budget < 0) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<std::wstring> subdirs;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..") continue;
        std::wstring l = n;
        CharLowerBuffW(l.data(), DWORD(l.size()));
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            bool skip = l == L"_commonredist" || l == L"__installer" || l == L"redist" || l == L"directx" ||
                        l == L"monobleedingedge" || (l.size() > 5 && l.compare(l.size() - 5, 5, L"_data") == 0);
            if (!skip && depth > 0) subdirs.push_back(n);
        } else if (l.size() > 4 && l.compare(l.size() - 4, 4, L".exe") == 0) {
            out.push_back({Narrow(rel + n), (uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow, 3 - depth});
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    for (const auto& s : subdirs) exes(dir + L"\\" + s, rel + s + L"\\", depth - 1, budget, out);
}

// folderGames finds a game in each folder inside the game folders, other
// than those the stores already account for (in have).
void folderGames(const std::vector<std::string>& folders, const std::vector<Game>& have, std::vector<Game>& out) {
    for (const auto& folder : folders) {
        std::wstring root = Widen(folder);
        while (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        for (const auto& name : files(root + L"\\*", true)) {
            std::wstring dir = root + L"\\" + name;
            DWORD attrs = GetFileAttributesW(dir.c_str());
            if (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM) || name[0] == L'$') continue;
            std::string l = lowerUtf8(dir);
            bool known = false;
            for (const auto& g : have) {
                if (g.dir.empty()) continue;
                if (g.dir == l || g.dir.rfind(l + "\\", 0) == 0 || l.rfind(g.dir + "\\", 0) == 0) known = true;
            }
            if (known) continue;
            std::vector<ExeFile> found;
            int budget = 300;
            exes(dir, L"", 3, budget, found);
            int pick = PickGameExe(Narrow(name), found);
            if (pick < 0) continue;
            std::wstring exe = dir + L"\\" + Widen(found[pick].path);
            Game g;
            g.name = FolderGameName(Narrow(name));
            g.store = GameStore::Local;
            g.id = Narrow(exe);
            g.dir = l;
            g.open = Narrow(exe);
            g.workDir = Narrow(exe.substr(0, exe.find_last_of(L'\\')));
            g.icon = g.open;
            out.push_back(std::move(g));
        }
    }
}

// shortcutGames are the shortcuts named in config.json, as games.
void shortcutGames(const std::vector<std::string>& specs, std::vector<Game>& out) {
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    std::wstring pinned = std::wstring(appdata ? appdata : L"") +
                          L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar\\";
    for (const auto& spec : specs) {
        std::wstring w = Widen(spec);
        std::wstring path = w.find(L'\\') != std::wstring::npos || w.find(L':') != std::wstring::npos ? w : pinned + w;
        if (!exists(path)) continue;
        std::wstring file = path.substr(path.find_last_of(L"\\/") + 1), target = path;
        if (file.size() > 4) file.resize(file.size() - 4);
        std::wstring lowerPath = path;
        CharLowerBuffW(lowerPath.data(), DWORD(lowerPath.size()));
        if (lowerPath.size() > 4 && lowerPath.compare(lowerPath.size() - 4, 4, L".lnk") == 0) {
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            Microsoft::WRL::ComPtr<IPersistFile> persist;
            wchar_t t[MAX_PATH * 2] = L"";
            if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link))) &&
                SUCCEEDED(link.As(&persist)) && SUCCEEDED(persist->Load(path.c_str(), STGM_READ)))
                link->GetPath(t, int(std::size(t)), nullptr, 0);
            if (t[0]) target = t;
        }
        Game g;
        g.name = Narrow(file);
        g.store = GameStore::Local;
        g.id = spec;
        g.dir = lowerUtf8(target.substr(0, target.find_last_of(L"\\/")));
        g.open = Narrow(path);
        g.icon = Narrow(target);
        out.push_back(std::move(g));
    }
}

std::string iconKey(const Game& g) { return "game-icon:" + std::string(GameStoreName(g.store)) + ":" + g.id + ":" + g.dir; }

}  // namespace

Games::~Games() {
    if (thread_.joinable()) thread_.join();
}

void Games::SetSources(const std::vector<std::string>& folders, const std::vector<std::string>& shortcuts,
                       const std::map<std::string, std::string>& names) {
    {
        std::lock_guard<std::mutex> l(mu_);
        if (folders == folders_ && shortcuts == shortcuts_ && names == names_) return;
        folders_ = folders, shortcuts_ = shortcuts, names_ = names;
    }
    scanned_ = -1e9;  // look again at the next Scan
}

void Games::Scan(bool force) {
    if (busy_) return;
    double now = seconds();
    if (!force && now - scanned_ < 60) return;
    scanned_ = now;
    if (thread_.joinable()) thread_.join();
    busy_ = true;
    thread_ = std::thread([this] { run(); });
}

void Games::run() {
    winrt::init_apartment(winrt::apartment_type::single_threaded);  // COM for the shell's icons too
    std::vector<Game> found;
    steam(found);
    gog(found);
    epic(found);
    ea(found);
    ubisoft(found);
    xbox(found);
    std::vector<std::string> folders, shortcuts;
    std::map<std::string, std::string> names;
    {
        std::lock_guard<std::mutex> l(mu_);
        folders = folders_, shortcuts = shortcuts_, names = names_;
    }
    // After the stores' games, so a store's copy of a game wins.
    shortcutGames(shortcuts, found);
    std::vector<Game> local;
    folderGames(folders, found, local);
    for (auto& g : local) found.push_back(std::move(g));
    RenameGames(found, names);
    SortGames(found);
    for (const auto& g : found) {
        std::string key = iconKey(g);
        if (icons_.Holds(key) || g.icon.empty()) continue;
        std::string png;
        if (g.store == GameStore::Xbox) {
            png = IconFromAppID(Widen(g.icon), 64);
        } else if (g.icon.size() > 4 && g.icon.compare(g.icon.size() - 4, 4, ".jpg") == 0) {
            png = readFile(Widen(g.icon));  // the image cache reads JPEGs too
        } else {
            png = IconFromShell(Widen(g.icon), 64);
        }
        icons_.Put(key, std::move(png));
    }
    {
        std::lock_guard<std::mutex> l(mu_);
        games_ = std::move(found);
    }
    winrt::uninit_apartment();
    busy_ = false;
    if (wake_) wake_();
}

std::vector<Game> Games::List() {
    std::lock_guard<std::mutex> l(mu_);
    return games_;
}

size_t Games::Count() {
    std::lock_guard<std::mutex> l(mu_);
    return games_.size();
}

const Texture* Games::Icon(const Game& g, ImageCache& images) { return icons_.Get(iconKey(g), images); }

void Games::Start(const Game& g) const {
    // The dashboard had the click, so it may hand the front on.
    AllowSetForegroundWindow(ASFW_ANY);
    std::wstring open = Widen(g.open), args = Widen(g.args), dir = Widen(g.workDir);
    ShellExecuteW(nullptr, L"open", open.c_str(), args.empty() ? nullptr : args.c_str(), dir.empty() ? nullptr : dir.c_str(),
                  SW_SHOWNORMAL);
}

}  // namespace ks
