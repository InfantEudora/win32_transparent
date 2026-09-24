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
            o.normal = f.Rotate(v.normal);
            o.tangent = f.Rotate(v.tangent);
            out.push_back(o);
        }
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
