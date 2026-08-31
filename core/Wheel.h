#ifndef _WHEEL_H_
#define _WHEEL_H_

#include "Object.h"
#include "physics/Physics.h"
#include "physics/PhysicsWorld.h"

//One raycast-sampled suspension contact, generalized out of TankCharacter/TankWheel so a
//second (wheeled) vehicle can share the same suspension math. A wheel's TANGENTIAL force
//(drive thrust, steering-dependent grip) is genuinely vehicle-specific - a tank's differential-
//track command vs. a car's throttle/brake plus a live steer angle - so it stays out of this
//struct and out of WheelSuspension::UpdateContact below, in each vehicle's own
//UpdatePhysicsState. Only the NORMAL (spring+damper) force, and the raycast/compression math
//that produces it, is shared here.
//
//radius/rest_length/travel/stiffness/damping/max_force/max_point_speed/friction_coefficient
//default to 0, meaning "inherit the vehicle's own shared default for this" - see each
//vehicle's ResolveTuning()-style accessor, which turns a Wheel plus the vehicle's own defaults
//into a fully-resolved WheelTuning. That's what makes later wheel/suspension damage possible
//without redesigning anything: dial one wheel's own value away from 0 (the debug UI drag
//already does this for radius/rest_length/travel) and only that wheel diverges from the rest.
struct Wheel{
    //--- Geometry ---
    //The suspension ANCHOR - the hard point on the body the strut hangs from - relative to
    //the body origin, in local space. NOT the wheel's centre: that hangs (rest_length -
    //compression) below this along suspension_axis, and moves every tick as the spring works.
    vec3 local_offset;
    //Local-space unit direction the hub travels as the suspension EXTENDS. (0,-1,0) is a
    //plain vertical strut. Renormalized every tick by WheelSuspension::UpdateContact, so
    //editing it live (e.g. via a debug-UI drag) is safe.
    vec3 suspension_axis = vec3(0,-1,0);
    //Rolling radius: the wheel rests its TREAD on the ground, not its hub - the raycast
    //reaches this much further and the contact is taken this much higher than a bare hub-to-
    //ground ray would find it. 0 = inherit the vehicle's default.
    float radius = 0.0f;

    //--- Per-wheel suspension/friction overrides; 0 = inherit the vehicle's shared default ---
    float rest_length = 0.0f;         //metres, anchor-to-hub distance at full extension
    float travel = 0.0f;              //extra compressible range beyond rest_length
    float stiffness = 0.0f;           //N per metre of compression
    float damping = 0.0f;             //N per (m/s) of compression rate
    float max_force = 0.0f;           //N, hard per-wheel ceiling on the spring+damper force
    float max_point_speed = 0.0f;     //m/s ceiling on the contact point's velocity
    float friction_coefficient = 0.0f;//Coulomb: max tangential force = this * spring_force

    //--- Role ---
    bool is_left_side = false;   //local_offset.x > 0 - matches Object::GetLeft()/ref_left's convention
    //Descriptive only (debug UI / telemetry): distinguishes a vehicle's "primary" wheels (a
    //tank's road wheels; a car's four) from any secondary ones (a tank's idler/drive sprocket).
    bool is_road_wheel = true;
    //Whether this wheel raycasts and produces suspension force at all - the escape hatch for
    //taking a single contact out of the picture while diagnosing one, or later, a wheel that's
    //been knocked off entirely.
    bool can_contact_ground = true;
    //Whether this wheel takes a share of engine thrust when grounded.
    bool driven = true;
    //Whether steer_angle is applied (around the anchor's local up) before the raycast basis is
    //built - false for every tank wheel and a car's rear wheels; true for a car's front ones.
    bool steerable = false;
    float steer_angle = 0.0f;    //radians, current steering deflection - only read if steerable

