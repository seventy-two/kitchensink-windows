// The dashboard: state from the stream, drawn with ImGui.
#pragma once

#include <windows.h>
#include <d3d11.h>

#include <array>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
#include "grabber.h"
#include "images.h"
#include "imgui_internal.h"
#include "launcher.h"
#include "lick.h"
#include "model.h"
#include "net.h"
#include "media.h"
#include "sky.h"
#include "sysinfo.h"
#include "theme.h"
#include "upload.h"

namespace ks {

class App {
public:
    // wake is called from background threads when there's something new.
    // notify shows a Windows notification. The header doubles as hwnd's
    // title bar, with its own window buttons.
    App(HWND hwnd, ID3D11Device* device, std::function<void()> wake,
        std::function<void(const std::string& title, const std::string& body)> notify);
    ~App();

    // SetScale reloads fonts and style for a DPI scale (1 at 96 DPI).
    void SetScale(float scale);
    void SetDark(bool dark);

    // Pump takes in what the background threads have received. It runs
    // whether or not the window is showing, so notifications still fire.
    void Pump();
    // Frame draws the UI. Call it between ImGui::NewFrame and Render.
    void Frame();

    // NotificationClicked opens what the last notification was about. It
    // returns false if there was nothing to open, so the caller can show
    // the window instead.
    bool NotificationClicked();

    // FlushIn is how many milliseconds until held notifications are due,
    // or INFINITE if none are waiting.
    unsigned long FlushIn() const;

    // InCaption reports whether a point in client pixels is on the title
    // bar but not on one of its buttons, so dragging there moves the window.
    bool InCaption(float x, float y) const;

    // Busy reports whether the UI needs frames drawn continuously, e.g.
    // while typing.
    bool Busy() const { return busy_; }
    // FrameInterval is how often, in milliseconds, something on screen
    // wants drawing while it moves (the cat, the retro theme's clock), or 0
    // when nothing does.
    unsigned long FrameInterval() const { return frameInterval_; }

    // FontsDirty says the fonts need loading again (the theme changed), which
    // the caller does with ReloadFonts before the next frame, then recreates
    // the renderer's font texture.
    bool FontsDirty() const { return fontsDirty_; }
    void ReloadFonts();

    // PostRender finishes the frame's draw data before it's drawn: the
    // retro theme's text shadows.
    void PostRender(ImDrawData* data) const;

private:
    // theme is the theme in use: retro if chosen, else true black for OLED
    // care, else Windows' light or dark.
    Theme theme() const;
    // bevel raises a retro panel: lit along its top and left, shaded along
    // its bottom and right.
    void bevel(ImVec2 min, ImVec2 max);
    void header();
    float windowButtons();
    // panels lists everything to draw, in the order the user dragged them
    // into; by default This PC, Now playing (while something is and Recent's
    // column, which has it as a bar, isn't shown), then the
    // server's monitors in config order.
    std::vector<Snapshot> panels() const;
    // cards draws the main column's panels; compactCards the same in the
    // compact layout.
    void cards(float width, const std::vector<Snapshot>& snaps);
    void card(const Snapshot& s, float width);
    void compactCards(float width, const std::vector<Snapshot>& snaps);
    float compactCard(const Snapshot& s, float width, Show show, float body, bool* clicked);
    void summary(const std::vector<Stat>& stats, float width);
    // chips draws a summary as chips. With live, chips with a URL open it
    // when clicked; it returns whether the mouse is over one.
    bool chips(const std::vector<Stat>& stats, float width, bool live = false);
    void refreshButton(const Snapshot& s);
    // pasteButton sends the clipboard's text to a monitor that takes it.
    void pasteButton(const Snapshot& s);
    void localButton(const Cell& cell);
    void actionButton(const std::string& monitor, const Cell& cell, bool show);
    void panelHead(const Snapshot& s);
    const Block* fullTable(const Snapshot& s, const Block& b);
    bool grip(const Snapshot& s, float y, bool shown);
    bool hoveringCard() const;
    void dragDrop();
    // block draws one block of a monitor's panel; monitor is where a
    // checklist sends its actions.
    void block(const std::string& monitor, const Block& b);
    void stats(const Block& b);
    void table(const std::string& monitor, const Block& b);
    void list(const Block& b);
    void checklist(const std::string& monitor, const Block& b);
    void itemControls(const std::string& monitor, const std::string& key, const Item& it, bool done, ImVec2 pos,
                      float w, float h);
    void mediaBlock(const Block& b);
    void feedHeading(float width);
    void feed();
    // sky draws a small landscape under Recent with the sun and moon where
    // they are now, for config_.place; nothing without one.
    void sky(float width, int64_t t);
    float skyHeight() const;
    // mediaBar is Now playing as one slim line above the landscape, there
    // whether or not anything's playing so nothing shifts.
    void mediaBar(float width);
    float mediaBarHeight() const;
    float landHeight() const;
    bool eventCard(const Event& e, int64_t t, float left);
    void temporaries();
    void cat(float width);
    float lickButton(float right, float mid);
    float appsButton(float right, float mid);
    void empty(const char* message, bool settingsButton);
    void settings();
    void diagnostics();
    void openSettings();

