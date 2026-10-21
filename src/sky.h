// Where the sun and moon are, for the landscape under Recent: their
// height and bearing at a time and place, sunrise and sunset, and the
// moon's phase. The maths follows SunCalc (Vladimir Agafonkin), which is
// good to a minute or so: plenty for a picture.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace ks {

struct Place {
    double lat = 0, lon = 0;  // degrees, north and east positive
};

// ParsePlace reads "51.5, -0.12" (latitude, longitude). Empty, malformed
// or out-of-range text gives nothing.
std::optional<Place> ParsePlace(const std::string& s);
std::string FormatPlace(const Place& p);

// Position is a body's place in the sky, in degrees: altitude above the
// horizon (negative below it) and azimuth clockwise from north.
struct Position {
    double altitude = 0, azimuth = 0;
};

// t is Unix seconds.
Position SunPosition(int64_t t, const Place& at);
Position MoonPosition(int64_t t, const Place& at);

// Moon is the moon's look: phase runs 0 (new) to 0.25 (first quarter),
// 0.5 (full), 0.75 (last quarter) and back towards 1; lit is the fraction
// of its face lit, 0 to 1.
struct Moon {
    double phase = 0, lit = 0;
};
Moon MoonPhase(int64_t t);

// PhaseName is the phase in words: "New moon", "Waxing crescent", ...
const char* PhaseName(double phase);

// Crossings are the times a body rises above and sets below the horizon
// between from and to, 0 where it doesn't (e.g. a polar summer).
struct Crossings {
    int64_t rise = 0, set = 0;
};
// SunCrossings uses the sun's top edge with refraction (-0.833 degrees),
// as published sunrise and sunset times do.
Crossings SunCrossings(int64_t from, int64_t to, const Place& at);
Crossings MoonCrossings(int64_t from, int64_t to, const Place& at);

}  // namespace ks
