#ifndef _BUGGY_CHARACTER_H_
#define _BUGGY_CHARACTER_H_

#include "Vehicle.h"
#include <windows.h>
#include <vector>

//A plain 4-wheeled vehicle sharing TankCharacter's wheel suspension (an rp3d VehicleConstraint,
//see core/Vehicle.h) but with a car's drivetrain instead of a tank's: only the front axle
//steers, and engine power is split between the front and rear axles by power_split_front rather
//than between left and right sides. 0.0 = rear-wheel drive, 1.0 = front-wheel drive, anything
//in between = that fraction to the front axle and the rest to the rear - one knob covering
//RWD/FWD/AWD instead of three separate drive modes.
class BuggyCharacter : public Vehicle{
public:
    BuggyCharacter();
    ~BuggyCharacter();

    void UpdatePhysicsState() override;

    //Lays out the 4 wheels: front axle at local Z = -half_wheelbase (ref_forward is -Z, so this
    //is ahead of the origin), rear axle at +half_wheelbase, both at +-track_half_width in local
    //X, suspension anchored mount_height above the body origin and hanging straight down. Only
    //the front two are marked steerable.
    void SetupWheels(float track_half_width, float half_wheelbase, float mount_height);

    void InputToPedals(); //Applies the current throttle/brake/steer inputs to the wheels

    //Turns a Wheel's per-field 0s into real numbers by falling back to the buggy's own shared
    //defaults below - see Vehicle::ResolveTuning's comment for why. Deliberately a separate set
    //of defaults from TankCharacter's, even though several start at the same value: a buggy's
    //suspension/friction isn't tuned like a tank's, and per-wheel damage later should degrade
    //one vehicle's own numbers, not a value shared across both.
    WheelTuning ResolveTuning(const Wheel& wheel) const override{
        WheelTuning t;
        t.radius = wheel.radius > 0.0f ? wheel.radius : wheel_radius;
        t.rest_length = wheel.rest_length > 0.0f ? wheel.rest_length : suspension_rest_length;
        t.travel = wheel.travel > 0.0f ? wheel.travel : suspension_travel;
        t.stiffness = wheel.stiffness > 0.0f ? wheel.stiffness : suspension_stiffness;
        t.damping = wheel.damping > 0.0f ? wheel.damping : suspension_damping;
        t.friction_coefficient = wheel.friction_coefficient > 0.0f ? wheel.friction_coefficient : friction_coefficient;
        t.lateral_friction = wheel.lateral_friction > 0.0f ? wheel.lateral_friction : lateral_friction;
        t.sliding_friction_ratio = wheel.sliding_friction_ratio > 0.0f ? wheel.sliding_friction_ratio : sliding_friction_ratio;
        t.peak_slip_ratio = wheel.peak_slip_ratio > 0.0f ? wheel.peak_slip_ratio : peak_slip_ratio;
        t.mass = wheel.mass > 0.0f ? wheel.mass : wheel_mass;
        return t;
    }

    //Default rolling radius - 0 degrades to a point-contact model, same as TankCharacter. Set
    //from the buggy wheel assets' own mesh extents in ApplicationTank::Init.
    float wheel_radius = 0.0f;

    //0 = rear-wheel drive, 1 = front-wheel drive, in between = that fraction of engine_force to
    //the front axle, the rest to the rear - see this class's own header comment.
    float power_split_front = 0.0f;

    //Front/rear BRAKE bias, the same shape as power_split_front above but for brake_force:
    //0 = all braking on the rear axle, 1 = all on the front, 0.5 = an even split. Each axle's
    //share is then divided over the wheels on it, so the default 0.5 across two wheels per axle
    //reproduces exactly the even brake_force/4 per wheel this used before the bias existed.
    //
    //Braking is where a car's weight transfers forward, so the front tires have the most load
    //(and so the most grip) to spend just when the rears have least: a real car brakes forward-
    //biased, typically 0.6-0.7, and biasing it the other way locks the rears and spins the car.
    //Each wheel's brake torque is still capped by its own tire, so an over-biased axle shows up
    //as Wheel::friction_saturated on that axle rather than as more stopping power.
    float brake_split_front = 0.5f;

    //Front wheels only (Wheel::steerable) are turned about the body's up axis by up to this
    //much, scaled by steering_position - see UpdatePhysicsState. 35 degrees is a conservative
    //real-car lock angle.
    float max_steer_angle_degrees = 35.0f;

    //Suspension tuning - same shape and role as TankCharacter's own fields, handed to the
    //constraint per wheel by Vehicle::MakeWheelSettings. The constraint solves the spring
    //implicitly, so any value here is stable - these are feel choices, not integrator limits.
    //
    //4000 N/m is a RIDE FREQUENCY of 3.0 Hz on this car: each of the 4 wheels carries 11.25 kg of
    //the 45 kg body, and f = sqrt(k/m)/2pi = sqrt(4000/11.25)/2pi = 3.00. That is the number the
    //Buggy Controls' "Susp. Freq" slider shows and the one worth tuning by - it stays meaningful
    //when the mass changes, where a raw N/m does not. It settles at ~0.028 m of static
    //compression. This was 8000 N/m (4.24 Hz), which drove noticeably worse: stiff enough that
    //the tires skated over bumps instead of following the ground.
    //
    //200 N/(m/s) leaves a damping ratio of ~0.47 for the assembly at this stiffness
    //(c_total / 2 sqrt(k_total m) = 800 / 2 sqrt(16000 * 45)), a soft, off-road sort of bounce.
    float suspension_rest_length = 0.15f;
    float suspension_travel = 0.10f;
    float suspension_stiffness = 4000.0f;
    float suspension_damping = 200.0f;
    //Coulomb friction along the rolling direction (drive/brake) and sideways (cornering) - the
    //most force a tire puts into the ground is the coefficient times its normal load. 1.0 each
    //is a grippy tire on dry ground: at these the buggy corners rather than slides, and brakes
    //until a wheel locks. Drop lateral_friction towards 0.5 for a loose, drifty surface.
    float friction_coefficient = 1.0f;
    float lateral_friction = 1.0f;
    //How much of its grip a tire keeps once it is SLIDING rather than gripping, and the slip at
    //which grip peaks (rp3d's slidingFrictionRatio/peakSlipRatio - it scales the friction budget
    //down past the peak instead of holding it flat). This is what makes breaking traction cost
    //something: a locked wheel stops the car less well than one braked right at the limit, and a
    //spinning one pushes less hard. 1.0 restores a single coefficient that never falls off, which
    //is the behaviour from before the falloff existed - useful for an A/B.
    float sliding_friction_ratio = 0.8f;
    float peak_slip_ratio = 0.12f;

    //Last-resort safety net on the body's roll/pitch rate - see TankCharacter's own field.
    float max_roll_speed = 3.0f;
};

#endif
