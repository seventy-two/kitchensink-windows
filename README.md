# kitchensink for Windows

A native dashboard for a [kitchensink](https://github.com/seventy-two/kitchensink) server: every monitor as a live panel,
recent events alongside, and a Windows notification for each new event.
It's written in C++ with Win32, Direct3D 11 and
[Dear ImGui](https://github.com/ocornut/imgui), and uses WinHTTP, so the
result is a single `kitchensink.exe` with nothing to install.

- Draws whatever blocks a monitor sends (stats, tables, lists, checklists,
  text) and its summary (tiles, or chips tinted by tone, a `lead` one as plain text on one line, or a
  `summaryBlock` drawn like the panel, such as its table's top rows), so new monitors
  appear without client changes.
- In a smaller window (under about 1100 px tall or 900 px wide at 100%
  scale) the layout tightens and each monitor becomes one row of its
  summary: the next events, your holdings' moves, today's games, the hourly
  forecast and so on. A monitor whose summary is its table cut short (the
  watchlist, OSRS) is drawn exactly as its open panel is, with the same
  subtitle, width and column widths, so opening it only adds rows. Full panels open beneath their rows, in panel order,
  as long as they fit; the first that doesn't takes whatever height is left
  and scrolls, if that fits a table header and four rows. Click a row to
  open or close it yourself. Panels have no titles: an open one keeps its controls
  in a slim strip down its right edge. The layout follows the window live
  as it's resized.
- **Ultra compact** (Settings → Layout): the compact layout at any window
  size, with every closed panel cut to one slim line: summaries as label
  and value pairs (chips as small tinted blocks), what doesn't fit counted
  as "+n", no subtitles, a short table's first row only, without details,
  and smaller buttons. Closed rows leave out table headings; an open panel is always
  the full one, headings and all.
  Right-click a panel's chevron to choose Compact or Ultra compact for
  that panel alone (kept as `rowStyle`). A stat's `brief`, when the server
  sends one, replaces its value on these lines (Food names the day only
  up to tomorrow).
- **Order by what's new** (Settings, on by default): a panel the server
  says matters now (a game on, an appointment soon) or that had an event
  in the last two hours rises to the top; one with neither an event nor a
  change to its summary for 12 hours sinks to the bottom, fades and doesn't
  open by itself (click it to). Each keeps the dragged order among its
  own. When each monitor last had something new is kept in `config.json`
  as `news`.
- In the wide layout, where every panel fits open, a panel's chevron (top
  right, on hover) folds it to its summary row; click the row to open it
  again. The choice is remembered and carries over to the compact layout.
- Adds a panel about the PC it runs on, **This PC**: CPU, memory, GPU and
  video memory, each with a minute's history, and each fixed drive's use.
  It reads Windows' own counters once a second, only while the window is
  showing, and works with the server offline. The GPU's temperature comes
  from NVIDIA's driver (NVML); the CPU's temperature and the GPU's fan
  speed come from MSI Afterburner's monitoring while it runs (Windows has
  no CPU temperature of its own on most desktops). They show under the
  CPU and GPU use (in place of the GPU's name); a part that runs hot turns
  its tile amber or red.
- Panels show their controls (grip, refresh, open/close) only while the
  mouse is over them. Drag a panel by the dots in its left margin to
  reorder; the order also decides which open first in the compact layout.
- **Now playing**: what's playing on this PC, from Windows' media
  sessions (the same source as the volume keys' media pop-up), so Spotify,
  browsers and other players all show. A slim bar at the bottom of the
  Recent column, just above the landscape, always there (it says "Nothing
  playing" when nothing is) so nothing moves when a track starts or stops:
  track and artist, previous / play-pause / next and a thin
  progress line; hover it for the rest. When the window is too narrow for
  the Recent column it's a panel among the others instead. A playing app
  wins over a paused one, and a paused track hides ten minutes after it
  last played.
- **Buttons for apps on this PC**: a table cell can name a local app
  (`local`), drawn as a button only where that app is installed. The one
  known is watchgrab (`%LOCALAPPDATA%\Programs\watchgrab\watchgrab.exe`),
  for the Letterboxd watchlist: its download button runs
  `watchgrab -grab <film>` in the background, and each row shows how the
  film stands from watchgrab's `state.json`: added (green tick), already
  had (grey tick), no release yet (amber, click to look again) or a failure
  (red, click to retry), with the detail on hover.
- **Links that open in an app**: a link (table cell or list item) can name
  an app to open it with (`open`). If `%LOCALAPPDATA%\Programs\<app>\<app>.exe`
  exists, clicking runs it with the URL; otherwise the browser opens the
  link as usual. The Reddit panel uses this for lurk, a Reddit reader. The
  name must be lower-case letters, digits and hyphens, so a server can't
  point it anywhere else.
- **Files for monitors**: a monitor can ask for a file on this PC
  (`upload`): every 5 seconds (or every `every` seconds, if the monitor
  says) the client looks at its last-write time and, when the wanted lines
  have changed (and once each start), sends them. The farming monitor asks
  for RuneLite's Time Tracking data this way, and the osrs monitor for the
  Delve Calculator's counts once a day.
- A monitor whose short view is a row of switch buttons (the lights)
  lives in the Recent column, above Now playing and the landscape, as a small card
  that's always there, each button with a bulb lit in the light's colour
  (a warm white without one) while on. Switches named alike but for a
  number at the end (Hallway 1, Hallway 2) are one joined button, the rest
  of the group named only by number; the server's row for the whole room
  ("Hallway", switching them together) heads it: Hallway | 1 | 2. In the card each group has a line
  of its own, the card's full width. Without the Recent column it's a
  panel like the rest.
- Checklists (the `todo` monitor) can be ticked, added to and deleted
  from; a tick shows at once, ahead of the server's update.
- Checklist items with a `slider` or `colour` get a slider, sent when let
  go, and a swatch opening presets and a colour wheel, lined up after the
  titles in place of the up and down buttons. A `switch` block (the `lights` monitor) draws each item as a
  slim row, flat on the panel: its title, its slider as a thin line (click or drag along it,
  sent when let go), a round colour swatch opening presets and a colour
  wheel, and an on/off switch. Clicking elsewhere on the row toggles it;
  a warning subtitle (e.g. unavailable) shows before the swatch. A `buttons` block (the lights'
  summary block, drawn when the panel is short) is a row of small buttons,
  lit with a dot of the item's colour while on; clicking one opens its
  tile in a popup under it.
- Keeps the server's event stream open, reconnects with backoff, and
  catches up on events missed while offline. A burst of events becomes one
  notification.
- Draws its own title bar, which keeps Windows' resizing, snapping, shadow
  and double-click to maximise.
- Closing the window keeps it running in the tray; **Quit** is in the tray
  icon's menu. It can start with Windows (minimised to the tray).
- An eye button in the title bar, there when a monitor sends private
  figures (a portfolio's value and gains), hides or shows them at a click:
  their blocks and summary chips go, and it's remembered.
- **Cat** (in settings, off by default): a black cat, drawn in code,
  sleeps curled up above the Recent column, breathing, with the odd tail
  twitch and Z's, and now and then stirs to lift her head and look about.
  Every 100 seconds or so she wakes, stretches, yawns, arches her back,
  sometimes wanders to another spot, and finds something to do: a wash
  (licking a paw, then her face), a scratch behind the ear, a slow blink at
  you, a pounce, a drink from a bowl of milk that slides in from the side,
  or hunting a toy mouse (stalking, pouncing, batting it along). Some days
  she kneads her bed before curling up again. Asleep she needs ten frames a
  second, awake about thirty, and none while the window's minimised or
  dimmed.
- **Lick of the Day**, if it's installed (`%LOCALAPPDATA%\Programs\dailylick`),
  starts with the dashboard and has no taskbar button of its own: the
  electric guitar in the title bar, by the eye, hides and shows it
  (muted while it's hidden).
- **Apps**: a greyed-out Windows 95 flag left of the guitar opens a menu of
  your own apps, A to Z, each with its icon and a green dot while it's running.
  Picking one brings its window to the front, or starts it. List them in
  `config.json` as `"apps"`: a name for
  `%LOCALAPPDATA%\Programs\<name>\<name>.exe`, or a full path to an exe.
  Ones that aren't there are left out, and with none the flag doesn't show.
  Apps named in `"privateApps"` can be hidden from the menu with **Hide
  private apps** in the eye's right-click menu (the eye shows whenever
  there are private apps), and it's remembered.
- There are no scrollbars: the Recent column, the panels under it, the
  panels column and a panel cut short to fit scroll with the mouse wheel.
- In the bottom-right corner, under Recent, a strip of countryside shows the sky as it is now: the
  sun and moon at their bearing (north at both ends, south in the middle)
  and height, the moon in its phase, today's path of the sun dotted across,
  stars at night, and sunrise, sunset and the phase beneath; hover it for
  the details. It needs where you are, set in Settings as "latitude,
  longitude"; without it there's no landscape.
- Hovering a Recent card shows an × that clears it; the × by the heading
  (shown while the mouse is over the column) clears them all. Cleared events stay cleared after a restart (they're
  remembered in `config.json` as `cleared`), and only for this install.
- Events go from Recent by themselves once they stop being relevant (the
  server's `until`: a day after for anything below high priority, sooner
  for weather alerts); high-priority ones stay until cleared.
- **OLED care** (in settings, each part off by default) guards OLED
  screens against burn-in: **Shift** moves the contents round a 6px circle
  every three minutes, **True black** is a black theme with dimmer text
  and fainter borders, **Dim when idle** dims everything after ten minutes
  without the mouse over the window (until hovered, or for two minutes
  after something new), and **Swap sides** moves the Recent column across
  every other hour.
- The moon in the title bar dims the window at once, whatever the mouse
  does, until it's clicked again (remembered as `forceDim`).
- Follows the Windows light or dark app theme (set `KITCHENSINK_THEME` to
  `light` or `dark` to override) and per-monitor DPI.
- **Retro** (in settings): a theme after the Weather Channel's 90s
  WeatherStar screens, with a deep blue sky fading to purple, raised blue
  panels, bold lettering with black drop shadows, yellow headings, an amber
  rule under the title bar and a clock beside the buttons.
- Settings live in `%APPDATA%\kitchensink\config.json`; the token is
  encrypted with DPAPI, so only your Windows account can read it.

## Building

Needs Visual Studio 2022 (or its Build Tools) with the C++ workload.

```bat
build.bat
```

This runs the tests, then builds `build\kitchensink.exe`. The portable
parts (the data model and the server-sent events parser) also build and
test on Linux:

```sh
g++ -std=c++17 -I src -I third_party -I third_party/imgui tests/tests.cpp src/model.cpp src/sse.cpp src/local.cpp src/grab.cpp \
    src/cat.cpp src/weather_icon.cpp src/retro.cpp src/sky.cpp third_party/imgui/imgui*.cpp && ./a.out
```

## Linux

The same dashboard also builds for Linux (tested on Bazzite, KDE on
Wayland): an SDL3 window drawn with OpenGL instead of Win32 and Direct3D,
libcurl instead of WinHTTP, stb_image instead of WIC. Only the dashboard
itself: the PC-specific parts (This PC and Drives, Now playing, the apps
menu, Lick of the Day, watchgrab's buttons, RuneLite uploads, turning the
displays off) are stubbed out in `linux/stubs.cpp`. There's no tray, so
closing the window quits. Notifications go through `notify-send`.

- Fonts come from fontconfig: Noto Sans (Liberation Sans Bold for the retro
  theme), and the icons from Symbols Nerd Font (Codicons and Font Awesome
  standing in for Segoe's). Light or dark follows the desktop.
- Settings are in `~/.config/kitchensink/config.json`, readable by the user
  only; the token is kept there as it is (no DPAPI). "Start when I log in"
  writes `~/.config/autostart/kitchensink.desktop`.
- Build (needs g++, pkg-config, SDL3 and libcurl development files):

```sh
make -f linux/Makefile -j4        # build/kitchensink, then the tests
```

- Install: `build/kitchensink` to `~/.local/bin`, `linux/kitchensink.desktop`
  to `~/.local/share/applications` and `linux/kitchensink.png` to
  `~/.local/share/icons/hicolor/256x256/apps`.

## Layout

| File | What it does |
| --- | --- |
| `src/main.cpp` | window, Direct3D, the tray icon, notifications and the message loop |
| `src/platform.h` | the window and clock calls the UI needs (`platform_win.cpp`, `linux/platform_linux.cpp`) |
| `linux/` | the Linux entry point, libcurl HTTP, stubs and Makefile |
| `src/app.cpp` | the UI: header, panels, block drawing, event feed, settings |
| `src/model.cpp` | snapshots and events parsed from JSON, and the dashboard's state |
| `src/sse.cpp` | server-sent events parser |
| `src/net.cpp` | WinHTTP requests and the stream client |
| `src/media.cpp` | reads and controls Windows' media sessions (C++/WinRT) |
| `src/sysinfo.cpp` | reads CPU, memory, GPU and drive use (performance counters, DXGI), temperatures and fan (NVML, Afterburner's shared memory) |
| `src/local.cpp` | turns those readings into the This PC panel |
| `src/grab.cpp` | watchgrab's state and results, and how each film's button looks |
| `src/grabber.cpp` | runs watchgrab for the download buttons and follows its state file |
| `src/upload.cpp` | sends monitors the local files they ask for |
| `src/lick.cpp` | starts Lick of the Day with the dashboard, and hides and shows it |
| `src/launcher.cpp` | the apps menu: finding, starting and bringing up the apps, and their icons |
| `src/images.cpp` | thumbnail download, WIC decoding and textures |
| `src/config.cpp` | settings and start-with-Windows |
| `src/theme.cpp` | palettes, fonts and ImGui style |
| `src/retro.cpp` | the retro theme's text shadows and clock |
| `src/cat.cpp` | the cat above Recent: her poses, routine and drawing |
| `src/weather_icon.cpp` | weather icons for the forecast's chips, drawn from shapes |
| `src/sky.cpp` | where the sun and moon are, sunrise and sunset and the moon's phase, for the landscape under Recent |
| `src/canvas.h` | the simple drawing interface those two draw through |

`third_party` holds Dear ImGui v1.91.9b and nlohmann/json v3.11.3, both MIT
licensed.

## Licence

MIT
