#include "Boulders.h"

#include <math.h>
#include <stdint.h>

static uint32_t Mix(uint32_t x){
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

//0..1 from a corner and a purpose - the Foliage kind of hash, so no shared random stream is touched.
static float HashUnit(uint32_t a, uint32_t b, uint32_t c){
    //Each input folded in through a full-avalanche mixer (lowbias32) in turn. A single multiply-xor
    //of all three first was tried and was not enough: small signed lattice indices came out within
    //a few percent of each other, and the ridge line was a plateau.
    uint32_t h = Mix(a + 0x9E3779B9u);
    h = Mix(h ^ b);
    h = Mix(h ^ c);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

static bool InsideAny(const std::vector<StageBlock>& blocks, float x, float y){
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& b = blocks[i];
        if (b.f_alive && x > b.Left() && x < b.Right() && y > b.Bottom() && y < b.Top()){
            return true;
        }
    }
    return false;
}

static bool IsSurface(const StageBlock& b){
    return b.f_alive && (b.kind == BLOCK_SOLID || b.kind == BLOCK_LEDGE);
}

static bool IsWall(const StageBlock& b){
    //Nor a crumble stone: a rock heaped against something about to fall away reads wrong too.
    return b.f_alive && b.kind != BLOCK_PLATFORM && b.kind != BLOCK_BREAKABLE && b.kind != BLOCK_CRUMBLE;
}

void FindBoulderCorners(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                        std::vector<BoulderCorner>& out){
    out.clear();
    const float eps = 0.02f;
    for (size_t a = 0; a < blocks.size(); a++){
        const StageBlock& A = blocks[a];
        if (!IsSurface(A)){
            continue;
        }
        const float top = A.Top();
        for (size_t w = 0; w < blocks.size(); w++){
            const StageBlock& W = blocks[w];
            if (w == a || !IsWall(W) || W.Bottom() > top + 0.05f || W.Top() < top + params.min_wall){
                continue;
            }
            //Its left face opens onto ground to the left of it, its right face to the right.
            for (int f = 0; f < 2; f++){
                float side = (f == 0) ? -1.0f : 1.0f;
                float xf = (f == 0) ? W.Left() : W.Right();
                float lo = (side < 0.0f) ? xf - params.room : xf;
                float hi = (side < 0.0f) ? xf : xf + params.room;
                if (lo < A.Left() - eps || hi > A.Right() + eps){
                    continue;
                }
                //Open ground, not the floor under something standing there.
                if (InsideAny(blocks,xf + side * params.room * 0.5f,top + 0.3f) ||
                    InsideAny(blocks,xf + side * 0.2f,top + 0.3f)){
                    continue;
                }
                bool f_seen = false;
                for (size_t k = 0; k < out.size(); k++){
                    if (out[k].top == (int)a && out[k].side == side && fabsf(out[k].x - xf) < 0.2f){
                        f_seen = true;
                        break;
                    }
                }
                if (!f_seen){
                    BoulderCorner c;
                    c.x = xf;
                    c.side = side;
                    c.top = (int)a;
                    out.push_back(c);
                }
            }
        }
    }
}

int BoulderMountOf(int kind){
    if (kind == BOULDER_STALACTITE || kind == BOULDER_ROOT){
        return BOULDER_UNDER;
    }
    if (kind == BOULDER_BRACKET_1 || kind == BOULDER_BRACKET_2){
        return BOULDER_ON_WALL;
    }
    return BOULDER_ON_TOP;
}

bool BoulderCastsShadow(int kind){
    return IsBigBoulder(kind) || kind == BOULDER_STALAGMITE || kind == BOULDER_STALACTITE ||
           kind == BOULDER_ROOT || kind == BOULDER_SKULL_STICK || kind == BOULDER_SNAKE;
}

float BoulderFrontZ(const Boulder& b, const BoulderParams& params){
    //The skeleton lies at yaw 0, and its radius - the whole coil swung round - would say it
    //reached a metre further forward than it does.
    if (b.kind == BOULDER_SNAKE){
        return b.z + params.snake_z_front * b.scale;
    }
    //A wall piece reaches out of the face, not across the slab (CAVE_WALL_WIDTH).
    if (BoulderMountOf(b.kind) == BOULDER_ON_WALL){
        return b.z + 0.8f * params.radius[b.kind] * b.scale;
    }
    return b.z + params.radius[b.kind] * b.scale;
}

BoulderBiome BoulderBiomeFor(int biome){
    BoulderBiome r;
    if (biome == BIOME_CAVE){
        r.cluster_at_least = 1.0f;
        //0.6 when rubble was all the floor had; the strip behind her is one rock deep, and at 0.6
        //it was stone from end to end with no room left for the bones and mushrooms below.
        r.rubble = 0.35f;
        r.rubble_big = 0.12f;
        r.drips = 0.8f;
        r.drip_mites = 0.5f;
        r.roots = 0.12f;
        r.mites = 0.25f;
        r.flats = 0.15f;
        r.remains = 0.3f;
        r.mushrooms = 0.5f;
        r.mite_mushrooms = 0.5f;
        r.brackets = 1.5f;
        r.deep = 0.9f;
        r.f_set_pieces = true;
    }
    return r;
}

//The rules at a spot: its biome's, scaled toward the jungle's (none) by how far into a fade it is.
//The shares and `deep` are not densities and stay as they are.
static BoulderBiome RulesAt(const std::vector<StageBiome>* biomes, float x, float y){
    float w = 1.0f;
    BoulderBiome r = BoulderBiomeFor(BiomeAt(biomes,x,y,&w));
    r.cluster_at_least *= w;
    r.rubble *= w;
    r.drips *= w;
    r.roots *= w;
    r.mites *= w;
    r.flats *= w;
    r.remains *= w;
    r.mushrooms *= w;
    r.brackets *= w;
    return r;
}

//Clear of every rock already placed, in (x,z), by `slack` of the two radii.
static bool ClearOfRocks(const std::vector<Boulder>& out, const BoulderParams& params, float x, float z,
                         float r, float slack){
    for (const Boulder& o : out){
        float ro = params.radius[o.kind] * o.scale;
        float dx = x - o.x, dz = z - o.z;
        if (sqrtf(dx * dx + dz * dz) < (r + ro) * slack){
            return false;
        }
    }
    return true;
}

//--- The cave's floor, roof and walls (Boulders.h) --------------------------------------------------

static const float CAVE_PI = 3.14159265f;
static const float CAVE_STEP = 0.5f;        //between candidates, as the rubble's
static const float CAVE_FLAT = 0.35f;       //a cluster's spread through the slab, of its spread along it
/*
    How close, as a share of the two radii, a mushroom or a bone may come to anything else on the
    floor. Closer than the rocks keep (0.9): a radius is the widest a piece gets under any yaw, a
    ribcage's twice its narrow side, and at 0.8 a mushroom could not stand in the lee of a stone -
    which is exactly where one grows.
*/
static const float CAVE_NESTLE = 0.65f;
/*
    A wall piece's width through the slab, as a share of its radius. The radius is its reach from
    the origin, and a bracket reaches OUT of the wall - along x, once it is turned to face out - so
    across the slab it is only its sides: 0.11 against a radius of 0.14 on mushroom_tree_1.
*/
static const float CAVE_WALL_WIDTH = 0.8f;

//How far into its box a spot is from the faded side - the mouth: 0 there, 1 at the far end. 0.5
//in a box faded on both sides or on neither, and outside every box.
static float DepthIn(const std::vector<StageBiome>* biomes, float x, float y){
    for (const StageBiome& b : *biomes){
        if (!b.Contains(x,y)){
            continue;
        }
        const float width = 2.0f * b.hw;
        if (width > 0.0f && b.fade_right > 0.0f && b.fade_left <= 0.0f){
            return (b.Right() - x) / width;
        }
        if (width > 0.0f && b.fade_left > 0.0f && b.fade_right <= 0.0f){
            return (x - b.Left()) / width;
        }
        break;
    }
    return 0.5f;
}

//1 - deep at the mouth up to 1 + deep at the far end; f_mouth turns it round.
static float Deepen(float deep, float depth, bool f_mouth = false){
    const float d = f_mouth ? 1.0f - depth : depth;
    return 1.0f - deep + 2.0f * deep * d;
}

static float Lerp(float a, float b, float t){
    return a + (b - a) * t;
}

//Clear, in (x,z), of every piece already standing on the same top - or hanging from the same
//underside - by `slack` of the two radii. Wall pieces are ClearOnWall's.
static bool ClearOfMount(const std::vector<Boulder>& out, const BoulderParams& params, int mount, float ground,
                         float x, float z, float r, float slack){
    for (const Boulder& o : out){
        if (BoulderMountOf(o.kind) != mount || fabsf(o.ground - ground) > 0.3f){
            continue;
        }
        float ro = params.radius[o.kind] * o.scale;
        float dx = x - o.x, dz = z - o.z;
        if (sqrtf(dx * dx + dz * dz) < (r + ro) * slack){
            return false;
        }
    }
    return true;
}

/*
    Two wall pieces on one face overlap when they are both closer than their thicknesses up the
    face and closer than their radii through the slab - the one test that lets brackets stack, as
    they grow, and keeps two from growing through each other side by side.
*/
bool BoulderWallOverlap(const Boulder& a, const Boulder& b, const BoulderParams& params, float slack){
    if (fabsf(a.x - b.x) > 0.2f){
        return false;       //another face
    }
    const float hy = 0.5f * (params.height[a.kind] * a.scale + params.height[b.kind] * b.scale);
    const float rz = params.radius[a.kind] * a.scale + params.radius[b.kind] * b.scale;
    return fabsf(a.y - b.y) < hy * slack && fabsf(a.z - b.z) < rz * slack;
}

static bool ClearOnWall(const std::vector<Boulder>& out, const BoulderParams& params, const Boulder& b){
    for (const Boulder& o : out){
        if (BoulderMountOf(o.kind) == BOULDER_ON_WALL && BoulderWallOverlap(o,b,params,1.0f)){
            return false;
        }
    }
    return true;
}

/*
    Where a centre of radius r may go through the slab on top A: behind her walking line, and in
    front of the back edge - by back_inset of the CENTRE for stone, as the rubble is, and by
    cave_back_inset of the whole FOOTPRINT for anything that should not be standing in the stream.
*/
static bool Band(const StageBlock& A, const BoulderParams& params, float r, bool f_stone, float& zlo, float& zhi){
    zlo = f_stone ? A.Back() + params.back_inset : A.Back() + params.cave_back_inset + r;
    zhi = params.z_front_max - r;
    return zhi >= zlo;
}

//A standing piece with its origin at (x, z) on the top at `top`, sunk by `sink` of its height.
static Boulder Standing(int kind, float s, float x, float z, float top, const BoulderParams& params, float sink){
    Boulder b;
    b.kind = kind;
    b.scale = s;
    b.x = x;
    b.z = z;
    b.ground = top;
    b.y = top + (params.base[kind] - sink * params.height[kind]) * s;
    return b;
}

//The highest SOLID or LEDGE top under (x, y) - the floor a drip falls on, or a wall stands over.
static int TopBelow(const std::vector<StageBlock>& blocks, float x, float y){
    int best = -1;
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& T = blocks[i];
        if (!IsSurface(T) || x <= T.Left() || x >= T.Right() || T.Top() > y){
            continue;
        }
        if (best < 0 || T.Top() > blocks[best].Top()){
            best = (int)i;
        }
    }
    return best;
}

