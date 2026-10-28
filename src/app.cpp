#include "app.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <ctime>
#include <tuple>

#include "imgui.h"
#include "imgui_internal.h"
#include "cat.h"
#include "weather_icon.h"
#include "local.h"
#include "retro.h"
#include "sky.h"

namespace ks {

// ChipsWheel owns the mouse wheel while chips that scroll are hovered.
const ImGuiID ChipsWheel = ImHashStr("kitchensink chips wheel");

namespace {

ImU32 u32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImVec4 fade(ImVec4 c, float a) {
    c.w = a;
    return c;
}

int64_t now() { return int64_t(std::time(nullptr)); }

// mix is k of the way from colour a to b, alpha and all.
ImVec4 mix(const ImVec4& a, const ImVec4& b, float k) {
    return ImVec4(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k, a.w + (b.w - a.w) * k);
}

// clockTime is a Unix time as local hours and minutes, or a dash for 0.
std::string clockTime(int64_t t) {
    if (t == 0) return "\xE2\x80\x93";
    std::time_t tt = std::time_t(t);
    std::tm l{};
    localtime_s(&l, &tt);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", l.tm_hour, l.tm_min);
    return buf;
}

const Priority kPriorities[] = {Priority::Low, Priority::Normal, Priority::High};
const char* kPriorityNames[] = {"Everything", "Normal and high priority", "High priority only"};

// textAligned draws text in the current table cell or line, aligned within
// the space left on it.
void textAligned(const std::string& s, Align align, const ImVec4& colour) {
    float w = ImGui::CalcTextSize(s.c_str()).x;
    float avail = ImGui::GetContentRegionAvail().x;
    if (align == Align::Right && avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    if (align == Align::Center && avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - w) / 2);
    ImGui::TextColored(colour, "%s", s.c_str());
}

// linkHovered is set when a link is under the mouse, so a collapsed row
// drawn around it can leave the click to the link.
bool linkHovered = false;

// linkBehaviour makes the last item open url when clicked.
void linkBehaviour(const std::string& url) {
    if (url.empty() || !ImGui::IsItemHovered()) return;
    linkHovered = true;
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, u32(Colours().muted));
    if (ImGui::IsItemClicked()) OpenURL(url);
}

void mutedText(const std::string& s) { ImGui::TextColored(Colours().muted, "%s", s.c_str()); }

// fit shortens s with an ellipsis to fit width in the current font.
std::string fit(const std::string& s, float width) {
    if (ImGui::CalcTextSize(s.c_str()).x <= width) return s;
    std::string out = s;
    while (!out.empty()) {
        // Drop one UTF-8 character: its continuation bytes, then its lead.
        while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
        while (!out.empty() && out.back() == ' ') out.pop_back();
        if (ImGui::CalcTextSize((out + "…").c_str()).x <= width) return out + "…";
    }
    return "…";
}

// fitLines shortens each line of s to fit width, as fit does.
std::string fitLines(const std::string& s, float width) {
    std::string out;
    size_t start = 0;
    for (;;) {
        size_t end = s.find('\n', start);
        out += fit(s.substr(start, end == std::string::npos ? std::string::npos : end - start), width);
        if (end == std::string::npos) return out;
        out += '\n';
        start = end + 1;
    }
}

void wrapped(const std::string& s, const ImVec4& colour) {
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    ImGui::PushTextWrapPos(0);
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
}

// sparkline draws a series as a filled line chart, from 0 (or below) to
// top, or to the series' highest point when top is 0.
void sparkline(const std::vector<float>& v, ImVec2 size, const ImVec4& colour, float top) {
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    if (v.size() < 2) return;
    auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    float min = std::min(*lo, 0.0f), range = std::max((top > 0 ? std::max(top, *hi) : *hi) - min, 1e-6f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    std::vector<ImVec2> pts;
    for (size_t i = 0; i < v.size(); i++)
        pts.push_back(ImVec2(p.x + size.x * float(i) / float(v.size() - 1), p.y + size.y - size.y * (v[i] - min) / range));
    float base = p.y + size.y;
    for (size_t i = 0; i + 1 < pts.size(); i++) {
        ImVec2 quad[4] = {pts[i], pts[i + 1], ImVec2(pts[i + 1].x, base), ImVec2(pts[i].x, base)};
        dl->AddConvexPolyFilled(quad, 4, u32(fade(colour, 0.18f)));
    }
    dl->AddPolyline(pts.data(), int(pts.size()), u32(colour), 0, 1.5f * ImGui::GetStyle().FramePadding.y / 6);
}

// DrawListCanvas draws the cat and weather icons with ImGui.
class DrawListCanvas : public Canvas {
public:
    explicit DrawListCanvas(ImDrawList* dl) : dl_(dl) {}
    void Circle(Vec2f c, float r, Rgba col) override { dl_->AddCircleFilled(v(c), r, u(col)); }
    void Ellipse(Vec2f c, float rx, float ry, float rot, Rgba col) override {
        dl_->AddEllipseFilled(v(c), ImVec2(rx, ry), u(col), rot);
    }
    void Line(Vec2f a, Vec2f b, float thickness, Rgba col) override { dl_->AddLine(v(a), v(b), u(col), thickness); }
    void Polygon(const Vec2f* points, int n, Rgba col) override {
        ImVec2 pts[16];
        n = std::min(n, 16);
        for (int i = 0; i < n; i++) pts[i] = v(points[i]);
        dl_->AddConvexPolyFilled(pts, n, u(col));
    }

private:
    static ImVec2 v(Vec2f p) { return ImVec2(p.x, p.y); }
    static ImU32 u(Rgba c) { return IM_COL32(c.r, c.g, c.b, c.a); }
    ImDrawList* dl_;
};

// rgba is an opaque colour k of the way from a to b: icons overlap their
// own shapes, so they can't be see-through.
Rgba rgba(const ImVec4& a, const ImVec4& b = {}, float k = 0) {
    auto c = [&](float x, float y) { return uint8_t(std::clamp(x + (y - x) * k, 0.0f, 1.0f) * 255 + 0.5f); };
    return {c(a.x, b.x), c(a.y, b.y), c(a.z, b.z), 255};
}

// weatherIcon draws a weather icon in the square at at, size across, in
// the theme's colours: rain in its accent, so wet hours stand out.
void weatherIcon(const std::string& name, ImVec2 at, float size) {
    const Palette& p = Colours();
    IconColours col{rgba(p.warn), rgba(p.text), rgba(p.text, p.muted, 0.3f), rgba(p.muted, p.frame, 0.2f), rgba(p.accent), rgba(p.text), rgba(p.warn)};
    DrawListCanvas canvas(ImGui::GetWindowDrawList());
    DrawWeatherIcon(canvas, name, Vec2f{at.x, at.y}, size, col);
}

}  // namespace

void OpenURL(const std::string& url) {
    if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) return;
    ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

App::App(HWND hwnd, ID3D11Device* device, std::function<void()> wake,
         std::function<void(const std::string&, const std::string&)> notify)
    : hwnd_(hwnd),
      config_(LoadConfig()),
      client_(std::make_unique<Client>(wake)),
      images_(std::make_unique<ImageCache>(device, wake)),
      local_(std::make_unique<LocalStats>(wake)),
      media_(std::make_unique<MediaWatcher>(wake, images_.get())),
      grabber_(std::make_unique<Grabber>(wake)),
      notify_(std::move(notify)) {
    // Idle dimming counts from launch, not from 1970.
    lastActivity_ = now();
    model_.SetCleared(config_.cleared);
    client_->Start(config_.url, config_.token);
    if (config_.url.empty() || config_.token.empty()) openSettings();
    lick_.Start();
    launcher_.Set(config_.apps);
}

App::~App() {
    media_.reset();  // before images_, which it feeds
    local_.reset();
    client_.reset();
    images_.reset();
}

Theme App::theme() const {
    if (config_.theme == "retro") return Theme::Retro;
    if (config_.oled.black) return Theme::Oled;
    return dark_ ? Theme::Dark : Theme::Light;
}

void App::SetScale(float scale) {
    scale_ = scale;
    ApplyTheme(theme(), scale_, compact_);
    fonts_ = LoadFonts(scale_, Colours().retro);
    fontsDirty_ = false;
}

void App::ReloadFonts() {
    fonts_ = LoadFonts(scale_, Colours().retro);
    fontsDirty_ = false;
}

void App::SetDark(bool dark) {
    dark_ = dark;
    ApplyTheme(theme(), scale_, compact_);
}

void App::PostRender(ImDrawData* data) const {
    if (!Colours().retro) return;
    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    float d = std::max(1.0f, std::round(1.5f * scale_));
    AddTextShadows(data, atlas->TexID, atlas->TexUvWhitePixel, ImVec2(d, d), IM_COL32(0, 0, 0, 210));
}

void App::bevel(ImVec2 min, ImVec2 max) {
    const Palette& p = Colours();
    if (!p.retro) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float t = std::max(1.0f, std::round(2 * scale_));
    dl->AddRectFilled(min, ImVec2(max.x, min.y + t), u32(p.bevelLight));
    dl->AddRectFilled(min, ImVec2(min.x + t, max.y), u32(p.bevelLight));
    dl->AddRectFilled(ImVec2(min.x, max.y - t), max, u32(p.bevelDark));
    dl->AddRectFilled(ImVec2(max.x - t, min.y), max, u32(p.bevelDark));
}

void App::Pump() {
    std::vector<Inbound> in;
    client_->Drain(in);
    for (auto& m : in) {
        switch (m.type) {
        case Inbound::Type::SyncStart: model_.BeginSync(); break;
        case Inbound::Type::Ready: model_.EndSync(); break;
        case Inbound::Type::Snapshot: {
            refreshing_.erase(m.snapshot.monitor);
            std::string prefix = m.snapshot.monitor + "/";
            for (auto it = ticked_.begin(); it != ticked_.end();)
                it = it->first.rfind(prefix, 0) == 0 ? ticked_.erase(it) : std::next(it);
            model_.ApplySnapshot(std::move(m.snapshot));
            break;
        }
        case Inbound::Type::Event: {
            Event e = m.event;
            if (!model_.AddEvent(std::move(m.event))) break;
            if (e.until > 0 && e.until <= now()) break;  // missed while offline and no longer relevant
            // History from before the app started has already been seen
            // (or missed for good); events missed while offline are new.
            if (!m.history && (!e.quiet || e.expires > 0)) wakeUntil_ = now() + 2 * 60;  // OLED care: show it
            if (!m.history && !e.quiet && config_.notify && e.priority >= config_.minPriority)
                pending_.push_back(std::move(e));
            break;
        }
        }
    }
    model_.DropExpired(now());
    if (client_->State() == ConnState::Connected) uploader_.Check(model_.Snapshots(), *client_);
    images_->Upload();
    local_->Take(localPanels_);
    media_->Take(mediaPanel_);

    if (pending_.empty()) return;
    if (pendingSince_ == 0) pendingSince_ = GetTickCount64();
    if (FlushIn() > 0) return;

    if (pending_.size() == 1) {
        const Event& e = pending_[0];
        lastNotifyURL_ = e.url;
        notify_(e.title, e.body.empty() ? model_.TitleOf(e.monitor) : e.body);
    } else {
        // Several at once, e.g. after being offline: one summary, newest
        // first.
        lastNotifyURL_.clear();
        std::string body;
        for (size_t i = pending_.size(); i-- > 0;) {
            if (!body.empty()) body += "\n";
            body += pending_[i].title;
        }
        notify_(std::to_string(pending_.size()) + " new events", body);
    }
    pending_.clear();
    pendingSince_ = 0;
}

unsigned long App::FlushIn() const {
    constexpr unsigned long long kHoldMs = 1500;
    if (pendingSince_ == 0) return INFINITE;
    unsigned long long waited = GetTickCount64() - pendingSince_;
    return waited >= kHoldMs ? 0 : (unsigned long)(kHoldMs - waited);
}

bool App::NotificationClicked() {
    if (lastNotifyURL_.empty()) return false;
    OpenURL(lastNotifyURL_);
    return true;
}

void App::Frame() {
    catInterval_ = 0;  // until she's drawn
    // The wheel goes back to the dashboard once no scrolling chips held it
    // last frame.
    if (ImGui::GetKeyOwner(ImGuiKey_MouseWheelY) == ChipsWheel && wheelHeld_ < ImGui::GetFrameCount() - 1)
        ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ImGuiKeyOwner_NoOwner);
    ImGuiIO& io = ImGui::GetIO();
    if (bool c = UseCompact(compact_, io.DisplaySize.x, io.DisplaySize.y, scale_); c != compact_) {
        compact_ = c;
        ApplyTheme(theme(), scale_, compact_);
    }
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##root", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    // The retro theme's sky, behind everything.
    if (Colours().retro) {
        const Palette& p = Colours();
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(ImVec2(0, 0), io.DisplaySize, u32(p.bgTop), u32(p.bgTop),
                                                            u32(p.bgBottom), u32(p.bgBottom));
    }

    // OLED care moves everything round a small circle, inside a margin left
    // for it. The window itself stays full size, so nothing behind it (the
    // panels being measured) shows round the edge.
    int64_t t = now();
    float margin = config_.oled.shift ? std::round(6 * scale_) : 0;
    auto [dx, dy] = config_.oled.shift ? OledShift(t, margin) : std::pair<float, float>{0.0f, 0.0f};
    ImGui::SetCursorPos(ImVec2(margin + dx, margin + dy));
    ImGui::BeginChild("##contents", ImVec2(io.DisplaySize.x - margin * 2, io.DisplaySize.y - margin * 2),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    header();
    local_->Want();
    media_->Want();

    float gap = (compact_ ? 8 : 16) * scale_;
    float avail = ImGui::GetContentRegionAvail().x;
    float feedW = avail >= 980 * scale_ ? (compact_ ? 300 : 340) * scale_ : 0;
    float cardsW = feedW > 0 ? avail - feedW - gap : avail;

    ConnState state = client_->State();
    if (state == ConnState::NotConfigured) {
        empty("Add your server's address and token to get started.", true);
    } else if (model_.Snapshots().empty() && localPanels_.empty()) {
        empty(state == ConnState::Offline ? "Can't reach the server." : "Connecting…", state == ConnState::Offline);
    } else {
        mediaBarShown_ = feedW > 0;
        std::vector<Snapshot> main = panels();

        // OLED care swaps the side column's side every other hour.
        bool feedFirst = feedW > 0 && config_.oled.swap && OledSwapSides(t);
        auto drawFeed = [&] {
            ImGui::BeginGroup();
            if (config_.cat) cat(feedW);
            ImGui::BeginChild("##temporary", ImVec2(feedW, 0), ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            temporaries();
            ImGui::EndChild();
            feedHeading(feedW);
            // Now playing and the landscape sit at the bottom, always the
            // same height so Recent doesn't move.
            float skyH = skyHeight(), barH = mediaBarHeight();
            float gap = 10 * scale_, tight = 4 * scale_;
            float below = barH + gap + (skyH > 0 ? skyH + tight : 0);
            // Scrolled with the wheel; no scrollbar, which would take width
            // and get in the way.
            ImGui::BeginChild("##feed", ImVec2(feedW, -below), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
            feed();
            ImGui::EndChild();
            float spacing = ImGui::GetStyle().ItemSpacing.y;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + gap - spacing);
            mediaBar(feedW);
            if (skyH > 0) {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + tight - spacing);
                sky(feedW, t);
            }
            ImGui::EndGroup();
            feedRect_ = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        };
        if (feedFirst) {
            drawFeed();
            ImGui::SameLine(0, gap);
        }
        // Like Recent, scrolled with the wheel, without a scrollbar.
        ImGui::BeginChild("##cards", ImVec2(cardsW, 0), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        cards(ImGui::GetContentRegionAvail().x, main);
        ImGui::EndChild();
        if (feedW > 0 && !feedFirst) {
            ImGui::SameLine(0, gap);
            drawFeed();
        }
    }

    ImGui::EndChild();
    settings();

    // OLED care dims everything when the mouse hasn't been over the window
    // for a while; it's back as soon as it is.
    if (ImGui::IsMousePosValid() || settingsOpen_) lastActivity_ = t;
    bool dimmed = config_.oled.dim && OledDim(t, lastActivity_, wakeUntil_);
    if (dimmed) ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 0, 153));
    // The cat isn't drawn while the window's dimmed.
    frameInterval_ = dimmed ? 0 : catInterval_;
    // The retro theme's clock shows seconds.
    if (!dimmed && Colours().retro) frameInterval_ = frameInterval_ ? std::min(frameInterval_, 1000ul) : 1000;
    // Lick of the Day hides or shows a moment after its button's clicked;
    // draw again to show which.
    if (ImGui::GetTime() - lickClicked_ < 1) frameInterval_ = frameInterval_ ? std::min(frameInterval_, 100ul) : 100;
    busy_ = ImGui::IsAnyItemActive() || ImGui::GetIO().WantTextInput || settingsOpen_;
    ImGui::End();
}

