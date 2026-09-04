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
//
//Second joint type under test: a telescoping extension - a thinner box nested inside the boom,
//on a real SliderJoint whose axis runs along the boom, with limits (fully retracted .. fully
//out) and a motor used exactly like the hinge's: velocity command, force rating, speed 0 holds.
//
//Third: a hook on a "stiff cable" hanging from the extension's tip. rp3d has no rope/distance
//joint and a joint's anchors can't be moved after creation, so a single BallAndSocketJoint alone
//would give a pendulum of FIXED length. Instead, a two-joint chain: a small swivel body pinned to
//the tip by a BallAndSocketJoint (the pendulum pivot - free to point any way), and the hook on a
//SliderJoint to the swivel along the swivel's own down axis - that slider IS the rigid cable, its
//motor is the winch (positive = pay out/lower). The cable you see is cosmetic, stretched between
//the swivel and the hook every tick, same trick as the piston.
class CraneCharacter : public Object{
public:
    CraneCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, const vec3& base_position);
    ~CraneCharacter();

    void UpdatePhysicsState() override;

    //Velocity command in +-1: positive raises, negative lowers, 0 holds the boom where it is
    //(against gravity, up to boom_max_motor_torque). Just stored; UpdatePhysicsState turns it into
    //the hinge motor's target speed each tick (it needs the current angle for the limit taper).
    void SetPistonSpeed(float speed);
    //Same idea for the telescoping extension: +-1, positive extends, negative retracts, 0 holds.
    void SetExtensionSpeed(float speed);
    //And the hook's winch: +-1, positive LOWERS (pays out cable), negative raises, 0 holds.
    void SetHookSpeed(float speed);

    //Root-level Object - own physics body, own AddObject call into target_scene from this
    //constructor - joined to `this` (the base) only through boom_hinge, NOT a parent/child
    //Object hierarchy. Mirrors DozerCharacter's own armobject.
    Object* boom = NULL;
    rp3d::HingeJoint* boom_hinge = NULL;
    float boom_speed_command = 0.0f;        //+-1, from SetPistonSpeed
    float boom_max_rate = 0.5f;             //rad/s at SetPistonSpeed's +-1
    float boom_max_motor_torque = 3000.0f;  //N.m - the piston's force rating. Gravity on the 40kg,
                                            //4m boom is ~800 N.m at horizontal, plus up to ~950 N.m
                                            //more from the 20kg extension at full reach, so this
                                            //holds and lifts it with margin; overload it and it sags.

    //Hard hinge limits, relative to the creation-time (elevation_angle) pose - same convention
    //as rp3d::HingeJoint::getAngle(). The commanded motor speed tapers to 0 over the last
    //boom_limit_margin before either one, so the motor never drives into the hard stop - a
    //motor shoving into a limit every tick is what shows up as bouncing there.
    float boom_min_angle = -35.0f * TYPE_PI / 180.0f;
    float boom_max_angle = 35.0f * TYPE_PI / 180.0f;
    float boom_limit_margin = 5.0f * TYPE_PI / 180.0f;

    //Telescoping extension: another root-level Object (same reasoning as boom), joined to the
    //boom only through extension_slider. Translation is along the boom's own +Z (toward its
    //tip), relative to the creation pose - same convention as rp3d::SliderJoint::getTranslation().
    Object* extension = NULL;
    rp3d::SliderJoint* extension_slider = NULL;
    float extension_speed_command = 0.0f;       //+-1, from SetExtensionSpeed
    float extension_max_rate = 0.6f;            //m/s at SetExtensionSpeed's +-1
    float extension_max_motor_force = 3000.0f;  //N - gravity along the boom on the 20kg extension is
                                                //at most ~200 N (boom near vertical), so plenty
    float extension_min = 0.0f;                 //m, fully retracted = the creation pose
    float extension_max = 2.5f;                 //m, fully out
    float extension_limit_margin = 0.15f;       //m, motor speed tapers to 0 over this before a limit

    //Hook chain - see this class's header comment. swivel hangs on hook_pivot (ball-and-socket)
    //at the extension's tip; hook hangs on hook_slider (the "cable") below the swivel. Slider
    //translation is along the swivel's local -Y (world down at creation), relative to the
    //creation pose where the cable is at hook_cable_min_length: 0 = fully reeled in.
    Object* swivel = NULL;
    Object* hook = NULL;
    rp3d::BallAndSocketJoint* hook_pivot = NULL;
    rp3d::SliderJoint* hook_slider = NULL;
    float hook_speed_command = 0.0f;        //+-1, from SetHookSpeed
    float hook_max_rate = 1.0f;             //m/s at SetHookSpeed's +-1
    float hook_max_motor_force = 300.0f;    //N - ~3x the 10kg hook's weight. Deliberately modest: a
                                            //rigid "cable" can PUSH, and a strong winch driving a
                                            //grounded hook would lever the whole boom up.
    float hook_min = 0.0f;                  //m of pay-out, reeled all the way in
    float hook_max = 3.0f;                  //m of pay-out, fully lowered
    float hook_limit_margin = 0.15f;        //m, motor speed tapers to 0 over this before a limit
    float hook_cable_min_length = 1.5f;     //m from pivot to hook centre when reeled in - long enough
                                            //that the hook clears the boom at max elevation (80 deg)
    //Cosmetic cable between the swivel and the hook's top, stretched/reoriented every tick.
    Object* cable_visual = NULL;
    float cable_radius = 0.03f;

    //Purely cosmetic - no physics, no joint. A separate root-level Object (same reasoning as
    //boom - see above), repositioned/reoriented/rescaled every tick in UpdatePhysicsState to
    //visually span from piston_base_anchor_local on the base to wherever boom_hinge's current
    //angle puts piston_boom_anchor_local on the boom - see UpdatePhysicsState's own comment.
    //Both anchors are stored in their own body's local space, so the piston keeps up if the
    //base is moved (the hinge itself already does - rp3d stores joint anchors body-local).
    Object* piston_visual = NULL;
    vec3 piston_base_anchor_local = {};   //this object's (the base's) local space
    vec3 piston_boom_anchor_local = {};   //the boom's own local space, relative to ITS origin
    float piston_radius = 0.08f;
};

#endif