    //--- Runtime state ---
    float compression = 0.0f;    //current spring compression in metres, 0 = extended/airborne
    bool grounded = false;
    float roll_angle = 0.0f;     //accumulated wheel spin (radians) around the wheel's own axle, visual only
    Object* visual = NULL;       //optional child Object placed at local_offset, followed tick to tick by WheelSuspension::UpdateVisual

    //--- Per-tick force diagnostics. Rewritten from scratch every tick (left at zero for a
    //wheel that never reached the relevant stage) - not fed back into the simulation, purely
    //for telemetry/debug UI, because hull-level numbers alone only ever say THAT something is
    //wrong, never which contact is doing it. Unsynchronized between the physics thread that
    //writes them and whatever reads them - same as every other per-tick input/output here. ---
    float point_speed = 0.0f;         //m/s magnitude of this contact's velocity BEFORE the max_point_speed clamp
    float compression_rate = 0.0f;    //m/s into the ground along the contact normal
    float spring_force = 0.0f;        //N along the ground normal, post-clamp, as actually applied
    float drive_force = 0.0f;         //N along the vehicle's forward axis from engine thrust, signed
    float longitudinal_force = 0.0f;  //N along forward from passive grip when undriven, signed
    float lateral_force = 0.0f;       //N along the vehicle's left axis from lateral_friction, signed
    float friction_budget = 0.0f;     //N, friction_coefficient * spring_force - the most this contact can transmit to the ground in any direction
    bool friction_saturated = false;  //true when the tick's combined longitudinal+lateral demand exceeded friction_budget and had to be scaled back
};

//A Wheel's per-field 0s resolved against a vehicle's own defaults - see Wheel's own comment.
//Every vehicle builds one of these per wheel per tick (cheap - it's 8 floats) and hands it to
//WheelSuspension::UpdateContact, which only ever sees real, resolved numbers.
struct WheelTuning{
    float radius = 0.15f;
    float rest_length = 0.15f;
    float travel = 0.08f;
    float stiffness = 6000.0f;
    float damping = 400.0f;
    float max_force = 4000.0f;
    float max_point_speed = 2.0f;
    float friction_coefficient = 1.0f;
};

namespace WheelSuspension{
    struct ContactResult{
        //True only once real compression/spring force was computed this tick - i.e. past the
        //"ray missed" and "extended past rest length" bail-outs. NOT the same as wheel.grounded,
        //which UpdateContact still sets true for a ray that hit but produced zero force (0
        //compression) - useful on its own for telemetry/debug UI, but not "this wheel did
        //anything this tick", which is what a caller deciding whether to use this contact's own
        //roll/point-velocity (vs. some fallback) actually needs.
        bool active = false;
        vec3 mount_world = {};
        vec3 push_dir = {};          //ground normal - only meaningful if active
        vec3 point_velocity = {};    //already max_point_speed-clamped - only meaningful if active
        float roll_distance = 0.0f;  //metres this contact rolled this tick, signed - only meaningful if active
    };

    //Raycasts along wheel.suspension_axis from its anchor, computes wheel.compression, applies
    //the spring+damper NORMAL force via AddWorldForceAt, and writes wheel.grounded/compression/
    //compression_rate/spring_force/point_speed/friction_budget/roll_angle (the last only when
    //active - see ContactResult::active). Does NOT apply any tangential force: drive thrust and
    //steering-dependent grip are vehicle-specific and stay in the caller, which uses
    //ContactResult's push_dir/point_velocity/mount_world to apply that itself.
    ContactResult UpdateContact(Wheel& wheel, const WheelTuning& tuning, Physics* physics,
                                 const vec3& body_world_pos, const quat& rotation,
                                 const vec3& com_world, const vec3& velocity,
                                 const vec3& angular_velocity, const vec3& forward, float timestep);

    //Positions/rotates wheel.visual from its current compression/roll_angle - the hub hangs
    //(rest_length - compression) from the anchor along suspension_axis, and spins around the
    //vehicle's local X. No-op if wheel.visual is NULL.
    void UpdateVisual(Wheel& wheel, float rest_length);
}

#endif
