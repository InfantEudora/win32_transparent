#include "TerrainMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include "Palette.h"

/*
    THE WALLS' LOOK (2026-10-06), from the Blender model the user chose (art_source/chasm/
    chasm_terraces.blend, apps/chasm/tools/blender_chasm_terraces.py - the same rules, in Python). All
    world units. A top has turf with a dark band of soil under it, the turf overhanging the rock; the
    rock stands in strata of TERRAIN_STEP, each column's stratum standing out or sitting back on its own,
    with a short chamfer from one stratum to the next.
*/
#define WALL_TURF           0.30f   //the turf's side, under every top
#define WALL_TURF_UNDER     0.40f   //the step under the turf's overhang
#define WALL_SOIL           1.00f   //the soil band's foot; the rock starts there
#define WALL_LIP            0.22f   //how far the turf overhangs the rock under it
#define WALL_CHAMFER        0.30f   //from one stratum to the next: a short slope, not a sliver
#define WALL_BULGE          0.10f   //a column's face stands out at its middle...
#define WALL_GROOVE         0.14f   //...and sits back at its joints with its neighbours
#define WALL_OFFSET_MAX     0.45f   //a stratum's own in or out
#define WALL_BLOCK          4.5f    //world units: how wide a block of one stratum's offset is, about
#define WALL_MOSS           0.12f   //the share of upward ledges that are moss rather than rock

/*
    The ground's gentle relief, looks only (grid_plan.md section 4: buildable land is flat within a
    level). A few tenths of a unit, so a triangle's face tilts enough to catch the sun differently
    from its neighbour - the faceted low-poly ground of the reference - while a house still stands
    on what reads as flat. Every ground vertex gets it through Ground(), walls' rings at a top
    included, so ground and wall still meet exactly.
*/
#define GROUND_BUMP         0.25f
#define GROUND_BUMP_SCALE   14.0f
//A ground triangle a river lowers by more than this anywhere is bank, not grass (Fan).
#define RIVER_BANK_DIP      0.45f

//Palette column "this cell's grass shade" - Cell resolves it from the coarse cell the fine one
//belongs to, so the shades lie in field-sized patches rather than speckling every triangle (which
//read as noise at the game's zoom).
#define COLUMN_GRASS        -1
//"The chasm floor": PAL_VOID on the effects row. A sentinel rather than PAL_VOID itself, because
//a column means nothing without its row - PAL_VOID is 5, which on a biome row is PAL_ROCK_0.
#define COLUMN_VOID         -2
#define GROUND_STEEP        0.80f   //a ground triangle's normal.y under this is bare rock: about 37 degrees
#define MOUNTAIN_STEEP      0.55f   //the mountain holds snow steeper than that - only its crags are bare: 57 degrees
#define BARE_COLUMN         (PAL_ROCK_0 + 3)    //the top of a patch too small to use: one brown for all

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

//A ground point at `level_height`, with the relief added. Position alone decides it, so two cells
//computing one point agree.
float Ground(const vec2& p, float level_height){
    float x = p.x / GROUND_BUMP_SCALE;
    float z = p.y / GROUND_BUMP_SCALE;
    float n = ValueNoise(x,z,1001) * 0.8f + ValueNoise(x * 2.7f,z * 2.7f,1002) * 0.2f;
    return level_height + n * GROUND_BUMP;
}

//Strata for steep GROUND (a hillside, a crag): a band's rock, by band index.
int RockColumn(int band){
    static const int pattern[7] = {0,1,2,1,3,0,2};
    return PAL_ROCK_0 + pattern[band % 7];
}

//The stratum a height is in: a stratum's top ring belongs to it, its chamfer above to the one above.
int Band(float z){
    return (int)std::floor((-z + WALL_CHAMFER * 0.5f) / TERRAIN_STEP);
}

/*
    A WALL's rock in stratum `band` of column `plot`: lighter near the rim, darker with depth, a
    stratum at one colour along the whole wall with the odd column a shade off.
*/
int StrataColumn(int band, int plot){
    static const int near_rim[4] = {3,0,3,1};
    static const int middle[4] = {1,3,1,2};
    static const int deep[2] = {1,2};
    float depth = band * TERRAIN_STEP;
    const int* seq = depth < 15.0f ? near_rim : (depth < 30.0f ? middle : deep);
    int len = depth < 30.0f ? 4 : 2;
    int k = band % len;
    if (Hash3(plot,band,81) % 100 < 12){
        k = (k + 1) % len;
    }
    return PAL_ROCK_0 + seq[k];
}

