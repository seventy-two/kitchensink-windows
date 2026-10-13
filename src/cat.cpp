#include "cat.h"

#include <algorithm>
#include <cmath>

namespace ks {

namespace {

constexpr float kPi = 3.14159265f;

Vec2f add(Vec2f a, Vec2f b) { return {a.x + b.x, a.y + b.y}; }
Vec2f sub(Vec2f a, Vec2f b) { return {a.x - b.x, a.y - b.y}; }
Vec2f scale(Vec2f a, float s) { return {a.x * s, a.y * s}; }
Vec2f lerp(Vec2f a, Vec2f b, float k) { return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k}; }
float lerpf(float a, float b, float k) { return a + (b - a) * k; }
float smooth(float k) {
    k = std::clamp(k, 0.0f, 1.0f);
    return k * k * (3 - 2 * k);
}
Vec2f rotate(Vec2f v, float a) { return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)}; }

CatPose make(Vec2f chest, float rc, Vec2f mid, float rm, Vec2f hips, float rh, Vec2f head, float tilt,
             std::array<Vec2f, 4> joint, std::array<Vec2f, 4> paw, std::array<Vec2f, CatPose::kTailPoints> tail,
             float eyes, float yawn) {
    CatPose p;
    p.chest = chest, p.rChest = rc, p.mid = mid, p.rMid = rm, p.hips = hips, p.rHips = rh;
    p.head = head, p.rHead = 8, p.headTilt = tilt;
    p.joint = joint, p.paw = paw, p.tail = tail;
    p.eyes = eyes, p.yawn = yawn;
    return p;
}

// Her poses, facing right.
const CatPose kStand = make({13, -22}, 9.5f, {0, -23}, 9, {-13, -22}, 10, {25, -31}, 0,
                            {{{14, -16}, {11, -16}, {-13, -15}, {-16, -15}}}, {{{15, 0}, {11, 0}, {-12, 0}, {-17, 0}}},
                            {{{-22, -25}, {-27, -29}, {-30, -35}, {-31, -42}, {-30, -48}, {-27, -53}, {-23, -55}, {-20, -54}}},
                            1, 0);
// Curled up asleep: head down on her paws, tail wrapped round the front.
const CatPose kSleep = make({6, -10}, 10, {-4, -12}, 11.5f, {-14, -10}, 10.5f, {19, -7.5f}, 0.25f,
                            {{{10, -4}, {8, -4}, {-12, -4}, {-15, -4}}}, {{{15, -1}, {12, -1}, {-8, -1}, {-11, -1}}},
                            {{{-23, -9}, {-25, -4}, {-21, -1}, {-13, 0}, {-4, 0}, {4, 0}, {11, -1}, {16, -2}}}, 0, 0);
// The front stretch: chest down, paws reaching, bottom and tail up, yawning.
const CatPose kBow = make({16, -11}, 8.5f, {3, -17}, 8.5f, {-12, -25}, 9.5f, {29, -14}, -0.25f,
                          {{{17, -7}, {15, -7}, {-12, -18}, {-15, -18}}}, {{{32, 0}, {29, 0}, {-11, 0}, {-15, 0}}},
                          {{{-21, -28}, {-25, -34}, {-27, -41}, {-27, -48}, {-25, -54}, {-22, -58}, {-18, -60}, {-15, -59}}},
                          0, 1);
// The arch: back up, legs together.
const CatPose kArch = make({12, -26}, 8.5f, {0, -33}, 9, {-12, -26}, 9.5f, {22, -30}, 0.3f,
                           {{{12, -19}, {10, -19}, {-11, -19}, {-13, -19}}}, {{{8, 0}, {6, 0}, {-7, 0}, {-9, 0}}},
                           {{{-20, -29}, {-24, -35}, {-26, -42}, {-26, -48}, {-24, -54}, {-21, -58}, {-17, -60}, {-14, -59}}},
                           0.6f, 0);

CatPose blend(const CatPose& a, const CatPose& b, float k) {
    CatPose p;
    p.chest = lerp(a.chest, b.chest, k), p.mid = lerp(a.mid, b.mid, k), p.hips = lerp(a.hips, b.hips, k);
    p.rChest = lerpf(a.rChest, b.rChest, k), p.rMid = lerpf(a.rMid, b.rMid, k), p.rHips = lerpf(a.rHips, b.rHips, k);
    p.head = lerp(a.head, b.head, k), p.rHead = lerpf(a.rHead, b.rHead, k), p.headTilt = lerpf(a.headTilt, b.headTilt, k);
    for (int i = 0; i < 4; i++) p.joint[i] = lerp(a.joint[i], b.joint[i], k), p.paw[i] = lerp(a.paw[i], b.paw[i], k);
    for (int i = 0; i < CatPose::kTailPoints; i++) p.tail[i] = lerp(a.tail[i], b.tail[i], k);
    p.eyes = lerpf(a.eyes, b.eyes, k), p.yawn = lerpf(a.yawn, b.yawn, k), p.tongue = lerpf(a.tongue, b.tongue, k);
    return p;
}

uint32_t hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// spot is where she settles at the end of episode k, from -1 to 1 across
// the floor.
float spot(long long k) { return float(hash(uint32_t(k) * 2654435761u) % 2001) / 1000.0f - 1; }

float sign(float v) { return v < 0 ? -1.0f : 1.0f; }

// Lengths of each part of waking up, in seconds.
constexpr float kWake = 1.4f, kToBow = 0.8f, kBowHold = 1.4f, kToArch = 0.9f, kArchHold = 0.9f, kToStand = 0.7f,
                kTurn = 1.3f, kPause = 0.5f, kLieDown = 1.8f;
