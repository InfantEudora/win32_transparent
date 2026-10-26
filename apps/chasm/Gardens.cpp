#include "Gardens.h"
#include "Forest.h"
#include "MeshBuild.h"

#include <algorithm>
#include <cmath>

//Current tuning (gardens_plan.md; tuning numbers, not rules).
#define GARDEN_SET          0.03f   //what is planted stands this far over the ground: just under the lawn (ZoneMesh.cpp)
#define GARDEN_BORDER_IN    0.34f   //a flower border's clumps, this far in from its garden wall...
#define GARDEN_BORDER_HEDGE 0.62f   //...or from a retaining wall's face, past the hedge behind its capstones (TerraceMesh.cpp)
#define GARDEN_BORDER_SHARE 70u     //percent of a walled side with a second clump
#define GARDEN_BUSH_SHARE   50u     //percent of the quads with a garden quarter beside more of it that have one bush or shrub...
#define GARDEN_BUSH_CLEAR   1.1f    //...not this near the tree or bush its plot kept
#define GARDEN_BUSH_AT      0.55f   //...that far from the plot's own corner toward the cell's middle - 0.4 round a cypress
#define GARDEN_BUSH_SIZE    0.6f    //...a garden's, clipped: this of the forest's size, give or take a sixth
#define GARDEN_CYPRESS_TALL 2.4f

bool GardenStands(const ZoneState& z, int v){
    return v >= 0 && v < (int)z.ground.size() && z.ground[v] == ZONE_GROUND_GARDEN && z.ground_built[v]
           && z.storeys[v] == 0;
}

namespace {

//Two plots on one ground: the same terrace's top, or the terrain on the same level.
bool OnSameGround(const ChasmWorld& w, const ZoneState& z, int a, int b){
    bool ta = ZoneTerraceStands(z,a);
    bool tb = ZoneTerraceStands(z,b);
    if (ta || tb){
        return ta && tb && z.terrace[a] == z.terrace[b];
    }
    return w.terrain->SameGround(a,b);
}

//The tree or bush a garden plot kept (Forest.h, ForestGardenKeeps), or NULL. Never on a terrace, which was built up over
//whatever grew there before any garden was painted on it.
const PropInstance* KeptOn(const ChasmWorld& w, const ZoneState& z, int v){
    const ForestData& f = *w.forest;
    if (v < 0 || v >= (int)f.plot_prop.size() || f.plot_prop[v] < 0 || ZoneTerraced(z,v)){
        return NULL;
    }
    const PropInstance& p = f.props[f.plot_prop[v]];
    return ForestGardenKeeps(p.kind) ? &p : NULL;
}

float Unit(uint32_t h, int shift){
    return (float)((h >> shift) % 1000u) / 1000.0f;
}

}