/*
    THE RINGS every wall is cut at, the same for every wall: each step's top, its turf, the step under
    the turf and the soil's foot, and a chamfer above every stratum - so where walls meet on one
    vertical line they meet at the same heights, and share their points. Under the cloud deck nothing
    is seen: a top there has no cap, and a stratum no chamfer.
*/
struct Ring{
    float z;            //as cut, before the ground's bump and relief
    int step;           //the step whose top it hangs under (-1: a chamfer, at a fixed height)
    float under;        //how far under that top
};

class Builder{
public:
    Builder(const Grid& g, const Terrain& t, TerrainMeshData& out) : grid(g), terrain(t), data(out){
        origin = g.bounds_min;
        vec2 size = g.bounds_max - g.bounds_min;
        data.chunks_x = std::max(1,(int)std::ceil(size.x / TERRAIN_CHUNK_SIZE));
        data.chunks_z = std::max(1,(int)std::ceil(size.y / TERRAIN_CHUNK_SIZE));
        data.chunks.assign((size_t)data.chunks_x * data.chunks_z,TerrainChunk());
        for (int k = 0; k <= TERRAIN_FLOOR_STEPS; k++){
            float top = -TERRAIN_STEP * (float)k;
            bool f_seen = top > TERRAIN_DECK_Y;
            if (k > 0 && f_seen){
                rings.push_back({top + WALL_CHAMFER,-1,0.0f});
            }
            ring_of_step[k] = (int)rings.size();
            rings.push_back({top,k,0.0f});
            if (f_seen){
                rings.push_back({top - WALL_TURF,k,WALL_TURF});
                rings.push_back({top - WALL_TURF_UNDER,k,WALL_TURF_UNDER});
                rings.push_back({top - WALL_SOIL,k,WALL_SOIL});
            }
        }
        //Across each quad's edge k: the quad on the other side, -1 on the map's outline.
        std::unordered_map<uint64_t,int> first;
        first.reserve(g.fine.quads.size() * 3);
        across.assign(g.fine.quads.size() * 4,-1);
        for (int q = 0; q < (int)g.fine.quads.size(); q++){
            for (int k = 0; k < 4; k++){
                uint64_t key = EdgeKey(g.fine.quads[q].v[k],g.fine.quads[q].v[(k + 1) % 4]);
                auto it = first.find(key);
                if (it == first.end()){
                    first[key] = q * 4 + k;
                }else{
                    across[q * 4 + k] = it->second / 4;
                    across[it->second] = q;
                }
            }
        }
        salt = g.settings.seed * 0x9E3779B1u;
    }

    static uint64_t EdgeKey(int a, int b){
        if (a > b){
            std::swap(a,b);
        }
        return ((uint64_t)(uint32_t)a << 32) | (uint64_t)(uint32_t)b;
    }

    void SetChunkAt(const vec2& p){
        int cx = std::max(0,std::min(data.chunks_x - 1,(int)((p.x - origin.x) / TERRAIN_CHUNK_SIZE)));
        int cz = std::max(0,std::min(data.chunks_z - 1,(int)((p.y - origin.y) / TERRAIN_CHUNK_SIZE)));
        chunk = &data.chunks[cz * data.chunks_x + cx];
    }

    //Wound so its face normal agrees with `want`: the renderer culls back faces, and which way
    //round a generated triangle comes out is otherwise an accident of the cell's corner order.
    void Tri(vec3 a, vec3 b, vec3 c, const vec3& want, int column){
        vec3 n = (b - a).cross(c - a);
        if (n.dot(want) < 0.0f){
            std::swap(b,c);
            n = n * -1.0f;
        }
        if (n.length() < 1e-9f){
            return;
        }
        n.normalize();
        int tri_row = row;
        if (column == COLUMN_GRASS){
            GroundColour((a + b + c) * (1.0f / 3.0f),n,column,tri_row);
        }else if (column == COLUMN_VOID){
            column = PAL_VOID;
            tri_row = PAL_EFFECTS;      //the same black in every biome
        }
        vec3 tangent = b - a;
        if (tangent.length() > 1e-9f){
            tangent.normalize();
        }
        vertex v = {};
        v.normal = n;
        v.tangent = tangent;
        v.uv = PaletteUV(column,tri_row);
        v.matid = 0;
        v.pos = a; chunk->verts.push_back(v);
        v.pos = b; chunk->verts.push_back(v);
        v.pos = c; chunk->verts.push_back(v);
        data.num_triangles++;
    }

