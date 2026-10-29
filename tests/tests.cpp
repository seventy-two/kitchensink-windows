// Tests for the dashboard's portable code. Build and run with build.bat, or
// on Linux with the command in the README.
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

#include <cmath>

#include "../src/cat.h"
#include "../src/grab.h"
#include "../src/local.h"
#include "../src/model.h"
#include "../src/retro.h"
#include "../src/sky.h"
#include "../src/sse.h"
#include "../src/weather_icon.h"

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

#define CHECK_EQ(a, b)                                                 \
    do {                                                               \
        auto va = (a);                                                 \
        auto vb = (b);                                                 \
        if (!(va == vb)) {                                             \
            std::printf("%s:%d: CHECK_EQ failed: %s != %s\n", __FILE__, __LINE__, #a, #b); \
            ++failures;                                                \
        }                                                              \
    } while (0)

using namespace ks;

static void testParseTime() {
    CHECK_EQ(ParseTime("1970-01-01T00:00:00Z"), int64_t(0));
    CHECK_EQ(ParseTime("2026-10-10T18:07:31.499738752Z"), int64_t(1791655651));
    CHECK_EQ(ParseTime("2026-10-10T19:07:31+01:00"), int64_t(1791655651));
    CHECK_EQ(ParseTime("2026-10-10T13:07:31.5-05:00"), int64_t(1791655651));
    CHECK_EQ(ParseTime("2000-02-29T00:00:00Z"), int64_t(951782400));
    CHECK_EQ(ParseTime("0001-01-01T00:00:00Z"), int64_t(-62135596800));  // Go's zero time
    CHECK_EQ(ParseTime(""), int64_t(0));
    CHECK_EQ(ParseTime("2026-10-10 18:07:31Z"), int64_t(0));
    CHECK_EQ(ParseTime("2026-10-10T18:07:31"), int64_t(0));  // no zone
}

static void testAgo() {
    CHECK_EQ(Ago(1000, 1030), std::string("just now"));
    CHECK_EQ(Ago(1000, 1000 + 4 * 60 + 59), std::string("4m ago"));
    CHECK_EQ(Ago(1000, 1000 + 2 * 3600), std::string("2h ago"));
    CHECK_EQ(Ago(1000, 1000 + 3 * 86400 + 5), std::string("3d ago"));
    CHECK_EQ(Ago(0, 1000), std::string("never"));
}

static void testParseSnapshot() {
    auto j = nlohmann::json::parse(R"({
        "monitor": "weather", "kind": "weather", "title": "Weather",
        "updated": "2026-10-10T18:07:31Z", "error": "site down", "subtitle": "Mazda3 · Automatic", "timely": true, "background": true,
        "relevant": {"from": "2026-10-10T18:00:00Z", "until": "2026-10-10T19:00:00Z"},
        "blocks": [
            {"type": "stats", "title": "Bristol", "stats": [
                {"label": "Now", "value": "13°C", "detail": "Clear", "tone": "accent", "series": [0, 0.4, 1.5]}
            ]},
            {"type": "table", "columns": [{"title": "Day"}, {"title": "High", "align": "right", "fill": true}],
             "rows": [[{"text": "Today", "url": "https://example.com", "detail": "dry"}, {"text": "15°", "tone": "bad"}]]},
            {"type": "list", "items": [{"title": "Car", "image": "https://example.com/a.jpg", "tone": "warn"}]},
            {"type": "text", "text": "hello", "unknown": {"nested": true}},
            {"type": "checklist", "addHint": "Add a task…", "deleteHint": "Put off a day", "noDelete": true, "movable": true, "switch": true, "downHint": "Later", "items": [{"id": "a1", "title": "Bins", "done": true}, {"id": "b2", "title": "Post", "slider": {"value": 140, "action": "brightness", "hint": "Brightness"}, "colour": {"value": "#ff8000", "action": "colour", "wheel": true, "presets": [{"label": "Warm white", "value": "2700K", "show": "#ffa757"}]}}]}
        ],
        "summary": [{"label": "Now", "value": "13°C", "tone": "good", "series": [1, 2]}, {"label": "Rain"}],
        "summaryStyle": "chips",
        "summaryBlock": {"type": "table", "columns": [{"title": ""}, {"title": "Today", "align": "right"}],
                         "rows": [[{"text": "Vorkath"}, {"text": "+12", "tone": "good"}]]}
    })");
    Snapshot s = ParseSnapshot(j);
    CHECK_EQ(s.summaryBlock.size(), size_t(1));
    CHECK_EQ(s.summaryBlock[0].rows[0][1].text, std::string("+12"));
    CHECK_EQ(s.monitor, std::string("weather"));
    CHECK_EQ(s.error, std::string("site down"));
    CHECK_EQ(s.subtitle, std::string("Mazda3 · Automatic"));
    CHECK(s.timely);
    CHECK(s.background);
    CHECK_EQ(s.updated, int64_t(1791655651));
    CHECK(!Relevant(s, 1791655200 - 1));
    CHECK(Relevant(s, 1791655200));
    CHECK(Relevant(s, 1791658799));
    CHECK(!Relevant(s, 1791658800));
    CHECK_EQ(s.blocks.size(), size_t(5));
    CHECK_EQ(s.blocks[4].addHint, std::string("Add a task…"));
    CHECK_EQ(s.blocks[4].deleteHint, std::string("Put off a day"));
    CHECK(s.blocks[4].noDelete && s.blocks[4].movable && s.blocks[4].switches);
    CHECK(!s.blocks[4].items[0].slider.present && !s.blocks[4].items[0].colour.present);
    CHECK(s.blocks[4].items[1].slider.present && s.blocks[4].items[1].slider.value == 100);
    CHECK(s.blocks[4].items[1].slider.action == "brightness" && s.blocks[4].items[1].slider.hint == "Brightness");
    CHECK(s.blocks[4].items[1].colour.present && s.blocks[4].items[1].colour.wheel);
    CHECK(s.blocks[4].items[1].colour.value == "#ff8000" && s.blocks[4].items[1].colour.action == "colour");
    CHECK(s.blocks[4].items[1].colour.presets.size() == 1 && s.blocks[4].items[1].colour.presets[0].value == "2700K");
    {
        float rgb[3];
        CHECK(ParseHexColour("#ff8000", rgb) && rgb[0] == 1.0f && rgb[2] == 0.0f);
        CHECK(HexColour(rgb) == "#ff8000");
        CHECK(!ParseHexColour("ff8000", rgb) && !ParseHexColour("#ff80zz", rgb) && !ParseHexColour("", rgb));
    }
    CHECK_EQ(s.blocks[4].downHint, std::string("Later"));
    CHECK(s.blocks[4].upHint.empty());
    CHECK_EQ(s.blocks[4].items[0].id, std::string("a1"));
    CHECK(s.blocks[4].items[0].done);
    CHECK(!s.blocks[4].items[1].done);
    CHECK_EQ(s.blocks[0].stats.size(), size_t(1));
    CHECK_EQ(s.blocks[0].stats[0].value, std::string("13°C"));
    CHECK(s.blocks[0].stats[0].tone == Tone::Accent);
    CHECK_EQ(s.blocks[0].stats[0].series.size(), size_t(3));
    CHECK(s.blocks[1].columns[1].align == Align::Right);
    CHECK(!s.blocks[1].columns[0].fill);
    CHECK(s.blocks[1].columns[1].fill);
    CHECK_EQ(s.summary.size(), size_t(2));
    CHECK_EQ(s.summaryStyle, std::string("chips"));
    CHECK_EQ(s.summary[0].value, std::string("13°C"));
    CHECK(s.summary[0].tone == Tone::Good);
    CHECK_EQ(s.summary[0].series.size(), size_t(2));
    CHECK_EQ(s.blocks[1].rows[0][0].url, std::string("https://example.com"));
    CHECK_EQ(s.blocks[1].rows[0][0].detail, std::string("dry"));
    CHECK(s.blocks[1].rows[0][1].detail.empty());
    CHECK(s.blocks[1].rows[0][1].tone == Tone::Bad);
    CHECK(s.blocks[2].items[0].tone == Tone::Warn);
    CHECK_EQ(s.blocks[3].text, std::string("hello"));

    // Wrong types and missing fields don't throw.
    Snapshot odd = ParseSnapshot(nlohmann::json::parse(R"({"monitor": 5, "blocks": {"x": 1}})"));
    CHECK(!odd.timely);
    CHECK(!odd.background);
    CHECK(!Relevant(odd, 1791655651));
    Snapshot open = ParseSnapshot(nlohmann::json::parse(R"({"relevant": {"until": "2026-10-10T19:00:00Z"}})"));
    CHECK(Relevant(open, 1791655651));
    CHECK(odd.monitor.empty());
    CHECK(odd.blocks.empty());
    CHECK(odd.summaryBlock.empty());
    CHECK(ParseSnapshot(nlohmann::json::parse(R"({"summaryBlock": null})")).summaryBlock.empty());
    CHECK_EQ(ParseSnapshot(nlohmann::json::parse(R"({"updated": "0001-01-01T00:00:00Z"})")).updated, int64_t(0));
}

