// Mag (Magnus), a black cat who sleeps above the Recent column and every so
// often wakes, stretches, perhaps wanders to another spot, finds something
// to do (a wash, a scratch, a slow blink at you, a pounce, kneading his bed,
// a mad sprint, a cardboard box) and curls up again. Some days a bowl of
// milk slides in, or a toy mouse; at mealtimes he waits for his food. When
// the panels leave room at the bottom of the window he walks off the right
// and comes in from the left down there, where he can jump up onto the last
// panel's edge (and knock a mug off it). He notices the mouse now and then,
// purrs when he's clicked (and stalks off if it's overdone), and lifts his
// head when something new arrives. Where he is and how he's lying at any
// moment is worked out here, so it can be tested anywhere; the app draws him
// through a Canvas.
#pragma once

#include <array>
#include <memory>

#include "canvas.h"

namespace ks {

// CatPose is his shape, side on, facing right, in units with the floor at
// y = 0 below his middle and y growing downwards. His body is three
// circles, chest to hips, joined smoothly.
struct CatPose {
    Vec2f chest, mid, hips;
    float rChest = 0, rMid = 0, rHips = 0;
    Vec2f head;
    float rHead = 0;
    float headTilt = 0;  // radians, clockwise: down towards his chest
    // Legs: front near, front far, back near, back far.
    std::array<Vec2f, 4> joint, paw;
    static constexpr int kTailPoints = 8;
    std::array<Vec2f, kTailPoints> tail;  // from the base out
    float eyes = 0;    // 0 closed to 1 open
    float yawn = 0;    // 0 to 1 (also his mouth open to meow)
    float tongue = 0;  // 0 to 1, licking
    float ear = 0;     // -1 (near ear flat back) to 1 (flicked forward)
};

// CatAction is what he's doing, for tests and anything that wants to
// know.
enum class CatAction {
    Sleep, Stir, Wake, Stretch, Turn, Walk, Stand, Groom, Scratch, Blink, Pounce, Knead, Drink, Hunt, LieDown,
    Zoomies, Box, Perch, KnockOff, Wait, Eat, Lash
};

// CatBed is how he lies asleep: curled up, a loaf (paws tucked under), on
// his side, or belly up (only on warm days).
enum class CatBed { Curl, Loaf, Side, Belly };

// CatPlace is where he is: above Recent, or in the space left at the
// bottom of the panels.
enum class CatPlace { Top, Bottom };

// CatProp is something with him: where along the floor, how far off it (a
// toy in the air, a mug on a panel's edge), how much it shows (0 hidden,
// fading in and out), its turn in radians, and for a bowl how full it is.
struct CatProp {
    float x = 0, lift = 0, alpha = 0, rot = 0, fill = 0;
};

// Cat is him at one moment: his pose, where he is along the floor, which
// way he faces (1 right, -1 left), how far he's turned round from there
// (0 to 1, his head leading, towards the viewer) and how asleep he is (1
// fast asleep, for his breathing and Z's).
struct Cat {
    CatPose pose;
    float x = 0;
    float facing = 1;
    float turn = 0;
    float asleep = 1;
    float lift = 0;    // units off the floor: mid-leap, or up on a panel's edge
    float ground = 0;  // units up of what he's on (a panel's edge), for his shadow
    float look = 0;    // 0 to 1, his head turned to look at the viewer
    float purr = 0;    // 0 to 1, being stroked
    float say = 0;     // 0 to 1, a "mrrp" showing
    bool inBox = false;
    CatPlace place = CatPlace::Top;
    CatBed bed = CatBed::Curl;
    CatAction action = CatAction::Sleep;
    CatProp bowl;  // of milk, slid in from the side
    CatProp food;  // his dinner, at mealtimes
    CatProp toy;   // a toy mouse to hunt
    CatProp box;   // a cardboard box to sit in
    CatProp mug;   // on a panel's edge, to knock off
};

// CatWorld is what he knows of the window and the day.
struct CatWorld {
    float half = 100;     // units of floor either side of the middle above Recent
    float gapHalf = 0;    // the same in the space at the bottom; 0 if there's none
    float ledge = 0;      // units from the bottom floor up to the last panel's edge; 0 for none
    float clock = 43200;  // local seconds since midnight
    int day = 0;          // local day number, so meals come once a day
    bool rain = false;    // raining now: he sleeps longer
    bool warm = false;    // 20°C or more: he may sleep belly up
};

// CatInput is what the viewer's doing to him this frame.
struct CatInput {
    bool hover = false;  // the mouse is over him
    Vec2f mouse;         // in units from the middle of his floor
    bool click = false;  // he's been clicked
    bool event = false;  // something new arrived in Recent
};

// kMinGapHalf is the narrowest space at the bottom he'll go to.
constexpr float kMinGapHalf = 110;

// Mag works out the cat from one frame to the next. Left alone he's the
// same every run: his plans come from a hash of the episode's number.
class Mag {
public:
    Mag();
    ~Mag();
    // Step moves him on to t seconds (ImGui time) and returns him.
    Cat Step(double t, const CatWorld& w, const CatInput& in = {});

private:
    struct State;
    std::unique_ptr<State> s_;
};

// DrawCat draws him, mapping pose units to the canvas by floor + p * size,
// where floor is the middle of the floor. t moves his Z's along.
void DrawCat(Canvas& c, const Cat& cat, double t, Vec2f floor, float size);

// CatHead is the top of his head on the canvas, for a speech bubble.
Vec2f CatHead(const Cat& cat, Vec2f floor, float size);

}  // namespace ks
