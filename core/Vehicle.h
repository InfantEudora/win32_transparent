#ifndef _VEHICLE_H_
#define _VEHICLE_H_

#include "Object.h"
#include "Wheel.h"
#include <windows.h>
#include <vector>

//Shared base for a physics-driven, wheeled character - factored out of TankCharacter once a
//second vehicle (front-steered, power-split buggy) needed the same pedal/steering-latch
//plumbing and per-wheel tuning resolution it already had. What's shared here is deliberately
//just the INPUT surface (pedals, steering position, MCP hold-latches) and the wheel array/reset
//bookkeeping - not the drivetrain itself (differential-track vs. front-steer + power-split),
//which stays in each subclass's own UpdatePhysicsState, the same way WheelSuspension::
//UpdateContact already keeps the raycast/spring math shared while leaving tangential force to
//the caller.
class Vehicle : public Object{
public:
    virtual ~Vehicle(){}

    //Manual movement: sets the pedal/steering state only. Each subclass's own
    //UpdatePhysicsState is what turns that into an actual drivetrain force - identical here
    //regardless of what the drivetrain does with it.
    void Accelerate(float factor);
    void Brake(float factor);
    void SteerLeft(float factor);
    void SteerRight(float factor);
    void Reverse(float factor);

    //For scripted/MCP control: a single external call can't realistically out-pace the physics
    //tick rate (gas_pedal/brake_pedal/steering_position all decay back to idle every tick unless
    //re-asserted), so these latch the equivalent input as "held" for duration_ms of real time,
    //re-asserted every tick until the latch expires - see ApplyHoldLatches, which every
    //subclass's UpdatePhysicsState calls once near its own top.
    void HoldDrive(bool reverse, float amount, float duration_ms);
    void HoldBrake(float amount, float duration_ms);
    void HoldSteer(float signed_amount, float duration_ms); //negative = left, positive = right
    void ReleaseInputs(); //cancels all latches and releases the pedals immediately

    //Re-asserts whichever hold-latch is still active, exactly as if RunLogic had just called
    //Accelerate/Reverse/Brake/SteerLeft/SteerRight this tick from a held key. Called once near
    //the top of every subclass's UpdatePhysicsState.
    void ApplyHoldLatches();
    //Steering converges toward 0 by step per call - same per-tick relaxation TankCharacter had
    //inline before this was shared. Called once per UpdatePhysicsState tick.
    void DecaySteering(float step = 0.05f);

    //Teleports the body to pos/rot, zeroes velocity/angular velocity, wakes the body (a
    //stationary rigidbody put to sleep by rp3d would otherwise ignore the teleport's own next
    //tick of forces), releases every pedal/steering/hold-latch, and clears each wheel's
    //transient per-tick state (roll_angle/compression/grounded/steer_angle) so nothing looks
    //mid-spin or mid-bounce right after the reset. Virtual so a subclass with extra reset-worthy
    //state (TankCharacter's turret recoil) can call Vehicle::ResetState first, then handle its
    //own on top.
    virtual void ResetState(const vec3& pos, const quat& rot);

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

    std::vector<Wheel> wheels;

    float gas_pedal = 0.0f;
    float brake_pedal = 0.0f;
    float steering_position = 0.0f; //From -1 to +1
    bool f_reverse = false;

    //Hold-latches backing HoldDrive/HoldBrake/HoldSteer, timestamped with GetTickCount64().
    float gas_latch_amount = 0.0f;
    bool gas_latch_reverse = false;
    unsigned long long gas_latch_until_ms = 0;
    float brake_latch_amount = 0.0f;
    unsigned long long brake_latch_until_ms = 0;
    float steer_latch_amount = 0.0f;
    unsigned long long steer_latch_until_ms = 0;
};

#endif
