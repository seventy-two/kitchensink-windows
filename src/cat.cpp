#include "cat.h"

#include <algorithm>
#include <cmath>
#include <deque>

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

// His poses, facing right.
const CatPose kStand = make({13, -22}, 9.5f, {0, -23}, 9, {-13, -22}, 10, {25, -31}, 0,
                            {{{14, -16}, {11, -16}, {-13, -15}, {-16, -15}}}, {{{15, 0}, {11, 0}, {-12, 0}, {-17, 0}}},
                            {{{-22, -25}, {-27, -29}, {-30, -35}, {-31, -42}, {-30, -48}, {-27, -53}, {-23, -55}, {-20, -54}}},
                            1, 0);
// Curled up asleep: head down on his paws, tail wrapped round the front.
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
    p.ear = lerpf(a.ear, b.ear, k);
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

// spot is where he settles at the end of episode k, from -1 to 1 across
// the floor.
float spot(long long k) { return float(hash(uint32_t(k) * 2654435761u) % 2001) / 1000.0f - 1; }

float sign(float v) { return v < 0 ? -1.0f : 1.0f; }

// Lengths of each part of waking up, in seconds.
constexpr float kWake = 1.4f, kToBow = 0.8f, kBowHold = 1.4f, kToArch = 0.9f, kArchHold = 0.9f, kToStand = 0.7f,
                kTurn = 1.3f, kPause = 0.5f, kLieDown = 1.8f;
constexpr float kSpeed = 22;   // units a second, walking
constexpr float kStride = 20;  // units a step cycle
constexpr float kMinWalk = 10; // closer than this, he doesn't bother

// walking moves his legs, body and tail for a step cycle at phase.
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

// breathing rises and falls his sides, and now and then his tail tip
// twitches in his sleep.
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

// More poses, for what he gets up to once he's awake.
// Sitting up, tail curled round his front paws.
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

// Curled up, but his head raised drowsily.
const CatPose kSleepUp = withHead(kSleep, {17, -16}, -0.15f, 0.45f);

// Sitting with his near front paw up at his mouth, to lick it.
CatPose pawUp() {
    CatPose p = withHead(kSit, {9, -38}, 0.25f, 0.3f);
    p.joint[0] = {16, -25}, p.paw[0] = {20, -34};
    return p;
}
const CatPose kSitPawUp = pawUp();

// Sitting with his near back leg up behind his ear, to scratch it.
CatPose legUp() {
    CatPose p = withHead(kSit, {10, -37}, -0.45f, 0.25f);
    p.joint[2] = {-10, -19}, p.paw[2] = {2, -43};
    return p;
}
const CatPose kSitScratch = legUp();

// Head down over a bowl in front of him, lapping: his chin at its rim and
// his tongue's tip in the milk, behind the rim.
const CatPose kDrink = make({14, -11}, 8.5f, {2, -14}, 9, {-11, -18}, 9.5f, {25, -15}, 0.55f,
                            {{{15, -7}, {12, -7}, {-12, -12}, {-14, -12}}}, {{{17, 0}, {14, 0}, {-11, 0}, {-15, 0}}},
                            {{{-21, -20}, {-26, -23}, {-29, -28}, {-30, -34}, {-29, -39}, {-26, -43}, {-22, -45}, {-19, -44}}},
                            0.4f, 0);

// Crouched with his front paws pinning something ahead of him.
CatPose pin() {
    CatPose p = withHead(kCrouch, {27, -11}, 0.25f, 1);
    p.joint[0] = {20, -6}, p.joint[1] = {18, -6};
    p.paw[0] = {25, 0}, p.paw[1] = {23, 0};
    return p;
}
const CatPose kPin = pin();

// The same, batting it on with his near paw.
CatPose bat() {
    CatPose p = kPin;
    p.joint[0] = {21, -12}, p.paw[0] = {29, -7};
    return p;
}
const CatPose kBat = bat();

// How he lies asleep. A loaf: sitting low, paws tucked under, head up.
const CatPose kLoaf = make({8, -11}, 9, {-2, -12}, 10.5f, {-12, -11}, 10, {17, -20}, 0.05f,
                           {{{10, -4}, {8, -4}, {-8, -4}, {-10, -4}}}, {{{13, -1.5f}, {11, -1.5f}, {-6, -1.5f}, {-9, -1.5f}}},
                           {{{-21, -8}, {-23, -4}, {-20, -1}, {-12, 0}, {-4, 0}, {4, 0}, {10, -0.5f}, {14, -1}}}, 0, 0);
// Flat out on his side, legs stretched out front and back.
const CatPose kSide = make({12, -8}, 8, {0, -8.5f}, 9, {-12, -8}, 9, {26, -7}, -0.1f,
                           {{{18, -5}, {16, -6}, {-16, -5}, {-18, -6}}}, {{{30, -1}, {28, -2}, {-26, -2}, {-27, -3}}},
                           {{{-20, -6}, {-25, -4}, {-29, -2}, {-33, -1}, {-37, -0.5f}, {-40, -0.5f}, {-42, -1}, {-44, -1.5f}}},
                           0, 0);
// Belly up, paws in the air, head tipped back.
const CatPose kBelly = make({10, -9}, 8.5f, {-1, -9}, 9.5f, {-12, -9}, 9.5f, {22, -9}, -0.9f,
                            {{{10, -19}, {8, -18}, {-12, -19}, {-14, -18}}}, {{{14, -24}, {11, -25}, {-9, -25}, {-16, -24}}},
                            {{{-20, -6}, {-25, -3}, {-30, -1.5f}, {-34, -1}, {-38, -1}, {-41, -1.5f}, {-43, -2.5f}, {-44, -4}}},
                            0, 0);

const CatPose& bedPose(CatBed b) {
    switch (b) {
    case CatBed::Loaf: return kLoaf;
    case CatBed::Side: return kSide;
    case CatBed::Belly: return kBelly;
    default: return kSleep;
    }
}

// headUp is a bed with his head raised drowsily.
CatPose headUp(CatBed b) {
    if (b == CatBed::Curl) return kSleepUp;
    const CatPose& p = bedPose(b);
    return withHead(p, add(p.head, {-1, -5}), p.headTilt - 0.2f, 0.45f);
}

// Sitting on a panel's edge, his tail hanging down over it.
CatPose perched() {
    CatPose p = kSit;
    p.tail = {{{-15, -6}, {-18, -2}, {-19, 3}, {-19, 9}, {-18, 15}, {-17, 21}, {-15, 26}, {-13, 28}}};
    return p;
}
const CatPose kPerch = perched();

// The same, his near paw out against a mug.
CatPose perchPaw() {
    CatPose p = withHead(kPerch, {10, -37}, 0.35f, 1);
    p.joint[0] = {12, -11}, p.paw[0] = {14, -5};
    return p;
}
const CatPose kPerchPaw = perchPaw();

// A loaf in a box, wide awake: tucked in small so all of him but his head
// and the top of his back is below the rim, his chin just over the front
// and the tip of his tail curled up over the back.
CatPose boxed() {
    CatPose p = make({7, -12}, 9, {-2, -13}, 10, {-9, -12}, 9.5f, {11, -25}, 0,
                     {{{9, -8}, {7, -8}, {-7, -8}, {-9, -8}}}, {{{12, -5}, {10, -5}, {-5, -5}, {-8, -5}}},
                     {{{-15, -14}, {-17, -17}, {-17.5f, -21}, {-17, -25}, {-15.5f, -28.5f}, {-13, -30.5f}, {-10.5f, -31}, {-8.5f, -30}}},
                     1, 0);
    return p;
}
const CatPose kBoxed = boxed();

// Rest is where he settles at the end of an episode: where, which way he
// faces, how he lies, and whether he's awake (only when a plan's cut
// short).
struct Rest {
    float x = 0, facing = 1;
    CatPlace place = CatPlace::Top;
    CatBed bed = CatBed::Curl;
    bool awake = false;
    float down = 0;     // at the bottom: seconds he's been down there so far
    bool back = false;  // just back up from the bottom
};

// How long each of his activities takes, in seconds.
constexpr float kToSit = 0.8f, kPawUp = 0.5f, kLick = 1.8f, kWash = 1.6f, kLegUp = 0.5f, kScratch = 1.6f,
                kLookRound = 0.7f, kLookHold = 0.5f, kSlowBlink = 1.4f, kLookBack = 0.6f, kToCrouch = 0.6f,
                kWiggle = 1.5f, kLeapTime = 1.0f, kLand = 0.4f, kKnead = 3.2f, kStirUp = 0.7f, kStirLook = 1.8f,
                kStirDown = 0.9f, kSlideIn = 1.8f, kLower = 0.75f, kLap = 4.0f, kLickLips = 0.8f,
                kSlideOut = 1.4f, kRollIn = 1.6f, kPinDown = 0.8f, kBatTime = 0.8f, kProud = 1.0f, kSkid = 0.4f,
                kZoomTurn = 0.6f, kSniff = 0.8f, kHop = 1.0f, kBoxStay = 14, kJumpUp = 0.9f, kSettle = 0.6f,
                kPerchStay = 12, kDrop = 0.6f, kEat = 10, kLash = 1.2f;
constexpr float kPounce = 18;  // units forward
constexpr float kBowlAt = 31;  // from his middle to the bowl's, under his mouth
constexpr float kToyAt = 45;   // from his middle to the toy's, before he pounces: just past his paws once he lands
constexpr float kBatBy = 5;    // how far each bat sends the toy
constexpr float kRun = 95;     // units a second, on a mad sprint
constexpr float kGallop = 34;  // units a stride, sprinting
constexpr float kSkidBy = 7;   // units he skids at the end of a sprint
constexpr float kBoxAt = 32, kBoxOut = 20;  // to the box's middle, and on past it hopping out
constexpr float kUpBy = 12, kDownBy = 8;    // forward jumping up onto a panel's edge, and down
constexpr float kMugAt = 16;                // from his middle to the mug's, up there

// galloping moves his legs, body and tail for a sprinting stride at phase,
// amt of the way into it (easing in and out of a sprint).
void galloping(CatPose& p, float phase, float amt) {
    static const float offsets[4] = {0, 0.5f, kPi, kPi + 0.5f};  // front pair, then back pair
    for (int i = 0; i < 4; i++) {
        float a = phase + offsets[i];
        p.paw[i].x += amt * 9 * std::sin(a);
        p.paw[i].y -= amt * std::max(0.0f, std::cos(a)) * 6;
    }
    float s = amt * std::sin(phase);
    p.chest.x += 2 * s, p.hips.x -= 2 * s;
    float bob = amt * 1.6f * std::sin(2 * phase);
    p.chest.y += bob, p.mid.y += bob, p.hips.y += bob;
    p.head = add(p.head, {2 * amt, 3 * amt + bob * 0.5f});
    p.ear = -0.6f * amt;  // ears back, going for it
    for (int i = 0; i < CatPose::kTailPoints; i++) p.tail[i] = lerp(p.tail[i], kCrouch.tail[i], 0.7f * amt);
}

