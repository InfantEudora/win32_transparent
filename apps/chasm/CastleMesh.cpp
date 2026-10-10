#include "CastleMesh.h"
#include "BuildingMesh.h"
#include "MeshBuild.h"
#include "Walkers.h"

#include <algorithm>
#include <cmath>

#define CASTLE_SINK         0.15f   //walls into the ground, so a slope never shows a gap under one
#define CASTLE_SLAB         0.35f   //an arch's slab: its passage is its storeys less this
#define CASTLE_PILLAR       0.15f   //an arch's pillar, half its width
#define CASTLE_PILLAR_IN    0.18f   //...set in from its plot's corner toward its point, so the corners' pillars cluster
#define CASTLE_PARAPET      0.34f   //a low wall along a deck's open edge: above a villager's knee (0.86 tall)
#define CASTLE_PARAPET_W    0.14f
#define CASTLE_STEP         (ZONE_STOREY_HEIGHT / 6.0f)     //a stair's step: six to a storey

namespace {

vec3 At(const vec2& p, float y){
    return vec3(p.x,y,p.y);
}

struct RampPoint{
    vec2 at;
    float h;
};

//The part of a polygon on a ramp at or above height `level` (f_above), or below it, cut where the ramp crosses it.
std::vector<RampPoint> ClipRamp(const std::vector<RampPoint>& in, float level, bool f_above){
    std::vector<RampPoint> out;
    for (size_t i = 0; i < in.size(); i++){
        const RampPoint& a = in[i];
        const RampPoint& b = in[(i + 1) % in.size()];
        bool f_a = f_above ? a.h >= level : a.h < level;
        bool f_b = f_above ? b.h >= level : b.h < level;
        if (f_a){
            out.push_back(a);
        }
        if (f_a != f_b){
            float f = (level - a.h) / (b.h - a.h);
            RampPoint c;
            c.at = a.at + (b.at - a.at) * f;
            c.h = level;
            out.push_back(c);
        }
    }
    return out;
}

/*
    A triangle of a stair's ramp, drawn as STEPS: the ramp's height is linear over it, so every step's height crosses it
    in a straight line. Between two of them a flat tread, half way up the step - so a walker on the ramp
    (ApplicationChasmWorkersView.cpp) is never more than half a step off it - and at each a riser up to the next,
    facing down the ramp. Heights are the world's, so the steps of neighbouring triangles line up.
*/
void SteppedTri(std::vector<vertex>& out, const RampPoint& a, const RampPoint& b, const RampPoint& c, int column){
    float lo = std::min(a.h,std::min(b.h,c.h));
    float hi = std::max(a.h,std::max(b.h,c.h));
    //Down the ramp: the way its height falls, in the plane - which the risers face.
    vec2 e1 = b.at - a.at;
    vec2 e2 = c.at - a.at;
    float det = e1.x * e2.y - e1.y * e2.x;
    vec2 down(0.0f,0.0f);
    if (std::fabs(det) > 1e-8f){
        float gx = ((b.h - a.h) * e2.y - (c.h - a.h) * e1.y) / det;
        float gz = ((c.h - a.h) * e1.x - (b.h - a.h) * e2.x) / det;
        down = vec2(-gx,-gz);
    }
    std::vector<RampPoint> tri = {a,b,c};
    for (int k = (int)std::floor(lo / CASTLE_STEP); (float)k * CASTLE_STEP < hi; k++){
        float bottom = (float)k * CASTLE_STEP;
        std::vector<RampPoint> band = ClipRamp(ClipRamp(tri,bottom,true),bottom + CASTLE_STEP,false);
        float tread = bottom + 0.5f * CASTLE_STEP;
        for (size_t i = 1; i + 1 < band.size(); i++){
            MeshTri(out,At(band[0].at,tread),At(band[i].at,tread),At(band[i + 1].at,tread),vec3(0,1,0),column);
        }
        //Its riser, where the ramp crosses the top of this step - if it does, inside the triangle.
        float top = bottom + CASTLE_STEP;
        if (top >= hi || down.dot(down) < 1e-10f){
            continue;
        }
        std::vector<vec2> cross;
        for (int i = 0; i < 3; i++){
            const RampPoint& p = tri[i];
            const RampPoint& q = tri[(i + 1) % 3];
            if ((p.h < top) != (q.h < top)){
                cross.push_back(p.at + (q.at - p.at) * ((top - p.h) / (q.h - p.h)));
            }
        }
        if (cross.size() == 2){
            MeshQuad(out,At(cross[0],tread),At(cross[1],tread),At(cross[1],tread + CASTLE_STEP),At(cross[0],tread + CASTLE_STEP),
                     vec3(down.x,0.0f,down.y),PAL_STONE);
        }
    }
}

//An axis-aligned box from lo to hi.
void Box(std::vector<vertex>& out, const vec3& lo, const vec3& hi, int column){
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3((i & 1) ? hi.x : lo.x,(i & 2) ? hi.y : lo.y,(i & 4) ? hi.z : lo.z);
    }
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[3],c[2],vec3(0,0,-1),column);
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,0,1),column);
}

