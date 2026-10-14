#include "grabber.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace ks {

namespace {

std::wstring env(const wchar_t* name) {
    const wchar_t* v = _wgetenv(name);
    return v ? v : L"";
}

std::wstring exePath() { return env(L"LOCALAPPDATA") + L"\\Programs\\watchgrab\\watchgrab.exe"; }
std::wstring statePath() { return env(L"APPDATA") + L"\\watchgrab\\state.json"; }

std::wstring widen(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

double seconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// run starts the exe with args, hidden, and returns what it printed.
std::string run(const std::wstring& exe, const std::wstring& args) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readEnd = nullptr, writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &sa, 0)) return "";
    SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writeEnd;
    si.hStdError = writeEnd;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + exe + L"\" " + args;
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(writeEnd);
    std::string out;
    if (ok) {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(readEnd, buf, sizeof(buf), &n, nullptr) && n > 0) out.append(buf, n);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    } else {
        out = "couldn't start watchgrab (error " + std::to_string(GetLastError()) + ")";
    }
    CloseHandle(readEnd);
    return out;
}

}  // namespace

struct Grabber::Shared {
    std::function<void()> wake;
    std::mutex mu;
    std::unordered_map<std::string, GrabState> runs;  // grabs from here, by slug

    // Installed, checked at most once a minute.
    bool installed = false;
    double installedAt = -1e9;
    // The state file, read again when it changes (looked at every 3s).
    std::unordered_map<std::string, GrabState> state;
    FILETIME stateTime{};
    double stateAt = -1e9;
};

Grabber::Grabber(std::function<void()> wake) : shared_(std::make_shared<Shared>()) { shared_->wake = std::move(wake); }

bool Grabber::Has(const std::string& app) {
    if (app != "watchgrab") return false;
    std::lock_guard<std::mutex> lock(shared_->mu);
    if (seconds() - shared_->installedAt > 60) {
        shared_->installedAt = seconds();
        shared_->installed = GetFileAttributesW(exePath().c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    return shared_->installed;
}

GrabState Grabber::Get(const std::string& slug) {
    Shared& s = *shared_;
    std::lock_guard<std::mutex> lock(s.mu);
    if (seconds() - s.stateAt > 3) {
        s.stateAt = seconds();
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        std::wstring path = statePath();
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) &&
            CompareFileTime(&fa.ftLastWriteTime, &s.stateTime) != 0) {
            HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, 0, nullptr);
            if (f != INVALID_HANDLE_VALUE) {
                std::string body;
                char buf[65536];
                DWORD n = 0;
                while (ReadFile(f, buf, sizeof(buf), &n, nullptr) && n > 0) body.append(buf, n);
                CloseHandle(f);
                auto parsed = ParseWatchgrabState(body);
                // A file caught mid-write doesn't parse; try again next time.
                if (!parsed.empty() || body.find("\"films\"") != std::string::npos) {
                    s.state = std::move(parsed);
                    s.stateTime = fa.ftLastWriteTime;
                }
            }
        }
    }
    if (auto it = s.runs.find(slug); it != s.runs.end()) return it->second;
    if (auto it = s.state.find(slug); it != s.state.end()) return it->second;
    return {};
}

void Grabber::Start(const std::string& slug) {
    // Letterboxd slugs are letters, digits and dashes; anything else from
    // the server isn't passed to a command line.
    if (slug.empty() || slug.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") != std::string::npos) return;
    auto shared = shared_;
    {
        std::lock_guard<std::mutex> lock(shared->mu);
        auto it = shared->runs.find(slug);
        if (it != shared->runs.end() && it->second.kind == GrabState::Running) return;
        shared->runs[slug] = {GrabState::Running, ""};
    }
    std::thread([shared, slug] {
        GrabState result = ParseGrabResult(run(exePath(), L"-grab \"" + widen(slug) + L"\""));
        {
            std::lock_guard<std::mutex> lock(shared->mu);
            shared->runs[slug] = result;
            shared->stateAt = -1e9;  // watchgrab has just written its state
        }
        if (shared->wake) shared->wake();
    }).detach();
}

}  // namespace ks