// Doing is one of the things he does in an episode: what, starting where,
// and for a wait, how long.
struct Doing {
    CatAction a = CatAction::Sleep;
    float x0 = 0, len = 0;
};

// Where is what doing needs to know of the place he's doing it in.
struct Where {
    float half = 0, room = 0, ledge = 0;
};

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

// wiggle is the bum wiggle and tail flick before he springs.
void wiggle(CatPose& p, float ft) {
    p = kCrouch;
    float w = std::sin(2 * kPi * 4 * ft);
    p.hips.x += 1.2f * w, p.hips.y -= 0.6f * std::fabs(w);
    for (int i = 5; i < CatPose::kTailPoints; i++) p.tail[i].y -= w * (i - 4) * 1.2f;
}

// doing is his pose partway through an activity, which starts and ends
// standing where he is; dir is the way he faces, and x, lift, facing and
// turn are set where it moves him.
bool doing(const Doing& d, Timeline& tl, double t, float dir, const Where& wh, Cat& c) {
    float k = 0;
    auto sitDown = [&] { return tl.take(kToSit, k) ? (c.pose = blend(kStand, kSit, smooth(k)), true) : false; };
    auto standUp = [&] { return tl.take(kToSit, k) ? (c.pose = blend(kSit, kStand, smooth(k)), true) : false; };
    float ft = float(t), half = wh.half;
    c.x = d.x0;
    switch (d.a) {
    case CatAction::Groom:
        if (sitDown()) return true;
        if (tl.take(kPawUp, k)) return c.pose = blend(kSit, kSitPawUp, smooth(k)), true;
        if (tl.take(kLick, k)) {
            // Quick licks, his head bobbing with them.
            c.pose = kSitPawUp;
            c.pose.tongue = 0.5f + 0.5f * std::sin(2 * kPi * 3 * ft);
            c.pose.headTilt += 0.06f * std::sin(2 * kPi * 3 * ft);
            return true;
        }
        if (tl.take(kWash, k)) {
            // Two strokes of the paw up over his face to his ear and down.
            c.pose = kSitPawUp;
            float up = std::pow(std::sin(2 * kPi * k), 2.0f);
            c.pose.paw[0] = {20 - 5 * up, -34 - 12 * up};
            c.pose.joint[0] = {16 - up, -25 - 6 * up};
            c.pose.headTilt = 0.35f + 0.1f * up;
            c.pose.ear = -0.5f * up;
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
        // He looks round at you and slowly blinks: a cat's way of saying
        // he likes you.
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
        if (tl.take(kWiggle, k)) return wiggle(c.pose, ft), true;
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
    case CatAction::Drink:
    case CatAction::Eat: {
        // A bowl slides in from his side of the floor and stops under his
        // nose: milk, which he laps, or at mealtimes his dinner, which he
        // crunches through. Then he licks his lips, and off it goes.
        bool eat = d.a == CatAction::Eat;
        CatProp& bowl = eat ? c.food : c.bowl;
        float edge = dir * (half - 10), at = c.x + dir * kBowlAt;
        bowl.x = at, bowl.alpha = 1, bowl.fill = 1;
        if (tl.take(kSlideIn, k)) {
            c.pose = kStand;
            if (eat) c.pose.ear = 1, c.pose.headTilt = 0.15f * smooth(k);
            bowl.x = lerpf(edge, at, smooth(k)), bowl.alpha = std::min(1.0f, k * 2.5f);
            return true;
        }
        if (tl.take(kLower, k)) return c.pose = blend(kStand, kDrink, smooth(k)), true;
        if (tl.take(eat ? kEat : kLap, k)) {
            c.pose = kDrink;
            if (eat) {
                // Head down, crunching: a slower, deeper bob.
                float chew = std::sin(2 * kPi * 2.2f * ft);
                c.pose.head.y += 0.9f * chew, c.pose.headTilt += 0.08f * chew;
            } else {
                float lap = std::sin(2 * kPi * 4 * ft);
                c.pose.tongue = 0.5f + 0.5f * lap;
                c.pose.head.y += 0.6f * lap;
            }
            bowl.fill = 1 - (eat ? 0.95f : 0.75f) * k;
            return true;
        }
        bowl.fill = eat ? 0.05f : 0.25f;
        if (tl.take(kLower, k)) return c.pose = blend(kDrink, kStand, smooth(k)), true;
        if (tl.take(kLickLips, k)) {
            c.pose = kStand;
            c.pose.tongue = std::fabs(std::sin(2 * kPi * k));
            return true;
        }
        if (tl.take(kSlideOut, k)) {
            c.pose = kStand;
            bowl.x = lerpf(at, edge, smooth(k)), bowl.alpha = std::min(1.0f, (1 - k) * 2.5f);
            return true;
        }
        bowl = CatProp{};
        return false;
    }
    case CatAction::Hunt: {
        // A toy mouse skids in ahead of him. He stalks it, pounces on it,
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
        if (tl.take(kWiggle, k)) return wiggle(c.pose, ft), true;
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
                // Knocked on with a hop and a tumble once his paw lands.
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
    case CatAction::Zoomies: {
        // A mad sprint to the end of the floor, a skid, round, flat out to
        // the other end, and back to where he started.
        float e1 = dir * wh.room, e2 = -dir * wh.room;
        const float from[3] = {d.x0, e1, e2}, to[3] = {e1, e2, d.x0};
        float fac = dir;
        for (int i = 0; i < 3; i++) {
            float dist = std::fabs(to[i] - from[i]), sgn = to[i] >= from[i] ? 1.0f : -1.0f;
            float runBy = std::max(0.0f, dist - kSkidBy);
            c.facing = fac;
            if (tl.take(runBy / kRun, k)) {
                float gone = runBy * k;
                c.x = from[i] + sgn * gone;
                c.pose = kStand;
                galloping(c.pose, 2 * kPi * gone / kGallop, smooth(std::min(gone, runBy - gone) / 12));
                return true;
            }
            if (tl.take(kSkid, k)) {
                c.x = from[i] + sgn * (runBy + (dist - runBy) * std::sin(k * kPi / 2));
                c.pose = blend(kStand, kCrouch, 0.3f * bump(k));
                c.pose.head.x -= 2 * bump(k), c.pose.ear = -0.6f;
                return true;
            }
            c.x = to[i];
            if (i < 2) {
                if (tl.take(kZoomTurn, k)) {
                    c.pose = kStand, c.turn = k;
                    walking(c.pose, 4 * kPi * k, t);
                    return true;
                }
                fac = -fac;
            }
        }
        c.facing = fac;
        return false;
    }
    case CatAction::Box: {
        // A cardboard box slides in ahead of him. He sniffs it, hops in,
        // sits there pleased with himself, hops out the far side, and it
        // slides away behind him.
        float at = d.x0 + dir * kBoxAt, out = at + dir * kBoxOut, off = half + 20;
        c.box.x = at, c.box.alpha = 1;
        if (tl.take(kSlideIn, k)) {
            c.pose = kStand;
            c.box.x = lerpf(dir * off, at, smooth(k));
            return true;
        }
        if (tl.take(kSniff, k)) return c.pose = kStand, c.pose.headTilt = 0.35f * bump(k), c.pose.ear = bump(k), true;
        if (tl.take(kToCrouch, k)) return c.pose = blend(kStand, kCrouch, smooth(k)), true;
        if (tl.take(kHop, k)) {
            c.pose = k < 0.5f ? blend(kCrouch, kLeap, smooth(k * 2)) : blend(kLeap, kBoxed, smooth(k * 2 - 1));
            c.x = lerpf(d.x0, at, smooth(k));
            c.lift = 12 * bump(k);
            c.inBox = k > 0.6f;
            return true;
        }
        c.x = at;
        if (tl.take(kBoxStay, k)) {
            c.inBox = true;
            c.pose = kBoxed;
            // A look round at you partway through, and a slow blink.
            c.look = smooth(std::clamp((k - 0.3f) * 8, 0.0f, 1.0f)) * smooth(std::clamp((0.65f - k) * 8, 0.0f, 1.0f));
            if (k > 0.42f && k < 0.52f) c.pose.eyes = 1 - smooth(bump((k - 0.42f) / 0.1f) * 1.4f);
            c.pose.ear = k > 0.78f && k < 0.81f ? 1.0f : 0.0f;
            for (int i = 4; i < CatPose::kTailPoints; i++) c.pose.tail[i].x += std::sin(ft * 2.4f + i * 0.6f) * (i - 3) * 0.4f;
            return true;
        }
        if (tl.take(kHop, k)) {
            c.pose = k < 0.5f ? blend(kBoxed, kLeap, smooth(k * 2)) : blend(kLeap, kStand, smooth(k * 2 - 1));
            c.x = lerpf(at, out, smooth(k));
            c.lift = 12 * bump(k);
            c.inBox = k < 0.3f;
            return true;
        }
        c.x = out;
        if (tl.take(kSlideOut, k)) {
            c.pose = kStand;
            c.box.x = lerpf(at, -dir * off, smooth(k));
            return true;
        }
        c.box = CatProp{};
        return false;
    }
    case CatAction::Perch:
    case CatAction::KnockOff: {
        // He jumps up onto the last panel's edge and sits there, tail
        // hanging, watching. Sometimes there's a mug up there: he looks at
        // it, looks at you, and pushes it off.
        bool mug = d.a == CatAction::KnockOff;
        float L = wh.ledge, up = d.x0 + dir * kUpBy, down = up + dir * kDownBy;
        float mugX = up + dir * kMugAt;
        if (mug) c.mug = {mugX, L, 1, 0, 0};
        if (mug && tl.take(kSniff, k)) {
            c.mug.alpha = smooth(k);
            return c.pose = kStand, c.pose.headTilt = -0.3f * smooth(k), true;
        }
        if (tl.take(kToCrouch, k)) return c.pose = blend(kStand, kCrouch, smooth(k)), c.pose.headTilt -= 0.3f, true;
        if (tl.take(kWiggle * 0.5f, k)) return wiggle(c.pose, ft), true;
        if (tl.take(kJumpUp, k)) {
            c.pose = k < 0.5f ? blend(kCrouch, kLeap, smooth(k * 2)) : blend(kLeap, kCrouch, smooth(k * 2 - 1));
            c.lift = L * smooth(k) + 10 * bump(k);
            c.x = lerpf(d.x0, up, smooth(k));
            return true;
        }
        c.x = up, c.lift = L, c.ground = L;
        if (tl.take(kSettle, k)) return c.pose = blend(kCrouch, kPerch, smooth(k)), true;
        auto swing = [&] {
            for (int i = 2; i < CatPose::kTailPoints; i++) c.pose.tail[i].x += std::sin(ft * 1.3f + i * 0.4f) * (i - 1) * 0.45f;
        };
        if (!mug) {
            if (tl.take(kPerchStay, k)) {
                c.pose = kPerch;
                swing();
                c.look = smooth(std::clamp((k - 0.35f) * 8, 0.0f, 1.0f)) * smooth(std::clamp((0.7f - k) * 8, 0.0f, 1.0f));
                if (k > 0.5f && k < 0.6f) c.pose.eyes = 1 - smooth(bump((k - 0.5f) / 0.1f) * 1.4f);
                return true;
            }
        } else {
            if (tl.take(1.0f, k)) return c.pose = kPerch, c.pose.headTilt = 0.35f * smooth(k), swing(), true;
            if (tl.take(1.4f, k)) return c.pose = kPerch, c.pose.headTilt = 0.35f * (1 - smooth(k * 3)), c.look = bump(k), swing(), true;
            if (tl.take(1.4f, k)) {
                // The push: his paw out, slowly, and the mug edging along
                // and tipping.
                c.pose = blend(kPerch, kPerchPaw, smooth(std::min(1.0f, k * 2)));
                float go = smooth(std::max(0.0f, k - 0.3f) / 0.7f);
                c.mug.x = mugX + dir * 6 * go, c.mug.rot = dir * 0.3f * go;
                return true;
            }
            float fall = mugX + dir * 6;
            if (tl.take(0.8f, k)) {
                c.pose = blend(kPerchPaw, kPerch, smooth(k)), c.pose.headTilt = 0.55f * smooth(k);
                c.mug = {fall + dir * 3 * k, L * (1 - k * k), 1, dir * (0.3f + (kPi / 2 - 0.3f) * smooth(k)), 0};
                return true;
            }
            c.mug = {fall + dir * 3, 0, 1, dir * kPi / 2, 0};
            if (tl.take(0.35f, k)) return c.pose = kPerch, c.pose.headTilt = 0.55f, c.mug.lift = 4 * bump(k), true;
            if (tl.take(1.4f, k)) return c.pose = kPerch, c.pose.headTilt = 0.55f * (1 - smooth(k * 3)), c.look = bump(k), true;
        }
        // Down again: the mug fades as he goes.
        float fade = c.mug.alpha;
        if (tl.take(kSettle, k)) return c.pose = blend(kPerch, kCrouch, smooth(k)), c.mug.alpha = fade * (1 - k / 3), true;
        c.mug.alpha = fade * 2 / 3;
        if (tl.take(kDrop, k)) {
            c.lift = L * (1 - k * k), c.ground = 0;
            c.x = lerpf(up, down, k);
            c.pose = blend(kCrouch, kStand, smooth(k));
            c.mug.alpha = fade * (2 - k) / 3;
            return true;
        }
        c.x = down, c.lift = 0, c.ground = 0;
        c.mug.alpha = fade / 3;
        if (tl.take(kLand, k)) return c.pose = kCrouch, c.mug.alpha = fade * (1 - k) / 3, true;
        c.mug = CatProp{};
        if (tl.take(kToCrouch, k)) return c.pose = blend(kCrouch, kStand, smooth(k)), true;
        return false;
    }
    case CatAction::Wait: {
        // Waiting for his dinner: sitting by the side it comes from, tail
        // twitching, telling you about it now and then.
        if (sitDown()) return true;
        float len = std::max(0.0f, d.len - 2 * kToSit);
        if (tl.take(len, k)) {
            c.pose = kSit;
            float e = k * len, ph = std::fmod(e, 16.0f);
            c.say = std::clamp(std::min(ph * 4, (2.2f - ph) * 3), 0.0f, 1.0f);
            c.pose.yawn = 0.7f * bump((ph - 0.1f) / 0.6f);
            c.look = smooth(std::clamp(ph * 3, 0.0f, 1.0f)) * smooth(std::clamp((2.6f - ph) * 3, 0.0f, 1.0f));
            c.pose.ear = 0.6f;
            for (int i = 5; i < CatPose::kTailPoints; i++) c.pose.tail[i].y -= std::sin(ft * 3) * (i - 4) * 0.8f;
            return true;
        }
        return standUp();
    }
    case CatAction::Lash: {
        // Had enough: ears back, tail lashing.
        if (tl.take(kLash, k)) {
            c.pose = kStand, c.pose.ear = -1;
            for (int i = 2; i < CatPose::kTailPoints; i++) c.pose.tail[i].x += std::sin(ft * 14) * (i - 1) * 1.1f;
            return true;
        }
        return false;
    }
    default: return false;
    }
}

// lengthOf is how long doing d takes: every part, timed by running it with
// more time than it needs.
float lengthOf(const Doing& d, float dir, const Where& wh) {
    Cat scratch;
    Timeline tl{10000};
    doing(d, tl, 0, dir, wh, scratch);
    return 10000 - tl.at;
}

constexpr float kWakeAll = kWake + kToBow + kBowHold + kToArch + kArchHold + kToStand;
constexpr float kOff = 50;     // past the end of the floor, he's out of sight

enum class Special { None, Meal, Annoyed, Hurry };

// Plan is one episode: he sleeps, wakes and stretches, perhaps turns round
// and walks somewhere (perhaps out of sight and into the other place),
// does a thing or two there, perhaps kneads his bed, and lies down.
struct Plan {
    Rest from;
    float sleep = 0;
    bool wake = false, lash = false;
    float turn = 0, dir = 1;
    bool walks = false, cross = false;
    float to = 0, exitX = 0, enterX = 0, walk = 0, dist = 0;
    CatPlace toPlace = CatPlace::Top;
    Where at;  // where he ends up
    Doing doings[3];
    int count = 0;
    bool knead = false, stir = false;
    float stirAt = 0;
    CatBed bed = CatBed::Curl;
    float length = 0;
    Rest end;
};

float halfOf(CatPlace pl, const CatWorld& w) { return pl == CatPlace::Top ? w.half : w.gapHalf; }
float roomOf(CatPlace pl, const CatWorld& w) { return std::max(0.0f, halfOf(pl, w) - 46); }
bool gapOpen(const CatWorld& w) { return w.gapHalf >= kMinGapHalf; }

// makePlan works out episode k from where he is. Everything comes from k's
// hash, the world and what's asked of him, so it's the same whenever it's
// asked. wait, for a meal, is the seconds till it comes.
Plan makePlan(long long k, Rest r, const CatWorld& w, Special sp, float wait) {
    uint32_t h = hash(uint32_t(k) * 0x9E3779B9u + 7), h2 = hash(h + 1), h3 = hash(h2 + 1);
    auto roll = [](uint32_t v, int pct) { return int(v % 100) < pct; };
    float hour = w.clock / 3600;
    bool lively = (hour >= 6 && hour < 9) || (hour >= 17 && hour < 21);
    bool night = hour >= 23 || hour < 6;

    Plan p;
    p.from = r;
    p.wake = !r.awake;
    p.lash = sp == Special::Annoyed;

    // Where to: somewhere else on this floor, or (now and then, when
    // there's room at the bottom) the other place.
    CatPlace dest = r.place;
    if (sp == Special::None || sp == Special::Annoyed) {
        // Down there only now and then, for a couple of minutes: from
        // the bottom he always comes back up.
        if (r.place == CatPlace::Top && !r.back && gapOpen(w) && roll(h3, sp == Special::Annoyed ? 25 : night ? 4 : 10))
            dest = CatPlace::Bottom;
        else if (r.place == CatPlace::Bottom)
            dest = CatPlace::Top;
    }
    float room = roomOf(dest, w), half = halfOf(dest, w);
    float to = spot(k) * room;
    if (sp == Special::Annoyed && dest == r.place) to = (r.x > 0 ? -1.0f : 1.0f) * room * (0.6f + 0.4f * float(h2 % 100) / 100);
    if (sp == Special::Meal) to = std::clamp(r.x, -room, room);
    p.cross = dest != r.place;
    p.toPlace = dest;
    float speed = sp == Special::Annoyed || sp == Special::Hurry ? kSpeed * 1.6f : kSpeed;
    if (p.cross) {
        // Out of sight to the right, and in from the left.
        p.walks = true, p.dir = 1;
        p.exitX = halfOf(r.place, w) + kOff, p.enterX = -(half + kOff);
        p.dist = (p.exitX - r.x) + (to - p.enterX);
    } else {
        p.dist = std::fabs(to - r.x);
        p.walks = p.dist >= kMinWalk;
        p.dir = p.walks ? sign(to - r.x) : r.facing;
    }
    if (!p.walks) to = r.x, p.dist = 0;
    p.to = to;
    p.walk = p.dist / speed;
    float x = to, dir = p.dir;
    // At a meal he faces the side with room for his bowl (walking in from
    // the end always does).
    if (sp == Special::Meal && !p.walks && std::fabs(x + dir * kBowlAt) > half - 10) dir = -dir;
    p.dir = dir;
    p.turn = dir != r.facing ? kTurn : 0;
    p.at = {half, room, dest == CatPlace::Bottom ? w.ledge : 0};

    // What he does there.
    auto fits = [&](CatAction a) {
        bool lands = room > 0 && std::fabs(x + dir * kPounce) <= room;
        switch (a) {
        case CatAction::Pounce: return lands;
        case CatAction::Drink:
        case CatAction::Eat: return std::fabs(x + dir * kBowlAt) <= half - 10;
        case CatAction::Hunt: return lands && std::fabs(x + dir * (kToyAt + kPounce + 2 * kBatBy)) <= half - 11;
        case CatAction::Zoomies: return room >= 70;
        case CatAction::Box: return std::fabs(x + dir * (kBoxAt + kBoxOut)) <= room;
        case CatAction::Perch:
        case CatAction::KnockOff:
            return dest == CatPlace::Bottom && w.ledge >= 50 && w.ledge <= 240 && std::fabs(x + dir * (kUpBy + kDownBy)) <= room;
        default: return true;
        }
    };
    auto put = [&](CatAction a, float len = 0) {
        Doing& d = p.doings[p.count++];
        d.a = a, d.x0 = x, d.len = len;
        if (a == CatAction::Pounce || a == CatAction::Hunt) x += dir * kPounce;
        if (a == CatAction::Box) x += dir * (kBoxAt + kBoxOut);
        if (a == CatAction::Perch || a == CatAction::KnockOff) x += dir * (kUpBy + kDownBy);
    };
    if (sp == Special::Meal) {
        float before = (p.wake ? kWakeAll : 0) + p.turn + p.walk + kPause;
        put(CatAction::Wait, std::max(3.0f, wait - before));
        put(CatAction::Eat);
        put(CatAction::Groom);
    } else if (sp == Special::None) {
        static const CatAction calm[] = {CatAction::Groom, CatAction::Scratch, CatAction::Blink, CatAction::Drink};
        static const CatAction all[] = {CatAction::Groom,   CatAction::Scratch,  CatAction::Blink,  CatAction::Pounce,
                                        CatAction::Drink,   CatAction::Hunt,     CatAction::Zoomies, CatAction::Box,
                                        CatAction::Perch,   CatAction::KnockOff, CatAction::Zoomies};
        const CatAction* list = night ? calm : all;
        int n = night ? 4 : int(sizeof(all) / sizeof(all[0])) - (lively ? 0 : 1);
        int count = night ? 1 : roll(h >> 8, lively ? 50 : 33) ? 2 : 1;
        for (int i = 0; i < count; i++) {
            // Evenly among what fits here, and not the same thing twice.
            CatAction fit[16];
            int m = 0;
            for (int j = 0; j < n; j++)
                if (fits(list[j]) && !(p.count > 0 && p.doings[0].a == list[j])) fit[m++] = list[j];
            CatAction a = m > 0 ? fit[(i == 0 ? h : h2 >> 3) % uint32_t(m)] : CatAction::Groom;
            put(a);
        }
        p.knead = (h >> 12) % 2 == 0;
        p.stir = (h >> 16) % 5 < 2;
        p.stirAt = 15 + float((h >> 20) % 40);
    }

    // How he lies down at the end: belly up only when it's warm.
    int b = int(h3 >> 7) % 100;
    p.bed = b < 40 ? CatBed::Curl : b < 70 ? CatBed::Loaf : CatBed::Side;
    if (w.warm && b >= 75) p.bed = CatBed::Belly;

    float active = (p.wake ? kWakeAll : 0) + (p.lash ? kLash : 0) + p.turn + p.walk + kPause + kLieDown + (p.knead ? kKnead : 0);
    for (int i = 0; i < p.count; i++) active += lengthOf(p.doings[i], dir, p.at);
    if (sp == Special::None && !r.awake) {
        float base = night ? 180.0f : lively ? 70.0f : 110.0f;
        if (w.rain) base *= 1.4f;
        p.sleep = std::max(20.0f, base - active);
        // At the bottom, a nap that rounds his visit off at 2 to 3
        // minutes, counting what he's done down there and his walk out.
        if (r.place == CatPlace::Bottom) {
            float stay = 120 + float(h2 % 61);
            float out = (p.cross ? (p.exitX - r.x) / speed : 0) + kWakeAll + p.turn;
            p.sleep = std::max(10.0f, stay - r.down - out);
        }
    }
    p.length = p.sleep + active;
    // Arriving at the bottom, how long he's been there by the end.
    float down = 0;
    if (dest == CatPlace::Bottom) {
        down = active - (p.wake ? kWakeAll : 0) - (p.lash ? kLash : 0) - p.turn - p.walk;
        down += p.cross ? (to - p.enterX) / speed : p.walk + p.sleep + r.down;
    }
    p.end = {x, dir, dest, p.bed, false, down, r.place == CatPlace::Bottom && dest == CatPlace::Top};
    return p;
}

// play is him partway through plan p, u seconds in.
Cat play(const Plan& p, float u, double t) {
    Cat c;
    c.x = p.from.x, c.facing = p.from.facing, c.place = p.from.place, c.bed = p.from.bed;
    c.asleep = p.from.awake ? 0.0f : 1.0f;
    const CatPose& bed = bedPose(p.from.bed);
    Timeline tl{u};
    float f = 0;
    if (tl.take(p.sleep, f)) {
        c.pose = bed, c.asleep = 1;
        // Now and then he stirs: lifts his head, looks about drowsily and
        // settles again.
        float since = f * p.sleep - p.stirAt;
        float len = kStirUp + kStirLook + kStirDown;
        if (p.stir && since >= 0 && since < len) {
            c.action = CatAction::Stir;
            CatPose up = headUp(p.from.bed);
            if (since < kStirUp) {
                float e = smooth(since / kStirUp);
                c.pose = blend(bed, up, e), c.asleep = 1 - 0.6f * e;
            } else if (since < kStirUp + kStirLook) {
                c.pose = up, c.asleep = 0.4f;
                c.pose.head.x += std::sin((since - kStirUp) / kStirLook * 2 * kPi);
            } else {
                float e = smooth((since - kStirUp - kStirLook) / kStirDown);
                c.pose = blend(up, bed, e), c.asleep = 0.4f + 0.6f * e;
            }
        }
        return c;
    }
    c.asleep = 0;
    if (p.wake && tl.take(kWakeAll, f)) {
        Timeline w{f * kWakeAll};
        c.action = CatAction::Wake;
        if (w.take(kWake, f)) return c.pose = blend(bed, kStand, smooth(f)), c.asleep = 1 - smooth(f * 2), c;
        c.action = CatAction::Stretch;
        if (w.take(kToBow, f)) return c.pose = blend(kStand, kBow, smooth(f)), c;
        if (w.take(kBowHold, f)) return c.pose = kBow, c;
        if (w.take(kToArch, f)) return c.pose = blend(kBow, kArch, smooth(f)), c;
        if (w.take(kArchHold, f)) return c.pose = kArch, c;
        w.take(kToStand, f);
        return c.pose = blend(kArch, kStand, smooth(f)), c;
    }
    if (p.lash) {
        Doing d{CatAction::Lash, c.x, 0};
        c.action = CatAction::Lash;
        if (doing(d, tl, t, c.facing, p.at, c)) return c;
    }
    if (tl.take(p.turn, f)) {
        // Turning round, towards the viewer: see DrawCat.
        c.action = CatAction::Turn;
        c.pose = kStand, c.turn = f;
        walking(c.pose, 2 * kPi * f, t);  // stepping round
        return c;
    }
    if (tl.take(p.walk, f)) {
        c.action = CatAction::Walk;
        c.pose = kStand, c.facing = p.dir;
        float gone = p.dist * f;
        if (!p.cross) {
            c.x = p.from.x + p.dir * gone;
        } else if (float out = p.exitX - p.from.x; gone < out) {
            c.x = p.from.x + gone;
        } else {
            c.place = p.toPlace;
            c.x = p.enterX + (gone - out);
        }
        walking(c.pose, 2 * kPi * gone / kStride, t);
        return c;
    }
    c.x = p.to, c.facing = p.dir, c.place = p.toPlace;
    c.pose = kStand;
    if (tl.take(kPause, f)) return c.action = CatAction::Stand, c;
    for (int i = 0; i < p.count; i++) {
        c.action = p.doings[i].a;
        if (doing(p.doings[i], tl, t, p.dir, p.at, c)) return c;
        c.lift = 0, c.ground = 0, c.turn = 0, c.look = 0, c.inBox = false, c.facing = p.dir;
    }
    c.x = p.end.x;
    c.pose = kStand;
    if (p.knead && tl.take(kKnead, f)) {
        // Making biscuits: front paws pressing in turn, eyes half shut.
        c.action = CatAction::Knead;
        float w = std::sin(2 * kPi * 1.6f * float(t));
        c.pose.paw[0].y -= std::max(0.0f, w) * 3, c.pose.joint[0].y -= std::max(0.0f, w) * 1.5f;
        c.pose.paw[1].y -= std::max(0.0f, -w) * 3, c.pose.joint[1].y -= std::max(0.0f, -w) * 1.5f;
        c.pose.chest.y += 0.4f * std::fabs(w);
        c.pose.eyes = 0.35f;
        return c;
    }
    c.action = CatAction::LieDown;
    c.bed = p.bed;
    f = std::min(tl.at / kLieDown, 1.0f);
    c.pose = blend(kStand, bedPose(p.bed), smooth(f)), c.asleep = smooth(f * 1.5f - 0.5f);
    return c;
}

// interruptible is whether he can be called away from what he's doing
// without it looking wrong: not mid-air, in a box, or out of sight.
bool interruptible(const Cat& c) {
    switch (c.action) {
    case CatAction::Pounce:
    case CatAction::Hunt:
    case CatAction::Zoomies:
    case CatAction::Box:
    case CatAction::Perch:
    case CatAction::KnockOff:
    case CatAction::Turn:
    case CatAction::Walk:
    case CatAction::Lash:
    case CatAction::Wait:
    case CatAction::Eat: return false;
    default: return c.lift == 0;
    }
}

// pettable is whether a click strokes him: when he's still, on the floor.
bool pettable(const Cat& c) {
    switch (c.action) {
    case CatAction::Sleep:
    case CatAction::Stir:
    case CatAction::Stand:
    case CatAction::Groom:
    case CatAction::Scratch:
    case CatAction::Blink:
    case CatAction::Knead:
    case CatAction::Wait: return c.lift == 0;
    default: return false;
    }
}

constexpr float kMeals[2] = {7.5f * 3600, 18 * 3600};  // breakfast and dinner, local time
constexpr float kMealEarly = 300;                      // he's waiting this long before
constexpr float kFade = 0.5f;                           // seconds blending into a new plan

}  // namespace

struct Mag::State {
    bool started = false;
    double lastT = 0;
    long long k = 0;
    float u = 0;
    Plan plan;
    Cat last;
    Cat fadeFrom;
    float fade = 0;
    int mealDay[2] = {-1, -1};
    float mealWait = -100;  // a meal's due: seconds till it comes, when he can go
    double petUntil = -100;
    std::deque<double> pets;
    double perkAt = -100, noticeTill = -100, noticeAt = -100, twitchAt = -100;
    bool hovered = false;
};

Mag::Mag() : s_(std::make_unique<State>()) {}
Mag::~Mag() = default;

Cat Mag::Step(double t, const CatWorld& w, const CatInput& in) {
    State& s = *s_;
    float dt = s.started ? std::clamp(float(t - s.lastT), 0.0f, 0.25f) : 0;
    s.lastT = t;

    auto restart = [&](Special sp, float wait) {
        Rest r{s.last.x, s.last.facing, s.last.place, s.last.bed, s.last.asleep < 0.5f};
        if (sp == Special::Hurry) {
            r = {-(w.half + kOff), 1, CatPlace::Top, s.last.bed, true};
            s.fade = 0;
        } else {
            s.fadeFrom = s.last;
            s.fade = kFade;
        }
        s.k++;
        s.plan = makePlan(s.k, r, w, sp, wait);
        s.u = 0;
    };

    if (!s.started) {
        s.started = true;
        Rest r{spot(-1) * roomOf(CatPlace::Top, w), 1};
        s.plan = makePlan(0, r, w, Special::None, 0);
        s.u = s.plan.sleep * 0.75f;  // some way in, so he's soon up for the first time
        s.last = play(s.plan, s.u, t);
    }

    // The space at the bottom's gone (a panel's opened): he's out of there
    // and back in at the top.
    if (s.last.place == CatPlace::Bottom && !gapOpen(w)) restart(Special::Hurry, 0);

    // Mealtimes: a few minutes before, he's waiting.
    for (int i = 0; i < 2; i++) {
        if (w.clock >= kMeals[i] - kMealEarly && w.clock < kMeals[i] + 120 && s.mealDay[i] != w.day) {
            s.mealDay[i] = w.day;
            s.mealWait = kMeals[i] - w.clock;
        }
    }
    if (s.mealWait > -60 && interruptible(s.last)) {
        restart(Special::Meal, std::max(0.0f, s.mealWait));
        s.mealWait = -100;
    }
    if (s.mealWait > -100) s.mealWait -= dt;

    // Stroked: he purrs, and his plan waits. Too much of it and he's off.
    bool petted = t < s.petUntil;
    if (in.click && pettable(s.last)) {
        s.pets.push_back(t);
        while (!s.pets.empty() && s.pets.front() < t - 10) s.pets.pop_front();
        if (s.pets.size() >= 4) {
            s.pets.clear();
            s.petUntil = -100;
            restart(Special::Annoyed, 0);
        } else {
            s.petUntil = t + 2.5;
            petted = true;
        }
    }
    if (!petted) s.u += dt;
    while (s.u >= s.plan.length) {
        s.u -= s.plan.length;
        s.k++;
        s.plan = makePlan(s.k, s.plan.end, w, Special::None, 0);
    }

    Cat c = play(s.plan, s.u, t);
    if (s.fade > 0) {
        float k = smooth(1 - s.fade / kFade);
        const Cat& a = s.fadeFrom;
        c.pose = blend(a.pose, c.pose, k);
        c.x = lerpf(a.x, c.x, k), c.lift = lerpf(a.lift, c.lift, k), c.look = lerpf(a.look, c.look, k);
        c.asleep = lerpf(a.asleep, c.asleep, k);
        s.fade -= dt;
    }

    // Something new in Recent: asleep, his head comes up a moment; awake,
    // an ear flicks.
    if (in.event) s.perkAt = t;
    float since = float(t - s.perkAt);
    if (since < 4) {
        if (c.action == CatAction::Sleep) {
            float e = since < 0.4f ? smooth(since / 0.4f) : since < 3 ? 1 : 1 - smooth((since - 3) / 1);
            c.pose = blend(c.pose, headUp(c.bed), e);
            c.pose.eyes = 0.7f * e;
            c.pose.ear = since > 0.6f && since < 1.2f ? std::sin((since - 0.6f) / 0.6f * 2 * kPi) : 0;
            c.asleep = std::min(c.asleep, 1 - 0.6f * e);
        } else if (since < 0.6f) {
            c.pose.ear = std::sin(since / 0.6f * 2 * kPi);
        }
    }

    // The mouse over him: now and then he notices. Awake, he watches it;
    // asleep, an ear twitches and his tail swishes.
    if (in.hover && !s.hovered && t > s.noticeAt + 20) {
        s.noticeAt = t;
        if (hash(uint32_t(t * 1000)) % 100 < 35) {
            if (c.asleep >= 1) s.twitchAt = t;
            else s.noticeTill = t + 5;
        }
    }
    s.hovered = in.hover;
    if (!in.hover) s.noticeTill = std::min(s.noticeTill, t);
    if (float tw = float(t - s.twitchAt); tw < 1.2f) {
        c.pose.ear = std::sin(tw / 0.4f * 2 * kPi) * (1 - tw / 1.2f);
        for (int i = 4; i < CatPose::kTailPoints; i++) c.pose.tail[i].y -= std::sin(tw / 1.2f * 2 * kPi) * (i - 3) * 1.2f;
    }
    if (float left = float(s.noticeTill - t); left > -0.6f && c.asleep < 0.5f && c.turn == 0) {
        float e = left > 0 ? smooth(std::min(1.0f, (5 - left) / 0.4f)) : smooth(1 + left / 0.6f);
        float hx = c.x + c.facing * c.pose.head.x, hy = c.pose.head.y - c.lift;
        float dx = (in.mouse.x - hx) * c.facing, dy = in.mouse.y - hy;
        if (dx > 0) {
            float a = std::clamp(std::atan2(dy, dx), -0.6f, 0.6f);
            c.pose.headTilt = lerpf(c.pose.headTilt, a * 0.8f, e);
        } else {
            c.look = std::max(c.look, 0.8f * e);  // behind him: he looks round
        }
        c.pose.eyes = std::max(c.pose.eyes, e);
        c.pose.ear = lerpf(c.pose.ear, 0.6f, e);
    }

    // Stroking: purring, eyes narrowed, chin up into it.
    float pet = float(s.petUntil - t);
    float pk = pet > 0 ? smooth(std::min(1.0f, (2.5f - pet) / 0.3f)) : smooth(1 + pet / 0.5f);
    if (pk > 0) {
        c.purr = pk;
        if (c.asleep < 1) {
            c.pose.eyes = lerpf(c.pose.eyes, 0.25f, pk);
            c.pose.headTilt -= 0.2f * pk;
            c.pose.head.x += 0.8f * pk;
        }
    }

    // A blink every few seconds while he's awake.
    if (c.asleep < 1 && std::fmod(t, 4.1) < 0.14) c.pose.eyes = 0;
    breathing(c.pose, t, c.asleep);
    s.last = c;
    return c;
}

namespace {

constexpr Rgba kFur{17, 17, 21}, kRim{92, 98, 112, 170}, kEye{92, 168, 245}, kPupil{8, 8, 10},
    kLid{120, 124, 136, 220}, kMouth{176, 84, 100}, kTongue{222, 112, 132}, kWhisker{150, 154, 166, 110}, kZ{150, 156, 172},
    kShadow{0, 0, 0, 55};

// Painter maps his pose onto the canvas, mirrored for which way he
// faces, and turned round about the vertical: pose x is how far forward a
// point is, and l how far to his side nearer the viewer. While he turns
// round, his head leads and his hips follow, so each point turns by an
// angle between theirs, from how far forward it is.
struct Painter {
    Canvas& c;
    Vec2f floor;
    float x, facing, size;
    float headTurn = 0, hipsTurn = 0;  // radians, 0 to pi
    float lift = 0;                     // units off the floor
    // His head can also turn by itself, about its own middle, to look at
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
    // onHead is this painter drawing his head.
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

// silhouette draws his body, legs, tail and head in one colour, grown by
// g units all round: once wide for his rim, then in black over it.
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
        // The near ear flicks about its base.
        float flick = l > 0 ? p.ear * 0.5f : 0;
        auto ear = [&](Vec2f v) { return head(add(Vec2f{e, -5}, rotate(sub(v, Vec2f{e, -5}), flick))); };
        Vec2f base1 = ear({e - 3.6f, -5}), base2 = ear({e + 3.6f, -5}), tip = ear({e + 1.2f, -13.5f});
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
    // Both eyes, the far one hidden behind his head unless he's turned
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
    // Whiskers both sides, fanning out, the far ones only once he's
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

// zs float up from his head while he's asleep: a small z, rising, growing
// and fading, one after another.
void zs(const Painter& d, const Cat& cat, double t) {
    if (cat.asleep < 0.5f) return;  // not once his head is up
    for (int i = 0; i < 3; i++) {
        float k = float(std::fmod(t / 4.5 + i / 3.0, 1.0));
        float s = 2.2f + 2.6f * k;
        float rise = std::min(30.0f, 46 + cat.pose.head.y);  // lower from a loaf's raised head
        Vec2f at = add(cat.pose.head, {4 + 9 * k, -14 - rise * k});
        Rgba col = kZ;
        col.a = uint8_t(255 * cat.asleep * std::min(k * 5, 1.0f) * (1 - k));
        // Z's read left to right whichever way he faces.
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

// drawBowl draws a dish of milk on the floor, its rim over his chin when
// he's drinking from it.
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
    // The milk, lower and narrower as he drinks it.
    if (b.fill > 0.05f) {
        float sink = (1 - b.fill) * 1.1f;
        c.Ellipse(at(0, -4.5f + sink * 0.5f), (6.8f - sink) * size, (1.1f - sink * 0.3f) * size, 0, faded(Rgba{250, 250, 244}, a));
    }
}

// drawToy draws a toy mouse, facing away from his (dir), tipped by rot.
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

// drawFood draws his dinner: a dish with kibble heaped in it, fewer as he
// eats.
static void drawFood(Canvas& c, const CatProp& b, Vec2f floor, float size) {
    if (b.alpha <= 0) return;
    float a = b.alpha;
    auto at = [&](float x, float y) { return Vec2f{floor.x + (b.x + x) * size, floor.y + y * size}; };
    c.Ellipse(at(0, 0.6f), 9 * size, 1.2f * size, 0, faded(kShadow, a));
    int bits = int(std::round(9 * b.fill));
    static const float kib[9][2] = {{-4, -5.2f}, {0, -5.4f}, {4, -5.1f}, {-2, -6.3f}, {2, -6.4f}, {-5.5f, -4.8f},
                                    {5.5f, -4.8f}, {0, -7.2f}, {-1, -4.9f}};
    for (int i = 0; i < bits; i++)
        c.Circle(at(kib[i][0], kib[i][1]), 1.3f * size, faded(i % 2 ? Rgba{150, 98, 52} : Rgba{124, 78, 40}, a));
    Vec2f dish[4] = {at(-8, -4.5f), at(8, -4.5f), at(6, 0), at(-6, 0)};
    c.Polygon(dish, 4, faded(Rgba{206, 110, 96}, a));
    Vec2f band[4] = {at(-7.4f, -3.2f), at(7.4f, -3.2f), at(7.1f, -2.2f), at(-7.1f, -2.2f)};
    c.Polygon(band, 4, faded(Rgba{240, 214, 190}, a));
}

// drawBox draws a cardboard box: all of it (behind him), or just its
// back or front (around him, sat in it).
enum class BoxPart { All, Back, Front };
static void drawBox(Canvas& c, const CatProp& b, Vec2f floor, float size, BoxPart part) {
    if (b.alpha <= 0) return;
    float a = b.alpha;
    auto at = [&](float x, float y) { return Vec2f{floor.x + (b.x + x) * size, floor.y + y * size}; };
    auto quad = [&](Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3, Rgba col) {
        Vec2f q[4] = {at(p0.x, p0.y), at(p1.x, p1.y), at(p2.x, p2.y), at(p3.x, p3.y)};
        c.Polygon(q, 4, faded(col, a));
    };
    if (part != BoxPart::Front) {
        c.Ellipse(at(0, 0.6f), 23 * size, 1.4f * size, 0, faded(kShadow, a));
        quad({-18, -21}, {18, -21}, {19, -17}, {-19, -17}, Rgba{92, 66, 40});  // the far flap
        quad({-20, -17}, {20, -17}, {20, 0}, {-20, 0}, Rgba{104, 76, 46});    // inside
    }
    if (part != BoxPart::Back) {
        quad({-20, -15}, {20, -15}, {20, 0.5f}, {-20, 0.5f}, Rgba{172, 128, 80});
        quad({-20, -15}, {-20, -12.5f}, {-27, -19}, {-25.5f, -21}, Rgba{152, 110, 66});  // side flaps, folded out
        quad({20, -15}, {25.5f, -21}, {27, -19}, {20, -12.5f}, Rgba{152, 110, 66});
        quad({-20, -15}, {20, -15}, {20, -14}, {-20, -14}, Rgba{196, 152, 100});         // the rim
        quad({-1.6f, -15}, {1.6f, -15}, {1.6f, 0.5f}, {-1.6f, 0.5f}, Rgba{206, 178, 128, 200});  // tape
    }
}

// drawMug draws a mug standing on its base, or tipping over by rot about
// the corner on the dir side, its handle towards him.
static void drawMug(Canvas& c, const CatProp& m, float dir, Vec2f floor, float size) {
    if (m.alpha <= 0) return;
    float a = m.alpha;
    auto at = [&](float x, float y) {
        Vec2f r = add(rotate({(x - 3.4f) * dir, y}, m.rot), {3.4f * dir, 0});
        return Vec2f{floor.x + (m.x + r.x) * size, floor.y + (r.y - m.lift) * size};
    };
    if (m.lift < 1) c.Ellipse({floor.x + m.x * size, floor.y + 0.5f * size}, 5 * size, 0.8f * size, 0, faded(kShadow, a));
    Rgba glaze = faded(Rgba{214, 98, 88}, a), light = faded(Rgba{238, 150, 136}, a);
    c.Line(at(-3.2f, -6.6f), at(-5.6f, -6), 1.2f * size, glaze);
    c.Line(at(-5.6f, -6), at(-5.6f, -2.6f), 1.2f * size, glaze);
    c.Line(at(-5.6f, -2.6f), at(-3.2f, -2), 1.2f * size, glaze);
    Vec2f body[4] = {at(-3.4f, -8), at(3.4f, -8), at(3.4f, 0), at(-3.4f, 0)};
    c.Polygon(body, 4, glaze);
    Vec2f shine[4] = {at(1.6f, -7), at(2.6f, -7), at(2.6f, -1), at(1.6f, -1)};
    c.Polygon(shine, 4, light);
    c.Ellipse(at(0, -8), 3.4f * size, 0.8f * size, m.rot, faded(Rgba{120, 52, 44}, a));
}

// purring draws little marks either side of his throat while he's
// stroked, rippling out.
static void purring(const Painter& d, const Cat& cat, double t) {
    if (cat.purr <= 0.05f) return;
    Vec2f throat = lerp(cat.pose.chest, cat.pose.head, 0.5f);
    for (int i = 0; i < 2; i++) {
        float k = float(std::fmod(t * 2.2 + i * 0.5, 1.0));
        Rgba col = kZ;
        col.a = uint8_t(200 * cat.purr * (1 - k));
        for (float side : {-1.0f, 1.0f}) {
            Vec2f o = add(throat, {side * (11 + 4 * k), -4});
            d.line(add(o, {0, -2.5f}), add(o, {side * 1.2f, 0}), 0.6f, col);
            d.line(add(o, {side * 1.2f, 0}), add(o, {0, 2.5f}), 0.6f, col);
        }
    }
}

// The finer styles. Each draws the same cat from the same pose, with a
// smooth outline in place of circles and quads, tapered legs with paws, a
// shaped head and ears, and more to his face. Their colours are opaque, so
// overlapping shapes don't show in his rim.
namespace {

// Look is what sets one finer style apart from the others.
struct Look {
    enum Eyes { Oval, Almond, Round, Narrow };
    Rgba rim{70, 75, 86};      // his rim light
    Rgba under{70, 75, 86};    // the rim where light from above doesn't reach
    Rgba halo{0, 0, 0, 0};     // a wider, fainter rim outside it, if any
    Rgba band{17, 17, 21};     // just inside each part's edge: fur colour for none
    Rgba fur{17, 17, 21};
    Rgba farFur{17, 17, 21};   // his far legs
    Rgba earIn{40, 35, 44};    // inside his ears
    float grow = 1.1f;         // how wide the rim is, in units
    bool topLit = false;       // the rim brightest along his top edges
    bool fluffy = false;       // tufts and a bushy tail
    bool toes = false;
    bool eyeGlow = false;      // a faint glow round his eyes
    Eyes eyes = Oval;
    // His eyes' colours: the iris, lighter below, and the glint, with its
    // size in units.
    Rgba eye{92, 168, 245}, iris{132, 198, 255}, glint{238, 246, 255, 235};
    float glintR = 0.42f;
};

Look lookOf(CatStyle style) {
    Look k;
    switch (style) {
    case CatStyle::Moonlit:
        k.rim = {104, 112, 130}, k.under = {46, 49, 58}, k.band = {36, 38, 46};
        k.farFur = {21, 22, 27}, k.earIn = {52, 44, 54};
        k.grow = 1.0f, k.topLit = true, k.toes = true, k.eyes = Look::Almond;
        break;
    case CatStyle::Fluffy:
        k.halo = {44, 47, 55}, k.rim = {76, 81, 93}, k.band = {30, 32, 38}, k.earIn = {50, 42, 52};
        k.grow = 0.8f, k.fluffy = true, k.eyes = Look::Round;
        break;
    case CatStyle::Backlit:
        k.rim = {78, 104, 148}, k.halo = {36, 44, 62};
        k.grow = 0.9f, k.eyeGlow = true, k.eyes = Look::Almond;
        break;
    case CatStyle::Sleek:
        // Calm eyes: a narrower almond in a muted steel blue, a thin slit
        // and a small, dim glint.
        k.eyes = Look::Narrow;
        k.eye = {74, 128, 172}, k.iris = {98, 148, 186}, k.glint = {214, 226, 240, 150}, k.glintR = 0.3f;
        break;
    default: break;
    }
    return k;
}

// along is the point u of the way along a smooth curve through n points,
// with u counted in points: 0 at the first, n - 1 at the last.
Vec2f along(const Vec2f* p, int n, float u) {
    int i = std::clamp(int(u), 0, n - 2);
    float t = u - float(i), t2 = t * t, t3 = t2 * t;
    Vec2f a = p[std::max(i - 1, 0)], b = p[i], c = p[i + 1], d = p[std::min(i + 2, n - 1)];
    auto mix = [&](float a0, float b0, float c0, float d0) {
        return 0.5f * (2 * b0 + (c0 - a0) * t + (2 * a0 - 5 * b0 + 4 * c0 - d0) * t2 + (3 * b0 - a0 - 3 * c0 + d0) * t3);
    };
    return {mix(a.x, b.x, c.x, d.x), mix(a.y, b.y, c.y, d.y)};
}

// Brush draws his parts in one colour, each grown by g units all round
// (or shrunk, below 0). Shapes are placed in pose units but worked out on
// the canvas, so they keep their thickness however he's turned.
struct Brush {
    Painter d;
    float g;
    Rgba col;

    // tube is a run of circles, each joined to the next along the lines
    // touching both: a smooth, tapering limb.
    void tube(const Vec2f* pts, const float* r, int n, float l = 0) const {
        Vec2f a;
        float ra = 0;
        for (int i = 0; i < n; i++) {
            Vec2f b = d.m(pts[i], l);
            float rb = d.w(std::max(0.25f, r[i] + g));
            d.c.Circle(b, rb, col);
            if (i > 0) {
                Vec2f u = sub(b, a);
                float len = std::hypot(u.x, u.y), s = len > 0 ? (ra - rb) / len : 1;
                if (std::fabs(s) < 1) {  // otherwise one circle is inside the other
                    u = scale(u, 1 / len);
                    float c = std::sqrt(1 - s * s);
                    Vec2f n1 = {u.x * s - u.y * c, u.y * s + u.x * c}, n2 = {u.x * s + u.y * c, u.y * s - u.x * c};
                    // Clockwise on the canvas, as ImGui's smooth edges want.
                    Vec2f quad[4] = {add(a, scale(n2, ra)), add(b, scale(n2, rb)), add(b, scale(n1, rb)), add(a, scale(n1, ra))};
                    d.c.Polygon(quad, 4, col);
                }
            }
            a = b, ra = rb;
        }
    }
    void blob(Vec2f at, float r, float l = 0) const { tube(&at, &r, 1, l); }

    // shape is a convex polygon, each point with its own l, its edges moved
    // out by g. Seen edge on it's left out.
    void shape(const Vec2f* pts, const float* l, int n) const {
        Vec2f p[8], out[8];
        float area = 0;
        for (int i = 0; i < n; i++) p[i] = d.m(pts[i], l[i]);
        for (int i = 0; i < n; i++) area += p[i].x * p[(i + 1) % n].y - p[(i + 1) % n].x * p[i].y;
        if (std::fabs(area) < d.w(1) * d.w(1)) return;
        float turn = area > 0 ? 1.0f : -1.0f, by = d.w(g);
        for (int i = 0; i < n; i++) {
            auto normal = [&](Vec2f from, Vec2f to) {
                Vec2f e = sub(to, from);
                float len = std::max(0.001f, std::hypot(e.x, e.y));
                return Vec2f{e.y / len * turn, -e.x / len * turn};
            };
            Vec2f n1 = normal(p[(i + n - 1) % n], p[i]), n2 = normal(p[i], p[(i + 1) % n]);
            Vec2f mid = add(n1, n2);
            float len = std::max(0.001f, std::hypot(mid.x, mid.y));
            mid = scale(mid, 1 / len);
            // Sharp corners would grow into long spikes: hold them back.
            out[i] = add(p[i], scale(mid, by / std::max(0.55f, mid.x * n1.x + mid.y * n1.y)));
        }
        // Clockwise on the canvas, as ImGui's smooth edges want: mirrored,
        // he's drawn the other way round.
        if (turn < 0) std::reverse(out, out + n);
        d.c.Polygon(out, n, col);
    }
    void shape(const Vec2f* pts, float l, int n) const {
        float ls[8];
        std::fill(ls, ls + n, l);
        shape(pts, ls, n);
    }

    // tuft is a lock of fur on the circle at c: its root across the angle
    // at, its tip len further out and leaning by lean. Angles are radians
    // from ahead (a unit vector) round towards belly. It only reaches
    // below the floor where the circle does.
    void tuft(Vec2f c, float r, Vec2f ahead, Vec2f belly, float at, float len, float lean) const {
        auto on = [&](float a, float out) {
            return add(c, add(scale(ahead, std::cos(a) * out), scale(belly, std::sin(a) * out)));
        };
        Vec2f pts[3] = {on(at - 0.36f, r - 0.8f), on(at + lean, r + len), on(at + 0.36f, r - 0.8f)};
        pts[1].y = std::min(pts[1].y, std::max(0.5f, c.y + r - 0.8f));
        shape(pts, 0.0f, 3);
    }
};

// His parts, back to front.
enum : unsigned { kFarLegs = 1, kTorso = 2, kHind = 4, kFront = 8, kTail = 16, kHead = 32, kWhole = 63 };

// leg draws leg i from inside his body out to its toes: front legs taper
// from the shoulder, back legs from the haunch with a hock bent back,
// towards his tail, whichever way the leg points.
void leg(const Brush& b, const CatPose& p, const Look& k, int i) {
    bool hind = i >= 2;
    float l = (i % 2 == 0 ? 1.0f : -1.0f) * (hind ? 4.0f : 3.5f);
    Vec2f joint = p.joint[i], paw = p.paw[i];
    Vec2f dir = sub(paw, joint);
    float len = std::max(0.001f, std::hypot(dir.x, dir.y));
    dir = scale(dir, 1 / len);
    Vec2f pts[5];
    float r[5];
    int n = 0;
    float fluff = k.fluffy ? 0.45f : 0;
    // A back leg folded under him is short: its knee is rounder, and it
    // shows no hock.
    float straight = hind ? std::clamp((len - 6) / 8, 0.0f, 1.0f) : 1;
    pts[n] = lerp(hind ? p.hips : p.chest, joint, hind ? 0.2f : 0.3f), r[n++] = hind ? p.rHips * 0.68f : 5.0f;
    pts[n] = joint, r[n++] = (hind ? 3.3f + 1.6f * (1 - straight) : 2.9f) + fluff;
    if (hind) {
        Vec2f back = {-dir.y, dir.x}, rear = sub(p.hips, p.chest);
        if (back.x * rear.x + back.y * rear.y < 0) back = scale(back, -1);
        Vec2f hock = add(lerp(joint, paw, 0.62f), scale(back, 2.4f * straight));
        hock.y = std::min(hock.y, std::max(joint.y, paw.y));
        pts[n] = hock, r[n++] = 2.3f + fluff;
    }
    pts[n] = paw, r[n++] = 2.2f;
    // Toes point along the floor when the paw is down, else on from the leg.
    Vec2f toe = lerp(dir, {1, 0}, std::clamp(1 + paw.y / 3, 0.0f, 1.0f));
    toe = scale(toe, 1.8f / std::max(0.001f, std::hypot(toe.x, toe.y)));
    toe.y = std::min(toe.y, 0.2f);
    pts[n] = add(paw, toe), r[n++] = 1.9f;
    b.tube(pts, r, n, l);
}

// nearLeg reports whether leg i is on the side of him towards the viewer,
// which swaps as he turns round.
bool nearLeg(const Painter& d, const CatPose& p, int i) { return (i % 2 == 0) == (std::cos(d.angle(p.joint[i].x)) >= 0); }

// pawRaised is his near front paw up by his face, to be drawn over it.
bool pawRaised(const CatPose& p) { return p.paw[0].y < p.head.y + 9; }

void tailShape(const Brush& b, const CatPose& p, const Look& k) {
    constexpr int kPer = 3, kCount = (CatPose::kTailPoints - 1) * kPer + 2;
    Vec2f pts[kCount];
    float r[kCount];
    // It starts inside his hips, so it leaves them smoothly.
    pts[0] = lerp(p.hips, p.tail[0], 0.55f), r[0] = k.fluffy ? 4.2f : 3.6f;
    for (int i = 1; i < kCount; i++) {
        float u = float(i - 1) / float(kCount - 2);
        pts[i] = along(p.tail.data(), CatPose::kTailPoints, u * (CatPose::kTailPoints - 1));
        // Tapering, from a thicker root.
        float thin = lerpf(2.6f, 1.45f, std::pow(u, 0.8f));
        r[i] = thin + 1.1f * std::max(0.0f, 1 - u / 0.25f);
        if (k.fluffy) {
            // A plume: swelling past the base, then tapering to a point.
            r[i] = u < 0.4f ? lerpf(2.7f, 3.5f, smooth(u / 0.4f)) : lerpf(3.5f, 1.4f, smooth((u - 0.4f) / 0.6f));
            // Lying along the floor it rests on it, where a thin tail would.
            pts[i].y -= std::max(0.0f, r[i] - thin) * std::clamp(1 - std::fabs(pts[i].y) / 3, 0.0f, 1.0f);
        }
    }
    b.tube(pts, r, kCount);
}

void torsoShape(const Brush& b, const CatPose& p, const Look& k) {
    const Vec2f spine[3] = {p.hips, p.mid, p.chest};
    const float girth[3] = {p.rHips, p.rMid, p.rChest};
    constexpr int kPer = 4, kCount = 2 * kPer + 1;
    Vec2f pts[kCount];
    float r[kCount];
    for (int i = 0; i < kCount; i++) {
        float u = float(i) / kPer;
        int at = std::min(int(u), 1);
        pts[i] = along(spine, 3, u), r[i] = lerpf(girth[at], girth[at + 1], u - float(at));
    }
    b.tube(pts, r, kCount);
    // His neck, wide at the shoulders.
    Vec2f neck[3] = {lerp(p.chest, p.head, 0.22f), lerp(p.chest, p.head, 0.55f), lerp(p.chest, p.head, 0.82f)};
    float rn[3] = {p.rChest * 0.74f, 5.4f, 4.7f};
    b.tube(neck, rn, 3);
    if (k.fluffy) {
        // A bib on his chest, his belly, and his britches: placed from
        // which way his body runs and which side his legs are, so they
        // follow him onto his back.
        Vec2f ahead = sub(p.chest, p.hips);
        ahead = scale(ahead, 1 / std::max(0.001f, std::hypot(ahead.x, ahead.y)));
        Vec2f belly = {-ahead.y, ahead.x};
        Vec2f legs = sub(add(p.joint[0], p.joint[2]), add(p.chest, p.hips));
        if (belly.x * legs.x + belly.y * legs.y < 0) belly = scale(belly, -1);
        // The fur is fuller there too.
        Vec2f bib = add(p.chest, add(scale(ahead, 3.2f), scale(belly, 1.6f)));
        Vec2f seat = add(p.hips, add(scale(ahead, -2.5f), scale(belly, 2.5f)));
        float rb = p.rChest * 0.78f, rs = p.rHips * 0.74f;
        b.blob(bib, rb), b.blob(seat, rs);
        for (float a : {0.2f, 0.75f, 1.3f}) b.tuft(bib, rb, ahead, belly, a, 1.8f, 0.5f);
        b.tuft(p.mid, p.rMid, ahead, belly, 1.7f, 1.5f, 0.5f);
        for (float a : {2.1f, 2.7f}) b.tuft(seat, rs, ahead, belly, a, 1.8f, -0.5f);
    }
}

// The points of an ear seen from his side, about the middle of its base,
// and seen face on, across from the middle of his head: forward or
// across, then up.
const Vec2f kEarSide[5] = {{-3.2f, 1.4f}, {-2.7f, -4.6f}, {0.4f, -8.5f}, {1.5f, -8.3f}, {3.6f, 0.8f}};
const Vec2f kEarFront[5] = {{0.8f, -1.4f}, {3.9f, -8.5f}, {4.9f, -8.2f}, {6.5f, -4.2f}, {6.9f, 0.6f}};
constexpr float kEarAcross = 4.3f;  // how far across its tip is
constexpr float kEarBase = -4.8f;   // how far up his head its base is

// ear draws one ear as two blades sharing a tip: one seen from his side,
// one seen face on, so it has some width from wherever it's seen. inner
// shrinks them to the hollow inside the ear. swivel turns the ear about
// its base, in radians: forward above 0, back below.
void ear(const Brush& h, const CatPose& p, float side, bool inner, float swivel = 0) {
    float e = side < 0 ? -3.1f : 3.0f;  // the far ear sits further back
    // A point of the ear, from where it is over the base's middle.
    auto at = [&](Vec2f v) { return add(p.head, rotate(add(rotate(v, swivel), {e, kEarBase}), p.headTilt)); };
    auto hollow = [&](Vec2f v, Vec2f about) { return inner ? lerp(about, v, 0.55f) : v; };
    Vec2f pts[5];
    float ls[5];
    for (int i = 0; i < 5; i++) pts[i] = at(hollow(kEarSide[i], {0.9f, -2.6f})), ls[i] = side * kEarAcross;
    // From his side, only the near ear shows its hollow.
    if (!inner || side * std::cos(h.d.headAngle()) > 0) h.shape(pts, ls, 5);
    for (int i = 0; i < 5; i++) {
        Vec2f v = hollow(kEarFront[i], {4.2f, -2.8f});
        pts[i] = at({0.6f, v.y}), ls[i] = side * v.x;
    }
    h.shape(pts, ls, 5);
}

void headShape(const Brush& b, const CatPose& p, const Look& k) {
    Brush h{b.d.onHead(), b.g, b.col};
    auto at = [&](Vec2f v) { return add(p.head, rotate(v, p.headTilt)); };
    h.blob(p.head, p.rHead);
    // His muzzle runs smoothly out of his cheek.
    Vec2f muzzle[2] = {at({2.5f, 1.2f}), at({6.9f, 2.7f})};
    float rm[2] = {6.2f, 3.8f};
    h.tube(muzzle, rm, 2);
    for (float side : {-1.0f, 1.0f}) {
        ear(h, p, side, false, side > 0 ? p.ear * 0.5f : 0);  // the near ear flicks
        if (!k.fluffy) continue;
        // Ruffs on his cheeks, out to each side when he's face on.
        h.blob(at({-1.0f, 3.2f}), 6.3f, side * 3);
        Vec2f ruff[3] = {at({-4.8f, 3.6f}), at({-3.6f, 10.6f}), at({1.4f, 7.2f})};
        ruff[1].y = std::min(ruff[1].y, std::max(0.5f, p.head.y + p.rHead - 0.8f));
        float ls[3] = {side * 5, side * 8, side * 4};
        h.shape(ruff, ls, 3);
    }
}

// parts draws those of his parts in which.
void parts(const Brush& b, const CatPose& p, const Look& k, unsigned which) {
    for (int i = 0; i < 4; i++) {
        bool near = nearLeg(b.d, p, i);
        unsigned part = !near ? kFarLegs : i >= 2 ? kHind : kFront;
        if (which & part) leg(b, p, k, i);
    }
    if (which & kTorso) torsoShape(b, p, k);
    if (which & kTail) tailShape(b, p, k);
    if (which & kHead) headShape(b, p, k);
}

// shifted is a painter moved up the canvas by up units, and by ahead
// units the way he faces: less the more he's turned towards the viewer.
Painter shifted(Painter d, float up, float ahead = 0) {
    d.floor.y -= up * d.size;
    d.floor.x += ahead * d.facing * std::cos((d.headTurn + d.hipsTurn) / 2) * d.size;
    return d;
}

// eye draws an open eye at a point on the canvas: narrow is how much of
// its width shows (he may be side on), open how far its lids are apart,
// slant its tilt in radians.
void eye(const Painter& d, Vec2f at, float narrow, float open, float slant, const Look& k) {
    constexpr Rgba kSocket{8, 8, 10};
    float rx = 1.9f * narrow, ry = 2.4f * open, slit = 0.5f, tall = 0.84f, glint = k.glintR, socket = 0.4f;
    if (k.eyes == Look::Almond) rx = 2.3f * narrow, ry = 1.9f * open, tall = 0.92f;
    if (k.eyes == Look::Round) rx = 2.15f * narrow, ry = 2.3f * open, slit = 0.95f * narrow, tall = 0.78f, glint = 0.5f;
    if (k.eyes == Look::Narrow) rx = 2.15f * narrow, ry = 1.55f * open, slit = 0.32f, tall = 0.9f, socket = 0.3f, slant *= 1.4f;
    if (k.eyes != Look::Almond && k.eyes != Look::Narrow) slant = 0;
    if (k.eyeGlow) d.c.Ellipse(at, d.w(rx + 1.0f), d.w(ry + 1.0f), slant, Rgba{k.eye.r, k.eye.g, k.eye.b, 26});
    d.c.Ellipse(at, d.w(rx + socket), d.w(ry + socket), slant, kSocket);
    d.c.Ellipse(at, d.w(rx), d.w(ry), slant, k.eye);
    // Lighter towards the bottom, where light through the eye comes out.
    d.c.Ellipse({at.x, at.y + d.w(ry * 0.42f)}, d.w(rx * 0.72f), d.w(ry * 0.46f), slant, k.iris);
    d.c.Ellipse(at, d.w(slit), d.w(ry * tall), 0, kPupil);
    if (open > 0.55f) d.c.Circle({at.x - d.w(rx * 0.36f), at.y - d.w(ry * 0.42f)}, d.w(glint), k.glint);
}

void fineFace(const Painter& body, const CatPose& p, const Look& k) {
    constexpr Rgba kNose{92, 64, 76}, kLine{44, 46, 56};
    Painter d = body.onHead();
    auto head = [&](Vec2f v) { return add(p.head, rotate(v, p.headTilt)); };
    float facing = d.facing;
    // The hollows of his ears.
    Brush in{d, 0, k.earIn};
    for (float side : {-1.0f, 1.0f}) ear(in, p, side, true, side > 0 ? p.ear * 0.5f : 0);
    // Both eyes, the far one hidden behind his head unless he's turned
    // towards the viewer, and narrower the more side on each is.
    for (float l : {2.6f, -2.6f}) {
        Vec2f at = head({3.8f, -1.2f});
        float show = std::clamp((d.depth(at.x, l, p.head.x) + 0.5f) / 3, 0.0f, 1.0f);
        if (show <= 0) continue;
        if (p.eyes > 0.15f) {
            // Each slants down towards his nose.
            float slant = (0.2f * (l > 0 ? 1.0f : -1.0f) * p.eyes + p.headTilt) * facing;
            eye(d, d.m(at, l), 0.5f + 0.5f * show, p.eyes, slant, k);
        } else {
            // Closed: a sleepy curve.
            const Vec2f lid[4] = {{2.1f, -0.8f}, {3.1f, 0.05f}, {4.5f, 0.05f}, {5.5f, -0.8f}};
            for (int i = 0; i + 1 < 4; i++) d.line(head(lid[i]), head(lid[i + 1]), 0.75f, kLid, l);
            for (int i = 1; i < 3; i++) d.circle(head(lid[i]), 0.37f, kLid, l);
        }
    }
    if (p.yawn > 0.3f) d.c.Ellipse(d.m(head({8.5f, 4.6f})), d.w(1.4f), d.w(2.4f * p.yawn), 0, kMouth);
    if (p.tongue > 0.05f) d.c.Ellipse(d.m(head({10.2f, 4.4f})), d.w(1.1f), d.w(1.7f * p.tongue), 0, kTongue);
    // His nose, and the line of his mouth below it: its two sides part as
    // he turns to face the viewer.
    d.circle(head({10.1f, 1.5f}), 0.8f, kNose);
    if (p.yawn <= 0.3f) {
        d.line(head({10.0f, 2.2f}), head({9.4f, 3.6f}), 0.4f, kLine);
        for (float side : {1.0f, -1.0f}) d.c.Line(d.m(head({9.4f, 3.6f})), d.m(head({7.6f, 4.6f}), side * 1.6f), d.w(0.4f), kLine);
    }
    // Whiskers both sides, fanning out and drooping, the far ones only
    // once he's turned enough to show them.
    bool far = std::sin(d.headAngle()) > 0.25f;
    for (float side : {1.0f, -1.0f}) {
        if (side < 0 && !far) continue;
        for (float dy : {-0.9f, 0.3f, 1.5f}) {
            Vec2f a = d.m(head({8.6f, 2.6f + dy * 0.5f}), side * 1.5f), b = d.m(head({11.8f, 1.6f + dy * 1.3f}), side * 3.8f),
                  c = d.m(head({14.6f, 1.8f + dy * 2.2f}), side * 6);
            d.c.Line(a, b, d.w(0.42f), kWhisker);
            d.c.Line(b, c, d.w(0.3f), kWhisker);
        }
    }
    // Lines between his toes, on the paws that are down.
    if (k.toes) {
        for (int i = 0; i < 4; i++) {
            if (!nearLeg(body, p, i) || p.paw[i].y < -0.5f) continue;
            float l = (i % 2 == 0 ? 1.0f : -1.0f) * (i >= 2 ? 4.0f : 3.5f);
            for (float x : {1.3f, 2.4f}) body.line(add(p.paw[i], {x, 0.2f}), add(p.paw[i], {x + 0.5f, 1.6f}), 0.35f, kLine, l);
        }
    }
}

// drawFine draws him in one of the finer styles: his rim first, as his
// whole shape grown, then each part over the last, back to front.
void drawFine(const Painter& d, const CatPose& p, const Look& k) {
    if (k.halo.a > 0) parts(Brush{d, k.grow + 0.5f, k.halo}, p, k, kWhole);
    if (k.topLit) {
        // Light from above: a dim rim all round, and his shape again in
        // the bright rim, raised, so it shows along his top edges.
        parts(Brush{d, k.grow, k.under}, p, k, kWhole);
        parts(Brush{shifted(d, 0.85f), 0.25f, k.rim}, p, k, kWhole);
    } else {
        parts(Brush{d, k.grow, k.rim}, p, k, kWhole);
    }
    bool banded = k.band.r != k.fur.r || k.band.g != k.fur.g || k.band.b != k.fur.b;
    bool raised = pawRaised(p);
    auto layer = [&](unsigned which, Rgba fur) {
        if (!banded) return parts(Brush{d, 0, fur}, p, k, which);
        // A lighter band inside the part's edge picks it out from what's
        // behind: along the top of his body, tail and head, and round the
        // front of his legs, so a haunch shows as a curve, not a disc.
        bool legs = which & (kHind | kFront);
        parts(Brush{d, 0, k.band}, p, k, which);
        parts(Brush{shifted(d, legs ? -0.3f : -0.5f, legs ? -0.45f : 0.0f), -0.12f, fur}, p, k, which);
    };
    parts(Brush{d, 0, k.farFur}, p, k, kFarLegs);
    layer(kTorso, k.fur);
    layer(raised ? kHind : kHind | kFront, k.fur);
    layer(kTail, k.fur);
    layer(kHead, k.fur);
    if (raised) layer(kFront, k.fur);
    fineFace(d, p, k);
}

}  // namespace

void DrawCat(Canvas& c, const Cat& cat, double t, Vec2f floor, float size, CatStyle style) {
    // The toy goes under his paws; the bowl's rim over his chin; a box
    // behind him, or round him when he's in it.
    drawToy(c, cat.toy, cat.facing, floor, size);
    drawBox(c, cat.box, floor, size, cat.inBox ? BoxPart::Back : BoxPart::All);
    Painter d{c, floor, cat.x, cat.facing, size};
    // Turning round, his head comes round first, towards the viewer, and
    // his hips follow.
    d.headTurn = kPi * smooth(cat.turn * 1.4f);
    d.hipsTurn = kPi * smooth(cat.turn * 1.4f - 0.4f);
    float across = std::fabs(std::cos((d.headTurn + d.hipsTurn) / 2));
    // His shadow stays on the floor, smaller the higher he leaps.
    float high = 1 - std::clamp((cat.lift - cat.ground) / 30, 0.0f, 0.5f);
    float wide = 26 * std::max(0.55f, across) * high, deep = (2.6f + 0.8f * (1 - across)) * high;
    if (cat.inBox) {
    } else if (style == CatStyle::Classic) {
        d.c.Ellipse(d.m({0, 0.5f - cat.ground}), d.w(wide), d.w(deep), 0, kShadow);
    } else {
        // Softer at its edge: three fainter shadows, one inside the next.
        for (float k : {1.0f, 0.78f, 0.55f})
            d.c.Ellipse(d.m({0, 0.5f - cat.ground}), d.w(wide * k), d.w(deep * k), 0, Rgba{0, 0, 0, 22});
    }
    d.lift = cat.lift;
    // Looking at the viewer, his head comes most of the way round.
    d.pivot = cat.pose.head;
    d.look = cat.look * kPi * 0.42f;
    if (style == CatStyle::Classic) {
        silhouette(d, cat.pose, 1.1f, kRim);
        silhouette(d, cat.pose, 0, kFur);
        face(d, cat.pose);
    } else {
        drawFine(d, cat.pose, lookOf(style));
    }
    if (cat.inBox) drawBox(c, cat.box, floor, size, BoxPart::Front);
    drawBowl(c, cat.bowl, floor, size);
    drawFood(c, cat.food, floor, size);
    drawMug(c, cat.mug, cat.facing, floor, size);
    purring(d, cat, t);
    zs(d, cat, t);
}

Vec2f CatHead(const Cat& cat, Vec2f floor, float size) {
    return {floor.x + (cat.x + cat.facing * cat.pose.head.x) * size, floor.y + (cat.pose.head.y - 14 - cat.lift) * size};
}

}  // namespace ks
