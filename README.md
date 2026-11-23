# kitchensink for Windows

A native dashboard for a [kitchensink](https://github.com/seventy-two/kitchensink) server: every monitor as a live panel,
recent events alongside, and a Windows notification for each new event.
It's written in C++ with Win32, Direct3D 11 and
[Dear ImGui](https://github.com/ocornut/imgui), and uses WinHTTP, so the
result is a single `kitchensink.exe` with nothing to install.

- Draws whatever blocks a monitor sends (stats, tables, lists, checklists,
  text) and its summary (tiles, or chips underlined in their tone's colour, or plain blocks with a
  price line under them when they send a `spark` (the watchlist, dotted on through pre-market or
  after-hours trading), a `lead` one as plain text on one line, or a
  `summaryBlock` drawn like the panel, such as its table's top rows), so new monitors
  appear without client changes. A stat marked `bar` is drawn as a bar filled to its
  `level`, in panels and summary tiles (in a tile, one line: label, bar, value,
  with its detail as a tooltip); a summary of nothing but bars shares the
  line between them; a group of them (the drives) that runs out of room carries on under itself. A stats block marked `chips` is a grid of chips (the open watchlist's charts), with buttons to switch between its `views` (Today, 1W, 1M). A `subtitleHover` subtitle shows only on hover, where
  the time it was updated goes. A monitor that sends only a summary (no blocks)
  stays a row and never opens.
- In a smaller window (under about 1100 px tall or 900 px wide at 100%
  scale) the layout tightens and each monitor becomes one row of its
  summary: the next events, your holdings' moves, today's games, the hourly
  forecast and so on. A monitor whose summary is its table cut short (the
  watchlist, OSRS) is drawn exactly as its open panel is, with the same
  subtitle, width and column widths, so opening it only adds rows. Full panels open beneath their rows, in panel order,
  as long as they fit whole; one that doesn't stays a row. An open panel is
  always its full size, never cut short to fit: click a row to open or close
  it yourself, and the column scrolls if it runs past the window. Panels have no titles: an open one keeps its controls
  in a slim strip down its right edge. The layout follows the window live
  as it's resized.
- **Ultra compact** (Settings → Layout): the compact layout at any window
  size, with every closed panel cut to one slim line: summaries as label
  and value pairs (chips underlined in their tone's colour, a small price
  line after the value when they have one), what doesn't fit counted
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
  open by itself (click it to). A panel the server marks `calm` (Reddit,
  whose hot posts always turn over) doesn't count summary changes: it stays
  at the bottom unless it matters now (a J-Mod's post is up) or had an event
  in the last 12 hours. Each keeps the dragged order among its
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
- A panel whose monitor sends `settings` (GE prices: its items and alerts)
  also shows a gear on hover, opening those blocks in a window over the
  dashboard. Table cells can be text boxes (`input`): Enter sends their
  action with what was typed.
- **Monitor brightness**: a slim slider under the landscape for the
  brightness of the monitor the dashboard is on, set over DDC/CI as the
  monitor's own buttons would (VCP 0x10). Drag along it, or turn the wheel
  over it for steps of 5. It reads the monitor again every minute, and
  doesn't show at all if the monitor doesn't answer DDC/CI.
- **Volume**: the same slider above it for the default speakers' volume
  (Windows only): drag, or the wheel for steps of 2; a click on the speaker
  mutes or unmutes, and the tooltip names the speakers. It follows changes
  made elsewhere (the volume keys, the flyout, a new default device).
  Right-click the speaker for the **Volume mixer** (a slider and mute for
  each app with sound, over the card), **Sound devices** (the classic
  panel, to pick or disable speakers) or **Sound settings**.
- **Network**: above the volume, the network's speeds down and up (the
  physical adapters that are up, read every second, always in MB/s so it
  sits at 0 or 0.1 while little is moving; Windows only). A click
  opens it to a graph of the last two minutes, down filled in the accent
  and up as a green line, scaled to the busiest moment; another closes it,
  and that's remembered (`"netExpanded"`).
- **Now playing**: what's playing on this PC, from Windows' media
  sessions (the same source as the volume keys' media pop-up), so Spotify,
  browsers and other players all show. A slim bar at the bottom of the
  Recent column, just above the landscape, always there (it says "Nothing
  playing" when nothing is) so nothing moves when a track starts or stops:
  track and artist, previous / play-pause / next and a thin
  progress line; hover it for the rest. When the window is too narrow for
  the Recent column it's a panel among the others instead. A playing app
  wins over a paused one, and paused tracks stay for as long as Windows
  keeps their session, as in its own media pop-up.
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
- **Batteries**: when the server has a `battery` monitor and Squeak is
  installed (`%LOCALAPPDATA%\Programs\squeak\squeak.exe`), every two
  minutes the client runs `squeak -battery <file>` (hidden, below normal
  priority) and sends the devices' battery and charging state as a
  `battery` action, so the server can say when a mouse has finished
  charging.
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
  lit with a dot of the item's colour while on; clicking one turns it on
  or off, and right-clicking a light opens its tile (brightness and
  colour) in a popup under it.
- Keeps the server's event stream open, reconnects with backoff, and
  catches up on events missed while offline. A burst of events becomes one
  notification.
- Draws its own title bar, which keeps Windows' resizing, snapping, shadow
  and double-click to maximise.
- A clock beside the title bar's buttons: the date, then the time
  (24-hour), both muted, e.g. "Sat 21 Nov 18:49".
- Closing the window keeps it running in the tray; **Quit** is in the tray
  icon's menu. It can start with Windows (minimised to the tray).
- An eye button in the title bar, there when a monitor sends private
  figures (a portfolio's value and gains), hides or shows them at a click:
  their blocks and summary chips go, and it's remembered.
- **Cat** (in settings, off by default): Mag, a black cat drawn in code,
  sleeps above the Recent column (curled up, as a loaf, on his side, or
  belly up on warm days), breathing, with the odd tail twitch and Z's, and
  now and then stirs to lift his head and look about. Every couple of
  minutes he wakes, stretches, yawns, arches his back, sometimes wanders to
  another spot, and finds something to do: a wash, a scratch behind the
  ear, a slow blink at you, a pounce, a drink of milk, hunting a toy mouse,
  a mad sprint, or sitting in a cardboard box. When the panels leave room
  at the bottom of the window he now and then walks off the right and comes
  in from the left down there for two or three minutes, where he can jump
  up onto the last panel's edge (and push a mug off it); if a panel opens
  and the room goes, he's back up top.
  He's livelier at dawn and dusk, sleepier at night and when the forecast
  says it's raining, and from 07:25 and 17:55 he waits for breakfast and
  dinner. He lifts his head when something new arrives, notices the mouse
  now and then, and purrs when clicked (too many clicks and he stalks off).
  Asleep he needs ten frames a second, awake about thirty, and none while
  the window's minimised or dimmed.
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
  `"appGroups"` sorts some of them under headings, in order, e.g.
  `[{"name": "System", "apps": ["cooler", "squeak"]}]`; the rest go under
  "Apps" above them. Private apps (below) in no group go under "Private",
  after the groups, or where a `{"name": "Private"}` group stands. The
  pinned apps (`"pins"`, below) sit at the top.
  Apps named in `"privateApps"` can be hidden from the menu with **Hide
  private apps** in the eye's right-click menu (the eye shows whenever
  there are private apps), and it's remembered.
- **Games menu** (Windows only): the joystick right of the apps flag lists
  the installed games, most played first, found where each store records them: Steam's
  library manifests, GOG's registry keys, Epic's `.item` manifests, the EA
  app's `EA Games` keys, Ubisoft Connect's installs and Xbox app games
  (Store packages with a `MicrosoftGame.config`). A click starts the game
  through its store (`steam://`, `origin2://`, Epic's and Ubisoft's links,
  GOG's own exe), or brings up its window while it's running (a green
  dot). Right-click a game to rename it (`"gameNames"`, by the name it was
  found as; clear the name to go back) or hide it (`"hiddenGames"`); "Hidden" at the
  bottom puts it back. It looks again when opened, at most once a minute.
  The order: how many of the last 90 days each game was seen running
  (any window of its, checked every five minutes however it was started;
  kept a year in `"gamePlays"`, by the name it was found as; a store's
  last played day, from Steam, counts too), then the most recent, then A-Z.
  Games from no store are found in `"gameFolders"` (each folder inside one
  is a game; its exe is the one named like the folder, else the nearest
  the top), and `"gameShortcuts"` lists shortcuts as games (as in
  `"pins"`, e.g. `Jagex Launcher.lnk`).
- **Instead of the Windows taskbar** (Windows only): the dashboard can stand in
  for it, and **Hide the Windows taskbar** in Settings hides it on every
  monitor while the dashboard runs (set to auto-hide, so windows get the
  whole screen, and hidden so it doesn't slide up at the edge). It comes
  back as it was when the dashboard quits or Windows signs out, and is
  hidden again if Explorer restarts. The Start menu and Windows' shortcuts
  still work.
  - **Pinned apps** go at the top of the apps menu (`"pins"`) or in the
    title bar between the apps flag and Mango (`"barPins"`). In the menu a
    click brings up the app's window used last, or starts it. In the title
    bar they show their icons (Windows Terminal is drawn instead, grey like
    Mango beside it until hovered), fainter while not running; a click brings up the app's window, the next one if one's
    in front already, minimises it if it's the only one, or starts the app. A middle click starts another,
    in either place. Each is a shortcut's file name among the taskbar's
    pinned ones (`Firefox.lnk`), a full path to a `.lnk`, a Store app's ID
    (`Microsoft.WindowsTerminalPreview_8wekyb3d8bbwe!App`, shown with its
    unplated logo as the taskbar does), or `explorer` for File Explorer
    (opens This PC).
  - **Windows**: the open windows the taskbar would show, a slim line each,
    sit above the lights in the side column, in the order they opened. The
    one in front is marked; one asking for attention is tinted. A click
    brings a window up, or minimises it if it's in front; a middle click
    closes it; right-click for Restore, Minimise, Maximise and Close. More
    than ten and the card scrolls.
  - **Tray**: the notification area's icons, read from Explorer's own (hidden)
    tray, sit above the network, volume and brightness. Explorer's own
    volume and network icons are left out, as those cards stand in for
    them. Clicks go to each icon's app
    just as Explorer sends them (in its icon version's format), so menus and
    windows open as from the tray, beside the pointer. The chevron cuts it
    to one slim line, "Tray (n)", and it's remembered.
  - Clicks on these leave whatever was in front where it is (as the
    taskbar does), so the dashboard doesn't take the front from the window
    you're minimising or the tray app you're opening.
- Scripts can quit the dashboard cleanly (as the tray's Quit, taking its tray
  icon with it and putting the taskbar back) by posting it the registered
  window message `kitchensink-quit`.
- There are no scrollbars: the Recent column, the panels under it, the
  and the panels column scroll with the mouse wheel.
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
g++ -std=c++17 -I src -I third_party -I third_party/imgui tests/tests.cpp src/gamescan.cpp src/model.cpp src/sse.cpp src/local.cpp src/grab.cpp \
    src/cat.cpp src/weather_icon.cpp src/retro.cpp src/sky.cpp third_party/imgui/imgui*.cpp && ./a.out
```

## Linux

The same dashboard also builds for Linux (tested on Bazzite, KDE on
Wayland): an SDL3 window drawn with OpenGL instead of Win32 and Direct3D,
libcurl instead of WinHTTP, stb_image instead of WIC. Only the dashboard
itself: the PC-specific parts (This PC and Drives, Now playing, the apps
menu, Lick of the Day, watchgrab's buttons, RuneLite uploads, Squeak's battery readings, turning the
displays off, monitor brightness) are stubbed out in `linux/stubs.cpp`. There's no tray, so
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
| `src/battery.cpp` | runs Squeak for the battery monitor's readings |
| `src/brightness.cpp` | reads and sets the monitor's brightness over DDC/CI, on a thread of its own |
| `src/volume.cpp` | reads and sets the default speakers' volume and mute (Core Audio) |
| `src/netspeed.cpp` | reads the network adapters' byte counters; `src/netrate.h` turns them into speeds, tested anywhere |
| `src/lick.cpp` | starts Lick of the Day with the dashboard, and hides and shows it |
| `src/gamescan.cpp`, `src/games.cpp` | the games menu: reading the stores' records (portable), and finding, starting and drawing the games on Windows |
| `src/launcher.cpp` | the apps menu and pinned apps: finding, starting and bringing up the apps, and their icons |
| `src/openwindows.cpp` | the open windows the taskbar would show, on a thread of its own, with the shell's window events |
| `src/tray.cpp` | reads the tray's icons from Explorer and sends them clicks |
| `src/taskbar.cpp` | hides the Windows taskbar and puts it back |
| `src/shell.h` | the rules behind those (which windows show, the tray's message formats, pins), tested anywhere |
| `src/winicon.cpp` | icons as PNGs from icon handles, the shell and Store apps' packages |
| `src/images.cpp` | thumbnail download, WIC decoding and textures |
| `src/config.cpp` | settings and start-with-Windows |
| `src/theme.cpp` | palettes, fonts and ImGui style |
| `src/retro.cpp` | the retro theme's text shadows and clock |
| `src/cat.cpp` | Mag the cat: his poses, behaviour and drawing |
| `src/weather_icon.cpp` | weather icons for the forecast's chips, drawn from shapes |
| `src/sky.cpp` | where the sun and moon are, sunrise and sunset and the moon's phase, for the landscape under Recent |
| `src/canvas.h` | the simple drawing interface those two draw through |

`third_party` holds Dear ImGui v1.91.9b and nlohmann/json v3.11.3, both MIT
licensed.

## Licence

MIT
