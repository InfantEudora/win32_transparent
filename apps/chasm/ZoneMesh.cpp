#include "ZoneMesh.h"
#include "MeshBuild.h"
#include "CropMesh.h"
#include "BuildingMesh.h"
#include "BoundaryMesh.h"
#include "RoadMesh.h"
#include "CastleMesh.h"
#include "TerrainMesh.h"
#include "Economy.h"

#include <algorithm>

#define GROUND_LIFT         0.04f   //a garden or lot plot above the ground's relief

namespace {

//A garden or lot plot's quarter in this cell, laid on the relief: corner, midpoint, centre, midpoint.
void GroundQuarter(std::vector<vertex>& out, const Terrain& t, const vec2* p, int k, float level, int column){
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    vec2 q[4] = {p[k],(p[k] + p[(k + 1) % 4]) * 0.5f,centre,(p[k] + p[(k + 3) % 4]) * 0.5f};
    vec3 c[4];
    for (int i = 0; i < 4; i++){
        c[i] = vec3(q[i].x,t.GroundHeight(q[i],level) + GROUND_LIFT,q[i].y);
    }
    MeshQuad(out,c[0],c[1],c[2],c[3],vec3(0.0f,1.0f,0.0f),column);
}

/*
    A GARDEN OR LOT STILL A SITE (yards_plan.md, the user's 2026-10-10): staked out as a field site is (CropMesh.h)
    rather than drawn finished - no grass or earth of its own yet, a stake where its outline turns and a cord along
    it. Staked as a whole, not plot by plot: no cord between two plots of the same site - the same ground, the same
    owner - so a lot painted in one drag is one staked-out patch. A plot's outline crosses a cell along two of the
    four spokes from the cell's middle to its edges' middles (GroundQuarter's quarters), so a cell has a cord on
    each spoke with a site on one side and not that same site on the other, and a stake in the middle if any.
*/
void GroundSiteCell(const ChasmWorld& w, const ZoneState& z, int q, std::vector<vertex>& out){
    const GridQuad& quad = w.grid->fine.quads[q];
    const Terrain& t = *w.terrain;
    auto site = [&](int v){
        return ZoneEnclosesGround(z.ground[v]) && !z.ground_built[v] && z.storeys[v] == 0;
    };
    auto same = [&](int a, int b){
        return site(a) && site(b) && z.ground[a] == z.ground[b] && ZoneGroundOwner(z,a) == ZoneGroundOwner(z,b);
    };
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = w.grid->fine.pos[quad.v[k]];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    int stake_on = -1;      //the site plot whose ground the stake stands on
    for (int k = 0; k < 4; k++){
        int a = quad.v[k];
        int b = quad.v[(k + 1) % 4];
        if (!(site(a) || site(b)) || same(a,b)){
            continue;
        }
        int on = site(a) ? a : b;
        float level = t.Height(on);
        vec2 mid = (p[k] + p[(k + 1) % 4]) * 0.5f;
        MeshCord(out,vec3(centre.x,t.GroundHeight(centre,level) + MESH_SITE_CORD,centre.y),
                 vec3(mid.x,t.GroundHeight(mid,level) + MESH_SITE_CORD,mid.y),PAL_WALL);
        stake_on = (stake_on < 0) ? on : stake_on;
    }
    if (stake_on >= 0){
        float ground = t.GroundHeight(centre,t.Height(stake_on));
        MeshPost(out,centre,0.04f,ground - 0.1f,ground + MESH_SITE_STAKE,PAL_TIMBER);
    }
}

}

//--- What the zombies have done (ZoneMesh.h) ----------------------------------------------------------------------------

namespace {

const ZoneHarmMark* FindMark(const std::vector<ZoneHarmMark>& marks, uint32_t id){
    auto it = std::lower_bound(marks.begin(),marks.end(),id,[](const ZoneHarmMark& m, uint32_t i){ return m.id < i; });
    return (it != marks.end() && it->id == id) ? &*it : NULL;
}

}

