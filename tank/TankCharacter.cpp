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
    //Accelerate/Reverse/Brake/SteerLeft/SteerRight this tick from a held key, and let steering
    //converge back toward 0 - both shared with any other Vehicle now, see core/Vehicle.cpp.
    ApplyHoldLatches();
    DecaySteering();

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
        //Built from the basis rather than read off ref_left, which is misnamed and points
        //right - see Wheel::is_left_side. Only the naming was ever at stake for this particular
        //vector (it feeds a slip-opposing force, so its sign cancels), but having "left" mean
        //left in the one function that also decides which track to drive is the point.
        vec3 left = up.cross(forward);
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
        for (Wheel& wheel : wheels){
            if (wheel.is_left_side){ num_left++; }else{ num_right++; }
        }

        //Real tracks are a closed loop, so every wheel on a side - road wheel, idler, drive
        //sprocket alike - has the same TRACK SPEED past it. What's shared between them is
        //therefore a distance in metres, not an angle: a smaller wheel covering the same
        //ground has to spin faster, by exactly the ratio of the radii. Accumulated here from
        //whichever wheels computed their own contact velocity this tick (ContactResult::active,
        //see core/Wheel.h), then handed to the ones that didn't (airborne, or with the ray
        //missing) in the visual-follow loop below, where each divides it by its OWN radius.
        float track_distance_sum[2] = {0.0f,0.0f};   //[0]=left,[1]=right, metres this tick
        int track_distance_count[2] = {0,0};

        for (Wheel& wheel : wheels){
            //Tangential diagnostics this loop owns - WheelSuspension::UpdateContact resets its
            //own (point_speed/compression_rate/spring_force/friction_budget) itself.
            wheel.drive_force = 0.0f;
            wheel.longitudinal_force = 0.0f;
            wheel.lateral_force = 0.0f;
            wheel.friction_saturated = false;

            //The suspension/normal-force half of this wheel's physics - raycast, compression,
            //spring+damper, and (if it produced real force this tick) the wheel's own roll -
            //is entirely shared with any other vehicle now; see core/Wheel.h/.cpp. Everything
            //below this call is tank-specific: differential-track drive and the friction-circle
            //clamp would look different on a steered car, so that part stays here.
            WheelTuning tuning = ResolveTuning(wheel);
            WheelSuspension::ContactResult contact = WheelSuspension::UpdateContact(
                wheel,tuning,physics,body_world_pos,rotation,com_world,velocity,angular_velocity,forward,timestep);

            if (!contact.active){
                continue;
            }
            int side = wheel.is_left_side ? 0 : 1;
            track_distance_sum[side] += contact.roll_distance;
            track_distance_count[side]++;

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
            if (wheel.driven && !drive_capped && command != 0.0f && side_count > 0){
                drive_force = (engine_force / side_count) * command;
            }else{
                grip_force = -contact.point_velocity.dot(forward) * tuning.lateral_friction;
            }
            //Lateral: oppose sideways slip so the tank doesn't slide sideways indefinitely,
            //while still allowing the scrub a real tank has when pivoting on its tracks. This
            //is velocity-proportional damping, not a hard no-slip constraint, so some scrub
            //always gets through even before the budget below trims it.
            float longitudinal_force = drive_force + grip_force;
            float lateral_force = -contact.point_velocity.dot(left) * tuning.lateral_friction;

            //The friction circle: it's the COMBINED tangential demand that has to fit inside
            //the budget (wheel.friction_budget, written by UpdateContact just above), not each
            //axis independently - a wheel already spending everything it has on forward thrust
            //has nothing left to resist a sideways slide with, which is what makes a hard-
            //accelerating vehicle slide wide instead of gripping. Scaling both axes by the same
            //factor preserves the direction of the force while bringing its magnitude down to
            //what the contact can actually deliver.
            float demand = sqrtf(longitudinal_force * longitudinal_force + lateral_force * lateral_force);
            if (demand > wheel.friction_budget){
                //demand > budget >= 0 implies demand > 0, so this can't divide by zero.
                float scale = wheel.friction_budget / demand;
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
            physics->AddWorldForceAt(forward * longitudinal_force + left * lateral_force,contact.mount_world);
        }

        //Drive each wheel's visual (if any - see ApplicationTank::Init) from the same
        //compression/roll_angle this tick just computed above, instead of leaving it fixed at
        //its mount point - see WheelSuspension::UpdateVisual.
        float avg_track_distance[2] = {
            track_distance_count[0] > 0 ? track_distance_sum[0] / track_distance_count[0] : 0.0f,
            track_distance_count[1] > 0 ? track_distance_sum[1] / track_distance_count[1] : 0.0f,
        };
        for (Wheel& wheel : wheels){
            WheelTuning tuning = ResolveTuning(wheel);
            //Exactly the wheels that reached the spin code inside UpdateContact: it sits after
            //the ray-missed and compression<=0 bail-outs, so this reconstructs "computed its
            //own roll this tick" without carrying a parallel flag around for it (ContactResult
            //doesn't survive past the loop above). Everything else - airborne, ray missed,
            //contact disabled - is a real track's own closed loop still dragging it around at
            //the track's speed, NOT freewheeling independently the way a car's wheel would (see
            //Wheel::angular_velocity) - overridden here rather than left alone for exactly that
            //reason, before WheelSuspension::UpdateVisual integrates roll_angle from it below.
            bool rolled_itself = wheel.can_contact_ground && wheel.grounded && wheel.compression > 0.0f;
            if (!rolled_itself && tuning.radius > 0.0f && timestep > 0.0f){
                wheel.angular_velocity = avg_track_distance[wheel.is_left_side ? 0 : 1] / (timestep * tuning.radius);
            }
            WheelSuspension::UpdateVisual(wheel,tuning,timestep);
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

    //Permanent idle brake/rolling resistance: without it, releasing all controls would leave the tank coasting at constant velocity forever.
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

    //Spring the barrel back towards rest after a shot - see Fire().
    if (turret){
        if (!turret_rest_pos_captured){
            turret_rest_local_pos = turret->GetPosition(STATE_ACCESS_PHYSICS);
            turret_rest_pos_captured = true;
        }
        if (turret_recoil_offset > 0.0f){
            turret_recoil_offset = max(turret_recoil_offset - turret_recoil_recover_speed * timestep,0.0f);
            turret->SetPosition(turret_rest_local_pos - turret->GetForward(STATE_ACCESS_PHYSICS) * turret_recoil_offset);
        }
    }

    Object::UpdatePhysicsState();
}

void TankCharacter::Fire(){
    if (!turret){
        return;
    }
    turret_recoil_offset = turret_recoil_kick;

    if (Physics* physics = GetPhysics()){
        //Same "forces/velocity changes do nothing to a sleeping body" issue as gas/brake/steer -
        //see UpdatePhysicsState's comment on AddLocalForce - so wake it first.
        physics->WakeUp();
        vec3 kick_dir = -turret->GetWorldForward(STATE_ACCESS_PHYSICS);
        kick_dir.y = 0; //Same yaw-only convention as the turret tracking above.
        if (kick_dir.length() > 0.0001f){
            kick_dir.normalize();
        }
        physics->SetVelocity(physics->GetVelocity() + kick_dir * recoil_kick_speed);
    }
}

void TankCharacter::ResetState(const vec3& pos,const quat& rot){
    Vehicle::ResetState(pos,rot);
    turret_recoil_offset = 0.0f;
}

void TankCharacter::SetupWheels(float track_offset_x,float half_length,float mount_height,int wheels_per_side){
    wheels.clear();
    wheels_per_side = max(wheels_per_side,1);
    for (int side = 0; side < 2; side++){
        bool is_left = (side == 0);
        //-X = left: this engine is right-handed with forward = -Z and up = +Y, so the driver's
        //left is up.cross(forward) = -X. See Wheel::is_left_side - Object::ref_left is misnamed
        //and points the other way, and taking it at face value here is what put left_command on
        //the right-hand track and reversed the tank's steering.
        float x = is_left ? -track_offset_x : track_offset_x;
        for (int i = 0; i < wheels_per_side; i++){
            float t = (wheels_per_side == 1) ? 0.5f : (float)i / (float)(wheels_per_side - 1);
            float z = fmap(t,0.0f,1.0f,-half_length,half_length);
            Wheel wheel;
            wheel.local_offset = vec3(x,mount_height,z);
            wheel.is_left_side = is_left;
            //Road wheels: plain vertical struts on the shared suspension tuning. radius,
            //rest_length and travel are deliberately left at 0 = "inherit", so the character-
            //level values stay the single place to tune all six of them at once; the debug UI
            //displays what they resolve to and only writes a per-wheel override once dragged.
            wheel.suspension_axis = vec3(0,-1,0);
            wheel.is_road_wheel = true;
            wheel.can_contact_ground = true;
            wheel.driven = true;
            wheels.push_back(wheel);
        }
    }
}

//Appends rather than clears (unlike SetupWheels) - call after SetupWheels, once per raised
//wheel position, so the road wheels it already laid out stay untouched.
void TankCharacter::AddRaisedWheel(float track_offset_x,float z,float hub_rest_height,
                                   float rest_length,float travel,const vec3& axis,float radius){
    vec3 unit_axis = axis;
    float axis_length = unit_axis.length();
    unit_axis = (axis_length > 0.0001f) ? unit_axis * (1.0f / axis_length) : vec3(0,-1,0);

    for (int side = 0; side < 2; side++){
        bool is_left = (side == 0);
        float x = is_left ? -track_offset_x : track_offset_x; //-X = left, see SetupWheels
        Wheel wheel;
        //The caller states where the hub should REST; the anchor is wherever that puts it,
        //back up the suspension axis. For an angled axis that shifts the anchor in Z as well
        //as Y, which is the point - the strut leans, so its top isn't above its bottom.
        wheel.local_offset = vec3(x,hub_rest_height,z) - unit_axis * rest_length;
        wheel.suspension_axis = unit_axis;
        wheel.radius = radius; //0 = inherit wheel_radius, same as the road wheels
        wheel.rest_length = rest_length;
        //Set explicitly, and much shorter than the road wheels' - not a style choice. A ray is
        //sized rest_length + travel + radius, so travel is what governs how far BELOW a wheel
        //it still reaches, and an over-generous one is what would let these find flat ground
        //from up here. At the road wheels' shared 0.08 the rear sprocket - the lower of the
        //two, its tread resting 0.0658 m up - would have overshot the terrain by 0.0097 m and
        //hit it on level going every tick. That produces no force (the compression comes out
        //negative and clamps to 0) but it does report grounded, which is exactly the kind of
        //contact that reads as real in telemetry and isn't. At 0.02 it clears by 0.0469 m.
        wheel.travel = travel;
        wheel.is_left_side = is_left;
        wheel.is_road_wheel = false;
        //Contact-capable, but not driven: these sit clear of flat ground and only find terrain
        //when there is something raised to find, so they add suspension force and passive grip
        //where it exists without changing anything on level going. Thrust stays with the road
        //wheels until the per-side share counts grounded wheels rather than mounted ones.
        wheel.can_contact_ground = true;
        wheel.driven = false;
        wheels.push_back(wheel);
    }
}
