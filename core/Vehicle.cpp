#include "Vehicle.h"
#include "type_helpers.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Vehicle", DEBUG_ALL);

Vehicle::~Vehicle(){
    //Runs before Object's own destructor takes the rigidbody with it - destroying the body
    //would destroy the constraint too, and then this pointer would dangle.
    DestroyVehicleConstraint();
}

//A number -1 to +1
void Vehicle::ThrottleInput(float input){
    throttle_input = clamp(input,-1.0f,1.0f);
}
//A number -1 to +1
void Vehicle::SteerInput(float input){
    steer_input = clamp(input,-1.0f,1.0f);
}
//A number -1 to +1
void Vehicle::BrakeInput(float input){
    brake_input = clamp(abs(input),0.0f,1.0f);
}

void Vehicle::Accelerate(float factor){
    gas_pedal = clamp(factor,0.0f,1.0f);
}

void Vehicle::Brake(float factor){
    brake_pedal = clamp(factor,0.0f,1.0f);
}

//Steer left right move the steering position toward either -1 or +1
void Vehicle::SteerLeft(float factor){
    float delta = steering_speed * factor;
    steering_position = clamp(steering_position - delta,-1.0f,1.0f);
}

void Vehicle::SteerRight(float factor){
    float delta = steering_speed * factor;
    steering_position = clamp(steering_position + delta,-1.0f,1.0f);
}

void Vehicle::Reverse(bool reverse){
    f_reverse = reverse;
}

void Vehicle::ReleaseInputs(){
    //Both pedals, explicitly. This used to call Brake(0) alone in the belief that it released
    //the gas too - it doesn't, Brake() only writes brake_pedal - which the tank masked by
    //zeroing gas_pedal itself every tick, while the buggy kept driving on whatever throttle it
    //last had after every "stop".
    gas_pedal = 0.0f;
    brake_pedal = 0.0f;
    steer_input = 0.0f;
    throttle_input = 0.0f;
    brake_input = 0.0f;
}

//A rev limiter in force terms: the most drive force this wheel may get this tick without its
//own surface speed passing top_speed in the commanded direction. One tick of an unlimited
//engine_force on a light wheel adds far more spin than the tire can hand to the ground (the
//tank: ~90 rad/s a tick against a 14 rad/s limit), so a plain "no torque above top speed"
//check overshoots by a lot, and the stored spin then keeps pushing the vehicle past top_speed
//after the throttle is cut - and in a pivot turn, where the hull's forward speed stays 0,
//nothing else limits how fast the tracks spin at all. Clamping the torque to what reaches
//the limit exactly (I * dw / dt) puts the wheel at top_speed and holds it there.
float Vehicle::GovernedDriveForce(const Wheel& wheel,const WheelTuning& tuning,float requested_force,float timestep,float speed_fraction) const{
    if (requested_force == 0.0f || tuning.radius <= 0.0f || timestep <= 0.0f){
        return requested_force;
    }
    float direction = requested_force > 0.0f ? 1.0f : -1.0f;
    //Surface speed in the commanded direction - this engine's sign flipped, see Wheel::angular_velocity.
    float surface_speed = -wheel.angular_velocity * tuning.radius * direction;
    //The speed THIS control position is asking for, not just the vehicle's maximum. Governing
    //every wheel to the same top_speed regardless of how far its lever/pedal was pushed made the
    //control a direction switch and nothing more: the force limit below sits far under what the
    //engine asks for at almost any position (about 50 N against 333 N at full lever on the tank),
    //so every command from roughly 0.15 upwards produced the IDENTICAL force. Two tracks pushed
    //different amounts in the same direction therefore ended up at exactly the same speed, which
    //is why a tank could pivot on opposed levers but could not steer while driving - there was no
    //differential left to steer with.
    float target_speed = top_speed * clamp(fabs(speed_fraction),0.0f,1.0f);
    float headroom = target_speed - surface_speed; //m/s of surface speed still allowed
    if (headroom <= 0.0f){
        return 0.0f;
    }
    float inertia = 0.5f * tuning.mass * tuning.radius * tuning.radius;
    //Torque that spins the wheel up by exactly headroom/radius over this tick, as a force at the tread.
    float max_force = inertia * (headroom / tuning.radius) / timestep / tuning.radius;
    return direction * min(fabs(requested_force),max_force);
}

void Vehicle::DecaySteering(float step){
    if (steering_position < 0){
        steering_position = clamp(steering_position + step,-1.0f,0.0f);
    }else if (steering_position > 0){
        steering_position = clamp(steering_position - step,0.0f,1.0f);
    }
}