const ZoneHarmMark* ZoneHarmLook::Wall(int plot) const{
    return (plot >= 0) ? FindMark(walls,(uint32_t)plot) : NULL;
}

const ZoneHarmMark* ZoneHarmLook::Gate(int plot) const{
    return (plot >= 0) ? FindMark(gates,(uint32_t)plot) : NULL;
}

bool ZoneHarmLook::Breached(uint32_t building) const{
    return FindMark(doors,building) != NULL;
}

void ZoneHarmLookOf(const EconomyState& e, const ZoneHarmLook* before, ZoneHarmLook& out){
    out.walls.clear();
    out.gates.clear();
    out.doors.clear();
    //The rules keep their harm by what and then id, so each list comes out by id.
    for (const EconomyHarm& h : e.harm){
        int strength = EconomyHarmStrength(h.what);
        ZoneHarmMark m;
        m.id = h.id;
        m.step = (h.ticks >= strength) ? ZONE_HARM_STEPS
                                       : 1 + (int)((int64_t)h.ticks * (ZONE_HARM_STEPS - 1) / strength);
        if (h.what == HARM_DOOR){
            if (m.step == ZONE_HARM_STEPS){
                out.doors.push_back(m);
            }
            continue;
        }
        const ZoneHarmMark* was = !before ? NULL : (h.what == HARM_WALL) ? before->Wall((int)h.id) : before->Gate((int)h.id);
        if (was && was->f_from){
            m.f_from = true;
            m.from = was->from;
        }else{
            //Where the zombies hitting it stand: the side its blows come from.
            vec2 sum;
            int n = 0;
            for (const EconomyZombie& z : e.zombies){
                if (z.hit == h.what && z.hit_id == h.id && EconomyZombieHitting(z)){
                    sum = sum + z.pos;
                    n++;
                }
            }
            if (n > 0){
                m.f_from = true;
                m.from = sum * (1.0f / n);
            }
        }
        ((h.what == HARM_WALL) ? out.walls : out.gates).push_back(m);
    }
}

void ZoneHarmChunks(const ChasmWorld& w, const ZoneState& z, const ZoneHarmLook& a, const ZoneHarmLook& b,
                    std::vector<uint8_t>& f_chunk){
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    auto mark_plot = [&](int v){
        if (v < 0 || v >= (int)g.fine.pos.size()){
            return;
        }
        for (int i = 0; i < picker.PlotQuadCount(v); i++){
            int chunk = TerrainChunkOfQuad(g,*w.mesh,picker.PlotQuadCorner(v,i) / 4);
            if (chunk >= 0 && chunk < (int)f_chunk.size()){
                f_chunk[chunk] = 1;
            }
        }
    };
    //A plot's palisade or gate, and its neighbours': a gate's crossbeam hangs from a broken plot's jamb.
    auto mark_line = [&](uint32_t v){
        mark_plot((int)v);
        int nb[8];
        int n = ZonePlotNeighbours(w,(int)v,nb,8);
        for (int i = 0; i < n; i++){
            mark_plot(nb[i]);
        }
    };
    //Each mark in one list and not the other, or drawn differently, is a change. Both lists are by id.
    auto differ = [](const std::vector<ZoneHarmMark>& x, const std::vector<ZoneHarmMark>& y, std::vector<uint32_t>& ids){
        size_t i = 0, j = 0;
        while (i < x.size() || j < y.size()){
            if (j == y.size() || (i < x.size() && x[i].id < y[j].id)){
                ids.push_back(x[i++].id);
            }else if (i == x.size() || y[j].id < x[i].id){
                ids.push_back(y[j++].id);
            }else{
                if (x[i].step != y[j].step || x[i].f_from != y[j].f_from){
                    ids.push_back(x[i].id);
                }
                i++;
                j++;
            }
        }
    };
    std::vector<uint32_t> ids;
    differ(a.walls,b.walls,ids);
    differ(a.gates,b.gates,ids);
    for (uint32_t v : ids){
        mark_line(v);
    }
    ids.clear();
    differ(a.doors,b.doors,ids);
    if (!ids.empty()){
        //A building's plots are not listed anywhere but on the plots: rare enough to look for.
        for (size_t v = 0; v < z.building.size(); v++){
            if (z.building[v] && std::find(ids.begin(),ids.end(),z.building[v]) != ids.end()){
                mark_plot((int)v);
            }
        }
    }
}

