#include <math.h>

#include "SplineDeform.h"

/*
    See SplineDeform.h. Like Spline.cpp, only core's maths types in here, so a test can link it
    with nothing else.
*/

static float Smooth01(float k){
    if (k <= 0.0f){ return 0.0f; }
    if (k >= 1.0f){ return 1.0f; }
    return k * k * (3.0f - 2.0f * k);
}

float SplineDeformTaper(const SplineDeformParams& params, float s, float range_start, float range_end){
    float scale = 1.0f;
    if (params.taper_start_length > 0.0f){
        float k = Smooth01((s - range_start) / params.taper_start_length);
        scale *= params.taper_start_scale + (1.0f - params.taper_start_scale) * k;
    }
    if (params.taper_end_length > 0.0f){
        float k = Smooth01((range_end - s) / params.taper_end_length);
        scale *= params.taper_end_scale + (1.0f - params.taper_end_scale) * k;
    }
    return scale;
}

bool SplineDeformMeasure(const std::vector<vertex>& tile, float& zmin, float& length){
    if (tile.empty()){
        return false;
    }
    float lo = tile[0].pos.z;
    float hi = tile[0].pos.z;
    for (size_t i = 1; i < tile.size(); i++){
        if (tile[i].pos.z < lo){ lo = tile[i].pos.z; }
        if (tile[i].pos.z > hi){ hi = tile[i].pos.z; }
    }
    zmin = lo;
    length = hi - lo;
    return length > 1e-6f;
}

/*
    A tile vertex's normal, in the frame's (side, normal, tangent) basis, after the deform.

    Locally the deform maps the tile's (x, y, z) to r(s) * (x side + y normal) + s tangent, with
    s = k z and the side/normal pair turning at `twist` per unit - so its Jacobian in the frame is

        | r  0  a |     a = k (r' x - r twist y)     r' = dr/ds, the taper's slope
        | 0  r  b |     b = k (r' y + r twist x)
        | 0  0  k |

    and a normal goes by the inverse transpose. Scaled by r k to clear the fractions, that is
    (k nx, k ny, r nz - a nx - b ny). With no taper, no twist and r == k it is the plain rotation,
    which is why the rope - a straight, untapered, untwisted run - is unchanged by it.
*/
static vec3 CarriedNormal(const SplineDeformParams& params, const vertex& v, float s, float r, float k,
                          float range_start, float range_end){
    //A central difference: the taper is a smoothstep, so this is as good as the closed form and
    //does not have to know which ends are tapering.
    const float h = 1e-3f;
    float slope = params.scale * (SplineDeformTaper(params,s + h,range_start,range_end) -
                                  SplineDeformTaper(params,s - h,range_start,range_end)) / (2.0f * h);
    const vec3& n = v.normal;
    float a = k * (slope * v.pos.x - r * params.twist * v.pos.y);
    float b = k * (slope * v.pos.y + r * params.twist * v.pos.x);
    vec3 c = vec3(k * n.x,k * n.y,r * n.z - a * n.x - b * n.y);
    float l = c.length();
    if (l <= 1e-12f){
        return n;       //a degenerate scale: the rotation alone is the best there is
    }
    return c / l;
}

//The tangent squared off against the normal, so a normal map still has a basis on the surface.
static void SquareTangent(vertex& o){
    vec3 t = o.tangent - o.normal * o.normal.dot(o.tangent);
    float tl = t.length();
    if (tl > 1e-6f){
        o.tangent = t / tl;
    }
}

/*
    See SplineDeformParams::f_weld_seams. The pairs are found on the TILE, once, and every seam
    reuses them: copy c's high-ring vertex h meets copy c+1's low-ring vertex l.

    A position on a ring usually has several vertices (a UV seam duplicates them), so each vertex is
    paired with the most alike vertex at its position on the other ring, from both sides. Every
    average is taken from the unwelded normals before any is written, so a vertex in two pairs gets
    the same answer from each.
*/
static void WeldSeams(const std::vector<vertex>& tile, int copies, size_t first, std::vector<vertex>& out){
    float zmin = 0.0f, extent = 0.0f;
    if (!SplineDeformMeasure(tile,zmin,extent)){
        return;
    }
    //A ring is not quite flat in an authored tile - vine_trunk's lean by 0.0007 in a length of 1.
    float ring_tol = 0.005f * extent;
    float match_tol = 1e-4f * extent;
    std::vector<int> low, high;
    for (size_t i = 0; i < tile.size(); i++){
        if (tile[i].pos.z < zmin + ring_tol){ low.push_back((int)i); }
        if (tile[i].pos.z > zmin + extent - ring_tol){ high.push_back((int)i); }
    }
    struct Pair{ int h, l; };
    std::vector<Pair> pairs;
    //from_ring's vertices each look for their best partner on to_ring.
    for (int pass = 0; pass < 2; pass++){
        const std::vector<int>& from_ring = (pass == 0) ? high : low;
        const std::vector<int>& to_ring = (pass == 0) ? low : high;
        for (size_t i = 0; i < from_ring.size(); i++){
            const vertex& a = tile[from_ring[i]];
            int best = -1;
            float best_dot = 0.5f;      //cos 60: anything less alike is a hard edge, left alone
            for (size_t j = 0; j < to_ring.size(); j++){
                const vertex& b = tile[to_ring[j]];
                float dx = a.pos.x - b.pos.x, dy = a.pos.y - b.pos.y;
                if (dx * dx + dy * dy > match_tol * match_tol){
                    continue;
                }
                float d = a.normal.dot(b.normal);
                if (d > best_dot){
                    best_dot = d;
                    best = to_ring[j];
                }
            }
            if (best >= 0){
                Pair p;
                p.h = (pass == 0) ? from_ring[i] : best;
                p.l = (pass == 0) ? best : from_ring[i];
                pairs.push_back(p);
            }
        }
    }
    if (pairs.empty()){
        return;
    }

    size_t per = tile.size();
    std::vector<vec3> welded(pairs.size());
    for (int c = 0; c + 1 < copies; c++){
        size_t lower = first + (size_t)c * per;
        size_t upper = lower + per;
        for (size_t p = 0; p < pairs.size(); p++){
            vec3 n = out[lower + pairs[p].h].normal + out[upper + pairs[p].l].normal;
            float l = n.length();
            welded[p] = (l > 1e-12f) ? n / l : out[lower + pairs[p].h].normal;
        }
        for (size_t p = 0; p < pairs.size(); p++){
            vertex& vh = out[lower + pairs[p].h];
            vertex& vl = out[upper + pairs[p].l];
            vh.normal = welded[p];
            vl.normal = welded[p];
            SquareTangent(vh);
            SquareTangent(vl);
        }
    }
}

