#include <math.h>

#include "Spline.h"

/*
    See Spline.h for what this is and why each part is shaped the way it is.

    Only core's maths types in here - tools/spline_test.cpp builds this with type_helpers.cpp and
    nothing else, and apps/archer's `make rules` links it the same way.
*/

static const float SPLINE_EPS = 1e-6f;

static vec3 Mix(const vec3& a, const vec3& b, float k){
    //vec3::lerp clamps through type_helpers' clamp(); this is called with k already in range and
    //keeps the header's promise that nothing but the vector types is needed.
    return a + (b - a) * k;
}

static vec3 Unit(const vec3& v, const vec3& fallback){
    float l = v.length();
    return (l > SPLINE_EPS) ? v / l : fallback;
}

//`v` with its component along unit `axis` removed, normalised - or `fallback` if nothing is left.
static vec3 OrthoUnit(const vec3& v, const vec3& axis, const vec3& fallback){
    return Unit(v - axis * axis.dot(v),fallback);
}

//--- Hermite --------------------------------------------------------------------------------------

vec3 Spline::Hermite(const vec3& p0, const vec3& m0, const vec3& p1, const vec3& m1, float t){
    float t2 = t * t;
    float t3 = t2 * t;
    float h00 =  2.0f * t3 - 3.0f * t2 + 1.0f;
    float h10 =         t3 - 2.0f * t2 + t;
    float h01 = -2.0f * t3 + 3.0f * t2;
    float h11 =         t3 -        t2;
    return p0 * h00 + m0 * h10 + p1 * h01 + m1 * h11;
}

vec3 Spline::HermiteDerivative(const vec3& p0, const vec3& m0, const vec3& p1, const vec3& m1, float t){
    float t2 = t * t;
    float d00 =  6.0f * t2 - 6.0f * t;
    float d10 =  3.0f * t2 - 4.0f * t + 1.0f;
    float d01 = -6.0f * t2 + 6.0f * t;
    float d11 =  3.0f * t2 - 2.0f * t;
    return p0 * d00 + m0 * d10 + p1 * d01 + m1 * d11;
}

//--- Building -------------------------------------------------------------------------------------

/*
    The knot interval between two points: |b - a|^alpha, floored so two coincident points do not
    divide by zero. The floor is small enough that it only matters for points that really are on
    top of each other, where any tangent is as good as another.
*/
static float Knot(const vec3& a, const vec3& b, float alpha){
    float d = a.distance(b);
    float k = (alpha == 0.5f) ? sqrtf(d) : powf(d,alpha);
    return (k > 1e-4f) ? k : 1e-4f;
}

