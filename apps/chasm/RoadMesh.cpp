#include "RoadMesh.h"
#include "MeshBuild.h"

#include <algorithm>
#include <cmath>

#define ROAD_HALF_WIDTH     0.5f    //world units either side of the centre line: a road about a metre wide
#define ROAD_LIFT           0.05f   //above the ground's relief - over a garden's 0.04, never beside one
#define ROAD_CURVE_STEPS    6       //segments in a two-edge vertex's curve
#define ROAD_JOINT_SIDES    8       //a junction's or a dead end's round joint
#define ROAD_MAX_NEIGHBOURS 8       //a fine vertex meets at most 6 edges (Grid.cpp checks 3-6)
//A bridge's plots (bridge_plan.md): the road's own shape, as a timber deck on piles.
#define DECK_THICK          0.12f
#define DECK_HALF_WIDTH     0.52f
#define DECK_RAIL_Y         0.42f   //the rail over the deck
#define DECK_RAIL           0.06f
#define DECK_POST_EVERY     3       //a rail post every this many points along a curve
#define DECK_PILE           0.12f
#define DECK_PILE_FOOT      1.6f    //below the water's surface: out of sight

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
    bool f_road = ZoneRoadStands(z,v);
    if (!f_road && !ZoneEnclosesGround(z.ground[v])){
        return 0;
    }
    int gate = (z.ground[v] == ZONE_GROUND_ROAD) ? ZoneGateOf(w,z,v) : -1;
    int n = 0;
    for (int i = 0; i < p.PlotQuadCount(v); i++){
        int qc = p.PlotQuadCorner(v,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        int both[2] = {q.v[(k + 1) % 4],q.v[(k + 3) % 4]};
        for (int u : both){
            bool f_joined = f_road ? (ZoneRoadStands(z,u) || u == gate)
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

//A box from a to b (3D, a bar of square section `thick`): a rail, a post, a pile.
void Bar(std::vector<vertex>& out, const vec3& a, const vec3& b, float thick, int column){
    vec3 d = b - a;
    float l = d.length();
    if (l < 1e-5f){
        return;
    }
    d = d * (1.0f / l);
    //Two axes across it: one level where it can be, the other square to both.
    vec3 s = (std::fabs(d.y) > 0.9f) ? vec3(1.0f,0.0f,0.0f) : vec3(-d.z,0.0f,d.x);
    s = s * (1.0f / std::max(1e-5f,s.length()));
    vec3 u(d.y * s.z - d.z * s.y,d.z * s.x - d.x * s.z,d.x * s.y - d.y * s.x);
    float h = thick * 0.5f;
    vec3 c[4] = {(s + u) * h,(s - u) * h,(s * -1.0f - u) * h,(s * -1.0f + u) * h};
    for (int i = 0; i < 4; i++){
        int j = (i + 1) % 4;
        vec3 n = c[i] + c[j];
        MeshQuad(out,a + c[i],b + c[i],b + c[j],a + c[j],n,column);
    }
    MeshQuad(out,a + c[0],a + c[3],a + c[2],a + c[1],d * -1.0f,column);
    MeshQuad(out,b + c[0],b + c[1],b + c[2],b + c[3],d,column);
}

/*
    A deck along a centre line (bridge_plan.md): its top, its two edges down to its underside, a rail on
    posts along each side. `centre` is the line, `ys` the deck's height at each point - the deck's own
    height on the bridge, ramping down where it meets a road on land - `dirs` the line's direction there.
*/
void BuildDeck(std::vector<vertex>& out, const std::vector<vec2>& centre, const std::vector<float>& ys,
               const std::vector<vec2>& dirs){
    const vec3 up(0.0f,1.0f,0.0f);
    std::vector<vec3> left(centre.size()), right(centre.size());
    for (size_t i = 0; i < centre.size(); i++){
        vec2 side(-dirs[i].y,dirs[i].x);
        vec2 l = centre[i] + side * DECK_HALF_WIDTH;
        vec2 r = centre[i] - side * DECK_HALF_WIDTH;
        left[i] = vec3(l.x,ys[i],l.y);
        right[i] = vec3(r.x,ys[i],r.y);
    }
    const vec3 down(0.0f,-DECK_THICK,0.0f);
    const vec3 rail(0.0f,DECK_RAIL_Y,0.0f);
    for (size_t i = 0; i + 1 < centre.size(); i++){
        MeshQuad(out,left[i],left[i + 1],right[i + 1],right[i],up,PAL_TIMBER);
        vec3 out_l = left[i] - right[i];
        MeshQuad(out,left[i] + down,left[i + 1] + down,left[i + 1],left[i],out_l,PAL_TIMBER);
        MeshQuad(out,right[i],right[i + 1],right[i + 1] + down,right[i] + down,out_l * -1.0f,PAL_TIMBER);
        MeshQuad(out,left[i] + down,right[i] + down,right[i + 1] + down,left[i + 1] + down,up * -1.0f,PAL_TIMBER);
        Bar(out,left[i] + rail,left[i + 1] + rail,DECK_RAIL,PAL_BARK);
        Bar(out,right[i] + rail,right[i + 1] + rail,DECK_RAIL,PAL_BARK);
    }
    for (size_t i = 0; i < centre.size(); i += DECK_POST_EVERY){
        Bar(out,left[i],left[i] + rail,DECK_RAIL * 1.2f,PAL_BARK);
        Bar(out,right[i],right[i] + rail,DECK_RAIL * 1.2f,PAL_BARK);
    }
}

//Who draws road vertex v: the cell that is its first plot quad.
int RoadVertexOwner(const ChasmWorld& w, int v){
    return w.picker->PlotQuadCorner(v,0) / 4;
}

#define STAKE_HEIGHT        0.60f
#define STAKE_THICK         0.07f
#define STAKE_SINK          0.10f   //into the ground
#define STAKE_TIE           0.45f   //where the cord is tied, up the stake
#define STAKE_CORD          0.025f
#define STAKE_RIBBON        0.16f   //a ribbon of colour at the top, so a line of stakes is seen

/*
    A planned road plot (line_works_plan.md): a stake at its vertex with a ribbon on top, and a cord from
    it out to the midpoint of each edge the road will take - toward a road plot beside it, planned or
    laid, or a standing bridge. Between two stakes each draws its half, meeting halfway up; toward a laid
    road the cord comes down to the ground at the midpoint, where that road's surface begins.
*/
void BuildRoadStake(const ChasmWorld& w, const ZoneState& z, int v, std::vector<vertex>& out){
    const std::vector<vec2>& pos = w.grid->fine.pos;
    const vec2 c = pos[v];
    float y0 = w.terrain->GroundHeight(c,w.terrain->Height(v));
    Bar(out,vec3(c.x,y0 - STAKE_SINK,c.y),vec3(c.x,y0 + STAKE_HEIGHT,c.y),STAKE_THICK,PAL_TIMBER);
    Bar(out,vec3(c.x,y0 + STAKE_HEIGHT - STAKE_RIBBON,c.y),vec3(c.x,y0 + STAKE_HEIGHT + 0.01f,c.y),
        STAKE_THICK * 1.6f,PAL_ACCENT);
    vec3 tie(c.x,y0 + STAKE_TIE,c.y);
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = ZonePlotNeighbours(w,v,nb,ROAD_MAX_NEIGHBOURS);
    for (int i = 0; i < n; i++){
        int u = nb[i];
        bool f_planned = ZoneRoadPlanned(z,u);
        if (!f_planned && !ZoneRoadStands(z,u)){
            continue;
        }
        vec2 mid = (c + pos[u]) * 0.5f;
        float ground_mid = w.terrain->GroundHeight(mid,w.terrain->Height(v));
        float y_mid = f_planned ? (tie.y + w.terrain->GroundHeight(pos[u],w.terrain->Height(u)) + STAKE_TIE) * 0.5f
                                : ground_mid + ROAD_LIFT;
        Bar(out,tie,vec3(mid.x,y_mid,mid.y),STAKE_CORD,PAL_WALL);
    }
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
        if (ZoneRoadPlanned(z,v)){
            if (RoadVertexOwner(w,v) == fine_quad){
                BuildRoadStake(w,z,v,out);
            }
            continue;
        }
        bool f_road = ZoneRoadStands(z,v);
        if ((!f_road && !ZoneEnclosesGround(z.ground[v])) || RoadVertexOwner(w,v) != fine_quad){
            continue;
        }
        /*
            A BRIDGE's plot: the road's own centre line, as a deck at the bridge's height - ramping down
            to the road where it meets one on land - with rails, and piles where it stands in the water.
        */
        if (ZoneBridgeWalkable(z,v)){
            int nb[ROAD_MAX_NEIGHBOURS];
            int n = RoadNeighbours(w,z,v,nb);
            RoadCentreLine(w,z,v,segs);
            const vec2 c = g.fine.pos[v];
            const float deck = terrain_levels[w.terrain->level[v]].height + ZONE_BRIDGE_DECK;
            //The height at a point on the way to neighbour u: the deck, or down to a land road's surface
            //over the half-edge to it.
            auto height_toward = [&](int u, float f){
                if (ZoneBridgeWalkable(z,u)){
                    return deck;
                }
                vec2 mid = (c + g.fine.pos[u]) * 0.5f;
                float road = w.terrain->GroundHeight(mid,w.terrain->Height(v)) + ROAD_LIFT;
                return deck + (road - deck) * f;
            };
            if (n == 2){
                size_t steps = segs.size() / 2;
                std::vector<vec2> centre, dirs;
                std::vector<float> ys;
                centre.push_back(segs[0]);
                for (size_t i = 0; i < steps; i++){
                    centre.push_back(segs[i * 2 + 1]);
                }
                vec2 a = centre.front(), b = centre.back();
                for (size_t i = 0; i < centre.size(); i++){
                    float t = (float)i / (float)(centre.size() - 1);
                    dirs.push_back(Unit((c - a) * (1.0f - t) + (b - c) * t));
                    //The first half leads to nb[0]'s midpoint, the second to nb[1]'s.
                    ys.push_back(t < 0.5f ? height_toward(nb[0],1.0f - 2.0f * t) : height_toward(nb[1],2.0f * t - 1.0f));
                }
                BuildDeck(out,centre,ys,dirs);
            }else{
                //A spoke to each midpoint, and a square of deck over the joint.
                for (size_t i = 0; i + 1 < segs.size(); i += 2){
                    vec2 d = Unit(segs[i + 1] - segs[i]);
                    if (d.length() < 0.5f){
                        continue;
                    }
                    int u = nb[i / 2];
                    std::vector<vec2> centre = {segs[i],segs[i + 1]};
                    std::vector<float> ys = {deck,height_toward(u,1.0f)};
                    std::vector<vec2> dirs = {d,d};
                    BuildDeck(out,centre,ys,dirs);
                }
                vec3 p0(c.x - DECK_HALF_WIDTH,deck,c.y - DECK_HALF_WIDTH), p1(c.x + DECK_HALF_WIDTH,deck,c.y - DECK_HALF_WIDTH);
                vec3 p2(c.x + DECK_HALF_WIDTH,deck,c.y + DECK_HALF_WIDTH), p3(c.x - DECK_HALF_WIDTH,deck,c.y + DECK_HALF_WIDTH);
                MeshQuad(out,p0,p3,p2,p1,up,PAL_TIMBER);
            }
            //Piles under it in the water, across the way it runs.
            if (w.terrain->wet[v]){
                vec2 run = (n >= 2) ? Unit(g.fine.pos[nb[0]] - g.fine.pos[nb[n - 1]]) : vec2(1.0f,0.0f);
                if (run.length() < 0.5f){
                    run = vec2(1.0f,0.0f);
                }
                vec2 side(-run.y,run.x);
                float foot = terrain_levels[w.terrain->level[v]].height + TERRAIN_WATER_Y - DECK_PILE_FOOT;
                for (int sgn = -1; sgn <= 1; sgn += 2){
                    vec2 q = c + side * ((float)sgn * DECK_HALF_WIDTH * 0.8f);
                    Bar(out,vec3(q.x,foot,q.y),vec3(q.x,deck - DECK_THICK,q.y),DECK_PILE,PAL_BARK_DARK);
                }
            }
            continue;
        }
        //A road plot is flat all round (Zones.h), so the whole road here is on v's level.
        float level = w.terrain->Height(v);
        const Terrain& terrain = *w.terrain;
        auto at = [level,&terrain](const vec2& q){
            return vec3(q.x,terrain.GroundHeight(q,level) + ROAD_LIFT,q.y);
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

bool RoadNear(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& p, float clearance, bool f_line_only){
    if (ZoneRoadLaid(z,plot) && !f_line_only){
        return true;
    }
    //Every road vertex whose drawing can reach plot `plot`: its own corners' neighbours, one cell out.
    const GridPicker& picker = *w.picker;
    std::vector<vec2> segs;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        const GridQuad& q = w.grid->fine.quads[picker.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = q.v[k];
            if (!ZoneRoadLaid(z,v)){
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