void Vehicle::ResetState(const vec3& pos,const quat& rot){
    ReleaseInputs();
    steering_position = 0.0f; //ReleaseInputs only clears the pedals - this normally decays
                               //toward 0 over several ticks (see DecaySteering), too slow for a
                               //reset that's supposed to be instant.
    f_reverse = false;

    SetPosition(pos);
    SetRotation(rot);

    if (Physics* physics = GetPhysics()){
        physics->SetVelocity(vec3());
        physics->SetAngularVelocity(vec3());
        physics->WakeUp(); //a sleeping body ignores this teleport's next tick of forces too -
                            //same issue as gas/brake/steer
    }

    for (size_t i = 0; i < wheels.size(); i++){
        Wheel& wheel = wheels[i];
        wheel.roll_angle = 0.0f;
        wheel.compression = 0.0f;
        wheel.grounded = false;
        wheel.angular_velocity = 0.0f;
        wheel.steer_angle = 0.0f; //no-op for a non-steerable wheel; resets a buggy's front wheels
        if (rp_vehicle && i < rp_vehicle->getNbWheels()){
            rp3d::VehicleWheel& w = rp_vehicle->getWheel((rp3d::uint32)i);
            w.setAngularVelocity(0.0f);
            w.setRotationAngle(0.0f);
            w.setDriveTorque(0.0f);
            w.setBrakeTorque(0.0f);
            w.setSteerAngle(0.0f);
        }
    }
}

//--- The physics bridge -------------------------------------------------------------------------

rp3d::VehicleWheelSettings Vehicle::MakeWheelSettings(const Wheel& wheel) const{
    WheelTuning t = ResolveTuning(wheel);
    rp3d::VehicleWheelSettings s;

    s.position = rp3d::Vector3(wheel.local_offset.x,wheel.local_offset.y,wheel.local_offset.z);

    //Normalized here rather than trusting a debug-UI drag to keep it unit - a non-unit axis
    //would silently scale every distance measured along it.
    vec3 axis = wheel.suspension_axis;
    float axis_length = axis.length();
    axis = (axis_length > 0.0001f) ? axis * (1.0f / axis_length) : vec3(0,-1,0);
    s.suspensionDirection = rp3d::Vector3(axis.x,axis.y,axis.z);

    //Steering is about the body's up; the wheel rolls along the body's forward when not steered.
    s.steeringAxis = rp3d::Vector3(ref_up.x,ref_up.y,ref_up.z);
    s.wheelForward = rp3d::Vector3(ref_forward.x,ref_forward.y,ref_forward.z);
    s.wheelUp = rp3d::Vector3(ref_up.x,ref_up.y,ref_up.z);
    //Each vehicle clamps its own steer angle before setting it; the constraint's own lock only
    //needs to not get in the way.
    s.maxSteerAngle = TYPE_PI * 0.5f;

    //rest_length is the spring's natural length (zero force at full droop, as the old explicit
    //spring had it) and the hub may rise `travel` above it before the hard stop takes over.
    //The stop can't sit above the anchor, so a travel larger than the rest length (the tank's
    //road wheels: 0.08 against 0.051) puts it AT the anchor.
    s.suspensionMaxLength = max(t.rest_length,0.001f);
    s.suspensionMinLength = clamp(t.rest_length - t.travel,0.0f,s.suspensionMaxLength);
    s.suspensionPreloadLength = 0.0f;
    //Given as a plain stiffness/damping pair, in the same N/m and N/(m/s) the tuning always
    //used - so the equilibrium compression under the vehicle's weight, and with it the ride
    //height every mesh was placed against, is exactly what it was. The difference is only in
    //how the constraint solves it: implicitly, so a damping that used to blow the explicit
    //integrator up (see TankCharacter's suspension_damping history) is simply more damping now.
    s.suspensionSpring = rp3d::SpringSettings::fromStiffnessAndDamping(t.stiffness,t.damping);

    s.radius = max(t.radius,0.001f);
    s.width = s.radius * 0.5f; //rendering helper only inside rp3d; the game draws its own meshes
    //A solid disc. What this sets is how fast an unloaded wheel spins up under torque and how
    //much the tire has to pull to bring a landing wheel up to ground speed.
    s.inertia = max(0.5f * t.mass * s.radius * s.radius,1e-5f);

    s.longitudinalFriction = max(t.friction_coefficient,0.0f);
    s.lateralFriction = max(t.lateral_friction,0.0f);
    //Grip falloff once the tire slides (see Wheel::sliding_friction_ratio). Left at rp3d's own
    //defaults until something sets them, so behaviour is unchanged unless deliberately tuned.
    s.slidingFrictionRatio = clamp(t.sliding_friction_ratio,0.0f,1.0f);
    s.peakSlipRatio = max(t.peak_slip_ratio,0.0001f);
    s.enabled = wheel.can_contact_ground;
    s.numContactSamples = (rp3d::uint32)max(contact_samples,1);
    return s;
}