    /*
        THE GROUND'S COLOUR, per triangle (biomes_plan.md step 5), from where its centre is, how high
        and how steep:
          - its biome's palette row, by TerrainBiomeAt with a little noise on the south regions' edges,
            so a biome's edge is ragged at a triangle's size rather than a staircase of whole cells;
          - steep ground (a normal under GROUND_STEEP) is bare rock in that row, banded by height like
            the cliff walls' strata - a hillside, a pocket's crags, a peak's faces;
          - the mountain's gentle ground is snow above the snowline (Terrain::SnowLine) and scree below
            it - grey stone from the very foot, so where the mountain begins, and nothing can be built,
            shows. Everything else gentle is its row's grass.
    */
    void GroundColour(const vec3& centre, const vec3& normal, int& column, int& tri_row){
        vec2 p(centre.x,centre.z);
        float dither = 0.18f * ValueNoise(p.x / 3.0f,p.y / 3.0f,1201);
        int biome = TerrainBiomeAt(grid.layout,p,ground_level,dither);
        bool f_steep = normal.y < ((biome == TERRAIN_BIOME_MOUNTAIN) ? MOUNTAIN_STEEP : GROUND_STEEP);
        int rock = RockColumn((int)std::floor(centre.y / TERRAIN_STEP) & 1023);
        switch (biome){
            case TERRAIN_BIOME_MOUNTAIN:    tri_row = PAL_FROZEN; break;
            case TERRAIN_BIOME_SWAMP:       tri_row = PAL_SWAMP; break;
            case TERRAIN_BIOME_DESERT:      tri_row = PAL_DESERT; break;
            default:                        tri_row = PAL_TEMPERATE; break;
        }
        if (f_steep){
            column = rock;
        }else if (biome == TERRAIN_BIOME_MOUNTAIN){
            bool f_snow = centre.y > terrain.SnowLine(p);
            column = f_snow ? grass_column : PAL_ROCK_0 + (int)(Hash3((int32_t)std::floor(p.x),(int32_t)std::floor(p.y),1202) % 2);
        }else{
            column = grass_column;
        }
    }

    /*
        A ground polygon as a fan. Grass that a river's channel lowers is drawn as its bank instead -
        only the strip above the water ever shows: pale pebbles, like the reference's shore.
    */
    void Fan(const std::vector<vec3>& p, const vec3& want, int column){
        bool f_dry_ground = (column == COLUMN_GRASS);
        for (size_t i = 1; i + 1 < p.size(); i++){
            int c = column;
            if (f_dry_ground){
                float dip = std::max(terrain.RiverDip(vec2(p[0].x,p[0].z)),
                            std::max(terrain.RiverDip(vec2(p[i].x,p[i].z)),terrain.RiverDip(vec2(p[i + 1].x,p[i + 1].z))));
                if (dip > RIVER_BANK_DIP){
                    c = PAL_STONE_LIGHT;
                }
            }
            Tri(p[0],p[i],p[i + 1],want,c);
        }
    }

    //The ground's height at p at `height`: the relief, and on the plateau any river's channel.
    float GroundY(const vec2& p, float height) const{
        float y = terrain.GroundHeight(p,height);
        if (height == 0.0f){
            y -= terrain.RiverDip(p);
        }
        return y;
    }

    vec3 GroundPoint(const vec2& p, float height) const{
        return vec3(p.x,GroundY(p,height),p.y);
    }

    float Top(int plot) const{
        return terrain.Height(plot);
    }

    //A ring's height at p: under a top it follows that top's ground; a chamfer stays where it is cut.
    float RingY(const vec2& p, int r) const{
        const Ring& ring = rings[r];
        return ring.step >= 0 ? GroundY(p,-TERRAIN_STEP * (float)ring.step) - ring.under : ring.z;
    }

