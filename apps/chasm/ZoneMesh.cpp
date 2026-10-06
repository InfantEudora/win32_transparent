#include "ZoneMesh.h"
#include "MeshBuild.h"
#include "CropMesh.h"
#include "BuildingMesh.h"
#include "BoundaryMesh.h"
#include "RoadMesh.h"

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

}

void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out,
                    const std::vector<uint8_t>* field_stage){
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
            //A standing bridge is drawn as a road over the water, a planned road as its stakes.
            f_road = f_road || ZoneRoadStands(z,quad.v[k]) || ZoneRoadPlanned(z,quad.v[k]);
        }
        if (f_ground){
            for (int k = 0; k < 4; k++){
                int gk = z.ground[quad.v[k]];
                if (!ZoneEnclosesGround(gk) || z.storeys[quad.v[k]] > 0){
                    continue;
                }
                float level = t.Height(quad.v[k]);
                GroundQuarter(out,t,p,k,level,(gk == ZONE_GROUND_GARDEN) ? PAL_BUSH : PAL_PATH);
            }
        }
        if (f_road || f_ground){    //f_ground: the path just inside a gate
            BuildRoadCell(w,z,q,out);
        }
        if (z.field[quad.parent]){
            uint32_t id = z.field[quad.parent];
            int stage = (field_stage && id < field_stage->size()) ? (*field_stage)[id] : FIELD_STAGE_RIPE;
            BuildFieldCell(w,z,q,stage,out);
        }
        if (f_house){
            BuildHouseCell(w,z,q,out);
        }
        if (f_house || f_ground || z.field[quad.parent]){
            BuildBoundaryCell(w,z,q,out);
        }
    }
}

void BuildPlotTile(const ChasmWorld& w, int v, float lift, std::vector<vertex>& out){
    const GridPicker& p = *w.picker;
    const Grid& g = *w.grid;
    float level = w.terrain->Height(v);
    auto at = [&](const vec2& x){
        return vec3(x.x,w.terrain->GroundHeight(x,level) + lift,x.y);
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