void Vehicle::CreateVehicleConstraint(){
    Physics* physics = GetPhysics();
    if (!physics){
        debug->Err("CreateVehicleConstraint: %s has no physics body\n",name.c_str());
        return;
    }
    DestroyVehicleConstraint();

    rp3d::VehicleConstraintSettings settings;
    settings.up = rp3d::Vector3(ref_up.x,ref_up.y,ref_up.z);
    settings.forward = rp3d::Vector3(ref_forward.x,ref_forward.y,ref_forward.z);
    rp_vehicle = physics->CreateVehicle(settings);
    if (!rp_vehicle){
        return;
    }
    for (Wheel& wheel : wheels){
        rp_vehicle->addWheel(MakeWheelSettings(wheel));
        wheel.compression = 0.0f;
        wheel.grounded = false;
    }
    debug->Info("%s: vehicle constraint with %u wheels\n",name.c_str(),rp_vehicle->getNbWheels());
}

void Vehicle::DestroyVehicleConstraint(){
    if (rp_vehicle){
        if (Physics* physics = GetPhysics()){
            physics->DestroyVehicle(rp_vehicle);
        }
        rp_vehicle = NULL;
    }
}

void Vehicle::SyncWheelSettings(){
    if (!rp_vehicle){
        return;
    }
    size_t count = min(wheels.size(),(size_t)rp_vehicle->getNbWheels());
    for (size_t i = 0; i < count; i++){
        rp_vehicle->getWheel((rp3d::uint32)i).getSettings() = MakeWheelSettings(wheels[i]);
    }
}

void Vehicle::ReadBackWheels(float timestep){
    if (!rp_vehicle){
        return;
    }
    float inv_dt = timestep > 0.0f ? 1.0f / timestep : 0.0f;
    size_t count = min(wheels.size(),(size_t)rp_vehicle->getNbWheels());
    for (size_t i = 0; i < count; i++){
        Wheel& wheel = wheels[i];
        const rp3d::VehicleWheel& w = rp_vehicle->getWheel((rp3d::uint32)i);
        WheelTuning t = ResolveTuning(wheel);

        wheel.grounded = wheel.can_contact_ground && w.hasContact();

        //The constraint reports the suspension LENGTH (anchor to hub); the game has always
        //talked about compression from rest. Airborne, the constraint reports full droop, which
        //is compression 0 - so the two agree there without a special case, but the explicit
        //zero keeps a wheel that was just disabled from carrying a stale value.
        float compression = wheel.grounded ? max(t.rest_length - (float)w.getSuspensionLength(),0.0f) : 0.0f;
        wheel.compression_rate = (compression - wheel.compression) * inv_dt;
        wheel.compression = compression;

        //Impulses over the tick -> average forces over the tick.
        wheel.spring_force = wheel.grounded ? (float)w.getNormalImpulse() * inv_dt : 0.0f;
        wheel.longitudinal_force = wheel.grounded ? (float)w.getLongitudinalImpulse() * inv_dt : 0.0f;
        //rp3d's lateral axis points to the vehicle's RIGHT; this engine has always reported
        //lateral force along the driver's left (see Wheel::is_left_side on which is which).
        wheel.lateral_force = wheel.grounded ? -(float)w.getLateralImpulse() * inv_dt : 0.0f;
        //Unsigned, and unaffected by the axis-direction flip above - it is an angle, not a force.
        wheel.lateral_slip_angle = wheel.grounded ? (float)w.getLateralSlipAngle() : 0.0f;
        //What was asked of this wheel, at the tread - the torque the drivetrain set on it.
        wheel.drive_force = (wheel.grounded && t.radius > 0.0f) ? (float)w.getDriveTorque() / t.radius : 0.0f;

        //The friction limits the tire is working against, and whether it is pinned to one of
        //them: at the limit the constraint's clamp is what decided the force, i.e. the tire is
        //slipping (wheelspin under power, a locked wheel under the brake, a sideways slide).
        wheel.friction_budget = t.friction_coefficient * wheel.spring_force;
        float lateral_budget = t.lateral_friction * wheel.spring_force;
        const float at_limit = 0.98f;
        wheel.friction_saturated = wheel.grounded && wheel.spring_force > 0.0f &&
            ((wheel.friction_budget > 0.0f && fabs(wheel.longitudinal_force) >= at_limit * wheel.friction_budget) ||
             (lateral_budget > 0.0f && fabs(wheel.lateral_force) >= at_limit * lateral_budget));

        //Sign flipped into this engine's convention - see Wheel::angular_velocity.
        wheel.angular_velocity = -(float)w.getAngularVelocity();
    }
}

void Vehicle::UpdateWheelVisuals(float timestep){
    for (Wheel& wheel : wheels){
        WheelSuspension::UpdateVisual(wheel,ResolveTuning(wheel),timestep);
    }
}
