// The retro theme's extras, after the Weather Channel's 90s WeatherStar
// screens: a drop shadow under all text and a clock for the header. Nothing
// here depends on Windows, so it's tested on any platform.
#pragma once

#include <ctime>
#include <string>
#include <utility>

#include "imgui.h"

namespace ks {

// AddTextShadows gives every glyph in the draw data a copy behind it,
// moved by offset and drawn in colour (its alpha scaled by the glyph's own),
// like the WeatherStar's black-shadowed lettering. Glyphs are the triangles
// drawn from the font texture at anything but its white pixel, which plain
// shapes use; so anti-aliased lines must not be drawn from the texture
// (ImGuiStyle::AntiAliasedLinesUseTex off), or they'd get shadows too.
// Commands are split as needed to keep 16-bit indices in range.
void AddTextShadows(ImDrawData* data, ImTextureID font, ImVec2 whiteUv, ImVec2 offset, ImU32 colour);

// RetroClock is the header's time and date, the WeatherStar way: "4:52:10 PM"
// and "MON 12 OCT".
std::pair<std::string, std::string> RetroClock(const std::tm& t);

}  // namespace ks