std::vector<Snapshot> App::panels() const {
    std::vector<Snapshot> out;
    if (!localPanels_.empty()) out.push_back(localPanels_.front());
    if (!mediaBarShown_ && !mediaPanel_.blocks.empty()) out.push_back(mediaPanel_);
    for (const Snapshot& s : model_.Snapshots()) {
        if (s.background) continue;  // in the settings' diagnostics instead
        out.push_back(config_.hidePrivate ? WithoutPrivate(s) : s);
    }
    if (config_.order.empty()) return out;
    std::vector<std::string> ids;
    for (const auto& p : out) ids.push_back(p.monitor);
    std::vector<Snapshot> arranged;
    for (const auto& id : Arrange(ids, config_.order))
        arranged.push_back(*std::find_if(out.begin(), out.end(), [&](const Snapshot& p) { return p.monitor == id; }));
    return arranged;
}

// hoveringCard reports whether the mouse is over the card being drawn,
// including the panels inside it, even while one of its items is held.
bool App::hoveringCard() const {
    ImVec2 min = ImGui::GetWindowPos();
    ImVec2 max(min.x + ImGui::GetWindowWidth(), min.y + ImGui::GetWindowHeight());
    return ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
           ImGui::IsMouseHoveringRect(min, max);
}

bool App::InCaption(float x, float y) const {
    if (y >= captionTop_ + captionH_) return false;
    for (const ImRect& r : noDrag_)
        if (r.Contains(ImVec2(x, y))) return false;
    return true;
}

// header draws the title bar: the name and connection status on the left,
// settings and the window buttons on the right. Everything else on it
// drags the window (see InCaption).
void App::header() {
    const Palette& p = Colours();
    ImVec2 origin = ImGui::GetWindowPos();
    float width = ImGui::GetWindowWidth();
    captionH_ = (compact_ ? 36 : 44) * scale_;
    captionTop_ = origin.y;
    noDrag_.clear();

    // Windows 10 draws no border along the top of a window without a
    // standard title bar, so draw one to match the sides.
    // It belongs at the window's real edge, wherever OLED care has moved
    // the contents.
    if (!IsZoomed(hwnd_))
        ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0, 0.5f), ImVec2(ImGui::GetIO().DisplaySize.x, 0.5f), u32(p.border));

    // The retro theme's title band, with an amber rule beneath, behind the
    // window buttons too.
    if (p.retro) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = origin, b(origin.x + width, origin.y + captionH_);
        dl->AddRectFilledMultiColor(a, b, IM_COL32(0x24, 0x3C, 0xB4, 0xFF), IM_COL32(0x24, 0x3C, 0xB4, 0xFF),
                                    IM_COL32(0x10, 0x1E, 0x7A, 0xFF), IM_COL32(0x10, 0x1E, 0x7A, 0xFF));
        float t = std::max(2.0f, std::round(3 * scale_));
        dl->AddRectFilledMultiColor(ImVec2(a.x, b.y - t), b, IM_COL32(0xE0, 0x62, 0x00, 0xFF), IM_COL32(0xFF, 0xC8, 0x1A, 0xFF),
                                    IM_COL32(0xFF, 0xC8, 0x1A, 0xFF), IM_COL32(0xE0, 0x62, 0x00, 0xFF));
    }

    float left = windowButtons();

    ImGui::PushFont(fonts_.title);
    float titleH = ImGui::GetTextLineHeight();
    float mid = captionH_ / 2;
    ImGui::SetCursorPos(ImVec2(ImGui::GetStyle().WindowPadding.x, mid - titleH / 2));
    ImGui::TextColored(p.heading, "kitchensink");
    ImGui::PopFont();

    // The retro theme's clock, as on the WeatherStar: time, then date.
    std::pair<std::string, std::string> clock;
    float clockW = 0, gapW = 10 * scale_;
    if (p.retro) {
        std::time_t tt = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &tt);
        clock = RetroClock(local);
        ImGui::PushFont(fonts_.heading);
        clockW = ImGui::CalcTextSize(clock.first.c_str()).x;
        ImGui::PopFont();
        ImGui::PushFont(fonts_.caption);
        clockW += gapW + ImGui::CalcTextSize(clock.second.c_str()).x;
        ImGui::PopFont();
    }

    ConnState state = client_->State();
    ImVec4 dot = p.muted;
    std::string status;
    switch (state) {
    case ConnState::NotConfigured: status = "Not set up"; break;
    case ConnState::Connecting: status = "Connecting…", dot = p.warn; break;
    case ConnState::Connected: status = "Live", dot = p.good; break;
    case ConnState::Offline: {
        std::string err = client_->LastError();
        status = err.empty() ? "Offline" : "Offline: " + err;
        dot = p.bad;
        break;
    }
    }
    // The dot and status sit centred on the title's line.
    ImGui::SameLine(0, 16 * scale_);
    ImGui::PushFont(fonts_.caption);
    float textH = ImGui::GetTextLineHeight();
    ImGui::SetCursorPosY(mid - textH / 2);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float r = 4 * scale_;
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(pos.x + r, pos.y + textH / 2), r, u32(dot));
    ImGui::Dummy(ImVec2(r * 2, textH));
    ImGui::SameLine(0, 6 * scale_);
    ImGui::SetCursorPosY(mid - textH / 2);
    // Long errors stop short of the buttons.
    ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(left - 48 * scale_ - (clockW > 0 ? clockW + 72 * scale_ : 0), origin.y + captionH_), true);
    mutedText(status);
    ImGui::PopClipRect();
    ImGui::PopFont();

    float bw = ImGui::CalcTextSize(icon::Settings).x + ImGui::GetStyle().FramePadding.x * 2;
    ImGui::SameLine();
    ImGui::SetCursorPos(ImVec2(left - origin.x - 8 * scale_ - bw, mid - ImGui::GetFrameHeight() / 2));
    // Flat until hovered, like the window buttons beside it, and muted
    // like the guitar: the text colour only while hovered.
    ImVec2 at = ImGui::GetCursorScreenPos();
    bool lit = ImGui::IsMouseHoveringRect(at, ImVec2(at.x + bw, at.y + ImGui::GetFrameHeight()));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, lit ? p.text : p.muted);
    bool open = ImGui::Button(icon::Settings);
    ImGui::PopStyleColor(2);
    if (open) openSettings();
    noDrag_.push_back(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
    float buttonsLeft = ImGui::GetItemRectMin().x;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Settings");

    // Hide or show private figures, e.g. a portfolio, at a click: only
    // there when a monitor sends some.
    const auto& snaps = model_.Snapshots();
    if (std::any_of(snaps.begin(), snaps.end(), [](const Snapshot& s) { return HasPrivate(s); })) {
        float ew = ImGui::CalcTextSize(icon::View).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(left - origin.x - 8 * scale_ - bw - 4 * scale_ - ew, mid - ImGui::GetFrameHeight() / 2));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, config_.hidePrivate ? p.muted : p.text);
        bool toggle = ImGui::Button(icon::View);
        ImGui::PopStyleColor(2);
        // Struck through while hidden: Windows 10's icon font has no such
        // eye.
        if (config_.hidePrivate) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax(), c((a.x + b.x) / 2, (a.y + b.y) / 2);
            float r = 7 * scale_;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(c.x - r, c.y - r + 2 * scale_), ImVec2(c.x + r, c.y + r + 2 * scale_),
                                                u32(p.muted), 1.5f * scale_);
        }
        noDrag_.push_back(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
        buttonsLeft = ImGui::GetItemRectMin().x;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", config_.hidePrivate ? "Show private figures" : "Hide private figures");
        if (toggle) {
            config_.hidePrivate = !config_.hidePrivate;
            SaveConfig(config_);
        }
    }
    if (lick_.Installed()) buttonsLeft = lickButton(buttonsLeft - 4 * scale_, mid);
    if (!launcher_.Installed().empty()) buttonsLeft = appsButton(buttonsLeft - 4 * scale_, mid);
    if (clockW > 0) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float x = buttonsLeft - 16 * scale_ - clockW;
        ImGui::PushFont(fonts_.heading);
        ImVec2 ts = ImGui::CalcTextSize(clock.first.c_str());
        dl->AddText(ImVec2(x, std::round(origin.y + mid - ts.y / 2)), u32(p.text), clock.first.c_str());
        ImGui::PopFont();
        ImGui::PushFont(fonts_.caption);
        ImVec2 ds = ImGui::CalcTextSize(clock.second.c_str());
        dl->AddText(ImVec2(x + ts.x + gapW, std::round(origin.y + mid - ds.y / 2)), u32(p.heading), clock.second.c_str());
        ImGui::PopFont();
    }
    ImGui::SetCursorPosY(captionH_ + (compact_ ? 2 : 8) * scale_);
}

// windowButtons draws minimise, maximise and close in the top-right corner,
// like Windows' own, and returns where they start. They read the mouse
// directly rather than being ImGui items, so they keep working while the
// settings dialog is open.
float App::windowButtons() {
    const Palette& p = Colours();
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetWindowPos();
    float w = std::round(46 * scale_), h = captionH_;
    float x = origin.x + ImGui::GetWindowWidth();
    bool active = GetForegroundWindow() == hwnd_;
    bool zoomed = IsZoomed(hwnd_);

    ImGui::PushClipRect(origin, ImVec2(x, origin.y + h), false);
    ImGui::PushFont(fonts_.chrome);
    enum { Minimise, Maximise, Close };
    const char* glyphs[] = {icon::Minimise, zoomed ? icon::Restore : icon::Maximise, icon::Close};
    const char* tips[] = {"Minimise", zoomed ? "Restore down" : "Maximise", "Close (keeps running in the tray)"};
    x -= 3 * w;
    float left = x;
    bool anyHovered = false;
    for (int i = 0; i < 3; i++, x += w) {
        ImRect r(ImVec2(x, origin.y), ImVec2(x + w, origin.y + h));
        noDrag_.push_back(r);
        bool hovered = r.Contains(io.MousePos);
        bool held = hovered && io.MouseDown[0] && r.Contains(io.MouseClickedPos[0]);
        bool clicked = hovered && io.MouseReleased[0] && r.Contains(io.MouseClickedPos[0]);

        ImVec4 glyph = active ? p.text : p.muted;
        if (i == Close && (hovered || held)) {
            dl->AddRectFilled(r.Min, r.Max, held ? IM_COL32(0xC4, 0x2B, 0x1C, 0xE6) : IM_COL32(0xC4, 0x2B, 0x1C, 0xFF));
            glyph = ImVec4(1, 1, 1, 1);
        } else if (hovered) {
            float a = (held ? 0.12f : 0.07f) * (dark_ ? 1.0f : 0.8f);
            dl->AddRectFilled(r.Min, r.Max, dark_ ? u32(ImVec4(1, 1, 1, a)) : u32(ImVec4(0, 0, 0, a)));
            glyph = p.text;
        }
        ImVec2 size = ImGui::CalcTextSize(glyphs[i]);
        dl->AddText(ImVec2(std::floor(x + (w - size.x) / 2), std::floor(origin.y + (h - size.y) / 2)), u32(glyph),
                    glyphs[i]);
        if (hovered) {
            if (hoverButton_ != i) hoverButton_ = i, hoverSince_ = ImGui::GetTime();
            if (!io.MouseDown[0] && ImGui::GetTime() - hoverSince_ > 0.6) {
                ImGui::PushFont(fonts_.body);
                ImGui::SetTooltip("%s", tips[i]);
                ImGui::PopFont();
            }
            anyHovered = true;
        }
        if (clicked) {
            if (i == Minimise) ShowWindow(hwnd_, SW_MINIMIZE);
            if (i == Maximise) ShowWindow(hwnd_, zoomed ? SW_RESTORE : SW_MAXIMIZE);
            if (i == Close) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
        }
    }
    if (!anyHovered) hoverButton_ = -1;
    ImGui::PopFont();
    ImGui::PopClipRect();
    return left;
}

void App::empty(const char* message, bool settingsButton) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + avail.y * 0.35f);
    float w = ImGui::CalcTextSize(message).x;
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - w) / 2);
    mutedText(message);
    if (client_->State() == ConnState::Offline) {
        std::string err = client_->LastError();
        if (!err.empty()) {
            ImGui::PushFont(fonts_.caption);
            ImGui::SetCursorPosX((ImGui::GetWindowWidth() - ImGui::CalcTextSize(err.c_str()).x) / 2);
            mutedText(err);
            ImGui::PopFont();
        }
    }
    if (settingsButton) {
        ImGui::Dummy(ImVec2(0, 8 * scale_));
        const char* label = "Open settings";
        float bw = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) / 2);
        if (ImGui::Button(label)) openSettings();
    }
}

void App::cards(float width, const std::vector<Snapshot>& snaps) {
    cardRects_.clear();
    if (compact_) {
        compactCards(width, snaps);
        dragDrop();
        return;
    }
    float gap = 16 * scale_;
    int cols = std::max(1, int((width + gap) / (440 * scale_ + gap)));
    cols = std::min<int>(cols, int(snaps.size()));
    float colW = (width - gap * (cols - 1)) / cols;

    // Masonry: each card goes in the shortest column so far, using the
    // heights cards had last frame.
    std::vector<std::vector<size_t>> columns(cols);
    std::vector<float> heights(cols, 0);
    for (size_t i = 0; i < snaps.size(); i++) {
        int c = int(std::min_element(heights.begin(), heights.end()) - heights.begin());
        columns[c].push_back(i);
        auto it = heights_.find(snaps[i].monitor);
        heights[c] += (it != heights_.end() ? it->second : 200 * scale_) + gap;
    }
    for (int c = 0; c < cols; c++) {
        if (c > 0) ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        for (size_t i : columns[c]) {
            card(snaps[i], colW);
            ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y * 2));
        }
        ImGui::EndGroup();
    }
    dragDrop();
}

