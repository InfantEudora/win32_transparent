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
    //stays consistent with what the wheel is visibly doing on the ground. Sets angular_velocity
    //(read by WheelSuspension::UpdateVisual to actually integrate roll_angle) rather than
    //touching roll_angle directly - this IS the "lock to the ground contact's velocity, assuming
    //0 slip" half of the freewheel/lock mechanic; see Wheel::angular_velocity for the other half.
    float roll_distance = -point_velocity.dot(forward) * timestep;
    if (tuning.radius > 0.0f){
        wheel.angular_velocity = -point_velocity.dot(forward) / tuning.radius;
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
    //TODO: applied at the ANCHOR (mount_world), not at the actual ground contact (hit.point).
    //For a near-vertical strut the two sit almost directly above one another, so this is a fine
    //approximation (the tank's own struts). It stops being one for a heavily raked strut - the
    //buggy's front suspension anchor sits ~0.22m from its own wheel's ground contact, mostly
    //horizontally - and applying a mostly-vertical force that far from where it actually acts
    //injects a real, persistent torque every tick that isn't physically there (observed: a
    //suspended buggy slowly pitching up onto its rear wheels over a few seconds with no input).
    //The physically correct fix is applying this force at the contact point instead - deferred
    //rather than changed here since it's shared by every vehicle, tank included.
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

void UpdateVisual(Wheel& wheel,const WheelTuning& tuning,float timestep){
    float rest_length = tuning.rest_length;
    //Runs every tick regardless of contact state - a wheel UpdateContact left ungrounded this
    //tick (or that a vehicle like TankCharacter overrode angular_velocity for on its own terms)
    //still spins by whatever angular_velocity currently holds, instead of freezing the moment
    //contact is lost. See Wheel::angular_velocity's own comment.
    wheel.roll_angle += wheel.angular_velocity * timestep;

    //Normalized here (not trusted from the caller/a debug-UI drag) same as UpdateContact does -
    //shared by both wheel.visual's position and wheel.suspension_visual's orientation/scale below.
    vec3 axis_local = wheel.suspension_axis;
    float axis_length = axis_local.length();
    if (axis_length > 0.0001f){
        axis_local = axis_local * (1.0f / axis_length);
    }else{
        axis_local = vec3(0,-1,0);
    }

    if (wheel.visual){
        //The hub hangs (rest_length - compression) from the anchor along the suspension axis -
        //which for a plain vertical strut (0,-1,0) is a simple downward bob, and for an angled
        //one also slides the wheel fore/aft as the spring works.
        vec3 visual_pos = wheel.local_offset + axis_local * (rest_length - wheel.compression);
        wheel.visual->SetPosition(visual_pos);
        //Negated for a mirrored wheel - see Wheel::visual_mirrored for why a rotation-based
        //mirror needs this to keep the mirrored wheel's APPARENT rolling direction matching the
        //other side, even though roll_angle itself is the same, correct, unmirrored value either way.
        float visual_roll = wheel.visual_mirrored ? -wheel.roll_angle : wheel.roll_angle;
        wheel.visual->SetRotation(wheel.visual_base_rotation * quat(vec3(1,0,0),visual_roll));
        //Grows/shrinks the mesh to match whatever radius actually governs the raycast/physics -
        //see Wheel::visual_natural_radius. Left alone (no SetScale call at all) if that was never
        //probed, rather than forcing an assumed 1:1 scale that could be wrong for a mesh nobody
        //measured.
        if (wheel.visual_natural_radius > 0.0f){
            float scale = tuning.radius / wheel.visual_natural_radius;
            wheel.visual->SetScale(vec3(scale,scale,scale));
        }
    }

    if (wheel.suspension_visual){
        wheel.suspension_visual->SetPosition(wheel.local_offset); //the anchor - the modeled spring's own origin, see Wheel's comment
        wheel.suspension_visual->SetRotation(wheel.suspension_visual_rotation); //fixed, authored - see Wheel's comment on why this isn't derived from axis_local
        //Object::SetScale applies in the object's own local (pre-rotation) axes - see Object's
        //"scale, rotate, translate" transform order - so scaling local Y here shortens the mesh
        //along whatever direction suspension_visual_rotation just pointed it, regardless of
        //which direction that is.
        float extension_fraction = rest_length > 0.0f ? clamp((rest_length - wheel.compression) / rest_length,0.0f,1.0f) : 1.0f;
        wheel.suspension_visual->SetScale(vec3(1,extension_fraction,1));
    }
}

}