//A spot on an open top inside a biome that dresses it: a candidate for everything on the floor.
struct CaveSpot{
    int   top = -1;
    float x = 0.0f;
    float depth = 0.5f;
    float weight = 1.0f;
    BoulderBiome rules;
    uint32_t seed = 0, level = 0;
};

/*
    A cluster of mushrooms round (cx, cz): mostly all of one kind, now and then mixed, the first
    near the middle and the biggest. Only where it fits - a mushroom with no room is left out rather
    than pushed somewhere it was not drawn for.
*/
static void MushroomCluster(const std::vector<StageBlock>& blocks, const StageBlock& A, const BoulderParams& params,
                            std::vector<Boulder>& out, float cx, float cz, float spread, int count,
                            uint32_t seed, uint32_t level){
    const float top = A.Top();
    const int one_kind = BOULDER_MUSHROOM_1 + (int)(HashUnit(seed,level,100u) * 2.999f);
    const bool f_mixed = HashUnit(seed,level,101u) < 0.25f;
    for (int j = 0, tries = 0; j < count && tries < count * 8; tries++){
        const uint32_t t = 120u + (uint32_t)tries * 8u;
        const int kind = f_mixed ? BOULDER_MUSHROOM_1 + (int)(HashUnit(seed,level,t) * 2.999f) : one_kind;
        if (params.radius[kind] <= 0.0f){
            continue;
        }
        const float s = (j == 0) ? Lerp(0.9f,1.2f,HashUnit(seed,level,t + 1u)) : Lerp(0.5f,1.0f,HashUnit(seed,level,t + 1u));
        const float r = params.radius[kind] * s;
        const float a = 2.0f * CAVE_PI * HashUnit(seed,level,t + 2u);
        //The first TRY at the middle; the rest anywhere on the disc, so a middle something else
        //already stands on moves the cluster over rather than emptying it.
        const float d = (tries == 0) ? 0.0f : spread * sqrtf(HashUnit(seed,level,t + 3u));
        //Flattened through the slab: behind her the floor is a strip, and a round cluster mostly
        //falls off it.
        const float x = cx + d * cosf(a);
        const float z = cz + CAVE_FLAT * d * sinf(a);
        float zlo, zhi;
        if (!Band(A,params,r,false,zlo,zhi) || z < zlo || z > zhi || x - r < A.Left() || x + r > A.Right()){
            continue;
        }
        if (InsideAny(blocks,x,top + 0.3f) || !ClearOfMount(out,params,BOULDER_ON_TOP,top,x,z,r,CAVE_NESTLE)){
            continue;
        }
        Boulder b = Standing(kind,s,x,z,top,params,0.02f);
        b.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,t + 4u);
        b.tilt = (6.0f * CAVE_PI / 180.0f) * HashUnit(seed,level,t + 5u);
        b.tilt_axis_yaw = 2.0f * CAVE_PI * HashUnit(seed,level,t + 6u);
        out.push_back(b);
        j++;
    }
}

