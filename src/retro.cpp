#include "retro.h"

#include <cstdio>

namespace ks {

namespace {

constexpr unsigned kMaxVertices = 65535;  // what a 16-bit index reaches

bool isGlyph(const ImDrawVert& v, ImVec2 white) { return v.uv.x != white.x || v.uv.y != white.y; }

void shadowList(ImDrawList* dl, ImTextureID font, ImVec2 white, ImVec2 offset, ImU32 colour) {
    ImVector<ImDrawVert> vtx;
    ImVector<ImDrawIdx> idx;
    ImVector<ImDrawCmd> cmds;
    vtx.reserve(dl->VtxBuffer.Size * 2);
    idx.reserve(dl->IdxBuffer.Size * 2);
    unsigned alpha = (colour >> IM_COL32_A_SHIFT) & 0xFF;

    for (const ImDrawCmd& in : dl->CmdBuffer) {
        if (in.UserCallback || in.ElemCount == 0) {
            ImDrawCmd c = in;
            c.VtxOffset = unsigned(vtx.Size);
            c.IdxOffset = unsigned(idx.Size);
            cmds.push_back(c);
            continue;
        }
        bool text = in.TextureId == font;
        // Each triangle is copied out with its own vertices, its shadow
        // first, so a command can start a new range of vertices whenever
        // the last one fills.
        ImDrawCmd out = in;
        auto start = [&] {
            out.VtxOffset = unsigned(vtx.Size);
            out.IdxOffset = unsigned(idx.Size);
            out.ElemCount = 0;
        };
        start();
        auto emit = [&](const ImDrawVert& v) {
            idx.push_back(ImDrawIdx(unsigned(vtx.Size) - out.VtxOffset));
            vtx.push_back(v);
            out.ElemCount++;
        };
        auto room = [&](unsigned n) {
            if (unsigned(vtx.Size) - out.VtxOffset + n > kMaxVertices) {
                cmds.push_back(out);
                start();
            }
        };
        // A run of glyphs (a piece of text) gets all its shadows before its
        // letters, so no shadow falls on the letter before; shapes keep their
        // place among them.
        ImVector<const ImDrawVert*> run;
        auto flush = [&] {
            for (int i = 0; i < run.Size; i += 3) {
                room(3);
                for (int k = 0; k < 3; k++) {
                    ImDrawVert s = *run[i + k];
                    s.pos.x += offset.x;
                    s.pos.y += offset.y;
                    unsigned a = alpha * ((s.col >> IM_COL32_A_SHIFT) & 0xFF) / 255;
                    s.col = (colour & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
                    emit(s);
                }
            }
            for (int i = 0; i < run.Size; i += 3) {
                room(3);
                for (int k = 0; k < 3; k++) emit(*run[i + k]);
            }
            run.resize(0);
        };
        for (unsigned i = 0; i + 2 < in.ElemCount; i += 3) {
            const ImDrawVert* t[3];
            for (int k = 0; k < 3; k++) t[k] = &dl->VtxBuffer[in.VtxOffset + dl->IdxBuffer[in.IdxOffset + i + k]];
            if (text && isGlyph(*t[0], white) && isGlyph(*t[1], white) && isGlyph(*t[2], white)) {
                for (const ImDrawVert* v : t) run.push_back(v);
                continue;
            }
            flush();
            room(3);
            for (const ImDrawVert* v : t) emit(*v);
        }
        flush();
        cmds.push_back(out);
    }
    dl->VtxBuffer.swap(vtx);
    dl->IdxBuffer.swap(idx);
    dl->CmdBuffer.swap(cmds);
}

}  // namespace

void AddTextShadows(ImDrawData* data, ImTextureID font, ImVec2 whiteUv, ImVec2 offset, ImU32 colour) {
    data->TotalVtxCount = data->TotalIdxCount = 0;
    for (ImDrawList* dl : data->CmdLists) {
        shadowList(dl, font, whiteUv, offset, colour);
        data->TotalVtxCount += dl->VtxBuffer.Size;
        data->TotalIdxCount += dl->IdxBuffer.Size;
    }
}

std::pair<std::string, std::string> RetroClock(const std::tm& t) {
    static const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* months[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int h = t.tm_hour % 12 == 0 ? 12 : t.tm_hour % 12;
    char time[32], date[32];
    std::snprintf(time, sizeof(time), "%d:%02d:%02d %s", h, t.tm_min, t.tm_sec, t.tm_hour < 12 ? "AM" : "PM");
    std::snprintf(date, sizeof(date), "%s %d %s", days[((t.tm_wday % 7) + 7) % 7], t.tm_mday, months[((t.tm_mon % 12) + 12) % 12]);
    return {time, date};
}

}  // namespace ks
