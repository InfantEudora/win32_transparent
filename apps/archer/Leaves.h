#ifndef _ARCHER_LEAVES_H_
#define _ARCHER_LEAVES_H_

#include "Wind.h"

#include <stdint.h>
#include <vector>

/*
    Leaves carried by the wind - docs/wind_plan.md step 4. A fixed swarm kept in the camera's view,
    blown through the WindField, landing on tops and lifting off them again in a gust.

    Engine-free, like Wind: the app turns each Leaf into an Object (a pose from `axis`/`angle`,
    or flat at `yaw` when resting), and leaves_test.cpp checks the behaviour with no window.

    --- HOW A LEAF MOVES --------------------------------------------------------------------------
    Its velocity is pulled toward the local wind over `drag_time` - a leaf is light, so it takes
    on the air's speed within a fraction of a second - plus a steady sink at `fall_speed` and the
    zig-zag a falling leaf makes. Nothing special makes it stay in an eddy: a leaf that follows
    the air closely, in a swirl faster than it sinks, simply circles, and drifts out as it sinks.
    That is the "caught in the eddies", and the test measures it against the same leaves with
    the eddies switched off.

    It lands on anything it sinks onto that faces up, if the air there is calm enough, and lies
    there - in a strong wind it slides on along the surface instead; a wind above `lift_speed`
    (a gust, usually) picks it up again, and a lesser one skitters it along. After `rest_ticks`
    lying still it shrinks away and comes back as a new leaf at the upwind edge of the view.
    Against a wall it is pushed back out and loses the speed it had into it.

    --- DETERMINISTIC -----------------------------------------------------------------------------
    Every random choice is a hash of the leaf's index and how many times it has been spawned -
    never a stream, never the engine's RRandom - so the same field and the same view blow the same
    leaves. Durations are ticks; ARCHER_DT is the step.
*/

struct LeafParams{
    /*
        How many. With density > 0 the swarm holds `density` leaves per square unit of its REGION
        (the view grown by `pad`), up to max_count, and `count` is what that came to; with density
        0, `count` is used as given - which is how the tests pin it.
    */
    float density      = 0.0f;
    int   max_count    = 1200;
    int   count        = 140;
    /*
        The region leaves live in: the view grown by this fraction of its size on every side. It
        is what lets the camera zoom out without the swarm showing as a box - the leaves are
        already there. Leaves outside the VIEW itself are stepped every FAR_STRIDE ticks (the
        stride times as far each time), so the padding costs a fraction of what it holds.
    */
    float pad          = 0.0f;
    float drag_time    = 0.30f;     //seconds for a leaf to take on the air's speed
    float fall_speed   = 0.55f;     //settling speed in still air, units/s
    float flutter      = 0.7f;      //the zig-zag's sideways speed, units/s
    float lift_speed   = 3.4f;      //wind that lifts a resting leaf
    float skitter      = 0.3f;      //a resting leaf slides at this fraction of the wind
    int   rest_ticks   = 360;       //how long one lies before it is recycled
    float size_min     = 0.6f;      //scale on the leaf mesh, per leaf
    float size_max     = 1.0f;
    int   tints        = 4;         //the app's material variants; each leaf picks one
    float z_min        = -1.8f;     //depth through the slab - some behind her, some in front
    float z_max        = 1.4f;
    float radius       = 0.05f;     //contact radius against the blocks
    float margin       = 3.0f;      //how far outside the view a leaf spawns and is let go
};

enum LeafState{
    LEAF_FLYING = 0,
    LEAF_RESTING,
    LEAF_FADING                     //shrinking away at the end of its rest
};

struct Leaf{
    float x = 0.0f, y = 0.0f, z = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    float axis[3] = {0.0f,0.0f,1.0f};   //tumble axis, unit
    float angle = 0.0f;                 //radians about it
    float yaw = 0.0f;                   //heading when lying flat
    float tilt = 0.0f;                  //how far it leans when lying, so a side view sees its face
    float size = 1.0f;
    float fade = 1.0f;                  //0..1, the app multiplies the scale by it
    float phase = 0.0f;                 //the zig-zag's
    int   tint = 0;
    int   state = LEAF_FLYING;
    int   ticks = 0;                    //in the current state
    uint32_t spawns = 0;                //how many times this slot has been (re)spawned
};

class LeafSwarm{
public:
    //Fills the view with leaves where there is air. Also what the first Step does.
    void Reset(const WindField& wind, float x0, float y0, float x1, float y1);
    /*
        One tick. The view is what the camera sees now; the swarm lives in it grown by
        params.pad. A change in the count (a zoom, with density on) adds or drops leaves at the
        end - the others are left exactly where they are - and new ones fade in.
    */
    void Step(const WindField& wind, int64_t tick, float x0, float y0, float x1, float y1);
    //The count the density asks for over a region, clamped - what Step uses.
    int CountFor(float x0, float y0, float x1, float y1) const;

    std::vector<Leaf> leaves;
    LeafParams params;

private:
    bool f_started = false;
    //Places leaf i anew - anywhere in the region (scatter, fading in) or just outside the side
    //opposite the one it left by.
    void Spawn(const WindField& wind, int i, bool f_scatter, float x0, float y0, float x1, float y1);
};

#endif