constexpr float kSpeed = 22;   // units a second, walking
constexpr float kStride = 20;  // units a step cycle
constexpr float kMinWalk = 10; // closer than this, she doesn't bother

// walking moves her legs, body and tail for a step cycle at phase.
void walking(CatPose& p, float phase, double t) {
    static const float offsets[4] = {0, kPi, kPi, 0};  // diagonal pairs together
    for (int i = 0; i < 4; i++) {
        float a = phase + offsets[i];
        p.paw[i].x += 6 * std::sin(a);
        p.paw[i].y -= std::max(0.0f, std::cos(a)) * 4;
    }
    float bob = 0.8f * std::sin(2 * phase);
    p.chest.y += bob, p.mid.y += bob, p.hips.y += bob, p.head.y += bob * 0.7f;
    for (int i = 0; i < CatPose::kTailPoints; i++) p.tail[i].x += std::sin(float(t) * 2.2f + i * 0.5f) * i * 0.35f;
}

// breathing rises and falls her sides, and now and then her tail tip
// twitches in her sleep.
void breathing(CatPose& p, double t, float asleep) {
    float b = std::sin(2 * kPi * float(t) / 3.6f) * asleep;
    p.rMid += 0.7f * b, p.mid.y -= 0.4f * b;
    p.rChest += 0.3f * b;
    float twitch = std::fmod(float(t), 7.3f);
    if (twitch < 0.8f) {
        float k = std::sin(twitch / 0.8f * kPi) * asleep;
        for (int i = 5; i < CatPose::kTailPoints; i++) p.tail[i].y -= k * (i - 4) * 1.5f;
    }
}

// More poses, for what she gets up to once she's awake.
// Sitting up, tail curled round her front paws.
const CatPose kSit = make({5, -25}, 8.5f, {0, -17}, 9.5f, {-6, -10}, 10.5f, {9, -38}, 0,
                          {{{8, -18}, {6, -18}, {-1, -7}, {-3, -7}}}, {{{9, 0}, {6, 0}, {6, -1}, {4, -1}}},
                          {{{-15, -6}, {-18, -3}, {-16, 0}, {-10, 0}, {-3, 0}, {4, 0}, {10, -1}, {14, -2}}}, 1, 0);
// Low and ready to spring: chest down, eyes wide, tail out straight.
const CatPose kCrouch = make({14, -9}, 8.5f, {2, -11}, 9, {-11, -13}, 9.5f, {25, -13}, -0.1f,
                             {{{16, -5}, {13, -5}, {-14, -8}, {-16, -8}}}, {{{19, 0}, {16, 0}, {-8, 0}, {-10, 0}}},
                             {{{-20, -14}, {-26, -13}, {-31, -12}, {-36, -11}, {-40, -10}, {-43, -10}, {-45, -11}, {-46, -13}}},
                             1, 0);
// Mid-pounce: stretched out, front paws reaching, back legs pushing off.
const CatPose kLeap = make({16, -26}, 8.5f, {2, -24}, 8.5f, {-12, -22}, 9, {28, -30}, -0.1f,
                           {{{22, -22}, {20, -21}, {-15, -18}, {-17, -18}}}, {{{30, -16}, {28, -15}, {-24, -10}, {-25, -9}}},
                           {{{-21, -24}, {-27, -25}, {-33, -26}, {-38, -27}, {-43, -27}, {-47, -26}, {-50, -25}, {-52, -24}}},
                           1, 0);

// withHead is a pose with its head moved, and its eyes as given.
CatPose withHead(CatPose p, Vec2f head, float tilt, float eyes) {
    p.head = head, p.headTilt = tilt, p.eyes = eyes;
    return p;
}

// Curled up, but her head raised drowsily.
const CatPose kSleepUp = withHead(kSleep, {17, -16}, -0.15f, 0.45f);

// Sitting with her near front paw up at her mouth, to lick it.
CatPose pawUp() {
    CatPose p = withHead(kSit, {9, -38}, 0.25f, 0.3f);
    p.joint[0] = {16, -25}, p.paw[0] = {20, -34};
    return p;
}
const CatPose kSitPawUp = pawUp();

// Sitting with her near back leg up behind her ear, to scratch it.
CatPose legUp() {
    CatPose p = withHead(kSit, {10, -37}, -0.45f, 0.25f);
    p.joint[2] = {-10, -19}, p.paw[2] = {2, -43};
    return p;
}
const CatPose kSitScratch = legUp();

// Head down over a bowl in front of her, lapping: her chin at its rim and
// her tongue's tip in the milk, behind the rim.
const CatPose kDrink = make({14, -11}, 8.5f, {2, -14}, 9, {-11, -18}, 9.5f, {25, -15}, 0.55f,
                            {{{15, -7}, {12, -7}, {-12, -12}, {-14, -12}}}, {{{17, 0}, {14, 0}, {-11, 0}, {-15, 0}}},
                            {{{-21, -20}, {-26, -23}, {-29, -28}, {-30, -34}, {-29, -39}, {-26, -43}, {-22, -45}, {-19, -44}}},
                            0.4f, 0);

// Crouched with her front paws pinning something ahead of her.
CatPose pin() {
    CatPose p = withHead(kCrouch, {27, -11}, 0.25f, 1);
    p.joint[0] = {20, -6}, p.joint[1] = {18, -6};
    p.paw[0] = {25, 0}, p.paw[1] = {23, 0};
    return p;
}
const CatPose kPin = pin();

