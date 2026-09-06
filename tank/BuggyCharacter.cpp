#include "BuggyCharacter.h"
#include "type_helpers.h"
#include <cmath>
#include "Debug.h"

static Debugger *debug = new Debugger("Buggy", DEBUG_ALL);

BuggyCharacter::BuggyCharacter(){
    top_speed = 6.0f;
    engine_force = 400.0f; //Newtons, total across both driven axles
    brake_force = 400.0f;  //Newtons
}

BuggyCharacter::~BuggyCharacter(){
}


//Turns the abstract driver inputs - throttle/steer/brake, each a scalar, set by a key, a stick or
//a scripted player alike - into the pedal and steering state this drivetrain actually runs on.
//A car needs the translation because its controls are not the same shape as its inputs: the
//throttle arrives as ONE signed axis but the drivetrain wants a magnitude plus a gear, and the
//steering arrives as a lock to hold but the front wheels reach it over time rather than snapping.
//The tank needs no equivalent - its tracks take a signed command per side directly.
void BuggyCharacter::InputToPedals(){
    //Throttle: the sign picks the gear, the magnitude is how far the pedal goes down. Note this
    //selects reverse the moment the input goes negative, with no "brake to a stop first" rule -
    //the drive force simply opposes the current motion, which decelerates the body before it ever
    //moves the other way, the same as a real vehicle being driven badly.
    Reverse(throttle_input < 0.0f);
    Accelerate(fabs(throttle_input));

    //Already unsigned - Vehicle::BrakeInput takes the absolute value - so this is a straight copy.
    Brake(brake_input);

    //Steering is a POSITION that ramps toward what is being asked for, not a demand that snaps to
    //it: steer_input is the lock the driver wants and steering_position walks there by at most
    //steering_speed per tick. Both halves matter. Ramping keeps a digital key from reaching full
    //lock in one tick, which is what SteerLeft/SteerRight gave before these inputs existed.
    //Target-seeking is what makes a half-deflected stick - or a scripted 0.5 - settle at half lock:
    //simply calling SteerRight(0.5) each tick would move it by steering_speed*0.5, which against
    //DecaySteering()'s pull back toward centre nets zero, leaving the wheels stuck wherever they
    //happened to be rather than at half lock.
    //
    //DecaySteering() has already run this tick (see UpdatePhysicsState), so a held input nets the
    //difference between the two rates - and a released one is left to centre on its own.
    float steer_delta = clamp(steer_input - steering_position,-steering_speed,steering_speed);
    steering_position = clamp(steering_position + steer_delta,-1.0f,1.0f);
}