void App::card(const Snapshot& s, float width) {
    const Palette& p = Colours();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.card);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale_, 14 * scale_));
    ImGui::BeginChild(s.monitor.c_str(), ImVec2(width, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    bool show = dragging_ == s.monitor || (dragging_.empty() && hoveringCard());
    ImGui::PushFont(fonts_.caption);
    grip(s, ImGui::GetCursorScreenPos().y + ImGui::GetFrameHeight() / 2, show);
    ImGui::PopFont();

    // What's being watched on the left, if the monitor says; when it was
    // updated and refresh on the right. Local panels need neither, and
    // the age only shows on hover, for monitors where it matters now.
    bool local = IsLocal(s);
    if (!local || !s.subtitle.empty()) {
        ImGui::PushFont(fonts_.caption);
        ImGui::AlignTextToFramePadding();
        mutedText(s.subtitle);
        if (!local) {
            bool pending = refreshing_.count(s.monitor) && ImGui::GetTime() - refreshing_[s.monitor] < 15;
            std::string when = pending                     ? "refreshing…"
                               : show && s.timely && s.updated ? "updated " + Ago(s.updated, now())
                                                               : "";
            ImGuiStyle& st = ImGui::GetStyle();
            float bw = ImGui::CalcTextSize(icon::Refresh).x + st.FramePadding.x * 2;
            float ww = ImGui::CalcTextSize(when.c_str()).x;
            float pw = s.paste.empty() ? 0 : ImGui::CalcTextSize(icon::Paste).x + st.FramePadding.x * 2 + st.ItemSpacing.x;
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - bw - pw - ww - st.ItemSpacing.x);
            ImGui::AlignTextToFramePadding();
            mutedText(when);
            if (!s.paste.empty()) {
                ImGui::SameLine();
                if (show) pasteButton(s);
                else ImGui::Dummy(ImVec2(pw - st.ItemSpacing.x, ImGui::GetFrameHeight()));
            }
            ImGui::SameLine();
            if (show) refreshButton(s);
            else ImGui::Dummy(ImVec2(bw, ImGui::GetFrameHeight()));
        }
        ImGui::PopFont();
    }

    if (!s.error.empty()) {
        ImGui::PushFont(fonts_.caption);
        wrapped(std::string(icon::Warning) + "  " + s.error, p.warn);
        ImGui::PopFont();
    }
    if (s.blocks.empty() && s.error.empty()) mutedText("Waiting for the first check…");

    for (size_t i = 0; i < s.blocks.size(); i++) {
        ImGui::PushID(int(i));
        ImGui::Dummy(ImVec2(0, 2 * scale_));
        block(s.monitor, s.blocks[i]);
        ImGui::PopID();
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    bevel(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    heights_[s.monitor] = ImGui::GetItemRectSize().y;
    cardRects_.push_back({s.monitor, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())});
}

void App::refreshButton(const Snapshot& s) {
    bool pending = refreshing_.count(s.monitor) && ImGui::GetTime() - refreshing_[s.monitor] < 15;
    // Dim, flat until hovered: it's rarely needed.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, Colours().muted);
    ImGui::PushID(s.monitor.c_str());
    ImGui::BeginDisabled(pending);
    if (ImGui::Button(icon::Refresh)) {
        refreshing_[s.monitor] = ImGui::GetTime();
        client_->Refresh(s.monitor);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tip = pending ? "Checking…" : "Check now";
        if (!pending && s.updated) tip += " (updated " + Ago(s.updated, now()) + ")";
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::PopStyleColor(2);
}

// actionButton is a table cell's button that sends its monitor an action,
// e.g. eBay's ignore. It shows only while its row is hovered (show), its
// space kept so nothing moves. Once sent it shows a tick until the server's
// next snapshot, which usually drops the button.
void App::actionButton(const std::string& monitor, const Cell& cell, bool show) {
    std::string key = monitor + "\n" + cell.actionType + "\n" + cell.actionId;
    bool sent = sentActions_.count(key) > 0;
    if (!show && !sent) {
        ImGui::Dummy(ImVec2(ImGui::CalcTextSize(icon::Dismiss).x + 8 * scale_, ImGui::GetTextLineHeight()));
        return;
    }
    ImGui::PushID(key.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, Colours().muted);
    if (sent) {
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * scale_, 0));
    if (ImGui::Button(sent ? icon::CheckMark : icon::Dismiss) && !sent) {
        sentActions_.insert(key);
        client_->Act(monitor, nlohmann::json{{"type", cell.actionType}, {"id", cell.actionId}}.dump());
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", sent ? "Done" : cell.actionLabel.c_str());
        localHovered_ = true;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(sent ? 4 : 2);
    ImGui::PopID();
}

// localButton is a table cell's button for an app on this PC: watchgrab's
// download, showing how the film stands with it.
void App::localButton(const Cell& cell) {
    GrabView v = ViewGrab(grabber_->Get(cell.localArg));
    const char* glyph = v.icon == GrabView::Busy      ? icon::Refresh
                        : v.icon == GrabView::Done    ? icon::CheckMark
                        : v.icon == GrabView::Warning ? icon::Warning
                                                      : icon::Download;
    ImVec4 colour = v.tone == Tone::None ? Colours().accent : ToneColour(v.tone);
    ImGui::PushID(cell.localArg.c_str());
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, colour);
    if (!v.clickable) {
        // A state, not a button: full colour, no highlight.
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0, 0, 0, 0));
    }
    // No vertical padding, so the row stays a line of text tall.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4 * scale_, 0));
    if (ImGui::Button(glyph) && v.clickable) grabber_->Start(cell.localArg);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", v.tooltip.c_str());
        localHovered_ = true;
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(v.clickable ? 2 : 4);
    ImGui::PopID();
}

void App::pasteButton(const Snapshot& s) {
    bool sent = pasted_.count(s.monitor) && ImGui::GetTime() - pasted_[s.monitor] < 3;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, Colours().muted);
    ImGui::PushID(s.monitor.c_str());
    if (ImGui::Button(icon::Paste)) {
        const char* text = ImGui::GetClipboardText();
        client_->Act(s.monitor, nlohmann::json{{"type", "paste"}, {"text", text ? text : ""}}.dump());
        pasted_[s.monitor] = ImGui::GetTime();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", sent ? "Sent" : s.paste.c_str());
    ImGui::PopID();
    ImGui::PopStyleColor(2);
}

// compactCards fills the space with one row per monitor, full width so the
// summaries line up, opening as many full panels as fit (see FitPanels).
void App::compactCards(float width, const std::vector<Snapshot>& snaps) {
    float gap = 8 * scale_;
    float height = ImGui::GetContentRegionAvail().y;

    // Measure every monitor each way at this width, in a window kept behind
    // the main one, whose opaque background hides it and which takes the
    // mouse first. Each copy is drawn at the top of the screen, on top of
    // the others: ImGui stops laying out child windows it can't see, so a
    // copy pushed off the bottom would keep a stale size.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::Begin("##measure", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    std::vector<Panel> panels;
    ImVec2 origin = ImGui::GetCursorScreenPos();
    for (const auto& s : snaps) {
        ImGui::PushID(s.monitor.c_str());
        Panel p;
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushID("row");
        p.row = compactCard(s, width, Show::Row, 0, nullptr);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushID("full");
        p.full = compactCard(s, width, Show::Full, 0, nullptr);
        ImGui::PopID();
        ImGui::SetCursorScreenPos(origin);
        ImGui::PushID("clipped");
        p.overhead = compactCard(s, width, Show::Clipped, 1, nullptr) - 1 - p.row;
        ImGui::PopID();
        ImGui::PopID();
        p.pin = config_.expanded.count(s.monitor) ? Pin::Open : config_.collapsed.count(s.monitor) ? Pin::Closed : Pin::Auto;
        panels.push_back(p);
    }
    ImGui::End();

    // A panel only takes the space left over if it shows something useful
    // there: a table's header and four rows. Otherwise it stays a row.
    const ImGuiStyle& st = ImGui::GetStyle();
    float minBody = 5 * (ImGui::GetTextLineHeight() + st.CellPadding.y * 2);
    Fit fit = FitPanels(panels, height, gap, minBody);
    for (size_t i = 0; i < snaps.size(); i++) {
        bool clicked = false;
        compactCard(snaps[i], width, fit.show[i], fit.body, &clicked);
        ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y * 2));
        if (!clicked) continue;
        // Open or close it, keeping that choice only while it differs from
        // what would happen by itself, so a pin doesn't outlive its use.
        const std::string& id = snaps[i].monitor;
        bool open = fit.show[i] == Show::Row;
        panels[i].pin = Pin::Auto;
        bool automatic = FitPanels(panels, height, gap, minBody).show[i] != Show::Row;
        config_.expanded.erase(id);
        config_.collapsed.erase(id);
        if (open != automatic) (open ? config_.expanded : config_.collapsed).insert(id);
        SaveConfig(config_);
    }
}

// compactCard draws one monitor: collapsed, a row of its summary; open,
// its full panel. Either way the top line ends in the same two slots,
// refresh then the chevron in the corner, so they line up down the column;
// an open panel keeps that corner to itself. A clipped panel scrolls
// within body. It sets *clicked when the row (or an open panel's corner
// strip) is clicked; with no clicked, it's only being measured and ignores
// the mouse. It returns the card's height.
float App::compactCard(const Snapshot& s, float width, Show show, float body, bool* clicked) {
    const Palette& p = Colours();
    bool open = show != Show::Row;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.card);
    // Wide enough at the sides for the grip in the left margin.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18 * scale_, 8 * scale_));
    ImGui::BeginChild(s.monitor.c_str(), ImVec2(width, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 top = ImGui::GetCursorScreenPos();
    ImVec2 winMin = ImGui::GetWindowPos();
    float winRight = winMin.x + ImGui::GetWindowWidth();
    float avail = ImGui::GetContentRegionAvail().x;
    const ImGuiStyle& st = ImGui::GetStyle();
    bool local = IsLocal(s);
    bool failed = !s.error.empty();

    // The two slots at the right of the top line, each a button wide.
    ImGui::PushFont(fonts_.caption);
    float lineH = ImGui::GetFrameHeight();
    float slot = ImGui::CalcTextSize(icon::Refresh).x + st.FramePadding.x * 2;
    float cw = ImGui::CalcTextSize(icon::ChevronDown).x;
    ImGui::PopFont();
    float chevronX = top.x + avail - slot;
    float refreshX = chevronX - 2 * scale_ - slot;
    bool paste = !local && !s.paste.empty();
    float pasteX = refreshX - 2 * scale_ - slot;
    float controlsX = local ? chevronX : paste ? pasteX : refreshX;

    // The grip and buttons only show while the mouse is over the panel (or
    // it's being dragged); their places stay kept so nothing moves.
    bool shown = clicked && (dragging_ == s.monitor || (dragging_.empty() && hoveringCard()));
    bool onButton = grip(s, top.y + lineH / 2, shown);
    auto controls = [&](bool lit) {
        ImGui::PushFont(fonts_.caption);
        if (shown) {
            ImGui::SetCursorScreenPos(ImVec2(chevronX + (slot - cw) / 2, top.y + st.FramePadding.y));
            ImGui::TextColored(lit ? p.text : p.muted, "%s", open ? icon::ChevronUp : icon::ChevronDown);
        }
        if (!local) {
            ImGui::SetCursorScreenPos(ImVec2(refreshX, top.y));
            if (shown) {
                refreshButton(s);
                onButton = onButton || ImGui::IsItemHovered();
            } else {
                ImGui::Dummy(ImVec2(slot, lineH));
            }
        }
        if (paste) {
            ImGui::SetCursorScreenPos(ImVec2(pasteX, top.y));
            if (shown) {
                pasteButton(s);
                onButton = onButton || ImGui::IsItemHovered();
            } else {
                ImGui::Dummy(ImVec2(slot, lineH));
            }
        }
        ImGui::PopFont();
    };

    if (!open) {
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        controls(false);
        // When it was updated, left of the slots; only the server's
        // monitors have one, and it only shows on hover, where it matters
        // now. Its space is kept, so the summary doesn't move.
        float x = controlsX;
        // A short table is drawn as the open panel's body, which has no
        // such text: hover the refresh button for it.
        bool asPanel = !s.summaryBlock.empty();
        // Chips can have the room kept for it (it only shows on hover):
        // then it's left out if they reach it.
        float chipsW = avail - (top.x + avail - x + 16 * scale_);
        std::string when;
        bool hidden = true;
        if (!local && !asPanel) {
            bool pending = refreshing_.count(s.monitor) && ImGui::GetTime() - refreshing_[s.monitor] < 15;
            when = failed      ? std::string(icon::Warning) + "  Failed"
                   : pending   ? "refreshing…"
                   : s.updated ? (s.timely ? Ago(s.updated, now()) : "")
                               : "waiting…";
            hidden = !failed && !pending && s.updated && !shown;
            ImGui::PushFont(fonts_.caption);
            if (!when.empty()) x -= ImGui::CalcTextSize(when.c_str()).x + 4 * scale_;
            ImGui::PopFont();
        }
        float whenX = x;
        // drawWhen draws it, unless something on its line reaches it.
        auto drawWhen = [&](float reach) {
            if (when.empty() || hidden || reach > whenX - 8 * scale_) return;
            ImGui::PushFont(fonts_.caption);
            ImGui::SetCursorScreenPos(ImVec2(whenX, top.y + st.FramePadding.y));
            ImGui::TextColored(failed ? p.warn : p.muted, "%s", when.c_str());
            if (failed && clicked && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.error.c_str());
            ImGui::PopFont();
        };
        float rightW = top.x + avail - x + 16 * scale_;
        float bottom = top.y + lineH;

        // On the left: what's being watched, if the monitor says, then the
        // summary, which shares the controls' line when there's no
        // subtitle above it.
        float y = top.y;
        if (!s.subtitle.empty() && !asPanel) {
            ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + st.FramePadding.y));
            ImGui::PushFont(fonts_.caption);
            mutedText(fit(s.subtitle, avail - rightW));
            ImGui::PopFont();
            y = bottom;
        }
        ImGui::SetCursorScreenPos(ImVec2(top.x, y));
        if (asPanel) {
            // The panel's own look, cut short: placed, sized and measured
            // as the open panel is, so opening it only adds rows. In a
            // window of its own, as a table splits the draw list and this
            // row has split it already.
            ImGui::SetCursorScreenPos(top);
            ImGui::BeginChild("##short", ImVec2(avail - (top.x + avail - controlsX + 8 * scale_), 0),
                              ImGuiChildFlags_AutoResizeY,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            panelHead(s);
            localHovered_ = linkHovered = false;
            measure_ = fullTable(s, s.summaryBlock[0]);
            block(s.monitor, s.summaryBlock[0]);
            measure_ = nullptr;
            // A click on a link or local button is for it, not the row.
            onButton = onButton || localHovered_ || (clicked && linkHovered);
            ImGui::EndChild();
        } else if (!s.summary.empty()) {
            if (s.summaryStyle == "chips") {
                float w = y == top.y && !failed ? chipsW : avail - (y == top.y ? rightW : 0);
                onButton = chips(s.summary, w, clicked && dragging_.empty()) || onButton;
                drawWhen(y == top.y ? ImGui::GetItemRectMax().x : 0);
            } else {
                summary(s.summary, avail - (y == top.y ? rightW : 0));
                drawWhen(0);
            }
        } else if (s.blocks.empty() && !failed) {
            mutedText("Waiting for the first check…");
            drawWhen(0);
        } else {
            mutedText("Click for details");
            drawWhen(0);
        }
        bottom = std::max(bottom, ImGui::GetItemRectMax().y);

        // Clicking anywhere on the row, bar the buttons, links and grip, opens it.
        ImVec2 hitMin = winMin, hitMax(winRight, bottom + 8 * scale_);
        dl->ChannelsSetCurrent(0);
        bool hovered = clicked && dragging_.empty() && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                       ImGui::IsMouseHoveringRect(hitMin, hitMax) && !onButton;
        if (hovered) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            dl->AddRectFilled(hitMin, hitMax, u32(p.hover));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) *clicked = true;
        }
        dl->ChannelsMerge();
        ImGui::SetCursorScreenPos(ImVec2(top.x, bottom));
        ImGui::Dummy(ImVec2(avail, 0));
    } else {
        // The panel takes the width bar a strip down the right holding the
        // slots; clicking the strip closes it.
        float strip = top.x + avail - controlsX + 8 * scale_;
        ImVec2 hitMin(controlsX - 4 * scale_, winMin.y), hitMax(winRight, winMin.y + ImGui::GetWindowHeight());
        bool inStrip = clicked && dragging_.empty() && ImGui::IsWindowHovered() &&
                       ImGui::IsMouseHoveringRect(hitMin, hitMax);
        controls(inStrip && !onButton);
        if (inStrip && !onButton) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) *clicked = true;
        }

        ImGui::SetCursorScreenPos(top);
        bool clip = show == Show::Clipped;
        // Cut short, it scrolls with the wheel, without a scrollbar.
        ImGui::BeginChild("##body", ImVec2(avail - strip, clip ? body : 0),
                          clip ? ImGuiChildFlags_None : ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                              (clip ? 0 : ImGuiWindowFlags_NoScrollWithMouse));
        panelHead(s);
        for (size_t i = 0; i < s.blocks.size(); i++) {
            ImGui::PushID(int(i));
            if (i > 0) ImGui::Dummy(ImVec2(0, 2 * scale_));
            block(s.monitor, s.blocks[i]);
            ImGui::PopID();
        }
        ImGui::EndChild();
        float bottom = std::max(ImGui::GetItemRectMax().y, top.y + lineH);
        ImGui::SetCursorScreenPos(ImVec2(top.x, bottom));
        ImGui::Dummy(ImVec2(avail, 0));
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    bevel(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    if (clicked) cardRects_.push_back({s.monitor, ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax())});
    return ImGui::GetItemRectSize().y;
}

// panelHead draws what an open panel shows above its blocks: the subtitle
// and the last error. A short table's row draws it too, to match.
void App::panelHead(const Snapshot& s) {
    ImGui::PushFont(fonts_.caption);
    if (!s.subtitle.empty()) wrapped(s.subtitle, Colours().muted);
    if (!s.error.empty()) wrapped(std::string(icon::Warning) + "  " + s.error, Colours().warn);
    ImGui::PopFont();
}

// fullTable finds the panel's table that a short table is cut from (the
// same columns), so the short one's columns are as wide as the full one's.
const Block* App::fullTable(const Snapshot& s, const Block& b) {
    for (const auto& full : s.blocks) {
        if (full.type != "table" || full.columns.size() != b.columns.size()) continue;
        bool same = true;
        for (size_t c = 0; c < b.columns.size() && same; c++) same = full.columns[c].title == b.columns[c].title;
        if (same) return &full;
    }
    return nullptr;
}

