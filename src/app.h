// The dashboard: state from the stream, drawn with ImGui.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "battery.h"
#include "brightness.h"
#include "cat.h"
#include "config.h"
#include "grabber.h"
#include "images.h"
#include "imgui_internal.h"
#include "launcher.h"
#include "lick.h"
#include "model.h"
#include "net.h"
#include "media.h"
#include "openwindows.h"
#include "platform.h"
#include "sky.h"
#include "sysinfo.h"
#include "taskbar.h"
#include "theme.h"
#include "tray.h"
#include "upload.h"

namespace ks {

class App {
public:
    // wake is called from background threads when there's something new.
    // notify shows a desktop notification. The header doubles as the
    // window's title bar, with its own window buttons.
    App(NativeWindow hwnd, GpuDevice device, std::function<void()> wake,
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

    // ShellEvent passes on the shell's word that a window came, went,
    // changed or was activated (RegisterShellHookWindow's messages).
    void ShellEvent(unsigned code, uintptr_t window);
    // TaskbarCreated is for Explorer having (re)started: the tray is read
    // again and the taskbar hidden again if it should be.
    void TaskbarCreated();
    // RestoreTaskbar puts the Windows taskbar back, e.g. at sign-out.
    void RestoreTaskbar();
    // NoActivate reports whether a click at a point in client pixels should
    // leave the window in front where it is, as the taskbar does: on the
    // open windows, the pins and the tray.
    bool NoActivate(float x, float y) const;

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
    bool card(const Snapshot& s, float width);
    bool collapseButton(bool show, const std::string& id);
    bool slimRow(const std::string& id) const;
    const Snapshot* switchesPanel() const;
    void switchesBar(const Snapshot& s, float width);
    void rowStyleMenu(const std::string& id);
    void compactCards(float width, const std::vector<Snapshot>& snaps);
    float compactCard(const Snapshot& s, float width, Show show, bool* clicked);
    void summary(const std::vector<Stat>& stats, float width);
    bool slimStats(const std::vector<Stat>& stats, float width, bool asChips, bool live);
    // chips draws a summary as chips. With live, chips with a URL open it
    // when clicked; it returns whether the mouse is over one.
    bool chips(const std::vector<Stat>& stats, float width, bool live = false, bool grid = false);
    void refreshButton(const Snapshot& s);
    // pasteButton sends the clipboard's text to a monitor that takes it.
    void pasteButton(const Snapshot& s);
    // gearButton opens a panel's own settings (see panelSettings).
    void gearButton(const Snapshot& s);
    // panelSettings draws the settings blocks of the panel whose gear was
    // clicked (panelSettings_), in a window over the dashboard.
    void panelSettings();
    // inputCell is a table cell's text box: Enter sends its action.
    void inputCell(const std::string& monitor, const Cell& cell);
    void localButton(const Cell& cell);
    void actionButton(const std::string& monitor, const Cell& cell, bool show);
    void choiceCell(const std::string& monitor, const Cell& cell);
    void panelHead(const Snapshot& s);
    std::vector<const Block*> fullTables(const Snapshot& s, const Block& b);
    bool grip(const Snapshot& s, float y, bool shown);
    bool hoveringCard() const;
    void dragDrop();
    // block draws one block of a monitor's panel; monitor is where a
    // checklist sends its actions.
    void block(const std::string& monitor, const Block& b);
    void stats(const Block& b);
    void chipsBlock(const std::string& monitor, const Block& b);
    void table(const std::string& monitor, const Block& b);
    void list(const Block& b);
    void checklist(const std::string& monitor, const Block& b);
    void switchTile(const std::string& monitor, const std::string& key, const Item& it, bool done, float w,
                    float titleW);
    void checklistButtons(const std::string& monitor, const Block& b);
    void colourPopup(const std::string& monitor, const std::string& key, const Item& it);
    void itemControls(const std::string& monitor, const std::string& key, const Item& it, bool done, ImVec2 pos,
                      float w, float h);
    void mediaBlock(const Block& b);
    void feedHeading(float width);
    void feed();
    // sky draws a small landscape under Recent with the sun and moon where
    // they are now, for config_.place; nothing without one.
    void sky(float width, int64_t t);
    float skyHeight() const;
    // mediaBar is Now playing as slim lines (one per app) above the landscape, there
    // whether or not anything's playing so nothing shifts.
    void mediaBar(float width);
    void mediaLine(float width, const Block* b);
    float mediaBarHeight() const;
    float mediaLineHeight() const;
    // brightnessBar is a slider for the monitor's brightness under the
    // landscape; nothing if the monitor can't be set (or on Linux).
    void brightnessBar(float width);
    float brightnessBarHeight();
    float landHeight() const;
    bool eventCard(const Event& e, int64_t t, float left);
    void temporaries();
    void cat(float width);
    // catBottom draws Mag in the space at the bottom of the panels, when
    // he's there; gapBelowCards measures that space, in the cards' child.
    void catBottom();
    void gapBelowCards();
    void drawMag(ImDrawList* dl, Vec2f floor, float size);
    float lickButton(float right, float mid);
    float mangoButton(float right, float mid);
    float dimButton(float right, float mid);
    float displayOffButton(float right, float mid);
    float appsButton(float right, float mid);
    // pinButtons draws the pinned apps' icons, ending at right, and returns
    // where they start.
    float pinButtons(float right, float mid);
    // windowsBar lists the open windows, a slim line each, above the
    // lights; trayBar the tray's icons above the brightness slider. Each
    // draws nothing if it has nothing to show.
    // They're given this frame's lists, and their heights are worked out
    // before the column is laid out, so a window or icon coming or going
    // doesn't push the cards below about for a frame.
    void windowsBar(const std::vector<OpenWindow>& list, float width);
    void trayBar(const std::vector<TrayIcon>& icons, float width);
    float windowsBarHeight(size_t windows);
    float trayBarHeight(size_t icons, float width);
    // applyTaskbar hides or shows the Windows taskbar as config_ says.
    void applyTaskbar();
    std::vector<LaunchApp> shownApps();
    void empty(const char* message, bool settingsButton);
    void settings();
    void diagnostics();
    void openSettings();

    // batteries sends Squeak's readings to the server's battery monitor,
    // if it has one.
    void batteries();
    bool mouseOverWindow() const;
    // Ordering panels by what's new: see PanelTier.
    void noteSummary(const Snapshot& s);
    Tier tierOf(const Snapshot& s, int64_t t) const;
    bool stale(const Snapshot& s) const;
    bool newsDirty_ = false;
    double newsSaved_ = -1e9;
    bool stackSwitches_ = false;  // drawing the side column's switches, one group a line
    bool sideShown_ = false;
    float switchesH_ = 0;  // the side column's lights card, last frame  // the side column (Recent) is up
    bool ultra_ = false;       // the ultra compact layout
    bool slimBlock_ = false;   // drawing a short block on an ultra compact line
    NativeWindow hwnd_;
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
    std::unique_ptr<BatteryWatcher> battery_;  // Squeak's readings, for the battery monitor
    std::unique_ptr<Brightness> brightness_;  // the monitor's brightness, over DDC/CI
    HiddenApp lick_{L"dailylick"};      // Lick of the Day, hidden and shown by the guitar
    double lickClicked_ = -1;           // when its button was last clicked
    HiddenApp mango_{L"mango", L"-docked"};  // Mango (Spotify), docked to the right, shown by the mango
    double mangoClicked_ = -1;
    bool mangoRaised_ = false;          // raised since the last click
    Launcher launcher_;                 // the apps menu's apps, and the pins
    std::unique_ptr<OpenWindows> windows_;  // the open windows, for the Windows card and pins
    std::unique_ptr<Tray> tray_;        // the notification area's icons
    Taskbar taskbar_;                   // hides the Windows taskbar while it's wanted
    double taskbarChecked_ = -1e9;
    // Explorer shows the taskbar again as it starts auto-hiding, so for a
    // few seconds after hiding it, it's checked every frame, not every 5 s.
    double taskbarWatchUntil_ = 0;
    float windowsH_ = 0, trayH_ = 0;    // the side column's Windows and Tray cards, last frame
    std::vector<ImRect> noActivate_, noActivateNext_;  // see NoActivate: last frame's, and this one's
    uintptr_t windowMenu_ = 0;          // the window whose right-click menu is open
    // While drawing a short table: the full ones it's cut from (a panel
    // can have several of the same shape), for column widths.
    std::vector<const Block*> measure_;
    std::string panelSettings_;  // the monitor whose settings are open
    // Each text box's text, by monitor, action and id, with the server's
    // value it started from: a new value replaces it unless it's being
    // edited.
    struct InputState {
        char buf[256] = {};
        std::string from;
    };
    std::map<std::string, InputState> inputs_;
    bool localHovered_ = false;         // a local or action button is under the mouse, so a row's click isn't for the row
    std::set<std::string> sentActions_;  // cells' actions sent, by monitor, type and ID, drawn as done
    // Options picked in drop-downs, by monitor, action and ID, shown until
    // the server's snapshot agrees (or 15s pass): the value and when.
    std::map<std::string, std::pair<std::string, double>> pickedChoices_;
    Snapshot mediaPanel_;  // no blocks while nothing's playing
    bool mediaBarShown_ = false;  // Recent's column is up, so Now playing is a bar in it, not a panel
    std::function<void(const std::string&, const std::string&)> notify_;
    std::string lastNotifyURL_;
    // Notifications are held briefly so a burst becomes one notification.
    std::vector<Event> pending_;
    unsigned long long pendingSince_ = 0;
    std::unordered_map<std::string, float> heights_;  // card heights last frame
    std::unordered_map<std::string, double> refreshing_;
    // Which view each chips block shows, by monitor (Today, 1W, 1M).
    std::unordered_map<std::string, size_t> chipView_;
    std::unordered_map<std::string, double> pasted_;  // when each monitor was last sent the clipboard
    bool busy_ = false;
    unsigned long frameInterval_ = 0, catInterval_ = 0;
    ImRect feedRect_;  // the Recent column, last frame
    // Mag, the cat: his state, how he was this frame, the space below the
    // panels last frame (empty for none), where his floors are and what he
    // covered last frame (for the mouse), and the newest event he's seen.
    Mag mag_;
    Cat magNow_;
    ImRect gapRect_, magRect_;
    Vec2f magTop_, magBottom_;
    int64_t magEventSeen_ = -1;
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
    bool hideTaskbarChoice_ = false;
    OledCare oledChoice_;
    bool catChoice_ = false;
    char placeBuf_[64] = {};
    int themeChoice_ = 0;  // 0 as Windows, 1 retro
    int layoutChoice_ = 0;  // 0 by window size, 1 ultra compact
    bool relevanceChoice_ = true;

    // OLED care: when the mouse was last over the window, and until when
    // something new keeps it bright (Unix seconds).
    int64_t lastActivity_ = 0;
    int64_t wakeUntil_ = 0;
    float captionTop_ = 0;  // where the title bar starts, as the orbit moves it
};

// OpenURL opens an http or https URL in the default browser. Other schemes
// are ignored, since URLs come from the server.
// If app names an app installed under %LOCALAPPDATA%\Programs (app\app.exe),
// it's run with the URL instead.
void OpenURL(const std::string& url, const std::string& app = "");

}  // namespace ks
