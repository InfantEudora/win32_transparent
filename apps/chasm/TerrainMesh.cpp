#include "TerrainMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>

#define WALL_BAND_HEIGHT    6.0f    //a rock stratum; also the wall's vertical resolution
#define WALL_ROUGHNESS      1.4f    //how far a stratum may stand out or sit back, world units
#define WALL_NOISE_SCALE    7.0f    //world units over which one stratum's offset changes

namespace {

uint32_t Hash3(int32_t x, int32_t z, int band){
    uint32_t h = (uint32_t)x * 0x8DA6B343u ^ (uint32_t)z * 0xD8163841u ^ (uint32_t)band * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x5BD1E995u;
    h ^= h >> 15;
    return h;
}

//Smooth value noise in -1..1: hashed lattice corners, blended with smoothstep.
float ValueNoise(float x, float z, int band){
    float fx = std::floor(x);
    float fz = std::floor(z);
    int32_t ix = (int32_t)fx;
    int32_t iz = (int32_t)fz;
    float tx = x - fx;
    float tz = z - fz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    auto corner = [band](int32_t cx, int32_t cz){
        return (float)(Hash3(cx,cz,band) & 0xFFFF) / 32767.5f - 1.0f;
    };
    float a = corner(ix,iz) + (corner(ix + 1,iz) - corner(ix,iz)) * tx;
    float b = corner(ix,iz + 1) + (corner(ix + 1,iz + 1) - corner(ix,iz + 1)) * tx;
    return a + (b - a) * tz;
}

/*
    How far a wall point strays sideways at band row `band`. A function of world position alone -
    a wall point is an edge midpoint, which both cells on the edge compute bit for bit the same, so
    both get the same offset and the walls meet. Smooth along the wall and different per row, so
    each stratum moves as one ledge rather than every vertex crumpling on its own.
*/
vec2 Jitter(const vec2& p, int band){
    float x = p.x / WALL_NOISE_SCALE;
    float z = p.y / WALL_NOISE_SCALE;
    return vec2(ValueNoise(x,z,band * 2),ValueNoise(x + 31.7f,z + 17.3f,band * 2 + 1)) * WALL_ROUGHNESS;
}

//Strata: a band's rock, by band index, so a stratum runs at one height along the whole wall.
int RockSlot(int band){
    static const int pattern[5] = {TERRAIN_SLOT_ROCK_A,TERRAIN_SLOT_ROCK_B,TERRAIN_SLOT_ROCK_A,
                                   TERRAIN_SLOT_ROCK_A,TERRAIN_SLOT_ROCK_B};
    return pattern[band % 5];
}

int LevelSlot(int level){
    return (level == TERRAIN_FLOOR) ? TERRAIN_SLOT_FLOOR : TERRAIN_SLOT_GROUND;
}

class Builder{
public:
    Builder(const Grid& g, TerrainMeshData& out) : grid(g), data(out){
        origin = g.bounds_min;
        vec2 size = g.bounds_max - g.bounds_min;
        data.chunks_x = std::max(1,(int)std::ceil(size.x / TERRAIN_CHUNK_SIZE));
        data.chunks_z = std::max(1,(int)std::ceil(size.y / TERRAIN_CHUNK_SIZE));
        data.chunks.assign((size_t)data.chunks_x * data.chunks_z,TerrainChunk());
    }

    void SetChunkAt(const vec2& p){
        int cx = std::max(0,std::min(data.chunks_x - 1,(int)((p.x - origin.x) / TERRAIN_CHUNK_SIZE)));
        int cz = std::max(0,std::min(data.chunks_z - 1,(int)((p.y - origin.y) / TERRAIN_CHUNK_SIZE)));
        chunk = &data.chunks[cz * data.chunks_x + cx];
    }

