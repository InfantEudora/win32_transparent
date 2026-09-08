#ifndef _WHEEL_H_
#define _WHEEL_H_

#include "Object.h"
#include "physics/Physics.h"
#include "physics/PhysicsWorld.h"

//One wheel of a Vehicle: where its suspension hangs from the body, how it is tuned, and what
//the physics reported for it on the last tick.
//
//The simulation itself lives in reactphysics3d's VehicleConstraint (see Vehicle::
//CreateVehicleConstraint): a ray per wheel from the anchor along the suspension axis, a
//spring+damper along the contact normal solved implicitly (stable at any stiffness/damping and
//timestep, unlike the explicit per-tick forces this struct used to drive), a hard stop at full
//compression, and Coulomb tire friction along and across the rolling direction, all solved
//together with the rest of the world's contacts and joints. Every tick the vehicle pushes these
//fields into the constraint's per-wheel settings (Vehicle::SyncWheelSettings - so a debug-UI
//drag on any of them applies immediately) and reads the results back out afterwards
//(Vehicle::ReadBackWheels). Nothing here raycasts or applies force any more: this struct is the
//game-side description of a wheel plus its telemetry.
//
//radius/rest_length/travel/stiffness/damping/friction_coefficient/lateral_friction/mass default
//to 0, meaning "inherit the vehicle's own shared default for this" - see each vehicle's
//ResolveTuning(), which turns a Wheel plus the vehicle's own defaults into a fully-resolved
//WheelTuning. That's what makes later wheel/suspension damage possible without redesigning
//anything: dial one wheel's own value away from 0 (the debug UI drag already does this for
//radius/rest_length/travel) and only that wheel diverges from the rest.
struct Wheel{
    //--- Geometry ---
    //The suspension ANCHOR - the hard point on the body the strut hangs from - relative to
    //the body origin, in local space. NOT the wheel's centre: that hangs (rest_length -
    //compression) below this along suspension_axis, and moves every tick as the spring works.
    vec3 local_offset;
    //Local-space unit direction the hub travels as the suspension EXTENDS. (0,-1,0) is a
    //plain vertical strut. Renormalized when handed to the constraint, so editing it live
    //(e.g. via a debug-UI drag) is safe.
    vec3 suspension_axis = vec3(0,-1,0);
    //Rolling radius: the wheel rests its TREAD on the ground, not its hub - the ray reaches
    //this much further and the contact is taken this much higher than a bare hub-to-ground ray
    //would find it. 0 = inherit the vehicle's default.
    float radius = 0.0f;

    //--- Per-wheel suspension/tire overrides; 0 = inherit the vehicle's shared default ---
    float rest_length = 0.0f;         //metres, anchor-to-hub distance at full droop; also the spring's natural length
    float travel = 0.0f;              //metres the hub may rise above its rest position before the hard stop
    float stiffness = 0.0f;           //N per metre of compression
    float damping = 0.0f;             //N per (m/s) of compression rate
    //Coulomb friction along the ROLLING direction: the most drive/brake force this tire can
    //put into the ground is this times its current normal (spring) force.
    float friction_coefficient = 0.0f;
    //Coulomb friction SIDEWAYS: the most cornering/scrub force this tire can put into the
    //ground is this times its current normal force. Dimensionless, like friction_coefficient.
    //(Before the move into rp3d this was a viscous rate in N per (m/s) of slip, whose value
    //was set by an explicit-integration stability limit rather than by how much grip was
    //wanted - see TankCharacter::lateral_friction for what replaced that reasoning.)
    float lateral_friction = 0.0f;
    //How much grip a SLIDING tire keeps, as a fraction of what a gripping one has, and the slip
    //at which grip peaks - rp3d's slidingFrictionRatio/peakSlipRatio. 1.0 for the ratio restores
    //a single coefficient that never falls off (the behaviour before the falloff existed). Both
    //are 0-means-inherit overrides like everything else here; 0 is not a useful setting for
    //either (a tire with no sliding grip at all, or one that peaks at no slip).
    float sliding_friction_ratio = 0.0f;
    float peak_slip_ratio = 0.0f;
    //kg. Sets the wheel's spin inertia (a solid disc: 0.5 * mass * radius^2), i.e. how quickly
    //drive torque spins it up when it has no grip, and how hard the tire has to pull to bring
    //a landing wheel up to ground speed.
    float mass = 0.0f;