bool Spline::Build(float sample_step){
    segments.clear();
    samples.clear();
    length = 0.0f;
    size_t n = points.size();
    if (n < 2){
        return false;
    }
    float step = (sample_step > 1e-4f) ? sample_step : 1e-4f;

    /*
        Centripetal Catmull-Rom as Hermite. For the segment p1 -> p2, with neighbours p0 and p3
        and knot intervals t01, t12, t23, the tangents in knot time are the Barry-Goldman ones
        below, and multiplying by t12 turns them into tangents for t in [0,1]. The ends reflect
        the second point through the first (and the same at the other end), which makes the end
        tangent point straight at the neighbour: the curve leaves an end without a hook.
    */
    for (size_t i = 0; i + 1 < n; i++){
        vec3 p1 = points[i];
        vec3 p2 = points[i + 1];
        vec3 p0 = (i > 0)     ? points[i - 1] : p1 * 2.0f - p2;
        vec3 p3 = (i + 2 < n) ? points[i + 2] : p2 * 2.0f - p1;
        float t01 = Knot(p0,p1,alpha);
        float t12 = Knot(p1,p2,alpha);
        float t23 = Knot(p2,p3,alpha);
        vec3 m1 = (p1 - p0) / t01 - (p2 - p0) / (t01 + t12) + (p2 - p1) / t12;
        vec3 m2 = (p2 - p1) / t12 - (p3 - p1) / (t12 + t23) + (p3 - p2) / t23;
        Segment seg;
        seg.p0 = p1;
        seg.m0 = m1 * t12;
        seg.p1 = p2;
        seg.m1 = m2 * t12;
        segments.push_back(seg);
    }

    /*
        The table. Each segment is cut into as many samples as its length needs at `step`,
        judged from a coarse pass first - a segment's parameter is not its length, so a fixed
        count per segment would leave a long one coarse and waste samples on a short one.

        Distance accumulates as chords between samples. That slightly under-reads a curved
        length, but it is the SAME measure everywhere, which is what matters: a distance handed
        back in maps to the point that was measured at it.
    */
    vec3 first_tangent = Unit(Derivative(0.0f),Unit(points[1] - points[0],vec3(0.0f,0.0f,1.0f)));
    Sample prev;
    prev.u = 0.0f;
    prev.s = 0.0f;
    prev.position = points[0];
    prev.tangent = first_tangent;
    //The first normal: `up`, as nearly as the tangent allows. Straight up a wall, `up` IS the
    //tangent and says nothing, so any perpendicular will do.
    prev.normal = OrthoUnit(up,first_tangent,Unit(first_tangent.orthogonal(),vec3(1.0f,0.0f,0.0f)));
    samples.push_back(prev);

    for (size_t i = 0; i < segments.size(); i++){
        const Segment& seg = segments[i];
        float rough = 0.0f;
        vec3 last = seg.p0;
        for (int k = 1; k <= 16; k++){
            vec3 p = Hermite(seg.p0,seg.m0,seg.p1,seg.m1,(float)k / 16.0f);
            rough += p.distance(last);
            last = p;
        }
        int count = (int)ceilf(rough / step);
        if (count < 4){ count = 4; }
        if (count > 4096){ count = 4096; }

        for (int k = 1; k <= count; k++){
            Sample next;
            next.u = (float)i + (float)k / (float)count;
            next.position = Evaluate(next.u);
            //A zero derivative - a curve that stops dead, as a pair of coincident points makes -
            //has no direction of its own; it keeps the one it arrived with.
            next.tangent = Unit(Derivative(next.u),prev.tangent);
            next.s = prev.s + next.position.distance(prev.position);

            /*
                Double reflection: reflect the frame in the plane bisecting the chord, which maps
                the old position onto the new one, then in the plane that takes the reflected
                tangent onto the real one. Two reflections make a rotation, and it is the one that
                twists least - the rotation-minimising frame to second order.
            */
            vec3 v1 = next.position - prev.position;
            float c1 = v1.dot(v1);
            vec3 r = prev.normal;
            if (c1 > SPLINE_EPS * SPLINE_EPS){
                vec3 rl = prev.normal - v1 * (2.0f / c1 * v1.dot(prev.normal));
                vec3 tl = prev.tangent - v1 * (2.0f / c1 * v1.dot(prev.tangent));
                vec3 v2 = next.tangent - tl;
                float c2 = v2.dot(v2);
                r = (c2 > SPLINE_EPS * SPLINE_EPS) ? rl - v2 * (2.0f / c2 * v2.dot(rl)) : rl;
            }
            //Squared off against the tangent every step, so float drift cannot accumulate into a
            //normal that has quietly tipped along the curve.
            next.normal = OrthoUnit(r,next.tangent,prev.normal);
            samples.push_back(next);
            prev = next;
        }
    }
    length = samples.back().s;
    return true;
}

//--- By parameter ---------------------------------------------------------------------------------

vec3 Spline::Evaluate(float u) const{
    if (segments.empty()){
        return points.empty() ? vec3() : points[0];
    }
    int count = (int)segments.size();
    if (u <= 0.0f){
        return segments[0].p0;
    }
    if (u >= (float)count){
        return segments[count - 1].p1;
    }
    int i = (int)u;
    const Segment& seg = segments[i];
    return Hermite(seg.p0,seg.m0,seg.p1,seg.m1,u - (float)i);
}

vec3 Spline::Derivative(float u) const{
    if (segments.empty()){
        return vec3();
    }
    int count = (int)segments.size();
    if (u < 0.0f){ u = 0.0f; }
    int i = (int)u;
    if (i >= count){
        i = count - 1;
    }
    const Segment& seg = segments[i];
    float t = u - (float)i;
    if (t > 1.0f){ t = 1.0f; }
    return HermiteDerivative(seg.p0,seg.m0,seg.p1,seg.m1,t);
}

//--- By distance ----------------------------------------------------------------------------------

