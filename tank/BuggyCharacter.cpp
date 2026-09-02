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

void BuggyCharacter::UpdatePhysicsState(){
    float timestep = 0.02f;

    ApplyHoldLatches();
    DecaySteering();

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;
    float max_steer_angle = max_steer_angle_degrees * TYPE_PI / 180.0f;

    if (Physics* physics = GetPhysics()){
        if (gas_pedal > 0.0f || brake_pedal > 0.0f || steering_position != 0.0f){
            physics->WakeUp(); //same "sleeping body ignores forces" issue as TankCharacter - see its UpdatePhysicsState
        }

        vec3 velocity = physics->GetVelocity();
        vec3 angular_velocity = physics->GetAngularVelocity();
        quat rotation = physics->GetBodyWorldOrientation(); //fresh this tick, unlike GetRotation() - see TankCharacter's comment
        vec3 forward = rotation * ref_forward;
        vec3 up = rotation * ref_up;
        vec3 left = up.cross(forward); //genuinely -X; ref_left is misnamed - see Wheel::is_left_side
        vec3 body_world_pos = physics->GetBodyWorldPosition();
        vec3 com_world = body_world_pos + rotation * physics->GetCenterofMass(); //lever arms measured from here, not the origin - see TankCharacter's comment on why
        forward_speed = velocity.dot(forward);

        //Braking is applied per-wheel further down (see brake_longitudinal below), through the
        //same friction-circle budget as everything else a tire does - not here as one whole-body
        //force at the centre of mass. A flat force at the COM ignored whether any wheel actually
        //had the grip to back it up, so it could brake exactly as hard on ice as on asphalt;
        //going through each wheel's own friction_budget means heavy braking can now saturate and
        //lock a wheel up (see the friction_saturated block below), same as too much throttle can.

        bool drive_capped = fabs(forward_speed) >= top_speed;
        float drive_command = reverse_multiplier * gas_pedal;

        int num_front_driven = 0, num_rear_driven = 0;
        for (Wheel& wheel : wheels){
            if (!wheel.driven){ continue; }
            if (wheel.is_front_side){ num_front_driven++; }else{ num_rear_driven++; }
        }

        int i = 0;
        for (Wheel& wheel : wheels){

            wheel.drive_force = 0.0f;
            wheel.longitudinal_force = 0.0f;
            wheel.lateral_force = 0.0f;
            wheel.friction_saturated = false;

            //A steered front wheel rolls and grips along its OWN axis, not the body's - real
            //tire forces act along the tire's own rolling/slip directions. Rotating forward/left
            //around the body's up axis by the wheel's own steer_angle gives that basis; rear
            //wheels (steerable == false) just use the body's own forward/left unchanged.
            vec3 wheel_forward = forward;
            vec3 wheel_left = left;
            if (wheel.steerable){
                //Negated: steering_position is negative for left (see Vehicle::SteerLeft), but a
                //LEFT turn is a POSITIVE rotation about +up here - quat(+Y,t) sends forward
                //(0,0,-1) to (-sin t,0,-cos t), i.e. toward -X, which is the driver's left. Take
                //steering_position's sign directly and the front wheels point the wrong way,
                //which was the most visible half of the reversed steering this fixes.
                wheel.steer_angle = -steering_position * max_steer_angle;
                if (fabs(wheel.steer_angle) > 0.0001f){
                    quat steer(up,wheel.steer_angle);
                    wheel_forward = steer * forward;
                    wheel_left = steer * left;
                }
            }

            //The suspension/normal-force half of this wheel's physics is entirely shared with
            //TankCharacter - see core/Wheel.h/.cpp. wheel_forward (not the body's forward) is
            //what's passed in, so a steered front wheel's roll_angle/roll_distance reflect the
            //direction IT is actually rolling in, not the hull's heading.
            WheelTuning tuning = ResolveTuning(wheel);
            WheelSuspension::ContactResult contact = WheelSuspension::UpdateContact(
                wheel,tuning,physics,body_world_pos,rotation,com_world,velocity,angular_velocity,wheel_forward,timestep);

            if (!contact.active){
                //A driven wheel with no ground contact still has an engine trying to spin it -
                //revs it up toward its own free-spin limit (top_speed through this wheel's own
                //radius) instead of leaving it dead in the air. WheelSuspension::UpdateVisual
                //integrates roll_angle from whatever angular_velocity ends up here regardless of
                //contact state - see Wheel::angular_velocity.
                if (wheel.driven && tuning.radius > 0.0f){
                    if (drive_command != 0.0f){
                        float free_spin_target = -drive_command * (top_speed / tuning.radius);
                        float max_step = free_spin_acceleration * timestep;
                        wheel.angular_velocity += clamp(free_spin_target - wheel.angular_velocity,-max_step,max_step);
                    }else{
                        float free_spin_target = 0.0f;
                        float max_step = free_spin_deceleration* timestep;
                        wheel.angular_velocity += clamp(free_spin_target - wheel.angular_velocity,-max_step,max_step);
                    }
                }
                continue;
            }

            //Longitudinal: engine thrust while this axle has a share of power and isn't capped,
            //passive grip otherwise - same reasoning as TankCharacter's own drive/grip split.
            //The passive branch is skipped entirely for a STEERED wheel, though (unlike
            //TankCharacter, which never rotates wheel_forward/wheel_left at all): grip_force and
            //lateral_force both scale the same tuning.lateral_friction along wheel_forward/
            //wheel_left, an orthonormal basis for the horizontal plane regardless of how far the
            //steer angle has rotated it - so applying BOTH reconstructs exactly
            //-lateral_friction*point_velocity, the same isotropic drag you'd get with no rotation
            //at all. That made the steer angle a complete no-op for a coasting front wheel's net
            //push on the chassis (only lateral_force's own, unmatched contribution actually
            //survived) - which is exactly why steering did nothing in RWD, where the front axle
            //is never in the driven branch. Leaving longitudinal_force at 0 here instead - relying
            //on lateral_force alone - is also the physically-right call for a wheel that's assumed
            //to roll without slip along its own axis anyway (see Wheel::angular_velocity): a
            //freely-rolling wheel shouldn't resist forward motion, only sideways slip.
            float drive_force = 0.0f;
            float grip_force = 0.0f;
            float axle_fraction = wheel.is_front_side ? power_split_front : (1.0f - power_split_front);
            int axle_driven_count = wheel.is_front_side ? num_front_driven : num_rear_driven;
            if (wheel.driven && axle_fraction > 0.0f && !drive_capped && drive_command != 0.0f && axle_driven_count > 0){
                drive_force = (engine_force * axle_fraction / axle_driven_count) * drive_command;
            }else if (!wheel.steerable){
                //tuning.rolling_resistance, NOT lateral_friction - a coasting wheel isn't
                //slipping (see this block's own comment above), so resisting it with the SAME
                //coefficient real sideways grip uses was dragging it down to nearly its full
                //friction budget on every undriven axle (e.g. the rear in FWD), far more braking
                //than an actually free-rolling wheel should produce.
                grip_force = -contact.point_velocity.dot(wheel_forward) * tuning.rolling_resistance;
            }

            //Braking - every wheel gets an even share of brake_force, regardless of driven/
            //steerable/axle, opposing whichever way THIS wheel's own contact point is currently
            //rolling. A constant force (sign of the wheel's own rolling direction), not a
            //velocity-proportional damper like grip_force/rolling_resistance above - same as a
            //real brake caliper clamping with roughly constant torque rather than one that eases
            //off as the wheel slows. Added into longitudinal_force BEFORE the friction-circle
            //clamp below, so it draws on the same grip budget as drive/grip/lateral - brake hard
            //enough on a wheel with little grip left and it saturates and locks, same as
            //overpowering the throttle does (see the friction_saturated block further down).
            float brake_longitudinal = 0.0f;
            if (brake_pedal > 0.0f){
                float wheel_speed_along_axis = contact.point_velocity.dot(wheel_forward);
                if (fabs(wheel_speed_along_axis) > 0.01f){
                    float per_wheel_brake_force = brake_force / (float)wheels.size();
                    brake_longitudinal = -(wheel_speed_along_axis > 0.0f ? 1.0f : -1.0f) * brake_pedal * per_wheel_brake_force;
                }
            }

            float longitudinal_force = drive_force + grip_force + brake_longitudinal;
            float lateral_force = -contact.point_velocity.dot(wheel_left) * tuning.lateral_friction;

            //Friction circle - same combined-demand clamp as TankCharacter, against this
            //wheel's own friction_budget (written by UpdateContact just above).
            float demand = sqrtf(longitudinal_force * longitudinal_force + lateral_force * lateral_force);
            if (demand > wheel.friction_budget){
                float scale = wheel.friction_budget / demand;
                longitudinal_force *= scale;
                lateral_force *= scale;
                drive_force *= scale;
                grip_force *= scale;
                wheel.friction_saturated = true;
            }
            wheel.drive_force = drive_force;
            wheel.longitudinal_force = longitudinal_force; //the actual applied along-wheel force (drive OR grip, whichever this tick used) - was reading back just grip_force, so a driven wheel's own longitudinal_force always showed 0 even while drive_force was nonzero
            wheel.lateral_force = lateral_force;

            //Rolling without slip, UNLESS saturated - WheelSuspension::UpdateContact no longer
            //sets wheel.angular_velocity itself (see its own comment: a caller-side saturation
            //check needs to blend away from ground-matched instead of snapping to it every tick,
            //and a write there would just get overwritten again next tick before that blend could
            //ever accumulate). Saturated grip means the tire can't actually supply the demanded
            //force, so it isn't genuinely rolling without slip any more - nudge angular_velocity
            //away from ground-matched toward whichever way the excess demand points, reusing the
            //same free-spin machinery as an airborne driven wheel (see the !contact.active branch
            //above) - saturated grip is, physically, the same "not fully coupled to the ground"
            //situation. Driven-and-saturated means the engine is asking for more thrust than the
            //tire can deliver, so it spins UP (wheelspin); saturated any other way means the
            //demand was mostly lateral (a steered wheel can't be longitudinally saturated any
            //more - see the branch above that computes grip_force only for non-steerable wheels),
            //so it's sliding rather than rolling and spins DOWN toward locked instead.
            if (tuning.radius > 0.0f){
                if (!wheel.friction_saturated){
                    wheel.angular_velocity = -contact.point_velocity.dot(wheel_forward) / tuning.radius;
                }else if (drive_force != 0.0f){
                    float free_spin_target = -drive_command * (top_speed / tuning.radius);
                    float max_step = free_spin_acceleration * timestep;
                    //debug->Info("Wheel is slipping . Max Step: %.2f Target = %.2f\n",max_step,free_spin_target);
                    wheel.angular_velocity += clamp(free_spin_target - wheel.angular_velocity,-max_step,max_step);
                }else{
                    float max_step = free_spin_deceleration * timestep;
                    wheel.angular_velocity += clamp(-wheel.angular_velocity,-max_step,max_step);
                }
            }

            //debug->Info("Wheel %i : Lateral Force = %.1f N, Longitudinal Force = %.1f N\n", i,lateral_force,longitudinal_force);
            //debug->Info("Wheel Left: %.3f, %.3f, %.3f\n", wheel_left.x,wheel_left.y,wheel_left.z);
            //ground_contact_world, NOT mount_world (the anchor) - the anchor sits close to (even
            //above) the chassis's own centre of mass, giving a horizontal force applied there
            //almost no lever arm for pitch. Down at the real contact patch, hard acceleration on
            //a grippy rear axle can now actually torque the nose up (or braking dip it down) -
            //see ContactResult::ground_contact_world's own comment. The NORMAL (spring) force a
            //few lines up in WheelSuspension::UpdateContact still uses the anchor - that one's
            //still deliberately deferred, see its own TODO.
            physics->AddWorldForceAt(wheel_forward * longitudinal_force + wheel_left * lateral_force,contact.ground_contact_world);
            i++;
        }

        //Visual follow: bob/spin from WheelSuspension::UpdateVisual, then layer the steer yaw on
        //top for the front wheels - UpdateVisual itself stays vehicle-agnostic (the tank never
        //needs a yaw component), so that part is applied here instead. Unlike the tank's tracks,
        //each buggy wheel rolls independently - there's no shared belt to keep an airborne wheel
        //in step with, so a wheel that didn't compute its own contact this tick simply freewheels
        //at its last angular_velocity instead (see Wheel's own comment on that field).
        for (Wheel& wheel : wheels){
            WheelTuning tuning = ResolveTuning(wheel);
            WheelSuspension::UpdateVisual(wheel,tuning,timestep);
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
        //debug->Info("Angular Velocity: %.3f, %.3f, %.3f\n",angvel_now.x,angvel_now.y,angvel_now.z);
        float yaw_rate = angvel_now.dot(up);
        vec3 tilt_rate = angvel_now - up * yaw_rate;
        float tilt_speed = tilt_rate.length();
        if (tilt_speed > max_roll_speed){
            physics->SetAngularVelocity(up * yaw_rate + tilt_rate * (max_roll_speed / tilt_speed));
        }
    }

    //Permanent idle brake/rolling resistance, same reasoning as TankCharacter's own.
    //brake_pedal = 0.0f;
    //gas_pedal = 0.0f;

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
