// The title bar's Windows Terminal icon, drawn rather than loaded: a little
// terminal window, to sit beside the guitar and the mango.
#pragma once

#include "imgui.h"

namespace ks {

// DrawTerminalIcon draws a plain terminal window in the square
// button at min, size across. Hovered, it's in Windows Terminal Preview's
// own colours. Otherwise it's in the muted grey, its parts kept apart as
// light and shade between that grey and bg (the title bar behind it), and
// fainter still when faint (the app isn't running).
void DrawTerminalIcon(ImDrawList* dl, ImVec2 min, float size, bool hovered, ImVec4 muted, ImVec4 bg, bool faint);

}  // namespace ks
