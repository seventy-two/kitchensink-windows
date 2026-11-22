// Icons as PNGs for the image cache: from an icon handle (a window's or a
// tray icon's) or from the shell (an exe, a Store app, a shortcut's
// target). Windows only.
#pragma once

#include <windows.h>

#include <string>

namespace ks {

// IconPNGs is an icon in colour, and in grey for buttons drawn muted until
// hovered.
struct IconPNGs {
    std::string colour, grey;
    bool empty() const { return colour.empty(); }
};

// IconFromHandle draws an icon handle at size pixels.
IconPNGs IconFromHandle(HICON icon, int size);
// IconFromShell asks the shell for the icon of a path or parsing name,
// e.g. "shell:AppsFolder\<app ID>". Needs COM on the calling thread.
IconPNGs IconFromShell(const std::wstring& name, int size);
// IconFromAppID is a Store app's icon as the taskbar shows it: the
// package's unplated logo, nearest size at or above size, else the
// shell's (which sits on a coloured tile).
IconPNGs IconFromAppID(const std::wstring& appID, int size);

}  // namespace ks
