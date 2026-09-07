#ifndef _VEHICLE_H_
#define _VEHICLE_H_

#include "Object.h"
#include "Wheel.h"
#include <windows.h>
#include <vector>
#include <atomic>
#include "stdbool.h"

//Shared base for a physics-driven, wheeled character - factored out of TankCharacter once a
//second vehicle (front-steered, power-split buggy) needed the same pedal/steering plumbing
//and per-wheel tuning resolution it already had.
//
//What's shared here is the INPUT surface (pedals, steering position), the
//wheel array/reset bookkeeping, and the bridge to the physics: the wheels are simulated by a
//reactphysics3d VehicleConstraint on this object's rigidbody (see CreateVehicleConstraint),
//built from `wheels` and kept in step with them every tick. What is NOT shared is the
//drivetrain - how pedals and steering become a drive torque, brake torque and steer angle per
//wheel (differential tracks vs. front-steer + power-split) - which stays in each subclass's own
//UpdatePhysicsState.
class Vehicle : public Object{
public:
    virtual ~Vehicle();

    //Manual movement: sets the pedal/steering state only. Each subclass's own
    //UpdatePhysicsState is what turns that into wheel torques - identical here regardless of
    //what the drivetrain does with it.
    void ThrottleInput(float factor); //0.0 = no throttle, +1.0 = full throttle
    void SteerInput(float factor); //-1.0 = full left, +1.0 = full right
    void BrakeInput(float factor); //0.0 = no brake, +1.0 = full brake

    //Modify pedal and steering position
    void Accelerate(float factor); //0-1
    void Brake(float factor);   // 0-1
    void SteerLeft(float factor); // 0-1
    void SteerRight(float factor); //0-1
    void Reverse(bool reverse); //true = reverse, false = forward

    //Scripted control does NOT live here any more. HoldDrive/HoldBrake/HoldSteer used to latch
    //an input for a duration because an MCP round trip cannot out-pace the tick rate - but that is
    //the same problem a synthetic key press has, so it is solved once, generically, in
    //InputController::HoldKey/HoldAxis. MCP is a player now; it holds a control and RunLogic drives
    //the vehicle from it like it does for a keyboard or a gamepad.
    //Virtual because a subclass may hold input state of its own that this has to clear too -
    //TankCharacter's direct per-track commands, which would otherwise stay latched and leave a
    //vehicle you switched away from driving on forever (the exact failure the comment on
    //ApplicationTank::SetControlledVehicle describes).
    virtual void ReleaseInputs(); //releases the pedals and steering immediately

    //Steering converges toward 0 by step per call - same per-tick relaxation TankCharacter had
    //inline before this was shared. Called once per UpdatePhysicsState tick.
    void DecaySteering(float step = 0.05f);

    //Teleports the body to pos/rot, zeroes velocity/angular velocity, wakes the body (a
    //stationary rigidbody put to sleep by rp3d would otherwise ignore the teleport's own next
    //tick of forces), releases the pedals and steering, and clears each wheel's
    //transient per-tick state (roll_angle/compression/grounded/steer_angle, and the
    //constraint's own spin/torques) so nothing looks mid-spin or mid-bounce right after the
    //reset. Virtual so a subclass with extra reset-worthy state (TankCharacter's turret recoil)
    //can call Vehicle::ResetState first, then handle its own on top.
    virtual void ResetState(const vec3& pos, const quat& rot);

    //ResetState from another thread than the physics thread (an MCP tool handler, the debug UI
    //on the render thread) races the physics step that may be running at that moment: a
    //teleport and velocity reset landing in the middle of the solver, or of the vehicle
    //constraint reading its wheels back, leaves the body and wheels in a corrupt state
    //(observed as wheels spinning at hundreds of rad/s with no torque after a reset). This
    //queues the reset instead; the physics thread applies it at the top of its next
    //UpdatePhysicsState via ApplyPendingReset, before anything else that tick reads the body.
    void RequestReset(const vec3& pos, const quat& rot);
    //Called first thing in every subclass's UpdatePhysicsState (physics thread).
    void ApplyPendingReset();

    //Turns a Wheel's per-field 0s into real numbers by falling back to this vehicle's own shared
    //defaults - see Wheel's own comment for why the fields are 0-means-inherit in the first
    //place. Pure virtual because what those defaults ARE is vehicle-specific (a tank's track
    //suspension isn't tuned like a buggy's), but every read of per-wheel geometry/suspension
    //tuning (physics, visuals, debug UI, telemetry) should go through this identically either
    //way, which is what makes it worth keeping as one seam here rather than duplicated per
    //subclass.
    virtual WheelTuning ResolveTuning(const Wheel& wheel) const = 0;
    float WheelRadius(const Wheel& wheel) const { return ResolveTuning(wheel).radius; }
    float WheelRestLength(const Wheel& wheel) const { return ResolveTuning(wheel).rest_length; }
    float WheelTravel(const Wheel& wheel) const { return ResolveTuning(wheel).travel; }
    float WheelFrictionCoefficient(const Wheel& wheel) const { return ResolveTuning(wheel).friction_coefficient; }
    float WheelLateralFriction(const Wheel& wheel) const { return ResolveTuning(wheel).lateral_friction; }