static void testUseCompact() {
    CHECK(UseCompact(false, 1242, 906, 1));   // the usual window
    CHECK(!UseCompact(false, 5120, 1400, 1));  // maximised
    CHECK(UseCompact(false, 800, 1400, 1));   // narrow
    CHECK(UseCompact(false, 2484, 1812, 2));   // the usual window, at 200%
    // Near the boundary, the current choice sticks.
    CHECK(UseCompact(true, 1600, 1120, 1));
    CHECK(!UseCompact(false, 1600, 1080, 1));
    CHECK(!UseCompact(true, 1600, 1150, 1));
}

static void testFitPanels() {
    // Rows of 50, gaps of 10: 300 high holds all four rows (240) with 60 over.
    std::vector<Panel> p = {{50, 150, 6}, {50, 600, 6}, {50, 80, 6}, {50, 100, 6}};
    auto shows = [](const Fit& f) {
        std::string s;
        for (Show v : f.show) s += v == Show::Full ? 'F' : v == Show::Clipped ? 'C' : 'r';
        return s;
    };
    // Plenty of room: everything opens.
    CHECK_EQ(shows(FitPanels(p, 2000, 10, 40)), std::string("FFFF"));
    // The first opens (+100), the second wouldn't fit, the third and
    // fourth open (+30, +50); the second takes what's left:
    // 500 - 420 - 6 = 74.
    Fit f = FitPanels(p, 500, 10, 40);
    CHECK_EQ(shows(f), std::string("FCFF"));
    CHECK_EQ(f.body, 74.0f);
    // At 400 the fourth no longer fits, and 24 is too little left over for
    // a useful scrolling panel.
    CHECK_EQ(shows(FitPanels(p, 400, 10, 40)), std::string("FrFr"));
    // Pins win: a closed one stays closed and an open one opens first,
    // even when it doesn't fit.
    p[0].pin = Pin::Closed;
    p[1].pin = Pin::Open;
    CHECK_EQ(shows(FitPanels(p, 500, 10, 40)), std::string("rFrr"));
    // Unmeasured panels stay as rows.
    CHECK_EQ(shows(FitPanels({{50, 0, 6}, {50, 100, 6}}, 2000, 10, 40)), std::string("rF"));
}

static void testGpuUtilisation() {
    // Two processes on the 3D engine add up; the video engine is separate;
    // the busiest engine wins. Junk names are ignored.
    std::vector<std::pair<std::string, double>> in = {
        {"pid_100_luid_0x00000000_0x0000D1A5_phys_0_eng_0_engtype_3D", 30},
        {"pid_200_luid_0x00000000_0x0000D1A5_phys_0_eng_0_engtype_3D", 15},
        {"pid_300_luid_0x00000000_0x0000D1A5_phys_0_eng_3_engtype_VideoDecode", 40},
        {"something else", 99},
    };
    CHECK_EQ(GpuUtilisation(in), 45.0f);
    in.push_back({"pid_400_luid_0x00000000_0x0000D1A5_phys_0_eng_0_engtype_3D", 90});
    CHECK_EQ(GpuUtilisation(in), 100.0f);  // capped
    CHECK_EQ(GpuUtilisation({}), 0.0f);
}