    //Wound so its face normal agrees with `want`: the renderer culls back faces, and which way
    //round a generated triangle comes out is otherwise an accident of the cell's corner order.
    void Tri(vec3 a, vec3 b, vec3 c, const vec3& want, int slot){
        vec3 n = (b - a).cross(c - a);
        if (n.dot(want) < 0.0f){
            std::swap(b,c);
            n = n * -1.0f;
        }
        if (n.length() < 1e-9f){
            return;
        }
        n.normalize();
        vec3 tangent = b - a;
        if (tangent.length() > 1e-9f){
            tangent.normalize();
        }
        vertex v = {};
        v.normal = n;
        v.tangent = tangent;
        v.uv = vec2(0.0f,0.0f);
        v.matid = slot;
        v.pos = a; chunk->verts.push_back(v);
        v.pos = b; chunk->verts.push_back(v);
        v.pos = c; chunk->verts.push_back(v);
        data.num_triangles++;
    }

    void Fan(const std::vector<vec3>& p, const vec3& want, int slot){
        for (size_t i = 1; i + 1 < p.size(); i++){
            Tri(p[0],p[i],p[i + 1],want,slot);
        }
    }

    /*
        A wall from `top` down to `bottom_a`/`bottom_b` along a->b, facing `out`, cut into bands.
        The first and last rows stay exactly on the cut, so the wall meets the ground above and
        below it; the rows between stray by Jitter.
    */
    void Wall(const vec2& a, const vec2& b, float top, float bottom_a, float bottom_b, const vec3& out){
        float drop = top - std::min(bottom_a,bottom_b);
        int bands = std::max(1,(int)std::ceil(drop / WALL_BAND_HEIGHT - 0.01f));
        for (int j = 0; j < bands; j++){
            float t0 = (float)j / bands;
            float t1 = (float)(j + 1) / bands;
            vec2 a0 = a + ((j > 0) ? Jitter(a,j) : vec2(0.0f,0.0f));
            vec2 b0 = b + ((j > 0) ? Jitter(b,j) : vec2(0.0f,0.0f));
            vec2 a1 = a + ((j + 1 < bands) ? Jitter(a,j + 1) : vec2(0.0f,0.0f));
            vec2 b1 = b + ((j + 1 < bands) ? Jitter(b,j + 1) : vec2(0.0f,0.0f));
            vec3 A0(a0.x,top + (bottom_a - top) * t0,a0.y);
            vec3 B0(b0.x,top + (bottom_b - top) * t0,b0.y);
            vec3 A1(a1.x,top + (bottom_a - top) * t1,a1.y);
            vec3 B1(b1.x,top + (bottom_b - top) * t1,b1.y);
            int slot = RockSlot(j);
            int before = data.num_triangles;
            Tri(A0,B0,B1,out,slot);
            Tri(A0,B1,A1,out,slot);
            data.num_wall_triangles += data.num_triangles - before;
        }
    }

    void Cell(int q, const Terrain& t){
        const GridQuad& quad = grid.fine.quads[q];
        vec2 p[4];
        int lv[4];
        float h[4];
        float hmax = -1e30f;
        vec2 centre(0.0f,0.0f);
        for (int k = 0; k < 4; k++){
            p[k] = grid.fine.pos[quad.v[k]];
            lv[k] = t.level[quad.v[k]];
            h[k] = terrain_levels[lv[k]].height;
            hmax = std::max(hmax,h[k]);
            centre += p[k];
        }
        SetChunkAt(centre * 0.25f);
        const vec3 up(0.0f,1.0f,0.0f);

        bool high[4];
        int num_high = 0;
        int high_level = lv[0];
        for (int k = 0; k < 4; k++){
            high[k] = (h[k] == hmax);
            if (high[k]){
                num_high++;
                high_level = lv[k];
            }
        }
        if (num_high == 4){
            std::vector<vec3> poly;
            for (int k = 0; k < 4; k++){
                poly.push_back(vec3(p[k].x,h[k],p[k].y));
            }
            Fan(poly,up,LevelSlot(lv[0]));
            return;
        }

        //Edge k runs corner k -> k+1; it is cut at its midpoint when it changes side.
        vec2 m[4];
        for (int k = 0; k < 4; k++){
            m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
        }

        //The high part: every high corner and every cut, in order - one polygon even in the saddle.
        std::vector<vec3> poly;
        for (int k = 0; k < 4; k++){
            if (high[k]){
                poly.push_back(vec3(p[k].x,hmax,p[k].y));
            }
            if (high[k] != high[(k + 1) % 4]){
                poly.push_back(vec3(m[k].x,hmax,m[k].y));
            }
        }
        Fan(poly,up,LevelSlot(high_level));

        //Each run of low corners: its ground, and the wall down to it from the cut.
        for (int k = 0; k < 4; k++){
            if (!(high[k] && !high[(k + 1) % 4])){
                continue;
            }
            //The run starts at corner k+1, entering through edge k.
            std::vector<vec3> low;
            int first = (k + 1) % 4;
            int j = first;
            vec2 low_centre(0.0f,0.0f);
            int count = 0;
            low.push_back(vec3(m[k].x,h[first],m[k].y));
            while (!high[j]){
                low.push_back(vec3(p[j].x,h[j],p[j].y));
                low_centre += p[j];
                count++;
                j = (j + 1) % 4;
            }
            int last = (j + 3) % 4;
            //Leaves through edge last -> j.
            low.push_back(vec3(m[last].x,h[last],m[last].y));
            Fan(low,up,LevelSlot(lv[first]));

            vec2 a = m[k];
            vec2 b = m[last];
            vec2 along = b - a;
            vec2 side(-along.y,along.x);
            vec2 to_low = low_centre / (float)count - (a + b) * 0.5f;
            if (side.dot(to_low) < 0.0f){
                side = -side;
            }
            Wall(a,b,hmax,h[first],h[last],vec3(side.x,0.0f,side.y));
        }
    }

