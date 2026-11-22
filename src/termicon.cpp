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

// A plain terminal window: a dark body inside a light rim, under a solid
// title band. Hovered, it's in Windows Terminal's own greys.
constexpr Tone kRim = {0xC8C8C8, 1.0f}, kBand = {0x9A9A9A, 0.82f}, kBody = {0x0C0C0C, 0.18f};

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

    // The window, on whole pixels so its edges are sharp at title-bar
    // size, and centred in the button.
    const float w = std::round(size * 0.74f), h = std::round(size * 0.60f);
    const float x0 = std::floor(min.x + (size - w) / 2 + 0.5f), y0 = std::floor(min.y + (size - h) / 2 + 0.5f);
    const float x1 = x0 + w, y1 = y0 + h;
    const float rim = std::max(1.0f, std::round(size * 0.055f));
    const float band = std::max(2.0f, std::round(h * 0.24f));
    const float corner = std::max(1.5f, size * 0.09f);

    // The rim as the whole window, the body cut into it below the band.
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), paint.colour(kRim), corner);
    dl->AddRectFilled(ImVec2(x0 + rim, y0 + band), ImVec2(x1 - rim, y1 - rim), paint.colour(kBody),
                      std::max(0.0f, corner - rim), ImDrawFlags_RoundCornersBottom);
    // The title band, a shade darker than the rim, under a rim-thick top.
    dl->AddRectFilled(ImVec2(x0 + rim, y0 + rim), ImVec2(x1 - rim, y0 + band), paint.colour(kBand),
                      std::max(0.0f, corner - rim), ImDrawFlags_RoundCornersTop);
}

}  // namespace ks
