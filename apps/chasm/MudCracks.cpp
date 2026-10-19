#include "MudCracks.h"
#include "Terrain.h"

#include <algorithm>
#include <cmath>

/*
    THE TUNING: the bench's uniforms as the user set them on 2026-10-18 - the look's, not rules. World units
    unless said. The bench's seed was -7, which is seed 4294967289 here (`chasm_generate` takes -7 as that): the
    shader's int read as the game's unsigned seed, which is all Salt() ever sees.
*/
#define CRACKS_PLATE_SIZE       30.0f   //between plate seeds
#define CRACKS_PLATE_JITTER     1.15f   //0 a square grid of seeds, 1 anywhere in their cell
#define CRACKS_WARP             -0.14f  //of a plate, how far its borders wander
#define CRACKS_WIGGLE           1.05f   //the fine waver along every border
#define CRACKS_SUB_SIZE         10.0f   //the pieces a plate breaks into
#define CRACKS_RIFT_OPEN        36.16f  //half the opening, where it is widest
#define CRACKS_RIFT_WANDER      68.94f  //how far the spine swings east and west
#define CRACKS_RIFT_TIP         1.11f   //of the depth, from the south edge: past 1, the rift runs off the north edge
#define CRACKS_ISLAND_BAND      0.5f    //plates across, about the spine, that may stay behind as islands
#define CRACKS_ISLAND_CHANCE    1.0f
#define CRACKS_CRACK_REACH      79.0f   //how far from the chasm side cracks are open
#define CRACKS_CRACK_WIDTH      2.1f    //half-width of a side crack where it leaves the chasm
#define CRACKS_CRACK_FAR        0.05f   //chance a border beyond the reach is still a shallow crack
#define CRACKS_CRUMBLE          0.7f    //chance a piece at the rim breaks away
#define CRACKS_CRUMBLE_REACH    8.0f    //how far back from the rim a piece's seed may be: about touching it
#define CRACKS_SLUMP_CHANCE     0.25f   //chance a whole plate on the rim slumps two steps: a balcony

//The game's heights, as Terrain.cpp's HEIGHT_* have them.
#define CRACKS_ISLAND_STEPS     4
#define CRACKS_BALCONY_STEPS    2
#define CRACKS_CRACK_DEEPEST    5

//Which way a plate went when the ground opened.
#define G_WEST      0
#define G_ISLAND    1
#define G_EAST      2

