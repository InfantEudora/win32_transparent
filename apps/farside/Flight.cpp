#include "Flight.h"
#include "type_helpers.h"
#include <cmath>

//GLSL's smoothstep. type_helpers has one under that name with a different argument order and
//contract, so this is local rather than a guess at what that one does at its edges.
static float Ease(float edge0, float edge1, float x){
    float t = clamp((x - edge0) / (edge1 - edge0),0.0f,1.0f);
    return t * t * (3.0f - 2.0f * t);
}

//Engine convention: local -z goes along `forward`, local +y toward `up`.
static quat Orient(const vec3& forward, const vec3& up){
    return quat::getquat(forward,vec3(),up);
}

//`v` with its component along `n` removed, normalised; `fallback` if nothing is left.
static vec3 Tangent(const vec3& v, const vec3& n, const vec3& fallback){
    vec3 t = v - n * v.dot(n);
    float l = t.length();
    return (l > 1e-5f) ? t * (1.0f / l) : fallback;
}

void StepFlight(ShipState& s, const FlightInput& in, const Field& field, const FlightTuning& t,
                float ceiling, float dt){
    //What is asked for moves first, landed or not: climbing out is what takes off.
    s.height_set = clamp(s.height_set + in.climb * t.climb_rate * dt + in.climb_step,0.0f,ceiling);

    if (s.f_landed){
        bool f_valid = (s.landed_on >= 0) && (s.landed_on < (int)field.spheres.size());
        if (f_valid && s.height_set <= 0.0f){
            //Riding the body: its spin carries the ship round, so the frame is all there is.
            const FieldSphere& body = field.spheres[s.landed_on];
            s.position = body.centre + body.rotation * s.landed_local;
            vec3 up = (s.position - body.centre).normalize();
            vec3 forward = Tangent(body.rotation * s.landed_forward,up,vec3(0,0,-1));
            s.yaw = atan2f(-forward.x,-forward.z);
            s.velocity = vec3();
            s.vertical_speed = 0.0f;
            s.bank = 0.0f;
            s.ground = field.Ground(s.position.x,s.position.z);
            s.height = 0.0f;
            s.orientation = Orient(forward,up);
            return;
        }
        //Taking off keeps the ship where it is, at rest; the servo below lifts it.
        s.f_landed = false;
        s.landed_on = -1;
    }

    //Steering. Positive turn is to the right, which seen from above is clockwise, i.e. less yaw.
    s.yaw -= in.turn * t.turn_rate * dt;
    vec3 forward = vec3(-sinf(s.yaw),0.0f,-cosf(s.yaw));
    s.velocity += forward * (in.thrust * t.accel * dt);
    s.velocity *= expf(-t.drag * dt);

    //Faster high, slower low - against last tick's height, which is what the ship is flying at.
    float high = (ceiling > 1e-3f) ? clamp(s.height / ceiling,0.0f,1.0f) : 0.0f;
    float top_speed = t.speed_low + (t.speed_high - t.speed_low) * high;
    float speed = s.velocity.length();
    if (speed > top_speed){
        s.velocity *= top_speed / speed;
    }
    s.position.x += s.velocity.x * dt;
    s.position.z += s.velocity.z * dt;

    //Height: hold height_set over whatever is under the ship now, never through the lid.
    GroundSample g = field.Ground(s.position.x,s.position.z);
    float target = g.height + fmaxf(s.height_set,t.clearance);
    target = fminf(target,ceiling);
    s.vertical_speed = clamp((target - s.position.y) * t.follow_rate,-t.max_vertical_speed,t.max_vertical_speed);
    s.position.y += s.vertical_speed * dt;
    s.position.y = clamp(s.position.y,t.clearance,ceiling);

    //Anything flown into pushes back, and the speed going into it stops. Ground pushes only when
    //the servo has fallen behind a slope, which lifts the ship up it and slows it.
    std::vector<vec3> pushes;
    if (field.PushOut(s.position,t.radius,t.clearance,&pushes) > 0){
        for (const vec3& n:pushes){
            vec3 across = vec3(n.x,0.0f,n.z);
            float l = across.length();
            if (l < 1e-4f){
                continue;
            }
            across *= 1.0f / l;
            float into = s.velocity.dot(across);
            if (into < 0.0f){
                s.velocity -= across * into;
            }
        }
        s.position.y = clamp(s.position.y,t.clearance,ceiling);
    }

    s.ground = field.Ground(s.position.x,s.position.z);
    s.height = s.position.y - s.ground.height;

    //Landing: asked all the way down, slow, low, on a body, and not too steep.
    float slope_deg = acosf(clamp(s.ground.normal.y,-1.0f,1.0f)) * 180.0f / TYPE_PI;
    if (s.height_set <= 0.0f && s.ground.sphere >= 0 && s.height <= t.clearance + 0.05f &&
        s.velocity.length() < t.land_speed && slope_deg <= t.land_slope_deg){
        const FieldSphere& body = field.spheres[s.ground.sphere];
        vec3 up = s.ground.normal;
        s.position = body.centre + up * (body.radius + t.clearance);
        quat inv = body.rotation;
        inv.inverse();
        s.landed_local = inv * (s.position - body.centre);
        s.landed_forward = inv * Tangent(forward,up,vec3(0,0,-1));
        s.landed_on = s.ground.sphere;
        s.f_landed = true;
        s.velocity = vec3();
        s.vertical_speed = 0.0f;
        s.height = 0.0f;
        s.orientation = Orient(Tangent(forward,up,vec3(0,0,-1)),up);
        return;
    }

    //The look: close to the ground the ship lies along the slope, higher up it flies level, and
    //it banks into a turn.
    float hug = 1.0f - Ease(0.0f,3.0f,s.height);
    vec3 up = (vec3(0,1,0) * (1.0f - hug) + s.ground.normal * hug).normalize();
    float bank_goal = in.turn * 0.45f;
    s.bank += (bank_goal - s.bank) * fminf(1.0f,6.0f * dt);
    vec3 nose = Tangent(forward,up,forward);
    vec3 banked_up = quat(nose,s.bank) * up;
    s.orientation = Orient(nose,banked_up);
}