size_t Spline::SampleBefore(float s) const{
    size_t lo = 0;
    size_t hi = samples.size() - 1;
    //Invariant: samples[lo].s <= s < samples[hi].s, or hi is the last sample.
    while (hi - lo > 1){
        size_t mid = (lo + hi) / 2;
        if (samples[mid].s <= s){
            lo = mid;
        }else{
            hi = mid;
        }
    }
    return lo;
}

size_t Spline::SampleBeforeParam(float u) const{
    size_t lo = 0;
    size_t hi = samples.size() - 1;
    while (hi - lo > 1){
        size_t mid = (lo + hi) / 2;
        if (samples[mid].u <= u){
            lo = mid;
        }else{
            hi = mid;
        }
    }
    return lo;
}

float Spline::ParamAtDistance(float s) const{
    if (samples.size() < 2){
        return 0.0f;
    }
    if (s <= 0.0f){
        return 0.0f;
    }
    if (s >= length){
        return samples.back().u;
    }
    size_t i = SampleBefore(s);
    const Sample& a = samples[i];
    const Sample& b = samples[i + 1];
    float span = b.s - a.s;
    float k = (span > SPLINE_EPS) ? (s - a.s) / span : 0.0f;
    return a.u + (b.u - a.u) * k;
}

float Spline::DistanceAtParam(float u) const{
    if (samples.size() < 2){
        return 0.0f;
    }
    if (u <= 0.0f){
        return 0.0f;
    }
    if (u >= samples.back().u){
        return length;
    }
    size_t i = SampleBeforeParam(u);
    const Sample& a = samples[i];
    const Sample& b = samples[i + 1];
    float span = b.u - a.u;
    float k = (span > SPLINE_EPS) ? (u - a.u) / span : 0.0f;
    return a.s + (b.s - a.s) * k;
}

vec3 Spline::PositionAt(float s) const{
    return Evaluate(ParamAtDistance(s));
}

vec3 Spline::TangentAt(float s) const{
    if (samples.empty()){
        return vec3(0.0f,0.0f,1.0f);
    }
    size_t i = SampleBefore(s);
    return Unit(Derivative(ParamAtDistance(s)),samples[i].tangent);
}

SplineFrame Spline::FrameAt(float s) const{
    SplineFrame f;
    if (samples.empty()){
        f.position = points.empty() ? vec3() : points[0];
        f.tangent = vec3(0.0f,0.0f,1.0f);
        f.normal = vec3(0.0f,1.0f,0.0f);
        f.side = vec3(1.0f,0.0f,0.0f);
        return f;
    }
    if (s < 0.0f){ s = 0.0f; }
    if (s > length){ s = length; }
    size_t i = SampleBefore(s);
    size_t j = (i + 1 < samples.size()) ? i + 1 : i;
    const Sample& a = samples[i];
    const Sample& b = samples[j];
    float span = b.s - a.s;
    float k = (span > SPLINE_EPS) ? (s - a.s) / span : 0.0f;

    float u = a.u + (b.u - a.u) * k;
    f.position = Evaluate(u);
    f.tangent = Unit(Derivative(u),a.tangent);
    //The normal between two samples, squared off against the EXACT tangent here. The samples are
    //close enough that the lerp is well short of the angle where it would lose length.
    f.normal = OrthoUnit(Mix(a.normal,b.normal,k),f.tangent,a.normal);
    f.side = f.normal.cross(f.tangent);
    return f;
}

float Spline::ClosestDistance(const vec3& p) const{
    if (samples.size() < 2){
        return 0.0f;
    }
    //Nearest sample first, then a ternary search either side of it. The table's spacing is far
    //below the curve's radius of curvature, so the distance is unimodal over that window.
    size_t best = 0;
    float best_d = samples[0].position.distance(p);
    for (size_t i = 1; i < samples.size(); i++){
        float d = samples[i].position.distance(p);
        if (d < best_d){
            best_d = d;
            best = i;
        }
    }
    float lo = samples[(best > 0) ? best - 1 : 0].s;
    float hi = samples[(best + 1 < samples.size()) ? best + 1 : best].s;
    for (int it = 0; it < 40 && hi - lo > 1e-5f; it++){
        float m1 = lo + (hi - lo) / 3.0f;
        float m2 = hi - (hi - lo) / 3.0f;
        if (PositionAt(m1).distance(p) < PositionAt(m2).distance(p)){
            hi = m2;
        }else{
            lo = m1;
        }
    }
    return 0.5f * (lo + hi);
}