    HWND hwnd_;
    float captionH_ = 0;
    std::vector<ImRect> noDrag_;  // title bar buttons, from the last frame
    int hoverButton_ = -1;        // window button under the mouse, for its tooltip
    int wheelHeld_ = -1;          // the frame chips last held the wheel
    double hoverSince_ = 0;

    float scale_ = 1;
    bool dark_ = true;
    // Compact shows each monitor's summary instead of its full panel, for
    // small windows; see UseCompact.
    bool compact_ = false;
    bool fontsDirty_ = false;
    Fonts fonts_;

    Config config_;
    Model model_;
    std::unique_ptr<Client> client_;
    std::unique_ptr<ImageCache> images_;
    std::unique_ptr<LocalStats> local_;
    std::vector<Snapshot> localPanels_;  // This PC, drives and all
    std::unique_ptr<MediaWatcher> media_;
    std::unique_ptr<Grabber> grabber_;  // the watchlist's download buttons
    Uploader uploader_;                 // local files monitors ask for
    LickOfTheDay lick_;                 // hidden and shown by the button in the corner
    double lickClicked_ = -1;           // when its button was last clicked
    Launcher launcher_;                 // the apps menu's apps
    const Block* measure_ = nullptr;     // while drawing a short table: the full one, for column widths
    bool localHovered_ = false;         // a local or action button is under the mouse, so a row's click isn't for the row
    std::set<std::string> sentActions_;  // cells' actions sent, by monitor, type and ID, drawn as done
    Snapshot mediaPanel_;  // no blocks while nothing's playing
    bool mediaBarShown_ = false;  // Recent's column is up, so Now playing is a bar in it, not a panel
    std::function<void(const std::string&, const std::string&)> notify_;
    std::string lastNotifyURL_;
    // Notifications are held briefly so a burst becomes one notification.
    std::vector<Event> pending_;
    unsigned long long pendingSince_ = 0;
    std::unordered_map<std::string, float> heights_;  // card heights last frame
    std::unordered_map<std::string, double> refreshing_;
    std::unordered_map<std::string, double> pasted_;  // when each monitor was last sent the clipboard
    bool busy_ = false;
    unsigned long frameInterval_ = 0, catInterval_ = 0;
    ImRect feedRect_;  // the Recent column, last frame
    // What sky draws, worked out once a minute: today's sunrise and
    // sunset, the sun's path between them and the moon's next rise and set.
    struct SkyDay {
        int64_t minute = -1;
        std::string place;
        Crossings sun, moon;
        std::vector<Position> path;
    } skyDay_;

    // Checklist ticks shown before the server confirms them, by
    // "monitor/item", with when they were made; a snapshot replaces them.
    std::unordered_map<std::string, std::pair<bool, double>> ticked_;
    // Checklist sliders' values and colours set here, shown for 10 seconds
    // (lights report their new state a moment late), by "monitor/item";
    // unlike ticks, a snapshot doesn't replace them.
    std::unordered_map<std::string, std::pair<int, double>> levelSet_;
    std::unordered_map<std::string, std::pair<std::string, double>> colourSet_;
    float pickColour_[3] = {1, 1, 1};  // the open colour picker's wheel
    std::unordered_map<std::string, std::array<char, 512>> newItem_;  // add boxes, by monitor

    // Dragging panels into a new order: the one being dragged, and where
    // each card was drawn this frame.
    std::string dragging_;
    std::vector<std::pair<std::string, ImRect>> cardRects_;

    bool settingsOpen_ = false;
    char urlBuf_[512] = {};
    char tokenBuf_[256] = {};
    int priorityChoice_ = 1;
    bool notifyChoice_ = true;
    bool startChoice_ = false;
    OledCare oledChoice_;
    bool catChoice_ = false;
    char placeBuf_[64] = {};
    int themeChoice_ = 0;  // 0 as Windows, 1 retro

    // OLED care: when the mouse was last over the window, and until when
    // something new keeps it bright (Unix seconds).
    int64_t lastActivity_ = 0;
    int64_t wakeUntil_ = 0;
    float captionTop_ = 0;  // where the title bar starts, as the orbit moves it
};

// OpenURL opens an http or https URL in the default browser. Other schemes
// are ignored, since URLs come from the server.
void OpenURL(const std::string& url);

}  // namespace ks