void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out,
                    const std::vector<uint8_t>* field_stage, const ZoneHarmLook* harm){
    out.clear();
    if (chunk < 0 || chunk >= (int)w.mesh->chunks.size()){
        return;
    }
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;

    for (int q : w.mesh->chunks[chunk].quads){
        const GridQuad& quad = g.fine.quads[q];
        vec2 p[4];
        bool f_house = false;
        bool f_ground = false;
        bool f_road = false;
        for (int k = 0; k < 4; k++){
            p[k] = g.fine.pos[quad.v[k]];
            f_house = f_house || z.storeys[quad.v[k]] > 0;
            f_ground = f_ground || ZoneEnclosesGround(z.ground[quad.v[k]]);
            //A standing bridge is drawn as a road over the water, a planned road as its stakes - and a
            //wall along the edges the same way, planned or standing (RoadMesh.h, WALLS).
            f_road = f_road || ZoneRoadStands(z,quad.v[k]) || ZoneRoadPlanned(z,quad.v[k])
                     || z.ground[quad.v[k]] == ZONE_GROUND_WALL;
        }
        if (f_ground){
            for (int k = 0; k < 4; k++){
                int gk = z.ground[quad.v[k]];
                //A site's ground is not laid until it stands: it is staked out instead (GroundSiteCell).
                if (!ZoneEnclosesGround(gk) || z.storeys[quad.v[k]] > 0 || !z.ground_built[quad.v[k]]){
                    continue;
                }
                float level = t.Height(quad.v[k]);
                GroundQuarter(out,t,p,k,level,(gk == ZONE_GROUND_GARDEN) ? PAL_BUSH : PAL_PATH);
            }
            GroundSiteCell(w,z,q,out);
        }
        if (f_road || f_ground){    //f_ground: the path just inside a gate
            BuildRoadCell(w,z,q,out,harm);
        }
        if (z.field[quad.parent]){
            uint32_t id = z.field[quad.parent];
            int stage = (field_stage && id < field_stage->size()) ? (*field_stage)[id] : FIELD_STAGE_RIPE;
            BuildFieldCell(w,z,q,stage,out);
        }
        if (f_house){
            BuildHouseCell(w,z,q,out,NULL,harm);
        }
        if (f_house || f_ground || z.field[quad.parent]){
            BuildBoundaryCell(w,z,q,out);
        }
        //The debug castle's stone (ramparts_plan.md).
        for (int k = 0; k < 4; k++){
            if (ZoneCastleShape(z,quad.v[k]) != ZONE_CASTLE_NONE){
                BuildCastleCell(w,z,q,out);
                break;
            }
        }
    }
}

void BuildPlotTile(const ChasmWorld& w, int v, float lift, std::vector<vertex>& out, float floor){
    const GridPicker& p = *w.picker;
    const Grid& g = *w.grid;
    float level = w.terrain->Height(v);
    auto at = [&](const vec2& x){
        return vec3(x.x,std::max(w.terrain->GroundHeight(x,level) + lift,floor),x.y);
    };
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        int qc = p.PlotQuadCorner(v,i);
        const GridQuad& q = g.fine.quads[qc / 4];
        int k = qc % 4;
        vec2 a = g.fine.pos[q.v[k]];
        vec2 centre = (g.fine.pos[q.v[0]] + g.fine.pos[q.v[1]] + g.fine.pos[q.v[2]] + g.fine.pos[q.v[3]]) * 0.25f;
        vec2 b = (a + g.fine.pos[q.v[(k + 1) % 4]]) * 0.5f;
        vec2 d = (a + g.fine.pos[q.v[(k + 3) % 4]]) * 0.5f;
        MeshQuad(out,at(a),at(b),at(centre),at(d),vec3(0.0f,1.0f,0.0f),PAL_PATH);
    }
}