    /*
        How far plot h's stratum `band` stands out (+) or sits back (-). Position noise cut into a
        few levels, so a block of rock is two or three plots wide, flush across itself and stepped
        against the next - the stacked blocks of chasm_aigen_1.png - and a little of each plot's own;
        and a soft stratum, here and there, eroded back all along.
    */
    float RockOffset(int h, int band) const{
        static const float levels[4] = {-0.28f,-0.08f,0.10f,0.28f};
        const vec2& p = grid.fine.pos[h];
        float n = 0.5f + 0.5f * ValueNoise(p.x / WALL_BLOCK,p.y / WALL_BLOCK,(int)(salt % 997u) + 71 + band);
        float level = levels[std::min(3,(int)(n * 4.0f))];
        float own = 0.07f * ((float)(Hash3(h,band,72) & 0xFFFF) / 32767.5f - 1.0f);
        float soft = (Hash3(band,0,(int)(salt % 997u) + 73) % 100 < 30) ? -0.20f : 0.0f;
        return std::max(-WALL_OFFSET_MAX,std::min(WALL_OFFSET_MAX,level + own + soft));
    }

    //Column h's offset at the ring cut at z: its turf overhangs, its soil tucks in, its rock by stratum.
    //Under the cloud deck the rock is flat: nobody sees it, and a flat wall needs no rings to step at.
    float ColumnOffset(int h, float z) const{
        if (z < TERRAIN_DECK_Y){
            return 0.0f;
        }
        float t = Top(h);
        float under_top = t - z;
        float rock_under_cap = RockOffset(h,Band(t - WALL_SOIL - 0.05f));
        if (under_top < WALL_TURF + 0.02f){
            bool f_turf = terrain.level[h] != TERRAIN_BARE && t > TERRAIN_DECK_Y;
            return f_turf ? rock_under_cap + WALL_LIP : rock_under_cap;
        }
        if (under_top < WALL_SOIL + 0.02f){
            return rock_under_cap;
        }
        return RockOffset(h,Band(z));
    }

    /*
        WHERE A WALL POINT GOES at ring r: out along its wall's normal (+) or back (-) by the columns
        it is the face of. `cols` are the plots round the point (an edge's two, or a quad's four in
        order) and `normals` the outward normals of the wall segments through it at that ring. A point
        between two columns' faces - a quad's centre on two high corners - is their joint and sits back;
        an edge's midpoint is the middle of one column's face and stands out. Where a wall's foot meets
        a lower top, or there is no wall, it stays put, so ground and wall meet.
    */
    vec2 Offset(const vec2& base, const int* cols, int num_cols, bool f_mid, float z,
                const vec2* normals, int num_normals) const{
        const float eps = 0.02f;
        int above[4];
        int num_above = 0;
        int at[4];
        int num_at = 0;
        int num_below = 0;
        for (int i = 0; i < num_cols; i++){
            float t = Top(cols[i]);
            if (t > z + eps){
                above[num_above++] = cols[i];
            }else if (t >= z - eps){
                at[num_at++] = cols[i];
            }else{
                num_below++;
            }
        }
        if (num_below == 0 || num_normals == 0 || (num_at > 0 && num_above > 0)){
            return base;
        }
        const int* faces = num_at > 0 ? at : above;
        int num_faces = num_at > 0 ? num_at : num_above;
        float o = 0.0f;
        for (int i = 0; i < num_faces; i++){
            o += ColumnOffset(faces[i],z);
        }
        o /= (float)num_faces;
        if (f_mid){
            o += WALL_BULGE;
        }else if (num_faces > 1){
            o -= WALL_GROOVE;
        }
        o = std::max(-0.6f,std::min(0.75f,o));
        vec2 sum(0.0f,0.0f);
        for (int i = 0; i < num_normals; i++){
            sum += normals[i];
        }
        float len = sum.length();
        if (len < 1e-6f){
            return base;
        }
        vec2 n = sum / len;
        float miter = 1.0f;
        for (int i = 0; i < num_normals; i++){
            miter = std::min(miter,n.dot(normals[i]));
        }
        return base + n * (o / std::max(miter,0.66f));
    }

    //The outward normal of a wall along a->b, facing from plot `high` toward plot `low`.
    vec2 SegmentNormal(const vec2& a, const vec2& b, int high, int low) const{
        vec2 d = b - a;
        vec2 n(-d.y,d.x);
        float len = n.length();
        if (len < 1e-9f){
            return vec2(0.0f,0.0f);
        }
        n = n / len;
        if (n.dot(grid.fine.pos[low] - grid.fine.pos[high]) < 0.0f){
            n = -n;
        }
        return n;
    }

