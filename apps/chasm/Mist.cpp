#include "Mist.h"
#include "MeshBuild.h"
#include "Object.h"

#include <chrono>
#include <cmath>
#include <map>

#define MIST_KEEP_ONE_IN    4       //floor vertices per puff, about
#define MIST_RADIUS_MIN     4.0f
#define MIST_RADIUS_MAX     7.6f
#define MIST_SQUASH         0.72f   //a puff's height against its width
#define MIST_ROLL           2.6f    //how far the blanket's top rises and falls over the chasm
#define MIST_ROLL_SCALE     38.0f   //world units over which it does
#define MIST_BOB            0.45f   //how far a puff rises and sinks in its cycle
#define MIST_BREATH         0.06f   //and how much it swells
//The swamp's mist: small, flat, pale wisps just over the pools.

#define FOAM_LIFE_MIN       2.4f    //seconds
#define FOAM_LIFE_MAX       3.8f
#define FOAM_RISE           7.0f    //world units over a life
#define FOAM_DRIFT          3.5f    //outward, away from the wall

namespace {

float Unit(uint32_t h){
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

//Smooth value noise in -1..1, for the blanket's slow roll.
float RollNoise(float x, float z){
    float fx = std::floor(x);
    float fz = std::floor(z);
    int32_t ix = (int32_t)fx;
    int32_t iz = (int32_t)fz;
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    auto corner = [](int32_t cx, int32_t cz){
        return Unit(MeshHash((uint32_t)cx,(uint32_t)cz,911)) * 2.0f - 1.0f;
    };
    float a = corner(ix,iz) + (corner(ix + 1,iz) - corner(ix,iz)) * tx;
    float b = corner(ix,iz + 1) + (corner(ix + 1,iz + 1) - corner(ix,iz + 1)) * tx;
    return a + (b - a) * tz;
}

}

void BuildMist(const Grid& g, const Terrain& t, const TerrainMeshData& mesh, MistData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = MistData();
    out.chunk_puffs.assign(mesh.chunks.size(),std::vector<int>());
    for (int v = 0; v < (int)g.fine.pos.size(); v++){
        if (t.level[v] != TERRAIN_FLOOR || g.fine.f_boundary[v]){
            continue;
        }
        uint32_t h = MeshHash((uint32_t)v,0x5157u);
        if (h % MIST_KEEP_ONE_IN != 0){
            continue;
        }
        const vec2& base = g.fine.pos[v];
        MistPuff p;
        vec2 at = base + vec2(Unit(MeshHash(h,1)) - 0.5f,Unit(MeshHash(h,2)) - 0.5f) * 2.0f;
        p.radius = MIST_RADIUS_MIN + (MIST_RADIUS_MAX - MIST_RADIUS_MIN) * Unit(MeshHash(h,3));
        float roll = RollNoise(at.x / MIST_ROLL_SCALE,at.y / MIST_ROLL_SCALE) * MIST_ROLL;
        //Its top near the blanket's, give or take: a big puff sits a little lower than a small one.
        float top = CHASM_MIST_TOP + roll + (Unit(MeshHash(h,4)) - 0.5f) * 1.6f;
        p.pos = vec3(at.x,top - p.radius * MIST_SQUASH * 0.85f,at.y);
        p.phase = Unit(MeshHash(h,5));
        p.period = 11.0f + 7.0f * Unit(MeshHash(h,6));
        //Lighter where the blanket rises, darker in its hollows - the shades follow the roll.
        float shade = roll / MIST_ROLL * 0.5f + 0.5f + (Unit(MeshHash(h,7)) - 0.5f) * 0.5f;
        p.variant = (uint8_t)((shade > 0.62f) ? 0 : (shade > 0.30f ? 1 : 2));
        int cx = std::max(0,std::min(mesh.chunks_x - 1,(int)((at.x - g.bounds_min.x) / TERRAIN_CHUNK_SIZE)));
        int cz = std::max(0,std::min(mesh.chunks_z - 1,(int)((at.y - g.bounds_min.y) / TERRAIN_CHUNK_SIZE)));
        p.bob = MIST_BOB;
        p.breath = MIST_BREATH;
        out.chunk_puffs[(size_t)cz * mesh.chunks_x + cx].push_back((int)out.puffs.size());
        out.puffs.push_back(p);
    }
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

/*
    An icosphere split once - 80 faces, which reads as a puff at the game's zoom and as the reference's
    faceted fog up close - with every vertex pushed a little in or out by hash so no two variants
    are the same lump. Flat-shaded: each face its own normal, which is what makes it faceted.
*/
void BuildPuffMesh(uint32_t variant, int column, int row, std::vector<vertex>& out){
    out.clear();
    const float a = 0.525731112f;
    const float b = 0.850650808f;
    std::vector<vec3> v = {
        {-a,b,0},{a,b,0},{-a,-b,0},{a,-b,0},{0,-a,b},{0,a,b},{0,-a,-b},{0,a,-b},{b,0,-a},{b,0,a},{-b,0,-a},{-b,0,a}
    };
    std::vector<int> f = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11, 1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9, 4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1
    };
    std::map<std::pair<int,int>,int> mids;
    auto Mid = [&](int i, int j){
        std::pair<int,int> key(std::min(i,j),std::max(i,j));
        auto it = mids.find(key);
        if (it != mids.end()){
            return it->second;
        }
        vec3 m = (v[i] + v[j]) * 0.5f;
        m.normalize();
        v.push_back(m);
        mids[key] = (int)v.size() - 1;
        return (int)v.size() - 1;
    };
    std::vector<int> split;
    for (size_t i = 0; i < f.size(); i += 3){
        int x = f[i], y = f[i + 1], z = f[i + 2];
        int xy = Mid(x,y), yz = Mid(y,z), zx = Mid(z,x);
        int faces[12] = {x,xy,zx, y,yz,xy, z,zx,yz, xy,yz,zx};
        split.insert(split.end(),faces,faces + 12);
    }
    for (size_t i = 0; i < v.size(); i++){
        float push = 0.93f + 0.14f * Unit(MeshHash((uint32_t)i,variant,0x9e37u));
        v[i] = v[i] * push;
        v[i].y *= MIST_SQUASH;
        //A flatter underside: nobody sees it, and a puff sitting on the blanket should look it.
        if (v[i].y < 0.0f){
            v[i].y *= 0.7f;
        }
    }
    for (size_t i = 0; i < split.size(); i += 3){
        vec3 p0 = v[split[i]], p1 = v[split[i + 1]], p2 = v[split[i + 2]];
        MeshTri(out,p0,p1,p2,(p0 + p1 + p2) * (1.0f / 3.0f),column,row);
    }
}

/*
    Where the puff stands when its cycle is at rest: the GPU bobs it sin(2 pi cycle) x bob and swells
    it 1 + cos(pi cycle) x breath about this (core's INSTANCE_MOTION_PUFF), which is the motion the
    CPU used to pose it with every frame.
*/
fmat4 MistRest(const MistPuff& p){
    return Object::ComposeTransformScale(p.pos,quat(vec3(0.0f,1.0f,0.0f),p.phase * 6.2831f),
                                         vec3(p.radius,p.radius * p.flat,p.radius));
}

void MistMotion(const MistPuff& p, vec4* out){
    out[0] = vec4(p.phase,p.period,p.bob,p.breath);
    out[1] = vec4(0.0f,0.0f,0.0f,0.0f);
}

/*
    One foam ball's life after another, from nothing kept: its slot has a life length and an offset
    of its own, the clock says which life it is in and how far through, and that life's number hashes
    where it starts. It starts small at the foot of the fall just inside the mist, swells quickly,
    and rises and drifts away from the wall as it shrinks to nothing.
*/
void FoamBall(const TerrainFall& f, int fall_index, int slot, double seconds, vec3& pos, float& radius){
    uint32_t h = MeshHash((uint32_t)fall_index,(uint32_t)slot,0xF0A3u);
    float life = FOAM_LIFE_MIN + (FOAM_LIFE_MAX - FOAM_LIFE_MIN) * Unit(h);
    double local = seconds / life + Unit(MeshHash(h,1));
    double cycle = std::floor(local);
    float age = (float)(local - cycle);
    uint32_t hl = MeshHash(h,(uint32_t)(int64_t)cycle,2);
    float scale = f.width / 6.0f;
    float across = (Unit(MeshHash(hl,3)) - 0.5f) * f.width * 1.4f;
    float out = 3.6f + 2.5f * Unit(MeshHash(hl,4));
    float r0 = (1.8f + 1.8f * Unit(MeshHash(hl,5))) * scale;
    float rise = FOAM_RISE * (1.0f - (1.0f - age) * (1.0f - age)) * (0.6f + 0.6f * Unit(MeshHash(hl,6)));
    vec2 at = f.lip + f.across * across + f.out * (out + FOAM_DRIFT * age);
    pos = vec3(at.x,CHASM_MIST_TOP - 1.5f + rise,at.y);
    radius = r0 * ((age < 0.18f) ? age / 0.18f : (1.0f - age) / 0.82f);
}
