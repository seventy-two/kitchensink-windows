// A black cat who sleeps above the Recent column and every so often wakes,
// stretches, perhaps wanders to another spot, finds something to do (a
// wash, a scratch, a slow blink at you, a pounce, kneading her bed) and
// curls up again. Some days a bowl of milk slides in, or a toy mouse. Where she is and
// how she's lying at any moment is worked out here, so it can be tested
// anywhere; the app draws her through a Canvas.
#pragma once

#include <array>

#include "canvas.h"

namespace ks {

// CatPose is her shape, side on, facing right, in units with the floor at
// y = 0 below her middle and y growing downwards. Her body is three
// circles, chest to hips, joined smoothly.
struct CatPose {
    Vec2f chest, mid, hips;
    float rChest = 0, rMid = 0, rHips = 0;
    Vec2f head;
    float rHead = 0;
    float headTilt = 0;  // radians, clockwise: down towards her chest
    // Legs: front near, front far, back near, back far.
    std::array<Vec2f, 4> joint, paw;
    static constexpr int kTailPoints = 8;
    std::array<Vec2f, kTailPoints> tail;  // from the base out
    float eyes = 0;    // 0 closed to 1 open
    float yawn = 0;    // 0 to 1
    float tongue = 0;  // 0 to 1, licking
};

// CatAction is what she's doing, for tests and anything that wants to
// know.
enum class CatAction { Sleep, Stir, Wake, Stretch, Turn, Walk, Stand, Groom, Scratch, Blink, Pounce, Knead, Drink, Hunt, LieDown };

// CatProp is something on the floor with her: where along it, how far off it
// (a toy in the air), how much it shows (0 hidden, fading in and out), its
// turn in radians, and for a bowl how full it is.
struct CatProp {
    float x = 0, lift = 0, alpha = 0, rot = 0, fill = 0;
};

// Cat is her at one moment: her pose, where she is along the floor, which
// way she faces (1 right, -1 left), how far she's turned round from there
// (0 to 1, her head leading, towards the viewer) and how asleep she is (1
// fast asleep, for her breathing and Z's).
struct Cat {
    CatPose pose;
    float x = 0;
    float facing = 1;
    float turn = 0;
    float asleep = 1;
    float lift = 0;  // units off the floor, mid-pounce
    float look = 0;  // 0 to 1, her head turned to look at the viewer
    CatAction action = CatAction::Sleep;
    CatProp bowl;  // of milk, slid in from the side
    CatProp toy;   // a toy mouse to hunt
};

// CatAt is the cat t seconds in, with half units of floor either side of
// the middle to wander. She sleeps for most of each kEpisode seconds.
constexpr double kEpisode = 100;
Cat CatAt(double t, float half);

// DrawCat draws her, mapping pose units to the canvas by floor + p * size,
// where floor is the middle of the floor. t moves her Z's along.
void DrawCat(Canvas& c, const Cat& cat, double t, Vec2f floor, float size);

}  // namespace ks