// grip draws the six dots that drag a panel, in the card's left margin,
// centred on y, and starts a drag from them; when not shown it does
// nothing. It reports whether the mouse is over them.
bool App::grip(const Snapshot& s, float y, bool shown) {
    if (!shown) return false;
    const Palette& p = Colours();
    ImVec2 winMin = ImGui::GetWindowPos();
    float dot = std::max(2.0f, std::round(2 * scale_)), step = dot * 2;
    float x = winMin.x + std::round(6 * scale_), h = step * 2 + dot;
    ImRect hit(ImVec2(winMin.x, y - h / 2 - 4 * scale_), ImVec2(x + step + dot + 3 * scale_, y + h / 2 + 4 * scale_));

    // The margin is outside the window's clip rect, so widen it.
    ImGui::PushClipRect(winMin, ImVec2(winMin.x + ImGui::GetWindowWidth(), winMin.y + ImGui::GetWindowHeight()), false);
    bool hovered = false;
    bool dragging = dragging_ == s.monitor;
    {
        ImVec2 saved = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(hit.Min);
        ImGui::PushID("##grip");
        ImGui::InvisibleButton("##grip", hit.GetSize());
        ImGui::PopID();
        hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3)) dragging_ = s.monitor;
        if (hovered && !dragging) ImGui::SetTooltip("Drag to move");
        if (hovered || dragging) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        ImGui::SetCursorScreenPos(saved);
    }
    ImVec4 colour = hovered || dragging ? p.text : fade(p.muted, 0.6f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 2; col++) {
            ImVec2 a(x + col * step, std::round(y - h / 2 + row * step));
            dl->AddRectFilled(a, ImVec2(a.x + dot, a.y + dot), u32(colour));
        }
    ImGui::PopClipRect();
    return hovered;
}

// dragDrop shows where a dragged panel will land, before or after the card
// nearest the mouse, and moves it there on release.
void App::dragDrop() {
    if (dragging_.empty()) return;
    const Palette& p = Colours();
    ImVec2 m = ImGui::GetMousePos();
    const std::pair<std::string, ImRect>* target = nullptr;
    const ImRect* dragged = nullptr;
    float best = FLT_MAX;
    for (const auto& c : cardRects_) {
        if (c.first == dragging_) dragged = &c.second;
        float dx = std::max({c.second.Min.x - m.x, 0.0f, m.x - c.second.Max.x});
        float dy = std::max({c.second.Min.y - m.y, 0.0f, m.y - c.second.Max.y});
        if (dx * dx + dy * dy < best) best = dx * dx + dy * dy, target = &c;
    }
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (dragged) fg->AddRectFilled(dragged->Min, dragged->Max, u32(fade(p.bg, 0.6f)));
    bool after = false;
    if (target) {
        const ImRect& r = target->second;
        after = m.y > (r.Min.y + r.Max.y) / 2;
        float y = after ? r.Max.y + 4 * scale_ : r.Min.y - 4 * scale_;
        float t = std::max(2.0f, std::round(2 * scale_));
        fg->AddRectFilled(ImVec2(r.Min.x, y - t / 2), ImVec2(r.Max.x, y + t / 2), u32(p.accent));
    }
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;

    // Dropped.
    if (target) {
        std::vector<std::string> order;
        for (const auto& s : panels()) order.push_back(s.monitor);
        config_.order = Move(order, dragging_, target->first, after);
        SaveConfig(config_);
    }
    dragging_.clear();
}

// summary lays stats out as tiles of label, value and detail, flowing onto
// more lines when they don't fit in width.
// chips lays stats out as small blocks filled with their tone's colour:
// the label (which may be several lines) above the value and any detail
// below, all one size so they line up. If they nearly fit on one line,
// they narrow to do so, shortening details that are too wide, though a chip
// whose label or value wouldn't fit keeps its own width; if labels or
// values would still be cut short, they stay on one line at their full
// width, which scrolls sideways with the mouse wheel past any lead chips
// at its start. A lead chip
// has no block, just its label and value on one line, as wide as they
// are, with more room after it.
bool App::chips(const std::vector<Stat>& stats, float width, bool live) {
    if (stats.empty()) return false;
    const Palette& p = Colours();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pad(6 * scale_, 4 * scale_);
    float gap = 4 * scale_;
    float leadGap = 10 * scale_;

    // One size for all, from the widest and tallest of each part. need is
    // what each chip's label and value take: a chip that can't fit them
    // in the shared width gets its own, e.g. a five-figure day by the
    // watchlist's percentages.
    float w = 0, labelH = 0, valueH = 0, detailH = 0;
    float beside = 0;  // the widest label or value with an icon by it
    bool icons = false;
    std::vector<float> need(stats.size());
    std::vector<float> own(stats.size());  // a lead chip's width, else 0
    float fixed = 0;                       // the lead chips' together
    size_t leads = 0;
    for (size_t i = 0; i < stats.size(); i++) {
        const Stat& s = stats[i];
        ImGui::PushFont(fonts_.caption);
        ImVec2 l = ImGui::CalcTextSize(s.label.c_str());
        if (s.lead) {
            labelH = std::max(labelH, l.y);
            ImGui::PopFont();
            ImGui::PushFont(fonts_.heading);
            ImVec2 v = ImGui::CalcTextSize(s.value.c_str());
            if (!s.value.empty()) valueH = std::max(valueH, v.y);
            ImGui::PopFont();
            own[i] = std::min(l.x + gap * 1.5f + v.x, width);
            fixed += own[i], leads++;
            continue;
        }
        w = std::max(w, l.x);
        need[i] = l.x;
        if (!s.label.empty()) labelH = std::max(labelH, l.y);  // none at all: no line for them
        if (!s.detail.empty()) {
            ImVec2 d = ImGui::CalcTextSize(s.detail.c_str());
            w = std::max(w, d.x);
            detailH = std::max(detailH, d.y);
        }
        ImGui::PopFont();
        ImGui::PushFont(fonts_.heading);
        ImVec2 v = ImGui::CalcTextSize(s.value.c_str());
        w = std::max(w, v.x);
        need[i] = std::max(need[i], v.x);
        // An icon, if any, goes right of the label and value, as tall as
        // both if there's room, as tall as the value at least.
        if (IsWeatherIcon(s.icon)) {
            icons = true;
            ImGui::PushFont(fonts_.caption);
            beside = std::max({beside, v.x, ImGui::CalcTextSize(s.label.c_str()).x});
            ImGui::PopFont();
            w = std::max(w, v.x + gap + v.y * 1.2f);
            need[i] = std::max(need[i], v.x + gap + v.y * 1.2f);
        }
        if (!s.value.empty()) valueH = std::max(valueH, v.y);
        ImGui::PopFont();
    }
    if (icons) w = std::max(w, beside + gap + labelH + valueH);
    w += pad.x * 2;
    const size_t n = stats.size();
    // The room after each chip.
    std::vector<float> after(n, gap);
    float gaps = 0;
    for (size_t i = 0; i < n; i++) {
        if (stats[i].lead && i + 1 < n) after[i] += leadGap;
        if (i + 1 < n) gaps += after[i];
    }
    for (size_t i = 0; i < n; i++) need[i] = own[i] > 0 ? own[i] : std::min(need[i] + pad.x * 2, width);
    const size_t shared = n - leads;  // the chips sharing a width
    float oneRow = shared > 0 ? (width - gaps - fixed) / float(shared) : width;
    // Each chip's width: the shared one, or its own if it needs more.
    std::vector<float> ws(n);
    for (size_t i = 0; i < n; i++) ws[i] = own[i] > 0 ? own[i] : std::min(w, width);
    float natural = gaps;
    for (float x : ws) natural += x;
    if (natural > width + 0.5f && shared > 0 && oneRow > 0) {
        // All on one row: the chips that need more than an equal share
        // take it, and the rest share what's left. Short ones (the
        // dailies' "W", "C") can go narrow, as long as each still fits.
        float share = oneRow;
        for (size_t pass = 0; pass < n; pass++) {
            float taken = 0;
            size_t wide = 0;
            for (size_t i = 0; i < n; i++)
                if (own[i] == 0 && need[i] > share) taken += need[i], wide++;
            if (wide == shared) break;
            float next = (width - gaps - fixed - taken) / float(shared - wide);
            if (next == share) break;
            share = next;
        }
        // If that still leaves a label or value cut short, they flow onto
        // more lines at their full width instead.
        float total = gaps + fixed;
        for (size_t i = 0; i < n; i++)
            if (own[i] == 0) total += std::max(share, need[i]);
        if (total <= width + 0.5f) {
            w = share;
            for (size_t i = 0; i < n; i++)
                if (own[i] == 0) ws[i] = std::max(share, need[i]);
        }
    }
    float h = pad.y * 2 + labelH + valueH + detailH;
    float iconSize = std::round(std::clamp(w - pad.x * 2 - beside - gap, valueH * 1.2f, labelH + valueH));

    // Scrolling anyway, a few long labels (an index's full name) needn't
    // widen the rest: they share what most of them need, and longer ones
    // take their own.
    {
        float natural = 0;
        for (size_t i = 0; i < n; i++) natural += ws[i] + (i + 1 < n ? after[i] : 0);
        std::vector<float> needs;
        for (size_t i = 0; i < n; i++)
            if (own[i] == 0) needs.push_back(need[i]);
        if (natural > width + 0.5f && !needs.empty()) {
            std::sort(needs.begin(), needs.end());
            w = needs[(needs.size() - 1) * 4 / 5];
            for (size_t i = 0; i < n; i++)
                if (own[i] == 0) ws[i] = std::max(w, need[i]);
        }
    }

    // Where each chip goes, in one row; row[i] is whether chip i starts
    // one. A pixel out to the left: a faint block's edge looks further in
    // than the bright text it lines up with in other panels.
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.x -= std::round(scale_);
    std::vector<ImVec2> at(n);
    std::vector<bool> row(n);
    float rowW = 0;
    for (size_t i = 0; i < n; i++) {
        row[i] = rowW == 0;
        at[i] = ImVec2(origin.x + rowW, origin.y);
        rowW += ws[i] + (i + 1 < n ? after[i] : 0);
    }
    // Too wide for the line: they keep their size and the line scrolls
    // sideways, in a window of its own, with the wheel while it's hovered.
    // Lead chips at the start (the watchlist's day) stay put.
    const bool scroll = rowW > width + 0.5f;
    size_t pinned = 0;
    if (scroll)
        while (pinned < n && stats[pinned].lead) pinned++;
    const float pinW = pinned < n ? at[pinned].x - origin.x : 0;
    bool levels = std::any_of(stats.begin(), stats.end(), [](const Stat& s) { return s.hasLevel; });
    bool onLink = false;
    ImGui::BeginGroup();
    float shift = 0;  // where the scrolling chips are drawn, from where they'd be
    for (size_t i = 0; i < n; i++) {
        if (scroll && i == pinned) {
            ImGui::SetCursorScreenPos(at[i]);
            ImGui::BeginChild("##chips", ImVec2(width - pinW, h), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
                                  ImGuiWindowFlags_NoScrollWithMouse);
            dl = ImGui::GetWindowDrawList();
            if (ImGui::IsWindowHovered()) {
                // Keep the wheel from the dashboard while it's ours;
                // Frame() gives it back once the chips aren't hovered.
                ImGuiIO& io = ImGui::GetIO();
                ImGui::SetKeyOwner(ImGuiKey_MouseWheelY, ChipsWheel);
                wheelHeld_ = ImGui::GetFrameCount();
                float step = (w + gap) * 2;
                ImGui::SetScrollX(ImGui::GetScrollX() - (io.MouseWheel + io.MouseWheelH) * step);
            }
            shift = ImGui::GetWindowPos().x - ImGui::GetScrollX() - at[i].x;
        }
        const Stat& s = stats[i];
        ImVec2 a(at[i].x + shift, at[i].y);
        float w = ws[i];
        ImVec4 tone = ToneColour(s.tone);
        bool coloured = s.tone == Tone::Good || s.tone == Tone::Bad || s.tone == Tone::Warn || s.tone == Tone::Accent;
        if (s.lead) {
            // The value centred in the row's height, the label before it
            // on the same line, both resting on its bottom.
            float y = a.y + std::round((h - valueH) / 2);
            ImGui::PushFont(fonts_.caption);
            float labelW = ImGui::CalcTextSize(s.label.c_str()).x;
            ImGui::SetCursorScreenPos(ImVec2(a.x, y + std::round(valueH - labelH - valueH * 0.08f)));
            ImGui::TextColored(s.tone == Tone::Muted ? p.muted : p.text, "%s", s.label.c_str());
            ImGui::PopFont();
            ImGui::PushFont(fonts_.heading);
            ImGui::SetCursorScreenPos(ImVec2(a.x + labelW + gap * 1.5f, y));
            ImGui::TextColored(coloured ? tone : s.tone == Tone::Muted ? p.muted : p.text, "%s", s.value.c_str());
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(a);
            ImGui::Dummy(ImVec2(w, h));
            continue;
        }
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), u32(coloured ? fade(tone, 0.2f) : p.frame));
        if (live && !s.url.empty() && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, ImVec2(a.x + w, a.y + h))) {
            dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), u32(fade(p.text, 0.12f)));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) OpenURL(s.url);
            onLink = true;
        }
        float inner = w - pad.x * 2;
        if (levels) {
            // The levels as one faint chart behind the row: through the
            // middle of each block at its own level, and at each edge
            // halfway to its neighbour's.
            auto lv = [&](size_t j) { return stats[j].level; };
            float mid = lv(i);
            float left = row[i] ? mid : (lv(i - 1) + mid) / 2;
            float right = i + 1 == n || row[i + 1] ? mid : (lv(i + 1) + mid) / 2;
            constexpr int steps = 10;
            ImVec2 pts[steps * 2 + 1];
            for (int k = 0; k <= steps * 2; k++) {
                float u = float(k) / (steps * 2);
                float from = k <= steps ? left : mid, to = k <= steps ? mid : right;
                float e = (1 - std::cos(float(k <= steps ? k : k - steps) / steps * 3.14159265f)) / 2;  // eased
                pts[k] = ImVec2(a.x + u * w, a.y + h - (from + (to - from) * e) * h);
            }
            ImU32 fill = u32(fade(p.accent, 0.13f)), line = u32(fade(p.accent, 0.5f));
            float thick = 1.25f * scale_, floor = a.y + h - thick / 2;
            for (int k = 0; k < steps * 2; k++) {
                ImVec2 q0 = pts[k], q1 = pts[k + 1];
                if (q0.y < a.y + h - 0.5f || q1.y < a.y + h - 0.5f) {
                    ImVec2 quad[4] = {q0, q1, ImVec2(q1.x, a.y + h), ImVec2(q0.x, a.y + h)};
                    dl->AddConvexPolyFilled(quad, 4, fill);
                }
                // At 0% the line still shows, along the bottom.
                q0.y = std::min(q0.y, floor), q1.y = std::min(q1.y, floor);
                dl->AddLine(q0, q1, line, thick);
            }
        }
        ImGui::PushFont(fonts_.caption);
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad.x, a.y + pad.y));
        ImGui::TextColored(s.tone == Tone::Muted ? p.muted : p.text, "%s", fitLines(s.label, inner).c_str());
        ImGui::PopFont();
        bool icon = IsWeatherIcon(s.icon);
        ImGui::PushFont(fonts_.heading);
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad.x, a.y + pad.y + labelH));
        ImGui::TextColored(coloured ? tone : s.tone == Tone::Muted ? p.muted : p.text, "%s",
                           fit(s.value, inner - (icon ? iconSize + gap : 0)).c_str());
        ImGui::PopFont();
        if (icon) weatherIcon(s.icon, ImVec2(a.x + w - pad.x - iconSize, a.y + pad.y + labelH + valueH - iconSize), iconSize);
        if (!s.detail.empty()) {
            ImGui::PushFont(fonts_.caption);
            ImGui::SetCursorScreenPos(ImVec2(a.x + pad.x, a.y + pad.y + labelH + valueH));
            ImGui::TextColored(p.muted, "%s", fitLines(s.detail, inner).c_str());
            ImGui::PopFont();
        }
        // The block itself, for the group's size.
        ImGui::SetCursorScreenPos(a);
        ImGui::Dummy(ImVec2(w, h));
    }
    if (scroll && pinned < n) ImGui::EndChild();
    ImGui::EndGroup();
    return onLink;
}