    /*
        The skirt: every edge on the map's outline dropped to TERRAIN_SKIRT_BOTTOM, facing out. An
        edge whose ends are on different levels - the chasm's mouth on the south edge - steps at
        its midpoint, which is exactly where the cliff wall inside meets it.
    */
    void Skirt(const Terrain& t){
        std::unordered_map<uint64_t,int> uses;
        uses.reserve(grid.fine.quads.size() * 3);
        auto key = [](int a, int b){
            if (a > b){
                std::swap(a,b);
            }
            return ((uint64_t)(uint32_t)a << 32) | (uint64_t)(uint32_t)b;
        };
        for (const GridQuad& q : grid.fine.quads){
            for (int k = 0; k < 4; k++){
                uses[key(q.v[k],q.v[(k + 1) % 4])]++;
            }
        }
        for (const GridQuad& q : grid.fine.quads){
            for (int k = 0; k < 4; k++){
                int ia = q.v[k];
                int ib = q.v[(k + 1) % 4];
                if (uses[key(ia,ib)] != 1){
                    continue;
                }
                vec2 a = grid.fine.pos[ia];
                vec2 b = grid.fine.pos[ib];
                vec2 mid = (a + b) * 0.5f;
                SetChunkAt(mid);
                //The quad is wound positively, so it lies to the left of a->b: outward is right.
                vec2 along = b - a;
                vec3 out(along.y,0.0f,-along.x);
                float ha = t.Height(ia);
                float hb = t.Height(ib);
                if (ha == hb){
                    SkirtPanel(a,b,ha,out);
                }else{
                    SkirtPanel(a,mid,ha,out);
                    SkirtPanel(mid,b,hb,out);
                }
            }
        }
    }

    void SkirtPanel(const vec2& a, const vec2& b, float top, const vec3& out){
        vec3 A0(a.x,top,a.y), B0(b.x,top,b.y);
        vec3 A1(a.x,TERRAIN_SKIRT_BOTTOM,a.y), B1(b.x,TERRAIN_SKIRT_BOTTOM,b.y);
        Tri(A0,B0,B1,out,TERRAIN_SLOT_ROCK_B);
        Tri(A0,B1,A1,out,TERRAIN_SLOT_ROCK_B);
    }

private:
    const Grid& grid;
    TerrainMeshData& data;
    vec2 origin;
    TerrainChunk* chunk = NULL;
};

}

void BuildTerrainMesh(const Grid& g, const Terrain& t, TerrainMeshData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = TerrainMeshData();
    Builder b(g,out);
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        b.Cell(q,t);
    }
    b.Skirt(t);
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}