void BuggyCharacter::UpdatePhysicsState(){
    float timestep = physics_timestep; //see TankCharacter::UpdatePhysicsState

    ApplyPendingReset(); //see Vehicle::RequestReset
    DecaySteering();
    InputToPedals(); //see Vehicle::ThrottleInput/BrakeInput/SteerInput

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;
    float max_steer_angle = max_steer_angle_degrees * TYPE_PI / 180.0f;

    //The wheels are simulated by the rp3d VehicleConstraint on the body (see Vehicle::
    //CreateVehicleConstraint). Decided here, every tick, is only the drivetrain: steer angle,
    //drive torque and brake torque per wheel from the pedals and steering.
    Physics* physics = GetPhysics();
    if (physics && rp_vehicle){
        if (gas_pedal > 0.0f || brake_pedal > 0.0f || steering_position != 0.0f){
            physics->WakeUp(); //same "sleeping body ignores forces" issue as TankCharacter - see its UpdatePhysicsState
        }

        quat rotation = physics->GetBodyWorldOrientation(); //fresh this tick, unlike GetRotation() - see TankCharacter's comment
        vec3 forward = rotation * ref_forward;
        vec3 up = rotation * ref_up;
        vec3 velocity = physics->GetVelocity();
        forward_speed = velocity.dot(forward);

        //Results of the step that just ran into the Wheel diagnostics, then the wheels' current
        //geometry/tuning into the constraint for the next one - see TankCharacter's comment.
        ReadBackWheels(timestep);
        SyncWheelSettings();

        //Braking goes through each wheel's own tire (a brake torque per wheel), not as one
        //whole-body force at the centre of mass: a flat force at the COM ignored whether any
        //wheel actually had the grip to back it up, so it could brake exactly as hard on ice as
        //on asphalt. Through the tire, heavy braking saturates and locks a wheel up (see
        //Wheel::friction_saturated), same as too much throttle spins one.
        bool drive_capped = fabs(forward_speed) >= top_speed;
        float drive_command = reverse_multiplier * gas_pedal;
        float brake_force_per_wheel = wheels.empty() ? 0.0f : brake_pedal * brake_force / (float)wheels.size();

        int num_front_driven = 0, num_rear_driven = 0;
        for (Wheel& wheel : wheels){
            if (!wheel.driven){ continue; }
            if (wheel.is_front_side){ num_front_driven++; }else{ num_rear_driven++; }
        }

        size_t count = min(wheels.size(),(size_t)rp_vehicle->getNbWheels());
        for (size_t i = 0; i < count; i++){
            Wheel& wheel = wheels[i];
            WheelTuning tuning = ResolveTuning(wheel);
            rp3d::VehicleWheel& w = rp_vehicle->getWheel((rp3d::uint32)i);

            //Steering: the front wheels' rolling/grip axes are turned about the body's up by
            //the steer angle - the constraint builds each tire's own forward/sideways basis from
            //it, so a steered wheel grips along ITS heading, not the body's, which is what turns
            //the car. Negated: steering_position is negative for left (see Vehicle::SteerLeft),
            //but a LEFT turn is a POSITIVE rotation about +up here (quat(+Y,t) sends forward
            //(0,0,-1) towards -X, the driver's left), and that is rp3d's convention too.
            wheel.steer_angle = wheel.steerable ? -steering_position * max_steer_angle : 0.0f;
            w.setSteerAngle(wheel.steer_angle);

            //Engine thrust while this axle has a share of power and the body isn't at top
            //speed. An undriven or coasting wheel just rolls: the tire couples its spin to the
            //ground, so it neither pushes nor drags. A wheel spinning under more torque than
            //its grip can take (wheelspin), or in the air, winds up against its own inertia -
            //rev-limited here at top_speed's own surface speed so it doesn't do so without
            //bound (see TankCharacter's own comment on this).
            float axle_fraction = wheel.is_front_side ? power_split_front : (1.0f - power_split_front);
            int axle_driven_count = wheel.is_front_side ? num_front_driven : num_rear_driven;
            float drive_force = 0.0f;
            if (wheel.driven && axle_fraction > 0.0f && !drive_capped && drive_command != 0.0f && axle_driven_count > 0){
                drive_force = GovernedDriveForce(wheel,tuning,(engine_force * axle_fraction / axle_driven_count) * drive_command,timestep,fabs(drive_command));
            }
            w.setDriveTorque(drive_force * tuning.radius);
            w.setBrakeTorque(brake_force_per_wheel * tuning.radius);
        }

        //Visual follow: bob/spin from WheelSuspension::UpdateVisual, then layer the steer yaw on
        //top for the front wheels - UpdateVisual itself stays vehicle-agnostic (the tank never
        //needs a yaw component), so that part is applied here instead. Unlike the tank's tracks,
        //each buggy wheel rolls independently - there's no shared belt to keep an airborne wheel
        //in step with, so a wheel off the ground simply freewheels in the constraint.
        UpdateWheelVisuals(timestep);
        for (Wheel& wheel : wheels){
            if (wheel.steerable && wheel.visual){
                //Re-set rather than left to UpdateVisual's own (visual_base_rotation * roll) -
                //a steerable wheel also needs the steer yaw composed in, between the roll spin
                //(applied first, around the wheel's own local X) and visual_base_rotation's
                //mirror flip (applied last, on the whole steered+spinning assembly). roll_angle
                //is negated the same way UpdateVisual's own generic path does, for the same
                //reason - see Wheel::visual_mirrored.
                float visual_roll = wheel.visual_mirrored ? -wheel.roll_angle : wheel.roll_angle;
                wheel.visual->SetRotation(wheel.visual_base_rotation * quat(vec3(0,1,0),wheel.steer_angle) * quat(vec3(1,0,0),visual_roll));
            }
        }

        //Same last-resort roll/pitch safety net as TankCharacter - see its own comment for why
        //yaw is exempt and why this should be dead code in any drivable configuration.
        vec3 angvel_now = physics->GetAngularVelocity();
        float yaw_rate = angvel_now.dot(up);
        vec3 tilt_rate = angvel_now - up * yaw_rate;
        float tilt_speed = tilt_rate.length();
        if (tilt_speed > max_roll_speed){
            physics->SetAngularVelocity(up * yaw_rate + tilt_rate * (max_roll_speed / tilt_speed));
        }
    }

    //No idle brake, unlike the tank: a car coasts when the pedals are released, and the tires'
    //rolling resistance plus the body's own damping bring it to rest on their own.

    Object::UpdatePhysicsState();
}

void BuggyCharacter::SetupWheels(float track_half_width,float half_wheelbase,float mount_height){
    wheels.clear();
    for (int axle = 0; axle < 2; axle++){
        bool is_front = (axle == 0);
        float z = is_front ? -half_wheelbase : half_wheelbase; //ref_forward is -Z, so front is negative Z
        for (int side = 0; side < 2; side++){
            bool is_left = (side == 0);
            float x = is_left ? -track_half_width : track_half_width; //-X = left, see Wheel::is_left_side
            Wheel wheel;
            wheel.local_offset = vec3(x,mount_height,z);
            wheel.suspension_axis = vec3(0,-1,0);
            wheel.is_left_side = is_left;
            wheel.is_front_side = is_front;
            wheel.is_road_wheel = true;
            wheel.can_contact_ground = true;
            wheel.driven = true; //power_split_front decides how much of the engine actually reaches this axle, not this flag
            wheel.steerable = is_front;
            wheels.push_back(wheel);
        }
    }
}