void App::summary(const std::vector<Stat>& stats, float width) {
    const Palette& p = Colours();
    bool details = std::any_of(stats.begin(), stats.end(), [](const Stat& s) { return !s.detail.empty(); });
    float spacing = 24 * scale_;
    // A bar tile: its value beside its label, and the bar across the tile.
    float barMin = std::round(48 * scale_), barH = std::max(4.0f, std::round(5 * scale_)), barGap = 6 * scale_;
    float groupGap = 12 * scale_;  // the extra room before a new group
    ImVec2 origin = ImGui::GetCursorScreenPos();
    float x = 0, y = 0, lineH = 0;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::BeginGroup();
    for (size_t i = 0; i < stats.size(); i++) {
        const Stat& s = stats[i];
        ImGui::PushFont(fonts_.caption);
        std::string label = fit(s.label, width), detail = fit(s.detail, width);
        float labelW = ImGui::CalcTextSize(label.c_str()).x;
        float w = std::max({labelW, ImGui::CalcTextSize(detail.c_str()).x, ImGui::CalcTextSize(s.widestDetail.c_str()).x});
        float captionH = ImGui::GetTextLineHeight();
        std::string value = fit(s.value, width);
        if (s.bar) {
            float valueW = std::max(ImGui::CalcTextSize(value.c_str()).x, ImGui::CalcTextSize(s.widestValue.c_str()).x);
            w = std::max({w, labelW + barGap + valueW, barMin});
        }
        ImGui::PopFont();
        ImGui::PushFont(fonts_.heading);
        float valueH = ImGui::GetTextLineHeight();
        if (!s.bar) w = std::max({w, ImGui::CalcTextSize(value.c_str()).x, ImGui::CalcTextSize(s.widestValue.c_str()).x});
        ImGui::PopFont();
        // Room kept for figures that change, short of the whole line.
        w = std::min(w, width);

        bool group = s.group && x > 0;
        if (x > 0 && x + (group ? groupGap : 0) + w > width) x = 0, y += lineH + 4 * scale_, lineH = 0, group = false;
        if (group) {
            // A faint rule midway between the groups.
            float rx = std::round(origin.x + x - spacing / 2 + groupGap / 2);
            float h = captionH + valueH + (details ? captionH : 0);
            dl->AddLine(ImVec2(rx, origin.y + y + 2 * scale_), ImVec2(rx, origin.y + y + h - 2 * scale_), u32(p.border),
                        std::max(1.0f, std::round(scale_)));
            x += groupGap;
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x + x, origin.y + y));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts_.caption);
        mutedText(label);
        if (s.bar) {
            // The value beside the label, right-aligned in the tile.
            ImGui::SameLine(0, 0);
            float vw = ImGui::CalcTextSize(value.c_str()).x;
            ImGui::SetCursorScreenPos(ImVec2(origin.x + x + w - vw, ImGui::GetCursorScreenPos().y));
            ImGui::TextColored(ToneColour(s.tone), "%s", value.c_str());
        }
        ImGui::PopFont();
        ImGui::PushFont(fonts_.heading);
        if (s.bar) {
            // How full, as a bar across the tile: in the accent normally, the
            // tone's colour as it fills up.
            ImGui::SetCursorScreenPos(ImVec2(origin.x + x, ImGui::GetCursorScreenPos().y));
            ImVec2 at = ImGui::GetCursorScreenPos();
            float by = std::round(at.y + (valueH - barH) / 2);
            ImVec4 fill = s.tone == Tone::Warn || s.tone == Tone::Bad ? ToneColour(s.tone)
                          : s.tone == Tone::Muted                     ? p.muted
                                                                      : p.accent;
            dl->AddRectFilled(ImVec2(at.x, by), ImVec2(at.x + w, by + barH), u32(p.frame));
            dl->AddRectFilled(ImVec2(at.x, by), ImVec2(at.x + std::round(w * s.level), by + barH), u32(fill));
            ImGui::Dummy(ImVec2(w, valueH));
        } else {
            ImGui::TextColored(ToneColour(s.tone), "%s", value.c_str());
        }
        ImGui::PopFont();
        if (details) {
            ImGui::PushFont(fonts_.caption);
            mutedText(detail.empty() ? " " : detail);
            ImGui::PopFont();
        }
        ImGui::EndGroup();
        lineH = std::max(lineH, ImGui::GetItemRectSize().y);
        x += w + spacing;
    }
    ImGui::EndGroup();
}

void App::block(const std::string& monitor, const Block& b) {
    if (!b.title.empty() && b.type != "stats") {
        ImGui::PushFont(fonts_.caption);
        ImGui::TextColored(Colours().blockTitle, "%s", b.title.c_str());
        ImGui::PopFont();
    }
    if (b.type == "stats") stats(b);
    else if (b.type == "table") table(monitor, b);
    else if (b.type == "list") list(b);
    else if (b.type == "checklist") checklist(monitor, b);
    else if (b.type == "media") mediaBlock(b);
    else if (!b.text.empty()) wrapped(b.text, Colours().text);
}

// mediaBlock draws what's playing: the cover, the track, a progress bar
// that moves on between readings, and previous, play/pause and next.
void App::mediaBlock(const Block& b) {
    if (b.items.empty()) return;
    const Item& it = b.items[0];
    const Media& m = b.media;
    const Palette& p = Colours();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float art = 76 * scale_, gap = 12 * scale_;

    ImVec2 at = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 to(at.x + art, at.y + art);
    if (const Texture* t = images_->Get(it.image)) {
        // Crop to a square rather than squash.
        float ta = float(t->width) / float(t->height);
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (ta > 1) uv0.x = (1 - 1 / ta) / 2, uv1.x = 1 - uv0.x;
        else uv0.y = (1 - ta) / 2, uv1.y = 1 - uv0.y;
        dl->AddImage((ImTextureID)(intptr_t)t->srv, at, to, uv0, uv1);
    } else {
        dl->AddRectFilled(at, to, u32(p.frame));
    }
    ImGui::Dummy(ImVec2(art, art));
    ImGui::SameLine(0, gap);

    float w = avail - art - gap;
    ImGui::BeginGroup();
    ImGui::PushFont(fonts_.heading);
    ImGui::TextUnformatted(fit(it.title, w).c_str());
    ImGui::PopFont();
    if (!it.subtitle.empty()) ImGui::TextUnformatted(fit(it.subtitle, w).c_str());
    ImGui::PushFont(fonts_.caption);
    if (!it.detail.empty()) mutedText(fit(it.detail, w));

    // Progress, moved on by the time since Windows last said where it was.
    if (m.duration > 0) {
        double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        double pos = std::clamp(m.position + (m.playing ? now - m.asOf : 0), 0.0, m.duration);
        std::string times = FormatTime(pos) + " / " + FormatTime(m.duration);
        float tw = ImGui::CalcTextSize(times.c_str()).x;
        float barW = std::max(w - tw - gap, 20 * scale_);
        ImVec2 bar = ImGui::GetCursorScreenPos();
        float y = std::round(bar.y + ImGui::GetTextLineHeight() / 2);
        float h = std::max(2.0f, std::round(3 * scale_));
        dl->AddRectFilled(ImVec2(bar.x, y - h / 2), ImVec2(bar.x + barW, y + h / 2), u32(p.frame));
        dl->AddRectFilled(ImVec2(bar.x, y - h / 2), ImVec2(bar.x + barW * float(pos / m.duration), y + h / 2),
                          u32(m.playing ? p.accent : p.muted));
        ImGui::Dummy(ImVec2(barW, ImGui::GetTextLineHeight()));
        ImGui::SameLine(0, gap);
        mutedText(times);
    }
    ImGui::PopFont();

    // Controls, flat and dim until hovered, like refresh.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
    auto control = [&](const char* glyph, bool enabled, MediaCommand c, const char* tip) {
        ImGui::BeginDisabled(!enabled);
        if (ImGui::Button(glyph)) media_->Command(c);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::EndDisabled();
    };
    control(icon::Previous, m.canPrevious, MediaCommand::Previous, "Previous");
    ImGui::SameLine(0, 2 * scale_);
    control(m.playing ? icon::Pause : icon::Play, m.canToggle, MediaCommand::TogglePlay, m.playing ? "Pause" : "Play");
    ImGui::SameLine(0, 2 * scale_);
    control(icon::Next, m.canNext, MediaCommand::Next, "Next");
    ImGui::PopStyleColor(2);
    ImGui::EndGroup();
}

// checklist draws items with checkboxes, up, down and delete buttons on
// the one under the mouse (as the block has them), and a box for new
// items if the block takes them. Items with a slider or colour (lights)
// get them in a column after the titles, and no up and down buttons. Each
// change is sent to the monitor as an action; ticks, slider values and
// colours show at once, ahead of the server's snapshot.
void App::checklist(const std::string& monitor, const Block& b) {
    const Palette& p = Colours();
    const ImGuiStyle& st = ImGui::GetStyle();
    auto act = [&](nlohmann::json action) { client_->Act(monitor, action.dump()); };
    float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(fonts_.caption);
    float bw = ImGui::CalcTextSize(icon::Close).x + st.FramePadding.x * 2;
    ImGui::PopFont();
    // Items with controls lose their up and down buttons.
    auto buttonsFor = [&](const Item& it) {
        bool controls = it.slider.present || it.colour.present;
        return (b.movable && !controls ? 2 : 0) + (b.noDelete ? 0 : 1);
    };
    // The titles' column, when any item has controls: as wide as the
    // longest, up to 45% of the row.
    float titleW = 0;
    for (const Item& it : b.items)
        if (it.slider.present || it.colour.present) titleW = std::max(titleW, 1.0f);
    if (titleW > 0) {
        for (const Item& it : b.items) titleW = std::max(titleW, ImGui::CalcTextSize(it.title.c_str()).x);
        titleW = std::min(titleW, ImGui::GetContentRegionAvail().x * 0.45f);
    }
    double now = ImGui::GetTime();

    for (const Item& it : b.items) {
        ImGui::PushID(it.id.c_str());
        std::string key = monitor + "/" + it.id;
        bool done = it.done;
        if (auto t = ticked_.find(key); t != ticked_.end() && now - t->second.second < 10)
            done = t->second.first;
        bool controls = it.slider.present || it.colour.present;
        int buttons = buttonsFor(it);
        float bsw = buttons * bw;  // the hover buttons' width

        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        bool toggle = false;
        bool v = done;
        if (ImGui::Checkbox("##done", &v)) toggle = true;
        ImGui::SameLine();
        ImVec4 titleColour = done ? p.muted : it.tone == Tone::None ? p.text : ToneColour(it.tone);
        if (controls) {
            // One line, cut to the column, so the controls line up.
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(titleColour, "%s", fit(it.title, titleW).c_str());
        } else {
            // The text wraps short of the buttons, and clicking it ticks
            // the box too.
            ImGui::PushTextWrapPos(right - bsw - st.ItemSpacing.x - ImGui::GetWindowPos().x);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(titleColour, "%s", it.title.c_str());
            ImGui::PopTextWrapPos();
        }
        ImVec2 textMin = ImGui::GetItemRectMin(), textMax = ImGui::GetItemRectMax();
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) toggle = true;
        if (done && textMax.y - textMin.y < ImGui::GetTextLineHeight() * 1.5f) {
            float y = std::round((textMin.y + textMax.y) / 2);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(textMin.x, y), ImVec2(textMax.x, y), u32(p.muted));
        }
        ImVec2 next = ImGui::GetCursorScreenPos();
        float rowBottom = std::max(textMax.y, ImGui::GetItemRectMax().y);
        if (controls) {
            float x = textMin.x + titleW + st.ItemSpacing.x * 2;
            itemControls(monitor, key, it, done, ImVec2(x, rowMin.y), right - bsw - x, rowBottom - rowMin.y);
            ImGui::SetCursorScreenPos(next);
        }

        // Allowing for an active item keeps the button there while it's
        // held, so the click completes.
        if (buttons > 0 && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseHoveringRect(rowMin, ImVec2(right, rowBottom))) {
            ImGui::PushFont(fonts_.caption);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Text, p.muted);
            float x = right - bsw;
            auto button = [&](const char* glyph, const char* id, const std::string& hint, const char* fallback) {
                ImGui::SetCursorScreenPos(ImVec2(x, rowMin.y));
                x += bw;
                bool clicked = ImGui::Button((std::string(glyph) + id).c_str(), ImVec2(bw, 0));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hint.empty() ? fallback : hint.c_str());
                return clicked;
            };
            // Ticked items stay in the order they were ticked.
            if (b.movable && !controls) {
                ImGui::BeginDisabled(done);
                if (button(icon::Up, "##up", b.upHint, "Move up")) act({{"type", "move"}, {"id", it.id}, {"text", "up"}});
                if (button(icon::Down, "##down", b.downHint, "Move down")) act({{"type", "move"}, {"id", it.id}, {"text", "down"}});
                ImGui::EndDisabled();
            }
            if (!b.noDelete && button(icon::Close, "##delete", b.deleteHint, "Delete")) act({{"type", "delete"}, {"id", it.id}});
            ImGui::PopStyleColor(2);
            ImGui::PopFont();
            ImGui::SetCursorScreenPos(next);
        }
        if (toggle) {
            ticked_[key] = {!done, ImGui::GetTime()};
            act({{"type", "toggle"}, {"id", it.id}});
        }
        ImGui::PopID();
    }
    if (b.items.empty()) mutedText("Nothing to do.");

    if (!b.addHint.empty()) {
        auto& buf = newItem_[monitor];
        ImGui::Dummy(ImVec2(0, 2 * scale_));
        ImGui::SetNextItemWidth(right - ImGui::GetCursorScreenPos().x);
        if (ImGui::InputTextWithHint("##add", b.addHint.c_str(), buf.data(), buf.size(),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            std::string text = buf.data();
            if (text.find_first_not_of(" \t") != std::string::npos) act({{"type", "add"}, {"text", text}});
            buf[0] = 0;
            ImGui::SetKeyboardFocusHere(-1);  // stay in the box for the next one
        }
    }
}

