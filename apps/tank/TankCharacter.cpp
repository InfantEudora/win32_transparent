#include "TankCharacter.h"
#include "type_helpers.h"
#include <cmath>
#include "Debug.h"

static Debugger *debug = new Debugger("Tank", DEBUG_ALL);

TankCharacter::TankCharacter(){
    top_speed = 1.0f;
    engine_force = 2000.0f; //Newtons
    brake_force = 3000.0f;  //Newtons
}

TankCharacter::~TankCharacter(){
}

void TankCharacter::UpdatePhysicsState(){
    //The real simulation timestep, pushed in by Scene::UpdatePhysics - not a guess. Used below
    //for the wheel readback, the drive-force governor and the turret slew.
    float timestep = physics_timestep;

    //Let steering converge back toward 0 - shared with any other Vehicle, see core/Vehicle.cpp.
    //Nothing re-asserts input here any more: scripted holds are ordinary input events now, applied
    //in RunSimulationTick with the keyboard and gamepad (see InputController::HoldAxis).
    DecaySteering();

    float reverse_multiplier = f_reverse ? -1.0f : 1.0f;

    //The wheels are simulated by the rp3d VehicleConstraint on the hull (see Vehicle::
    //CreateVehicleConstraint): each is a raycast contact with a spring+damper along the ground
    //normal and Coulomb tire friction along and across the track, solved implicitly together
    //with everything else in the world. What is decided HERE, every tick, is only the
    //drivetrain: the drive and brake torque each wheel gets from the pedals and the steering.
    //A tank's tracks apply propulsion (and, incidentally, most of its weight support) at several
    //points along its own length rather than through its centre of mass, so that's modelled
    //with one contact per road wheel instead of approximating it with one force/torque through
    //the middle.
    Physics* physics = GetPhysics();
    if (physics && rp_vehicle){
        //Forces applied to a sleeping rigidbody appear to do nothing (confirmed empirically:
        //two AddLocalForce calls in a row left velocity at exactly zero and is_sleeping still true).
        //Any actual input intent needs to wake it first, or the tank goes completely unresponsive
        //the moment it's sat still long enough for rp3d to put it to sleep - not just an issue for
        //scripted/MCP control, this would affect normal keyboard play too.
        //throttle_input/steer_input and the direct track commands were missing from this test:
        //on a gamepad they are the only thing driving the tank, so a hull rp3d had put to sleep
        //stayed asleep under a stick that was pushed but never touched a pedal. Direct track
        //control is exactly that case, so what is tested here is the commands, not the pedals
        //alone.
        if (gas_pedal > 0.0f || brake_pedal > 0.0f || steering_position != 0.0f ||
            throttle_input != 0.0f || steer_input != 0.0f ||
            (direct_track_control && (left_track_input != 0.0f || right_track_input != 0.0f))){
            physics->WakeUp();
        }

        //Orientation read straight off the rigidbody rather than through GetRotation()/
        //GetForward()/GetUp(): those all return the object's ObjectState rotation, which
        //Object::UpdatePhysicsState() only refreshes at the END of this function, so they are
        //a full tick behind. The tank is a root object with no parent, so local == world.
        quat rotation = physics->GetBodyWorldOrientation();
        vec3 forward = rotation * ref_forward;
        vec3 up = rotation * ref_up;
        vec3 velocity = physics->GetVelocity();
        forward_speed = velocity.dot(forward);

        //What the constraint found on the step that just ran (Scene::UpdatePhysics steps the
        //world BEFORE calling this): contact, compression, forces, wheel spin - into the Wheel
        //diagnostics the debug UI and telemetry read. Then hand the constraint the wheels'
        //current geometry/tuning for the next step, so a debug-UI drag applies immediately.
        ReadBackWheels(timestep);
        SyncWheelSettings();

        //Two levers, one per track - which is what a real tank actually has, and what this now
        //models. Each lever is a SIGNED command for its own side: forward, neutral, or pulled
        //back. throttle/steer are just the driver-facing way of naming the pair, mixed here:
        //
        //    left  = throttle + steer      right = throttle - steer
        //
        //Every position the levers can take is reachable, which is the point:
        //  both levers forward    throttle 1.0, steer 0.0  -> left +1, right +1  straight ahead
        //  gentle bend right      throttle 0.9, steer 0.1  -> left +1, right +0.8
        //  left lever only        throttle 0.5, steer 0.5  -> left +1, right  0  swings right
        //  levers opposed         throttle 0.0, steer 1.0  -> left +1, right -1  pivot in place
        //
        //The previous mixing multiplied BOTH sides by the steering magnitude, so with steer at 0
        //both commands were zero and with steer at full one side was always zero - which is why
        //only straight-ahead and pivot-in-place were reachable and nothing in between.
        //
        //Clamped per side rather than rescaled together: a lever hits its stop, and pushing the
        //other one further is how you get more turn. That means asking for throttle 1.0 AND steer
        //1.0 saturates the outer track and turns less sharply than from a standstill - which is
        //also how the real thing behaves.
        //With direct_track_control the mix above is skipped entirely and each track takes its
        //own stick straight through (see TankCharacter.h). Nothing downstream changes: these are
        //the same signed per-side commands the mix would have produced, so the governor, the
        //track drag and the brakes all treat them identically.
        float left_command, right_command;
        if (direct_track_control){
            left_command  = clamp(left_track_input,-1.0f,1.0f);
            right_command = clamp(right_track_input,-1.0f,1.0f);
        }else{
            left_command  = clamp(throttle_input + steer_input,-1.0f,1.0f);
            right_command = clamp(throttle_input - steer_input,-1.0f,1.0f);
        }

        //Each side's engine share is split over the wheels that actually take drive (the road
        //wheels - the raised idler/sprocket are contact-capable but undriven, see AddRaisedWheel).
        int num_left_driven = 0, num_right_driven = 0;
        for (Wheel& wheel : wheels){
            if (!wheel.driven){ continue; }
            if (wheel.is_left_side){ num_left_driven++; }else{ num_right_driven++; }
        }

        float wheel_count = wheels.empty() ? 1.0f : (float)wheels.size();
        float brake_force_per_wheel = brake_pedal * brake_force / wheel_count;

        size_t count = min(wheels.size(),(size_t)rp_vehicle->getNbWheels());
        for (size_t i = 0; i < count; i++){
            Wheel& wheel = wheels[i];
            WheelTuning tuning = ResolveTuning(wheel);
            rp3d::VehicleWheel& w = rp_vehicle->getWheel((rp3d::uint32)i);

            //Engine thrust while this side is driven and the hull is below top speed. Passive
            //grip when it isn't is the tire's own business now: a free wheel on the ground rolls
            //without slipping, so it neither pushes nor drags (a real track doesn't slip
            //lengthwise on its own, so a hull left on a slope is held by the idle brake below
            //rather than rolling away).
            //
            //The engine is also rev-limited at the track (Vehicle::GovernedDriveForce): a track
            //spinning under more torque than its grip can take (engine_force is ~2.5x the
            //traction limit, see friction_coefficient) would otherwise wind up without bound,
            //and the spin it stores then keeps pushing the hull past top_speed after the
            //throttle is cut - and in a pivot turn, where the hull's forward speed stays 0 and
            //drive_capped never trips, nothing else would limit how fast the tracks (and so
            //the hull) spin at all. With it, a pivot turns at the tracks' kinematic rate,
            //top_speed / half the track spacing.
            float command = wheel.is_left_side ? left_command : right_command;
            int side_count = wheel.is_left_side ? num_left_driven : num_right_driven;
            float drive_force = 0.0f;
            //No whole-vehicle speed cap here any more. GovernedDriveForce already limits each
            //wheel to top_speed in the direction IT was commanded, which a single hull-speed test
            //cannot do: at top speed that test cut all drive torque, so a tank at speed lost its
            //differential entirely and could not turn at all until it slowed down.
            if (wheel.driven && command != 0.0f && side_count > 0){
                drive_force = GovernedDriveForce(wheel,tuning,(engine_force / side_count) * command,timestep,fabs(command));
            }

            //Track drag on the side whose lever is near neutral. Without it an undriven track
            //free-wheels, so "left lever forward, right lever neutral" barely turns - the hull
            //just pushes the loose track along. A real track always resists: its own tension, the
            //road wheel bearings, the ground it churns. That resistance is what lets one lever
            //swing the nose, and what brings the tank to rest when both come back to neutral.
            //
            //Applied as a retarding DRIVE torque, NOT as a brake torque. In rp3d the brake torque
            //dominates the drive torque instead of summing with it, so routing this through
            //setBrakeTorque made even a fraction of a Newton stop the tire transmitting anything:
            //the track span up to top_speed, longitudinal force collapsed to zero and the tank
            //would only turn at exactly full lock, where the drag happened to be zero. Through the
            //drive channel the two simply add, as they should.
            //
            //Only once the wheel is actually turning - a static track is held by the tire's own
            //grip, and pushing a retarding force against a stationary wheel just makes it hunt.
            float drag = rolling_resistance * (1.0f - min(fabs(command),1.0f)) / wheel_count;
            if (fabs(wheel.angular_velocity) > 0.01f){
                //angular_velocity is NEGATIVE while rolling forward (see Wheel), and a positive
                //drive force rolls forward, so opposing the motion means taking its sign.
                drive_force += (wheel.angular_velocity > 0.0f ? 1.0f : -1.0f) * drag;
            }

            //Torque at the tread: force * radius. Positive rolls the vehicle forward (rp3d's
            //convention, and command's).
            w.setDriveTorque(drive_force * tuning.radius);
            w.setBrakeTorque(brake_force_per_wheel * tuning.radius);
            w.setSteerAngle(0.0f); //tracks never steer
        }

        //Real tracks are a closed loop, so every wheel on a side - road wheel, idler, drive
        //sprocket alike - has the same TRACK SPEED past it. What's shared between them is
        //therefore a surface speed in m/s, not an angle: a smaller wheel covering the same
        //ground has to spin faster, by exactly the ratio of the radii. Averaged here over the
        //wheels the constraint has on the ground this tick, then imposed on the ones it hasn't
        //(airborne, or clear of flat ground like the raised idler/sprocket), which would
        //otherwise freewheel independently the way a car's wheel does. Written back into the
        //constraint too, so a wheel that then touches down arrives already turning at track
        //speed instead of being yanked up to it by the tire.
        float track_speed_sum[2] = {0.0f,0.0f};   //[0]=left,[1]=right, m/s (this engine's sign, see Wheel::angular_velocity)
        int track_speed_count[2] = {0,0};
        for (Wheel& wheel : wheels){
            if (!wheel.grounded){ continue; }
            int side = wheel.is_left_side ? 0 : 1;
            track_speed_sum[side] += wheel.angular_velocity * ResolveTuning(wheel).radius;
            track_speed_count[side]++;
        }
        for (size_t i = 0; i < count; i++){
            Wheel& wheel = wheels[i];
            if (wheel.grounded){ continue; }
            int side = wheel.is_left_side ? 0 : 1;
            if (track_speed_count[side] == 0){ continue; } //whole side airborne: let them freewheel
            float radius = ResolveTuning(wheel).radius;
            if (radius <= 0.0f){ continue; }
            wheel.angular_velocity = (track_speed_sum[side] / track_speed_count[side]) / radius;
            rp_vehicle->getWheel((rp3d::uint32)i).setAngularVelocity(-wheel.angular_velocity);
        }

        //Drive each wheel's visual (if any - see ApplicationTank::Init) from the compression and
        //spin just read back, instead of leaving it fixed at its mount point.
        UpdateWheelVisuals(timestep);

        //Last-resort safety net against a roll/pitch excursion, applied after all of this tick's
        //physics is in so it catches one regardless of which contact caused it. Historically
        //this was load-bearing: several wheels could each push a bounded amount in the same
        //rotational direction for enough consecutive ticks to tip the hull to a stable rest ON
        //ITS SIDE. That had a specific cause (lever arms measured from the body origin rather
        //than the centre of mass) and it is fixed, so this should now be genuinely dead code in
        //any drivable configuration. If it starts engaging again, something upstream has
        //regressed; treat it as an alarm, not as the fix.
        //Yaw is deliberately exempt. This net used to clamp the magnitude of the WHOLE angular
        //velocity, on the stated assumption that nothing in normal play would reach it - but a
        //pivot-in-place turn is precisely an angular manoeuvre, and it pinned this limit exactly,
        //at 3.000 rad/s, while the actual roll rate was still only ~0.2. The safety net was
        //silently governing how fast the tank could steer. Splitting yaw out leaves steering to
        //be limited by track scrub, which the tire friction models, and leaves this net doing
        //only the job it was added for.
        vec3 angvel_now = physics->GetAngularVelocity();
        float yaw_rate = angvel_now.dot(up);
        vec3 tilt_rate = angvel_now - up * yaw_rate; //roll+pitch, with yaw projected out
        float tilt_speed = tilt_rate.length();

        if (tilt_speed > max_roll_speed){
            physics->SetAngularVelocity(up * yaw_rate + tilt_rate * (max_roll_speed / tilt_speed));
        }
    }

    if (turret && turret_target){
        //Same facing-angle-difference approach as PlayerCharacter::ComputeFacingAngles,
        //reimplemented here since that method lives on PlayerCharacter, not on Object.
        vec3 forward = turret->GetWorldForward();
        forward.y = 0;
        forward.normalize();
        float facing = atan2(forward.x,-forward.z) + TYPE_PI/2;

        vec3 to_target = turret_target->GetWorldPosition() - turret->GetWorldPosition();
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
            turret_rest_local_pos = turret->GetPosition();
            turret_rest_pos_captured = true;
        }
        if (turret_recoil_offset > 0.0f){
            turret_recoil_offset = max(turret_recoil_offset - turret_recoil_recover_speed * timestep,0.0f);
            turret->SetPosition(turret_rest_local_pos - turret->GetForward() * turret_recoil_offset);
        }
    }

    Object::UpdatePhysicsState();
}

void TankCharacter::ReleaseInputs(){
    Vehicle::ReleaseInputs();
    left_track_input = 0.0f;
    right_track_input = 0.0f;
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
        vec3 kick_dir = -turret->GetWorldForward();
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
            //level values stay the single place to tune all of them at once; the debug UI
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
        //Set explicitly, and much shorter than the road wheels' - not a style choice: it is
        //how far ABOVE its rest position the hub may rise before the hard stop, i.e. how much
        //of a step or ledge the idler/sprocket can ride up before it becomes a rigid contact.
        //A short arm matches what a tensioner actually has.
        wheel.travel = travel;
        wheel.is_left_side = is_left;
        wheel.is_road_wheel = false;
        //Contact-capable, but not driven: these sit clear of flat ground (their tread rests
        //above the road wheels' contact plane) and only find terrain when there is something
        //raised to find, so they add suspension force and grip where it exists without changing
        //anything on level going. Thrust stays with the road wheels.
        wheel.can_contact_ground = true;
        wheel.driven = false;
        wheels.push_back(wheel);
    }
}