/*
    One piece's quarter of a cell: its corner k. `shape` and `layer` are the piece's - the plot's own, or the arch over
    it, whose pillar stands on the piece under it at `floor_c` (f_over) rather than on the ground. Returns the top's
    height at the cell's middle, where an arch over it stands its pillar.
*/
float PieceQuarter(std::vector<vertex>& out, const ChasmWorld& w, const ZoneState& z, const GridQuad& quad, const vec2* p,
                   int k, int shape, int layer, bool f_over, float floor_c){
    const Terrain& t = *w.terrain;
    const vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    const float half = 0.5f * ZONE_STOREY_HEIGHT;
    int v = quad.v[k];
    float level = t.Height(v);
    float top_v = t.ground[v] + (float)layer * half;
    //Toward this cell's two other plots beside it: the edge's midpoint, and the top's height there.
    int nb[2] = {quad.v[(k + 1) % 4],quad.v[(k + 3) % 4]};
    vec2 mid[2] = {(p[k] + p[(k + 1) % 4]) * 0.5f,(p[k] + p[(k + 3) % 4]) * 0.5f};
    float top_m[2];
    bool f_open[2];
    bool f_climbs[2];       //a stair's top: the side it climbs to
    for (int i = 0; i < 2; i++){
        int meet = WalkJoinAny(w,z,v,layer,nb[i]);     //where the walkers step across, the tops meet
        f_open[i] = meet < 0;
        f_climbs[i] = meet == layer + 1;
        if (meet < 0){
            top_m[i] = top_v;
        }else if (meet == 0){
            top_m[i] = t.GroundHeight(mid[i],level);
        }else{
            top_m[i] = (t.ground[v] + t.ground[nb[i]]) * 0.5f + (float)meet * half;
        }
    }
    float top_c = (top_m[0] + top_m[1]) * 0.5f;
    bool f_stair = shape == ZONE_CASTLE_STAIR;
    bool f_arch = shape == ZONE_CASTLE_ARCH;

    //--- The top: the deck, or the stair's ramp in steps ------------------------------------------------
    if (f_stair){
        RampPoint q[4] = {{p[k],top_v},{mid[0],top_m[0]},{centre,top_c},{mid[1],top_m[1]}};
        SteppedTri(out,q[0],q[1],q[2],PAL_STONE_LIGHT);
        SteppedTri(out,q[0],q[2],q[3],PAL_STONE_LIGHT);
    }else{
        MeshQuad(out,At(p[k],top_v),At(mid[0],top_m[0]),At(centre,top_c),At(mid[1],top_m[1]),vec3(0,1,0),
                 PAL_STONE_LIGHT);
    }
    if (f_arch){
        MeshQuad(out,At(p[k],top_v - CASTLE_SLAB),At(mid[0],top_m[0] - CASTLE_SLAB),At(centre,top_c - CASTLE_SLAB),
                 At(mid[1],top_m[1] - CASTLE_SLAB),vec3(0,-1,0),PAL_STONE_DARK);
        //A pillar at its plot's corner here, from the ground - or the piece under it - to the slab.
        vec2 in = p[k] - centre;
        float l = in.length();
        vec2 pc = centre + ((l > 1e-4f) ? in * (CASTLE_PILLAR_IN / l) : vec2(0.0f,0.0f));
        float foot = f_over ? floor_c : t.GroundHeight(pc,level) - CASTLE_SINK;
        Box(out,vec3(pc.x - CASTLE_PILLAR,foot,pc.y - CASTLE_PILLAR),
            vec3(pc.x + CASTLE_PILLAR,top_c - CASTLE_SLAB,pc.y + CASTLE_PILLAR),PAL_STONE);
    }

    //--- Its sides: the two half-edges of its plot's outline in this cell, each facing a neighbour --------
    for (int i = 0; i < 2; i++){
        vec2 a = mid[i];
        vec2 b = centre;
        float ta = top_m[i];
        float tb = top_c;
        vec2 n(-(b.y - a.y),b.x - a.x);
        float nl = n.length();
        if (nl < 1e-5f){
            continue;
        }
        n = n * (1.0f / nl);
        if (n.dot((a + b) * 0.5f - p[k]) < 0.0f){
            n = n * -1.0f;      //out of the plot
        }
        vec3 want(n.x,0.0f,n.y);
        float ba = f_arch ? ta - CASTLE_SLAB : t.GroundHeight(a,level) - CASTLE_SINK;
        float bb = f_arch ? tb - CASTLE_SLAB : t.GroundHeight(b,level) - CASTLE_SINK;
        //A wall-walk's open edge: a low parapet, its outer face the wall's carried on up. A stair's sides stand half
        //a step over its ramp, as high as its treads go, but where it reaches the top it climbs to.
        float rise = (f_open[i] && !f_stair) ? CASTLE_PARAPET : 0.0f;
        if (f_stair && !f_climbs[i]){
            ta += 0.5f * CASTLE_STEP;
            tb += 0.5f * CASTLE_STEP;
        }
        MeshQuad(out,At(a,ba),At(b,bb),At(b,tb + rise),At(a,ta + rise),want,PAL_STONE);
        if (rise > 0.0f){
            vec2 ai = a - n * CASTLE_PARAPET_W;
            vec2 bi = b - n * CASTLE_PARAPET_W;
            MeshQuad(out,At(a,ta + rise),At(b,tb + rise),At(bi,tb + rise),At(ai,ta + rise),vec3(0,1,0),PAL_STONE_LIGHT);
            MeshQuad(out,At(ai,ta),At(bi,tb),At(bi,tb + rise),At(ai,ta + rise),want * -1.0f,PAL_STONE);
        }
    }
    return top_c;
}

}

void BuildCastleCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
    }
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        int shape = ZoneCastleShape(z,v);
        if (shape == ZONE_CASTLE_NONE){
            continue;
        }
        float top_c = PieceQuarter(out,w,z,quad,p,k,shape,ZoneCastleLayer(z,v),false,0.0f);
        //An arch over it, its pillars on its top.
        if (ZoneCastleOver(z,v)){
            PieceQuarter(out,w,z,quad,p,k,ZONE_CASTLE_ARCH,ZoneCastleOverLayer(z,v),true,top_c);
        }
    }
}
