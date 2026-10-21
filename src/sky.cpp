#include "sky.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace ks {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180;
constexpr double kObliquity = kRad * 23.4397;

// days is days since J2000 (noon, 1 January 2000, UTC).
double days(int64_t t) { return double(t) / 86400 - 0.5 + 2440588 - 2451545; }

double rightAscension(double l, double b) {
    return std::atan2(std::sin(l) * std::cos(kObliquity) - std::tan(b) * std::sin(kObliquity), std::cos(l));
}
double declination(double l, double b) {
    return std::asin(std::sin(b) * std::cos(kObliquity) + std::cos(b) * std::sin(kObliquity) * std::sin(l));
}

struct Coords {
    double ra = 0, dec = 0, dist = 0;  // dist in km, the moon's only
};

Coords sunCoords(double d) {
    double m = kRad * (357.5291 + 0.98560028 * d);  // mean anomaly
    double c = kRad * (1.9148 * std::sin(m) + 0.02 * std::sin(2 * m) + 0.0003 * std::sin(3 * m));
    double l = m + c + kRad * 102.9372 + kPi;  // ecliptic longitude
    return {rightAscension(l, 0), declination(l, 0)};
}

Coords moonCoords(double d) {
    double l0 = kRad * (218.316 + 13.176396 * d);  // mean longitude
    double m = kRad * (134.963 + 13.064993 * d);   // mean anomaly
    double f = kRad * (93.272 + 13.229350 * d);    // mean distance
    double l = l0 + kRad * 6.289 * std::sin(m);
    double b = kRad * 5.128 * std::sin(f);
    return {rightAscension(l, b), declination(l, b), 385001 - 20905 * std::cos(m)};
}

// refraction lifts a body near the horizon, h in radians.
double refraction(double h) {
    if (h < 0) h = 0;
    return 0.0002967 / std::tan(h + 0.00312536 / (h + 0.08901179));
}

Position position(const Coords& c, double d, const Place& at, bool refract) {
    double lw = kRad * -at.lon, phi = kRad * at.lat;
    double hourAngle = kRad * (280.16 + 360.9856235 * d) - lw - c.ra;
    double alt = std::asin(std::sin(phi) * std::sin(c.dec) + std::cos(phi) * std::cos(c.dec) * std::cos(hourAngle));
    if (refract) alt += refraction(alt);
    // From south, westwards; turned to from north, clockwise.
    double az = std::atan2(std::sin(hourAngle), std::cos(hourAngle) * std::sin(phi) - std::tan(c.dec) * std::cos(phi));
    double deg = std::fmod(az / kRad + 180, 360);
    return {alt / kRad, deg < 0 ? deg + 360 : deg};
}

// crossings finds when height(t) - h0 changes sign between from and to:
// sampled every ten minutes, then narrowed to the second.
Crossings crossings(int64_t from, int64_t to, double h0, const std::function<double(int64_t)>& height) {
    Crossings out;
    constexpr int64_t kStep = 600;
    double prev = height(from) - h0;
    for (int64_t a = from; a < to; a += kStep) {
        int64_t b = std::min(a + kStep, to);
        double next = height(b) - h0;
        if ((prev < 0) != (next < 0)) {
            bool rising = prev < 0;
            int64_t lo = a, hi = b;
            while (hi - lo > 1) {
                int64_t mid = lo + (hi - lo) / 2;
                ((height(mid) - h0 < 0) == rising ? lo : hi) = mid;
            }
            int64_t& slot = rising ? out.rise : out.set;
            if (slot == 0) slot = hi;
        }
        prev = next;
    }
    return out;
}

}  // namespace

std::optional<Place> ParsePlace(const std::string& s) {
    const char* p = s.c_str();
    char* end = nullptr;
    double lat = std::strtod(p, &end);
    if (end == p) return std::nullopt;
    p = end;
    while (*p == ' ') p++;
    if (*p != ',') return std::nullopt;
    p++;
    double lon = std::strtod(p, &end);
    if (end == p) return std::nullopt;
    while (*end == ' ') end++;
    if (*end != '\0' || std::abs(lat) > 90 || std::abs(lon) > 180) return std::nullopt;
    return Place{lat, lon};
}

std::string FormatPlace(const Place& p) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f, %.4f", p.lat, p.lon);
    return buf;
}

Position SunPosition(int64_t t, const Place& at) {
    double d = days(t);
    return position(sunCoords(d), d, at, false);
}

Position MoonPosition(int64_t t, const Place& at) {
    double d = days(t);
    return position(moonCoords(d), d, at, true);
}

Moon MoonPhase(int64_t t) {
    double d = days(t);
    Coords s = sunCoords(d), m = moonCoords(d);
    constexpr double sunDist = 149598000;  // km
    double phi = std::acos(std::sin(s.dec) * std::sin(m.dec) + std::cos(s.dec) * std::cos(m.dec) * std::cos(s.ra - m.ra));
    double inc = std::atan2(sunDist * std::sin(phi), m.dist - sunDist * std::cos(phi));
    double angle = std::atan2(std::cos(s.dec) * std::sin(s.ra - m.ra),
                              std::sin(s.dec) * std::cos(m.dec) - std::cos(s.dec) * std::sin(m.dec) * std::cos(s.ra - m.ra));
    return {0.5 + 0.5 * inc * (angle < 0 ? -1 : 1) / kPi, (1 + std::cos(inc)) / 2};
}

const char* PhaseName(double p) {
    if (p < 0.03 || p >= 0.97) return "New moon";
    if (p < 0.22) return "Waxing crescent";
    if (p < 0.28) return "First quarter";
    if (p < 0.47) return "Waxing gibbous";
    if (p < 0.53) return "Full moon";
    if (p < 0.72) return "Waning gibbous";
    if (p < 0.78) return "Last quarter";
    return "Waning crescent";
}

Crossings SunCrossings(int64_t from, int64_t to, const Place& at) {
    return crossings(from, to, -0.833, [&](int64_t t) { return SunPosition(t, at).altitude; });
}

Crossings MoonCrossings(int64_t from, int64_t to, const Place& at) {
    // Its centre, refracted, a little below the horizon for its radius.
    return crossings(from, to, -0.27, [&](int64_t t) { return MoonPosition(t, at).altitude; });
}

}  // namespace ks
