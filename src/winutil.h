// Small Windows helpers shared by the launcher, the open windows, the tray
// and the volume mixer. (UTF-8 conversion is net.h's Widen and Narrow.)
#pragma once

#include <windows.h>

#include <cwctype>
#include <iterator>
#include <string>

namespace ks {

// LowerPath lower-cases a path, so exes compare as Windows does: pins are
// matched to windows by it.
inline std::wstring LowerPath(std::wstring s) {
    for (auto& c : s) c = wchar_t(std::towlower(c));
    return s;
}

// ExePath is a process's exe, or empty if it can't be read.
inline std::wstring ExePath(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return L"";
    wchar_t buf[MAX_PATH * 2];
    DWORD n = DWORD(std::size(buf));
    std::wstring path = QueryFullProcessImageNameW(h, 0, buf, &n) ? std::wstring(buf, n) : L"";
    CloseHandle(h);
    return path;
}

}  // namespace ks