// itemControls draws a checklist item's slider and colour swatch in w x h
// at pos: the slider takes the width the swatch leaves. Letting go of the
// slider, or picking a colour, sends the item's action.
void App::itemControls(const std::string& monitor, const std::string& key, const Item& it, bool done, ImVec2 pos,
                       float w, float h) {
    const Palette& p = Colours();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    double now = ImGui::GetTime();
    auto act = [&](const std::string& type, const std::string& text) {
        client_->Act(monitor, nlohmann::json{{"type", type}, {"id", it.id}, {"text", text}}.dump());
    };

    // The colour now: one just picked, else the server's.
    std::string colour = it.colour.value;
    if (auto c = colourSet_.find(key); c != colourSet_.end() && now - c->second.second < 10) colour = c->second.first;
    float rgb[3];
    bool hasColour = ParseHexColour(colour, rgb);

    float sw = it.colour.present ? std::round(h * 0.7f) : 0;  // the swatch
    float gap = std::round(8 * scale_);
    if (it.slider.present) {
        float sliderW = w - (sw > 0 ? sw + gap : 0);
        int value = it.slider.value;
        if (auto l = levelSet_.find(key); l != levelSet_.end() && now - l->second.second < 10) value = l->second.first;
        if (sliderW > 20 * scale_) {
            ImGui::SetCursorScreenPos(pos);
            ImGui::InvisibleButton("##level", ImVec2(sliderW, h));
            float knob = std::round(5 * scale_);
            float x0 = pos.x + knob, x1 = pos.x + sliderW - knob;
            if (ImGui::IsItemActive()) {
                float t = (ImGui::GetIO().MousePos.x - x0) / std::max(1.0f, x1 - x0);
                value = std::clamp(int(std::lround(t * 100)), 0, 100);
                levelSet_[key] = {value, now};
            }
            if (ImGui::IsItemDeactivated() && value != it.slider.value) {
                act(it.slider.action, std::to_string(value));
                ticked_[key] = {value == 0, now};
                levelSet_[key] = {value, now};
            }
            if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("%s %d%%", it.slider.hint.empty() ? "Level" : it.slider.hint.c_str(), value);
            }
            // A thin track, filled in the light's colour up to the knob.
            ImVec4 fill = hasColour ? ImVec4(rgb[0], rgb[1], rgb[2], 1) : p.accent;
            if (value == 0 || done) fill = p.muted;
            float y = std::round(pos.y + h / 2), th = std::max(2.0f, std::round(3 * scale_));
            float kx = x0 + (x1 - x0) * value / 100.0f;
            dl->AddRectFilled(ImVec2(x0, y - th / 2), ImVec2(x1, y + th / 2), u32(p.frame), th / 2);
            dl->AddRectFilled(ImVec2(x0, y - th / 2), ImVec2(kx, y + th / 2), u32(fill), th / 2);
            dl->AddCircleFilled(ImVec2(kx, y), knob, u32(fill));
            dl->AddCircle(ImVec2(kx, y), knob, u32(p.card), 0, std::max(1.0f, scale_));
        }
    }

    if (!it.colour.present) return;
    ImVec2 sp(pos.x + w - sw, std::round(pos.y + (h - sw) / 2));
    ImGui::SetCursorScreenPos(sp);
    if (ImGui::InvisibleButton("##colour", ImVec2(sw, sw))) {
        if (!hasColour) rgb[0] = rgb[1] = rgb[2] = 1;
        std::copy(rgb, rgb + 3, pickColour_);
        ImGui::OpenPopup("##colours");
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("Colour");
    }
    float round = std::round(3 * scale_);
    if (hasColour && !done)
        dl->AddRectFilled(sp, ImVec2(sp.x + sw, sp.y + sw), u32(ImVec4(rgb[0], rgb[1], rgb[2], 1)), round);
    dl->AddRect(sp, ImVec2(sp.x + sw, sp.y + sw), u32(ImGui::IsItemHovered() ? p.text : p.muted), round, 0,
                std::max(1.0f, scale_));

    if (ImGui::BeginPopup("##colours")) {
        auto pick = [&](const std::string& value, const std::string& show) {
            act(it.colour.action, value);
            colourSet_[key] = {show, now};
            ticked_[key] = {false, now};  // picking a colour turns a light on
        };
        // Presets in rows of six, the whites (sent as kelvin, not
        // "#rrggbb") on rows of their own above the hues.
        float ps = std::round(26 * scale_);
        int n = 0, col = 0;
        bool lastWhite = false;
        for (const Swatch& s : it.colour.presets) {
            float c[3];
            if (!ParseHexColour(s.show, c)) continue;
            bool white = s.value.empty() || s.value[0] != '#';
            if (n > 0 && white != lastWhite) col = 0;
            if (col++ % 6 != 0) ImGui::SameLine();
            lastWhite = white;
            ImGui::PushID(n++);
            if (ImGui::ColorButton(s.label.c_str(), ImVec4(c[0], c[1], c[2], 1),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop, ImVec2(ps, ps))) {
                pick(s.value, s.show);
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.label.c_str());
            ImGui::PopID();
        }
        if (it.colour.wheel) {
            if (n > 0) ImGui::Dummy(ImVec2(0, gap / 2));
            ImGui::SetNextItemWidth(6 * ps + 5 * ImGui::GetStyle().ItemSpacing.x);
            ImGui::ColorPicker3("##wheel", pickColour_,
                                ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoInputs |
                                    ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoLabel |
                                    ImGuiColorEditFlags_NoAlpha);
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                // Only hue and saturation count: brightness is the slider's.
                float hh, ss, vv;
                ImGui::ColorConvertRGBtoHSV(pickColour_[0], pickColour_[1], pickColour_[2], hh, ss, vv);
                ImGui::ColorConvertHSVtoRGB(hh, ss, 1, pickColour_[0], pickColour_[1], pickColour_[2]);
                std::string hex = HexColour(pickColour_);
                pick(hex, hex);
            }
        }
        ImGui::EndPopup();
    }
}

void App::stats(const Block& b) {
    if (!b.title.empty()) {
        ImGui::PushFont(fonts_.caption);
        ImGui::TextColored(Colours().blockTitle, "%s", b.title.c_str());
        ImGui::PopFont();
    }
    size_t n = b.stats.size();
    if (n == 0) return;
    float avail = ImGui::GetContentRegionAvail().x;
    float spacing = 12 * scale_;
    size_t perRow = std::clamp<size_t>(size_t((avail + spacing) / (96 * scale_ + spacing)), 1, n);
    float tileW = (avail - spacing * (perRow - 1)) / perRow;
    for (size_t i = 0; i < n; i++) {
        const Stat& s = b.stats[i];
        if (i % perRow != 0) ImGui::SameLine(0, spacing);
        ImGui::BeginGroup();
        ImGui::PushFont(fonts_.caption);
        mutedText(s.label);
        ImGui::PopFont();
        ImGui::PushFont(fonts_.big);
        ImGui::TextColored(ToneColour(s.tone), "%s", s.value.c_str());
        ImGui::PopFont();
        if (IsWeatherIcon(s.icon)) {
            ImVec2 vmin = ImGui::GetItemRectMin(), vmax = ImGui::GetItemRectMax();
            float size = std::round((vmax.y - vmin.y) * 0.95f);
            weatherIcon(s.icon, ImVec2(vmax.x + 8 * scale_, vmax.y - size), size);
        }
        // A flat series, e.g. no rain at all, says nothing worth a chart,
        // unless its scale is fixed: memory steady at 50% still shows.
        bool flat = s.series.empty() ||
                    *std::max_element(s.series.begin(), s.series.end()) == *std::min_element(s.series.begin(), s.series.end());
        if (!flat || (s.top > 0 && !s.series.empty())) {
            sparkline(s.series, ImVec2(tileW, 22 * scale_), s.tone == Tone::None ? Colours().accent : ToneColour(s.tone),
                      s.top);
        }
        ImGui::PushFont(fonts_.caption);
        mutedText(s.detail.empty() ? " " : s.detail);
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(tileW, 0));
        ImGui::EndGroup();
    }
    ImGui::Dummy(ImVec2(0, 4 * scale_));
}

void App::table(const std::string& monitor, const Block& b) {
    size_t cols = b.columns.size();
    if (cols == 0) return;
    // The column marked fill takes the spare width, or failing that the
    // left-aligned column with the most text. The rest fit their contents.
    // A short table takes its column widths from the full one.
    const std::vector<std::vector<Cell>>& sized = measure_ ? measure_->rows : b.rows;
    auto fill = std::find_if(b.columns.begin(), b.columns.end(), [](const Column& c) { return c.fill; });
    int stretch = fill != b.columns.end() ? int(fill - b.columns.begin()) : -1;
    size_t most = 0;
    for (size_t c = 0; c < cols && fill == b.columns.end(); c++) {
        if (b.columns[c].align != Align::Left) continue;
        size_t total = 0;
        for (const auto& r : sized)
            if (c < r.size()) total += r[c].text.size();
        if (stretch < 0 || total > most) stretch = int(c), most = total;
    }

    ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoPadOuterX;
    if (!ImGui::BeginTable("##table", int(cols), flags)) return;
    // The rest are as wide as their widest text, measured here: ImGui's own
    // fitting can settle narrower than the text, cutting it off.
    for (size_t c = 0; c < cols; c++) {
        if (int(c) == stretch) {
            ImGui::TableSetupColumn(b.columns[c].title.c_str(), ImGuiTableColumnFlags_WidthStretch);
            continue;
        }
        ImGui::PushFont(fonts_.caption);
        float w = ImGui::CalcTextSize(b.columns[c].title.c_str()).x;
        for (const auto& r : sized)
            if (c < r.size() && !r[c].detail.empty() && b.columns[c].align != Align::Left)
                w = std::max(w, ImGui::CalcTextSize(r[c].detail.c_str()).x);
        ImGui::PopFont();
        for (const auto& r : sized) {
            if (c >= r.size()) continue;
            w = std::max(w, ImGui::CalcTextSize(r[c].text.c_str()).x);
            if (!r[c].localApp.empty() && grabber_->Has(r[c].localApp))
                w = std::max(w, ImGui::CalcTextSize(icon::Download).x + 8 * scale_);
            if (!r[c].actionType.empty()) w = std::max(w, ImGui::CalcTextSize(icon::Dismiss).x + 8 * scale_);
        }
        ImGui::TableSetupColumn(b.columns[c].title.c_str(), ImGuiTableColumnFlags_WidthFixed, std::ceil(w));
    }

    bool headers = std::any_of(b.columns.begin(), b.columns.end(), [](const Column& c) { return !c.title.empty(); });
    if (headers) {
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::PushFont(fonts_.caption);
        for (size_t c = 0; c < cols; c++) {
            ImGui::TableSetColumnIndex(int(c));
            textAligned(b.columns[c].title, b.columns[c].align, Colours().muted);
        }
        ImGui::PopFont();
    }
    for (const auto& row : b.rows) {
        ImGui::TableNextRow();
        // As of the last frame; the window keeps drawing a few after input.
        bool rowHovered = ImGui::TableGetHoveredRow() == ImGui::TableGetRowIndex();
        for (size_t c = 0; c < cols && c < row.size(); c++) {
            ImGui::TableSetColumnIndex(int(c));
            const Cell& cell = row[c];
            if (!cell.localApp.empty()) {
                if (grabber_->Has(cell.localApp)) localButton(cell);
                continue;
            }
            if (!cell.actionType.empty()) {
                actionButton(monitor, cell, rowHovered);
                continue;
            }
            textAligned(cell.text, b.columns[c].align, ToneColour(cell.tone));
            linkBehaviour(cell.url);
            if (!cell.detail.empty()) {
                ImGui::PushFont(fonts_.caption);
                if (b.columns[c].align == Align::Left) wrapped(cell.detail, Colours().muted);
                else textAligned(cell.detail, b.columns[c].align, Colours().muted);
                ImGui::PopFont();
            }
        }
    }
    ImGui::EndTable();
}

void App::list(const Block& b) {
    const Palette& p = Colours();
    float thumbW = 112 * scale_, thumbH = 84 * scale_;
    float pad = 6 * scale_;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < b.items.size(); i++) {
        const Item& it = b.items[i];
        ImGui::PushID(int(i));
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);

        ImVec2 start = ImGui::GetCursorScreenPos();
        float right = start.x + ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad));
        ImGui::BeginGroup();
        if (!it.image.empty()) {
            ImVec2 tp = ImGui::GetCursorScreenPos();
            ImVec2 tq(tp.x + thumbW, tp.y + thumbH);
            if (const Texture* t = images_->Get(it.image)) {
                // Crop to the thumbnail's shape rather than squash.
                float ta = float(t->width) / float(t->height), ba = thumbW / thumbH;
                ImVec2 uv0(0, 0), uv1(1, 1);
                if (ta > ba) uv0.x = (1 - ba / ta) / 2, uv1.x = 1 - uv0.x;
                else uv0.y = (1 - ta / ba) / 2, uv1.y = 1 - uv0.y;
                dl->AddImage((ImTextureID)(intptr_t)t->srv, tp, tq, uv0, uv1);
            } else {
                dl->AddRectFilled(tp, tq, u32(p.frame));
            }
            ImGui::Dummy(ImVec2(thumbW, thumbH));
            ImGui::SameLine(0, 12 * scale_);
        }
        ImGui::BeginGroup();
        ImGui::PushTextWrapPos(right - pad - ImGui::GetWindowPos().x);
        ImGui::PushFont(fonts_.heading);
        ImGui::TextUnformatted(it.title.c_str());
        ImGui::PopFont();
        if (!it.subtitle.empty()) wrapped(it.subtitle, it.tone == Tone::None ? p.text : ToneColour(it.tone));
        if (!it.detail.empty()) {
            ImGui::PushFont(fonts_.caption);
            wrapped(it.detail, p.muted);
            ImGui::PopFont();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndGroup();
        ImGui::EndGroup();

        ImVec2 end(right, ImGui::GetItemRectMax().y + pad);
        ImGui::SetCursorScreenPos(start);
        ImGui::InvisibleButton("##item", ImVec2(right - start.x, end.y - start.y));
        dl->ChannelsSetCurrent(0);
        if (ImGui::IsItemHovered() && !it.url.empty()) {
            linkHovered = true;
            dl->AddRectFilled(start, end, u32(p.hover));
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        }
        if (ImGui::IsItemClicked()) OpenURL(it.url);
        dl->ChannelsMerge();
        ImGui::PopID();
    }
}

