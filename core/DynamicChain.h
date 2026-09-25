#ifndef _DYNAMIC_CHAIN_H_
#define _DYNAMIC_CHAIN_H_

#include <vector>
#include "type_vec3.h"
#include "type_quat.h"

/*
    A DYNAMIC CHAIN: secondary motion for a run of bones - legs dangling from a rope, a quiver
    bouncing on a back, a hood, a leaf on a vine. Unity's "dynamic bone", UE's AnimDynamics, in
    their simplest form.

    The chain is the joint positions of the bones, root first: point 0 is where the chain hangs
    from and is PINNED to the animation; every point after it is a particle, integrated by verlet
    in WORLD space. That is the whole trick: the particles do not move with the body unless
    something pulls them, so a body that accelerates leaves them behind, a body that stops sees
    them carry on, and gravity hangs them - inertia with no forces or pendulum equations written
    down, and nothing needed from a physics engine.

    Each tick, for each particle, root outward:
      0. moved by the pose's OWN motion relative to its frame, `follow` of it, velocity untouched -
         so the clip's own leg swing is not lagged, only the body's;
      1. verlet: carry on at the velocity it had, less `damping`, plus gravity;
      2. pull back toward THE POSE, carried along the simulated parent: where the animation puts
         the segment, hung from where the parent segment actually is and turned with it. So
         `stiffness` is a joint's muscle tone, not a spring to a point in space - 0 is dead
         weight, 1 is exactly the pose, and the ends of a loose chain whip rather than follow;
      3. held at its segment's animated length;
      4. PLANAR (a side view): kept on the plane through its animated point, `plane_normal`;
      5. and within its joint's limit (planar only), below.

    What comes out is the simulated points; turning the bones to them is the caller's, because it
    is rig and engine work (see ArcherModel::ApplyLegChains). SegmentDeviation gives each
    segment's swing off the pose as one angle about the plane normal, which is what a planar
    caller needs to apply it.

    VIEW STATE. It never feeds back into anything, and it is stepped once per simulation tick with
    a fixed dt - so it is deterministic and a replay reproduces it, but no rule may read it.

    No engine type beyond core's maths: tools/dynamic_chain_test.cpp builds it standalone.
*/

struct DynamicChainParams{
    vec3  gravity = vec3(0.0f,-9.81f,0.0f);     //world, units per second squared
    float dt = 1.0f / 60.0f;                    //one tick
    //The fraction of the way back to the pose closed each tick, 0..1. See step 2 above.
    float stiffness = 0.05f;
    //The fraction of a particle's velocity lost each tick, 0..1.
    float damping = 0.05f;
    //Non-zero: the chain moves only in the plane with this normal, through its animated points.
    vec3  plane_normal = vec3(0.0f,0.0f,0.0f);
    //A root that jumps further than this in one tick has been placed rather than moved - a
    //restart, a scene switch - and the chain starts again from the pose instead of whipping.
    float teleport = 1.0f;
    /*
        How much of the pose's OWN motion - the animation moving the limb relative to the body it
        hangs off, the `frame` passed to Step - goes straight through, 0..1. At 0 the chain lags
        everything, the clip's leg swing included, and a soft chain smears it into a wobble. At 1
        the animated motion arrives exactly and only the FRAME's motion - the body swinging,
        tilting, accelerating - is left to inertia, which is the motion secondary animation is for.
    */
    float follow = 0.0f;
};

/*
    A PLANAR joint limit on segment i: its angle about the plane normal relative to its PARENT
    segment, as a deviation from the POSE's own angle there, in radians - so {-0.5, 0.5} lets a
    joint bend half a radian further or less than the animation has it. Segment 0's parent is its
    own animated direction: its limit is how far the whole chain may swing off the pose.

    `bend_sign` +-1 additionally keeps the joint's total bend on that side of straight - a knee or
    an elbow, which fold one way only. 0 for a joint that bends both ways.
*/
struct DynamicChainLimit{
    float lo = -3.2f;
    float hi = 3.2f;
    int   bend_sign = 0;
};

class DynamicChain{
public:
    //Starts again from these points - the pose, standing still.
    void Reset(const std::vector<vec3>& animated);
    /*
        One tick. `animated` is the chain as the pose has it this tick, root first, in world space;
        `limits` is one per segment (points - 1), or empty for none. `frame` is the world rotation
        of what the chain hangs off - the character - which DynamicChainParams::follow measures the
        pose's own motion against. The first call, a change in point count or a teleport of the
        root resets instead.
    */
    void Step(const std::vector<vec3>& animated, const DynamicChainParams& params,
              const std::vector<DynamicChainLimit>& limits,
              const quat& frame = quat(0.0f,0.0f,0.0f,1.0f));

    /*
        Moves the whole chain RIGIDLY, velocity and all: every point turned by `rotation` about
        `about`. For motion that is the character's own decision rather than physics - she turns
        round - which the chain should go through with her instead of being left behind by. Call it
        before the Step that sees the pose turned.
    */
    void Carry(const quat& rotation, const vec3& about);

    const std::vector<vec3>& Points() const { return points; }
    //Segment i's swing off the pose it was last stepped with, about `normal`, in radians. 0 before
    //the first step.
    float SegmentDeviation(int i, const vec3& normal) const;
    bool  IsStarted() const { return !points.empty(); }

    //The signed angle from `a` to `b` about `normal`, both taken in the plane.
    static float SignedAngle(const vec3& a, const vec3& b, const vec3& normal);

private:
    std::vector<vec3> points;       //simulated, root first
    std::vector<vec3> previous;     //last tick's, for the verlet velocity
    std::vector<vec3> pose;         //the animated points of the last step
    quat last_frame = quat(0.0f,0.0f,0.0f,1.0f);  //and the frame they were in
};

#endif
