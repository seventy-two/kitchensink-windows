// The games menu's Windows side: where each store keeps its records of
// installed games, their icons, and starting them.
#include "games.h"

#include <windows.h>
#include <shellapi.h>

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

std::string iconKey(const Game& g) { return "game-icon:" + std::string(GameStoreName(g.store)) + ":" + g.id + ":" + g.dir; }

}  // namespace

Games::~Games() {
    if (thread_.joinable()) thread_.join();
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
