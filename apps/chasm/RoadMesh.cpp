#include "RoadMesh.h"
#include "MeshBuild.h"

#include <algorithm>
#include <cmath>

#define ROAD_HALF_WIDTH     0.5f    //world units either side of the centre line: a road about a metre wide
#define ROAD_LIFT           0.05f   //above the ground's relief - over a garden's 0.04, never beside one
#define ROAD_CURVE_STEPS    6       //segments in a two-edge vertex's curve
#define ROAD_JOINT_SIDES    8       //a junction's or a dead end's round joint
#define ROAD_MAX_NEIGHBOURS 8       //a fine vertex meets at most 6 edges (Grid.cpp checks 3-6)

namespace {

vec2 Unit(const vec2& d){
    float len = d.length();
    return (len > 1e-6f) ? d / len : vec2(0.0f,0.0f);
}

/*
    What plot v's road joins, by a fine edge, each once. Their order is the plot quads' order round v,
    which is fixed, so a vertex is always drawn the same way.
      - For a road plot: its road neighbours, and the plot behind its gate if it ends at one - so the
        road runs on into the gateway.
      - For an enclosed plot behind a gate: the road plot outside it - the path through the gate,
        which goes on one plot into the garden or town and stops.
    Nothing for any other plot.
*/
int RoadNeighbours(const ChasmWorld& w, const ZoneState& z, int v, int* out){
    const GridPicker& p = *w.picker;
    bool f_road = z.ground[v] == ZONE_GROUND_ROAD;
    if (!f_road && !ZoneEnclosesGround(z.ground[v])){
        return 0;
    }
    int gate = f_road ? ZoneGateOf(w,z,v) : -1;
    int n = 0;
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        int qc = p.PlotQuadCorner(v,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        int both[2] = {q.v[(k + 1) % 4],q.v[(k + 3) % 4]};
        for (int u : both){
            bool f_joined = f_road ? (z.ground[u] == ZONE_GROUND_ROAD || u == gate)
                                   : (z.ground[u] == ZONE_GROUND_ROAD && ZoneGateOf(w,z,u) == v);
            if (!f_joined){
                continue;
            }
            bool f_seen = false;
            for (int j = 0; j < n; j++){
                f_seen = f_seen || out[j] == u;
            }
            if (!f_seen && n < ROAD_MAX_NEIGHBOURS){
                out[n++] = u;
            }
        }
    }
    return n;
}

//The quadratic Bezier a road takes through a two-edge vertex: from one midpoint to the other, the
//vertex pulling it.
vec2 Curve(const vec2& a, const vec2& c, const vec2& b, float t){
    float s = 1.0f - t;
    return a * (s * s) + c * (2.0f * s * t) + b * (t * t);
}

float SegmentDistance(const vec2& p, const vec2& a, const vec2& b){
    vec2 d = b - a;
    float len2 = d.dot(d);
    float t = (len2 > 1e-9f) ? std::max(0.0f,std::min(1.0f,(p - a).dot(d) / len2)) : 0.0f;
    return (p - (a + d * t)).length();
}

/*
    Road vertex v's centre line as segments (pairs of points): the curve for two edges, the spokes to
    the midpoints otherwise. One description, used by both the mesh and the forest's clearance, so the
    trees give way to exactly the road that is drawn.
*/
int RoadCentreLine(const ChasmWorld& w, const ZoneState& z, int v, std::vector<vec2>& segs){
    segs.clear();
    const std::vector<vec2>& pos = w.grid->fine.pos;
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = RoadNeighbours(w,z,v,nb);
    vec2 c = pos[v];
    if (n == 2){
        vec2 a = (c + pos[nb[0]]) * 0.5f;
        vec2 b = (c + pos[nb[1]]) * 0.5f;
        vec2 prev = a;
        for (int i = 1; i <= ROAD_CURVE_STEPS; i++){
            vec2 q = Curve(a,c,b,(float)i / ROAD_CURVE_STEPS);
            segs.push_back(prev);
            segs.push_back(q);
            prev = q;
        }
    }else{
        for (int i = 0; i < n; i++){
            segs.push_back(c);
            segs.push_back((c + pos[nb[i]]) * 0.5f);
        }
        if (n == 0){
            segs.push_back(c);      //a lone road plot: a point, drawn as its joint
            segs.push_back(c);
        }
    }
    return n;
}

//Who draws road vertex v: the cell that is its first plot quad.
int RoadVertexOwner(const ChasmWorld& w, int v){
    return w.picker->PlotQuadCorner(v,0) / 4;
}

}

void BuildRoadCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    const vec3 up(0.0f,1.0f,0.0f);
    //Plain earth, like town ground: the road reads by its shape, and the palette has no spare column.
    const int column = PAL_PATH;
    const float h = ROAD_HALF_WIDTH;
    std::vector<vec2> segs;

    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        bool f_road = z.ground[v] == ZONE_GROUND_ROAD;
        if ((!f_road && !ZoneEnclosesGround(z.ground[v])) || RoadVertexOwner(w,v) != fine_quad){
            continue;
        }
        //A road plot is flat all round (Zones.h), so the whole road here is on v's level.
        float level = terrain_levels[w.terrain->level[v]].height;
        auto at = [level](const vec2& q){
            return vec3(q.x,TerrainGroundHeight(q,level) + ROAD_LIFT,q.y);
        };
        int n = RoadCentreLine(w,z,v,segs);
        if (!f_road && n == 0){
            continue;   //enclosed ground with no gate onto it: no path to draw
        }
        const vec2 c = g.fine.pos[v];
        if (n == 2){
            /*
                Along the curve, each cross-section square to the curve's own direction - which at its
                two ends IS the edge's direction, since a quadratic Bezier leaves along its first leg
                and arrives along its last. So the ends match the straight pieces a neighbour draws.
            */
            size_t steps = segs.size() / 2;
            std::vector<vec2> centre;
            centre.push_back(segs[0]);
            for (size_t i = 0; i < steps; i++){
                centre.push_back(segs[i * 2 + 1]);
            }
            vec2 a = centre.front(), b = centre.back();
            std::vector<vec2> left(centre.size()), right(centre.size());
            for (size_t i = 0; i < centre.size(); i++){
                float t = (float)i / (float)(centre.size() - 1);
                //The curve's derivative: 2(1-t)(c-a) + 2t(b-c).
                vec2 d = Unit((c - a) * (1.0f - t) + (b - c) * t);
                vec2 side(-d.y,d.x);
                left[i] = centre[i] + side * h;
                right[i] = centre[i] - side * h;
            }
            for (size_t i = 0; i + 1 < centre.size(); i++){
                MeshQuad(out,at(left[i]),at(left[i + 1]),at(right[i + 1]),at(right[i]),up,column);
            }
            continue;
        }
        //A spoke to each midpoint, square-ended there.
        for (size_t i = 0; i + 1 < segs.size(); i += 2){
            vec2 d = Unit(segs[i + 1] - segs[i]);
            if (d.length() < 0.5f){
                continue;
            }
            vec2 side(-d.y,d.x);
            MeshQuad(out,at(segs[i] + side * h),at(segs[i + 1] + side * h),at(segs[i + 1] - side * h),
                     at(segs[i] - side * h),up,column);
        }
        //The joint: a round patch over the spokes' inner ends. Drawn over them, not cut against them -
        //it is the same colour on the same ground, so the overlap cannot show.
        for (int i = 0; i < ROAD_JOINT_SIDES; i++){
            float a0 = 6.2831853f * i / ROAD_JOINT_SIDES;
            float a1 = 6.2831853f * (i + 1) / ROAD_JOINT_SIDES;
            vec2 p0 = c + vec2(cosf(a0),sinf(a0)) * h;
            vec2 p1 = c + vec2(cosf(a1),sinf(a1)) * h;
            MeshTri(out,at(c),at(p0),at(p1),up,column);
        }
    }
}

bool RoadNear(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& p, float clearance){
    if (z.ground[plot] == ZONE_GROUND_ROAD){
        return true;
    }
    //Every road vertex whose drawing can reach plot `plot`: its own corners' neighbours, one cell out.
    const GridPicker& picker = *w.picker;
    std::vector<vec2> segs;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        const GridQuad& q = w.grid->fine.quads[picker.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = q.v[k];
            if (z.ground[v] != ZONE_GROUND_ROAD){
                continue;
            }
            RoadCentreLine(w,z,v,segs);
            for (size_t s = 0; s + 1 < segs.size(); s += 2){
                if (SegmentDistance(p,segs[s],segs[s + 1]) < clearance){
                    return true;
                }
            }
        }
    }
    return false;
}

int RoadBridgePlot(const ChasmWorld& w, int a, int b, const vec2& toward){
    const GridPicker& p = *w.picker;
    if (a < 0 || b < 0 || a == b){
        return -1;
    }
    for (int i = 0; i < p.PlotQuadCount(a); i++){
        int qc = p.PlotQuadCorner(a,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        if (q.v[(k + 1) % 4] == b || q.v[(k + 3) % 4] == b){
            return -1;      //already joined by an edge
        }
    }
    for (int i = 0; i < p.PlotQuadCount(a); i++){
        int qc = p.PlotQuadCorner(a,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        if (q.v[(k + 2) % 4] != b){
            continue;
        }
        int c0 = q.v[(k + 1) % 4];
        int c1 = q.v[(k + 3) % 4];
        const std::vector<vec2>& pos = w.grid->fine.pos;
        return ((pos[c0] - toward).length() <= (pos[c1] - toward).length()) ? c0 : c1;
    }
    return -1;
}
