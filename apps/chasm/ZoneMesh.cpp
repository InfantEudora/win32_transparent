#include "ZoneMesh.h"
#include "Palette.h"

#include <algorithm>
#include <cmath>

#define FIELD_LIFT          0.06f   //above the ground's relief, so the field never sinks into it
#define HOUSE_FOOTING       0.5f    //how far a wall runs below its level, under the relief

namespace {

//Wound so its face normal agrees with `want` - see TerrainMesh.cpp's Tri for why.
void Tri(std::vector<vertex>& out, vec3 a, vec3 b, vec3 c, const vec3& want, int column){
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
    v.uv = PaletteUV(column,PAL_TEMPERATE);
    v.matid = 0;
    v.pos = a; out.push_back(v);
    v.pos = b; out.push_back(v);
    v.pos = c; out.push_back(v);
}

}

void BuildZoneChunk(const ChasmWorld& w, const ZoneState& z, int chunk, std::vector<vertex>& out){
    out.clear();
    if (chunk < 0 || chunk >= (int)w.mesh->chunks.size()){
        return;
    }
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const vec3 up(0.0f,1.0f,0.0f);

    for (int q : w.mesh->chunks[chunk].quads){
        const GridQuad& quad = g.fine.quads[q];
        vec2 p[4];
        int s[4];
        bool f_any_house = false;
        for (int k = 0; k < 4; k++){
            p[k] = g.fine.pos[quad.v[k]];
            s[k] = z.storeys[quad.v[k]];
            f_any_house = f_any_house || (s[k] > 0);
        }

        //--- A field: the cell on the ground, lifted a hair ------------------------------------
        if (z.field[quad.parent]){
            float level = terrain_levels[t.level[quad.v[0]]].height;
            vec3 c[4];
            for (int k = 0; k < 4; k++){
                c[k] = vec3(p[k].x,TerrainGroundHeight(p[k],level) + FIELD_LIFT,p[k].y);
            }
            Tri(out,c[0],c[1],c[2],up,PAL_FIELD);
            Tri(out,c[0],c[2],c[3],up,PAL_FIELD);
        }
        if (!f_any_house){
            continue;
        }

        //--- Houses: each corner's quarter, extruded ---------------------------------------------
        vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
        vec2 m[4];      //m[k] is the midpoint of edge k -> k+1
        for (int k = 0; k < 4; k++){
            m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
        }
        for (int k = 0; k < 4; k++){
            if (s[k] == 0){
                continue;
            }
            //A house's plot is flat by the rules (ZoneCanHouse), so its level is every corner's.
            float level = terrain_levels[t.level[quad.v[k]]].height;
            float top = level + s[k] * ZONE_STOREY_HEIGHT;
            vec2 quarter[4] = {p[k],m[k],centre,m[(k + 3) % 4]};

            //The roof: the quarter at the top.
            vec3 r[4];
            for (int i = 0; i < 4; i++){
                r[i] = vec3(quarter[i].x,top,quarter[i].y);
            }
            Tri(out,r[0],r[1],r[2],up,PAL_ROOF);
            Tri(out,r[0],r[2],r[3],up,PAL_ROOF);

            /*
                The two edges this quarter shares with the cell's other quarters: m[k]->centre with
                corner k+1's, centre->m[k-1] with corner k-1's. A wall goes up the difference where
                the neighbour is lower (from below the ground where it has no house at all). The
                quarter's other two edges lie inside its own plot, between quarters of one house.
            */
            int neighbour[2] = {(k + 1) % 4,(k + 3) % 4};
            vec2 edge_a[2] = {m[k],centre};
            vec2 edge_b[2] = {centre,m[(k + 3) % 4]};
            for (int e = 0; e < 2; e++){
                int j = neighbour[e];
                if (s[j] >= s[k]){
                    continue;
                }
                float bottom = (s[j] > 0) ? level + s[j] * ZONE_STOREY_HEIGHT : level - HOUSE_FOOTING;
                vec2 a = edge_a[e];
                vec2 b = edge_b[e];
                //Facing away from this quarter's own corner.
                vec2 along = b - a;
                vec2 side(-along.y,along.x);
                if (side.dot(p[k] - (a + b) * 0.5f) > 0.0f){
                    side = -side;
                }
                vec3 out_dir(side.x,0.0f,side.y);
                vec3 A0(a.x,top,a.y), B0(b.x,top,b.y), A1(a.x,bottom,a.y), B1(b.x,bottom,b.y);
                Tri(out,A0,B0,B1,out_dir,PAL_WALL);
                Tri(out,A0,B1,A1,out_dir,PAL_WALL);
            }
        }
    }
}