// The same, batting it on with her near paw.
CatPose bat() {
    CatPose p = kPin;
    p.joint[0] = {21, -12}, p.paw[0] = {29, -7};
    return p;
}
const CatPose kBat = bat();

// Rest is where she settles at the end of an episode, and which way she
// faces there.
struct Rest {
    float x = 0, facing = 1;
};

// How long each of her activities takes, in seconds.
constexpr float kToSit = 0.8f, kPawUp = 0.5f, kLick = 1.8f, kWash = 1.6f, kLegUp = 0.5f, kScratch = 1.6f,
                kLookRound = 0.7f, kLookHold = 0.5f, kSlowBlink = 1.4f, kLookBack = 0.6f, kToCrouch = 0.6f,
                kWiggle = 1.5f, kLeapTime = 1.0f, kLand = 0.4f, kKnead = 3.2f, kStirUp = 0.7f, kStirLook = 1.8f,
                kStirDown = 0.9f, kSlideIn = 1.8f, kLower = 0.75f, kLap = 4.0f, kLickLips = 0.8f,
                kSlideOut = 1.4f, kRollIn = 1.6f, kPinDown = 0.8f, kBatTime = 0.8f, kProud = 1.0f;
constexpr float kPounce = 18;  // units forward
constexpr float kBowlAt = 31;  // from her middle to the bowl's, under her mouth
constexpr float kToyAt = 45;   // from her middle to the toy's, before she pounces: just past her paws once she lands
constexpr float kBatBy = 5;    // how far each bat sends the toy

float lengthOf(CatAction a) {
    switch (a) {
    case CatAction::Groom: return kToSit + kPawUp + kLick + kWash + kPawUp + kToSit;
    case CatAction::Scratch: return kToSit + kLegUp + kScratch + kLegUp + kToSit;
    case CatAction::Blink: return kToSit + kLookRound + kLookHold + kSlowBlink + kLookHold + kLookBack + kToSit;
    case CatAction::Pounce: return kToCrouch + kWiggle + kLeapTime + kLand + kToCrouch;
    case CatAction::Drink: return kSlideIn + 2 * kLower + kLap + kLickLips + kSlideOut;
    case CatAction::Hunt: return kRollIn + kToCrouch + kWiggle + kLeapTime + kPinDown + 2 * kBatTime + kToSit + kProud + kToSit;
    default: return 0;
    }
}

// Plan is what she does in one episode: where she starts and whether she
// walks somewhere, what she does there, and where she ends up.
struct Plan {
    float from = 0, to = 0, facing = 1, walkTo = 1;
    bool walks = false;
    float walk = 0, turn = 0;  // seconds of each
    CatAction doings[2] = {CatAction::Sleep, CatAction::Sleep};
    int count = 0;
    bool knead = false;
    bool stir = false;
    float stirAt = 0;  // seconds into her sleep
    Rest end;
};

// plan works out episode k from where the last left her. Everything comes
// from k's hash, so it's the same whenever it's asked.
Plan plan(long long k, Rest rest, float half) {
    float room = std::max(0.0f, half - 46);  // keep all of her on the floor, nose to tail
    Plan p;
    p.from = rest.x, p.facing = rest.facing;
    p.to = spot(k) * room;
    p.walkTo = sign(p.to - p.from);
    p.walks = std::fabs(p.to - p.from) >= kMinWalk;
    p.walk = p.walks ? std::fabs(p.to - p.from) / kSpeed : 0;
    p.turn = p.walks && p.walkTo != p.facing ? kTurn : 0;
    float x = p.walks ? p.to : p.from, dir = p.walks ? p.walkTo : p.facing;

    uint32_t h = hash(uint32_t(k) * 0x9E3779B9u + 7);
    const CatAction choices[] = {CatAction::Groom, CatAction::Scratch, CatAction::Blink,
                                 CatAction::Pounce, CatAction::Drink, CatAction::Hunt};
    int first = int(h % 6), second = (first + 1 + int((h >> 4) % 5)) % 6;
    p.count = (h >> 8) % 3 == 0 ? 2 : 1;
    p.doings[0] = choices[first], p.doings[1] = choices[second];
    for (int i = 0; i < p.count; i++) {
        CatAction& a = p.doings[i];
        // Each only with room on the floor for it: to land a pounce, for
        // the bowl in front of her, for the toy to skid along.
        bool lands = room > 0 && std::fabs(x + dir * kPounce) <= room;
        if (a == CatAction::Pounce && !lands) a = CatAction::Blink;
        if (a == CatAction::Drink && std::fabs(x + dir * kBowlAt) > half - 10) a = CatAction::Groom;
        if (a == CatAction::Hunt && (!lands || std::fabs(x + dir * (kToyAt + kPounce + 2 * kBatBy)) > half - 11))
            a = CatAction::Scratch;
        if (a == CatAction::Pounce || a == CatAction::Hunt) x += dir * kPounce;
    }
    p.knead = (h >> 12) % 2 == 0;
    p.stir = (h >> 16) % 5 < 2;
    p.stirAt = 15 + float((h >> 20) % 40);
    p.end = {x, dir};
    return p;
}

// restAfter is where she is after episode k, played from the first: where
// she is and which way she faces depend on every episode before. The last
// answer is kept, so each frame takes a step at most.
Rest restAfter(long long k, float half) {
    static long long lastK = -2;
    static float lastHalf = -1;
    static Rest last;
    long long from = -1;
    Rest r{spot(-1) * std::max(0.0f, half - 46), 1};
    if (half == lastHalf && lastK >= -1 && lastK <= k) from = lastK, r = last;
    for (long long j = from + 1; j <= k; j++) r = plan(j, r, half).end;
    lastK = k, lastHalf = half, last = r;
    return r;
}