// lickButton draws an electric guitar (an Ibanez XPT, all points) in the
// title bar, ending at right, that hides and shows Lick of the Day: in the
// text colour while it's showing, muted while hidden. It returns its left
// edge.
float App::lickButton(float right, float mid) {
    const Palette& p = Colours();
    float size = ImGui::GetFrameHeight();
    ImVec2 origin = ImGui::GetWindowPos();
    ImGui::SetCursorPos(ImVec2(right - origin.x - size, mid - size / 2));
    ImVec2 min = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##lick", ImVec2(size, size));
    bool hovered = ImGui::IsItemHovered();
    bool shown = lick_.Shown();
    if (clicked) lick_.Toggle(), lickClicked_ = ImGui::GetTime();
    noDrag_.push_back(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Flat until hovered, like the buttons beside it.
    if (hovered)
        dl->AddRectFilled(min, ImVec2(min.x + size, min.y + size),
                          ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                          ImGui::GetStyle().FrameRounding);
    // Solid, so it looks brighter than the line icons beside it: muted
    // while showing, fainter still while hidden, the text colour hovered.
    ImU32 body = u32(hovered ? p.text : shown ? p.muted : mix(p.muted, p.bg, 0.45f));

    // Drawn upright, from -1 (the headstock's tip) to 1 (the body's), leant
    // over to the right, then centred in the button by its bounds: the body
    // is heavier than the headstock.
    float r = size * 0.44f, ca = std::cos(0.75f), sa = std::sin(0.75f);
    std::vector<std::vector<ImVec2>> shapes = {
        // The body: two long horns forward, the treble one longer, and two
        // points behind with a notch between.
        {{0.07f, 0.08f}, {0.32f, -0.30f}, {0.24f, 0.12f}, {0.36f, 0.62f}, {0.30f, 0.98f}, {0.04f, 0.72f},
         {-0.30f, 1.0f}, {-0.32f, 0.56f}, {-0.19f, 0.2f}, {-0.27f, -0.14f}, {-0.07f, 0.08f}},
        {{-0.05f, 0.2f}, {0.05f, 0.2f}, {0.05f, -0.76f}, {-0.05f, -0.76f}},  // neck
        // A pointed headstock, swept back to one side.
        {{-0.06f, -0.74f}, {0.08f, -0.74f}, {0.13f, -0.84f}, {-0.1f, -1.04f}, {-0.09f, -0.86f}},
    };
    ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
    for (auto& shape : shapes)
        for (ImVec2& q : shape) {
            q = ImVec2((q.x * ca - q.y * sa) * r, (q.x * sa + q.y * ca) * r);
            lo = ImVec2(std::min(lo.x, q.x), std::min(lo.y, q.y));
            hi = ImVec2(std::max(hi.x, q.x), std::max(hi.y, q.y));
        }
    ImVec2 c(min.x + size / 2 - (lo.x + hi.x) / 2, min.y + size / 2 - (lo.y + hi.y) / 2);
    for (auto& shape : shapes) {
        for (ImVec2& q : shape) q = ImVec2(c.x + q.x, c.y + q.y);
        dl->AddConcavePolyFilled(shape.data(), int(shape.size()), body);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip(shown ? "Hide Lick of the Day" : "Show Lick of the Day");
    return min.x;
}

// appsButton draws Windows 95's flag in the title bar, ending at right, that opens a
// menu of the user's own apps (config.json's "apps", in A-Z order): each with its icon,
// and a dot while it's running. Picking one brings it to the front or
// starts it. It returns its left edge.
float App::appsButton(float right, float mid) {
    const Palette& p = Colours();
    float size = ImGui::GetFrameHeight();
    ImVec2 origin = ImGui::GetWindowPos();
    float w = std::round(size * 1.35f);  // wide enough for the flag's trail
    ImGui::SetCursorPos(ImVec2(right - origin.x - w, mid - size / 2));
    bool open = ImGui::IsPopupOpen("##apps");
    if (ImGui::InvisibleButton("##appsButton", ImVec2(w, size))) ImGui::OpenPopup("##apps");
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    noDrag_.push_back(ImRect(min, max));
    ImDrawList* bar = ImGui::GetWindowDrawList();
    bool lit = open || ImGui::IsItemHovered();
    // Flat until hovered or open, like the buttons beside it.
    if (lit)
        bar->AddRectFilled(min, max, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                           ImGui::GetStyle().FrameRounding);
    // Windows 95's waving flag, greyed out: muted like the guitar, the
    // text colour while hovered or open. The frame is the ink and the panes
    // a darker grey, with a trail of squares off to the upper left, smaller
    // and fainter the further they go.
    ImVec4 ink = lit ? p.text : p.muted, pane = mix(ink, p.bg, 0.42f);
    constexpr float kFlag = 0.54f;  // where the flag starts along the band
    // band maps it, t from the trail's end (0) to the flag's right
    // edge (1) and v from top (0) to bottom (1), into the button: rising
    // to the left, the flag bowed by a wave and its right edge curling in.
    auto band = [&](float t, float v) {
        float f = std::max(0.0f, (t - kFlag) / (1 - kFlag)), arch = 0.035f * std::sin(3.14159f * f);
        float top = 0.08f + 0.27f * t - 0.06f * t * t - arch, bot = 0.58f + 0.32f * t - arch * 0.6f;
        float x = 0.06f + 0.86f * t - v * (0.03f + 0.07f * t * t * t) - 0.07f * v * v * f;
        return ImVec2(min.x + x * w, min.y + (top + v * (bot - top)) * size);
    };
    auto quad = [&](float t0, float t1, float v0, float v1, int n, ImU32 col) {
        std::vector<ImVec2> q;
        for (int k = 0; k <= n; k++) q.push_back(band(t0 + (t1 - t0) * k / n, v0));
        for (int k = 1; k < n; k++) q.push_back(band(t1, v0 + (v1 - v0) * k / n));
        for (int k = 0; k <= n; k++) q.push_back(band(t1 - (t1 - t0) * k / n, v1));
        for (int k = 1; k < n; k++) q.push_back(band(t0, v1 - (v1 - v0) * k / n));
        bar->AddConcavePolyFilled(q.data(), int(q.size()), col);
    };
    quad(kFlag, 1, 0, 1, 6, u32(ink));
    auto fl = [&](float a) { return kFlag + (1 - kFlag) * a; };
    const float g = 0.04f;
    const float panes[4][4] = {{0.1f, 0.5f - g / 2, 0.07f, 0.5f - g}, {0.5f + g / 2, 0.92f, 0.07f, 0.5f - g},
                               {0.1f, 0.5f - g / 2, 0.5f + g, 0.93f}, {0.5f + g / 2, 0.92f, 0.5f + g, 0.93f}};
    for (const auto& q : panes) quad(fl(q[0]), fl(q[1]), q[2], q[3], 6, u32(pane));
    // The trail, column by column from the flag leftwards, rows top to
    // bottom: 1 ink, 2 pane, 0 a gap.
    const char* trail[] = {"121212", "212121", "120211", "201202", "020120", "102010", "010201"};
    float t = kFlag, cw = 0.06f;
    for (int j = 0; j < 7; j++) {
        float t1 = t - cw * 0.35f, t0 = t1 - cw;
        if (t0 < 0) break;
        for (int i = 0; i < 6; i++) {
            if (trail[j][i] == '0') continue;
            ImVec4 c = mix(trail[j][i] == '1' ? ink : pane, p.bg, std::min(0.8f, 0.08f * (j + 1)));
            quad(t0, t1, i / 6.0f + 0.015f, (i + 1) / 6.0f - 0.015f, 2, u32(c));
        }
        t = t0, cw *= 0.86f;
    }
    if (!open && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Apps");

    // Under the button, its right edge lined up with the button's.
    ImGui::SetNextWindowPos(ImVec2(max.x, max.y + 4 * scale_), ImGuiCond_Always, ImVec2(1, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6 * scale_, 6 * scale_));
    if (ImGui::BeginPopup("##apps")) {
        float iconSize = std::round(20 * scale_), pad = 6 * scale_;
        float rowH = std::max(iconSize, ImGui::GetTextLineHeight()) + pad * 2;
        float rw = 0;
        for (const auto& a : launcher_.Installed()) rw = std::max(rw, ImGui::CalcTextSize(a.name.c_str()).x);
        rw += iconSize + pad * 4 + 14 * scale_;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (const auto& a : launcher_.Installed()) {
            ImGui::PushID(a.name.c_str());
            ImVec2 at = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##app", false, 0, ImVec2(rw, rowH - ImGui::GetStyle().ItemSpacing.y))) launcher_.Open(a);
            float cy = at.y + (rowH - ImGui::GetStyle().ItemSpacing.y) / 2;
            ImVec2 ic(at.x + pad, std::round(cy - iconSize / 2));
            std::string key = launcher_.Icon(a, *images_, int(iconSize));
            if (const Texture* t = images_->Get(key))
                dl->AddImage((ImTextureID)(intptr_t)t->srv, ic, ImVec2(ic.x + iconSize, ic.y + iconSize));
            float th = ImGui::GetTextLineHeight();
            dl->AddText(ImVec2(ic.x + iconSize + pad * 1.5f, std::round(cy - th / 2)), u32(p.text), a.name.c_str());
            if (launcher_.Running(a))
                dl->AddCircleFilled(ImVec2(at.x + rw - pad - 3 * scale_, cy), 3 * scale_, u32(p.good));
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s", launcher_.Running(a) ? "Running: bring to the front" : "Start");
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
    return min.x;
}

// cat draws the black cat along the top of the Recent column.
void App::cat(float width) {
    float size = 1.6f * scale_;
    // Her space is 68 units tall: 64 above the floor (an arched tail),
    // 4 below (rounded paws and her rim).
    float h = std::round(68 * size);
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(width, h));
    double t = ImGui::GetTime();
    Cat c = CatAt(t, width / 2 / size);
    DrawListCanvas canvas(ImGui::GetWindowDrawList());
    DrawCat(canvas, c, t, Vec2f{at.x + width / 2, at.y + h - 4 * size}, size);
    // Asleep, her breathing and Z's are slow: ten frames a second will do.
    catInterval_ = c.asleep >= 1 ? 100 : 33;
}

float App::landHeight() const { return std::round((compact_ ? 140 : 160) * scale_); }

// skyHeight is how tall sky draws, 0 when it draws nothing.
float App::skyHeight() const {
    if (!ParsePlace(config_.place)) return 0;
    float pad = (compact_ ? 7 : 10) * scale_;
    return landHeight() + std::round(pad * 0.7f) * 2 + fonts_.caption->FontSize;
}

float App::mediaBarHeight() const {
    return std::round(fonts_.caption->FontSize + (compact_ ? 8 : 10) * scale_);
}

// mediaBar draws what's playing in one line: the track and artist,
// previous, play/pause and next, and its progress as a thin line along the
// bottom. With nothing playing it says so and stays put.
void App::mediaBar(float width) {
    const Palette& p = Colours();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = scale_, h = mediaBarHeight();
    float pad = (compact_ ? 6 : 8) * s;
    ImVec2 start = ImGui::GetCursorScreenPos(), end(start.x + width, start.y + h);
    const Block* b = mediaPanel_.blocks.empty() ? nullptr : &mediaPanel_.blocks[0];
    const Item* it = b && !b->items.empty() ? &b->items[0] : nullptr;
    Media m = b ? b->media : Media{};

    ImGui::PushID("##media");
    dl->AddRectFilled(start, end, u32(p.card));
    ImGui::PushFont(fonts_.caption);
    float lineH = ImGui::GetTextLineHeight(), ty = std::round(start.y + (h - lineH) / 2);

    // Controls at the right, flat and dim until hovered.
    float btn = h, right = end.x - 2 * s;
    bool overControl = false;
    auto control = [&](const char* glyph, bool enabled, MediaCommand c, const char* tip) {
        right -= btn;
        ImVec2 a(right, start.y), z(right + btn, end.y);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(glyph);
        bool clicked = ImGui::InvisibleButton("##c", ImVec2(btn, h)) && enabled;
        bool hovered = ImGui::IsItemHovered() && enabled;
        ImGui::PopID();
        if (hovered) {
            overControl = true;
            dl->AddRectFilled(a, z, u32(p.hover));
            ImGui::SetTooltip("%s", tip);
        }
        if (clicked) media_->Command(c);
        ImVec2 gs = ImGui::CalcTextSize(glyph);
        ImVec4 col = !enabled ? mix(p.muted, p.card, 0.5f) : hovered ? p.text : p.muted;
        dl->AddText(ImVec2(std::round(right + (btn - gs.x) / 2), ty), u32(col), glyph);
    };
    control(icon::Next, m.canNext, MediaCommand::Next, "Next");
    control(m.playing ? icon::Pause : icon::Play, m.canToggle, MediaCommand::TogglePlay, m.playing ? "Pause" : "Play");
    control(icon::Previous, m.canPrevious, MediaCommand::Previous, "Previous");

    float x = start.x + pad;
    float textW = right - 4 * s - x;
    if (it) {
        std::string title = fit(it->title, textW);
        dl->AddText(ImVec2(x, ty), u32(m.playing ? p.text : p.muted), title.c_str());
        float tw = ImGui::CalcTextSize(title.c_str()).x;
        if (!it->subtitle.empty() && textW - tw > 30 * s) {
            std::string artist = fit("  " + it->subtitle, textW - tw);
            dl->AddText(ImVec2(x + tw, ty), u32(p.muted), artist.c_str());
        }
    } else {
        dl->AddText(ImVec2(x, ty), u32(mix(p.muted, p.card, 0.3f)), "Nothing playing");
    }

    // Progress, moved on by the time since Windows last said where it was.
    double pos = 0;
    if (m.duration > 0) {
        double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
        pos = std::clamp(m.position + (m.playing ? now - m.asOf : 0), 0.0, m.duration);
        float lh = std::max(1.0f, std::round(2 * s));
        dl->AddRectFilled(ImVec2(start.x, end.y - lh), ImVec2(start.x + width * float(pos / m.duration), end.y),
                          u32(m.playing ? p.accent : p.muted));
    }
    ImGui::PopFont();
    bevel(start, end);

    // Over the rest of the bar, the whole story.
    ImGui::SetCursorScreenPos(start);
    ImGui::Dummy(ImVec2(width, h));
    if (it && !overControl && ImGui::IsItemHovered()) {
        std::string tip = it->title;
        if (!it->subtitle.empty()) tip += "\n" + it->subtitle;
        if (!it->detail.empty()) tip += "\n" + it->detail;
        if (m.duration > 0) tip += "\n" + FormatTime(pos) + " / " + FormatTime(m.duration);
        if (!m.playing) tip += "\nPaused";
        ImGui::SetTooltip("%s", tip.c_str());
    }
    ImGui::PopID();
}

// sky draws a strip of countryside under the sky as it is now: the sun
// and moon at their bearing (north at both ends, south in the middle) and
// height, the sun's path for today dotted across, stars at night, and
// sunrise, sunset and the moon's phase beneath.
void App::sky(float width, int64_t t) {
    auto place = ParsePlace(config_.place);
    if (!place) return;
    if (skyDay_.minute != t / 60 || skyDay_.place != config_.place) {
        skyDay_.minute = t / 60;
        skyDay_.place = config_.place;
        std::time_t tt = std::time_t(t);
        std::tm l{};
        localtime_s(&l, &tt);
        l.tm_hour = l.tm_min = l.tm_sec = 0;
        l.tm_isdst = -1;
        int64_t midnight = int64_t(std::mktime(&l));
        l.tm_mday++;
        l.tm_isdst = -1;
        int64_t tomorrow = int64_t(std::mktime(&l));
        skyDay_.sun = SunCrossings(midnight, tomorrow, *place);
        skyDay_.moon = MoonCrossings(t, t + 36 * 3600, *place);  // the next of each
        skyDay_.path.clear();
        for (int64_t s = midnight; s <= tomorrow; s += 600) skyDay_.path.push_back(SunPosition(s, *place));
    }
    Position sun = SunPosition(t, *place), moonAt = MoonPosition(t, *place);
    Moon moon = MoonPhase(t);

    const Palette& p = Colours();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float s = scale_;
    float pad = (compact_ ? 7 : 10) * s;
    ImVec2 start = ImGui::GetCursorScreenPos();
    float landH = landHeight();
    ImVec2 landEnd(start.x + width, start.y + landH);
    float horizon = start.y + std::round(landH * 0.74f);
    auto px = [&](double az) { return start.x + width * float(az / 360); };
    auto py = [&](double alt) { return horizon - (horizon - start.y - 9 * s) * float(alt / 70); };

    // How much it's day, 0 at night to 1, and how near the sun is to the
    // horizon, for a warm glow at either end of the day.
    float day = std::clamp(float((sun.altitude + 6) / 14), 0.0f, 1.0f);
    float dusk = std::clamp(1 - float(std::abs(sun.altitude - 1) / 9), 0.0f, 1.0f);
    // Natural colours, toned towards the theme's card, less so on OLED.
    float strength = config_.oled.black ? 0.4f : 0.7f;
    auto tone = [&](ImVec4 c) { return mix(p.card, c, strength); };
    ImVec4 top = mix(ImVec4(0.03f, 0.05f, 0.13f, 1), ImVec4(0.36f, 0.6f, 0.9f, 1), day);
    ImVec4 bottom = mix(ImVec4(0.1f, 0.13f, 0.26f, 1), ImVec4(0.72f, 0.85f, 0.96f, 1), day);
    bottom = mix(bottom, ImVec4(0.98f, 0.58f, 0.32f, 1), dusk * 0.7f);
    top = mix(top, ImVec4(0.55f, 0.45f, 0.62f, 1), dusk * 0.35f);

    dl->PushClipRect(start, landEnd, true);
    dl->AddRectFilledMultiColor(start, ImVec2(landEnd.x, horizon + 6 * s), u32(tone(top)), u32(tone(top)),
                                u32(tone(bottom)), u32(tone(bottom)));
    // Stars, in the same places every night.
    if (day < 1) {
        unsigned h = 2166136261u;
        for (int i = 0; i < 28; i++) {
            h = (h ^ unsigned(i)) * 16777619u;
            float x = float(h % 1000) / 1000, y = float((h / 1000) % 1000) / 1000;
            float a = (1 - day) * (0.35f + 0.5f * float((h >> 20) % 100) / 100);
            dl->AddCircleFilled(ImVec2(start.x + x * width, start.y + y * (horizon - start.y)), 0.9f * s,
                                u32(ImVec4(1, 1, 1, a * strength)));
        }
    }
    // Today's path, dotted while it's up.
    ImVec4 sunColour = mix(p.warn, ImVec4(1, 0.85f, 0.4f, 1), 0.4f);
    for (const Position& q : skyDay_.path)
        if (q.altitude > 0) dl->AddCircleFilled(ImVec2(px(q.azimuth), py(q.altitude)), 1.1f * s, u32(fade(sunColour, 0.45f)));
    if (sun.altitude > -2) {
        ImVec2 c(px(sun.azimuth), py(sun.altitude));
        dl->AddCircleFilled(c, 14 * s, u32(fade(sunColour, 0.18f)));
        dl->AddCircleFilled(c, 8 * s, u32(sunColour));
    }
    if (moonAt.altitude > -2) {
        // The lit part: the limb down one side, then the terminator, an
        // ellipse, back up. Lit on the right while waxing, seen from the
        // north; the other way round from the south.
        ImVec2 c(px(moonAt.azimuth), py(moonAt.altitude));
        float r = 7 * s;
        float shown = 0.55f + 0.45f * (1 - day);  // fainter by day
        dl->AddCircleFilled(c, r, u32(fade(mix(tone(top), ImVec4(1, 1, 1, 1), 0.15f), shown)));
        float side = (moon.phase < 0.5) != (place->lat < 0) ? 1.0f : -1.0f;
        float k = float(std::cos(2 * 3.14159265 * moon.phase));  // the terminator, as a fraction of the limb
        constexpr int n = 24;
        std::vector<ImVec2> lit;
        for (int i = 0; i <= n; i++) {
            float a = 3.14159265f * float(i) / n;
            lit.push_back(ImVec2(c.x + side * r * std::sin(a), c.y - r * std::cos(a)));
        }
        for (int i = n - 1; i > 0; i--) {
            float a = 3.14159265f * float(i) / n;
            lit.push_back(ImVec2(c.x + side * k * r * std::sin(a), c.y - r * std::cos(a)));
        }
        if (side < 0) std::reverse(lit.begin(), lit.end());  // ImGui fills it only wound one way
        if (moon.lit > 0.01) dl->AddConcavePolyFilled(lit.data(), int(lit.size()), u32(ImVec4(0.96f, 0.95f, 0.88f, shown)));
    }
    // Two lines of hills, darker at night, and a couple of trees.
    float night = 0.55f * (1 - day);
    ImVec4 back = mix(tone(ImVec4(0.38f, 0.55f, 0.38f, 1)), ImVec4(0, 0, 0, 1), night);
    ImVec4 front = mix(tone(ImVec4(0.22f, 0.4f, 0.25f, 1)), ImVec4(0, 0, 0, 1), night);
    auto hill = [&](float u, bool closer) {
        return closer ? horizon + s * (7 + 3 * std::sin(u * 7 + 4) + 1.5f * std::sin(u * 19))
                      : horizon - s * (2 + 3 * std::sin(u * 9 + 1) + 2 * std::sin(u * 23 + 2));
    };
    for (bool closer : {false, true}) {
        float step = 3 * s;
        for (float x = 0; x < width; x += step) {
            float x1 = std::min(x + step, width);
            ImVec2 q[4] = {ImVec2(start.x + x, hill(x / width, closer)), ImVec2(start.x + x1, hill(x1 / width, closer)),
                           ImVec2(start.x + x1, landEnd.y), ImVec2(start.x + x, landEnd.y)};
            dl->AddConvexPolyFilled(q, 4, u32(closer ? front : back));
        }
    }
    for (float u : {0.17f, 0.21f, 0.83f}) {
        float x = start.x + u * width, ground = hill(u, true) + 1 * s;
        float h = (u == 0.21f ? 9 : 12) * s;
        dl->AddRectFilled(ImVec2(x - 0.8f * s, ground - h * 0.35f), ImVec2(x + 0.8f * s, ground), u32(front));
        dl->AddTriangleFilled(ImVec2(x, ground - h), ImVec2(x - h * 0.3f, ground - h * 0.3f),
                              ImVec2(x + h * 0.3f, ground - h * 0.3f), u32(mix(front, ImVec4(0, 0, 0, 1), 0.25f)));
    }
    dl->PopClipRect();

    // Beneath it, on the card: sunrise and sunset, and the moon's phase.
    ImGui::PushFont(fonts_.caption);
    float lineH = ImGui::GetTextLineHeight();
    ImVec2 end(landEnd.x, landEnd.y + std::round(pad * 0.7f) * 2 + lineH);
    dl->AddRectFilled(ImVec2(start.x, landEnd.y), end, u32(p.card));
    float ty = landEnd.y + std::round(pad * 0.7f);
    std::string times = std::string(icon::Up) + " " + clockTime(skyDay_.sun.rise) + "    " + icon::Down + " " +
                        clockTime(skyDay_.sun.set);
    dl->AddText(ImVec2(start.x + pad, ty), u32(p.text), times.c_str());
    const char* phase = PhaseName(moon.phase);
    float pw = ImGui::CalcTextSize(phase).x;
    dl->AddText(ImVec2(end.x - pad - pw, ty), u32(p.muted), phase);
    ImGui::PopFont();
    bevel(start, end);

    ImGui::Dummy(ImVec2(width, end.y - start.y));
    if (ImGui::IsItemHovered()) {
        int len = skyDay_.sun.rise && skyDay_.sun.set ? int(skyDay_.sun.set - skyDay_.sun.rise) / 60 : 0;
        std::string sunLine = sun.altitude > 0 ? "Sun " + std::to_string(int(std::lround(sun.altitude))) + "\xC2\xB0 up"
                                               : std::string("Sun down");
        if (len > 0) sunLine += ", " + std::to_string(len / 60) + "h " + std::to_string(len % 60) + "m of daylight today";
        ImGui::SetTooltip("%s\n%s, %d%% lit\nMoon %s, rises %s, sets %s", sunLine.c_str(), phase,
                          int(std::lround(moon.lit * 100)), moonAt.altitude > 0 ? "up" : "down",
                          clockTime(skyDay_.moon.rise).c_str(), clockTime(skyDay_.moon.set).c_str());
    }
}

// feedHeading is the Recent column's heading, above its scrolling cards,
// with an x at the right to clear them all while the mouse is over the
// column.
void App::feedHeading(float width) {
    const Palette& p = Colours();
    const ImGuiIO& io = ImGui::GetIO();
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushFont(fonts_.heading);
    ImGui::TextColored(p.heading, "Recent");
    ImGui::PopFont();
    float h = ImGui::GetItemRectSize().y;
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    if (model_.Events().empty() || !feedRect_.Contains(io.MousePos)) return;

    float xs = std::round(22 * scale_);
    // In line with the cards' x's.
    float right = at.x + width - 3 * scale_;
    ImRect x(ImVec2(right - xs, std::round(at.y + (h - xs) / 2)), ImVec2(right, std::round(at.y + (h + xs) / 2)));
    bool over = x.Contains(io.MousePos) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (over) dl->AddRectFilled(x.Min, x.Max, u32(p.frame), 4 * scale_);
    ImGui::PushFont(fonts_.chrome);
    ImVec2 g = ImGui::CalcTextSize(icon::Close);
    dl->AddText(ImVec2(std::round(x.GetCenter().x - g.x / 2), std::round(x.GetCenter().y - g.y / 2)),
                u32(over ? p.text : p.muted), icon::Close);
    ImGui::PopFont();
    if (!over) return;
    ImGui::SetTooltip("Clear all");
    if (io.MouseClicked[0]) {
        model_.ClearAll();
        config_.cleared = model_.GetCleared();
        SaveConfig(config_);
    }
}

// eventCard draws one event as a card, which opens its link when clicked
// and has an x to clear it while hovered; left, from 1 down to 0, draws
// how long a temporary one has left (negative for none). It returns
// whether the x was clicked.
bool App::eventCard(const Event& e, int64_t t, float left) {
    const Palette& p = Colours();
    const ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float pad = (compact_ ? 7 : 10) * scale_;
    ImGui::PushID(int(e.id));
    dl->ChannelsSplit(2);
    dl->ChannelsSetCurrent(1);
    ImVec2 start = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::SetCursorScreenPos(ImVec2(start.x + pad, start.y + pad * 0.8f));
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(start.x + width - pad - ImGui::GetWindowPos().x);
    ImGui::PushFont(fonts_.caption);
    mutedText(model_.TitleOf(e.monitor) + "  ·  " + Ago(e.time, t));
    ImGui::PopFont();
    // Quiet ones are quieter in Recent; a temporary one is there to be read.
    bool quiet = e.quiet && left < 0;
    ImGui::PushFont(quiet ? fonts_.body : fonts_.heading);
    wrapped(e.title, quiet ? p.muted : p.text);
    ImGui::PopFont();
    if (!e.body.empty()) {
        ImGui::PushFont(fonts_.caption);
        wrapped(e.body, p.muted);
        ImGui::PopFont();
    }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImVec2 end(start.x + width, ImGui::GetItemRectMax().y + pad * 0.8f);
    ImGui::SetCursorScreenPos(start);
    ImGui::InvisibleButton("##event", ImVec2(width, end.y - start.y));

    bool over = ImGui::IsItemHovered();
    // An x in the top-right corner, while the card's hovered, clears it.
    float xs = std::round(22 * scale_);
    float edge = end.x - 3 * scale_;
    ImRect x(ImVec2(edge - xs, start.y + 3 * scale_), ImVec2(edge, start.y + 3 * scale_ + xs));
    bool overX = over && x.Contains(io.MousePos);
    if (over) {
        if (overX) dl->AddRectFilled(x.Min, x.Max, u32(p.frame), 4 * scale_);
        ImGui::PushFont(fonts_.chrome);
        ImVec2 g = ImGui::CalcTextSize(icon::Close);
        dl->AddText(ImVec2(std::round(x.GetCenter().x - g.x / 2), std::round(x.GetCenter().y - g.y / 2)),
                    u32(overX ? p.text : p.muted), icon::Close);
        ImGui::PopFont();
        if (overX) ImGui::SetTooltip("Clear");
    }

    dl->ChannelsSetCurrent(0);
    bool hovered = over && !overX && !e.url.empty();
    dl->AddRectFilled(start, end, u32(hovered ? p.hover : p.card));
    if (e.priority == Priority::High && !e.quiet)
        dl->AddRectFilled(start, ImVec2(start.x + 3 * scale_, end.y), u32(p.accent));
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    bool cleared = false;
    if (ImGui::IsItemClicked()) {
        if (overX) cleared = true;
        else OpenURL(e.url);
    }
    if (left >= 0) {
        // How long it has left, as a bar along the bottom that empties.
        float h = std::max(2.0f, std::round(2 * scale_));
        dl->AddRectFilled(ImVec2(start.x, end.y - h), ImVec2(start.x + (end.x - start.x) * left, end.y), u32(p.accent));
    }
    dl->ChannelsMerge();
    bevel(start, end);
    ImGui::Dummy(ImVec2(0, 1 * scale_));
    ImGui::PopID();
    return cleared;
}

// temporaries draws the temporary events above Recent, under their own
// heading, until each one's time is up.
void App::temporaries() {
    const auto& temp = model_.Temporary();
    if (temp.empty()) return;
    const Palette& p = Colours();
    ImGui::PushFont(fonts_.heading);
    ImGui::TextColored(p.heading, "Just now");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 2 * scale_));
    // Fractions of a second, so the bars move smoothly between redraws.
    double t = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t clear = 0;
    for (const auto& e : temp) {
        float span = float(std::max<int64_t>(e.expires - e.time, 1));
        float left = std::clamp(float((double(e.expires) - t) / span), 0.0f, 1.0f);
        if (eventCard(e, int64_t(t), left)) clear = e.id;
    }
    ImGui::Dummy(ImVec2(0, 10 * scale_));
    if (clear != 0) {
        model_.Clear(clear);
        config_.cleared = model_.GetCleared();
        SaveConfig(config_);
    }
}

void App::feed() {
    const Palette& p = Colours();
    const ImGuiIO& io = ImGui::GetIO();
    const auto& events = model_.Events();
    if (events.empty()) {
        mutedText("Nothing new.");
        return;
    }
    int64_t t = now();
    size_t shown = 0;
    int64_t clear = 0;
    for (const auto& e : events) {
        if (++shown > 150) break;
        if (eventCard(e, t, -1)) clear = e.id;
    }
    if (clear != 0) {
        model_.Clear(clear);
        config_.cleared = model_.GetCleared();
        SaveConfig(config_);
    }
}

void App::openSettings() {
    snprintf(urlBuf_, sizeof(urlBuf_), "%s", config_.url.c_str());
    snprintf(tokenBuf_, sizeof(tokenBuf_), "%s", config_.token.c_str());
    priorityChoice_ = int(std::find(std::begin(kPriorities), std::end(kPriorities), config_.minPriority) - std::begin(kPriorities));
    notifyChoice_ = config_.notify;
    startChoice_ = StartsWithWindows();
    oledChoice_ = config_.oled;
    catChoice_ = config_.cat;
    snprintf(placeBuf_, sizeof(placeBuf_), "%s", config_.place.c_str());
    themeChoice_ = config_.theme == "retro" ? 1 : 0;
    settingsOpen_ = true;
}

// diagnostics shows the background monitors, which feed other panels
// rather than having their own (the mail reader and what it found), each
// collapsed until opened.
void App::diagnostics() {
    std::vector<Snapshot> bg;
    for (const Snapshot& s : model_.Snapshots())
        if (s.background) bg.push_back(s);
    if (bg.empty()) return;
    mutedText("Diagnostics");
    for (const Snapshot& s : bg) {
        if (!ImGui::CollapsingHeader((s.title + "##diag-" + s.monitor).c_str())) continue;
        ImGui::BeginChild(("##diag-body-" + s.monitor).c_str(), ImVec2(0, 260 * scale_), ImGuiChildFlags_Borders);
        if (!s.error.empty()) ImGui::TextColored(ToneColour(Tone::Bad), "%s", s.error.c_str());
        for (const Block& b : s.blocks) block(s.monitor, b);
        if (s.blocks.empty()) mutedText("Nothing to show.");
        ImGui::EndChild();
    }
    ImGui::Dummy(ImVec2(0, 8 * scale_));
}

void App::settings() {
    if (settingsOpen_ && !ImGui::IsPopupOpen("Settings")) ImGui::OpenPopup("Settings");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x / 2, io.DisplaySize.y / 2), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(std::min(520 * scale_, io.DisplaySize.x - 32 * scale_), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24 * scale_, 20 * scale_));
    bool open = ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                                               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar();
    if (!open) return;
    ImGui::PushFont(fonts_.title);
    ImGui::TextColored(Colours().heading, "Settings");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    float w = ImGui::GetContentRegionAvail().x;
    mutedText("Server address");
    ImGui::SetNextItemWidth(w);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##url", "https://example.com/kitchensink", urlBuf_, sizeof(urlBuf_));
    mutedText("Token");
    ImGui::SetNextItemWidth(w);
    ImGui::InputText("##token", tokenBuf_, sizeof(tokenBuf_), ImGuiInputTextFlags_Password);
    ImGui::Dummy(ImVec2(0, 4 * scale_));

    ImGui::Checkbox("Show Windows notifications", &notifyChoice_);
    ImGui::BeginDisabled(!notifyChoice_);
    mutedText("Notify me about");
    ImGui::SetNextItemWidth(w);
    ImGui::Combo("##priority", &priorityChoice_, kPriorityNames, IM_ARRAYSIZE(kPriorityNames));
    ImGui::EndDisabled();
    ImGui::Checkbox("Start with Windows", &startChoice_);
    ImGui::Dummy(ImVec2(0, 4 * scale_));
    mutedText("Theme");
    static const char* kThemes[] = {"As Windows (light or dark)", "Retro (the 90s Weather Channel)"};
    ImGui::SetNextItemWidth(w);
    ImGui::Combo("##theme", &themeChoice_, kThemes, IM_ARRAYSIZE(kThemes));
    ImGui::Dummy(ImVec2(0, 4 * scale_));
    mutedText("OLED care, against burn-in");
    auto care = [&](const char* label, bool* on, const char* what) {
        ImGui::Checkbox(label, on);
        ImGui::SameLine();
        ImGui::PushFont(fonts_.caption);
        ImGui::AlignTextToFramePadding();
        mutedText(what);
        ImGui::PopFont();
    };
    care("Shift", &oledChoice_.shift, "moves everything a few pixels every three minutes");
    ImGui::BeginDisabled(themeChoice_ == 1);
    care("True black", &oledChoice_.black, themeChoice_ == 1 ? "not with the retro theme" : "a black theme with dimmer text and borders");
    ImGui::EndDisabled();
    care("Dim when idle", &oledChoice_.dim, "after ten minutes without the mouse over the window");
    care("Swap sides", &oledChoice_.swap, "moves the Recent column across every other hour");
    ImGui::Dummy(ImVec2(0, 4 * scale_));
    ImGui::Checkbox("Cat", &catChoice_);
    ImGui::SameLine();
    ImGui::PushFont(fonts_.caption);
    ImGui::AlignTextToFramePadding();
    mutedText("a black cat asleep above the Recent column");
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4 * scale_));
    mutedText("Where you are, for the sun and moon under Recent");
    ImGui::SetNextItemWidth(w);
    ImGui::InputTextWithHint("##place", "latitude, longitude, e.g. 51.5, -0.12", placeBuf_, sizeof(placeBuf_));
    bool placeOK = placeBuf_[0] == '\0' || ParsePlace(placeBuf_);
    if (!placeOK) {
        ImGui::PushFont(fonts_.caption);
        ImGui::TextColored(ToneColour(Tone::Bad), "Two numbers, latitude then longitude, with a comma between");
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, 8 * scale_));
    diagnostics();

    bool configured = !config_.url.empty() && !config_.token.empty();
    const ImGuiStyle& st = ImGui::GetStyle();
    float bw = 96 * scale_;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - (configured ? bw * 2 + st.ItemSpacing.x : bw));
    if (configured) {
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            settingsOpen_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
    }
    ImGui::PushStyleColor(ImGuiCol_Button, Colours().accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, fade(Colours().accent, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button("Save", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        Config c = config_;
        c.url = urlBuf_;
        c.token = tokenBuf_;
        c.notify = notifyChoice_;
        c.oled = oledChoice_;
        c.cat = catChoice_;
        if (placeOK) c.place = placeBuf_;
        c.theme = themeChoice_ == 1 ? "retro" : "";
        c.minPriority = kPriorities[std::clamp(priorityChoice_, 0, 2)];
        bool reconnect = c.url != config_.url || c.token != config_.token;
        config_ = c;
        SaveConfig(config_);
        SetStartWithWindows(startChoice_);
        bool wasRetro = Colours().retro;
        ApplyTheme(theme(), scale_, compact_);
        if (Colours().retro != wasRetro) fontsDirty_ = true;
        if (reconnect) {
            model_ = Model();
            client_->Start(config_.url, config_.token);
        }
        settingsOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::PopStyleColor(3);
    ImGui::EndPopup();
}

}  // namespace ks
