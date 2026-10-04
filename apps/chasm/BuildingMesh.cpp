#include "BuildingMesh.h"
#include "MeshBuild.h"

#include <cmath>

#define HOUSE_FOOTING       0.5f    //how far a wall runs below its level, under the ground's relief
#define ROOF_RISE           0.85f   //ridge above the eaves
#define WINDOW_W            0.30f   //of a wall segment's width... in world units, capped by the segment
#define WINDOW_H            0.36f
#define WINDOW_SILL         0.42f   //of a storey's height
#define DOOR_W              0.34f
#define DOOR_H              0.72f
#define DOOR_CHANCE         4       //one ground-floor segment in this many gets a door
#define CHIMNEY_CHANCE      45      //percent of houses
#define CHIMNEY_W           0.22f
#define FACE_OUT            0.012f  //windows and doors stand this far off their wall

namespace {

/*
    A window or door: a dark rectangle a hair in front of the wall a->b (facing `out`), centred at
    `t` along it, between heights y0 and y1.
*/
void Opening(std::vector<vertex>& out, const vec2& a, const vec2& b, const vec3& outward, float t,
             float half_w, float y0, float y1, int column){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-4f){
        return;
    }
    dir = dir / len;
    half_w = std::min(half_w,len * 0.38f);
    vec2 c = a + (b - a) * t;
    vec2 off(outward.x * FACE_OUT,outward.z * FACE_OUT);
    vec2 l = c - dir * half_w + off;
    vec2 r = c + dir * half_w + off;
    MeshQuad(out,vec3(l.x,y0,l.y),vec3(r.x,y0,r.y),vec3(r.x,y1,r.y),vec3(l.x,y1,l.y),outward,column);
}

//A box standing on (x, z) from y0 to y1, `w` across, square to the world axes - a chimney.
void Box(std::vector<vertex>& out, const vec2& p, float w, float y0, float y1, int column){
    float h = w * 0.5f;
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3(p.x + ((i & 1) ? h : -h),(i & 4) ? y1 : y0,p.y + ((i & 2) ? h : -h));
    }
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,0,-1),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,0,1),column);
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
}

}

void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& picker = *w.picker;
    const GridQuad& quad = g.fine.quads[fine_quad];
    const vec3 up(0.0f,1.0f,0.0f);

    vec2 p[4];
    int s[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
        s[k] = z.storeys[quad.v[k]];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    vec2 m[4];      //m[k] is the midpoint of edge k -> k+1
    for (int k = 0; k < 4; k++){
        m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
    }
    bool f_all_same = (s[0] == s[1] && s[1] == s[2] && s[2] == s[3]);

    for (int k = 0; k < 4; k++){
        if (s[k] == 0){
            continue;
        }
        int plot = quad.v[k];
        //A house's plot is flat by the rules (ZoneCanHouse), so its level is every corner's.
        float level = terrain_levels[t.level[plot]].height;
        float eave = level + s[k] * ZONE_STOREY_HEIGHT;
        int next = (k + 1) % 4;
        int prev = (k + 3) % 4;
        uint32_t look = MeshHash((uint32_t)plot,0x40115Eu);
        int roof_column = ((look % 10) < 7) ? PAL_ROOF : PAL_TIMBER;

        //--- The roof: the quarter P, M1, C, M0, each point at the eave or raised to the ridge ---
        //Raised where the house carries on past it at the same height: always over the plot's own
        //vertex; at an edge midpoint when the corner across it is as tall; at the cell's centre when
        //all four corners are. Each point is decided by the corners it lies between and nothing
        //else, so the cells on either side of it agree.
        vec3 P(p[k].x,eave + ROOF_RISE,p[k].y);
        vec3 M1(m[k].x,eave + ((s[next] == s[k]) ? ROOF_RISE : 0.0f),m[k].y);
        vec3 M0(m[prev].x,eave + ((s[prev] == s[k]) ? ROOF_RISE : 0.0f),m[prev].y);
        vec3 C(centre.x,eave + (f_all_same ? ROOF_RISE : 0.0f),centre.y);
        MeshTri(out,P,M1,C,up,roof_column);
        MeshTri(out,P,C,M0,up,roof_column);

        //--- The walls up the quarter's two outer edges, where the corner across is lower -------
        int neighbour[2] = {next,prev};
        vec2 edge_a[2] = {m[k],centre};
        vec2 edge_b[2] = {centre,m[prev]};
        for (int e = 0; e < 2; e++){
            int j = neighbour[e];
            if (s[j] >= s[k]){
                continue;
            }
            float bottom = (s[j] > 0) ? level + s[j] * ZONE_STOREY_HEIGHT : level - HOUSE_FOOTING;
            vec2 a = edge_a[e];
            vec2 b = edge_b[e];
            vec2 along = b - a;
            vec2 side(-along.y,along.x);
            if (side.dot(p[k] - (a + b) * 0.5f) > 0.0f){
                side = -side;
            }
            side = side / std::max(1e-6f,side.length());
            vec3 outward(side.x,0.0f,side.y);
            MeshQuad(out,vec3(a.x,bottom,a.y),vec3(b.x,bottom,b.y),vec3(b.x,eave,b.y),vec3(a.x,eave,a.y),
                     outward,PAL_WALL);
            //The gable: where the ridge carries on along this edge's far end, the wall closes up to
            //the roof's raised point rather than leaving a hole under it.
            vec3 roof_a = (e == 0) ? M1 : C;
            vec3 roof_b = (e == 0) ? C : M0;
            if (roof_a.y > eave || roof_b.y > eave){
                MeshQuad(out,vec3(a.x,eave,a.y),vec3(b.x,eave,b.y),roof_b,roof_a,outward,PAL_WALL);
            }
            //A window on every storey this wall shows; on the ground floor, sometimes a door.
            int first_storey = (s[j] > 0) ? s[j] : 0;
            for (int st = first_storey; st < s[k]; st++){
                float floor_y = level + st * ZONE_STOREY_HEIGHT;
                bool f_door = (st == 0) && (MeshHash((uint32_t)plot,(uint32_t)fine_quad,(uint32_t)e) % DOOR_CHANCE == 0);
                if (f_door){
                    Opening(out,a,b,outward,0.5f,DOOR_W * 0.5f,floor_y,floor_y + DOOR_H,PAL_BARK_DARK);
                }else{
                    float y0 = floor_y + ZONE_STOREY_HEIGHT * WINDOW_SILL;
                    Opening(out,a,b,outward,0.5f,WINDOW_W * 0.5f,y0,y0 + WINDOW_H,PAL_TIMBER);
                }
            }
        }

        //--- A chimney on some houses, built by one of the plot's quarters --------------------------
        if ((look >> 8) % 100 < CHIMNEY_CHANCE){
            int n = picker.PlotQuadCount(plot);
            int chosen = (int)((look >> 16) % (uint32_t)n);
            if (picker.PlotQuadCorner(plot,chosen) / 4 == fine_quad){
                //Partway from the ridge toward the cell's centre, through the roof.
                vec2 at = p[k] + (centre - p[k]) * 0.45f;
                Box(out,at,CHIMNEY_W,eave + ROOF_RISE * 0.3f,eave + ROOF_RISE + 0.35f,PAL_STONE);
            }
        }
    }
}