// bump rises from 0 to 1 and back over k from 0 to 1, smoothly.
float bump(float k) { return std::sin(kPi * std::clamp(k, 0.0f, 1.0f)); }

// Timeline steps through an episode's parts: take reports whether the time
// falls in the next part, of len seconds, and how far into it.
struct Timeline {
    float at;
    bool take(float len, float& k) {
        if (len <= 0) return false;
        if (at < len) {
            k = at / len;
            return true;
        }
        at -= len;
        return false;
    }
};

// doing is her pose partway through an activity, which starts and ends
// standing where she is; dir is the way she faces, and x and lift are set
// for a pounce.
bool doing(CatAction a, Timeline& tl, double t, float dir, float half, Cat& c) {
    float k = 0;
    auto sitDown = [&] { return tl.take(kToSit, k) ? (c.pose = blend(kStand, kSit, smooth(k)), true) : false; };
    auto standUp = [&] { return tl.take(kToSit, k) ? (c.pose = blend(kSit, kStand, smooth(k)), true) : false; };
    float ft = float(t);
    switch (a) {
    case CatAction::Groom:
        if (sitDown()) return true;
        if (tl.take(kPawUp, k)) return c.pose = blend(kSit, kSitPawUp, smooth(k)), true;
        if (tl.take(kLick, k)) {
            // Quick licks, her head bobbing with them.
            c.pose = kSitPawUp;
            c.pose.tongue = 0.5f + 0.5f * std::sin(2 * kPi * 3 * ft);
            c.pose.headTilt += 0.06f * std::sin(2 * kPi * 3 * ft);
            return true;
        }
        if (tl.take(kWash, k)) {
            // Two strokes of the paw up over her face to her ear and down.
            c.pose = kSitPawUp;
            float up = std::pow(std::sin(2 * kPi * k), 2.0f);
            c.pose.paw[0] = {20 - 5 * up, -34 - 12 * up};
            c.pose.joint[0] = {16 - up, -25 - 6 * up};
            c.pose.headTilt = 0.35f + 0.1f * up;
            return true;
        }
        if (tl.take(kPawUp, k)) return c.pose = blend(kSitPawUp, kSit, smooth(k)), true;
        return standUp();
    case CatAction::Scratch:
        if (sitDown()) return true;
        if (tl.take(kLegUp, k)) return c.pose = blend(kSit, kSitScratch, smooth(k)), true;
        if (tl.take(kScratch, k)) {
            c.pose = kSitScratch;
            float s = std::sin(2 * kPi * 7 * ft);
            c.pose.paw[2].x += 0.8f * s, c.pose.paw[2].y += 1.6f * s;
            c.pose.joint[2].y += 0.8f * s;
            return true;
        }
        if (tl.take(kLegUp, k)) return c.pose = blend(kSitScratch, kSit, smooth(k)), true;
        return standUp();
    case CatAction::Blink:
        // She looks round at you and slowly blinks: a cat's way of saying
        // she likes you.
        if (sitDown()) return true;
        c.pose = kSit;
        if (tl.take(kLookRound, k)) return c.look = smooth(k), true;
        c.look = 1;
        if (tl.take(kLookHold, k)) return true;
        if (tl.take(kSlowBlink, k)) return c.pose.eyes = 1 - smooth(bump(k) * 1.4f), true;
        if (tl.take(kLookHold, k)) return true;
        if (tl.take(kLookBack, k)) return c.look = 1 - smooth(k), true;
        c.look = 0;
        return standUp();
    case CatAction::Pounce: {
        if (tl.take(kToCrouch, k)) return c.pose = blend(kStand, kCrouch, smooth(k)), true;
        if (tl.take(kWiggle, k)) {
            // The bum wiggle and tail flick before she goes.
            c.pose = kCrouch;
            float w = std::sin(2 * kPi * 4 * ft);
            c.pose.hips.x += 1.2f * w, c.pose.hips.y -= 0.6f * std::fabs(w);
            for (int i = 5; i < CatPose::kTailPoints; i++) c.pose.tail[i].y -= w * (i - 4) * 1.2f;
            return true;
        }
        if (tl.take(kLeapTime, k)) {
            c.pose = k < 0.5f ? blend(kCrouch, kLeap, smooth(k * 2)) : blend(kLeap, kCrouch, smooth(k * 2 - 1));
            c.x += dir * kPounce * smooth(k);
            c.lift = 10 * bump(k);
            return true;
        }
        c.x += dir * kPounce;
        if (tl.take(kLand, k)) return c.pose = kCrouch, true;
        if (tl.take(kToCrouch, k)) return c.pose = blend(kCrouch, kStand, smooth(k)), true;
        return false;
    }
    case CatAction::Drink: {
        // A bowl of milk slides in from her side of the floor and stops
        // under her nose; she laps it, licks her lips, and off it goes.
        float edge = dir * (half - 10), at = c.x + dir * kBowlAt;
        c.bowl.x = at, c.bowl.alpha = 1, c.bowl.fill = 1;
        if (tl.take(kSlideIn, k)) {
            c.pose = kStand;
            c.bowl.x = lerpf(edge, at, smooth(k)), c.bowl.alpha = std::min(1.0f, k * 2.5f);
            return true;
        }
        if (tl.take(kLower, k)) return c.pose = blend(kStand, kDrink, smooth(k)), true;
        if (tl.take(kLap, k)) {
            c.pose = kDrink;
            float lap = std::sin(2 * kPi * 4 * ft);
            c.pose.tongue = 0.5f + 0.5f * lap;
            c.pose.head.y += 0.6f * lap;
            c.bowl.fill = 1 - 0.75f * k;
            return true;
        }
        c.bowl.fill = 0.25f;
        if (tl.take(kLower, k)) return c.pose = blend(kDrink, kStand, smooth(k)), true;
        if (tl.take(kLickLips, k)) {
            c.pose = kStand;
            c.pose.tongue = std::fabs(std::sin(2 * kPi * k));
            return true;
        }
        if (tl.take(kSlideOut, k)) {
            c.pose = kStand;
            c.bowl.x = lerpf(at, edge, smooth(k)), c.bowl.alpha = std::min(1.0f, (1 - k) * 2.5f);
            return true;
        }
        c.bowl = CatProp{};
        return false;
    }
    case CatAction::Hunt: {
        // A toy mouse skids in ahead of her. She stalks it, pounces on it,
        // bats it along twice, then sits back pleased while it goes.
        float start = c.x, toyAt = start + dir * kToyAt;
        c.toy.x = toyAt, c.toy.alpha = 1;
        if (tl.take(kRollIn, k)) {
            c.pose = kStand;
            c.toy.x = lerpf(dir * (half - 11), toyAt, smooth(k));
            c.toy.rot = 0.45f * std::sin(4 * kPi * k) * (1 - k);  // wobbling as it skids
            c.toy.alpha = std::min(1.0f, k * 3);
            return true;
        }
        if (tl.take(kToCrouch, k)) return c.pose = blend(kStand, kCrouch, smooth(k)), true;
        if (tl.take(kWiggle, k)) {
            c.pose = kCrouch;
            float w = std::sin(2 * kPi * 4 * ft);
            c.pose.hips.x += 1.2f * w, c.pose.hips.y -= 0.6f * std::fabs(w);
            for (int i = 5; i < CatPose::kTailPoints; i++) c.pose.tail[i].y -= w * (i - 4) * 1.2f;
            return true;
        }
        if (tl.take(kLeapTime, k)) {
            c.pose = k < 0.5f ? blend(kCrouch, kLeap, smooth(k * 2)) : blend(kLeap, kPin, smooth(k * 2 - 1));
            c.x = start + dir * kPounce * smooth(k);
            c.lift = 10 * bump(k);
            return true;
        }
        c.x = start + dir * kPounce;
        if (tl.take(kPinDown, k)) return c.pose = kPin, true;
        for (int hit = 0; hit < 2; hit++) {
            if (tl.take(kBatTime, k)) {
                c.pose = blend(kPin, kBat, bump(k));
                // Knocked on with a hop and a tumble once her paw lands.
                float go = smooth(std::clamp(k * 2 - 0.5f, 0.0f, 1.0f));
                c.toy.x = toyAt + dir * kBatBy * (hit + go);
                c.toy.lift = 3 * bump(std::clamp(k * 2 - 0.5f, 0.0f, 1.0f));
                c.toy.rot = 0.45f * dir * std::sin(2 * kPi * go);
                return true;
            }
        }
        c.toy.x = toyAt + dir * 2 * kBatBy, c.toy.rot = 0;
        if (tl.take(kToSit, k)) return c.pose = blend(kPin, kSit, smooth(k)), true;
        if (tl.take(kProud, k)) return c.pose = kSit, c.toy.alpha = 1 - smooth(k), true;
        c.toy = CatProp{};
        if (tl.take(kToSit, k)) return c.pose = blend(kSit, kStand, smooth(k)), true;
        return false;
    }
    default: return false;
    }
}

}  // namespace