    //Whether ring r is on a wall between tops of two plots.
    bool Spans(int r, int a, int b) const{
        int ra = ring_of_step[terrain.steps[a]];
        int rb = ring_of_step[terrain.steps[b]];
        return ra != rb && r >= std::min(ra,rb) && r <= std::max(ra,rb);
    }

    vec2 QuadCentre(int q) const{
        const GridQuad& quad = grid.fine.quads[q];
        vec2 c(0.0f,0.0f);
        for (int k = 0; k < 4; k++){
            c += grid.fine.pos[quad.v[k]];
        }
        return c * 0.25f;
    }

    void Cell(int q){
        const GridQuad& quad = grid.fine.quads[q];
        vec2 p[4];
        float h[4];
        vec2 centre(0.0f,0.0f);
        for (int k = 0; k < 4; k++){
            p[k] = grid.fine.pos[quad.v[k]];
            h[k] = Top(quad.v[k]);
            centre += p[k];
        }
        centre = centre * 0.25f;
        SetChunkAt(centre);
        chunk->quads.push_back(q);
        const vec3 up(0.0f,1.0f,0.0f);
        grass_column = PAL_GRASS_0 + (int)(Hash3(quad.parent,0,77) % PAL_GRASS_COUNT);
        //The biome's palette row, by the cell's corners: the north mountain draws from the frozen row,
        //the swamp and the desert from theirs.
        int mountain = 0;
        int swamp = 0;
        int desert = 0;
        for (int k = 0; k < 4; k++){
            mountain += terrain.Mountain(quad.v[k]) ? 1 : 0;
            swamp += (terrain.biome[quad.v[k]] == TERRAIN_BIOME_SWAMP) ? 1 : 0;
            desert += (terrain.biome[quad.v[k]] == TERRAIN_BIOME_DESERT) ? 1 : 0;
        }
        row = (mountain >= 2) ? PAL_FROZEN : (swamp >= 2) ? PAL_SWAMP : (desert >= 2) ? PAL_DESERT : PAL_TEMPERATE;

        if (h[0] == h[1] && h[1] == h[2] && h[2] == h[3]){
            std::vector<vec3> poly;
            for (int k = 0; k < 4; k++){
                poly.push_back(GroundPoint(p[k],h[k]));
            }
            SetGround(quad.v[0]);
            Fan(poly,up,TopColumn(quad.v[0]));
            return;
        }

        /*
            A cell with a drop in it, IN QUARTERS: each corner's - its corner, its two edges' midpoints
            and the cell's centre - is that corner's plot's top, and a wall stands on every inner
            half-edge (midpoint to centre) between two quarters at different heights. A plot's top is
            the quarters round it from every cell it is a corner of: its column.
        */
        vec2 m[4];
        for (int k = 0; k < 4; k++){
            m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
        }
        //Each half-edge's outward normal, toward its lower corner; zero where both corners agree.
        vec2 seg_normal[4];
        for (int k = 0; k < 4; k++){
            int a = quad.v[k];
            int b = quad.v[(k + 1) % 4];
            seg_normal[k] = (h[k] == h[(k + 1) % 4]) ? vec2(0.0f,0.0f)
                                                       : (h[k] > h[(k + 1) % 4] ? SegmentNormal(m[k],centre,a,b)
                                                                                : SegmentNormal(m[k],centre,b,a));
        }
        //The centre at ring r: its quad's four plots, the normals of the half-edges whose wall is there.
        auto centre_at = [&](int r){
            vec2 normals[4];
            int num = 0;
            for (int k = 0; k < 4; k++){
                if (Spans(r,quad.v[k],quad.v[(k + 1) % 4])){
                    normals[num++] = seg_normal[k];
                }
            }
            vec2 at = Offset(centre,quad.v,4,false,rings[r].z,normals,num);
            return vec3(at.x,RingY(centre,r),at.y);
        };
        //Midpoint k at ring r: its edge's two plots, and the half-edges on both sides of the edge -
        //the other cell computes the very same point from the very same numbers.
        auto mid_at = [&](int k, int r){
            int a = quad.v[k];
            int b = quad.v[(k + 1) % 4];
            vec2 at = m[k];
            int other = across[q * 4 + k];
            if (other >= 0 && Spans(r,a,b)){
                int high = h[k] > h[(k + 1) % 4] ? a : b;
                int low = (high == a) ? b : a;
                vec2 normals[2] = {seg_normal[k],SegmentNormal(m[k],QuadCentre(other),high,low)};
                int cols[2] = {a,b};
                at = Offset(m[k],cols,2,true,rings[r].z,normals,2);
            }
            return vec3(at.x,RingY(m[k],r),at.y);
        };

        for (int k = 0; k < 4; k++){
            int r = ring_of_step[terrain.steps[quad.v[k]]];
            int prev = (k + 3) % 4;
            std::vector<vec3> quarter = {GroundPoint(p[k],h[k]),mid_at(k,r),centre_at(r),mid_at(prev,r)};
            SetGround(quad.v[k]);
            Fan(quarter,up,TopColumn(quad.v[k]));
        }
        /*
            The walls, ring to ring. A wall takes the turf and soil rings only of the tops in its own
            cell, and under the deck only its tops' rings at all: anyone else's fall inside one of its
            strata, or in the flat rock under the deck, where every ring has the same offset - so a
            neighbouring wall that does have them meets this one along a straight edge anyway, and a
            full-height wall is cut half as often.
        */
        uint32_t tops_here = 0;
        for (int k = 0; k < 4; k++){
            tops_here |= 1u << terrain.steps[quad.v[k]];
        }
        for (int k = 0; k < 4; k++){
            int a = quad.v[k];
            int b = quad.v[(k + 1) % 4];
            if (h[k] == h[(k + 1) % 4]){
                continue;
            }
            int high = h[k] > h[(k + 1) % 4] ? a : b;
            vec3 out(seg_normal[k].x,0.0f,seg_normal[k].y);
            int r0 = ring_of_step[terrain.steps[high]];
            int r1 = ring_of_step[terrain.steps[high == a ? b : a]];
            vec3 A0 = mid_at(k,r0);
            vec3 B0 = centre_at(r0);
            int prev = r0;
            for (int r = r0 + 1; r <= r1; r++){
                bool f_cap = rings[r].under > 0.0f || rings[r].z < TERRAIN_DECK_Y;
                if (f_cap && rings[r].step >= 0 && !(tops_here & (1u << rings[r].step))){
                    continue;
                }
                vec3 A1 = mid_at(k,r);
                vec3 B1 = centre_at(r);
                int column = WallColumn(high,prev,r,A0,B0,B1,A1,out);
                int before = data.num_triangles;
                Tri(A0,B0,B1,out,column);
                Tri(A0,B1,A1,out,column);
                data.num_wall_triangles += data.num_triangles - before;
                A0 = A1;
                B0 = B1;
                prev = r;
            }
        }
    }