    //--- Role ---
    //local_offset.x < 0. This engine is right-handed with ref_forward = -Z and ref_up = +Y, so
    //the driver's right is forward x up = +X, and their left is -X. Note that makes
    //Object::ref_left = (1,0,0) MISNAMED - it points right. Nothing here derives a side from
    //that constant any more; where a lateral axis is needed it is built as up.cross(forward),
    //which is -X and genuinely left. Trusting the name instead is what silently put the tank's
    //two track commands on the wrong sides of the hull and reversed its steering.
    bool is_left_side = false;
    //local_offset.z < 0 - matches ref_forward's convention (a front axle sits ahead of the
    //body origin). Unused by the tank (its road/raised split serves the analogous "which group
    //of wheels" role there); a 4-wheeled vehicle's front/rear power split reads this instead of
    //re-deriving it from local_offset every tick.
    bool is_front_side = false;
    //Descriptive only (debug UI / telemetry): distinguishes a vehicle's "primary" wheels (a
    //tank's road wheels; a car's four) from any secondary ones (a tank's idler/drive sprocket).
    bool is_road_wheel = true;
    //Whether this wheel touches the ground at all (rp3d's per-wheel `enabled`): off, it casts
    //no ray and carries nothing - the escape hatch for taking a single contact out of the
    //picture while diagnosing one, or later, a wheel that's been knocked off entirely.
    bool can_contact_ground = true;
    //Whether this wheel takes a share of engine torque.
    bool driven = true;
    //Whether steer_angle is applied (around the body's local up) - false for every tank wheel
    //and a car's rear wheels; true for a car's front ones.
    bool steerable = false;
    float steer_angle = 0.0f;    //radians, current steering deflection, positive = left - only read if steerable

    //--- Runtime state (read back from the constraint each tick) ---
    float compression = 0.0f;    //current spring compression in metres, 0 = at rest length/airborne
    bool grounded = false;
    //rad/s, current spin rate around the wheel's own axle, in this engine's sign convention:
    //NEGATIVE while rolling the vehicle forward (WheelSuspension::UpdateVisual integrates
    //roll_angle from it around local X, and that's the way the meshes were authored). rp3d's
    //own convention is positive-forward; Vehicle::ReadBackWheels flips it. Owned by the
    //constraint: it spins the wheel up under drive torque, slows it under the brake, couples
    //it to the ground through the tire, and lets it freewheel in the air. A vehicle whose
    //wheels are mechanically coupled (a tank's own track) overrides this for its airborne
    //wheels - see TankCharacter's own per-tick loop.
    float angular_velocity = 0.0f;
    float roll_angle = 0.0f;     //accumulated wheel spin (radians) around the wheel's own axle, visual only - integrated from angular_velocity
    Object* visual = NULL;       //optional child Object placed at local_offset, followed tick to tick by WheelSuspension::UpdateVisual
    //The rolling radius wheel.visual's own mesh represents AT SCALE 1.0 - i.e. whatever a spawn-
    //time probe of its extents produced (see ApplicationTank::Init), the same number a wheel with
    //radius left at 0 resolves to by default. WheelSuspension::UpdateVisual uses the ratio of the
    //CURRENT resolved radius to this to uniformly scale wheel.visual, so dragging a wheel's own
    //radius in the debug UI grows/shrinks how it actually looks, not just how far its ray
    //reaches. 0 (never probed, or the mesh was missing) leaves the visual's scale untouched.
    float visual_natural_radius = 0.0f;
    //Fixed base orientation composed OUTSIDE the roll spin (visual->SetRotation ends up
    //visual_base_rotation * quat(local-X, +-roll_angle), sign per visual_mirrored below) -
    //identity leaves plain rolling behavior unchanged (every existing wheel). Exists for a wheel
    //whose visual is the SAME mesh asset on both sides of the vehicle (e.g. one "front wheel"
    //asset mirrored left/right): a 180 degree flip here is what turns the hubcap to face outward
    //on one side without needing a second, mirrored mesh asset.
    quat visual_base_rotation = quat(0,0,0,1);
    //True for a wheel whose visual_base_rotation is one of these 180 degree flips - tells
    //WheelSuspension::UpdateVisual to negate roll_angle for THIS wheel's rotation only (not the
    //stored value itself, which stays a real, unmirrored physics quantity shared with telemetry
    //and TankCharacter's own track-averaging).
    //
    //Necessary because a single rotation-based "mirror" can only ever stay consistent with ONE
    //other rotation it's composed with, since two rotations commute only when they share an axis
    //(see visual_base_rotation's own PI-around-up choice, picked to keep STEERING direction
    //correct because steering is also around up) - rolling is around local X instead, a
    //different axis, so composing the same mirror rotation on top of it flips its APPARENT
    //direction. Negating roll_angle for just the mirrored wheel's visual is what cancels that
    //back out. (The alternative - a true reflection via a negative-axis scale instead of any
    //rotation - would fix both at once with no sign games, but this renderer backface-culls with
    //a fixed winding order, so a mirrored object would need its winding flipped too or render
    //inside-out; not attempted here.)
    bool visual_mirrored = false;
    //Optional child Object representing the spring/strut itself (e.g. a coil-spring mesh),
    //authored with its own origin at the anchor. WheelSuspension::UpdateVisual places it at the
    //anchor, applies suspension_visual_rotation as-is (a FIXED orientation, measured once in the
    //modeling tool and set at spawn time - not derived from suspension_axis, since the authored
    //mesh's own axes don't necessarily agree with the strut's physics direction), and
    //non-uniformly scales it along its own local Y by how much of its full rest_length remains
    //extended - shorter as the wheel compresses, the way a real coil spring visibly does,
    //without needing a second mesh per compression state.
    Object* suspension_visual = NULL;
    quat suspension_visual_rotation = quat(0,0,0,1);

