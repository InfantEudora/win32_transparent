#ifndef _BUGGY_CHARACTER_H_
#define _BUGGY_CHARACTER_H_

#include "Vehicle.h"
#include <windows.h>
#include <vector>

//A plain 4-wheeled vehicle sharing TankCharacter's raycast-wheel suspension (see core/Wheel.h/
//.cpp) but with a car's drivetrain instead of a tank's: only the front axle steers, and engine
//power is split between the front and rear axles by power_split_front rather than between left
//and right sides. 0.0 = rear-wheel drive, 1.0 = front-wheel drive, anything in between = that
//fraction to the front axle and the rest to the rear - one knob covering RWD/FWD/AWD instead of
//three separate drive modes.
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
        t.max_force = wheel.max_force > 0.0f ? wheel.max_force : max_wheel_force;
        t.max_point_speed = wheel.max_point_speed > 0.0f ? wheel.max_point_speed : max_point_speed;
        t.friction_coefficient = wheel.friction_coefficient > 0.0f ? wheel.friction_coefficient : friction_coefficient;
        t.lateral_friction = wheel.lateral_friction > 0.0f ? wheel.lateral_friction : lateral_friction;
        return t;
    }

    //Default rolling radius - 0 degrades to the old point-contact model, same as TankCharacter.
    //Set from the buggy wheel asset's own mesh extents once that asset exists.
    float wheel_radius = 0.0f;

    float top_speed = 6.0f; //m/s soft cap - a wheeled buggy is unhindered by anything like a
                             //track's scrub, so this can be much higher than the tank's

    float engine_force = 2500.0f; //Newtons, total across both driven axles
    float brake_force = 3500.0f;  //Newtons

    //0 = rear-wheel drive, 1 = front-wheel drive, in between = that fraction of engine_force to
    //the front axle, the rest to the rear - see this class's own header comment.
    float power_split_front = 0.0f;

    //Front wheels only (Wheel::steerable) rotate their own rolling/tangential basis by up to
    //this much, scaled by steering_position - see UpdatePhysicsState. 35 degrees is a
    //conservative real-car lock angle.
    float max_steer_angle_degrees = 35.0f;

    //Suspension tuning - same shape and role as TankCharacter's own fields, see UpdatePhysicsState
    //for how these become a per-wheel spring+damper force via WheelSuspension::UpdateContact.
    //Starting values are a plain guess (no real chassis mass/mesh to derive them from yet, unlike
    //the tank's track-mesh-derived numbers) and expected to need live tuning once the buggy asset
    //exists - see the stability-limit math in TankCharacter.h's own suspension_damping comment
    //if retuning stiffness/damping: the same c_per_wheel < mass/(num_wheels*dt) bound applies here.
    float suspension_rest_length = 0.15f;
    float suspension_travel = 0.10f;
    float suspension_stiffness = 8000.0f;
    float suspension_damping = 200.0f;
    float max_wheel_force = 5000.0f;
    //Default for any wheel that doesn't override its own Wheel::lateral_friction (0 = inherit,
    //same pattern as every other tuning field here - see ResolveTuning above).
    float lateral_friction = 150.0f;   //N per (m/s) of sideways slip, per grounded wheel
    float friction_coefficient = 1.0f; //Coulomb - see TankCharacter's own field for why this exists
    float max_point_speed = 2.0f;
    float max_roll_speed = 3.0f;

    //How fast a DRIVEN wheel with no ground contact revs up under throttle - a free-spinning
    //wheel still has an engine trying to turn it, it just has nothing to grip, so it accelerates
    //toward its own free-spin limit instead of sitting inert (see UpdatePhysicsState). That limit
    //is top_speed carried through the wheel's own radius (rad/s = m/s / m), the same governor
    //normal driving is already capped by, rather than a separate number to keep in sync with it.
    float free_spin_acceleration = 60.0f; //rad/s^2
    //How much friction is in the drivetrain
    float free_spin_deceleration = 10.0f; //rad/s^2
};

#endif