Cat CatAt(double t, float half) {
    // Start some way into an episode, so she's soon up for the first time.
    double s = t + kEpisode * 0.75;
    long long k = (long long)std::floor(s / kEpisode);
    float u = float(s - double(k) * kEpisode);
    Plan p = plan(k, restAfter(k - 1, half), half);

    float active = kWake + kToBow + kBowHold + kToArch + kArchHold + kToStand + p.turn + p.walk + kPause + kLieDown;
    for (int i = 0; i < p.count; i++) active += lengthOf(p.doings[i]);
    if (p.knead) active += kKnead;
    float sleep = float(kEpisode) - active;

    Cat c;
    c.x = p.from, c.facing = p.facing;
    Timeline tl{u};
    float f = 0;
    if (tl.take(sleep, f)) {
        c.pose = kSleep, c.asleep = 1;
        // Now and then she stirs: lifts her head, looks about drowsily and
        // settles again.
        float since = f * sleep - p.stirAt;
        float len = kStirUp + kStirLook + kStirDown;
        if (p.stir && since >= 0 && since < len) {
            c.action = CatAction::Stir;
            if (since < kStirUp) {
                float e = smooth(since / kStirUp);
                c.pose = blend(kSleep, kSleepUp, e), c.asleep = 1 - 0.6f * e;
            } else if (since < kStirUp + kStirLook) {
                c.pose = kSleepUp, c.asleep = 0.4f;
                c.pose.head.x += std::sin((since - kStirUp) / kStirLook * 2 * kPi);
            } else {
                float e = smooth((since - kStirUp - kStirLook) / kStirDown);
                c.pose = blend(kSleepUp, kSleep, e), c.asleep = 0.4f + 0.6f * e;
            }
        }
    } else if (c.action = CatAction::Wake; tl.take(kWake, f)) {
        c.pose = blend(kSleep, kStand, smooth(f)), c.asleep = 1 - smooth(f * 2);
    } else if (c.action = CatAction::Stretch, c.asleep = 0; tl.take(kToBow, f)) {
        c.pose = blend(kStand, kBow, smooth(f));
    } else if (tl.take(kBowHold, f)) {
        c.pose = kBow;
    } else if (tl.take(kToArch, f)) {
        c.pose = blend(kBow, kArch, smooth(f));
    } else if (tl.take(kArchHold, f)) {
        c.pose = kArch;
    } else if (tl.take(kToStand, f)) {
        c.pose = blend(kArch, kStand, smooth(f));
    } else if (c.action = CatAction::Turn; tl.take(p.turn, f)) {
        // Turning round, towards the viewer: see DrawCat.
        c.pose = kStand, c.turn = f;
        walking(c.pose, 2 * kPi * f, t);  // stepping round
    } else if (c.action = CatAction::Walk; tl.take(p.walk, f)) {
        c.pose = kStand, c.facing = p.walkTo;
        float gone = std::fabs(p.to - p.from) * f;
        c.x = p.from + p.walkTo * gone;
        walking(c.pose, 2 * kPi * gone / kStride, t);
    } else {
        if (p.walks) c.x = p.to, c.facing = p.walkTo;
        c.pose = kStand;
        bool busy = tl.take(kPause, f);
        if (busy) c.action = CatAction::Stand;
        for (int i = 0; i < p.count && !busy; i++) {
            c.action = p.doings[i];
            busy = doing(p.doings[i], tl, t, c.facing, half, c);
            if (!busy) c.lift = 0;
        }
        if (!busy && p.knead && tl.take(kKnead, f)) {
            // Making biscuits: front paws pressing in turn, eyes half shut.
            busy = true;
            c.action = CatAction::Knead;
            c.pose = kStand;
            float w = std::sin(2 * kPi * 1.6f * float(t));
            c.pose.paw[0].y -= std::max(0.0f, w) * 3, c.pose.joint[0].y -= std::max(0.0f, w) * 1.5f;
            c.pose.paw[1].y -= std::max(0.0f, -w) * 3, c.pose.joint[1].y -= std::max(0.0f, -w) * 1.5f;
            c.pose.chest.y += 0.4f * std::fabs(w);
            c.pose.eyes = 0.35f;
        }
        if (!busy) {
            c.action = CatAction::LieDown;
            f = std::min(tl.at / kLieDown, 1.0f);
            c.pose = blend(kStand, kSleep, smooth(f)), c.asleep = smooth(f * 1.5f - 0.5f);
        }
    }
    // A blink every few seconds while she's awake.
    if (c.asleep < 1 && std::fmod(t, 4.1) < 0.14) c.pose.eyes = 0;
    breathing(c.pose, t, c.asleep);
    return c;
}

