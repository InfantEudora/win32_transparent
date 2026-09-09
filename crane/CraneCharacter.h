#ifndef _CRANE_CHARACTER_H_
#define _CRANE_CHARACTER_H_

#include "AssetManager.h"
#include "physics/Physics.h"
#include "physics/PhysicsWorld.h"
#include "Scene.h"
#include "type_helpers.h"
#include <vector>

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
//
//Fourth axis, and the only one that is NOT a joint: the base slews (turns on its own vertical
//axis), carrying the whole mechanism above it around. There is no rp3d joint for it because
//there is nothing to join it TO - the base is the root of the mechanism. Instead the base body
//is KINEMATIC rather than STATIC and is turned by setting its angular velocity every tick,
//exactly the way Scene::AdvanceObjectMotions moves an object: a kinematic body still has
//infinite mass (so nothing hanging off the boom can shove the base around), but unlike a static
//body being setTransform()ed it presents a real VELOCITY to the solver - so boom_hinge, whose
//anchor and axis rp3d stores in each body's own local frame, carries the boom around with the
//base smoothly, rather than leaving the solver a position error to repair after the fact on
//every tick. Same reason Scene::MoveObjectOverTicks goes kinematic instead of teleporting.
//
//Fifth: an electromagnet on the underside of the hook, which is two mechanisms rather than one.
//
//  FINDING something to grab. rp3d has no sphere cast, but it does not need one: a second,
//  TRIGGER collider on the hook body (magnet_collider - a sphere over the pad) is the field, and
//  the engine already reports what is inside a trigger every step through
//  rp3d::EventListener::onTrigger. The app owns that listener (only one per world) and forwards
//  each overlap here - see OnMagnetFieldOverlap. A trigger collider produces no contact response,
//  so the field itself never pushes the load away.
//
//  HOLDING it: a FixedJoint between the hook and the load, anchored at the pad. Fixed rather than
//  BallAndSocket on purpose - a flat magnet pad holds its load's orientation; a ball joint would
//  let a grabbed crate spin freely underneath, which is a winch hook, not a magnet.
//
//The two halves run at different points in the tick and that split is load-bearing. onTrigger
//fires from INSIDE PhysicsWorld::Update, so it may only record candidates; creating or destroying
//a joint there would mutate the world mid-solve. All of it is reconciled afterwards in
//UpdatePhysicsState, which runs between steps - see UpdateMagnet.
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
    //The base turning on its own vertical axis, carrying boom/extension/hook around with it:
    //+-1, positive turns counter-clockwise seen from above (a positive right-hand rotation about
    //world +Y - the same convention every quat(axis,angle) in this engine uses), 0 holds.
    //Stored only; UpdatePhysicsState turns it into the base body's angular velocity each tick.
    //Deliberately unlimited - a real crane slews continuously, and nothing here winds up.
    void SetSlewSpeed(float speed);
    //Current base heading in radians. Read back off the body's actual orientation rather than
    //integrated from the command, so it stays honest whatever else ever moves the base.
    float GetSlewAngle();

    //--- Magnet. See this class's header comment for the two halves and why they are split.
    //
    //Switch the electromagnet on or off. Switching it OFF drops whatever is held. Only the flag
    //moves here; the joint is made and broken in UpdateMagnet, on the physics thread between
    //steps, so this is safe to call from a command handler.
    void SetMagnetEnabled(bool enabled);
    bool IsMagnetEnabled() const { return magnet_enabled; }
    //What the magnet currently holds, or NULL. Not owned - it is just another scene object.
    Object* GetGrabbedObject() const { return grabbed_object; }
    //Mass of that object in kg, or 0 if nothing is held.
    float GetGrabbedMass() const;
    //Centre of the magnet pad in world space - where the field is measured from and where a
    //grabbed load is anchored.
    vec3 GetMagnetWorldPosition();

    //Called by the app's rp3d::EventListener::onTrigger with the OTHER collider of a pair that
    //involved magnet_collider - i.e. from inside the physics step. Records a candidate and
    //nothing else; UpdateMagnet decides. Public only because the listener lives on the app.
    void OnMagnetFieldOverlap(rp3d::Collider* other_collider);

    //The field: a trigger sphere over the pad, a second collider on the HOOK's body. Kept as a
    //pointer because that pointer is the identity test in onTrigger - the hook has two colliders
    //and only this one is the magnet. (Collision category bits would be the tidier filter, but
    //every other body in this scene is on the default category, so a mask would exclude nothing
    //that matters; what is and is not grabbable is decided in IsGrabbable instead.)
    rp3d::Collider* magnet_collider = NULL;
    float magnet_radius = 0.35f;        //m - reach of the field below the pad
    //Refuses anything heavier. The winch (hook_max_motor_force) is what ultimately has to hold
    //the load up, so grabbing more than it can lift just sags the cable to its limit.
    float magnet_max_mass = 60.0f;      //kg
    //The pad, in the HOOK's own local space - its bottom face. Stored rather than recomputed
    //because the hook's half-height is a construction-time local, same as the piston's anchors.
    vec3 magnet_local_offset = {};

    //Makes the joint match the flag: grabs the nearest candidate when the magnet is on and empty,
    //drops the load when it is switched off. Called once per tick from UpdatePhysicsState, which
    //is between physics steps - the only point at which a joint may be created or destroyed.
    void UpdateMagnet();
    //Whether the magnet is allowed to pick this up. Excludes the crane's own parts (welding the
    //hook to its own boom would lock the mechanism), anything that is not a DYNAMIC body (a
    //FixedJoint to the static terrain would pin the whole crane to the ground, or tear it apart),
    //and anything over magnet_max_mass.
    bool IsGrabbable(Object* candidate) const;

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

    bool magnet_enabled = false;
    //How many bodies the field reported last tick, and how many of those were actually grabbable.
    //Reported in crane_telemetry: the difference between "the magnet sees nothing" and "the magnet
    //sees it but refuses it" is otherwise invisible from outside.
    int magnet_in_field = 0;
    int magnet_grabbable_in_field = 0;
    Object* grabbed_object = NULL;
    rp3d::FixedJoint* magnet_joint = NULL;
    //Written by OnMagnetFieldOverlap during the step, read and cleared by UpdateMagnet after it.
    //Both run on the physics thread within one tick, so no lock: the step has finished producing
    //these before anything reads them, and nothing else ever touches the vector.
    std::vector<Object*> magnet_candidates;
    //Kept so the magnet can create and destroy joints long after construction. The constructor
    //already takes it; before the magnet, nothing needed it afterwards.
    PhysicsWorld* physics_world = NULL;

    float slew_speed_command = 0.0f;    //+-1, from SetSlewSpeed
    float slew_max_rate = 0.6f;         //rad/s at SetSlewSpeed's +-1 (~34 deg/s) - slow enough that
                                        //the boom's hinge and the hook's ball joint track the turn
                                        //rather than being whipped around by it

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