    //What a top is drawn as: the void under the deck, bare rock on a patch too small to use, else grass.
    int TopColumn(int plot) const{
        if (terrain.level[plot] == TERRAIN_FLOOR){
            return COLUMN_VOID;
        }
        return terrain.level[plot] == TERRAIN_BARE ? BARE_COLUMN : COLUMN_GRASS;
    }

    void SetGround(int plot){
        ground_level = (terrain.steps[plot] == 0) ? TERRAIN_PLATEAU : terrain.level[plot];
    }

    /*
        A wall face's rock, between rings r and r + 1 of the wall under plot `high`: its turf, the soil
        under it, then by which way the face looks - up, a ledge (rock, the odd one moss); down, the
        dark underside of an overhang - or else its stratum. Rings r and r_below need not be next to
        each other: Cell skips other tops' caps.
    */
    int WallColumn(int high, int r, int r_below, const vec3& a0, const vec3& b0, const vec3& b1, const vec3& a1,
                   const vec3& out) const{
        float zm = 0.5f * (rings[r].z + rings[r_below].z);
        float under_top = Top(high) - zm;
        bool f_capped = Top(high) > TERRAIN_DECK_Y && terrain.level[high] != TERRAIN_BARE;
        if (f_capped && under_top < WALL_TURF){
            return PAL_LIP;
        }
        if (f_capped && under_top < WALL_SOIL){
            return PAL_EARTH;
        }
        //The face's normal, turned to look out of the wall: its up part says ledge or underside.
        vec3 n = (b0 - a0).cross(a1 - a0) + (b1 - b0).cross(a1 - b0);
        if (n.x * out.x + n.z * out.z < 0.0f){
            n = n * -1.0f;
        }
        float len = n.length();
        float up = len > 1e-9f ? n.y / len : 0.0f;
        if (up > 0.4f){
            return (Hash3(high,r,33) % 100 < (uint32_t)(WALL_MOSS * 100.0f)) ? PAL_GRASS_0 + 2 : PAL_ROCK_0;
        }
        if (up < -0.4f){
            return PAL_ROCK_0 + 2;
        }
        return StrataColumn(Band(zm),high);
    }