namespace {

constexpr Rgba kFur{17, 17, 21}, kRim{92, 98, 112, 170}, kEye{92, 168, 245}, kPupil{8, 8, 10},
    kLid{120, 124, 136, 220}, kMouth{176, 84, 100}, kTongue{222, 112, 132}, kWhisker{150, 154, 166, 110}, kZ{150, 156, 172},
    kShadow{0, 0, 0, 55};

// Painter maps her pose onto the canvas, mirrored for which way she
// faces, and turned round about the vertical: pose x is how far forward a
// point is, and l how far to her side nearer the viewer. While she turns
// round, her head leads and her hips follow, so each point turns by an
// angle between theirs, from how far forward it is.
struct Painter {
    Canvas& c;
    Vec2f floor;
    float x, facing, size;
    float headTurn = 0, hipsTurn = 0;  // radians, 0 to pi
    float lift = 0;                     // units off the floor
    // Her head can also turn by itself, about its own middle, to look at
    // the viewer: head is set while drawing it, about pivot by look radians.
    bool head = false;
    Vec2f pivot;
    float look = 0;

    float angle(float f) const { return lerpf(hipsTurn, headTurn, std::clamp((f + 13) / 38, 0.0f, 1.0f)); }
    float headAngle() const { return angle(pivot.x) + look; }
    Vec2f m(Vec2f p, float l = 0) const {
        float y = floor.y + (p.y - lift) * size;
        if (head) {
            float a0 = angle(pivot.x), a = a0 + look, dx = p.x - pivot.x;
            return {floor.x + (x + facing * (pivot.x * std::cos(a0) + dx * std::cos(a) - l * std::sin(a))) * size, y};
        }
        float a = angle(p.x);
        return {floor.x + (x + facing * (p.x * std::cos(a) - l * std::sin(a))) * size, y};
    }
    // depth is how far towards the viewer a point is, beside a centre at
    // forward f0.
    float depth(float f, float l, float f0) const {
        float a = head ? headAngle() : angle(f0);
        return (f - f0) * std::sin(a) + l * std::cos(a);
    }
    // onHead is this painter drawing her head.
    Painter onHead() const {
        Painter h = *this;
        h.head = true;
        return h;
    }
    float w(float r) const { return r * size; }
    void circle(Vec2f p, float r, Rgba col, float l = 0) const { c.Circle(m(p, l), w(r), col); }
    void line(Vec2f a, Vec2f b, float t, Rgba col, float l = 0) const { c.Line(m(a, l), m(b, l), w(t), col); }
    void limb(Vec2f a, Vec2f b, float t, Rgba col, float l = 0) const {
        line(a, b, t, col, l);
        circle(a, t / 2, col, l);
        circle(b, t / 2, col, l);
    }
    void poly(std::initializer_list<Vec2f> pts, Rgba col, float l = 0) const {
        Vec2f out[8];
        int n = 0;
        for (Vec2f p : pts) out[n++] = m(p, l);
        // Mirroring reverses the winding, which ImGui doesn't mind.
        c.Polygon(out, n, col);
    }
};

// silhouette draws her body, legs, tail and head in one colour, grown by
// g units all round: once wide for her rim, then in black over it.
void silhouette(const Painter& d, const CatPose& p, float g, Rgba col) {
    // Far legs, then the tail, behind the body.
    // One colour throughout, so what's in front of what doesn't matter.
    d.limb(p.joint[1], p.paw[1], 4.6f + 2 * g, col, -3.5f);
    d.limb(p.joint[3], p.paw[3], 5.2f + 2 * g, col, -4);
    for (int i = 0; i + 1 < CatPose::kTailPoints; i++) {
        float t = 4.4f - 1.4f * float(i) / (CatPose::kTailPoints - 1);
        d.limb(p.tail[i], p.tail[i + 1], t + 2 * g, col);
    }
    // The body: three circles and the quads joining them.
    auto join = [&](Vec2f a, float ra, Vec2f b, float rb) {
        Vec2f dir = sub(b, a);
        float len = std::max(0.001f, std::hypot(dir.x, dir.y));
        Vec2f n = {-dir.y / len, dir.x / len};
        d.poly({add(a, scale(n, ra + g)), add(b, scale(n, rb + g)), sub(b, scale(n, rb + g)), sub(a, scale(n, ra + g))}, col);
    };
    d.circle(p.chest, p.rChest + g, col);
    d.circle(p.mid, p.rMid + g, col);
    d.circle(p.hips, p.rHips + g, col);
    join(p.chest, p.rChest, p.mid, p.rMid);
    join(p.mid, p.rMid, p.hips, p.rHips);
    d.limb(p.joint[0], p.paw[0], 4.6f + 2 * g, col, 3.5f);
    d.limb(p.joint[2], p.paw[2], 5.2f + 2 * g, col, 4);
    // Neck, head, muzzle and ears.
    d.limb(lerp(p.chest, p.head, 0.35f), lerp(p.chest, p.head, 0.8f), 8 + 2 * g, col);
    auto head = [&](Vec2f v) { return add(p.head, rotate(v, p.headTilt)); };
    Painter h = d.onHead();
    h.circle(p.head, p.rHead + g, col);
    h.circle(head({6, 2.5f}), 4.6f + g, col);
    for (float e : {-3.5f, 3.0f}) {
        float l = e < 0 ? -3.4f : 3.4f;  // side by side, seen face on
        Vec2f base1 = head({e - 3.6f, -5}), base2 = head({e + 3.6f, -5}), tip = head({e + 1.2f, -13.5f});
        Vec2f mid = scale(add(add(base1, base2), tip), 1.0f / 3);
        auto grow = [&](Vec2f v) {
            Vec2f off = sub(v, mid);
            float len = std::max(0.001f, std::hypot(off.x, off.y));
            return add(v, scale(off, g / len * 1.4f));
        };
        h.poly({grow(base1), grow(tip), grow(base2)}, col, l);
    }
}

void face(const Painter& body, const CatPose& p) {
    Painter d = body.onHead();
    auto head = [&](Vec2f v) { return add(p.head, rotate(v, p.headTilt)); };
    // Both eyes, the far one hidden behind her head unless she's turned
    // towards the viewer, and narrower the more side on each is.
    for (float l : {2.6f, -2.6f}) {
        Vec2f eye = head({3.8f, -1.2f});
        float k = std::clamp((d.depth(eye.x, l, p.head.x) + 0.5f) / 3, 0.0f, 1.0f);
        if (k <= 0) continue;
        if (p.eyes > 0.15f) {
            d.c.Ellipse(d.m(eye, l), d.w(1.9f * (0.5f + 0.5f * k)), d.w(2.4f * p.eyes), 0, kEye);
            d.c.Ellipse(d.m(eye, l), d.w(0.55f), d.w(2.0f * p.eyes), 0, kPupil);
        } else {
            // Closed: a sleepy curve.
            d.line(head({2.2f, -0.6f}), head({3.8f, 0.2f}), 0.8f, kLid, l);
            d.line(head({3.8f, 0.2f}), head({5.4f, -0.6f}), 0.8f, kLid, l);
        }
    }
    if (p.yawn > 0.3f) d.c.Ellipse(d.m(head({8.5f, 4.6f})), d.w(1.4f), d.w(2.4f * p.yawn), 0, kMouth);
    if (p.tongue > 0.05f) d.c.Ellipse(d.m(head({10.2f, 4.4f})), d.w(1.1f), d.w(1.7f * p.tongue), 0, kTongue);
    // Whiskers both sides, fanning out, the far ones only once she's
    // turned enough to show them.
    bool far = std::sin(d.headAngle()) > 0.25f;
    for (float side : {1.0f, -1.0f}) {
        if (side < 0 && !far) continue;
        for (float dy : {-0.6f, 1.0f}) {
            Vec2f a = head({9, 2 + dy}), b = head({15, 1 + dy * 2.2f});
            d.c.Line(d.m(a, side * 1.5f), d.m(b, side * 6), d.w(0.45f), kWhisker);
        }
    }
}

// zs float up from her head while she's asleep: a small z, rising, growing
// and fading, one after another.
void zs(const Painter& d, const Cat& cat, double t) {
    if (cat.asleep < 0.5f) return;  // not once her head is up
    for (int i = 0; i < 3; i++) {
        float k = float(std::fmod(t / 4.5 + i / 3.0, 1.0));
        float s = 2.2f + 2.6f * k;
        Vec2f at = add(cat.pose.head, {4 + 9 * k, -14 - 30 * k});
        Rgba col = kZ;
        col.a = uint8_t(255 * cat.asleep * std::min(k * 5, 1.0f) * (1 - k));
        // Z's read left to right whichever way she faces.
        Vec2f o = d.m(at);
        float w = d.w(s);
        d.c.Line({o.x - w / 2, o.y - w / 2}, {o.x + w / 2, o.y - w / 2}, d.w(0.9f), col);
        d.c.Line({o.x + w / 2, o.y - w / 2}, {o.x - w / 2, o.y + w / 2}, d.w(0.9f), col);
        d.c.Line({o.x - w / 2, o.y + w / 2}, {o.x + w / 2, o.y + w / 2}, d.w(0.9f), col);
    }
}

}  // namespace