    //--- Per-tick diagnostics, read back from the constraint by Vehicle::ReadBackWheels.
    //Forces are the constraint's impulses over the tick divided by the timestep. Rewritten every
    //tick (zero for an airborne wheel) - not fed back into the simulation, purely for telemetry/
    //debug UI, because hull-level numbers alone only ever say THAT something is wrong, never
    //which contact is doing it. Unsynchronized between the physics thread that writes them and
    //whatever reads them - same as every other per-tick input/output here. ---
    float compression_rate = 0.0f;    //m/s, positive = compressing (from the change in compression over the tick)
    float spring_force = 0.0f;        //N along the ground normal: suspension spring plus hard stop
    float drive_force = 0.0f;         //N at the tread from the drive torque this wheel was commanded, signed
    float longitudinal_force = 0.0f;  //N the tire actually put into the ground along its rolling direction, signed (drive, brake or passive grip)
    float lateral_force = 0.0f;       //N the tire put into the ground sideways, positive towards the vehicle's LEFT
    float friction_budget = 0.0f;     //N, friction_coefficient * spring_force - the most this contact can transmit along the rolling direction
    //Angle (rad) between where the tire POINTS and where it is actually travelling, as the
    //constraint measured it. This is what the sideways tire force is built from (see rp3d's
    //corneringStiffness), so it separates "the tire isn't asked to corner" from "the tire is
    //asked to corner and has no grip left to do it with" - which read identically in
    //lateral_force alone, both being 0.
    float lateral_slip_angle = 0.0f;
    bool friction_saturated = false;  //true when the tire is at its friction limit along either direction (wheelspin, a locked wheel, a sliding tire)
};

//A Wheel's per-field 0s resolved against a vehicle's own defaults - see Wheel's own comment.
//Every vehicle builds one of these per wheel (cheap - it's 8 floats) whenever it hands the
//wheel to the constraint (Vehicle::MakeWheelSettings) or reads it back.
struct WheelTuning{
    float radius = 0.15f;
    float rest_length = 0.15f;
    float travel = 0.08f;
    float stiffness = 6000.0f;
    float damping = 400.0f;
    float friction_coefficient = 1.0f;
    float lateral_friction = 1.0f;
    float mass = 2.0f;
    float sliding_friction_ratio = 0.8f;  //rp3d default
    float peak_slip_ratio = 0.12f;        //rp3d default
};

namespace WheelSuspension{
    //First integrates roll_angle by angular_velocity*timestep (see Wheel::angular_velocity for
    //why this runs unconditionally, contact or no), then positions/rotates/scales wheel.visual
    //from the result - the hub hangs (tuning.rest_length - compression) from the anchor along
    //suspension_axis, spins around the vehicle's local X under wheel.visual_base_rotation, and is
    //uniformly scaled by tuning.radius against wheel.visual_natural_radius (see its own comment).
    //Also positions/rotates/scales wheel.suspension_visual, if set - see its own comment on
    //Wheel. Each of the two visuals is independently a no-op if left NULL.
    void UpdateVisual(Wheel& wheel, const WheelTuning& tuning, float timestep);
}

#endif
