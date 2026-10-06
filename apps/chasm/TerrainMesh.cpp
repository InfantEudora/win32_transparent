#include "TerrainMesh.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <unordered_map>
#include "Palette.h"

#define WALL_BAND_HEIGHT    6.0f    //a rock stratum; also the wall's vertical resolution
#define WALL_ROUGHNESS      1.4f    //how far a stratum may stand out or sit back, world units
#define WALL_NOISE_SCALE    7.0f    //world units over which one stratum's offset changes

/*
    The ground's gentle relief, looks only (grid_plan.md section 4: buildable land is flat within a
    level). A few tenths of a unit, so a triangle's face tilts enough to catch the sun differently
    from its neighbour - the faceted low-poly ground of the reference - while a house still stands
    on what reads as flat. Every ground vertex gets it through Ground(), walls' top and bottom rows
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
#define GROUND_STEEP        0.80f   //a ground triangle's normal.y under this is bare rock: about 37 degrees
#define MOUNTAIN_STEEP      0.55f   //the mountain holds snow steeper than that - only its crags are bare: 57 degrees

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

//A ground point at `level_height`, with the relief added. Position alone decides it - see Jitter.
float Ground(const vec2& p, float level_height){
    float x = p.x / GROUND_BUMP_SCALE;
    float z = p.y / GROUND_BUMP_SCALE;
    float n = ValueNoise(x,z,1001) * 0.8f + ValueNoise(x * 2.7f,z * 2.7f,1002) * 0.2f;
    return level_height + n * GROUND_BUMP;
}

//Strata: a band's rock, by band index, so a stratum runs at one height along the whole wall.
int RockColumn(int band){
    static const int pattern[7] = {0,1,2,1,3,0,2};
    return PAL_ROCK_0 + pattern[band % 7];
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
        int rock = RockColumn((int)std::floor(centre.y / WALL_BAND_HEIGHT) & 1023);
        switch (biome){
            case TERRAIN_BIOME_MOUNTAIN:    tri_row = PAL_FROZEN; break;
            case TERRAIN_BIOME_SWAMP:       tri_row = PAL_SWAMP; break;
            case TERRAIN_BIOME_DESERT:      tri_row = PAL_DESERT; break;
            default:                        tri_row = PAL_TEMPERATE; break;
        }
        if (f_steep){
            column = rock;
        }else if (biome == TERRAIN_BIOME_MOUNTAIN){
            bool f_snow = terrain && centre.y > terrain->SnowLine(p);
            column = f_snow ? grass_column : PAL_ROCK_0 + (int)(Hash3((int32_t)std::floor(p.x),(int32_t)std::floor(p.y),1202) % 2);
        }else{
            column = grass_column;
        }
    }

    /*
        A ground polygon as a fan. Grass or lip that a river's channel lowers is drawn as its bank
        instead - only the strip above the water ever shows: pale pebbles, like the reference's shore.
    */
    void Fan(const std::vector<vec3>& p, const vec3& want, int column){
        bool f_dry_ground = (column == COLUMN_GRASS || column == PAL_LIP);
        for (size_t i = 1; i + 1 < p.size(); i++){
            int c = column;
            if (f_dry_ground && terrain){
                float dip = std::max(terrain->RiverDip(vec2(p[0].x,p[0].z)),
                            std::max(terrain->RiverDip(vec2(p[i].x,p[i].z)),terrain->RiverDip(vec2(p[i + 1].x,p[i + 1].z))));
                if (dip > RIVER_BANK_DIP){
                    c = PAL_STONE_LIGHT;
                }
            }
            Tri(p[0],p[i],p[i + 1],want,c);
        }
    }

    //The ground's height at p on a level: the relief, and on the plateau any river's channel.
    float GroundY(const vec2& p, float level_height){
        float y = terrain ? terrain->GroundHeight(p,level_height) : Ground(p,level_height);
        if (terrain && level_height == terrain_levels[TERRAIN_PLATEAU].height){
            y -= terrain->RiverDip(p);
        }
        return y;
    }

    vec3 GroundPoint(const vec2& p, float level_height){
        return vec3(p.x,GroundY(p,level_height),p.y);
    }

    /*
        A wall from level height `top` down to `bottom` along a->b, facing `out`, cut into strata.
        The first and last rows stay exactly on the cut and on the ground's relief, so the wall
        meets the ground above and below it; the rows between stray by Jitter - except at an end
        flagged still (`f_still_a`/`f_still_b`), a cell's centre where three walls meet (Cell), which
        must be the same point in all of them.

        The strata are cut at FIXED HEIGHTS - every WALL_BAND_HEIGHT below the plateau's level - not
        shared out over the drop, so a balcony's wall (from -12) and the full wall beside it (from 0)
        have rows at the same heights and the same rock in each, and meet at a balcony's end without a
        seam. A row's jitter and rock are by that height's index, so two walls through one point agree
        there whatever their tops. A row is placed by its share of the LEVELS' drop, not the wall's
        real height, so under a hill or the mountain the strata stand thicker, the relief shared out
        among them.
    */
    void Wall(const vec2& a, const vec2& b, float top, float bottom, const vec3& out,
              bool f_still_a = false, bool f_still_b = false){
        float top_a = GroundY(a,top);
        float top_b = GroundY(b,top);
        float bot_a = GroundY(a,bottom);
        float bot_b = GroundY(b,bottom);
        float drop = top - bottom;
        if (drop <= 0.0f){
            return;
        }
        //Row heights below the plateau's level, and each one's stratum index.
        float rows[64];
        int index[64];
        int n = 0;
        rows[n] = top;
        index[n++] = (int)std::lround(-top / WALL_BAND_HEIGHT);
        for (int k = index[0] + 1; n < 63; k++){
            float y = -WALL_BAND_HEIGHT * (float)k;
            if (y <= bottom + 0.3f * WALL_BAND_HEIGHT){
                break;      //a sliver of a stratum at the foot joins the one above it
            }
            rows[n] = y;
            index[n++] = k;
        }
        rows[n] = bottom;
        index[n++] = -1;
        for (int j = 0; j + 1 < n; j++){
            float t0 = (top - rows[j]) / drop;
            float t1 = (top - rows[j + 1]) / drop;
            bool f_inner0 = j > 0;
            bool f_inner1 = j + 2 < n;
            vec2 a0 = a + ((f_inner0 && !f_still_a) ? Jitter(a,index[j]) : vec2(0.0f,0.0f));
            vec2 b0 = b + ((f_inner0 && !f_still_b) ? Jitter(b,index[j]) : vec2(0.0f,0.0f));
            vec2 a1 = a + ((f_inner1 && !f_still_a) ? Jitter(a,index[j + 1]) : vec2(0.0f,0.0f));
            vec2 b1 = b + ((f_inner1 && !f_still_b) ? Jitter(b,index[j + 1]) : vec2(0.0f,0.0f));
            vec3 A0(a0.x,top_a + (bot_a - top_a) * t0,a0.y);
            vec3 B0(b0.x,top_b + (bot_b - top_b) * t0,b0.y);
            vec3 A1(a1.x,top_a + (bot_a - top_a) * t1,a1.y);
            vec3 B1(b1.x,top_b + (bot_b - top_b) * t1,b1.y);
            int column = RockColumn(index[j]);
            int before = data.num_triangles;
            Tri(A0,B0,B1,out,column);
            Tri(A0,B1,A1,out,column);
            data.num_wall_triangles += data.num_triangles - before;
        }
    }

    /*
        A cell with THREE levels in it - where a balcony meets its rim, and nowhere else (Terrain's
        `steps` check). Marching squares cuts a cell once, high against low; three levels need a cut
        for every pair. So the cell is drawn in QUARTERS instead, each corner's - its corner, its two
        edges' midpoints and the cell's centre - at that corner's level, with a wall down every inner
        half-edge (midpoint to centre) between two quarters of different levels. A midpoint is
        where the cell across the edge cuts too, between the same two levels, so the neighbours meet;
        the centre is this cell's alone, and every wall through it holds still there (Wall).
    */
    void QuarterCell(const vec2* p, const int* lv, const float* h){
        const vec3 up(0.0f,1.0f,0.0f);
        vec2 m[4];
        vec2 c(0.0f,0.0f);
        for (int k = 0; k < 4; k++){
            m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
            c += p[k] * 0.25f;
        }
        float hmax = std::max(std::max(h[0],h[1]),std::max(h[2],h[3]));
        for (int k = 0; k < 4; k++){
            std::vector<vec3> quarter = {GroundPoint(p[k],h[k]),GroundPoint(m[k],h[k]),GroundPoint(c,h[k]),
                                         GroundPoint(m[(k + 3) % 4],h[k])};
            ground_level = lv[k];
            //The top quarter is the cliff's lip, a bottom one the dark foot of a wall, as in Cell.
            int column = (h[k] == hmax) ? PAL_LIP : ((lv[k] == TERRAIN_FLOOR) ? PAL_FLOOR_DARK : COLUMN_GRASS);
            Fan(quarter,up,column);
        }
        //Between quarter k and k + 1: the half-edge from m[k] to the centre.
        for (int k = 0; k < 4; k++){
            int a = k;
            int b = (k + 1) % 4;
            if (h[a] == h[b]){
                continue;
            }
            int hi_q = (h[a] > h[b]) ? a : b;
            int lo_q = (hi_q == a) ? b : a;
            vec2 along = c - m[k];
            vec2 side(-along.y,along.x);
            if (side.dot(p[lo_q] - m[k]) < 0.0f){
                side = -side;
            }
            Wall(m[k],c,h[hi_q],h[lo_q],vec3(side.x,0.0f,side.y),false,true);
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
        chunk->quads.push_back(q);
        const vec3 up(0.0f,1.0f,0.0f);
        grass_column = PAL_GRASS_0 + (int)(Hash3(quad.parent,0,77) % PAL_GRASS_COUNT);
        //The biome's palette row, by the cell's corners: the north mountain draws from the frozen row,
        //snow for grass and grey-blue strata, until it has relief of its own (biomes_plan.md step 1).
        //The swamp (step 4) from its own row, darker and browner.
        int mountain = 0;
        int swamp = 0;
        for (int k = 0; k < 4; k++){
            mountain += t.Mountain(quad.v[k]) ? 1 : 0;
            swamp += (t.biome[quad.v[k]] == TERRAIN_BIOME_SWAMP) ? 1 : 0;
        }
        int desert = 0;
        for (int k = 0; k < 4; k++){
            desert += (t.biome[quad.v[k]] == TERRAIN_BIOME_DESERT) ? 1 : 0;
        }
        row = (mountain >= 2) ? PAL_FROZEN : (swamp >= 2) ? PAL_SWAMP : (desert >= 2) ? PAL_DESERT : PAL_TEMPERATE;

        int distinct = 1;
        for (int k = 1; k < 4; k++){
            bool f_new = true;
            for (int j = 0; j < k; j++){
                f_new = f_new && h[j] != h[k];
            }
            distinct += f_new ? 1 : 0;
        }
        if (distinct > 2){
            QuarterCell(p,lv,h);
            return;
        }

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
                poly.push_back(GroundPoint(p[k],h[k]));
            }
            ground_level = lv[0];
            Fan(poly,up,(lv[0] == TERRAIN_FLOOR) ? PAL_FLOOR : COLUMN_GRASS);
            return;
        }

        //Edge k runs corner k -> k+1; it is cut at its midpoint when it changes side.
        vec2 m[4];
        for (int k = 0; k < 4; k++){
            m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
        }

        //The high part: every high corner and every cut, in order - one polygon even in the saddle.
        //Drawn as the lip: the edge of the ground along a cliff top, a shade off the grass.
        std::vector<vec3> poly;
        for (int k = 0; k < 4; k++){
            if (high[k]){
                poly.push_back(GroundPoint(p[k],hmax));
            }
            if (high[k] != high[(k + 1) % 4]){
                poly.push_back(GroundPoint(m[k],hmax));
            }
        }
        Fan(poly,up,(high_level == TERRAIN_FLOOR) ? PAL_FLOOR : PAL_LIP);

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
            low.push_back(GroundPoint(m[k],h[first]));
            while (!high[j]){
                low.push_back(GroundPoint(p[j],h[j]));
                low_centre += p[j];
                count++;
                j = (j + 1) % 4;
            }
            int last = (j + 3) % 4;
            //Leaves through edge last -> j.
            low.push_back(GroundPoint(m[last],h[last]));
            //The ground at a wall's foot, a shade darker: the shade the wall itself would cast,
            //standing in for occlusion the renderer does not compute at this scale.
            ground_level = lv[first];
            Fan(low,up,(lv[first] == TERRAIN_FLOOR) ? PAL_FLOOR_DARK : COLUMN_GRASS);

            vec2 a = m[k];
            vec2 b = m[last];
            vec2 along = b - a;
            vec2 side(-along.y,along.x);
            vec2 to_low = low_centre / (float)count - (a + b) * 0.5f;
            if (side.dot(to_low) < 0.0f){
                side = -side;
            }
            Wall(a,b,hmax,h[first],vec3(side.x,0.0f,side.y));
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
        if (level_a == terrain_levels[TERRAIN_PLATEAU].height && (wa > A0.y || wb > B0.y)){
            vec3 AW(a.x,wa,a.y), BW(b.x,wb,b.y);
            Tri(AW,BW,B0,out,PAL_WATER);
            Tri(AW,B0,A0,out,PAL_WATER);
        }
    }

    int row = PAL_TEMPERATE;    //the current cell's biome row - see Cell; ground triangles choose their own
    int ground_level = TERRAIN_PLATEAU;     //the level of the ground being fanned, for GroundColour
    const Terrain* terrain = NULL;      //for the rivers' channels
    int grass_column = PAL_GRASS_0;     //the current cell's, see COLUMN_GRASS

private:
    const Grid& grid;
    TerrainMeshData& data;
    vec2 origin;
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
    Builder b(g,out);
    b.terrain = &t;
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        b.Cell(q,t);
    }
    b.row = PAL_TEMPERATE;      //the skirt is cut earth, whatever stands above it
    b.Skirt(t);
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}
