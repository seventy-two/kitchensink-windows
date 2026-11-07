// Colours, fonts and ImGui style: light and dark variants that follow the
// Windows app theme, OLED care's true black, and a retro theme after the
// Weather Channel's 90s WeatherStar screens.
#pragma once

#include "imgui.h"
#include "model.h"

namespace ks {

struct Palette {
    ImVec4 bg, card, border, text, muted, good, bad, warn, accent, hover, rowAlt, frame;
    // The app's name and Recent's heading, and blocks' titles: text and
    // muted, except in the retro theme's yellow.
    ImVec4 heading, blockTitle;
    // The retro theme's extras: the background's gradient, top to bottom,
    // and panels' bevel, lit along the top and left.
    ImVec4 bgTop, bgBottom, bevelLight, bevelDark;
    bool retro = false;
};

enum class Theme { Light, Dark, Oled, Retro };

struct Fonts {
    ImFont* body = nullptr;
    ImFont* caption = nullptr;  // not "small": windows.h defines that as char
    ImFont* heading = nullptr;
    ImFont* big = nullptr;
    ImFont* title = nullptr;
    ImFont* chrome = nullptr;  // window button glyphs, at the size Windows uses
};

// Icon code points, merged into the body and small fonts: on Windows in
// Segoe Fluent Icons / Segoe MDL2 Assets; on Linux the same icons in
// Symbols Nerd Font (its Codicons and Font Awesome sets).
#ifdef _WIN32
namespace icon {
inline constexpr const char* Refresh = "\xEE\xA2\x95";   // U+E895 Sync
inline constexpr const char* Settings = "\xEE\x9C\x80";  // U+E700 GlobalNavButton, three lines
inline constexpr const char* Warning = "\xEE\x9E\xBA";   // U+E7BA
inline constexpr const char* Minimise = "\xEE\xA4\xA1";  // U+E921
inline constexpr const char* Maximise = "\xEE\xA4\xA2";  // U+E922
inline constexpr const char* Restore = "\xEE\xA4\xA3";   // U+E923
inline constexpr const char* Close = "\xEE\xA2\xBB";     // U+E8BB
inline constexpr const char* Previous = "\xEE\xA2\x92";  // U+E892
inline constexpr const char* Next = "\xEE\xA2\x93";      // U+E893
inline constexpr const char* Play = "\xEE\x9D\xA8";      // U+E768
inline constexpr const char* Pause = "\xEE\x9D\xA9";     // U+E769
inline constexpr const char* ChevronDown = "\xEE\x9C\x8D";  // U+E70D
inline constexpr const char* ChevronUp = "\xEE\x9C\x8E";    // U+E70E
inline constexpr const char* View = "\xEE\xA2\x90";         // U+E890, an eye
inline constexpr const char* Up = "\xEE\x9D\x8A";           // U+E74A
inline constexpr const char* Down = "\xEE\x9D\x8B";         // U+E74B
inline constexpr const char* Paste = "\xEE\x9D\xBF";        // U+E77F
inline constexpr const char* Download = "\xEE\xA2\x96";     // U+E896
inline constexpr const char* CheckMark = "\xEE\x9C\xBE";    // U+E73E
inline constexpr const char* Lightbulb = "\xEE\xAA\x80";   // U+EA80
inline constexpr const char* Moon = "\xEE\x9C\x88";        // U+E708 QuietHours
inline constexpr const char* Monitor = "\xEE\x9F\xB4";     // U+E7F4 TVMonitor
inline constexpr const char* Dismiss = "\xEE\x9C\x91";      // U+E711 Cancel, a cross
}  // namespace icon
#else
namespace icon {
inline constexpr const char* Refresh = "\xEE\xAC\xB7";  // U+EB37 cod-refresh
inline constexpr const char* Settings = "\xEE\xAE\x94";  // U+EB94 cod-menu
inline constexpr const char* Warning = "\xEE\xA9\xAC";  // U+EA6C cod-warning
inline constexpr const char* Minimise = "\xEE\xAA\xBA";  // U+EABA cod-chrome_minimize
inline constexpr const char* Maximise = "\xEE\xAA\xB9";  // U+EAB9 cod-chrome_maximize
inline constexpr const char* Restore = "\xEE\xAA\xBB";  // U+EABB cod-chrome_restore
inline constexpr const char* Close = "\xEE\xAA\xB8";  // U+EAB8 cod-chrome_close
inline constexpr const char* Previous = "\xEF\x81\x88";  // U+F048 fa-step_backward
inline constexpr const char* Next = "\xEF\x81\x91";  // U+F051 fa-step_forward
inline constexpr const char* Play = "\xEF\x81\x8B";  // U+F04B fa-play
inline constexpr const char* Pause = "\xEF\x81\x8C";  // U+F04C fa-pause
inline constexpr const char* ChevronDown = "\xEE\xAA\xB4";  // U+EAB4 cod-chevron_down
inline constexpr const char* ChevronUp = "\xEE\xAA\xB7";  // U+EAB7 cod-chevron_up
inline constexpr const char* View = "\xEE\xA9\xB0";  // U+EA70 cod-eye
inline constexpr const char* Up = "\xEE\xAA\xA1";  // U+EAA1 cod-arrow_up
inline constexpr const char* Down = "\xEE\xAA\x9A";  // U+EA9A cod-arrow_down
inline constexpr const char* Paste = "\xEE\xAB\x80";  // U+EAC0 cod-clippy
inline constexpr const char* Download = "\xEF\x80\x99";  // U+F019 fa-download
inline constexpr const char* CheckMark = "\xEE\xAA\xB2";  // U+EAB2 cod-check
inline constexpr const char* Lightbulb = "\xEE\xA9\xA1";  // U+EA61 cod-lightbulb
inline constexpr const char* Moon = "\xEF\x86\x86";  // U+F186 fa-moon
inline constexpr const char* Monitor = "\xEF\x84\x88";  // U+F108 fa-desktop
inline constexpr const char* Dismiss = "\xEE\xA9\xB6";  // U+EA76 cod-close
}  // namespace icon
#endif

// SystemUsesDarkTheme reads the Windows "app mode" setting, or on Linux the
// desktop's colour scheme (via SDL).
bool SystemUsesDarkTheme();

// ApplyTheme sets the palette and ImGui style for a theme and DPI scale.
// Compact spacing is tighter, for small windows. OLED is a true-black dark
// theme with dimmer text and fainter borders.
void ApplyTheme(Theme theme, float scale, bool compact = false);
const Palette& Colours();

// LoadFonts rebuilds the font atlas for a DPI scale: Segoe UI, or bold
// Arial for the retro theme. The renderer's font texture must be recreated
// afterwards.
Fonts LoadFonts(float scale, bool retro = false);

ImVec4 ToneColour(Tone t);

}  // namespace ks
