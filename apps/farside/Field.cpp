#include "Field.h"
#include <cmath>

//A rotation's inverse. quat::inverse works in place, and every caller here wants a copy.
static quat Inverse(const quat& q){
    quat r = q;
    r.inverse();
    return r;
}

GroundSample Field::Ground(float x, float z) const{
    GroundSample g;
    for (size_t i = 0; i < spheres.size(); i++){
        const FieldSphere& s = spheres[i];
        if (!s.f_ground){
            continue;
        }
        float dx = x - s.centre.x;
        float dz = z - s.centre.z;
        float r2 = dx * dx + dz * dz;
        float R2 = s.radius * s.radius;
        if (r2 >= R2){
            continue;
        }
        //The upper surface only: the underside is never seen, so it is never ground.
        float up = sqrtf(R2 - r2);
        float h = s.centre.y + up;
        if (h > g.height){
            g.height = h;
            g.normal = vec3(dx,up,dz) * (1.0f / s.radius);
            g.sphere = (int)i;
        }
    }
    return g;
}

float Field::HighestGround() const{
    float top = 0.0f;
    for (const FieldSphere& s:spheres){
        if (s.f_ground){
            top = fmaxf(top,s.centre.y + s.radius);
        }
    }
    return top;
}

//Slab test of the segment a..b, in the box's own frame, against the box -half..half.
static bool SegmentHitsBox(const vec3& a, const vec3& b, const vec3& half){
    float A[3] = {a.x,a.y,a.z};
    float B[3] = {b.x,b.y,b.z};
    float H[3] = {half.x,half.y,half.z};
    float t0 = 0.0f;
    float t1 = 1.0f;
    for (int k = 0; k < 3; k++){
        float d = B[k] - A[k];
        if (fabsf(d) < 1e-9f){
            if (A[k] < -H[k] || A[k] > H[k]){
                return false;
            }
            continue;
        }
        float ta = (-H[k] - A[k]) / d;
        float tb = ( H[k] - A[k]) / d;
        if (ta > tb){
            float swap = ta;
            ta = tb;
            tb = swap;
        }
        t0 = fmaxf(t0,ta);
        t1 = fminf(t1,tb);
        if (t0 > t1){
            return false;
        }
    }
    return true;
}

bool Field::Blocked(const vec3& a, const vec3& b) const{
    vec3 d = b - a;
    float len2 = d.dot(d);
    for (const FieldSphere& s:spheres){
        float t = 0.0f;
        if (len2 > 1e-12f){
            t = (s.centre - a).dot(d) / len2;
            t = fminf(fmaxf(t,0.0f),1.0f);
        }
        vec3 off = s.centre - (a + d * t);
        //A hair smaller than the sphere, so an end lying ON the surface - a post's foot, a point
        //of the overlay sampled at ground level - is not counted as inside it.
        float r = s.radius * 0.9995f;
        if (off.dot(off) < r * r){
            return true;
        }
    }
    for (const FieldBox& box:boxes){
        quat inv = Inverse(box.rotation);
        if (SegmentHitsBox(inv * (a - box.centre),inv * (b - box.centre),box.half * 0.999f)){
            return true;
        }
    }
    return false;
}

int Field::PushOut(vec3& p, float radius, float ground_radius, std::vector<vec3>* pushes) const{
    int count = 0;
    for (const FieldSphere& s:spheres){
        //Ground keeps a ship only its landed clearance off the surface; anything bigger would
        //lift a landed ship back off the cap. Obstacles keep the whole ship clear.
        float keep = s.radius + (s.f_ground ? ground_radius : radius);
        vec3 d = p - s.centre;
        float dist = d.length();
        if (dist >= keep){
            continue;
        }
        vec3 n = (dist > 1e-6f) ? d * (1.0f / dist) : vec3(0,1,0);
        p = s.centre + n * keep;
        if (pushes){
            pushes->push_back(n);
        }
        count++;
    }
    for (const FieldBox& box:boxes){
        quat inv = Inverse(box.rotation);
        vec3 lp = inv * (p - box.centre);
        vec3 closest = vec3(fminf(fmaxf(lp.x,-box.half.x),box.half.x),
                            fminf(fmaxf(lp.y,-box.half.y),box.half.y),
                            fminf(fmaxf(lp.z,-box.half.z),box.half.z));
        vec3 diff = lp - closest;
        float dist2 = diff.dot(diff);
        if (dist2 >= radius * radius){
            continue;
        }
        vec3 n_local;
        if (dist2 > 1e-12f){
            float dist = sqrtf(dist2);
            n_local = diff * (1.0f / dist);
            lp = closest + n_local * radius;
        }else{
            //The centre is inside the box: out through the nearest face.
            float L[3] = {lp.x,lp.y,lp.z};
            float H[3] = {box.half.x,box.half.y,box.half.z};
            int axis = 0;
            float least = 1e30f;
            for (int k = 0; k < 3; k++){
                float depth = H[k] - fabsf(L[k]);
                if (depth < least){
                    least = depth;
                    axis = k;
                }
            }
            float sign = (L[axis] < 0.0f) ? -1.0f : 1.0f;
            float N[3] = {0,0,0};
            N[axis] = sign;
            L[axis] = sign * (H[axis] + radius);
            n_local = vec3(N[0],N[1],N[2]);
            lp = vec3(L[0],L[1],L[2]);
        }
        p = box.centre + box.rotation * lp;
        if (pushes){
            pushes->push_back(box.rotation * n_local);
        }
        count++;
    }
    return count;
}