static void testLocalPanels() {
    constexpr uint64_t GB = 1024ull * 1024 * 1024;
    CHECK_EQ(FormatBytes(512ull * 1024 * 1024), std::string("512 MB"));
    CHECK_EQ(FormatBytes(14 * GB + GB / 5), std::string("14.2 GB"));
    CHECK_EQ(FormatBytes(2048 * GB), std::string("2.0 TB"));
    CHECK(LoadTone(84) == Tone::None);
    CHECK(LoadTone(85) == Tone::Warn);
    CHECK(LoadTone(95) == Tone::Bad);

    PcHistory h;
    for (int i = 0; i < 70; i++) h.Add({float(i), 50, 8 * GB, 32 * GB, 0, 0, "RTX"});
    Snapshot pc = h.Build(1000);
    CHECK(IsLocal(pc));
    CHECK_EQ(pc.title, std::string("This PC"));
    CHECK_EQ(pc.blocks.size(), size_t(1));
    const auto& st = pc.blocks[0].stats;
    CHECK_EQ(st.size(), size_t(4));
    CHECK_EQ(st[0].value, std::string("69%"));
    CHECK_EQ(st[0].series.size(), PcHistory::kLength);  // a minute kept
    CHECK_EQ(st[0].top, 100.0f);                        // percentages on a fixed scale
    CHECK_EQ(st[1].value, std::string("25%"));
    CHECK_EQ(st[1].detail, std::string("8.0 GB / 32.0 GB"));
    // Room for the most each can show, so the tiles don't shift.
    CHECK_EQ(st[0].widestValue, std::string("100%"));
    CHECK_EQ(st[1].widestDetail, std::string("32.0 GB / 32.0 GB"));
    CHECK(st[2].widestDetail.empty());
    CHECK_EQ(st[2].detail, std::string("RTX"));
    CHECK_EQ(st[3].value, std::string("–"));  // no VRAM reading
    CHECK(st[3].tone == Tone::Muted);
    CHECK_EQ(pc.summary.size(), size_t(4));
    CHECK(pc.summary[2].detail.empty());  // the GPU's name only in the full panel

    Snapshot d = DrivesSnapshot({{"C:\\", "", 1000 * GB, 40 * GB}, {"D:\\", "Games", 2000 * GB, 1000 * GB}, {"E:\\", "", 0, 0}}, 1000);
    CHECK_EQ(d.blocks[0].rows.size(), size_t(2));  // the empty drive is skipped
    CHECK_EQ(d.blocks[0].rows[0][0].text, std::string("C:"));
    CHECK_EQ(d.blocks[0].rows[0][1].text, std::string("Local Disk"));
    CHECK_EQ(d.blocks[0].rows[0][2].text, std::string("96%"));
    CHECK(d.blocks[0].rows[0][2].tone == Tone::Bad);
    CHECK_EQ(d.blocks[0].rows[1][2].text, std::string("50%"));
    CHECK(d.blocks[0].columns[1].fill);
    CHECK_EQ(d.summary.size(), size_t(2));
    CHECK_EQ(d.summary[1].detail, std::string("1000.0 GB free"));
    // The drives join This PC: their table under its stats, their tiles
    // after its own.
    Snapshot all = WithDrives(pc, d);
    CHECK_EQ(all.monitor, std::string("local:pc"));
    CHECK_EQ(all.blocks.size(), pc.blocks.size() + 1);
    CHECK_EQ(all.blocks.back().type, std::string("table"));
    CHECK_EQ(all.blocks.back().title, std::string("Drives"));
    CHECK_EQ(all.summary.size(), pc.summary.size() + 2);
    CHECK_EQ(all.summary.back().label, std::string("D:"));
    CHECK_EQ(d.summary[1].widestValue, std::string("100%"));
    // Drawn as bars of how full each is, the first starting their group.
    CHECK(d.summary[0].bar && d.summary[0].hasLevel);
    CHECK(std::fabs(d.summary[0].level - 0.96f) < 0.001f);
    CHECK(d.summary[0].group && !d.summary[1].group);
    CHECK_EQ(DrivesSnapshot({}, 0).blocks[0].type, std::string("text"));
}

static void testMedia() {
    CHECK_EQ(AppName("Spotify.exe"), std::string("Spotify"));
    CHECK_EQ(AppName("MSEdge"), std::string("Edge"));
    CHECK_EQ(AppName("308046B0AF4A39CB"), std::string("Firefox"));
    CHECK_EQ(AppName("Microsoft.ZuneMusic_8wekyb3d8bbwe!Microsoft.ZuneMusic"), std::string("Media Player"));
    CHECK_EQ(AppName("Tidal.exe"), std::string("Tidal"));
    CHECK_EQ(AppName("Some.App_abc!App"), std::string("Some.App_abc"));
    CHECK_EQ(StripEmoji("Big win \xF0\x9F\x98\x82\xF0\x9F\x94\xA5 tonight \xE2\x9D\xA4\xEF\xB8\x8F"),
             std::string("Big win tonight"));
    CHECK_EQ(StripEmoji("Café – Σίγουρα, Привет"), std::string("Café – Σίγουρα, Привет"));  // kept
    CHECK_EQ(FormatTime(185), std::string("3:05"));
    CHECK_EQ(FormatTime(3723), std::string("1:02:03"));
    CHECK_EQ(FormatTime(-4), std::string("0:00"));

    CHECK(MediaSnapshot({}, 0).blocks.empty());  // nothing playing: no panel
    NowPlaying np;
    np.active = true;
    np.title = "Song";
    np.artist = "Band";
    np.album = "Record";
    np.app = "Spotify";
    np.image = "media:1";
    np.media.playing = true;
    np.media.duration = 200;
    Snapshot s = MediaSnapshot(np, 1000);
    CHECK(IsLocal(s));
    CHECK_EQ(s.blocks[0].type, std::string("media"));
    CHECK_EQ(s.blocks[0].items[0].subtitle, std::string("Band"));
    CHECK_EQ(s.blocks[0].items[0].detail, std::string("Record · Spotify"));
    CHECK_EQ(s.blocks[0].items[0].image, std::string("media:1"));
    CHECK_EQ(s.blocks[0].media.duration, 200.0);
    CHECK_EQ(s.summary[0].label, std::string("Now playing"));
    CHECK_EQ(s.summary[0].detail, std::string("Band · Spotify"));
    np.media.playing = false;
    CHECK_EQ(MediaSnapshot(np, 1000).summary[0].label, std::string("Paused"));
}

static void testArrange() {
    using V = std::vector<std::string>;
    V defaults = {"pc", "calendar", "todo", "stocks", "drives"};
    CHECK(Arrange(defaults, {}) == defaults);
    CHECK(Arrange(defaults, {"stocks", "pc", "drives", "calendar", "todo"}) == V({"stocks", "pc", "drives", "calendar", "todo"}));
    // A new panel (media, after pc by default) slots in after pc; a removed
    // one (gone) is dropped.
    defaults = {"pc", "media", "calendar", "todo", "stocks", "drives"};
    CHECK(Arrange(defaults, {"stocks", "pc", "gone", "drives", "calendar", "todo"}) ==
          V({"stocks", "pc", "media", "drives", "calendar", "todo"}));
    // A new first panel goes first.
    CHECK(Arrange({"new", "a", "b"}, {"b", "a"}) == V({"new", "b", "a"}));

    V order = {"a", "b", "c", "d"};
    CHECK(Move(order, "d", "b", false) == V({"a", "d", "b", "c"}));
    CHECK(Move(order, "a", "c", true) == V({"b", "c", "a", "d"}));
    CHECK(Move(order, "b", "b", true) == order);
    CHECK(Move(order, "x", "b", true) == order);
}

