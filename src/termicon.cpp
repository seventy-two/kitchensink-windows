#include "termicon.h"

#include <algorithm>
#include <cmath>

namespace ks {

namespace {

// Tone is one colour of the icon: its own, as 0xRRGGBB, for when it's
// hovered, and how strongly the grey shows at rest: 0 is the background, 1
// the grey itself, and past 1 is further from the background still.
struct Tone {
    unsigned rgb;
    float rest;
};

// Windows Terminal Preview's logo: a charcoal slab, lit from the top left,
// under a strip of three tabs in lighter greys; a white prompt folded like
// a ribbon, its lower half in shadow; and the cyan of the logo's "PRE"
// badge on the cursor.
constexpr Tone kSlabLit = {0x505050, 0.50f}, kSlabShade = {0x363636, 0.36f};
constexpr Tone kTabs[3] = {{0xCCCCCC, 1.12f}, {0x999999, 0.88f}, {0x666666, 0.66f}};
constexpr Tone kArmLit = {0xF4F4F4, 1.32f}, kArmLitEnd = {0xDADADA, 1.20f};
constexpr Tone kArmFold = {0x969696, 0.90f}, kArmFoldEnd = {0xBEBEBE, 1.08f};
constexpr Tone kCursor = {0x50E6FF, 1.20f};

// Paint turns tones into colours for one drawing of the icon.
struct Paint {
    ImDrawList* dl;
    bool hovered;
    ImVec4 grey, bg;

    ImU32 colour(Tone t) const {
        if (hovered) return IM_COL32((t.rgb >> 16) & 0xFF, (t.rgb >> 8) & 0xFF, t.rgb & 0xFF, 255);
        auto channel = [&](float from, float to) { return std::clamp(from + (to - from) * t.rest, 0.0f, 1.0f); };
        return ImGui::ColorConvertFloat4ToU32(
            ImVec4(channel(bg.x, grey.x), channel(bg.y, grey.y), channel(bg.z, grey.z), grey.w));
    }

    // blend recolours every vertex added since the draw list held `from` of
    // them: tone ta at a, running to tb at b. Each keeps its alpha, so the
    // shapes' anti-aliased edges stay as they were drawn.
    void blend(int from, ImVec2 a, Tone ta, ImVec2 b, Tone tb) const {
        const ImU32 ca = colour(ta), cb = colour(tb);
        const ImVec2 d(b.x - a.x, b.y - a.y);
        const float len2 = std::max(d.x * d.x + d.y * d.y, 1e-6f);
        for (int i = from; i < dl->VtxBuffer.Size; i++) {
            ImDrawVert& v = dl->VtxBuffer.Data[i];
            const float k = std::clamp(((v.pos.x - a.x) * d.x + (v.pos.y - a.y) * d.y) / len2, 0.0f, 1.0f);
            auto channel = [&](int shift) {
                const float from8 = float((ca >> shift) & 0xFF), to8 = float((cb >> shift) & 0xFF);
                return ImU32(from8 + (to8 - from8) * k + 0.5f) << shift;
            };
            v.col = channel(IM_COL32_R_SHIFT) | channel(IM_COL32_G_SHIFT) | channel(IM_COL32_B_SHIFT) |
                    (v.col & IM_COL32_A_MASK);
        }
    }
};

}  // namespace

void DrawTerminalIcon(ImDrawList* dl, ImVec2 min, float size, bool hovered, ImVec4 muted, ImVec4 bg, bool faint) {
    // Not running: 0.45 of the way to the background, as the mango is.
    const float k = faint ? 0.45f : 0.0f;
    const ImVec4 grey(muted.x + (bg.x - muted.x) * k, muted.y + (bg.y - muted.y) * k, muted.z + (bg.z - muted.z) * k,
                      muted.w + (bg.w - muted.w) * k);
    const Paint paint{dl, hovered, grey, bg};

    // The window, on whole pixels so its edges and the tabs' joins are
    // sharp at title-bar size, and centred in the button.
    const float w = std::round(size * 0.80f), h = std::round(size * 0.64f);
    const float x0 = std::floor(min.x + (size - w) / 2 + 0.5f), y0 = std::floor(min.y + (size - h) / 2 + 0.5f);
    const float x1 = x0 + w, y1 = y0 + h;
    const float tabs = std::max(2.0f, std::round(h * 0.2f));  // the tab strip's height
    const float top = y0 + tabs, body = y1 - top;             // the slab below it
    const float corner = size * 0.06f;

    // The slab, lit from the top left.
    int from = dl->VtxBuffer.Size;
    dl->AddRectFilled(ImVec2(x0, top), ImVec2(x1, y1), paint.colour(kSlabLit), corner, ImDrawFlags_RoundCornersBottom);
    paint.blend(from, ImVec2(x0, top), kSlabLit, ImVec2(x1, y1), kSlabShade);

    // Three tabs across the top, lightest on the left.
    const float join1 = x0 + std::round(w / 3), join2 = x0 + std::round(w * 2 / 3);
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(join1, top), paint.colour(kTabs[0]), corner, ImDrawFlags_RoundCornersTopLeft);
    dl->AddRectFilled(ImVec2(join1, y0), ImVec2(join2, top), paint.colour(kTabs[1]));
    dl->AddRectFilled(ImVec2(join2, y0), ImVec2(x1, top), paint.colour(kTabs[2]), corner, ImDrawFlags_RoundCornersTopRight);

    // The prompt's ">", as tall as the slab allows with the same gap above
    // and below: two slanted bars meeting at a point on the right, the
    // upper one bright and the lower one in shadow, like a folded ribbon.
    const float gap = std::round(body * 0.19f), tall = body - gap * 2;
    const float thick = tall * 0.42f, reach = tall * 0.5f;  // the bars' width, and how far the point is to the right
    const float left = x0 + std::round(w * 0.17f), yTop = top + gap, yMid = yTop + tall / 2, yBot = yTop + tall;
    const ImVec2 lower[4] = {{left + reach, yMid}, {left + reach + thick, yMid}, {left + thick, yBot}, {left, yBot}};
    from = dl->VtxBuffer.Size;
    dl->AddConvexPolyFilled(lower, 4, paint.colour(kArmFold));
    paint.blend(from, lower[0], kArmFold, lower[3], kArmFoldEnd);
    const ImVec2 upper[4] = {{left, yTop}, {left + thick, yTop}, {left + reach + thick, yMid}, {left + reach, yMid}};
    from = dl->VtxBuffer.Size;
    dl->AddConvexPolyFilled(upper, 4, paint.colour(kArmLit));
    paint.blend(from, upper[0], kArmLit, upper[3], kArmLitEnd);

    // The cursor: a bar on the prompt's baseline, on whole pixels.
    const float cx = std::round(left + reach + thick + std::max(1.0f, w * 0.06f));
    const float cw = std::round(w * 0.28f), ct = std::max(2.0f, std::round(tall * 0.27f));
    dl->AddRectFilled(ImVec2(cx, yBot - ct), ImVec2(cx + cw, yBot), paint.colour(kCursor));
}

}  // namespace ks