//The open tops, once: every stretch of floor the cave's passes draw on.
static void CaveSpots(const std::vector<StageBlock>& blocks, const std::vector<StageBiome>* biomes,
                      std::vector<CaveSpot>& spots){
    auto dresses = [](const BoulderBiome& r){
        return r.drips > 0.0f || r.roots > 0.0f || r.mites > 0.0f || r.flats > 0.0f || r.remains > 0.0f ||
               r.mushrooms > 0.0f || r.brackets > 0.0f || r.f_set_pieces;
    };
    spots.clear();
    for (size_t a = 0; a < blocks.size(); a++){
        const StageBlock& A = blocks[a];
        if (!IsSurface(A)){
            continue;
        }
        const float top = A.Top();
        for (float x = A.Left() + CAVE_STEP * 0.5f; x < A.Right(); x += CAVE_STEP){
            CaveSpot sp;
            sp.rules = RulesAt(biomes,x,top);
            if (!dresses(sp.rules) || InsideAny(blocks,x,top + 0.3f)){
                continue;
            }
            BiomeAt(biomes,x,top,&sp.weight);
            sp.top = (int)a;
            sp.x = x;
            sp.depth = DepthIn(biomes,x,top);
            sp.seed = (uint32_t)(int32_t)floorf(x * 10.0f) * 2u + 2000003u;
            sp.level = (uint32_t)(int32_t)floorf(top * 10.0f);
            spots.push_back(sp);
        }
    }
}