static void testOled() {
    // Eight points round a circle, three minutes apart, then round again.
    std::set<std::pair<float, float>> points;
    for (int64_t t = 0; t < 8 * 180; t += 180) points.insert(OledShift(t, 6));
    CHECK_EQ(points.size(), size_t(8));
    CHECK(OledShift(0, 6) == std::make_pair(6.0f, 0.0f));
    CHECK(OledShift(179, 6) == OledShift(0, 6));
    CHECK(OledShift(2 * 180, 6) == std::make_pair(0.0f, 6.0f));
    CHECK(OledShift(8 * 180, 6) == OledShift(0, 6));
    for (const auto& [x, y] : points) CHECK(x * x + y * y <= 37);

    CHECK(!OledSwapSides(0));
    CHECK(OledSwapSides(3600));
    CHECK(!OledSwapSides(7200 + 59));

    CHECK(!OledDim(1000, 1000, 0));
    CHECK(!OledDim(1000 + 599, 1000, 0));
    CHECK(OledDim(1000 + 600, 1000, 0));
    CHECK(!OledDim(1000 + 600, 1000, 1000 + 700));  // woken by something new
}

static void testParseEvent() {
    Event e = ParseEvent(nlohmann::json::parse(
        R"({"id": 42, "monitor": "nfl", "key": "final:1", "time": "2026-10-10T18:07:31Z",
            "title": "Final: TB 24 – 16 DAL", "priority": "high", "quiet": true})"));
    CHECK_EQ(e.id, int64_t(42));
    CHECK_EQ(e.title, std::string("Final: TB 24 – 16 DAL"));
    CHECK(e.priority == Priority::High);
    CHECK(e.quiet);
    CHECK(ParseEvent(nlohmann::json::object()).priority == Priority::Normal);
}

static Snapshot snap(const std::string& id) {
    Snapshot s;
    s.monitor = id;
    s.title = "T" + id;
    return s;
}

static void testModel() {
    Model m;
    m.BeginSync();
    m.ApplySnapshot(snap("a"));
    m.ApplySnapshot(snap("b"));
    m.EndSync();
    m.ApplySnapshot(snap("c"));  // a live update for a new monitor
    CHECK_EQ(m.Snapshots().size(), size_t(3));

    // Reconnect: the server now has b and a, in that order, and no c.
    m.BeginSync();
    m.ApplySnapshot(snap("b"));
    m.ApplySnapshot(snap("a"));
    m.EndSync();
    CHECK_EQ(m.Snapshots().size(), size_t(2));
    CHECK_EQ(m.Snapshots()[0].monitor, std::string("b"));
    CHECK_EQ(m.Snapshots()[1].monitor, std::string("a"));
    CHECK_EQ(m.TitleOf("a"), std::string("Ta"));
    CHECK_EQ(m.TitleOf("zzz"), std::string("zzz"));

    Event e;
    e.id = 2;
    CHECK(m.AddEvent(e));
    CHECK(!m.AddEvent(e));
    // An updated copy replaces it, without counting as new.
    e.title = "AMD down 0.71% today", e.quiet = true;
    CHECK(!m.AddEvent(e));
    CHECK_EQ(m.Events().front().title, std::string("AMD down 0.71% today"));
    CHECK(m.Events().front().quiet);
    CHECK_EQ(m.Events().size(), size_t(1));
    e.title.clear(), e.quiet = false;
    e.id = 5;
    CHECK(m.AddEvent(e));
    e.id = 3;
    CHECK(m.AddEvent(e));
    CHECK_EQ(m.Events().front().id, int64_t(5));
    CHECK_EQ(m.Events()[1].id, int64_t(3));
    CHECK_EQ(m.LastEventID(), int64_t(5));

    for (int i = 10; i < 10 + int(Model::kMaxEvents) + 5; i++) {
        e.id = i;
        m.AddEvent(e);
    }
    CHECK_EQ(m.Events().size(), Model::kMaxEvents);

    // Clearing: one, then all; neither comes back when history is replayed,
    // but newer events still arrive.
    Model c;
    for (int i = 1; i <= 4; i++) {
        e.id = i;
        c.AddEvent(e);
    }
    c.Clear(3);
    CHECK_EQ(c.Events().size(), size_t(3));
    e.id = 3;
    CHECK(!c.AddEvent(e));
    CHECK(c.GetCleared().ids.count(3));
    c.ClearAll();
    CHECK(c.Events().empty());
    CHECK_EQ(c.GetCleared().through, int64_t(4));
    CHECK(c.GetCleared().ids.empty());
    e.id = 2;
    CHECK(!c.AddEvent(e));
    e.id = 5;
    CHECK(c.AddEvent(e));
    CHECK_EQ(c.LastEventID(), int64_t(5));

    // After a restart: what was cleared before is dropped, the rest kept.
    Model r;
    for (int i = 1; i <= 6; i++) {
        e.id = i;
        r.AddEvent(e);
    }
    Model::Cleared saved;
    saved.through = 2;
    saved.ids = {1, 5};
    r.SetCleared(saved);
    CHECK_EQ(r.Events().size(), size_t(3));
    CHECK_EQ(r.Events().front().id, int64_t(6));
    CHECK_EQ(r.Events().back().id, int64_t(3));
    CHECK_EQ(r.GetCleared().ids.size(), size_t(1));
    for (int i = 100; i < 100 + int(Model::kMaxCleared) + 10; i++) r.Clear(i);
    CHECK_EQ(r.GetCleared().ids.size(), Model::kMaxCleared);
    CHECK(!r.GetCleared().ids.count(5));
}

static void testSse() {
    SseParser p;
    std::vector<SseMessage> out;
    // Split mid-line and mid-CRLF, with a heartbeat comment and a
    // multi-line data field.
    p.Feed("id: 7\r\nevent: event\r", out);
    p.Feed("\ndata: {\"a\":1}\r\n\r\n: ping\n\n", out);
    p.Feed("event: snapshot\ndata: x\ndata:y\n", out);
    CHECK_EQ(out.size(), size_t(1));
    p.Feed("\n", out);
    p.Feed("data: no event name\n\n", out);
    p.Feed("event: empty\n\n", out);  // no data: nothing dispatched
    CHECK_EQ(out.size(), size_t(3));
    CHECK_EQ(out[0].id, std::string("7"));
    CHECK_EQ(out[0].event, std::string("event"));
    CHECK_EQ(out[0].data, std::string("{\"a\":1}"));
    CHECK_EQ(out[1].event, std::string("snapshot"));
    CHECK_EQ(out[1].data, std::string("x\ny"));
    CHECK_EQ(out[1].id, std::string("7"));  // the ID carries over
    CHECK_EQ(out[2].event, std::string("message"));
}

