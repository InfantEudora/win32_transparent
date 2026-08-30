#include "TankCharacter.h"
#include "type_helpers.h"
#include <cmath>
#include "Debug.h"

static Debugger *debug = new Debugger("Tank", DEBUG_ALL);

TankCharacter::TankCharacter(){
    top_speed = 1.0f;
}

TankCharacter::~TankCharacter(){
}

void TankCharacter::UpdatePhysicsState(){
    float timestep = 0.02f; // conservative default; if called more frequently it's fine

    //Re-assert any still-active hold-latch, exactly as if RunLogic had just called
    //Accelerate/Reverse/Brake/SteerLeft/SteerRight this tick from a held key - see
    //HoldDrive/HoldBrake/HoldSteer below for why this exists.
    unsigned long long now_ms = GetTickCount64();
    if (now_ms < gas_latch_until_ms){
        if (gas_latch_reverse){
            Reverse(gas_latch_amount);
        }else{
            Accelerate(gas_latch_amount);
        }
    }
    if (now_ms < brake_latch_until_ms){
        Brake(brake_latch_amount);
    }
    if (now_ms < steer_latch_until_ms){
        if (steer_latch_amount < 0.0f){
            SteerLeft(-steer_latch_amount);
        }else{
            SteerRight(steer_latch_amount);
        }
    }

    //Steering converges toward 0.
    if (steering_position < 0){
        steering_position = clamp(steering_position + 0.05f,-1.0f,0.0f);
    }else if (steering_position > 0){
        steering_position = clamp(steering_position - 0.05f,0.0f,1.0f);
    }

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;

    //Everything below applies forces to the rigidbody via each wheel's raycast contact point -
    //no single rigid collider rests on the terrain, and steering is no longer a direct rotation.
    //A tank's tracks apply propulsion (and, incidentally, most of its weight support) at several
    //points along its own length rather than through its centre of mass, so that's modelled here
    //instead of approximating it with one force/torque through the middle.
    if (Physics* physics = GetPhysics()){
        //Forces applied to a sleeping rigidbody appear to do nothing (confirmed empirically:
        //two AddLocalForce calls in a row left velocity at exactly zero and is_sleeping still true).
        //Any actual input intent needs to wake it first, or the tank goes completely unresponsive
        //the moment it's sat still long enough for rp3d to put it to sleep - not just an issue for
        //scripted/MCP control, this would affect normal keyboard play too.
        if (gas_pedal > 0.0f || brake_pedal > 0.0f || steering_position != 0.0f){
            physics->WakeUp();
        }

        vec3 velocity = physics->GetVelocity();
        vec3 angular_velocity = physics->GetAngularVelocity();
        //Orientation read straight off the rigidbody rather than through GetRotation()/
        //GetForward()/GetUp()/GetLeft(): those all return state_physics.rotation, which
        //Object::UpdatePhysicsState() only refreshes at the END of this function, so they were
        //a full tick behind the body_world_pos read below. Mixing a current position with a
        //stale rotation puts every mount point on the wrong arc, and adds a tick of phase lag
        //to forces that are already integrated a tick late (Scene::UpdatePhysics steps the
        //world BEFORE calling this, so this tick's forces land on the next step) - phase lag
        //being the one thing a stiff velocity damper can least afford. Fine to use as the
        //local rotation too: the tank is a root object with no parent, so local == world.
        quat rotation = physics->GetBodyWorldOrientation();
        vec3 forward = rotation * ref_forward;
        vec3 up = rotation * ref_up;
        vec3 left = rotation * ref_left;
        vec3 body_world_pos = physics->GetBodyWorldPosition();
        //rp3d's body transform origin is NOT its centre of mass. The hull's box collider is
        //deliberately centred well above the hull origin (see ApplicationTank::Init, which
        //trims the box's bottom face up to the wheels' ground clearance), leaving the two
        //roughly half a metre apart vertically. Every lever arm below has to be measured from
        //the centre of mass, because that is the point the body actually rotates about - rp3d's
        //own applyWorldForceAtWorldPosition already computes its torque as
        //(point - centre_of_mass) x force, so anything computed here against the origin instead
        //disagrees with what the solver then does with it.
        //
        //Measuring from the origin (as this did) put r.y at about +0.12 where the truth is
        //about -0.34: wrong sign AND ~3x magnitude. The vertical component of a contact's point
        //velocity only depends on r.x/r.z, so the springs still behaved and the bug stayed
        //invisible there - but the LATERAL component during a roll is -w*r.y, so it came out
        //INVERTED. That put lateral_friction, the largest coefficient in the whole system, to
        //work pumping roll instead of damping it: a positive feedback loop present on every
        //single tick, whose direction is fixed by whichever way the hull first happens to tip.
        //The one-directional roll bias that ends with the hull resting on its side is this.
        vec3 com_world = body_world_pos + rotation * physics->GetCenterofMass();
        float forward_speed = velocity.dot(forward);

        //Braking: oppose whatever the current horizontal velocity actually is (not just facing),
        //same as a real brake would. Applied once at the centre of mass - it's resisting the
        //hull's actual motion, not something that needs a per-wheel breakdown. Applied at the
        //body ORIGIN (as it was) it acted ~0.5m below the centre of mass, so every brake
        //application also fed in a pitch torque of roughly force*0.5 N.m that nothing asked
        //for - including the permanent idle brake at the bottom of this function.
        vec3 horizontal_velocity = vec3(velocity.x,0,velocity.z);
        float horizontal_speed = horizontal_velocity.length();
        if (brake_pedal > 0.0f && horizontal_speed > 0.01f){
            vec3 brake_dir = horizontal_velocity * (-1.0f / horizontal_speed);
            physics->AddWorldForceAt(brake_dir * brake_pedal * brake_force,com_world);
        }

        //Differential drive: steering comes from the two tracks being pushed with different
        //(possibly opposite) force, not from directly rotating the hull. steering_position > 0
        //(SteerRight) biases the left track stronger and the right track weaker/reversed, which
        //swings the nose right - same as a real tank pivoting on its tracks. This works even
        //with gas_pedal at 0 (a pure pivot-in-place turn), since it doesn't depend on gas_pedal.
        float base_command = reverse_multiplier * gas_pedal;
        float left_command  = clamp(base_command + steering_position * steer_authority,-1.0f,1.0f);
        float right_command = clamp(base_command - steering_position * steer_authority,-1.0f,1.0f);
        bool drive_capped = fabs(forward_speed) >= top_speed;

        int num_left = 0, num_right = 0;
        for (TankWheel& wheel : wheels){
            if (wheel.is_left_side){ num_left++; }else{ num_right++; }
        }

        for (TankWheel& wheel : wheels){
            //Diagnostics are rewritten from scratch every tick (see TankWheel) - cleared up
            //front so a wheel that's airborne, or that bails out at one of the early continues
            //below, reports honest zeroes rather than whatever it last did while grounded.
            wheel.point_speed = 0.0f;
            wheel.compression_rate = 0.0f;
            wheel.spring_force = 0.0f;
            wheel.drive_force = 0.0f;
            wheel.longitudinal_force = 0.0f;
            wheel.lateral_force = 0.0f;
            wheel.friction_budget = 0.0f;
            wheel.friction_saturated = false;

            vec3 mount_world = body_world_pos + rotation * wheel.local_offset;
            //Start slightly above the mount point so a wheel that's already compressed past
            //rest_length at the start of this tick is still detected, not missed by starting
            //the ray exactly at (or below) the surface.
            const float start_margin = 0.05f;
            vec3 ray_start = mount_world + up * start_margin;
            float ray_length = start_margin + suspension_rest_length + suspension_travel;
            vec3 ray_end = ray_start - up * ray_length;

            PhysicsWorld::RaycastHit hit = physics->world->Raycast(ray_start,ray_end,physics->body->rigidbody);
            if (!hit.hit){
                wheel.grounded = false;
                wheel.compression = 0.0f;
                continue;
            }

            float clearance = (hit.point - ray_start).length() - start_margin;
            float compression = suspension_rest_length - clearance;
            wheel.grounded = true;
            wheel.compression = clamp(compression,0.0f,suspension_rest_length + suspension_travel);

            if (wheel.compression <= 0.0f){
                continue; //extended past rest length - a passive spring gives no force here
            }

            //Velocity of the hull material at this exact wheel's contact point (linear +
            //angular_velocity x r) so every force below reacts to how fast THIS point is
            //moving, not just the hull's overall velocity - matters once the hull starts
            //pitching/rolling. Clamped in magnitude before ANY force derives from it: all
            //three forces below (spring damping, longitudinal grip, lateral friction) feed
            //back into velocity/angular_velocity next tick, through this exact same lever arm.
            //With a wide multi-wheel track and a discrete timestep, that loop can amplify a
            //small initial asymmetry into a divergent oscillation tick over tick (confirmed
            //empirically: one wheel's damping term alone hit 1300+ N once its local
            //compression_rate - derived from this same point_velocity - reached 1.4 m/s).
            //Clamping point_velocity itself, once, is what actually breaks that loop - clamping
            //only one of the three forces that read it (as an earlier version of this code did)
            //left the other two just as able to run away.
            //Measured from the centre of mass, NOT the body origin - see com_world above for
            //why that distinction was inverting this contact's lateral response to roll.
            vec3 r = mount_world - com_world;
            vec3 point_velocity = velocity + angular_velocity.cross(r);
            float point_speed = point_velocity.length();
            wheel.point_speed = point_speed; //recorded PRE-clamp - see TankWheel::point_speed
            if (point_speed > max_point_speed){
                point_velocity = point_velocity * (max_point_speed / point_speed);
            }

            //Pushed along the actual ground normal, not the hull's own (possibly already
            //tilted) up vector - using the hull's up here would mean that once the hull is
            //tilted even slightly, the "corrective" force is ALSO tilted, extending the error
            //instead of fixing it (confirmed empirically: this is what let a small initial
            //asymmetry escalate into the hull settling on its side instead of upright, even
            //with the point_velocity/force clamps above already in place). The ground normal
            //has no such feedback - it only reflects the terrain, never the hull's own state.
            vec3 push_dir = hit.normal;
            float compression_rate = -point_velocity.dot(push_dir); //positive = moving further into the ground
            float spring_force = wheel.compression * suspension_stiffness + compression_rate * suspension_damping;
            //Hard ceiling regardless of the above: a wheel bearing its share of the tank's
            //weight should never need many times that just to support it, so anything past
            //max_wheel_force is almost certainly still a transient spike, not real load.
            spring_force = clamp(spring_force,0.0f,max_wheel_force);
            wheel.compression_rate = compression_rate;
            wheel.spring_force = spring_force;
            physics->AddWorldForceAt(push_dir * spring_force,mount_world);

            //Everything this contact does tangentially - engine thrust, passive grip,
            //resistance to sideways slip - is friction against the ground, so all of it draws
            //on one budget set by how hard this wheel is actually pressed down. spring_force IS
            //that normal load, already computed and clamped just above.
            float friction_budget = friction_coefficient * spring_force;
            wheel.friction_budget = friction_budget;

            //Longitudinal: engine thrust while this side is driven, passive grip when it isn't.
            //Exactly one of the two is ever non-zero, so they're tracked separately for
            //telemetry but share a single axis in the circle below.
            //
            //Grip exists because a raycast contact with no drive force and no friction of its
            //own would let gravity's component along a slope accelerate the hull downhill
            //forever - a real track doesn't slip lengthwise on its own. It's skipped while
            //actively driven so it doesn't simply fight the drive force.
            float drive_force = 0.0f;
            float grip_force = 0.0f;
            float command = wheel.is_left_side ? left_command : right_command;
            int side_count = wheel.is_left_side ? num_left : num_right;
            if (!drive_capped && command != 0.0f && side_count > 0){
                drive_force = (engine_force / side_count) * command;
            }else{
                grip_force = -point_velocity.dot(forward) * lateral_friction;
            }
            //Lateral: oppose sideways slip so the tank doesn't slide sideways indefinitely,
            //while still allowing the scrub a real tank has when pivoting on its tracks. This
            //is velocity-proportional damping, not a hard no-slip constraint, so some scrub
            //always gets through even before the budget below trims it.
            float longitudinal_force = drive_force + grip_force;
            float lateral_force = -point_velocity.dot(left) * lateral_friction;

            //The friction circle: it's the COMBINED tangential demand that has to fit inside
            //the budget, not each axis independently - a wheel already spending everything it
            //has on forward thrust has nothing left to resist a sideways slide with, which is
            //what makes a hard-accelerating vehicle slide wide instead of gripping. Scaling
            //both axes by the same factor preserves the direction of the force while bringing
            //its magnitude down to what the contact can actually deliver.
            float demand = sqrtf(longitudinal_force * longitudinal_force + lateral_force * lateral_force);
            if (demand > friction_budget){
                //demand > budget >= 0 implies demand > 0, so this can't divide by zero.
                float scale = friction_budget / demand;
                longitudinal_force *= scale;
                lateral_force *= scale;
                drive_force *= scale;
                grip_force *= scale;
                wheel.friction_saturated = true;
            }
            wheel.drive_force = drive_force;
            wheel.longitudinal_force = grip_force;
            wheel.lateral_force = lateral_force;

            //One call rather than three: same total force, and the two tangential components
            //are now a single vector that was scaled as a unit.
            physics->AddWorldForceAt(forward * longitudinal_force + left * lateral_force,mount_world);
        }

        //Last-resort safety net against a roll/pitch excursion, applied after all of this tick's
        //forces are in so it catches one regardless of which force caused it. Historically this
        //was load-bearing: several wheels could each push a bounded amount in the same
        //rotational direction for enough consecutive ticks to tip the hull to a stable rest ON
        //ITS SIDE. That had a specific cause (lever arms measured from the body origin rather
        //than the centre of mass - see com_world above) and it is fixed, so this should now be
        //genuinely dead code in any drivable configuration. If it starts engaging again,
        //something upstream has regressed; treat it as an alarm, not as the fix.
        //Yaw is deliberately exempt. This net used to clamp the magnitude of the WHOLE angular
        //velocity, on the stated assumption that nothing in normal play would reach it - but a
        //pivot-in-place turn is precisely an angular manoeuvre, and once the tracks had real
        //grip (see the friction budget above) it pinned this limit exactly, at 3.000 rad/s,
        //while the actual roll rate was still only ~0.2. The safety net was silently governing
        //how fast the tank could steer. Splitting yaw out leaves steering to be limited by
        //track scrub, which is now modelled, and leaves this net doing only the job it was
        //added for: catching a roll/pitch excursion no drivable configuration should produce.
        vec3 angvel_now = physics->GetAngularVelocity();
        float yaw_rate = angvel_now.dot(up);
        vec3 tilt_rate = angvel_now - up * yaw_rate; //roll+pitch, with yaw projected out
        float tilt_speed = tilt_rate.length();
        if (tilt_speed > max_roll_speed){
            physics->SetAngularVelocity(up * yaw_rate + tilt_rate * (max_roll_speed / tilt_speed));
        }
    }

    brake_pedal = 0.1f;
    gas_pedal = 0.0f;

    if (turret && turret_target){
        //Same facing-angle-difference approach as PlayerCharacter::ComputeFacingAngles,
        //reimplemented here since that method lives on PlayerCharacter, not on Object.
        vec3 forward = turret->GetWorldForward(STATE_ACCESS_PHYSICS);
        forward.y = 0;
        forward.normalize();
        float facing = atan2(forward.x,-forward.z) + TYPE_PI/2;

        vec3 to_target = turret_target->GetWorldPosition(STATE_ACCESS_PHYSICS) - turret->GetWorldPosition(STATE_ACCESS_PHYSICS);
        to_target.y = 0;
        to_target.normalize();
        float target_angle = atan2(to_target.x,-to_target.z) + TYPE_PI/2;

        //Normalize into [-PI, PI] so we always turn the short way round.
        float diff = fmod(target_angle - facing + 3 * TYPE_PI, 2 * TYPE_PI) - TYPE_PI;

        //Constant angular speed: rotate by a fixed step towards target, clamped so we don't overshoot.
        float max_step = turret_turn_speed * timestep;
        float turn_by = clamp(diff,-max_step,max_step);
        turret->RotateAroundAxis(vec3(0,1,0),-turn_by);
    }

    Object::UpdatePhysicsState();
}