/*
    THE SET PIECES, once each. BEFORE THE RUBBLE, which then makes room for them: the rubble is
    strewn every couple of units, and a skeleton looking for a gap in it after the fact found none.
    After the corner clusters, which check nothing but themselves - so these check them.
*/
static void CaveSetPieces(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                          std::vector<Boulder>& out, const std::vector<CaveSpot>& spots){
    /*
        THE SNAKE'S SKELETON, deep in: among the spots in the far 45%, the one with the lowest hash
        it fits at - hashed rather than the deepest, which is the far wall's corner and its rocks.
        At yaw 0, long side along the floor, fitted through the slab by its own extent: its back as
        far back as a stone may stand, which puts its tail at the stream's edge, and shrunk if
        need be to keep its front behind her line.
    */
    if (params.radius[BOULDER_SNAKE] > 0.0f){
        std::vector<size_t> order;
        for (size_t i = 0; i < spots.size(); i++){
            if (spots[i].rules.f_set_pieces && spots[i].weight >= 1.0f && spots[i].depth >= 0.55f){
                order.push_back(i);
            }
        }
        for (size_t i = 1; i < order.size(); i++){
            for (size_t j = i; j > 0 && HashUnit(spots[order[j]].seed,spots[order[j]].level,60u) <
                                        HashUnit(spots[order[j - 1]].seed,spots[order[j - 1]].level,60u); j--){
                size_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        }
        for (size_t oi = 0; oi < order.size(); oi++){
            const CaveSpot& sp = spots[order[oi]];
            const StageBlock& A = blocks[sp.top];
            const float back = A.Back() + params.back_inset;
            const float extent = params.snake_z_front - params.snake_z_back;
            float s = params.snake_scale;
            if (back + extent * s > params.z_front_max){
                s = (params.z_front_max - back) / extent;
            }
            const float r = params.radius[BOULDER_SNAKE] * s;
            if (s < params.snake_scale * 0.5f || sp.x - r * 0.9f < A.Left() || sp.x + r * 0.9f > A.Right()){
                continue;
            }
            const float z = back - params.snake_z_back * s;
            if (!ClearOfMount(out,params,BOULDER_ON_TOP,A.Top(),sp.x,z,r,0.9f)){
                continue;
            }
            out.push_back(Standing(BOULDER_SNAKE,s,sp.x,z,A.Top(),params,0.03f));
            break;
        }
    }
    /*
        THE SKULL ON A STICK, where the daylight gives out: the shallowest spot at full weight - just
        past the mouth's fade - that it fits at, at the back of the floor.
    */
    if (params.radius[BOULDER_SKULL_STICK] > 0.0f){
        //Shallowest first, each in turn until one has room.
        std::vector<size_t> order;
        for (size_t i = 0; i < spots.size(); i++){
            if (spots[i].rules.f_set_pieces && spots[i].weight >= 1.0f){
                order.push_back(i);
            }
        }
        for (size_t i = 1; i < order.size(); i++){
            for (size_t j = i; j > 0 && spots[order[j]].depth < spots[order[j - 1]].depth; j--){
                size_t t = order[j]; order[j] = order[j - 1]; order[j - 1] = t;
            }
        }
        for (size_t oi = 0; oi < order.size(); oi++){
            const CaveSpot& sp = spots[order[oi]];
            const StageBlock& A = blocks[sp.top];
            const float s = params.stick_scale;
            const float r = params.radius[BOULDER_SKULL_STICK] * s;
            float zlo, zhi;
            if (!Band(A,params,r,false,zlo,zhi) || !ClearOfMount(out,params,BOULDER_ON_TOP,A.Top(),sp.x,zlo,r,0.9f)){
                continue;
            }
            Boulder b = Standing(BOULDER_SKULL_STICK,s,sp.x,zlo,A.Top(),params,0.1f);
            //Turned a little either way from as authored, so it is never quite square to the view.
            b.yaw = 0.4f * (2.0f * HashUnit(sp.seed,sp.level,61u) - 1.0f);
            out.push_back(b);
            break;
        }
    }
}

//Everything else the cave has - after the rubble, and clear of it.
static void ScatterCave(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                        std::vector<Boulder>& out, const std::vector<StageBiome>* biomes,
                        const std::vector<CaveSpot>& spots){
    //--- The roof: stalactites, and roots come through from above --------------------------------
    std::vector<size_t> drips_over_floor;       //the stalactites a stalagmite may grow up under
    for (size_t u = 0; u < blocks.size(); u++){
        const StageBlock& U = blocks[u];
        if (!U.f_alive || U.kind == BLOCK_PLATFORM){
            continue;
        }
        const float under = U.Bottom();
        const uint32_t level = (uint32_t)(int32_t)floorf(under * 10.0f);
        for (float x = U.Left() + CAVE_STEP * 0.5f; x < U.Right(); x += CAVE_STEP){
            //Just under it: the box holds its floor and not its ceiling.
            const BoulderBiome rules = RulesAt(biomes,x,under - 0.01f);
            if ((rules.drips <= 0.0f && rules.roots <= 0.0f) || InsideAny(blocks,x,under - 0.3f)){
                continue;
            }
            const float depth = DepthIn(biomes,x,under - 0.01f);
            const uint32_t seed = (uint32_t)(int32_t)floorf(x * 10.0f) * 2u + 3000017u;
            int kind = -1;
            if (HashUnit(seed,level,70u) < rules.drips * CAVE_STEP * Deepen(rules.deep,depth)){
                kind = BOULDER_STALACTITE;
            }else if (HashUnit(seed,level,71u) < rules.roots * CAVE_STEP * Deepen(rules.deep,depth,true)){
                kind = BOULDER_ROOT;
            }
            if (kind < 0 || params.radius[kind] <= 0.0f){
                continue;
            }
            //Longer toward the back, where they have had longest to grow; roots as they come.
            const float u01 = HashUnit(seed,level,72u);
            const float s = (kind == BOULDER_STALACTITE) ? Lerp(0.55f,1.15f,0.5f * u01 + 0.5f * depth)
                                                         : Lerp(0.8f,1.3f,u01);
            const float r = params.radius[kind] * s;
            const float bx = x + CAVE_STEP * 0.4f * (2.0f * HashUnit(seed,level,73u) - 1.0f);
            const float zlo = fmaxf(U.Back() + r,params.hang_z_back);
            const float zhi = params.z_front_max - r;
            if (zhi < zlo || bx - r * 0.5f < U.Left() || bx + r * 0.5f > U.Right() || InsideAny(blocks,bx,under - 0.3f)){
                continue;
            }
            const float z = Lerp(zlo,zhi,HashUnit(seed,level,74u));
            if (!ClearOfMount(out,params,BOULDER_UNDER,under,bx,z,r,0.9f)){
                continue;
            }
            Boulder b;
            b.kind = kind;
            b.scale = s;
            b.x = bx;
            b.z = z;
            b.ground = under;
            b.y = under + params.hang_sink;
            b.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,75u);
            out.push_back(b);
            if (kind == BOULDER_STALACTITE && HashUnit(seed,level,76u) < rules.drip_mites){
                drips_over_floor.push_back(out.size() - 1);
            }
        }
    }

    //--- The floor ---------------------------------------------------------------------------
    std::vector<size_t> mites;
    /*
        A stalagmite under a drip: straight under it where the floor reaches that far back, brought
        forward onto the floor if the drip hangs over the stream behind it - from the side the two
        line up in x, which is what reads - and not at all if it hangs further back than that.
    */
    for (size_t k = 0; k < drips_over_floor.size(); k++){
        const Boulder drip = out[drips_over_floor[k]];
        const int t = TopBelow(blocks,drip.x,drip.ground - 1.0f);
        if (t < 0 || params.radius[BOULDER_STALAGMITE] <= 0.0f){
            continue;
        }
        const StageBlock& T = blocks[t];
        if (InsideAny(blocks,drip.x,T.Top() + 0.3f) || RulesAt(biomes,drip.x,T.Top()).mites <= 0.0f){
            continue;
        }
        const uint32_t seed = (uint32_t)(int32_t)floorf(drip.x * 10.0f) * 2u + 3000017u;
        const uint32_t level = (uint32_t)(int32_t)floorf(T.Top() * 10.0f);
        const float s = Lerp(0.6f,1.1f,HashUnit(seed,level,77u));
        const float r = params.radius[BOULDER_STALAGMITE] * s;
        float zlo, zhi;
        if (!Band(T,params,r,true,zlo,zhi)){
            continue;
        }
        const float z = fminf(fmaxf(drip.z,zlo),zhi);
        if (fabsf(z - drip.z) > 1.8f || drip.x - r < T.Left() || drip.x + r > T.Right() ||
            !ClearOfMount(out,params,BOULDER_ON_TOP,T.Top(),drip.x,z,r,0.9f)){
            continue;
        }
        Boulder b = Standing(BOULDER_STALAGMITE,s,drip.x,z,T.Top(),params,0.05f);
        b.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,78u);
        out.push_back(b);
        mites.push_back(out.size() - 1);
    }

    for (const CaveSpot& sp : spots){
        const StageBlock& A = blocks[sp.top];
        const float top = A.Top();
        const BoulderBiome& rules = sp.rules;
        const uint32_t seed = sp.seed, level = sp.level;
        const float bx = sp.x + CAVE_STEP * 0.4f * (2.0f * HashUnit(seed,level,79u) - 1.0f);

        //A stalagmite on its own, toward the back of the floor.
        if (params.radius[BOULDER_STALAGMITE] > 0.0f &&
            HashUnit(seed,level,80u) < rules.mites * CAVE_STEP * Deepen(rules.deep,sp.depth)){
            const float s = Lerp(0.5f,1.0f,HashUnit(seed,level,81u));
            const float r = params.radius[BOULDER_STALAGMITE] * s;
            float zlo, zhi;
            if (Band(A,params,r,true,zlo,zhi) && bx - r > A.Left() && bx + r < A.Right()){
                const float u = HashUnit(seed,level,82u);
                const float z = Lerp(zlo,zhi,u * u);
                if (ClearOfMount(out,params,BOULDER_ON_TOP,top,bx,z,r,0.8f)){
                    Boulder b = Standing(BOULDER_STALAGMITE,s,bx,z,top,params,0.05f);
                    b.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,83u);
                    out.push_back(b);
                    mites.push_back(out.size() - 1);
                }
            }
        }

        //A flat stone, sunk most of the way in: part of the floor rather than something on it.
        if (params.radius[BOULDER_FLAT] > 0.0f && HashUnit(seed,level,84u) < rules.flats * CAVE_STEP){
            const float s = Lerp(0.3f,0.5f,HashUnit(seed,level,85u));
            const float r = params.radius[BOULDER_FLAT] * s;
            float zlo, zhi;
            if (Band(A,params,r,true,zlo,zhi) && bx - r > A.Left() && bx + r < A.Right()){
                const float z = Lerp(zlo,zhi,HashUnit(seed,level,86u));
                if (ClearOfMount(out,params,BOULDER_ON_TOP,top,bx,z,r,0.9f)){
                    Boulder b = Standing(BOULDER_FLAT,s,bx,z,top,params,0.4f);
                    b.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,87u);
                    b.tilt = (4.0f * CAVE_PI / 180.0f) * HashUnit(seed,level,88u);
                    b.tilt_axis_yaw = 2.0f * CAVE_PI * HashUnit(seed,level,89u);
                    out.push_back(b);
                }
            }
        }

        //Remains: a ribcage or a skull, and a bone or two about it - a skull, now and then, by a ribcage.
        if (HashUnit(seed,level,90u) < rules.remains * CAVE_STEP * Deepen(rules.deep,sp.depth)){
            const int centre = (HashUnit(seed,level,91u) < 0.6f) ? BOULDER_RIBCAGE : BOULDER_SKULL;
            const float s = Lerp(0.7f,1.0f,HashUnit(seed,level,92u));
            const float r = params.radius[centre] * s;
            float zlo = 0.0f, zhi = 0.0f;
            const bool f_band = params.radius[centre] > 0.0f && Band(A,params,r,false,zlo,zhi);
            const float cz = Lerp(zlo,zhi,HashUnit(seed,level,93u));
            //Here, or a little to either side if something already lies here: a heap is a heap
            //wherever along this stretch it ends up.
            float cx = bx;
            bool f_room = false;
            for (int k = 0; k < 5 && f_band && !f_room; k++){
                cx = bx + ((k & 1) ? 1.0f : -1.0f) * 0.4f * (float)((k + 1) / 2);
                f_room = cx - r > A.Left() && cx + r < A.Right() &&
                         ClearOfMount(out,params,BOULDER_ON_TOP,top,cx,cz,r,CAVE_NESTLE);
            }
            if (f_room){
                Boulder c = Standing(centre,s,cx,cz,top,params,0.05f);
                c.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,94u);
                out.push_back(c);
                const int extras = 1 + (int)(HashUnit(seed,level,95u) * 2.999f);
                for (int e = 0, tries = 0; e < extras && tries < extras * 8; tries++){
                    const uint32_t t = 140u + (uint32_t)tries * 8u;
                    const int kind = (e == 0 && centre == BOULDER_RIBCAGE && HashUnit(seed,level,t) < 0.5f)
                                   ? BOULDER_SKULL : BOULDER_BONE;
                    if (params.radius[kind] <= 0.0f){
                        continue;
                    }
                    const float es = Lerp(0.8f,1.2f,HashUnit(seed,level,t + 1u));
                    const float er = params.radius[kind] * es;
                    const float a = 2.0f * CAVE_PI * HashUnit(seed,level,t + 2u);
                    const float d = (r + er) * Lerp(0.85f,1.4f,HashUnit(seed,level,t + 3u));
                    const float ex = cx + d * cosf(a);
                    const float ez = cz + CAVE_FLAT * d * sinf(a);
                    float elo, ehi;
                    if (!Band(A,params,er,false,elo,ehi) || ez < elo || ez > ehi || ex - er < A.Left() ||
                        ex + er > A.Right() || !ClearOfMount(out,params,BOULDER_ON_TOP,top,ex,ez,er,CAVE_NESTLE)){
                        continue;
                    }
                    Boulder b = Standing(kind,es,ex,ez,top,params,0.1f);
                    b.yaw = 2.0f * CAVE_PI * HashUnit(seed,level,t + 4u);
                    b.tilt = (8.0f * CAVE_PI / 180.0f) * HashUnit(seed,level,t + 5u);
                    b.tilt_axis_yaw = 2.0f * CAVE_PI * HashUnit(seed,level,t + 6u);
                    out.push_back(b);
                    e++;
                }
            }
        }

        //A cluster of mushrooms, anywhere in the band.
        if (HashUnit(seed,level,96u) < rules.mushrooms * CAVE_STEP * Deepen(rules.deep,sp.depth)){
            const int count = 3 + (int)(HashUnit(seed,level,97u) * 4.999f);
            float zlo, zhi;
            if (Band(A,params,params.radius[BOULDER_CANTHARELL],false,zlo,zhi)){
                MushroomCluster(blocks,A,params,out,bx,Lerp(zlo,zhi,HashUnit(seed,level,98u)),0.55f,count,seed,level);
            }
        }
    }

    //And a few more at a stalagmite's foot, where the drip keeps the floor wet.
    for (size_t k = 0; k < mites.size(); k++){
        const Boulder m = out[mites[k]];
        const int t = TopBelow(blocks,m.x,m.ground + 0.01f);
        if (t < 0){
            continue;
        }
        const StageBlock& A = blocks[t];
        const uint32_t seed = (uint32_t)(int32_t)floorf(m.x * 10.0f) * 2u + 5000011u;
        const uint32_t level = (uint32_t)(int32_t)floorf(A.Top() * 10.0f);
        if (HashUnit(seed,level,99u) >= RulesAt(biomes,m.x,A.Top()).mite_mushrooms){
            continue;
        }
        //Beside it and in front, out of the stream it may be standing in - a cluster round its
        //middle would be inside it.
        const float r = params.radius[BOULDER_STALAGMITE] * m.scale;
        const float side = (HashUnit(seed,level,102u) < 0.5f) ? -1.0f : 1.0f;
        const int count = 2 + (int)(HashUnit(seed,level,103u) * 2.999f);
        float zlo, zhi;
        if (!Band(A,params,params.radius[BOULDER_MUSHROOM_1],false,zlo,zhi)){
            continue;
        }
        const float cz = fminf(fmaxf(m.z + r * 0.8f,zlo),zhi);
        MushroomCluster(blocks,A,params,out,m.x + side * r * 0.9f,cz,0.35f,count,seed,level);
    }

    //--- The walls: bracket fungus, low down ---------------------------------------------------
    for (size_t w = 0; w < blocks.size(); w++){
        const StageBlock& W = blocks[w];
        if (!IsWall(W)){
            continue;
        }
        for (int f = 0; f < 2; f++){
            const float side = (f == 0) ? -1.0f : 1.0f;
            const float xf = (f == 0) ? W.Left() : W.Right();
            const float px = xf + side * 0.05f;     //the air just off the face
            for (float y = W.Bottom() + CAVE_STEP * 0.5f; y < W.Top(); y += CAVE_STEP){
                if (InsideAny(blocks,px,y)){
                    continue;
                }
                const BoulderBiome rules = RulesAt(biomes,px,y);
                if (rules.brackets <= 0.0f){
                    continue;
                }
                const int t = TopBelow(blocks,px,y);
                if (t < 0 || y - blocks[t].Top() < 0.3f || y - blocks[t].Top() > 2.0f){
                    continue;
                }
                const StageBlock& T = blocks[t];
                const uint32_t seed = (uint32_t)(int32_t)floorf(y * 10.0f) * 2u + (f ? 1u : 0u) + 4000037u;
                const uint32_t level = (uint32_t)(int32_t)floorf(xf * 10.0f);
                if (HashUnit(seed,level,110u) >= rules.brackets * CAVE_STEP * Deepen(rules.deep,DepthIn(biomes,px,y))){
                    continue;
                }
                /*
                    Through the slab: up on the face over the stream as well as the floor - it grows
                    on the wall, not in the water - from a unit behind the floor to behind her line.
                */
                const float wide = CAVE_WALL_WIDTH * params.radius[BOULDER_BRACKET_2] * 2.0f;
                const float zlo = fmaxf(W.Back(),T.Back() - 1.0f) + wide;
                const float zhi = params.z_front_max - wide;
                if (zhi < zlo){
                    continue;
                }
                const float zc = Lerp(zlo,zhi,HashUnit(seed,level,111u));
                //And a gap between stacks, or two of them meet in one column - the steps again.
                bool f_apart = true;
                for (const Boulder& o : out){
                    if (BoulderMountOf(o.kind) == BOULDER_ON_WALL && fabsf(o.x - (xf - side * 0.02f)) < 0.2f &&
                        fabsf(o.y - y) < 1.2f && fabsf(o.z - zc) < 0.8f){
                        f_apart = false;
                        break;
                    }
                }
                if (!f_apart){
                    continue;
                }
                //Two or three: a taller stack of even shelves up a wall reads as steps to climb.
                const int count = 2 + (int)(HashUnit(seed,level,112u) * 1.999f);
                float by = y;
                for (int i = 0; i < count; i++){
                    const uint32_t c = 160u + (uint32_t)i * 8u;
                    const int kind = (HashUnit(seed,level,c) < 0.5f) ? BOULDER_BRACKET_1 : BOULDER_BRACKET_2;
                    if (params.radius[kind] <= 0.0f){
                        continue;
                    }
                    Boulder b;
                    b.kind = kind;
                    //Half again to twice as authored: at 1 a bracket is a fingernail on the wall from
                    //where the camera is.
                    b.scale = Lerp(1.3f,2.0f,HashUnit(seed,level,c + 1u));
                    b.x = xf - side * 0.02f;        //its root a touch into the face
                    b.y = by;
                    b.ground = by;
                    //Within the band, which was sized for the widest bracket there is.
                    b.z = fminf(fmaxf(zc + 0.3f * (2.0f * HashUnit(seed,level,c + 2u) - 1.0f),zlo),zhi);
                    //Out of the face: +Z turned to +X on a right face, to -X on a left one.
                    b.yaw = side * 0.5f * CAVE_PI + 0.3f * (2.0f * HashUnit(seed,level,c + 3u) - 1.0f);
                    by += Lerp(0.3f,0.45f,HashUnit(seed,level,c + 4u));
                    if (InsideAny(blocks,px,b.y) || !ClearOnWall(out,params,b)){
                        continue;
                    }
                    out.push_back(b);
                }
            }
        }
    }
}