static void testPrivate() {
    Snapshot s = ParseSnapshot(nlohmann::json::parse(R"({
        "monitor": "stocks",
        "summary": [{"label": "Today", "value": "+£10", "private": true, "lead": true}, {"label": "AMD", "value": "+1%"}],
        "blocks": [
            {"type": "stats", "private": true, "stats": [{"label": "Portfolio", "value": "£1,000"}]},
            {"type": "stats", "stats": [{"label": "Mine", "value": "£5", "private": true}, {"label": "Public", "value": "1"}]},
            {"type": "stats", "stats": [{"label": "Only mine", "value": "£5", "private": true}]},
            {"type": "table", "private": true, "columns": [{"title": "Day"}]},
            {"type": "table", "columns": [{"title": "Symbol"}]}
        ]})"));
    CHECK(HasPrivate(s));
    CHECK(s.blocks[0].isPrivate);
    CHECK(s.summary[0].isPrivate);
    CHECK(s.summary[0].lead);
    CHECK(!s.summary[1].lead);

    Snapshot hidden = WithoutPrivate(s);
    CHECK(!HasPrivate(hidden));
    CHECK_EQ(hidden.summary.size(), size_t(1));
    CHECK_EQ(hidden.summary[0].label, std::string("AMD"));
    CHECK_EQ(hidden.blocks.size(), size_t(2));
    CHECK_EQ(hidden.blocks[0].stats.size(), size_t(1));
    CHECK_EQ(hidden.blocks[0].stats[0].label, std::string("Public"));
    CHECK_EQ(hidden.blocks[1].columns[0].title, std::string("Symbol"));

    CHECK(!HasPrivate(ParseSnapshot(nlohmann::json::parse(R"({"blocks": [{"type": "text", "text": "hi"}]})"))));
}

// Recorder is a canvas that keeps the bounds of what's drawn.
struct Recorder : Canvas {
    float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
    int shapes = 0;
    void see(Vec2f p, float rx, float ry) {
        minX = std::min(minX, p.x - rx), maxX = std::max(maxX, p.x + rx);
        minY = std::min(minY, p.y - ry), maxY = std::max(maxY, p.y + ry);
        shapes++;
    }
    void see(Vec2f p, float r) { see(p, r, r); }
    void Circle(Vec2f c, float r, Rgba) override { see(c, r); }
    // Rotated ellipses are small, nearly round or barely turned: their
    // radii do.
    void Ellipse(Vec2f c, float rx, float ry, float, Rgba) override { see(c, rx, ry); }
    void Line(Vec2f a, Vec2f b, float w, Rgba) override { see(a, w / 2), see(b, w / 2); }
    void Polygon(const Vec2f* p, int n, Rgba) override {
        for (int i = 0; i < n; i++) see(p[i], 0);
    }
};