void GardenQuadPlants(const ChasmWorld& w, const ZoneState& z, int q, std::vector<GardenPlant>& out){
    if (!w.forest){
        return;
    }
    const GridQuad& quad = w.grid->fine.quads[q];
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = w.grid->fine.pos[quad.v[k]];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    vec3 cypress_foot;
    float cypress_tall = 0.0f;
    uint32_t cypress_seed = 0;
    bool f_cypress = GardenQuadCypress(w,z,q,&cypress_foot,&cypress_tall,&cypress_seed);
    //The one quarter of the quad a bush may stand in: of those beside more of their garden, the one the quad's hash picks.
    int bush_k = -1;
    uint32_t bush_h = 0xFFFFFFFFu;
    vec2 bush_at;
    int bush_biome = 0;
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        if (!GardenStands(z,v)){
            continue;
        }
        int nb[2] = {quad.v[(k + 1) % 4],quad.v[(k + 3) % 4]};
        //Where the path comes in at a gate, nothing planted in its way.
        if (ZoneGateBetween(w,z,v,nb[0]) || ZoneGateBetween(w,z,v,nb[1])){
            continue;
        }
        //The quarter's two edge points, as its lawn has them (ZoneMesh.cpp, GroundQuarter): on a terrace the crossings.
        vec2 edge[2] = {(p[k] + p[(k + 1) % 4]) * 0.5f,(p[k] + p[(k + 3) % 4]) * 0.5f};
        if (ZoneTerraceStands(z,v)){
            edge[0] = BuildingWallCrossing(w,quad.v[k],nb[0]);
            edge[1] = BuildingWallCrossing(w,nb[1],quad.v[k]);
        }
        int biome = w.terrain->biome[v];
        const PropInstance* kept = KeptOn(w,z,v);
        auto set_at = [&](int kind, const vec2& at, uint32_t h, float scale){
            GardenPlant g;
            g.kind = kind;
            g.pos = vec3(at.x,ZoneGroundHeight(w,z,v,at) + GARDEN_SET,at.y);
            g.yaw = Unit(h,3) * 6.2831853f;
            g.scale = scale;
            g.seed = h;
            out.push_back(g);
        };
        bool f_more = false;    //another plot of the garden beside this one, in this quad
        for (int s = 0; s < 2; s++){
            int u = nb[s];
            if (GardenStands(z,u) && OnSameGround(w,z,v,u)){
                f_more = true;
                continue;
            }
            //A WALLED side: a border along it, set in from the wall - further from a retaining wall, past its hedge.
            float in = (ZoneTerraceStep(w,z,v,u) != ZONE_STEP_OPEN) ? GARDEN_BORDER_HEDGE : GARDEN_BORDER_IN;
            vec2 along = centre - edge[s];
            float len = along.length();
            if (len < 1e-4f){
                continue;
            }
            vec2 n(-along.y / len,along.x / len);
            if (n.dot(p[k] - edge[s]) < 0.0f){
                n = n * -1.0f;
            }
            uint32_t hs = MeshHash((uint32_t)v,(uint32_t)q,(uint32_t)(s + 1) * 0x9E37u);
            int clumps = (hs % 100u < GARDEN_BORDER_SHARE) ? 2 : 1;
            for (int c = 0; c < clumps; c++){
                uint32_t hc = MeshHash(hs,(uint32_t)c);
                float t = (clumps == 1) ? 0.35f + 0.3f * Unit(hc,0) : 0.18f + 0.32f * (float)c + 0.14f * Unit(hc,0);
                vec2 at = edge[s] + along * t + n * in;
                int kind = (biome == TERRAIN_BIOME_SWAMP) ? PROP_FERN : (biome == TERRAIN_BIOME_DESERT) ? PROP_SHRUB
                         : ((hc >> 20) % 2u) ? PROP_FLOWERS_A : PROP_FLOWERS_B;
                set_at(kind,at,hc,0.85f + 0.3f * Unit(hc,12));
            }
        }
        //A BUSH or a shrub inside, once the garden is more than this plot - clear of what the plot kept, and of a cypress.
        uint32_t hb = MeshHash((uint32_t)v,(uint32_t)q,0x62757368u);
        if (f_more && hb < bush_h){
            float share = f_cypress ? 0.4f : GARDEN_BUSH_AT;
            vec2 at = p[k] + (centre - p[k]) * share + vec2(Unit(hb,7) - 0.5f,Unit(hb,17) - 0.5f) * 0.3f;
            vec2 keep = kept ? vec2(kept->pos.x,kept->pos.z) : at;
            if (!kept || (keep - at).length() >= GARDEN_BUSH_CLEAR){
                bush_k = k;
                bush_h = hb;
                bush_at = at;
                bush_biome = biome;
            }
        }
    }
    if (bush_k >= 0 && MeshHash((uint32_t)q,0x62757368u) % 100u < GARDEN_BUSH_SHARE){
        int v = quad.v[bush_k];
        uint32_t hb = bush_h;
        int kind = (bush_biome == TERRAIN_BIOME_DESERT) ? PROP_SHRUB : ((hb >> 9) % 3u == 0u) ? PROP_SHRUB
                 : ((hb >> 9) % 3u == 1u) ? PROP_BUSH_A : PROP_BUSH_B;
        GardenPlant g;
        g.kind = kind;
        g.pos = vec3(bush_at.x,ZoneGroundHeight(w,z,v,bush_at) + GARDEN_SET,bush_at.y);
        g.yaw = Unit(hb,3) * 6.2831853f;
        g.scale = GARDEN_BUSH_SIZE * (0.85f + 0.3f * Unit(hb,22));
        g.seed = hb;
        out.push_back(g);
    }
}

bool GardenQuadCypress(const ChasmWorld& w, const ZoneState& z, int q, vec3* foot, float* tall, uint32_t* seed){
    if (!w.forest){
        return false;
    }
    const GridQuad& quad = w.grid->fine.quads[q];
    //One quad of each coarse cell may have one, chosen by the cell: so they stand apart, and a garden of many cells
    //is not an orchard of them. Its four quads are parent * 4 on.
    if ((uint32_t)(q - quad.parent * 4) != MeshHash((uint32_t)quad.parent,0x63797072u) % 4u){
        return false;
    }
    vec2 centre;
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        if (!GardenStands(z,v) || !OnSameGround(w,z,quad.v[0],v) || KeptOn(w,z,v)){
            return false;
        }
        centre += w.grid->fine.pos[v] * 0.25f;
    }
    uint32_t h = MeshHash((uint32_t)q,0x63797072u);
    *foot = vec3(centre.x,ZoneGroundHeight(w,z,quad.v[0],centre) + GARDEN_SET,centre.y);
    *tall = GARDEN_CYPRESS_TALL * (0.88f + 0.24f * Unit(h,4));
    *seed = h;
    return true;
}
