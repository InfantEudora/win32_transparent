#include "TerraceMesh.h"
#include "MeshBuild.h"
#include "Palette.h"
#include "Walkers.h"

#include <algorithm>

#define TERRACE_LIFT        0.02f   //its top over the terrain, where the two all but meet
#define TERRACE_SINK        0.15f   //a wall's foot into the ground under it
#define TERRACE_COPING_H    0.07f   //the coping along a wall's head: over the top...
#define TERRACE_COPING_W    0.14f   //...and in from the wall's face

void BuildTerraceCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridQuad& quad = g.fine.quads[fine_quad];
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    vec2 m[4];      //m[k] the midpoint of edge k -> k+1: where the quarters meet, as on the ground (TerrainMesh)
    for (int k = 0; k < 4; k++){
        m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
    }
    const vec3 up(0.0f,1.0f,0.0f);
    //One of the grass's shades for the cell, as the terrain picks one a coarse cell.
    int grass = PAL_GRASS_0 + (int)(MeshHash((uint32_t)quad.parent,77u) % PAL_GRASS_COUNT);
    auto site = [&](int v){
        return ZoneTerraced(z,v) && !ZoneTerraceStands(z,v);
    };
    bool f_staked = false;
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        if (!ZoneTerraced(z,v)){
            continue;
        }
        int next = (k + 1) % 4;
        int prev = (k + 3) % 4;
        int across[2] = {quad.v[next],quad.v[prev]};
        vec2 edge_a[2] = {m[k],centre};
        vec2 edge_b[2] = {centre,m[prev]};
        float level = t.Height(v);
        if (site(v)){
            //Staked out: a cord along each edge to anything but more of the same site.
            for (int e = 0; e < 2; e++){
                int u = across[e];
                if (site(u) && z.terrace[u] == z.terrace[v]){
                    continue;
                }
                const vec2& a = edge_a[e];
                const vec2& b = edge_b[e];
                MeshCord(out,vec3(a.x,t.GroundHeight(a,level) + MESH_SITE_CORD,a.y),
                         vec3(b.x,t.GroundHeight(b,level) + MESH_SITE_CORD,b.y),PAL_WALL);
                f_staked = true;
            }
            continue;
        }
        int row = (t.biome[v] == TERRAIN_BIOME_SWAMP) ? PAL_SWAMP : (t.biome[v] == TERRAIN_BIOME_DESERT) ? PAL_DESERT
                : PAL_TEMPERATE;
        float top = ZoneTerraceTop(z,v) + TERRACE_LIFT;
        MeshQuad(out,vec3(p[k].x,top,p[k].y),vec3(m[k].x,top,m[k].y),vec3(centre.x,top,centre.y),
                 vec3(m[prev].x,top,m[prev].y),up,grass,row);
        for (int e = 0; e < 2; e++){
            int u = across[e];
            bool f_terrace = ZoneTerraceStands(z,u);
            if (f_terrace && z.terrace[u] >= z.terrace[v]){
                continue;       //as high or higher: one floor, or its wall
            }
            const vec2& a = edge_a[e];
            const vec2& b = edge_b[e];
            auto foot = [&](const vec2& x){
                return f_terrace ? ZoneTerraceTop(z,u) : t.GroundHeight(x,level) - TERRACE_SINK;
            };
            vec2 side(-(b - a).y,(b - a).x);
            if (side.dot(p[k] - (a + b) * 0.5f) > 0.0f){
                side = side * -1.0f;
            }
            side = side * (1.0f / std::max(1e-6f,side.length()));
            vec3 outward(side.x,0.0f,side.y);
            float head = top + TERRACE_COPING_H;
            MeshQuad(out,vec3(a.x,foot(a),a.y),vec3(b.x,foot(b),b.y),vec3(b.x,head,b.y),vec3(a.x,head,a.y),outward,PAL_STONE);
            //Against a building standing over the top the wall is the building's from there up: no coping along its foot,
            //where its upper door may open onto the terrace (terraces_plan.md stage 3).
            int kind = z.KindOf(u);
            if (!f_terrace && z.standing[u] > 0 && ZoneKindHasBody(kind)
                && (BuildingFootHalves(w,z,kind,u) + 2 * z.standing[u]) * ZONE_STOREY_HEIGHT * 0.5f > top){
                continue;
            }
            //...and against a castle piece standing to the top: its deck goes on onto the terrace (ramparts_plan.md).
            if (ZoneCastleShape(z,u) != ZONE_CASTLE_NONE
                && WalkNodeHalves(w,z,u,ZoneCastleLayer(z,u)) * ZONE_STOREY_HEIGHT * 0.5f >= top - TERRACE_LIFT - 1e-3f){
                continue;
            }
            //The coping: its head, and its face over the top.
            vec2 ai = a - side * TERRACE_COPING_W;
            vec2 bi = b - side * TERRACE_COPING_W;
            MeshQuad(out,vec3(a.x,head,a.y),vec3(b.x,head,b.y),vec3(bi.x,head,bi.y),vec3(ai.x,head,ai.y),up,PAL_STONE_LIGHT);
            MeshQuad(out,vec3(ai.x,top,ai.y),vec3(bi.x,top,bi.y),vec3(bi.x,head,bi.y),vec3(ai.x,head,ai.y),outward * -1.0f,
                     PAL_STONE_LIGHT);
        }
    }
    //A stake at the cell's middle wherever an outline turns through it.
    if (f_staked){
        float level = t.Height(quad.v[0]);
        float ground = t.GroundHeight(centre,level);
        MeshPost(out,centre,0.04f,ground - 0.1f,ground + MESH_SITE_STAKE,PAL_TIMBER);
    }
}