// faded is a colour with its alpha scaled, for props fading in and out.
static Rgba faded(Rgba c, float a) {
    c.a = uint8_t(std::clamp(float(c.a) * a, 0.0f, 255.0f));
    return c;
}

// drawBowl draws a dish of milk on the floor, its rim over her chin when
// she's drinking from it.
static void drawBowl(Canvas& c, const CatProp& b, Vec2f floor, float size) {
    if (b.alpha <= 0) return;
    float a = b.alpha;
    auto at = [&](float x, float y) { return Vec2f{floor.x + (b.x + x) * size, floor.y + y * size}; };
    c.Ellipse(at(0, 0.6f), 9 * size, 1.2f * size, 0, faded(kShadow, a));
    Vec2f dish[4] = {at(-8, -4.5f), at(8, -4.5f), at(6, 0), at(-6, 0)};
    c.Polygon(dish, 4, faded(Rgba{196, 204, 222}, a));
    Vec2f band[4] = {at(-7.4f, -3.2f), at(7.4f, -3.2f), at(7.1f, -2.2f), at(-7.1f, -2.2f)};
    c.Polygon(band, 4, faded(Rgba{90, 140, 220}, a));
    c.Ellipse(at(0, -4.5f), 8 * size, 1.6f * size, 0, faded(Rgba{226, 232, 244}, a));
    // The milk, lower and narrower as she drinks it.
    if (b.fill > 0.05f) {
        float sink = (1 - b.fill) * 1.1f;
        c.Ellipse(at(0, -4.5f + sink * 0.5f), (6.8f - sink) * size, (1.1f - sink * 0.3f) * size, 0, faded(Rgba{250, 250, 244}, a));
    }
}