void ScatterBoulders(const std::vector<StageBlock>& blocks, const BoulderParams& params,
                     std::vector<Boulder>& out, const std::vector<StageBiome>* biomes){
    out.clear();
    std::vector<BoulderCorner> corners;
    FindBoulderCorners(blocks,params,corners);

    for (size_t ci = 0; ci < corners.size(); ci++){
        const BoulderCorner& c = corners[ci];
        const StageBlock& A = blocks[c.top];
        //Seeded by WHERE the corner is, not by its place in the list, so an edit elsewhere in the
        //level does not reshuffle every cluster.
        const uint32_t seed = (uint32_t)(int32_t)floorf(c.x * 10.0f) * 2u + (c.side > 0.0f ? 1u : 0u);
        const uint32_t level = (uint32_t)(int32_t)floorf(A.Top() * 10.0f);
        //A biome may want more of them; the jungle's floor of 0 changes nothing.
        const float chance = fmaxf(params.cluster_chance,RulesAt(biomes,c.x,A.Top()).cluster_at_least);
        if (HashUnit(seed,level,0u) > chance){
            continue;
        }

        //--- The big one, into the corner and to the back ------------------------------------------
        /*
            Which of the big shapes, from its own hash purpose (4) so choosing it leaves every
            other draw for this corner where it was. Only among the kinds that have a size: one
            whose mesh did not load has radius 0, and would otherwise be picked and draw nothing.
        */
        int big_kinds[BOULDER_BIG_KINDS];
        int num_big = 0;
        for (int k = BOULDER_BIG_1; k < BOULDER_BIG_1 + BOULDER_BIG_KINDS; k++){
            if (params.radius[k] > 0.0f){
                big_kinds[num_big++] = k;
            }
        }
        if (num_big == 0){
            continue;
        }
        int pick = (int)(HashUnit(seed,level,4u) * (float)num_big);
        const int kind = big_kinds[pick < num_big ? pick : num_big - 1];

        //Its centre (1 - back_overhang) R in front of the back edge, so its front is at
        //back + (2 - back_overhang) R - which must not pass z_front_max.
        const float back = A.Back();
        const float depth_span = 2.0f - params.back_overhang;
        float s = params.big_scale_min + (params.big_scale_max - params.big_scale_min) * HashUnit(seed,level,1u);
        float R = params.radius[kind] * s;
        if (back + depth_span * R > params.z_front_max){
            R = (params.z_front_max - back) / depth_span;
            s = R / params.radius[kind];
        }
        if (s < params.big_scale_min * 0.5f){
            continue;       //this platform is too shallow behind her for a big rock
        }
        Boulder big;
        big.kind = kind;
        big.scale = s;
        //Slightly INTO the wall: the terrain's fillet fills the foot of a wall, and a rock standing
        //clear of it leaves a gap that reads as the rock floating.
        big.x = c.x + c.side * R * 0.85f;
        big.z = back + (1.0f - params.back_overhang) * R;
        big.ground = A.Top();
        big.y = A.Top() - params.sink * params.height[kind] * s;
        big.yaw = 6.2831853f * HashUnit(seed,level,2u);
        out.push_back(big);

        //--- The small ones, round its base on the open side ---------------------------------------
        size_t first_small = out.size();
        int span = params.small_max - params.small_min + 1;
        int count = params.small_min + (int)(HashUnit(seed,level,3u) * (float)(span > 0 ? span : 1));
        if (count > params.small_max){
            count = params.small_max;
        }
        for (int j = 0, tries = 0; j < count && tries < count * 16; tries++){
            uint32_t t = 16u + (uint32_t)tries * 8u;
            float ss = params.small_scale_min
                     + (params.small_scale_max - params.small_scale_min) * HashUnit(seed,level,t);
            float r = params.radius[BOULDER_SMALL_1] * ss;
            float x = big.x + c.side * (R * 0.2f + HashUnit(seed,level,t + 1u) * (R + params.small_reach));
            //Up to half of it past the back edge, like the big one - a band from the back edge to
            //z_front_max holds nothing much bigger than a pebble otherwise.
            float zlo = A.Back() + params.back_inset;
            float zhi = params.z_front_max - r;
            if (zhi < zlo){
                continue;       //this one is too big to fit; the next may not be
            }
            float z = zlo + (zhi - zlo) * HashUnit(seed,level,t + 2u);
            //On this top, on the open side of the face, clear of the big one and of each other.
            if (x - r < A.Left() || x + r > A.Right() || c.side * (x - c.x) < r * 0.5f){
                continue;
            }
            float dxb = x - big.x, dzb = z - big.z;
            if (sqrtf(dxb * dxb + dzb * dzb) < (R + r) * 0.8f){
                continue;
            }
            bool f_clear = true;
            for (size_t k = first_small; k < out.size(); k++){
                float dx = x - out[k].x, dz = z - out[k].z;
                float rk = params.radius[BOULDER_SMALL_1] * out[k].scale;
                if (sqrtf(dx * dx + dz * dz) < (r + rk) * 0.9f){
                    f_clear = false;
                    break;
                }
            }
            if (!f_clear){
                continue;
            }
            Boulder b;
            b.kind = BOULDER_SMALL_1;
            b.scale = ss;
            b.x = x;
            b.z = z;
            b.ground = A.Top();
            b.y = A.Top() - params.sink * params.height[BOULDER_SMALL_1] * ss;
            b.yaw = 6.2831853f * HashUnit(seed,level,t + 3u);
            b.tilt = (params.small_tilt_deg * 3.14159265f / 180.0f) * HashUnit(seed,level,t + 4u);
            b.tilt_axis_yaw = 6.2831853f * HashUnit(seed,level,t + 5u);
            out.push_back(b);
            j++;
        }
    }

    /*
        RUBBLE, where a biome asks for it: small rocks strewn along the open tops, now and then a
        big one, as if come down from the roof. After the clusters, and clear of them. A candidate
        every half unit, seeded by WHERE it is, like a corner, so an edit elsewhere moves none of
        it; and only where a biome's rubble is above zero is anything drawn at all - so the
        jungle's rocks are exactly what they were.
    */
    if (!biomes || biomes->empty()){
        return;
    }
    std::vector<CaveSpot> cave_spots;
    CaveSpots(blocks,biomes,cave_spots);
    CaveSetPieces(blocks,params,out,cave_spots);
    const float rubble_step = 0.5f;
    for (size_t a = 0; a < blocks.size(); a++){
        const StageBlock& A = blocks[a];
        if (!IsSurface(A)){
            continue;
        }
        const float top = A.Top();
        const uint32_t level = (uint32_t)(int32_t)floorf(top * 10.0f);
        for (float x = A.Left() + rubble_step * 0.5f; x < A.Right(); x += rubble_step){
            const BoulderBiome rules = RulesAt(biomes,x,top);
            if (rules.rubble <= 0.0f){
                continue;
            }
            const uint32_t seed = (uint32_t)(int32_t)floorf(x * 10.0f) * 2u + 1000003u;
            if (HashUnit(seed,level,40u) >= rules.rubble * rubble_step){
                continue;
            }
            //Open ground: nothing standing on the top here.
            if (InsideAny(blocks,x,top + 0.3f)){
                continue;
            }
            const bool f_big = HashUnit(seed,level,41u) < rules.rubble_big;
            int kind = BOULDER_SMALL_1;
            float sc;
            if (f_big){
                kind = (HashUnit(seed,level,42u) < 0.5f) ? BOULDER_BIG_1 : BOULDER_BIG_2;
                if (params.radius[kind] <= 0.0f){
                    kind = BOULDER_BIG_1;
                }
                sc = params.big_scale_min + (params.big_scale_max - params.big_scale_min) * HashUnit(seed,level,43u) * 0.6f;
            }else{
                sc = params.small_scale_min + (params.small_scale_max - params.small_scale_min) * HashUnit(seed,level,43u);
            }
            if (params.radius[kind] <= 0.0f){
                continue;
            }
            float r = params.radius[kind] * sc;
            //Off the half-unit lattice, so a row of them does not read as one.
            const float bx = x + rubble_step * 0.4f * (2.0f * HashUnit(seed,level,45u) - 1.0f);
            //Behind her walking line, like every rock; a big one to the back, as a cluster's is.
            float zlo = A.Back() + params.back_inset;
            float zhi = params.z_front_max - r;
            if (f_big){
                float zb = A.Back() + (1.0f - params.back_overhang) * r;
                if (zb + r > params.z_front_max){
                    continue;
                }
                zlo = zhi = zb;
            }
            if (zhi < zlo || bx - r < A.Left() || bx + r > A.Right()){
                continue;
            }
            float z = zlo + (zhi - zlo) * HashUnit(seed,level,44u);
            if (!ClearOfRocks(out,params,bx,z,r,0.9f)){
                continue;
            }
            Boulder b;
            b.kind = kind;
            b.scale = sc;
            b.x = bx;
            b.z = z;
            b.ground = top;
            b.y = top - params.sink * params.height[kind] * sc;
            b.yaw = 6.2831853f * HashUnit(seed,level,46u);
            if (!f_big){
                b.tilt = (params.small_tilt_deg * 3.14159265f / 180.0f) * HashUnit(seed,level,47u);
                b.tilt_axis_yaw = 6.2831853f * HashUnit(seed,level,48u);
            }
            out.push_back(b);
        }
    }

    //Last, clear of every rock above.
    ScatterCave(blocks,params,out,biomes,cave_spots);
}
