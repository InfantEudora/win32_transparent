#ifndef _CRANE_CHARACTER_H_
#define _CRANE_CHARACTER_H_

#include "AssetManager.h"
#include "physics/Physics.h"
#include "physics/PhysicsWorld.h"
#include "Scene.h"
#include "type_helpers.h"

//A small test rig for reactphysics3d's joint types, built entirely from the "cube" asset (no
//dedicated crane mesh exists yet). First attempt was a real closed physical loop - the boom on
//its own HingeJoint to the base, PLUS a separate piston (two bodies joined by a SliderJoint)
//pinned to the base at one end and to the boom (off its hinge axis) at the other, so extending
//the piston would mechanically lever the boom up the way a real hydraulic crane works. That blew
//up within a few physics steps (three hinges plus a slider all constraining the same handful of
//small, light bodies was too much for the iterative solver to resolve cleanly) - confirming the
//exact risk this was meant to test. Fallback now in place: the boom stays on a single real
//HingeJoint to the base (no closed loop - the same, already-proven pattern DozerCharacter's own
//arm uses), driven by the hinge's own motor, which behaves exactly like a hydraulic piston:
//velocity-controlled with a force rating, holding position when commanded to 0. The piston is
//purely cosmetic - no physics body, no joint - just a box that stretches and reorients every
//tick to visually connect the base to wherever the boom's real hinge angle currently puts it,
//the same trick the buggy's suspension-spring visual already uses.
class CraneCharacter : public Object{
public:
    CraneCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, const vec3& base_position);
    ~CraneCharacter();

    void UpdatePhysicsState() override;

    //Velocity command in +-1: positive raises, negative lowers, 0 holds the boom where it is
    //(against gravity, up to boom_max_motor_torque). Just stored; UpdatePhysicsState turns it into
    //the hinge motor's target speed each tick (it needs the current angle for the limit taper).
    void SetPistonSpeed(float speed);

    //Root-level Object - own physics body, own AddObject call into target_scene from this
    //constructor - joined to `this` (the base) only through boom_hinge, NOT a parent/child
    //Object hierarchy. Mirrors DozerCharacter's own armobject.
    Object* boom = NULL;
    rp3d::HingeJoint* boom_hinge = NULL;
    float boom_speed_command = 0.0f;        //+-1, from SetPistonSpeed
    float boom_max_rate = 0.5f;             //rad/s at SetPistonSpeed's +-1
    float boom_max_motor_torque = 2000.0f;  //N.m - the piston's force rating. Gravity on the 40kg,
                                            //4m boom is ~800 N.m at horizontal, so this holds and
                                            //lifts it with margin; overload it and the boom sags.

    //Hard hinge limits, relative to the creation-time (elevation_angle) pose - same convention
    //as rp3d::HingeJoint::getAngle(). The commanded motor speed tapers to 0 over the last
    //boom_limit_margin before either one, so the motor never drives into the hard stop - a
    //motor shoving into a limit every tick is what shows up as bouncing there.
    float boom_min_angle = -35.0f * TYPE_PI / 180.0f;
    float boom_max_angle = 35.0f * TYPE_PI / 180.0f;
    float boom_limit_margin = 5.0f * TYPE_PI / 180.0f;

    //Purely cosmetic - no physics, no joint. A separate root-level Object (same reasoning as
    //boom - see above), repositioned/reoriented/rescaled every tick in UpdatePhysicsState to
    //visually span from piston_base_anchor_world (fixed) to wherever boom_hinge's current angle
    //puts piston_boom_anchor_local on the boom - see UpdatePhysicsState's own comment.
    Object* piston_visual = NULL;
    vec3 piston_base_anchor_world = {};   //fixed - the base never moves
    vec3 piston_boom_anchor_local = {};   //the boom's own local space, relative to ITS origin
    float piston_radius = 0.08f;
};

#endif