namespace {

//ChasmLayout.cpp's, and the shader's.
uint32_t HashInts(int x, int z, uint32_t s){
    uint32_t h = s * 0x9E3779B1u ^ (uint32_t)x * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float Hash01(uint32_t h){
    return (float)(h & 0xFFFFu) / 65535.0f;
}

//Smooth value noise, -1..1, varying over about `wavelength`.
float Noise(const vec2& p, float wavelength, uint32_t s){
    float fx = p.x / wavelength;
    float fz = p.y / wavelength;
    float ix = std::floor(fx);
    float iz = std::floor(fz);
    float tx = fx - ix;
    float tz = fz - iz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    int x = (int)ix;
    int z = (int)iz;
    auto v = [s](int a, int b){ return (float)(HashInts(a,b,s) & 0xFFFFu) / 32767.5f - 1.0f; };
    float a = v(x,z) + (v(x + 1,z) - v(x,z)) * tx;
    float b = v(x,z + 1) + (v(x + 1,z + 1) - v(x,z + 1)) * tx;
    return a + (b - a) * tz;
}

float Clamp01(float x){
    return std::max(0.0f,std::min(1.0f,x));
}

}

MudCracks::MudCracks(uint32_t seed, const vec2& lo, const vec2& hi) :
    seed(seed), west(lo.x), south(hi.y), width(hi.x - lo.x), depth(hi.y - lo.y){
    /*
        Every plate a point of the map can land in: a point is moved by up to the widest opening either way and
        by the warp, and a Voronoi looks two cells past the one it lands in. One asked for outside this is still
        made, just not kept - the same plate either way.
    */
    float reach = 1.3f * CRACKS_RIFT_OPEN + 1.25f * std::fabs(CRACKS_WARP) * CRACKS_PLATE_SIZE + CRACKS_WIGGLE;
    plate_x0 = (int)std::floor(-reach / CRACKS_PLATE_SIZE) - 3;
    plate_y0 = plate_x0;
    plate_w = (int)std::floor((width + reach) / CRACKS_PLATE_SIZE) + 4 - plate_x0;
    plate_h = (int)std::floor((depth + reach) / CRACKS_PLATE_SIZE) + 4 - plate_y0;
    plates.resize((size_t)plate_w * plate_h);
    for (int j = 0; j < plate_h; j++){
        for (int i = 0; i < plate_w; i++){
            plates[(size_t)j * plate_w + i] = MakePlate(plate_x0 + i,plate_y0 + j);
        }
    }
}

//--- the rift ------------------------------------------------------------------------------------------------
float MudCracks::SpineX(float y) const{
    float swing = 0.75f * Noise(vec2(0.0f,y),170.0f,Salt(11u)) + 0.25f * Noise(vec2(0.0f,y),60.0f,Salt(12u));
    return 0.5f * width + CRACKS_RIFT_WANDER * swing;
}

//Half the opening at y: widest in the south, closing to nothing at the tip. North of the tip the plates still
//meet along the same line, which reads as the crack running on, shut.
float MudCracks::OpenFull(float y) const{
    float tip = CRACKS_RIFT_TIP * depth;
    float t = Clamp01((tip - y) / (0.5f * tip));
    return CRACKS_RIFT_OPEN * std::sqrt(t) * (1.0f + 0.3f * Noise(vec2(y,0.0f),90.0f,Salt(13u)));
}

//Which way a plate went, from where its seed was before anything opened: a plate is decided whole, never point
//by point, which is what keeps every edge of the chasm an edge of a plate.
int MudCracks::CellGroup(const vec2& c, int ix, int iy) const{
    float side = c.x - SpineX(c.y);
    float o = OpenFull(c.y);
    float h = Hash01(HashInts(ix,iy,Salt(21u)));
    bool f_island = o > 0.5f * CRACKS_PLATE_SIZE && std::fabs(side) < 0.5f * CRACKS_ISLAND_BAND * CRACKS_PLATE_SIZE &&
                    h < CRACKS_ISLAND_CHANCE;
    return f_island ? G_ISLAND : (side < 0.0f ? G_WEST : G_EAST);
}

//--- plates ----------------------------------------------------------------------------------------------------
//The space plates are cut in: borders that wander at the plate's scale, and waver at a plot's.
vec2 MudCracks::Warp(const vec2& q) const{
    const float a = CRACKS_WARP * CRACKS_PLATE_SIZE;
    vec2 w = a * vec2(Noise(q,1.1f * CRACKS_PLATE_SIZE,Salt(1u)),Noise(q,1.1f * CRACKS_PLATE_SIZE,Salt(2u)));
    w += 0.25f * a * vec2(Noise(q,0.3f * CRACKS_PLATE_SIZE,Salt(3u)),Noise(q,0.3f * CRACKS_PLATE_SIZE,Salt(4u)));
    w += CRACKS_WIGGLE * vec2(Noise(q,3.0f,Salt(5u)),Noise(q,3.0f,Salt(6u)));
    return q + w;
}

vec2 MudCracks::SeedOf(int ix, int iy, float size, uint32_t salt) const{
    float ox = Hash01(HashInts(ix,iy,salt)) - 0.5f;
    float oy = Hash01(HashInts(ix,iy,salt + 1u)) - 0.5f;
    return vec2(((float)ix + 0.5f + ox * CRACKS_PLATE_JITTER) * size,((float)iy + 0.5f + oy * CRACKS_PLATE_JITTER) * size);
}

MudCracks::Plate MudCracks::MakePlate(int ix, int iy) const{
    Plate p;
    p.c = SeedOf(ix,iy,CRACKS_PLATE_SIZE,Salt(0u));
    p.grp = CellGroup(p.c,ix,iy);
    return p;
}

MudCracks::Plate MudCracks::PlateAt(int ix, int iy) const{
    int i = ix - plate_x0;
    int j = iy - plate_y0;
    if (i < 0 || j < 0 || i >= plate_w || j >= plate_h){
        return MakePlate(ix,iy);
    }
    return plates[(size_t)j * plate_w + i];
}

/*
    Voronoi with the exact distance to the border (two passes, the second over 5x5 round the nearest seed): the
    border distance is what makes cracks of an even width. Over the plates, with their groups and the distance
    to the rim; or over a plate's pieces, without.
*/
MudCracks::Vor MudCracks::Voronoi(const vec2& w, bool f_plates) const{
    const float size = f_plates ? CRACKS_PLATE_SIZE : CRACKS_SUB_SIZE;
    const uint32_t salt = Salt(f_plates ? 0u : 40u);
    const int bx = (int)std::floor(w.x / size);
    const int by = (int)std::floor(w.y / size);
    float best = 1e20f;
    Vor v;
    v.id[0] = bx;
    v.id[1] = by;
    v.c = w;
    for (int j = -1; j <= 1; j++){
        for (int i = -1; i <= 1; i++){
            vec2 c = f_plates ? PlateAt(bx + i,by + j).c : SeedOf(bx + i,by + j,size,salt);
            float d = (c - w).dot(c - w);
            if (d < best){
                best = d;
                v.id[0] = bx + i;
                v.id[1] = by + j;
                v.c = c;
            }
        }
    }
    v.grp = f_plates ? PlateAt(v.id[0],v.id[1]).grp : 0;
    v.nid[0] = v.id[0];
    v.nid[1] = v.id[1];
    //Only the crumbling reads b_rim, and only this close.
    const float rim_needed = CRACKS_CRUMBLE_REACH + 1.5f * CRACKS_SUB_SIZE;
    const vec2 mr = v.c - w;
    for (int j = -2; j <= 2; j++){
        for (int i = -2; i <= 2; i++){
            if (i == 0 && j == 0){
                continue;
            }
            int ix = v.id[0] + i;
            int iy = v.id[1] + j;
            Plate pl;
            if (f_plates){
                pl = PlateAt(ix,iy);
            }else{
                pl.c = SeedOf(ix,iy,size,salt);
            }
            vec2 r = pl.c - w;
            vec2 dr = r - mr;
            float d = (0.5f * (mr + r)).dot(dr * (1.0f / std::sqrt(std::max(dr.dot(dr),1e-9f))));
            if (d < v.b){
                v.b = d;
                v.nid[0] = ix;
                v.nid[1] = iy;
            }
            if (f_plates && d < v.b_rim && d < rim_needed && pl.grp != v.grp){
                v.b_rim = d;
            }
        }
    }
    return v;
}

//Whether a plate stands on the rim, from its seed and its neighbours', so the whole plate agrees. Neighbours by
//grid cell and distance rather than by shared border, which is near enough at this jitter.
bool MudCracks::OnRim(int ix, int iy, const vec2& c, int grp) const{
    for (int j = -1; j <= 1; j++){
        for (int i = -1; i <= 1; i++){
            if (i == 0 && j == 0){
                continue;
            }
            Plate n = PlateAt(ix + i,iy + j);
            if ((n.c - c).dot(n.c - c) < 2.25f * CRACKS_PLATE_SIZE * CRACKS_PLATE_SIZE && n.grp != grp){
                return true;
            }
        }
    }
    return false;
}

//The same for the cell on either side of a border, whichever side asks.
uint32_t MudCracks::EdgeHash(const int a[2], const int b[2], uint32_t k) const{
    uint32_t ka = HashInts(a[0],a[1],Salt(30u));
    uint32_t kb = HashInts(b[0],b[1],Salt(30u));
    return HashInts((int)std::min(ka,kb),(int)std::max(ka,kb),Salt(k));
}

//--- the ground ------------------------------------------------------------------------------------------------
//The shader's Sample(), mode 0 (pull-apart), fully open, without what only colours it.
int MudCracks::Steps(const vec2& world) const{
    const vec2 p(world.x - west,south - world.y);
    const float side = p.x - SpineX(p.y);
    const float o = OpenFull(p.y);

    //Which plate p is on: one Voronoi per way a plate can have moved, and only where a plate that moved that way
    //could reach.
    const float far = CRACKS_PLATE_SIZE * (1.2f + 1.3f * CRACKS_WARP) + CRACKS_WIGGLE + 2.0f;
    Vor v;
    vec2 w;
    bool f_hit = false;
    if (side > o - far){
        w = Warp(p - vec2(o,0.0f));
        v = Voronoi(w,true);
        f_hit = v.grp == G_EAST;
    }
    if (!f_hit && side < far - o){
        w = Warp(p + vec2(o,0.0f));
        v = Voronoi(w,true);
        f_hit = v.grp == G_WEST;
    }
    if (!f_hit && std::fabs(side) < 0.5f * CRACKS_ISLAND_BAND * CRACKS_PLATE_SIZE + far){
        w = Warp(p);
        v = Voronoi(w,true);
        f_hit = v.grp == G_ISLAND;
    }
    if (!f_hit){
        return TERRAIN_FLOOR_STEPS;
    }

    int top = v.grp == G_ISLAND ? CRACKS_ISLAND_STEPS : 0;
    bool f_plateau = v.grp != G_ISLAND;
    //A plate on the rim may slump two steps in one piece: a balcony a whole plate wide.
    if (v.grp != G_ISLAND && std::fabs(side) - o < 2.5f * CRACKS_PLATE_SIZE && OpenFull(v.c.y) > 0.4f * CRACKS_PLATE_SIZE &&
        Hash01(HashInts(v.id[0],v.id[1],Salt(23u))) < CRACKS_SLUMP_CHANCE && OnRim(v.id[0],v.id[1],v.c,v.grp)){
        top = CRACKS_BALCONY_STEPS;
        f_plateau = false;
    }
    int steps = top;

    //1 at the rim, 0 from the reach on: what opens the side cracks.
    const float near = Clamp01(1.0f - std::max(std::fabs(side) - o,0.0f) / CRACKS_CRACK_REACH);

    //The pieces of a rim plate break away, each decided by its own seed - how far IT stands from the rim - so a
    //piece breaks whole; and only where the rift has opened, since north of the tip nothing has anywhere to fall.
    if (v.b_rim < CRACKS_CRUMBLE_REACH + 1.5f * CRACKS_SUB_SIZE){
        Vor s = Voronoi(w,false);
        Vor ps = Voronoi(s.c,true);
        if (ps.id[0] == v.id[0] && ps.id[1] == v.id[1] && ps.b_rim < CRACKS_CRUMBLE_REACH){
            float opened = Clamp01(OpenFull(s.c.y) / (0.5f * CRACKS_SUB_SIZE));
            if (Hash01(HashInts(s.id[0],s.id[1],Salt(41u))) < CRACKS_CRUMBLE * opened){
                float h2 = Hash01(HashInts(s.id[0],s.id[1],Salt(42u)));
                if (h2 < 0.2f){
                    return TERRAIN_FLOOR_STEPS;
                }
                if (h2 < 0.55f && f_plateau){
                    steps = top + CRACKS_BALCONY_STEPS;
                }else{
                    steps = top + 1 + (int)std::floor(5.0f * Hash01(HashInts(s.id[0],s.id[1],Salt(43u))));
                }
            }
        }
    }

    //A border with a plate that went the same way: a crack, deep and wide by the chasm, a shallow one now and then
    //away from it.
    if (v.b < 4.0f * CRACKS_CRACK_WIDTH + 1.0f && PlateAt(v.nid[0],v.nid[1]).grp == v.grp){
        uint32_t e = EdgeHash(v.id,v.nid,50u);
        bool f_open = Hash01(e) < CRACKS_CRACK_FAR * (1.0f - near) + near;
        if (f_open){
            float along = 0.75f + 0.25f * Noise(w,7.0f,Salt(51u));
            float hw = CRACKS_CRACK_WIDTH * (0.35f + 1.65f * near) * along * (0.7f + 0.6f * Hash01(e >> 16));
            if (v.b < hw){
                int d = top + 1 + (int)std::floor((float)(CRACKS_CRACK_DEEPEST - 1) * near + 0.5f);
                steps = std::max(steps,std::min(d,TERRAIN_FLOOR_STEPS));
            }
        }
    }
    return steps;
}
