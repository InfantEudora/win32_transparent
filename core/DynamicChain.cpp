#include <math.h>

#include "DynamicChain.h"

/*
    See DynamicChain.h. Pure maths on core's types - tools/dynamic_chain_test.cpp links this file
    and the type helpers and nothing else.
*/

static const float DYNAMIC_CHAIN_PI = 3.14159265358979f;

static float WrapAngle(float a){
    while (a > DYNAMIC_CHAIN_PI){ a -= 2.0f * DYNAMIC_CHAIN_PI; }
    while (a < -DYNAMIC_CHAIN_PI){ a += 2.0f * DYNAMIC_CHAIN_PI; }
    return a;
}

float DynamicChain::SignedAngle(const vec3& a, const vec3& b, const vec3& normal){
    vec3 pa = a - normal * a.dot(normal);
    vec3 pb = b - normal * b.dot(normal);
    return atan2f(normal.dot(pa.cross(pb)),pa.dot(pb));
}

void DynamicChain::Reset(const std::vector<vec3>& animated){
    points = animated;
    previous = animated;
    pose = animated;
    felt = vec3(0.0f,0.0f,0.0f);        //a fresh chain starts still, whatever the root is doing
}

void DynamicChain::Carry(const quat& rotation, const vec3& about){
    for (size_t i = 0; i < points.size(); i++){
        points[i] = about + rotation * (points[i] - about);
        previous[i] = about + rotation * (previous[i] - about);
        pose[i] = about + rotation * (pose[i] - about);
        if (i == 0){
            felt = rotation * felt;     //a velocity, so turned but not moved
        }
    }
    //The pose was turned with it, so the frame it was in was too.
    last_frame = rotation * last_frame;
    last_frame.normalize();
}

float DynamicChain::SegmentDeviation(int i, const vec3& normal) const{
    if (i < 0 || i + 1 >= (int)points.size() || i + 1 >= (int)pose.size()){
        return 0.0f;
    }
    return SignedAngle(pose[i + 1] - pose[i],points[i + 1] - points[i],normal);
}

