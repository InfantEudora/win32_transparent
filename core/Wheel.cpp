#include "Wheel.h"

namespace WheelSuspension{

ContactResult UpdateContact(Wheel& wheel,const WheelTuning& tuning,Physics* physics,
                             const vec3& body_world_pos,const quat& rotation,
                             const vec3& com_world,const vec3& velocity,
                             const vec3& angular_velocity,const vec3& forward,float timestep){
    ContactResult result;

    //Diagnostics this function owns - rewritten from scratch every call so a wheel that bails
    //out below reports honest zeroes rather than whatever it last did while active. The
    //tangential diagnostics (drive_force/longitudinal_force/lateral_force/friction_saturated)
    //are the caller's own concern and are left untouched here.
    wheel.point_speed = 0.0f;
    wheel.compression_rate = 0.0f;
    wheel.spring_force = 0.0f;
    wheel.friction_budget = 0.0f;

    if (!wheel.can_contact_ground){
        wheel.grounded = false;
        wheel.compression = 0.0f;
        return result;
    }

    //Normalized here rather than trusting the caller (or a debug-UI drag) to keep it unit - a
    //non-unit axis would silently scale every distance measured along it.
    vec3 axis_local = wheel.suspension_axis;
    float axis_length = axis_local.length();
    if (axis_length < 0.0001f){
        //No direction to hang the wheel along. Left explicitly ungrounded rather than bailing
        //with last tick's state, so the caller can tell a wheel that produced its own contact
        //this tick from one that didn't.
        wheel.grounded = false;
        wheel.compression = 0.0f;
        return result;
    }
    axis_local = axis_local * (1.0f / axis_length);
    vec3 axis_world = rotation * axis_local; //points the way the strut EXTENDS

    vec3 mount_world = body_world_pos + rotation * wheel.local_offset;
    result.mount_world = mount_world;

    //Start slightly back up the suspension axis from the anchor so a wheel that's already
    //compressed past rest_length at the start of this tick is still detected, not missed by
    //starting the ray exactly at (or below) the surface.
    const float start_margin = 0.05f;
    vec3 ray_start = mount_world - axis_world * start_margin;
    //Reaches the wheel's RADIUS further than a bare hub-to-ground ray would: the tread touches
    //down while the hub is still a radius clear of the terrain, so the contact happens that
    //much sooner.
    float ray_length = start_margin + tuning.rest_length + tuning.travel + tuning.radius;
    vec3 ray_end = ray_start + axis_world * ray_length;

    PhysicsWorld::RaycastHit hit = physics->world->Raycast(ray_start,ray_end,physics->body->rigidbody);
    if (!hit.hit){
        wheel.grounded = false;
        wheel.compression = 0.0f;
        return result;
    }

    //Measured ALONG the suspension axis (rather than as a plain distance) so an angled strut
    //still reads how far the wheel can travel before it meets the ground, not the shorter
    //straight line to a hit that sits off to one side of that axis.
    float clearance = (hit.point - ray_start).dot(axis_world) - start_margin;
    //The wheel is against the ground once the anchor is within rest_length + radius of it, the
    //radius being the part a bare point-contact model would drop - without it the wheel sits
    //buried up to its axle instead of resting its tread on the surface.
    float compression = tuning.rest_length + tuning.radius - clearance;
    wheel.grounded = true;
    wheel.compression = clamp(compression,0.0f,tuning.rest_length + tuning.travel);

    if (wheel.compression <= 0.0f){
        return result; //extended past rest length - a passive spring gives no force here
    }

    //Velocity of the body material at this exact wheel's contact point (linear + angular_velocity
    //x r) so every force below reacts to how fast THIS point is moving, not just the body's
    //overall velocity - matters once it starts pitching/rolling. Clamped in magnitude before ANY
    //force derives from it: the spring damping term below feeds back into velocity/angular_velocity
    //next tick, through this exact same lever arm, and an unclamped point_velocity can turn that
    //into a divergent oscillation tick over tick on a wide, stiff, discretely-integrated suspension.
    //Measured from the centre of mass, NOT the body origin - a rigidbody rotates about its centre
    //of mass, and rp3d's own applyWorldForceAtWorldPosition already computes torque that way, so a
    //lever arm measured against the origin instead disagrees with what the solver then does with it.
    vec3 r = mount_world - com_world;
    vec3 point_velocity = velocity + angular_velocity.cross(r);
    float point_speed = point_velocity.length();
    wheel.point_speed = point_speed; //recorded PRE-clamp - see Wheel::point_speed
    if (point_speed > tuning.max_point_speed){
        point_velocity = point_velocity * (tuning.max_point_speed / point_speed);
    }

    //Rolling-without-slip: the distance this contact rolled along the ground this tick, from
    //the same (already-clamped) point_velocity every force below derives from, so the spin
    //stays consistent with what the wheel is visibly doing on the ground.
    float roll_distance = -point_velocity.dot(forward) * timestep;
    if (tuning.radius > 0.0f){
        wheel.roll_angle += roll_distance / tuning.radius;
    }

    //Pushed along the actual ground normal, not the body's own (possibly already tilted) up
    //vector - using the body's up here would mean that once it's tilted even slightly, the
    //"corrective" force is ALSO tilted, extending the error instead of fixing it. The ground
    //normal has no such feedback - it only reflects the terrain, never the body's own state.
    vec3 push_dir = hit.normal;
    float compression_rate = -point_velocity.dot(push_dir); //positive = moving further into the ground
    float spring_force = wheel.compression * tuning.stiffness + compression_rate * tuning.damping;
    //Hard ceiling regardless of the above - see each vehicle's own tuning comments for the
    //stability math a stiffness/damping pair needs to respect at a given timestep/wheel count.
    spring_force = clamp(spring_force,0.0f,tuning.max_force);
    wheel.compression_rate = compression_rate;
    wheel.spring_force = spring_force;
    physics->AddWorldForceAt(push_dir * spring_force,mount_world);

    //Everything a wheel does tangentially - engine thrust, passive grip, resistance to sideways
    //slip - is friction against the ground, so all of it draws on one budget set by how hard
    //this wheel is actually pressed down. spring_force IS that normal load, already computed
    //and clamped just above. The tangential force itself is vehicle-specific and stays in the
    //caller; this budget is what a friction-circle clamp there needs to work against.
    wheel.friction_budget = tuning.friction_coefficient * spring_force;

    result.active = true;
    result.push_dir = push_dir;
    result.point_velocity = point_velocity;
    result.roll_distance = roll_distance;
    return result;
}

void UpdateVisual(Wheel& wheel,float rest_length){
    if (!wheel.visual){
        return;
    }
    //The hub hangs (rest_length - compression) from the anchor along the suspension axis -
    //which for a plain vertical strut (0,-1,0) is a simple downward bob, and for an angled one
    //also slides the wheel fore/aft as the spring works.
    vec3 axis_local = wheel.suspension_axis;
    float axis_length = axis_local.length();
    if (axis_length > 0.0001f){
        axis_local = axis_local * (1.0f / axis_length);
    }else{
        axis_local = vec3(0,-1,0);
    }
    vec3 visual_pos = wheel.local_offset + axis_local * (rest_length - wheel.compression);
    wheel.visual->SetPosition(visual_pos);
    wheel.visual->SetRotation(quat(vec3(1,0,0),wheel.roll_angle));
}

}