static void testWeatherIcons() {
    // Each stays inside its square and draws something; unknown names
    // draw nothing.
    IconColours col;
    for (const char* n : {"sun", "moon", "sun-cloud", "moon-cloud", "cloud", "fog", "drizzle", "rain", "showers", "snow", "storm"}) {
        CHECK(IsWeatherIcon(n));
        Recorder r;
        DrawWeatherIcon(r, n, {10, 20}, 50, col);
        CHECK(r.shapes > 0);
        CHECK(r.minX >= 10 && r.maxX <= 60 && r.minY >= 20 && r.maxY <= 70);
    }
    CHECK(!IsWeatherIcon("tornado"));
    Recorder none;
    DrawWeatherIcon(none, "tornado", {0, 0}, 50, col);
    CHECK_EQ(none.shapes, 0);
    Snapshot snap = ParseSnapshot(nlohmann::json::parse(
        R"({"summary": [{"label": "13:00", "value": "18°", "icon": "rain", "level": 0.6}, {"level": 3}, {"url": "https://example.com/"}, {"level": 0}]})"));
    CHECK_EQ(snap.summary[0].icon, std::string("rain"));
    CHECK_EQ(snap.summary[2].url, std::string("https://example.com/"));
    CHECK(snap.paste.empty());
    CHECK_EQ(ParseSnapshot(nlohmann::json::parse(R"({"paste": "Log it"})")).paste, std::string("Log it"));
    CHECK(snap.summary[0].url.empty());
    CHECK(std::fabs(snap.summary[0].level - 0.6f) < 1e-6f);
    CHECK_EQ(snap.summary[1].level, 1.0f);  // clamped
    CHECK_EQ(snap.summary[2].level, 0.0f);
    CHECK(!snap.summary[2].hasLevel);
    CHECK(snap.summary[3].hasLevel);  // 0% still draws
}

static void testCat() {
    // Over two whole episodes, at a narrow and a wide column, she stays on
    // the floor and inside her space: 68 units tall (4 below the floor) and
    // the column's width. She sleeps most of the time, but wakes, yawns,
    // walks and turns round.
    for (float half : {120.0f, 150.0f}) {
        bool walked = false, turned = false, yawned = false;
        int asleep = 0, frames = 0;
        float lastX = CatAt(0, half).x;
        for (double t = 0; t < 2 * kEpisode; t += 1.0 / 20, frames++) {
            Cat c = CatAt(t, half);
            for (const Vec2f& paw : c.pose.paw) CHECK(paw.y <= 0.01f);
            CHECK(std::isfinite(c.x) && std::fabs(c.facing) <= 1);
            walked = walked || std::fabs(c.x - lastX) > 0.01f;
            turned = turned || (c.turn > 0.4f && c.turn < 0.6f);
            yawned = yawned || c.pose.yawn > 0.9f;
            asleep += c.asleep >= 1;
            lastX = c.x;
            Recorder r;
            DrawCat(r, c, t, {0, 0}, 1);
            CHECK(r.shapes > 30);
            CHECK(r.maxY <= 4);
            CHECK(r.minY >= -64);
            CHECK(r.minX >= -half && r.maxX <= half);
        }
        CHECK(walked);
        CHECK(yawned);
        CHECK(asleep > frames / 2);
        (void)turned;  // depends on where she happens to go
    }
    // Over many episodes she gets up to all sorts, always on the floor and
    // inside her space, pouncing included.
    for (float half : {60.0f, 150.0f}) {
        std::set<int> did;
        bool licked = false, looked = false, leapt = false, drank = false, batted = false;
        for (double t = 0; t < 30 * kEpisode; t += 1.0 / 10) {
            Cat c = CatAt(t, half);
            did.insert(int(c.action));
            licked = licked || c.pose.tongue > 0.9f;
            looked = looked || c.look > 0.99f;
            leapt = leapt || c.lift > 9;
            drank = drank || (c.action == CatAction::Drink && c.bowl.alpha >= 1 && c.bowl.fill < 0.5f);
            batted = batted || (c.action == CatAction::Hunt && c.toy.lift > 2);
            // Props only while she's busy with them.
            if (c.action != CatAction::Drink) CHECK(c.bowl.alpha == 0);
            if (c.action != CatAction::Hunt) CHECK(c.toy.alpha == 0);
            CHECK(c.lift >= 0 && c.look >= 0 && c.look <= 1);
            for (const Vec2f& paw : c.pose.paw) CHECK(paw.y <= 0.01f);
            Recorder r;
            DrawCat(r, c, t, {0, 0}, 1);
            CHECK(r.maxY <= 4);
            CHECK(r.minY >= -64);
            CHECK(r.minX >= -half && r.maxX <= half);
        }
        for (CatAction a : {CatAction::Stir, CatAction::Groom, CatAction::Scratch, CatAction::Blink, CatAction::Knead})
            CHECK(did.count(int(a)));
        CHECK(licked && looked);
        // A narrow column has no room to land a pounce, or chase a toy.
        CHECK(leapt == (half > 100));
        CHECK(did.count(int(CatAction::Pounce)) == (half > 100));
        CHECK(did.count(int(CatAction::Hunt)) == (half > 100));
        CHECK(batted == (half > 100));
        CHECK(did.count(int(CatAction::Drink)) && drank);
    }
    // She never jumps: from one frame to the next she moves a little, and
    // only turns round by turning, at the end of which she faces the other
    // way. Over many episodes, including ones she doesn't walk in.
    for (float half : {60.0f, 150.0f}) {
        for (double t = 0; t < 40 * kEpisode; t += 0.05) {
            Cat a = CatAt(t, half), b = CatAt(t + 0.05, half);
            CHECK(std::fabs(a.x - b.x) < 3);
            CHECK(std::fabs(a.pose.head.y - b.pose.head.y) < 3);
            CHECK(std::fabs(a.lift - b.lift) < 3);
            CHECK(std::fabs(a.look - b.look) < 0.2f);
            if (a.toy.alpha > 0 && b.toy.alpha > 0) CHECK(std::fabs(a.toy.x - b.toy.x) < 12);
            if (a.bowl.alpha > 0 && b.bowl.alpha > 0) CHECK(std::fabs(a.bowl.x - b.bowl.x) < 12);
            if (a.facing != b.facing) CHECK(a.turn > 0.9f);
        }
    }
    // Asked out of order, e.g. after a jump in time, she's the same.
    Cat later = CatAt(37 * kEpisode + 3, 150);
    CatAt(2 * kEpisode, 150);
    Cat again = CatAt(37 * kEpisode + 3, 150);
    CHECK(later.x == again.x && later.facing == again.facing);
}

static ImDrawVert vert(float x, float y, float u, float v, ImU32 col = IM_COL32(255, 255, 255, 255)) {
    ImDrawVert d;
    d.pos = ImVec2(x, y);
    d.uv = ImVec2(u, v);
    d.col = col;
    return d;
}

static void testRetro() {
    const ImTextureID font = 1, image = 2;
    const ImVec2 white(0.5f, 0.5f);
    ImDrawList dl(nullptr);
    // A shape (white pixel), then a glyph's quad, then an image.
    dl.VtxBuffer.push_back(vert(0, 0, 0.5f, 0.5f));
    dl.VtxBuffer.push_back(vert(10, 0, 0.5f, 0.5f));
    dl.VtxBuffer.push_back(vert(0, 10, 0.5f, 0.5f));
    dl.VtxBuffer.push_back(vert(20, 20, 0.1f, 0.1f, IM_COL32(255, 255, 255, 128)));
    dl.VtxBuffer.push_back(vert(30, 20, 0.2f, 0.1f, IM_COL32(255, 255, 255, 128)));
    dl.VtxBuffer.push_back(vert(30, 30, 0.2f, 0.2f, IM_COL32(255, 255, 255, 128)));
    dl.VtxBuffer.push_back(vert(20, 30, 0.1f, 0.2f, IM_COL32(255, 255, 255, 128)));
    for (ImDrawIdx i : {0, 1, 2, 3, 4, 5, 3, 5, 6}) dl.IdxBuffer.push_back(i);
    ImDrawCmd text;
    text.TextureId = font;
    text.ElemCount = 9;
    dl.CmdBuffer.push_back(text);
    for (int i = 0; i < 3; i++) dl.VtxBuffer.push_back(vert(float(i), 0, 0.3f, 0.3f));
    for (ImDrawIdx i : {0, 1, 2}) dl.IdxBuffer.push_back(i);
    ImDrawCmd img;
    img.TextureId = image;
    img.VtxOffset = 7;
    img.IdxOffset = 9;
    img.ElemCount = 3;
    dl.CmdBuffer.push_back(img);

    ImDrawData data;
    data.CmdLists.push_back(&dl);
    AddTextShadows(&data, font, white, ImVec2(2, 2), IM_COL32(0, 0, 0, 200));
    CHECK_EQ(dl.CmdBuffer.Size, 2);
    // The shape, the glyph's two shadow triangles, then the glyph itself.
    CHECK_EQ(dl.CmdBuffer[0].ElemCount, 15u);
    auto at = [&](const ImDrawCmd& c, unsigned i) { return dl.VtxBuffer[c.VtxOffset + dl.IdxBuffer[c.IdxOffset + i]]; };
    CHECK_EQ(at(dl.CmdBuffer[0], 0).pos.x, 0.0f);
    ImDrawVert shadow = at(dl.CmdBuffer[0], 3);
    CHECK_EQ(shadow.pos.x, 22.0f);
    CHECK_EQ(shadow.pos.y, 22.0f);
    CHECK_EQ(shadow.col, IM_COL32(0, 0, 0, 200 * 128 / 255));
    CHECK_EQ(at(dl.CmdBuffer[0], 9).pos.x, 20.0f);  // the glyph, unmoved
    CHECK_EQ(at(dl.CmdBuffer[0], 9).col, IM_COL32(255, 255, 255, 128));
    // The image is left alone.
    CHECK_EQ(dl.CmdBuffer[1].ElemCount, 3u);
    CHECK(dl.CmdBuffer[1].TextureId == image);
    CHECK_EQ(at(dl.CmdBuffer[1], 2).pos.x, 2.0f);
    CHECK_EQ(data.TotalVtxCount, dl.VtxBuffer.Size);
    CHECK_EQ(data.TotalIdxCount, dl.IdxBuffer.Size);

    // Lots of text splits into commands that each stay in 16-bit range.
    ImDrawList big(nullptr);
    const unsigned glyphs = 15000;
    for (unsigned g = 0; g < glyphs; g++) {
        for (int k = 0; k < 4; k++) big.VtxBuffer.push_back(vert(float(k), float(g), 0.1f, 0.1f));
    }
    ImDrawCmd all;
    all.TextureId = font;
    unsigned base = 0;
    for (unsigned g = 0; g < glyphs; g++, base += 4) {
        // Each command can only reach 65535 vertices from its offset, so
        // the input itself comes in commands.
        if (base + 4 > 65535) {
            big.CmdBuffer.push_back(all);
            all.VtxOffset += base;
            all.IdxOffset = unsigned(big.IdxBuffer.Size);
            all.ElemCount = 0;
            base = 0;
        }
        for (unsigned i : {0u, 1u, 2u, 0u, 2u, 3u}) big.IdxBuffer.push_back(ImDrawIdx(base + i));
        all.ElemCount += 6;
    }
    big.CmdBuffer.push_back(all);
    ImDrawData bigData;
    bigData.CmdLists.push_back(&big);
    AddTextShadows(&bigData, font, white, ImVec2(1, 1), IM_COL32(0, 0, 0, 255));
    unsigned elems = 0;
    bool inRange = true;
    for (const ImDrawCmd& c : big.CmdBuffer) {
        elems += c.ElemCount;
        for (unsigned i = 0; i < c.ElemCount; i++)
            if (c.VtxOffset + big.IdxBuffer[c.IdxOffset + i] >= unsigned(big.VtxBuffer.Size))
                inRange = false;
    }
    CHECK(big.CmdBuffer.Size > 2);
    CHECK_EQ(elems, glyphs * 12);
    CHECK(inRange);

    std::tm t{};
    t.tm_hour = 16, t.tm_min = 52, t.tm_sec = 10, t.tm_wday = 1, t.tm_mday = 12, t.tm_mon = 9;
    CHECK(RetroClock(t) == std::make_pair(std::string("4:52:10 PM"), std::string("MON 12 OCT")));
    t.tm_hour = 0, t.tm_min = 5, t.tm_sec = 0;
    CHECK_EQ(RetroClock(t).first, std::string("12:05:00 AM"));
    t.tm_hour = 12;
    CHECK_EQ(RetroClock(t).first, std::string("12:05:00 PM"));
}

static void testTemporaryEvents() {
    Model m;
    Event lasting = ParseEvent(nlohmann::json::parse(R"({"id": 1, "title": "TD", "time": "2026-10-14T10:00:00Z"})"));
    Event brief = ParseEvent(nlohmann::json::parse(
        R"({"id": 2, "title": "Logged", "quiet": true, "time": "2026-10-14T10:00:00Z", "expires": "2026-10-14T10:02:00Z"})"));
    CHECK_EQ(brief.expires - brief.time, int64_t(120));
    CHECK(m.AddEvent(lasting));
    CHECK(m.AddEvent(brief));
    CHECK(!m.AddEvent(brief));  // the same one again
    CHECK_EQ(m.Events().size(), size_t(1));  // not in Recent
    CHECK_EQ(m.Temporary().size(), size_t(1));
    CHECK_EQ(m.LastEventID(), int64_t(2));
    m.DropExpired(brief.expires - 1);
    CHECK_EQ(m.Temporary().size(), size_t(1));
    m.DropExpired(brief.expires);
    CHECK(m.Temporary().empty());
    // Cleared by hand, it doesn't come back with the history.
    Event again = brief;
    again.id = 3;
    m.AddEvent(again);
    m.Clear(3);
    CHECK(m.Temporary().empty());
    CHECK(!m.AddEvent(again));
}

static void testUpload() {
    Snapshot s = ParseSnapshot(nlohmann::json::parse(
        R"({"monitor":"farming","blocks":[],"upload":{"path":"%USERPROFILE%\\x.properties","prefixes":["a.","b."]}})"));
    CHECK_EQ(s.uploadPath, std::string("%USERPROFILE%\\x.properties"));
    CHECK_EQ(s.uploadPrefixes.size(), size_t(2));
    CHECK(ParseSnapshot(nlohmann::json::parse(R"({"monitor":"x","blocks":[]})")).uploadPath.empty());
    CHECK_EQ(KeepLines("a.1=2\r\nc.3=4\nb.5=6\n\na.7", s.uploadPrefixes), std::string("a.1=2\nb.5=6\na.7"));
    CHECK_EQ(KeepLines("x\r\ny", {}), std::string("x\ny"));
}

static void testStaleEvents() {
    Model m;
    Event kept = ParseEvent(nlohmann::json::parse(R"({"id": 1, "title": "Pet", "time": "2026-10-14T10:00:00Z"})"));
    Event rain = ParseEvent(nlohmann::json::parse(
        R"({"id": 2, "title": "Rain", "time": "2026-10-14T10:00:00Z", "until": "2026-10-14T11:00:00Z"})"));
    CHECK_EQ(rain.until - rain.time, int64_t(3600));
    CHECK(m.AddEvent(kept));
    CHECK(m.AddEvent(rain));
    CHECK_EQ(m.Events().size(), size_t(2));  // in Recent, not temporary
    CHECK(m.Temporary().empty());
    m.DropExpired(rain.until - 1);
    CHECK_EQ(m.Events().size(), size_t(2));
    m.DropExpired(rain.until);
    CHECK_EQ(m.Events().size(), size_t(1));
    CHECK_EQ(m.Events().front().title, std::string("Pet"));
}

static void testGrab() {
    // A table cell with a local app's button.
    Snapshot s = ParseSnapshot(nlohmann::json::parse(R"({"monitor":"letterboxd","blocks":[{"type":"table","columns":[{"title":"Film"},{"title":""}],
        "rows":[[{"text":"Fish Tank","url":"https://letterboxd.com/film/fish-tank/"},{"text":"","local":{"app":"watchgrab","arg":"fish-tank"}}]]}]})"));
    CHECK_EQ(s.blocks.size(), size_t(1));
    if (!s.blocks.empty()) {
        const Cell& c = s.blocks[0].rows[0][1];
        CHECK_EQ(c.localApp, std::string("watchgrab"));
        CHECK_EQ(c.localArg, std::string("fish-tank"));
        CHECK(s.blocks[0].rows[0][0].localApp.empty());
    }

    // A table cell with a button that sends the monitor an action.
    Snapshot e = ParseSnapshot(nlohmann::json::parse(R"({"monitor":"ebay","blocks":[{"type":"table","columns":[{"title":"Listing"},{"title":""}],
        "rows":[[{"text":"Wipeout"},{"text":"","action":{"type":"ignore","id":"v1|1|0","label":"Ignore Wipeout from now on"}}]]}]})"));
    if (!e.blocks.empty()) {
        const Cell& c = e.blocks[0].rows[0][1];
        CHECK_EQ(c.actionType, std::string("ignore"));
        CHECK_EQ(c.actionId, std::string("v1|1|0"));
        CHECK_EQ(c.actionLabel, std::string("Ignore Wipeout from now on"));
        CHECK(e.blocks[0].rows[0][0].actionType.empty());
    }

    auto st = ParseWatchgrabState(R"({"started":"2026-10-14T21:22:48Z","films":{
        "fish-tank":{"name":"Fish Tank","old":true,"status":"grabbed","torrent":"Fish Tank 2009 1080p BluRay-hallowed"},
        "pi":{"name":"Pi","status":"have"},
        "obscure":{"name":"Obscure","status":"none"},
        "plain":{"name":"Plain","old":true}}})");
    CHECK_EQ(st.size(), size_t(3));
    CHECK(st["fish-tank"].kind == GrabState::Grabbed);
    CHECK_EQ(st["fish-tank"].message, std::string("Added Fish Tank 2009 1080p BluRay-hallowed"));
    CHECK(st["pi"].kind == GrabState::Have);
    CHECK(st["obscure"].kind == GrabState::NoRelease);
    CHECK(!st.count("plain"));
    CHECK(ParseWatchgrabState("not json").empty());

    GrabState r = ParseGrabResult("{\"slug\":\"a\",\"status\":\"grabbed\",\"message\":\"Added X (10.9 GB, freeleech)\"}\r\n");
    CHECK(r.kind == GrabState::Grabbed);
    CHECK_EQ(r.message, std::string("Added X (10.9 GB, freeleech)"));
    r = ParseGrabResult("{\"slug\":\"a\",\"status\":\"error\",\"message\":\"404\"}\nwatchgrab: 404\n");
    CHECK(r.kind == GrabState::Failed);
    CHECK_EQ(r.message, std::string("404"));
    CHECK(ParseGrabResult("").kind == GrabState::Failed);
    CHECK(ParseGrabResult("{\"status\":\"would\"}").kind == GrabState::Failed);

    CHECK(ViewGrab({}).clickable);
    CHECK(ViewGrab({}).icon == GrabView::Download);
    CHECK(!ViewGrab({GrabState::Running, ""}).clickable);
    CHECK(!ViewGrab({GrabState::Grabbed, "Added X"}).clickable);
    CHECK(ViewGrab({GrabState::Grabbed, "Added X"}).tone == Tone::Good);
    CHECK(ViewGrab({GrabState::NoRelease, "No release"}).clickable);
    CHECK(ViewGrab({GrabState::Failed, "Not enough space"}).clickable);
    CHECK_EQ(ViewGrab({GrabState::Failed, "Not enough space"}).tooltip, std::string("Not enough space. Click to try again."));
}