int DeformAlongSpline(const Spline& spline, const std::vector<vertex>& tile,
                      const SplineDeformParams& params, std::vector<vertex>& out){
    if (tile.size() < 3 || !spline.IsBuilt()){
        return 0;
    }
    float range_start = (params.start > 0.0f) ? params.start : 0.0f;
    float range_end = (params.end >= 0.0f && params.end < spline.GetLength()) ? params.end : spline.GetLength();
    float range = range_end - range_start;
    if (range <= 1e-4f){
        return 0;
    }

    float zmin = 0.0f;
    float extent = 0.0f;
    if (params.tile_length > 0.0f){
        zmin = params.tile_start;
        extent = params.tile_length;
    }else if (!SplineDeformMeasure(tile,zmin,extent)){
        return 0;
    }
    float tile_len = extent * params.scale;
    if (tile_len <= 1e-4f){
        return 0;
    }
    int copies = (int)floorf(range / tile_len + 0.5f);
    if (copies < 1){
        copies = 1;
    }
    //Stretched (or squeezed) so the copies end exactly at range_end - a gap or an overhang there
    //would be a sawn-off stump the taper cannot hide.
    float stretch = range / ((float)copies * tile_len);

    size_t first = out.size();
    out.reserve(first + tile.size() * (size_t)copies);
    for (int c = 0; c < copies; c++){
        float base = range_start + (float)c * tile_len * stretch;
        for (size_t i = 0; i < tile.size(); i++){
            const vertex& v = tile[i];
            float s = base + (v.pos.z - zmin) * params.scale * stretch;
            /*
                Past either end of the curve - an overlay's overhang on the first and last copy -
                the frame is carried on straight along the end tangent. FrameAt clamps, and a
                clamped overhang is squashed flat onto the end ring.
            */
            float on_curve = (s < 0.0f) ? 0.0f : ((s > spline.GetLength()) ? spline.GetLength() : s);
            SplineFrame f = spline.FrameAt(on_curve);
            f.position = f.position + f.tangent * (s - on_curve);
            float turn = params.roll + params.twist * s;
            if (turn != 0.0f){
                float ct = cosf(turn);
                float st = sinf(turn);
                vec3 side   = f.side * ct + f.normal * st;
                vec3 normal = f.normal * ct - f.side * st;
                f.side = side;
                f.normal = normal;
            }
            float r = params.scale * SplineDeformTaper(params,s,range_start,range_end);

            vertex o = v;
            o.pos = f.Place(v.pos.x * r,v.pos.y * r);
            o.normal = f.Rotate(CarriedNormal(params,v,s,r,params.scale * stretch,range_start,range_end));
            o.tangent = f.Rotate(v.tangent);
            SquareTangent(o);
            out.push_back(o);
        }
    }

    if (params.f_weld_seams && !params.f_flat_normals && copies > 1){
        WeldSeams(tile,copies,first,out);
    }

    if (params.f_flat_normals){
        for (size_t i = first; i + 2 < out.size(); i += 3){
            vec3 e1 = out[i + 1].pos - out[i].pos;
            vec3 e2 = out[i + 2].pos - out[i].pos;
            vec3 n = e1.cross(e2);
            float l = n.length();
            if (l <= 1e-12f){
                continue;       //a degenerate sliver keeps its carried normal
            }
            n = n / l;
            for (int k = 0; k < 3; k++){
                //The tangent is kept, squared off against the new normal, so a normal map on the
                //tile still has a basis that agrees with the surface.
                vec3 t = out[i + k].tangent - n * n.dot(out[i + k].tangent);
                float tl = t.length();
                out[i + k].normal = n;
                if (tl > 1e-6f){
                    out[i + k].tangent = t / tl;
                }
            }
        }
    }
    return copies;
}