void TankCharacter::Accelerate(float factor){
    //No "brake to stop first" state machine needed anymore - a real opposing force naturally
    //decelerates the hull before it starts moving the other way, same as an actual vehicle.
    f_reverse = false;
    gas_pedal = clamp(factor,0.0f,1.0f);
    brake_pedal = 0.0f;
}

void TankCharacter::Brake(float factor){
    brake_pedal = clamp(factor,0.0f,1.0f);
    gas_pedal = 0.0f;
}

void TankCharacter::SteerLeft(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position - delta,-1.0f,0.0f);
}

void TankCharacter::SteerRight(float factor){
    float delta = 0.10f * factor;
    steering_position = clamp(steering_position + delta,0.0f,1.0f);
}

void TankCharacter::Reverse(float factor){
    f_reverse = true;
    gas_pedal = clamp(factor,0.0f,1.0f);
    brake_pedal = 0.0f;
}

void TankCharacter::HoldDrive(bool reverse,float amount,float duration_ms){
    gas_latch_amount = clamp(amount,0.0f,1.0f);
    gas_latch_reverse = reverse;
    gas_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    //Apply immediately too, rather than waiting for the next tick's latch check.
    if (reverse){
        Reverse(gas_latch_amount);
    }else{
        Accelerate(gas_latch_amount);
    }
}