void DynamicChain::Step(const std::vector<vec3>& animated, const DynamicChainParams& params,
                        const std::vector<DynamicChainLimit>& limits, const quat& frame){
    size_t n = animated.size();
    if (n < 2 || points.size() != n || (animated[0] - points[0]).length() > params.teleport){
        Reset(animated);
        last_frame = frame;
        return;
    }

    /*
        0. The pose's own motion, passed through. Each point's place relative to the root, in the
        frame's own axes, now and last tick; the difference is what the ANIMATION did, as opposed to
        what the body did, and it moves the particle and its previous position alike - a move, not
        a push, so it carries no momentum into the verlet below.
    */
    if (params.follow > 0.0f){
        quat inv_now = frame;
        inv_now.inverse();
        quat inv_last = last_frame;
        inv_last.inverse();
        for (size_t i = 1; i < n; i++){
            vec3 local_now = inv_now * (animated[i] - animated[0]);
            vec3 local_last = inv_last * (pose[i] - pose[0]);
            vec3 shift = (frame * (local_now - local_last)) * params.follow;
            points[i] = points[i] + shift;
            previous[i] = previous[i] + shift;
        }
    }
    last_frame = frame;
    /*
        0b. The root's acceleration, felt only up to max_accel. `felt` is the root's velocity as the
        chain knows it, in units per tick, closing on the real one by at most max_accel dt^2 a tick;
        the difference is carried straight on - a move, like step 0, so it carries no momentum. See
        DynamicChainParams::max_accel.
    */
    vec3 moved = animated[0] - points[0];
    vec3 carried(0.0f,0.0f,0.0f);
    if (params.max_accel > 0.0f){
        vec3 dv = moved - felt;
        float limit = params.max_accel * params.dt * params.dt;
        float dl = dv.length();
        if (dl > limit){
            dv = dv * (limit / dl);
        }
        felt = felt + dv;
        carried = moved - felt;
        for (size_t i = 1; i < n; i++){
            points[i] = points[i] + carried;
            previous[i] = previous[i] + carried;
        }
    }else{
        felt = moved;
    }
    vec3 nrm = params.plane_normal;
    bool f_planar = nrm.length() > 1e-6f;
    if (f_planar){
        nrm.normalize();
    }
    float keep = 1.0f - params.damping;
    vec3 fall = params.gravity * (params.dt * params.dt);

    points[0] = animated[0];
    previous[0] = animated[0];
    //How the simulated parent segment has turned off its pose. The root has no segment, so none.
    quat parent_turn(0.0f,0.0f,0.0f,1.0f);
    for (size_t i = 1; i < n; i++){
        const vec3& parent = points[i - 1];
        vec3 anim_seg = animated[i] - animated[i - 1];
        float len = anim_seg.length();

        //1. Verlet: the velocity it had, damped, plus gravity.
        vec3 p = points[i];
        /*
            WITH A CAP, THE DAMPING IS THE CHAIN'S OWN - its motion relative to the root as the chain
            feels it (`felt`) loses `damping` a tick, not its motion through the world. Both others
            were tried and fling it: damped in the carried frame outright, a capped landing (that
            frame still moving down while the root has stopped) slowed the particles against a root
            that kept going and they trailed UP over it; damped against the world, the damping
            became DRAG, and 8% of 20 u/s a tick streamed the hair straight up off a falling head. The
            wind is where the air belongs.
            Without a cap it is as it always was - against the world - which the legs are tuned to.
        */
        vec3 v = (params.max_accel > 0.0f) ? felt + ((p - previous[i]) - felt) * keep
                                           : (p - previous[i]) * keep;
        previous[i] = p;
        p = p + v + fall;

        //2. Toward the pose, hung from the simulated parent and turned with it.
        vec3 target = parent + parent_turn * anim_seg;
        p = p + (target - p) * params.stiffness;

        //3 and 4. Its length, and in the plane. The segment's depth along the normal is the pose's
        //own and is kept; only the part in the plane is rescaled.
        vec3 d = p - parent;
        if (f_planar){
            float depth = anim_seg.dot(nrm);
            vec3 flat = d - nrm * d.dot(nrm);
            float flat_len = flat.length();
            float want = sqrtf(fmaxf(len * len - depth * depth,0.0f));
            if (flat_len > 1e-6f){
                flat = flat * (want / flat_len);
            }else{
                vec3 t = target - parent;
                flat = t - nrm * t.dot(nrm);
            }
            d = flat + nrm * depth;
        }else{
            float dl = d.length();
            d = (dl > 1e-6f) ? d * (len / dl) : (target - parent);
        }

        //5. The joint's limit, as a deviation from the pose's own bend there.
        size_t k = i - 1;       //this particle ends segment k
        if (f_planar && k < limits.size()){
            const DynamicChainLimit& lim = limits[k];
            float dev = 0.0f;
            float rel_anim = 0.0f;
            if (k == 0){
                dev = SignedAngle(anim_seg,d,nrm);
            }else{
                vec3 anim_parent = animated[i - 1] - animated[i - 2];
                vec3 sim_parent = points[i - 1] - points[i - 2];
                rel_anim = SignedAngle(anim_parent,anim_seg,nrm);
                dev = WrapAngle(SignedAngle(sim_parent,d,nrm) - rel_anim);
            }
            float clamped = fminf(fmaxf(dev,lim.lo),lim.hi);
            if (k > 0 && lim.bend_sign != 0 && (rel_anim + clamped) * (float)lim.bend_sign < 0.0f){
                clamped = -rel_anim;        //straight, and no further
            }
            if (clamped != dev){
                d = quat(nrm,clamped - dev) * d;
            }
        }else if (!f_planar && k < limits.size() && limits[k].cone > 0.0f){
            //The 3D cone - see DynamicChainLimit::cone. Turned back onto its edge, about the axis
            //between where it points and where the POSE points this segment - not the pose carried
            //along the simulated parent, or the cones add up down the chain.
            vec3 want = anim_seg;
            float dl = d.length();
            float wl = want.length();
            if (dl > 1e-6f && wl > 1e-6f){
                vec3 a = d * (1.0f / dl);
                vec3 b = want * (1.0f / wl);
                float angle = acosf(fminf(fmaxf(a.dot(b),-1.0f),1.0f));
                if (angle > limits[k].cone){
                    vec3 axis = a.cross(b);
                    if (axis.length() < 1e-6f){
                        //Pointing straight back against the pose: any perpendicular will do.
                        axis = (fabsf(b.x) < 0.9f) ? b.cross(vec3(1.0f,0.0f,0.0f)) : b.cross(vec3(0.0f,1.0f,0.0f));
                    }
                    axis.normalize();
                    d = quat(axis,angle - limits[k].cone) * d;
                }
            }
        }
        p = parent + d;

        //6. Out of the spheres, then back to its length - see DynamicChainSphere.
        for (const DynamicChainSphere& s : params.spheres){
            vec3 out = p - s.centre;
            float dist = out.length();
            if (s.radius <= 0.0f || dist >= s.radius){
                continue;
            }
            p = (dist > 1e-6f) ? s.centre + out * (s.radius / dist) : parent + d;
            vec3 back = p - parent;
            float bl = back.length();
            p = (bl > 1e-6f) ? parent + back * (len / bl) : parent + d;
            d = p - parent;
        }
        points[i] = p;

        //What this segment's pose is turned by now, for its child's target.
        if (f_planar){
            parent_turn = quat(nrm,SignedAngle(anim_seg,d,nrm));
        }else if (len > 1e-6f){
            vec3 from = anim_seg * (1.0f / len);
            vec3 to = d;
            to.normalize();
            parent_turn = quat::getquat(from,to);
        }
    }
    pose = animated;
}