    //--- The physics bridge ---
    //Creates the rp3d VehicleConstraint on this object's rigidbody, one constraint wheel per
    //entry of `wheels`, from MakeWheelSettings. Call once the wheels are all laid out and the
    //body has its colliders (the constraint reads the body's mass and inertia every step, so
    //those may still change afterwards; the wheel COUNT may not - there is no removeWheel).
    //Replaces any constraint created before.
    void CreateVehicleConstraint();
    void DestroyVehicleConstraint();
    //Every tick, before the next step: pushes each Wheel's current geometry/tuning/enabled state
    //into its constraint wheel's settings, so a debug-UI drag on an anchor, radius, rest length
    //or friction value takes effect on the very next step, no rebuild needed. Cheap (a struct
    //copy per wheel), so it just runs unconditionally.
    void SyncWheelSettings();
    //Every tick, after the step: copies what the constraint found into each Wheel's runtime
    //state and diagnostics (grounded, compression and its rate, spring/tire forces, friction
    //saturation, spin) - see Wheel's own field comments for the units and sign conventions.
    void ReadBackWheels(float timestep);
    //Every tick, last: moves each wheel's visual (and strut) to where the constraint says it is.
    //Separate from ReadBackWheels so a subclass can adjust wheel state in between (a tank
    //imposing its track speed on airborne wheels) before it is drawn.
    void UpdateWheelVisuals(float timestep);
    //Clamps a drive force at the tread so this wheel's surface speed does not pass top_speed in
    //the commanded direction this tick (a rev limiter on the engine) - see the .cpp for why a
    //plain on/off check at top_speed isn't enough.
    //speed_fraction is how far this wheel's own control is pushed, 0..1: the governor holds it to
    //top_speed * that, so two wheels driven by different amounts settle at different speeds and a
    //differential actually exists. Defaulting it to 1 would restore the old behaviour where the
    //control only chose a direction - pass the real command.
    float GovernedDriveForce(const Wheel& wheel, const WheelTuning& tuning, float requested_force, float timestep, float speed_fraction) const;
    //One Wheel, fully resolved through ResolveTuning, as the constraint wants it. Conventions:
    //the vehicle's up is ref_up (+Y) and its forward ref_forward (-Z); a positive steer angle
    //turns a wheel to the left - the same convention Wheel::steer_angle already used.
    rp3d::VehicleWheelSettings MakeWheelSettings(const Wheel& wheel) const;

    rp3d::VehicleConstraint* rp_vehicle = NULL;

    std::vector<Wheel> wheels;

    //Default for any wheel whose own Wheel::mass is 0 - see that field. 2 kg is a small
    //tire-and-hub on a light vehicle; only its spin inertia (0.5 m r^2) matters here, the
    //chassis body already carries the vehicle's actual mass.
    float wheel_mass = 2.0f;
    //Ground samples per wheel (rp3d's numContactSamples): 1 is a single ray straight down the
    //suspension axis; 3 adds one forward and one backward around the tire's rim, so a kerb or
    //bump edge the tire's curve would reach before its very bottom does is found in time. Each
    //extra sample is one more raycast per wheel per tick.
    int contact_samples = 1;

    //Input mapped from either button or a gamepad, or something else.
    //Get clamped to [-1,1].
    float throttle_input = 0.0f;
    float steer_input = 0.0f;
    float brake_input = 0.0f;

    float gas_pedal = 0.0f;
    float brake_pedal = 0.0f;

    float steering_position = 0.0f; //From -1 to +1
    float steering_speed = 0.1f;

    bool f_reverse = false;


    //Settings
    //Total drive force at the treads (N) at full throttle, shared out over the driven wheels by
    //each subclass and handed to the constraint as a torque (force * radius) per wheel. Only
    //ever reached if the tires can hold it - beyond friction_coefficient * normal load the
    //wheel spins instead.
    float engine_force = 1000.0f;
    //Total brake force at the treads (N) at full pedal, an even share per wheel, as a brake
    //torque per wheel. Likewise limited by grip: brake harder than the tire can hold and the
    //wheel locks and slides.
    float brake_force = 1000.0f;
    float top_speed = 1.0f; //Soft cap (m/s): stop adding drive torque once real physics velocity reaches this.

    //Statistics
    float forward_speed = 0.0f; //m/s, read from the physics engine each tick, for telemetry/debug UI

private:
    //Backing state for RequestReset/ApplyPendingReset. The pose is written before the flag is
    //set and read after it is seen, so the flag alone needs to be atomic.
    std::atomic<bool> f_reset_requested{false};
    vec3 reset_position = {};
    quat reset_rotation = {};
};

#endif