void TankCharacter::HoldBrake(float amount,float duration_ms){
    brake_latch_amount = clamp(amount,0.0f,1.0f);
    brake_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    Brake(brake_latch_amount);
}

void TankCharacter::HoldSteer(float signed_amount,float duration_ms){
    steer_latch_amount = clamp(signed_amount,-1.0f,1.0f);
    steer_latch_until_ms = GetTickCount64() + (unsigned long long)max(duration_ms,0.0f);
    if (steer_latch_amount < 0.0f){
        SteerLeft(-steer_latch_amount);
    }else{
        SteerRight(steer_latch_amount);
    }
}

void TankCharacter::ReleaseInputs(){
    gas_latch_until_ms = 0;
    brake_latch_until_ms = 0;
    steer_latch_until_ms = 0;
    Brake(0.0f); //releases both gas and brake pedals immediately
}

void TankCharacter::SetupWheels(float track_offset_x,float half_length,float mount_height,int wheels_per_side){
    wheels.clear();
    wheels_per_side = max(wheels_per_side,1);
    for (int side = 0; side < 2; side++){
        bool is_left = (side == 0);
        //+X = left, matching Object::GetLeft()/ref_left's convention.
        float x = is_left ? track_offset_x : -track_offset_x;
        for (int i = 0; i < wheels_per_side; i++){
            float t = (wheels_per_side == 1) ? 0.5f : (float)i / (float)(wheels_per_side - 1);
            float z = fmap(t,0.0f,1.0f,-half_length,half_length);
            TankWheel wheel;
            wheel.local_offset = vec3(x,mount_height,z);
            wheel.is_left_side = is_left;
            wheels.push_back(wheel);
        }
    }
}