// drawToy draws a toy mouse, facing away from her (dir), tipped by rot.
static void drawToy(Canvas& c, const CatProp& m, float dir, Vec2f floor, float size) {
    if (m.alpha <= 0) return;
    float a = m.alpha;
    Vec2f mid{m.x, -2.4f - m.lift};
    auto at = [&](float x, float y) {
        Vec2f r = rotate({x * dir, y}, m.rot);
        return Vec2f{floor.x + (mid.x + r.x) * size, floor.y + (mid.y + r.y) * size};
    };
    if (m.lift < 4) c.Ellipse({floor.x + m.x * size, floor.y + 0.5f * size}, 5 * size, 0.9f * size, 0, faded(kShadow, a));
    Rgba fur = faded(Rgba{138, 143, 153}, a), pink = faded(Rgba{230, 140, 160}, a);
    c.Line(at(-4, 0.3f), at(-7, -1.2f), 0.6f * size, pink);
    c.Line(at(-7, -1.2f), at(-9.5f, 0.6f), 0.5f * size, pink);
    c.Ellipse(at(0, 0), 4.2f * size, 2.4f * size, m.rot, fur);
    c.Circle(at(3.4f, -0.3f), 1.8f * size, fur);
    c.Circle(at(2.4f, -2.2f), 1.2f * size, pink);
    c.Circle(at(5.3f, 0), 0.6f * size, pink);
    c.Circle(at(4.2f, -0.6f), 0.4f * size, faded(kPupil, a));
}

void DrawCat(Canvas& c, const Cat& cat, double t, Vec2f floor, float size) {
    // The toy goes under her paws; the bowl's rim over her chin.
    drawToy(c, cat.toy, cat.facing, floor, size);
    Painter d{c, floor, cat.x, cat.facing, size};
    // Turning round, her head comes round first, towards the viewer, and
    // her hips follow.
    d.headTurn = kPi * smooth(cat.turn * 1.4f);
    d.hipsTurn = kPi * smooth(cat.turn * 1.4f - 0.4f);
    float across = std::fabs(std::cos((d.headTurn + d.hipsTurn) / 2));
    // Her shadow stays on the floor, smaller the higher she leaps.
    float high = 1 - std::clamp(cat.lift / 30, 0.0f, 0.5f);
    d.c.Ellipse(d.m({0, 0.5f}), d.w(26 * std::max(0.55f, across) * high), d.w((2.6f + 0.8f * (1 - across)) * high), 0, kShadow);
    d.lift = cat.lift;
    // Looking at the viewer, her head comes most of the way round.
    d.pivot = cat.pose.head;
    d.look = cat.look * kPi * 0.42f;
    silhouette(d, cat.pose, 1.1f, kRim);
    silhouette(d, cat.pose, 0, kFur);
    face(d, cat.pose);
    drawBowl(c, cat.bowl, floor, size);
    zs(d, cat, t);
}

}  // namespace ks