static void testSky() {
    Place london{51.5, -0.12};
    auto near = [](int64_t a, int64_t b, int64_t within) { return std::llabs(a - b) <= within; };
    // 21 June 2024: sunrise 03:43 UTC, sunset 20:21 UTC.
    int64_t midsummer = 1718928000;
    Crossings sun = SunCrossings(midsummer, midsummer + 86400, london);
    CHECK(near(sun.rise, midsummer + 3 * 3600 + 43 * 60, 120));
    CHECK(near(sun.set, midsummer + 20 * 3600 + 21 * 60, 120));
    // Noon at the equinox: due south, 90 - 51.5 degrees up.
    Position noon = SunPosition(1710892800 + 12 * 3600 + 10 * 60, london);
    CHECK(std::abs(noon.altitude - 38.5) < 1);
    CHECK(std::abs(noon.azimuth - 180) < 3);
    Position morning = SunPosition(midsummer + 6 * 3600, london);
    CHECK(morning.azimuth > 60 && morning.azimuth < 100 && morning.altitude > 10);
    CHECK(SunPosition(midsummer, london).altitude < 0);
    // Above the Arctic circle at midsummer it doesn't set.
    Crossings arctic = SunCrossings(midsummer, midsummer + 86400, Place{78, 15});
    CHECK_EQ(arctic.rise, int64_t(0));
    CHECK_EQ(arctic.set, int64_t(0));

    // Full moon 23 April 2024 23:49 UTC; new moon 8 April 2024 18:21 UTC.
    Moon full = MoonPhase(1713830400 + 23 * 3600 + 49 * 60);
    CHECK(full.lit > 0.99);
    CHECK(std::abs(full.phase - 0.5) < 0.02);
    CHECK_EQ(std::string(PhaseName(full.phase)), std::string("Full moon"));
    Moon fresh = MoonPhase(1712534400 + 18 * 3600 + 21 * 60);
    CHECK(fresh.lit < 0.01);
    CHECK_EQ(std::string(PhaseName(fresh.phase)), std::string("New moon"));
    Moon waxing = MoonPhase(1712534400 + 4 * 86400);
    CHECK_EQ(std::string(PhaseName(waxing.phase)), std::string("Waxing crescent"));
    CHECK_EQ(std::string(PhaseName(MoonPhase(1713830400 + 4 * 86400).phase)), std::string("Waning gibbous"));
    // The moon rises and sets about once a day.
    Crossings moon = MoonCrossings(midsummer, midsummer + 86400, london);
    CHECK(moon.rise != 0 || moon.set != 0);
    if (moon.rise) CHECK(std::abs(MoonPosition(moon.rise, london).altitude + 0.27) < 0.05);

    auto place = ParsePlace(" 51.5, -0.12");
    CHECK(place && place->lat == 51.5 && place->lon == -0.12);
    CHECK(ParsePlace("51.5,-0.12").has_value());
    CHECK(!ParsePlace("").has_value());
    CHECK(!ParsePlace("51.5").has_value());
    CHECK(!ParsePlace("91, 0").has_value());
    CHECK(!ParsePlace("51.5, -0.12 x").has_value());
    CHECK_EQ(FormatPlace(Place{51.5, -0.12}), std::string("51.5000, -0.1200"));
}

int main() {
    testGrab();
    testSky();
    testRetro();
    testCat();
    testTemporaryEvents();
    testStaleEvents();
    testUpload();
    testWeatherIcons();
    testPrivate();
    testParseTime();
    testAgo();
    testParseSnapshot();
    testUseCompact();
    testFitPanels();
    testGpuUtilisation();
    testLocalPanels();
    testMedia();
    testArrange();
    testOled();
    testParseEvent();
    testModel();
    testSse();
    if (failures) {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
