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
- Adds a panel about the PC it runs on, **This PC**: CPU, memory, GPU and
  video memory, each with a minute's history, and each fixed drive's use.
  It reads Windows' own counters once a second, only while the window is
  showing, and works with the server offline.
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
- **Files for monitors**: a monitor can ask for a file on this PC
  (`upload`): every 5 seconds the client looks at its last-write time and,
  when the wanted lines have changed (and once each start), sends them.
  The farming monitor asks for RuneLite's Time Tracking data this way.
- Checklists (the `todo` monitor) can be ticked, added to and deleted
  from; a tick shows at once, ahead of the server's update.
- Checklist items with a `slider` or `colour` get a slider, sent when let
  go, and a swatch opening presets and a colour wheel, lined up after the
  titles in place of the up and down buttons. A `switch` block (the `lights` monitor) draws each item as a
  tile: a round colour swatch opening presets and a colour wheel, the
  title over its subtitle, and an on/off switch. Clicking the tile toggles
  it; its slider's level fills the tile in the item's colour, and dragging
  across the tile sets it, sent when let go.
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

## Layout

| File | What it does |
| --- | --- |
| `src/main.cpp` | window, Direct3D, the tray icon, notifications and the message loop |
| `src/app.cpp` | the UI: header, panels, block drawing, event feed, settings |
| `src/model.cpp` | snapshots and events parsed from JSON, and the dashboard's state |
| `src/sse.cpp` | server-sent events parser |
| `src/net.cpp` | WinHTTP requests and the stream client |
| `src/media.cpp` | reads and controls Windows' media sessions (C++/WinRT) |
| `src/sysinfo.cpp` | reads CPU, memory, GPU and drive use (performance counters, DXGI) |
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
