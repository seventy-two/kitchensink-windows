#include "upload.h"

#include <windows.h>

#include <nlohmann/json.hpp>

namespace ks {

namespace {

// expand fills in a path's %VARS%.
std::wstring expand(const std::string& path) {
    std::wstring in = Widen(path);
    DWORD n = ExpandEnvironmentStringsW(in.c_str(), nullptr, 0);
    if (n == 0) return in;
    std::wstring out(n, L'\0');
    ExpandEnvironmentStringsW(in.c_str(), out.data(), n);
    out.resize(n - 1);
    return out;
}

// readFile reads a whole file, sharing it with the app that writes it.
bool readFile(const std::wstring& path, std::string& out) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart < (64 << 20);
    if (ok) {
        out.resize(size_t(size.QuadPart));
        DWORD got = 0;
        ok = ReadFile(f, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size();
    }
    CloseHandle(f);
    return ok;
}

}  // namespace

void Uploader::Check(const std::vector<Snapshot>& snapshots, Client& client) {
    uint64_t now = GetTickCount64();
    if (now - lastCheck_ < 5000) return;
    lastCheck_ = now;
    for (const auto& s : snapshots) {
        if (s.uploadPath.empty()) continue;
        Seen& seen = seen_[s.monitor];
        // A new file or new lines wanted: look now, whatever the interval.
        if (seen.path != s.uploadPath || seen.prefixes != s.uploadPrefixes) seen = Seen{s.uploadPath, s.uploadPrefixes};
        if (s.uploadEvery > 0 && seen.looked != 0 && now - seen.looked < uint64_t(s.uploadEvery) * 1000) continue;
        seen.looked = now;
        std::wstring path = expand(s.uploadPath);
        WIN32_FILE_ATTRIBUTE_DATA attrs;
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs)) continue;
        uint64_t written = (uint64_t(attrs.ftLastWriteTime.dwHighDateTime) << 32) | attrs.ftLastWriteTime.dwLowDateTime;
        if (written == seen.written) continue;
        std::string text;
        if (!readFile(path, text)) {  // being written: try again next time
            seen.looked = 0;
            continue;
        }
        seen.written = written;
        std::string lines = KeepLines(text, s.uploadPrefixes);
        if (lines.empty() || lines == seen.sent) continue;
        client.Act(s.monitor, nlohmann::json{{"type", "upload"}, {"text", lines}}.dump());
        seen.sent = std::move(lines);
    }
}

}  // namespace ks
