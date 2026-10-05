#include "WaterMesh.h"
#include "Mist.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#define WATER_CELL_DIP      0.15f   //a cell a channel lowers this much at a corner gets water
#define WATER_REACH         8.0f    //how far past its edge a river still names a water vertex

namespace {

//One triangle facing `want`, with the shader's coordinates as UVs.
void WaterTri(std::vector<vertex>& out, vec3 a, vec2 ua, vec3 b, vec2 ub, vec3 c, vec2 uc, const vec3& want){
    vec3 n = (b - a).cross(c - a);
    if (n.dot(want) < 0.0f){
        std::swap(b,c);
        std::swap(ub,uc);
        n = n * -1.0f;
    }
    if (n.length() < 1e-9f){
        return;
    }
    n.normalize();
    vec3 tangent = b - a;
    tangent.normalize();
    vertex v = {};
    v.normal = n;
    v.tangent = tangent;
    v.matid = 0;
    v.pos = a; v.uv = ua; out.push_back(v);
    v.pos = b; v.uv = ub; out.push_back(v);
    v.pos = c; v.uv = uc; out.push_back(v);
}

/*
    A fall's profile: (how far out from the rim line, how far down from the water). It leaves the
    lip in a curl and is clear of the wall - whose strata stand out up to WALL_ROUGHNESS past the
    cut, itself half a cell past the line - by the third row; after that it drops straight.
*/
const float fall_out[] = {0.0f,1.6f,2.8f,3.6f,4.1f};
const float fall_down[] = {0.0f,0.7f,3.0f,8.0f,0.0f};    //the last row is set from the mist
const int fall_rows = 5;
#define FALL_BELOW_MIST     10.0f   //how far the sheet runs on down inside the mist

}

void BuildWaterMesh(const Grid& g, const Terrain& t, WaterMeshData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = WaterMeshData();
    const vec3 up(0.0f,1.0f,0.0f);

    //--- The rivers' surface ------------------------------------------------------------------------
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        const GridQuad& quad = g.fine.quads[q];
        vec2 p[4];
        bool high[4];
        int num_high = 0;
        float dip = 0.0f;
        for (int k = 0; k < 4; k++){
            p[k] = g.fine.pos[quad.v[k]];
            high[k] = (t.level[quad.v[k]] == TERRAIN_PLATEAU);
            num_high += high[k] ? 1 : 0;
            dip = std::max(dip,t.RiverDip(p[k]));
        }
        if (num_high == 0 || dip < WATER_CELL_DIP){
            continue;
        }
        //The plateau part of the cell, cut at the edge midpoints as the terrain cuts it.
        std::vector<vec2> poly;
        for (int k = 0; k < 4; k++){
            if (high[k]){
                poly.push_back(p[k]);
            }
            if (high[k] != high[(k + 1) % 4]){
                poly.push_back((p[k] + p[(k + 1) % 4]) * 0.5f);
            }
        }
        std::vector<vec2> uv(poly.size());
        bool f_named = true;
        for (size_t i = 0; i < poly.size(); i++){
            int river;
            float along, across;
            if (!t.RiverCoords(poly[i],WATER_REACH,river,along,across)){
                f_named = false;
                break;
            }
            uv[i] = vec2(across,along);
        }
        if (!f_named){
            continue;
        }
        for (size_t i = 1; i + 1 < poly.size(); i++){
            WaterTri(out.flat,
                     vec3(poly[0].x,TERRAIN_WATER_Y,poly[0].y),uv[0],
                     vec3(poly[i].x,TERRAIN_WATER_Y,poly[i].y),uv[i],
                     vec3(poly[i + 1].x,TERRAIN_WATER_Y,poly[i + 1].y),uv[i + 1],up);
        }
    }

    //--- The falls --------------------------------------------------------------------------------
    for (const TerrainFall& f : t.falls){
        vec3 row_l[fall_rows], row_r[fall_rows];
        float arc[fall_rows];
        float bottom = CHASM_MIST_TOP - FALL_BELOW_MIST;
        float run = 0.0f;
        float mist_arc = 0.0f;
        for (int r = 0; r < fall_rows; r++){
            float y = (r + 1 < fall_rows) ? TERRAIN_WATER_Y - fall_down[r] : bottom;
            vec2 c = f.lip + f.out * fall_out[r];
            //A little narrower than the river where it goes over, spreading as it falls.
            float spread = (r + 1 < fall_rows) ? 0.92f + 0.05f * r : 1.35f;
            vec2 half = f.across * (f.width * 0.5f * spread);
            row_l[r] = vec3(c.x - half.x,y,c.y - half.y);
            row_r[r] = vec3(c.x + half.x,y,c.y + half.y);
            if (r > 0){
                vec3 prev = (row_l[r - 1] + row_r[r - 1]) * 0.5f;
                vec3 here = (row_l[r] + row_r[r]) * 0.5f;
                float seg = (here - prev).length();
                if (r + 1 == fall_rows){
                    //Where along the last, straight drop the mist's top is.
                    float frac = (prev.y - CHASM_MIST_TOP) / std::max(1e-3f,prev.y - here.y);
                    mist_arc = run + seg * frac;
                }
                run += seg;
            }
            arc[r] = run;
        }
        for (int r = 0; r + 1 < fall_rows; r++){
            float v0 = arc[r] / mist_arc;
            float v1 = arc[r + 1] / mist_arc;
            //Facing out of the chasm wall: the side a camera over the chasm sees.
            vec3 want(f.out.x,0.35f,f.out.y);
            WaterTri(out.sheets,row_l[r],vec2(-1.0f,v0),row_r[r],vec2(1.0f,v0),row_r[r + 1],vec2(1.0f,v1),want);
            WaterTri(out.sheets,row_l[r],vec2(-1.0f,v0),row_r[r + 1],vec2(1.0f,v1),row_l[r + 1],vec2(-1.0f,v1),want);
        }
    }
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}