    /*
        The skirt: every edge on the map's outline dropped to TERRAIN_SKIRT_BOTTOM, facing out. An
        edge whose ends are at different heights steps at its midpoint, which is exactly where the
        column wall inside meets it.
    */
    void Skirt(){
        for (int q = 0; q < (int)grid.fine.quads.size(); q++){
            const GridQuad& quad = grid.fine.quads[q];
            for (int k = 0; k < 4; k++){
                if (across[q * 4 + k] >= 0){
                    continue;
                }
                int ia = quad.v[k];
                int ib = quad.v[(k + 1) % 4];
                vec2 a = grid.fine.pos[ia];
                vec2 b = grid.fine.pos[ib];
                vec2 mid = (a + b) * 0.5f;
                SetChunkAt(mid);
                //The quad is wound positively, so it lies to the left of a->b: outward is right.
                vec2 along = b - a;
                vec3 out(along.y,0.0f,-along.x);
                float ha = terrain.Height(ia);
                float hb = terrain.Height(ib);
                if (ha == hb){
                    SkirtPanel(a,b,ha,ha,out);
                }else{
                    SkirtPanel(a,mid,ha,ha,out);
                    SkirtPanel(mid,b,hb,hb,out);
                }
            }
        }
    }

    void SkirtPanel(const vec2& a, const vec2& b, float level_a, float level_b, const vec3& out){
        vec3 A0 = GroundPoint(a,level_a), B0 = GroundPoint(b,level_b);
        vec3 A1(a.x,TERRAIN_SKIRT_BOTTOM,a.y), B1(b.x,TERRAIN_SKIRT_BOTTOM,b.y);
        Tri(A0,B0,B1,out,PAL_EARTH);
        Tri(A0,B1,A1,out,PAL_EARTH);
        //Where a river runs off the map, its water stands above the channel's cut: a face of water
        //from the surface down to the ground, so the river reads as cut through, not as a sheet.
        float wa = std::max(A0.y,TERRAIN_WATER_Y);
        float wb = std::max(B0.y,TERRAIN_WATER_Y);
        if (level_a == 0.0f && (wa > A0.y || wb > B0.y)){
            vec3 AW(a.x,wa,a.y), BW(b.x,wb,b.y);
            Tri(AW,BW,B0,out,PAL_WATER);
            Tri(AW,B0,A0,out,PAL_WATER);
        }
    }

    int row = PAL_TEMPERATE;    //the current cell's biome row - see Cell; ground triangles choose their own
    int ground_level = TERRAIN_PLATEAU;     //the kind of the ground being fanned, for GroundColour
    int grass_column = PAL_GRASS_0;     //the current cell's, see COLUMN_GRASS

private:
    const Grid& grid;
    const Terrain& terrain;
    TerrainMeshData& data;
    vec2 origin;
    std::vector<Ring> rings;
    int ring_of_step[TERRAIN_FLOOR_STEPS + 1];
    std::vector<int> across;
    uint32_t salt = 0;
    TerrainChunk* chunk = NULL;
};

}

float TerrainGroundHeight(const vec2& p, float level_height){
    return Ground(p,level_height);
}

int TerrainChunkOfQuad(const Grid& g, const TerrainMeshData& m, int quad){
    vec2 c = g.FineQuadCentre(quad);
    int cx = std::max(0,std::min(m.chunks_x - 1,(int)((c.x - g.bounds_min.x) / TERRAIN_CHUNK_SIZE)));
    int cz = std::max(0,std::min(m.chunks_z - 1,(int)((c.y - g.bounds_min.y) / TERRAIN_CHUNK_SIZE)));
    return cz * m.chunks_x + cx;
}

void BuildTerrainMesh(const Grid& g, const Terrain& t, TerrainMeshData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = TerrainMeshData();
    Builder b(g,t,out);
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        b.Cell(q);
    }
    b.row = PAL_TEMPERATE;      //the skirt is cut earth, whatever stands above it
    b.Skirt();
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}
