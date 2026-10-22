#include "RoadMesh.h"
#include "BuildingMesh.h"
#include "ZoneMesh.h"
#include "MeshBuild.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#define ROAD_HALF_WIDTH     0.5f    //world units either side of the centre line: a road about a metre wide
#define ROAD_LIFT           0.05f   //above the ground's relief - over a garden's 0.04, never beside one
#define ROAD_CURVE_STEPS    6       //segments in a two-edge vertex's curve
#define ROAD_JOINT_SIDES    8       //a junction's or a dead end's round joint
#define ROAD_MAX_NEIGHBOURS 8       //a fine vertex meets at most 6 edges (Grid.cpp checks 3-6)
#define ROAD_FILL_PIECES    4       //a cell road all round, cut this many ways each way to lie on the relief
//The STONES along a road's edges (the user's, 2026-10-18): a place for one every step, a few of them taken.
#define ROAD_STONE_STEP     0.45f
#define ROAD_STONE_CHANCE   18      //percent of the places
#define ROAD_STONE_MIN      0.06f   //a stone's radius, by its hash, between these
#define ROAD_STONE_MAX      0.12f
#define ROAD_STONE_HEIGHT   0.06f   //its top over the road, at most
#define ROAD_STONE_BEYOND   0.22f   //how far past the edge the ground is looked at: road there too, no border
//The WORN MIDDLE (the user's, 2026-10-18): a lighter band down a road's centre, where the feet go - a road between
//grass only: along or inside a closed area (RoadCellFilled) there is no one line to wear.
#define ROAD_WORN_HALF      0.20f
#define ROAD_WORN_LIFT      0.006f  //over the road
//GRASS creeping in at the edges: a tuft at a few of the places along them, its blades in the grass props' colours
//(PAL_LEAF, PAL_LEAF_LIGHT), so it turns with the seasons as they do.
#define ROAD_GRASS_STEP     0.38f
#define ROAD_GRASS_CHANCE   26      //percent of the places
#define ROAD_GRASS_BLADES   6
#define ROAD_GRASS_HEIGHT   0.24f   //a blade's, at most
#define ROAD_GRASS_BLADE_W  0.03f   //half a blade's width at its foot
//COBBLES: a filled cell beside a building - a square in the town - laid with stones, in a little grid of its own.
#define ROAD_COBBLE_LATTICE 3       //the lattice a cell's stones grow from (CobbleGrid), so many squares a side: ~44 stones
#define ROAD_COBBLE_SQUARES 40      //percent of its squares that may be merged back whole; the rest only across to a neighbour
#define ROAD_COBBLE_RELAX   25      //passes toward squares, as the map's own (GridSquareFit)
#define ROAD_COBBLE_GAP     0.14f   //of a stone's size, left round it: the road shows in the joints
#define ROAD_COBBLE_RIM     0.07f   //the road left showing round a square's cobbles, inside the road's own edge (CobblePatch)
#define ROAD_COBBLE_SAMPLES 12      //points along a side of a cell's patch: a multiple of the lattice's 2 x 3 edge cuts
#define ROAD_COBBLE_BLEND   0.3f    //of a side, at each end, over which it turns from square out to the corner's way
#define ROAD_COBBLE_LIFT    0.012f  //over the road, about
#define ROAD_TOWN_REACH     400     //the most cells of a closed area looked through for a building beside it
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
    trees give way to exactly the road that is drawn - and a wall's line is the same shape (LineThrough),
    through the neighbours `nb` it joins.
*/
void LineThrough(const ChasmWorld& w, int v, const int* nb, int n, std::vector<vec2>& segs){
    segs.clear();
    const std::vector<vec2>& pos = w.grid->fine.pos;
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
}

int RoadCentreLine(const ChasmWorld& w, const ZoneState& z, int v, std::vector<vec2>& segs){
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = RoadNeighbours(w,z,v,nb);
    LineThrough(w,v,nb,n,segs);
    return n;
}

//A plot a wall's line runs through: a wall plot, planned or standing, or a gate - a road plot it crosses - or a
//guard tower standing in it (defence_plan.md), which the palisade ends against.
bool WallLine(const ZoneState& z, int v){
    return z.ground[v] == ZONE_GROUND_WALL || z.gate[v] || z.KindOf(v) == ZONE_KIND_TOWER;
}

//Every plot beside v, by a fine edge, that a wall's line runs through - in the plot quads' fixed order.
int WallNeighboursAll(const ChasmWorld& w, const ZoneState& z, int v, int* out){
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = ZonePlotNeighbours(w,v,nb,ROAD_MAX_NEIGHBOURS);
    int m = 0;
    for (int i = 0; i < n; i++){
        if (WallLine(z,nb[i])){
            out[m++] = nb[i];
        }
    }
    return m;
}

/*
    Where a wall's line is on all four corners of one cell - two lines meeting, a stroke crossing itself -
    each corner is a neighbour of two others, and a palisade drawn to all of them would ring the cell. The
    wall needs only the L round it: of the corners joined to nothing outside the cell (two neighbours on
    the line, both the cell's), the highest index is SPARE, and its two edges in the cell are not drawn -
    it stands as a post. Decided by the cell alone, so the plots either end of an edge agree. A cell with
    no such corner - a wall two plots thick - keeps every edge.
*/
bool WallEdgeDropped(const ChasmWorld& w, const ZoneState& z, int a, int b){
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(a); i++){
        int qc = p.PlotQuadCorner(a,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        if (q.v[(k + 1) % 4] != b && q.v[(k + 3) % 4] != b){
            continue;       //a cell at a, but not on the edge a - b
        }
        if (!WallLine(z,q.v[0]) || !WallLine(z,q.v[1]) || !WallLine(z,q.v[2]) || !WallLine(z,q.v[3])){
            continue;
        }
        int spare = -1;
        for (int j = 0; j < 4; j++){
            int nb[ROAD_MAX_NEIGHBOURS];
            int n = WallNeighboursAll(w,z,q.v[j],nb);
            bool f_inside = (n == 2);
            for (int t = 0; t < n && f_inside; t++){
                f_inside = nb[t] == q.v[(j + 1) % 4] || nb[t] == q.v[(j + 3) % 4];
            }
            if (f_inside && q.v[j] > spare){
                spare = q.v[j];
            }
        }
        if (spare == a || spare == b){
            return true;
        }
    }
    return false;
}

/*
    What wall plot v's palisade joins, by a fine edge, each once in the plot quads' fixed order: the wall
    plots beside it, planned or standing, and the gates - the road plots its wall runs through - less the
    edges of a walled cell's spare corner (WallEdgeDropped). For a gate plot the same: the walls and gates
    beside it, which its crossbeam spans.
*/
int WallNeighbours(const ChasmWorld& w, const ZoneState& z, int v, int* out){
    int all[ROAD_MAX_NEIGHBOURS];
    int n = WallNeighboursAll(w,z,v,all);
    int m = 0;
    for (int i = 0; i < n; i++){
        if (!WallEdgeDropped(w,z,v,all[i])){
            out[m++] = all[i];
        }
    }
    return m;
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
    float y0 = ZoneGroundHeight(w,z,v,c);
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
        float ground_mid = ZoneGroundHeight(w,z,v,mid);
        float y_mid = f_planned ? (tie.y + ZoneGroundHeight(w,z,u,pos[u]) + STAKE_TIE) * 0.5f
                                : ground_mid + ROAD_LIFT;
        Bar(out,tie,vec3(mid.x,y_mid,mid.y),STAKE_CORD,PAL_WALL);
    }
}

//--- Walls (line_works_plan.md, step 3) ---

#define WALL_LOG_HEIGHT     1.30f   //over the ground: one and a half villagers (0.86) - the user's, 2026-10-08
#define WALL_LOG_W          0.17f
#define WALL_LOG_SPACING    0.18f   //log to log along the line: side by side, a chink of light here and there
#define WALL_LOG_TIP        0.24f   //the sharpened top
#define WALL_LOG_SINK       0.15f   //into the ground, so a slope never shows a gap under one
#define WALL_LOG_JITTER     0.12f   //of height, by the log's hash: cut by hand
#define WALL_GATE_POST_W    0.26f
#define WALL_GATE_HEIGHT    1.80f   //the gate posts, and the top of the crossbeam between them
#define WALL_GATE_BEAM      0.18f
#define WALL_DOOR_HEIGHT    1.42f   //a gate's door's logs (defence_plan.md): over the palisade's, under the beam
#define WALL_DOOR_LOG_W     0.15f
#define WALL_DOOR_BATTEN    0.21f   //the battens across a leaf: wider than its logs, so they show either side
#define WALL_DOOR_GAP       0.03f   //between the two leaves of a shut door
#define WALL_STAKE_HEIGHT   0.95f   //a planned wall's stake: taller than a road's, so the two read apart
#define WALL_STAKE_TIE      0.70f
//HARMED by zombies (zombies_plan.md, step 4d), each log by its own hash. Radians, and shares of a plot's strength.
#define WALL_LEAN_MOST      0.45f   //a log leans at most this far, just before the plot breaks...
#define WALL_FALL_FROM      0.35f   //...and logs come down from this much harm on...
#define WALL_FALL_SHARE     0.75f   //...this share more of them for each share more of it
#define WALL_DOWN           1.48f   //a fallen log: all but flat
#define WALL_STUMPS         0.30f   //of a broken plot's logs, these stand on snapped off...
#define WALL_STUMP_LOW      0.20f   //...between these heights
#define WALL_STUMP_HIGH     0.60f
#define GATE_GIVE           0.24f   //a shut gate's door, just before it breaks: its leaves swung in this far...
#define GATE_SAG            0.07f   //...and their far ends dropped this far
#define GATE_HANG_SWING     0.70f   //a broken gate's leaf hangs swung in this far, and up to GATE_HANG_SWING_MORE more...
#define GATE_HANG_SWING_MORE 0.90f
#define GATE_HANG_SAG       0.22f   //...dropped at its far end this far, and up to GATE_HANG_SAG_MORE more
#define GATE_HANG_SAG_MORE  0.18f

//A sharpened log standing at p on ground y0: a square post and a four-sided tip.
void Log(std::vector<vertex>& out, const vec2& p, float y0, float height, float width, int column){
    float body = height - WALL_LOG_TIP;
    Bar(out,vec3(p.x,y0 - WALL_LOG_SINK,p.y),vec3(p.x,y0 + body,p.y),width,column);
    float h = width * 0.5f;
    vec3 apex(p.x,y0 + height,p.y);
    vec3 c[4] = {vec3(p.x + h,y0 + body,p.y - h),vec3(p.x + h,y0 + body,p.y + h),
                 vec3(p.x - h,y0 + body,p.y + h),vec3(p.x - h,y0 + body,p.y - h)};
    for (int i = 0; i < 4; i++){
        vec3 mid = (c[i] + c[(i + 1) % 4]) * 0.5f;
        MeshTri(out,c[i],c[(i + 1) % 4],apex,vec3(mid.x - p.x,0.3f,mid.z - p.y),column);
    }
}

//A hash's bits as a share, 0 to 1: three of them from one hash, by `which`.
float HashShare(uint32_t hash, int which){
    return (float)((hash >> (which * 10)) % 1000) / 1000.0f;
}

//`v` on the ground plane turned by `a` radians about up, as MeshTurn turns it.
vec2 TurnOnGround(const vec2& v, float a){
    float c = std::cos(a);
    float s = std::sin(a);
    return vec2(v.x * c + v.y * s,-v.x * s + v.y * c);
}

/*
    A LOG OF A HARMED PALISADE (zombies_plan.md, step 4d), standing at p on ground y0 - or not: `f` is the plot's harm,
    0 whole to 1 broken, and `away` the way the blows push, unit, on the ground. Each log by its own hash: it leans that
    way by its share of the harm, and past its own point it is down on the ground, roughly that way. Once the plot is
    broken all are down but a few, snapped off.
*/
void HarmedLog(std::vector<vertex>& out, const vec2& p, float y0, float height, float width, int column,
               const vec2& away, float f, uint32_t hash){
    float lean = HashShare(hash,0);
    float fall = HashShare(hash,1);
    float yaw = HashShare(hash,2) - 0.5f;
    const vec3 up(0.0f,1.0f,0.0f);
    size_t first = out.size();
    if (f >= 1.0f && fall < WALL_STUMPS){
        float top = WALL_STUMP_LOW + (WALL_STUMP_HIGH - WALL_STUMP_LOW) * lean;
        Bar(out,vec3(p.x,y0 - WALL_LOG_SINK,p.y),vec3(p.x,y0 + top,p.y),width,column);
        vec2 to = TurnOnGround(away,yaw * 2.0f);
        MeshTurn(out,first,vec3(p.x,y0,p.y),up.cross(vec3(to.x,0.0f,to.y)),0.25f * lean);
        return;
    }
    bool f_down = f >= 1.0f || fall < (f - WALL_FALL_FROM) * WALL_FALL_SHARE;
    if (f_down){
        //Laid along the ground, half its width up: tipped over about its foot.
        float lift = width * 0.5f;
        Log(out,p,y0 + lift,height,width,column);
        vec2 to = TurnOnGround(away,yaw * 1.4f);
        MeshTurn(out,first,vec3(p.x,y0 + lift,p.y),up.cross(vec3(to.x,0.0f,to.y)),WALL_DOWN + 0.08f * lean);
        return;
    }
    Log(out,p,y0,height,width,column);
    if (f > 0.0f){
        vec2 to = TurnOnGround(away,yaw * 0.6f);
        MeshTurn(out,first,vec3(p.x,y0,p.y),up.cross(vec3(to.x,0.0f,to.y)),WALL_LEAN_MOST * f * (0.3f + 0.7f * lean));
    }
}

/*
    A planned wall plot: a tall stake at its vertex with a ribbon, and a cord out to each edge the wall
    will take - to the stakes of planned plots beside it, meeting halfway, and to the midpoint toward one
    standing or a gate.
*/
void BuildWallStake(const ChasmWorld& w, const ZoneState& z, int v, std::vector<vertex>& out){
    const std::vector<vec2>& pos = w.grid->fine.pos;
    const vec2 c = pos[v];
    float y0 = ZoneGroundHeight(w,z,v,c);
    Bar(out,vec3(c.x,y0 - STAKE_SINK,c.y),vec3(c.x,y0 + WALL_STAKE_HEIGHT,c.y),STAKE_THICK * 1.3f,PAL_TIMBER);
    Bar(out,vec3(c.x,y0 + WALL_STAKE_HEIGHT - STAKE_RIBBON,c.y),vec3(c.x,y0 + WALL_STAKE_HEIGHT + 0.01f,c.y),
        STAKE_THICK * 2.0f,PAL_ACCENT);
    vec3 tie(c.x,y0 + WALL_STAKE_TIE,c.y);
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = WallNeighbours(w,z,v,nb);
    for (int i = 0; i < n; i++){
        int u = nb[i];
        vec2 mid = (c + pos[u]) * 0.5f;
        float y_mid = ZoneWallPlanned(z,u) ? (tie.y + ZoneGroundHeight(w,z,u,pos[u]) + WALL_STAKE_TIE) * 0.5f
                                           : ZoneGroundHeight(w,z,v,mid) + WALL_STAKE_TIE;
        Bar(out,tie,vec3(mid.x,y_mid,mid.y),STAKE_CORD,PAL_WALL);
    }
}

/*
    A standing wall plot: sharpened logs side by side along its line (LineThrough - the curve through the
    midpoints of its two edges, or a spoke out to each). Each plot draws its own half of every edge, its
    logs at the middles of equal steps along each piece, so two plots' logs meet at a midpoint without a
    doubled one. A thicker log at the vertex where the line does not run straight through. Toward a gate
    the piece ends in a tall post at the midpoint - the jamb - and the crossbeam is the gate's (BuildGate).
    HARMED (`harm`, step 4d), its logs lean and drop away from the blows (HarmedLog); BROKEN, a wall site again, it is
    drawn here too, as what is left of it. Its jambs stand either way.
*/
void BuildPalisade(const ChasmWorld& w, const ZoneState& z, int v, std::vector<vertex>& out,
                   const ZoneHarmMark* harm = NULL){
    const std::vector<vec2>& pos = w.grid->fine.pos;
    auto ground = [&](const vec2& p){
        return ZoneGroundHeight(w,z,v,p);     //a terrace's top, if it stands on one (terraces_plan.md)
    };
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = WallNeighbours(w,z,v,nb);
    //Which way the blows push a log standing at `at` in a line running across `side`: to the side of the wall away from
    //where they came from - by the plot's chord, the line between its neighbours in the wall, since the zombies stand
    //off its point and may stand along a bend of it - or with that not known, one way for the whole plot by its hash.
    const float f = harm ? std::min(1.0f,(float)harm->step / ZONE_HARM_STEPS) : 0.0f;
    const float turn = HashShare(MeshHash((uint32_t)v,0xFA11u),0) * 6.2831853f;
    vec2 push(std::cos(turn),std::sin(turn));
    if (harm && harm->f_from){
        push = pos[v] - harm->from;
        if (n == 2){
            vec2 d = pos[nb[1]] - pos[nb[0]];
            vec2 across(-d.y,d.x);
            push = (across.dot(push) < 0.0f) ? -across : across;
        }
    }
    auto away = [&](vec2 side){
        if (side.length() < 1e-6f){
            side = push;
        }
        if (side.length() < 1e-6f){
            return vec2(1.0f,0.0f);
        }
        side.normalize();
        return (side.dot(push) < 0.0f) ? -side : side;
    };
    std::vector<vec2> segs;
    LineThrough(w,v,nb,n,segs);
    //The pieces as polylines, with the neighbour at each end (-1 at the vertex).
    struct Piece{
        std::vector<vec2> pts;
        int at_start, at_end;
    };
    std::vector<Piece> pieces;
    if (n == 2){
        Piece p{{segs[0]},nb[0],nb[1]};
        for (size_t i = 1; i < segs.size(); i += 2){
            p.pts.push_back(segs[i]);
        }
        pieces.push_back(p);
    }else{
        for (int i = 0; i < n; i++){
            pieces.push_back(Piece{{segs[i * 2],segs[i * 2 + 1]},-1,nb[i]});
        }
        HarmedLog(out,pos[v],ground(pos[v]),WALL_LOG_HEIGHT + WALL_LOG_JITTER,WALL_LOG_W * 1.4f,PAL_BARK,
                  away(vec2()),f,MeshHash((uint32_t)v,0xC0u));
    }
    /*
        Against a GUARD TOWER (defence_plan.md) the piece runs on past the midpoint to the tower's wall, which stands
        well back from it: the tower draws no palisade of its own.
    */
    auto on_to_tower = [&](std::vector<vec2>& pts, bool f_front, int tower){
        if (tower < 0 || z.KindOf(tower) != ZONE_KIND_TOWER || pts.size() < 2){
            return;
        }
        vec2 end = f_front ? pts.front() : pts.back();
        vec2 d = end - (f_front ? pts[1] : pts[pts.size() - 2]);
        float l = d.length();
        float reach = (pos[tower] - end).length() - BuildingTowerReach(w,tower,end - pos[tower]) + WALL_LOG_W;
        if (l < 1e-5f || reach <= 0.0f){
            return;
        }
        vec2 past = end + d * (reach / l);
        if (f_front){
            pts.insert(pts.begin(),past);
        }else{
            pts.push_back(past);
        }
    };
    for (Piece& p : pieces){
        on_to_tower(p.pts,true,p.at_start);
        on_to_tower(p.pts,false,p.at_end);
    }
    for (size_t pi = 0; pi < pieces.size(); pi++){
        const Piece& p = pieces[pi];
        std::vector<float> along(1,0.0f);
        for (size_t i = 1; i < p.pts.size(); i++){
            along.push_back(along.back() + (p.pts[i] - p.pts[i - 1]).length());
        }
        float len = along.back();
        //A jamb where the piece meets a gate: the logs stop short of it.
        bool f_gate_start = p.at_start >= 0 && z.gate[p.at_start];
        bool f_gate_end = p.at_end >= 0 && z.gate[p.at_end];
        float from = f_gate_start ? WALL_GATE_POST_W : 0.0f;
        float to = len - (f_gate_end ? WALL_GATE_POST_W : 0.0f);
        if (f_gate_start){
            Log(out,p.pts.front(),ground(p.pts.front()),WALL_GATE_HEIGHT,WALL_GATE_POST_W,PAL_BARK_DARK);
        }
        if (f_gate_end){
            Log(out,p.pts.back(),ground(p.pts.back()),WALL_GATE_HEIGHT,WALL_GATE_POST_W,PAL_BARK_DARK);
        }
        if (to - from < WALL_LOG_SPACING * 0.5f){
            continue;
        }
        int logs = std::max(1,(int)std::lround((to - from) / WALL_LOG_SPACING));
        size_t seg = 1;
        for (int i = 0; i < logs; i++){
            float s = from + (to - from) * ((float)i + 0.5f) / logs;
            while (seg + 1 < p.pts.size() && along[seg] < s){
                seg++;
            }
            float t = (s - along[seg - 1]) / std::max(1e-6f,along[seg] - along[seg - 1]);
            vec2 d = p.pts[seg] - p.pts[seg - 1];
            vec2 at = p.pts[seg - 1] + d * t;
            float jitter = (float)(MeshHash((uint32_t)v,(uint32_t)pi,(uint32_t)i) % 100) / 100.0f;
            HarmedLog(out,at,ground(at),WALL_LOG_HEIGHT + jitter * WALL_LOG_JITTER,WALL_LOG_W,PAL_BARK,
                      away(vec2(-d.y,d.x)),f,MeshHash((uint32_t)v,(uint32_t)pi + 64u,(uint32_t)i));
        }
    }
}

//A gate door's leaf: sharpened logs from `hinge` to `end` on the gate's ground level, and two battens across.
void DoorLeaf(const ChasmWorld& w, const ZoneState& z, const vec2& hinge, const vec2& end, int g, int leaf,
              std::vector<vertex>& out){
    float len = (end - hinge).length();
    if (len < WALL_DOOR_LOG_W){
        return;
    }
    int logs = std::max(1,(int)std::lround(len / WALL_LOG_SPACING));
    float lowest = 1e30f;
    for (int i = 0; i < logs; i++){
        vec2 at = hinge + (end - hinge) * (((float)i + 0.5f) / logs);
        float y0 = ZoneGroundHeight(w,z,g,at);
        lowest = std::min(lowest,y0);
        float jitter = (float)(MeshHash((uint32_t)g,(uint32_t)(leaf + 16),(uint32_t)i) % 100) / 100.0f;
        Log(out,at,y0,WALL_DOOR_HEIGHT + jitter * WALL_LOG_JITTER * 0.5f,WALL_DOOR_LOG_W,PAL_TIMBER);
    }
    vec2 a = hinge + (end - hinge) * (0.5f / logs);
    vec2 b = end - (end - hinge) * (0.5f / logs);
    for (float h : {0.32f,1.02f}){
        Bar(out,vec3(a.x,lowest + h,a.y),vec3(b.x,lowest + h,b.y),WALL_DOOR_BATTEN * 0.45f,PAL_BARK_DARK);
    }
}

/*
    A GATE - a road plot a wall runs through (ZoneState::gate): a crossbeam over the road from jamb to jamb,
    the posts the walls either side end in. Only once every wall beside it stands, so the beam never hangs
    from a post not there yet.
    Its DOOR (defence_plan.md, step 3): a leaf from each jamb to the gate's middle. SHUT, the leaves meet
    there across the road; OPEN, each is swung back about its jamb to lie along the road - both to the same
    side of the wall, by a fixed direction, so the two of a gate never fold opposite ways. Between the two plots
    of a gate on the slant there is no jamb: shut, the leaves run on to their meeting; open, nothing hangs there.
    HARMED (`harm`, step 4d), a shut door's leaves give on their hinges away from the blows, their far ends sagging;
    BROKEN, each hangs that way askew, by its hash, half off its hinges.
*/
void BuildGate(const ChasmWorld& w, const ZoneState& z, int g, std::vector<vertex>& out,
               const ZoneHarmLook* harm = NULL){
    const std::vector<vec2>& pos = w.grid->fine.pos;
    int nb[ROAD_MAX_NEIGHBOURS];
    int n = WallNeighbours(w,z,g,nb);
    bool f_wall = false;
    for (int i = 0; i < n; i++){
        if (z.ground[nb[i]] == ZONE_GROUND_WALL){
            //A broken plot beside it keeps its jamb (BuildPalisade), so the beam still has a post to hang from.
            const ZoneHarmMark* broken = harm ? harm->Wall(nb[i]) : NULL;
            if (!ZoneWallStands(z,nb[i]) && !(broken && broken->step >= ZONE_HARM_STEPS)){
                return;
            }
            f_wall = true;
        }
    }
    if (!f_wall || n < 2){
        return;
    }
    auto top = [&](const vec2& p){
        return vec3(p.x,ZoneGroundHeight(w,z,g,p) + WALL_GATE_HEIGHT - WALL_GATE_BEAM * 0.5f,p.y);
    };
    const vec2 c = pos[g];
    if (n == 2){
        Bar(out,top((c + pos[nb[0]]) * 0.5f),top((c + pos[nb[1]]) * 0.5f),WALL_GATE_BEAM,PAL_BARK_DARK);
    }else{
        for (int i = 0; i < n; i++){
            Bar(out,top(c),top((c + pos[nb[i]]) * 0.5f),WALL_GATE_BEAM,PAL_BARK_DARK);
        }
    }
    bool f_shut = ZoneGateShut(z,g);
    //HARMED (step 4d): its mark - or, of a gate two plots wide on the slant, the other plot's: the door is one.
    const ZoneHarmMark* hit = harm ? harm->Gate(g) : NULL;
    for (int i = 0; i < n && harm && !hit; i++){
        if (z.gate[nb[i]]){
            hit = harm->Gate(nb[i]);
        }
    }
    bool f_broken = hit && hit->step >= ZONE_HARM_STEPS && !f_shut;
    float f = (hit && f_shut) ? std::min(1.0f,(float)hit->step / ZONE_HARM_STEPS) : 0.0f;
    auto jamb_at = [&](int i){
        return z.ground[nb[i]] == ZONE_GROUND_WALL || z.KindOf(nb[i]) == ZONE_KIND_TOWER;
    };
    /*
        Harmed, each leaf turns as one about its jamb's hinge. A piece this plot draws with no jamb of its own is the far
        part of the leaf hung at the plot's one jamb (a gate on the slant), and turns with it; with no one jamb it stands
        as it is. Swung along the road to the side of the wall away from the blows - by the gate's chord, the line between
        its neighbours, as a palisade's logs are, since the zombies stand off its point and may stand along a bend of the
        wall - or with that not known, the open door's way.
    */
    int one_jamb = -1;
    int jambs = 0;
    for (int i = 0; i < n; i++){
        if (jamb_at(i)){
            one_jamb = i;
            jambs++;
        }
    }
    one_jamb = (jambs == 1) ? one_jamb : -1;
    vec2 push(0.6f,0.8f);
    if (hit && hit->f_from){
        push = c - hit->from;
        if (n == 2){
            vec2 d = pos[nb[1]] - pos[nb[0]];
            vec2 across(-d.y,d.x);
            push = (across.dot(push) < 0.0f) ? -across : across;
        }
    }
    const vec3 up(0.0f,1.0f,0.0f);
    for (int i = 0; i < n; i++){
        vec2 mid = (c + pos[nb[i]]) * 0.5f;
        vec2 in = c - mid;
        float half = in.length();
        if (half < 1e-4f){
            continue;
        }
        in = in * (1.0f / half);
        bool f_jamb = jamb_at(i);
        //From the jamb's inner face - or, with no jamb, from the meeting with the next plot's leaf.
        vec2 hinge = mid + in * (f_jamb ? WALL_GATE_POST_W * 0.5f : 0.0f);
        float reach = half - (f_jamb ? WALL_GATE_POST_W * 0.5f : 0.0f) - WALL_DOOR_GAP * 0.5f;
        int hung = f_jamb ? i : one_jamb;      //the jamb the leaf this piece is part of hangs from
        if (hung >= 0 && (f > 0.0f || f_broken)){
            //Built shut, then turned on its hinge: its far end dropped, then swung along the road away from the blows.
            vec2 jamb_mid = (c + pos[nb[hung]]) * 0.5f;
            vec2 shuts = c - jamb_mid;          //the way the leaf runs from its hinge, shut
            float span = shuts.length();
            if (span < 1e-4f){
                continue;
            }
            shuts = shuts * (1.0f / span);
            vec2 at = jamb_mid + shuts * (WALL_GATE_POST_W * 0.5f);
            size_t first = out.size();
            DoorLeaf(w,z,hinge,hinge + in * reach,g,i,out);
            uint32_t hash = MeshHash((uint32_t)g,(uint32_t)hung,0x6A7Eu);
            float swing = f_broken ? GATE_HANG_SWING + GATE_HANG_SWING_MORE * HashShare(hash,0)
                                   : GATE_GIVE * f * (0.7f + 0.3f * HashShare(hash,0));
            float sag = f_broken ? GATE_HANG_SAG + GATE_HANG_SAG_MORE * HashShare(hash,1) : GATE_SAG * f;
            vec3 pivot(at.x,ZoneGroundHeight(w,z,g,at),at.y);
            MeshTurn(out,first,pivot,up.cross(vec3(shuts.x,0.0f,shuts.y)),sag);
            //About up a turn takes `shuts` toward (shuts.y, -shuts.x).
            MeshTurn(out,first,pivot,up,(push.dot(vec2(shuts.y,-shuts.x)) > 0.0f) ? swing : -swing);
            if (f_broken){
                //Hung from its top hinge: its far end down near the ground, its near end lifted off it.
                MeshShift(out,first,vec3(0.0f,span * std::sin(sag) * 0.8f,0.0f));
            }
        }else if (f_shut){
            DoorLeaf(w,z,hinge,hinge + in * reach,g,i,out);
        }else if (f_jamb){
            vec2 side(-in.y,in.x);
            if (side.x * 0.6f + side.y * 0.8f < 0.0f){
                side = side * -1.0f;
            }
            DoorLeaf(w,z,hinge + side * (WALL_DOOR_LOG_W * 0.6f),hinge + side * (WALL_DOOR_LOG_W * 0.6f + reach),g,i,out);
        }
    }
}

/*
    A CELL ROAD ALL ROUND (the user's, 2026-10-18): its four corners laid road - two roads side by side, a square of
    them - so its four edges are road, and the patch of grass they ringed is road too. Not a bridge's: a deck over
    the water.
*/
bool RoadCellFilled(const ZoneState& z, const GridQuad& q){
    return ZoneRoadLaid(z,q.v[0]) && ZoneRoadLaid(z,q.v[1]) && ZoneRoadLaid(z,q.v[2]) && ZoneRoadLaid(z,q.v[3]);
}

//...drawn whole at the road's height, in pieces that each lie on the ground's relief as the road's own do.
void BuildRoadFill(const ChasmWorld& w, const ZoneState& z, const GridQuad& q, std::vector<vertex>& out){
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = w.grid->fine.pos[q.v[k]];
    }
    //Each piece on the ground of the corner whose quarter it lies in - a terrace's top, if that one is (terraces_plan.md).
    int plot = q.v[0];
    auto at = [&](float s, float t){
        vec2 x = MeshBilinear(p,s,t);
        return vec3(x.x,ZoneGroundHeight(w,z,plot,x) + ROAD_LIFT,x.y);
    };
    const float step = 1.0f / ROAD_FILL_PIECES;
    for (int i = 0; i < ROAD_FILL_PIECES; i++){
        for (int j = 0; j < ROAD_FILL_PIECES; j++){
            float s = i * step, t = j * step;
            float sm = s + step * 0.5f, tm = t + step * 0.5f;
            plot = q.v[(sm < 0.5f) ? ((tm < 0.5f) ? 0 : 3) : ((tm < 0.5f) ? 1 : 2)];
            MeshQuad(out,at(s,t),at(s + step,t),at(s + step,t + step),at(s,t + step),vec3(0.0f,1.0f,0.0f),PAL_PATH);
        }
    }
}

/*
    A STAIR up a terrace's wall (terraces_plan.md stage 2): where road plot v's road runs on to a road on a terrace above
    it (ZONE_STEP_STAIR), steps from the wall's head down to v's road, over v's half of the edge between them - where the
    walkers go down it (RouteHeight) - as wide as the road, in the terrace's stone. Each step a block down into the ground.
*/
#define ROAD_STAIR_RISE     0.2f

void RoadStairs(const ChasmWorld& w, const ZoneState& z, int v, const int* nb, int n, float half_width,
                std::vector<vertex>& out){
    const vec2 c = w.grid->fine.pos[v];
    float low = ZoneGroundAt(w,z,v);
    for (int i = 0; i < n; i++){
        int u = nb[i];
        if (ZoneTerraceStep(w,z,v,u) != ZONE_STEP_STAIR || ZoneGroundAt(w,z,u) <= low){
            continue;
        }
        float high = ZoneGroundAt(w,z,u);
        vec2 m = (c + w.grid->fine.pos[u]) * 0.5f;
        vec2 run = c - m;
        float len = run.length();
        if (len < 1e-4f){
            continue;
        }
        vec2 d = run * (1.0f / len);
        vec2 side(-d.y * half_width,d.x * half_width);
        int steps = std::max(1,(int)std::ceil((high - low) / ROAD_STAIR_RISE));
        float rise = (high - low) / steps;
        float tread = len / steps;
        for (int k = 0; k < steps; k++){
            vec2 p0 = m + d * (tread * k);
            vec2 p1 = m + d * (tread * (k + 1));
            float top = high - rise * (k + 1) + ROAD_LIFT;
            float foot = std::min(ZoneGroundHeight(w,z,v,p0),ZoneGroundHeight(w,z,v,p1)) - 0.05f;
            vec3 a0(p0.x - side.x,top,p0.y - side.y), a1(p0.x + side.x,top,p0.y + side.y);
            vec3 b0(p1.x - side.x,top,p1.y - side.y), b1(p1.x + side.x,top,p1.y + side.y);
            auto down = [&](const vec3& q){ return vec3(q.x,foot,q.z); };
            MeshQuad(out,a0,a1,b1,b0,vec3(0.0f,1.0f,0.0f),PAL_STONE_LIGHT);
            MeshQuad(out,down(b0),down(b1),b1,b0,vec3(d.x,0.0f,d.y),PAL_STONE_LIGHT);           //its riser, facing down the stair
            MeshQuad(out,down(a0),down(b0),b0,a0,vec3(-side.x,0.0f,-side.y),PAL_STONE);
            MeshQuad(out,down(a1),down(b1),b1,a1,vec3(side.x,0.0f,side.y),PAL_STONE);
        }
    }
}

//Whether `p` is road: within the half width of a laid road's centre line, or in a filled cell. RoadNear's lines, so
//a standing wall's count too - no stones against a palisade.
bool OnRoad(const ChasmWorld& w, const ZoneState& z, const vec2& p){
    GridPick g = w.picker->Pick(p);
    if (!g.f_hit){
        return false;
    }
    if (g.fine_quad >= 0 && RoadCellFilled(z,w.grid->fine.quads[g.fine_quad])){
        return true;
    }
    return RoadNear(w,z,g.plot,p,ROAD_HALF_WIDTH,true);
}

//A pebble lying at `at`: a low cone over an uneven ring, turned and shaped by `hash`.
void Stone(std::vector<vertex>& out, const vec3& at, float r, float h, uint32_t hash, int column){
    const int sides = 5;
    float turn = (float)(hash % 360) * 0.0174533f;
    vec3 ring[sides];
    for (int k = 0; k < sides; k++){
        float a = turn + 6.2831853f * k / sides;
        float rr = r * (0.75f + 0.5f * (float)((hash >> (k * 3 + 9)) & 7) / 7.0f);
        ring[k] = vec3(at.x + cosf(a) * rr,at.y + 0.004f,at.z + sinf(a) * rr);
    }
    vec3 top(at.x,at.y + h,at.z);
    for (int k = 0; k < sides; k++){
        vec3 mid = (ring[k] + ring[(k + 1) % sides]) * 0.5f;
        MeshTri(out,ring[k],ring[(k + 1) % sides],top,vec3(mid.x - at.x,r,mid.z - at.z),column);
    }
}

/*
    THE ROAD'S EDGE, where it meets the grass (the user's, 2026-10-18): what lies along it - stones, grass creeping
    in - at places a step apart along its sides, none where it runs on into more road, a junction's other arm or a
    filled cell (OnRoad, a step past the edge). `edges` are the road's sides as polylines, each with the centre line
    it runs beside, the way out from the road being away from its nearest point; the places along each and which of
    them take something go by the vertex's hash and a salt of each kind's own, so a road always looks the same.
*/
struct RoadEdge{
    std::vector<vec2> pts;
    std::vector<vec2> centre;   //one point for a round cap, out from its middle
};
vec2 NearestOn(const std::vector<vec2>& line, const vec2& p){
    if (line.size() == 1){
        return line[0];
    }
    vec2 best = line[0];
    float best_d = 1e30f;
    for (size_t i = 1; i < line.size(); i++){
        vec2 d = line[i] - line[i - 1];
        float len2 = d.dot(d);
        float t = (len2 > 1e-9f) ? std::max(0.0f,std::min(1.0f,(p - line[i - 1]).dot(d) / len2)) : 0.0f;
        vec2 q = line[i - 1] + d * t;
        if ((p - q).length() < best_d){
            best_d = (p - q).length();
            best = q;
        }
    }
    return best;
}
//`f(p, out, hash)` at each place along `edges` that `chance` percent of them take, where the road meets the grass.
template <class F>
void ForEdgePlaces(const ChasmWorld& w, const ZoneState& z, int v, const std::vector<RoadEdge>& edges, float step,
                   uint32_t salt, int chance, F f){
    for (size_t e = 0; e < edges.size(); e++){
        const RoadEdge& edge = edges[e];
        float along = 0.0f;
        float next = step * 0.5f;
        int place = 0;
        for (size_t i = 1; i < edge.pts.size(); i++){
            vec2 a = edge.pts[i - 1];
            vec2 b = edge.pts[i];
            float len = (b - a).length();
            while (next <= along + len){
                vec2 p = a + (b - a) * ((next - along) / std::max(1e-6f,len));
                next += step;
                uint32_t hash = MeshHash((uint32_t)v,(uint32_t)(e * 64 + place),salt);
                place++;
                if (hash % 100 >= (uint32_t)chance){
                    continue;
                }
                vec2 o = Unit(p - NearestOn(edge.centre,p));
                if (o.length() < 0.5f || OnRoad(w,z,p + o * ROAD_STONE_BEYOND)){
                    continue;
                }
                f(p,o,hash);
            }
            along += len;
        }
    }
}

//A tuft of grass at `at`: blades leaning out from its middle, each a thin triangle seen from both sides.
void Tuft(std::vector<vertex>& out, const vec3& at, uint32_t hash){
    for (int b = 0; b < ROAD_GRASS_BLADES; b++){
        uint32_t hb = MeshHash(hash,(uint32_t)b,0x6B1D);
        float a = (float)(hb % 628) / 100.0f;
        float from = 0.05f * (float)((hb >> 10) & 255) / 255.0f;
        vec3 foot(at.x + cosf(a) * from,at.y,at.z + sinf(a) * from);
        float tall = ROAD_GRASS_HEIGHT * (0.45f + 0.55f * (float)((hb >> 18) & 255) / 255.0f);
        vec3 tip(foot.x + cosf(a) * tall * 0.3f,at.y + tall,foot.z + sinf(a) * tall * 0.3f);
        vec3 across(-sinf(a) * ROAD_GRASS_BLADE_W,0.0f,cosf(a) * ROAD_GRASS_BLADE_W);
        vec3 face(cosf(a),0.3f,sinf(a));
        int column = ((hb >> 26) & 1) ? PAL_LEAF : PAL_LEAF_LIGHT;
        MeshTri(out,foot - across,foot + across,tip,face,column);
        MeshTri(out,foot - across,foot + across,tip,face * -1.0f,column);
    }
}

//What lies along a road's edges: the stones, and the grass creeping in over its rim.
void EdgeDressing(const ChasmWorld& w, const ZoneState& z, int v, float level, const std::vector<RoadEdge>& edges,
                  std::vector<vertex>& out){
    ForEdgePlaces(w,z,v,edges,ROAD_STONE_STEP,0x570E,ROAD_STONE_CHANCE,[&](const vec2& p, const vec2& o, uint32_t hash){
        float r = ROAD_STONE_MIN + (ROAD_STONE_MAX - ROAD_STONE_MIN) * (float)((hash >> 8) & 255) / 255.0f;
        float h = ROAD_STONE_HEIGHT * (0.5f + 0.5f * (float)((hash >> 16) & 255) / 255.0f);
        vec2 s = p - o * (r * 0.6f);
        Stone(out,vec3(s.x,ZoneGroundHeight(w,z,v,s) + ROAD_LIFT,s.y),r,h,hash,((hash >> 24) & 1) ? PAL_EARTH : PAL_BARK);
    });
    ForEdgePlaces(w,z,v,edges,ROAD_GRASS_STEP,0x6A55,ROAD_GRASS_CHANCE,[&](const vec2& p, const vec2& o, uint32_t hash){
        vec2 s = p - o * 0.03f;     //its foot just over the rim, onto the road
        Tuft(out,vec3(s.x,ZoneGroundHeight(w,z,v,s) + ROAD_LIFT,s.y),hash);
    });
}

//Whether the edge a - b has a filled cell on either side: it is along a closed area or inside one, and wears no
//middle of its own (ROAD_WORN_HALF).
bool EdgeBesideFill(const ChasmWorld& w, const ZoneState& z, int a, int b){
    const GridPicker& p = *w.picker;
    for (int i = 0; i < p.PlotQuadCount(a); i++){
        int qc = p.PlotQuadCorner(a,i);
        const GridQuad& q = w.grid->fine.quads[qc / 4];
        int k = qc % 4;
        if ((q.v[(k + 1) % 4] == b || q.v[(k + 3) % 4] == b) && RoadCellFilled(z,q)){
            return true;
        }
    }
    return false;
}

/*
    COBBLES (the user's, 2026-10-18): a closed area - its filled cells joined by an edge - with a building beside any
    of it, one with walls and a roof (ZoneKindHasBody: a house, a store, a workshop, not a camp's tents), is a square
    in the town, and all of it is laid with stones rather than trodden earth: a little grid of them over each cell
    (CobbleGrid), the road showing in the joints, each a stone's grey by its hash. A building put beside it redraws the chunk it is in, so
    a square across a chunk's border may lag on its far side until that chunk is next drawn.
*/
bool CellInTown(const ChasmWorld& w, const ZoneState& z, const GridQuad& q){
    for (int k = 0; k < 4; k++){
        int nb[ROAD_MAX_NEIGHBOURS];
        int n = ZonePlotNeighbours(w,q.v[k],nb,ROAD_MAX_NEIGHBOURS);
        for (int i = 0; i < n; i++){
            const ZoneBuilding* b = z.BuildingOf(nb[i]);
            if (b && ZoneKindHasBody(b->kind)){
                return true;
            }
        }
    }
    return false;
}
bool AreaInTown(const ChasmWorld& w, const ZoneState& z, int fine_quad){
    const GridPicker& picker = *w.picker;
    std::vector<int> open(1,fine_quad);
    std::vector<int> seen(1,fine_quad);
    while (!open.empty() && seen.size() < ROAD_TOWN_REACH){
        int at = open.back();
        open.pop_back();
        const GridQuad& q = w.grid->fine.quads[at];
        if (CellInTown(w,z,q)){
            return true;
        }
        //The filled cells across its edges.
        for (int k = 0; k < 4; k++){
            int a = q.v[k];
            int b = q.v[(k + 1) % 4];
            for (int i = 0; i < picker.PlotQuadCount(a); i++){
                int other = picker.PlotQuadCorner(a,i) / 4;
                const GridQuad& o = w.grid->fine.quads[other];
                bool f_across = other != at && (o.v[0] == b || o.v[1] == b || o.v[2] == b || o.v[3] == b);
                if (f_across && RoadCellFilled(z,o) && std::find(seen.begin(),seen.end(),other) == seen.end()){
                    seen.push_back(other);
                    open.push_back(other);
                }
            }
        }
    }
    return false;
}
//The cell across side k (corner k to k + 1) of fine cell `fine_quad`, or -1 at the map's edge.
int CellAcross(const ChasmWorld& w, int fine_quad, int k){
    const GridPicker& picker = *w.picker;
    const GridQuad& q = w.grid->fine.quads[fine_quad];
    int a = q.v[k];
    int b = q.v[(k + 1) % 4];
    for (int i = 0; i < picker.PlotQuadCount(a); i++){
        int other = picker.PlotQuadCorner(a,i) / 4;
        const GridQuad& o = w.grid->fine.quads[other];
        if (other != fine_quad && (o.v[0] == b || o.v[1] == b || o.v[2] == b || o.v[3] == b)){
            return other;
        }
    }
    return -1;
}

/*
    A CELL'S PATCH OF COBBLES, OUT TO THE ROAD ROUND IT (the user's, 2026-10-18: the stones filled the cell's square
    while the road under them curved round it). Its four sides: where the cell across is filled too, the straight
    line the two cells share; where it is not - the square's outline - the road's own edge, ROAD_COBBLE_RIM inside
    where the road drawn along that side ends, which is ROAD_HALF_WIDTH from its centre lines (RoadCentreLine). So
    round a corner the patch curves as the road does, rounds a junction's joint, and where another road leaves the
    square it stops straight across its mouth, no further out than the road's own half width.

    A corner on the outline is pushed out the same way, along a line every cell round it agrees on - away from the
    middle of the filled cells there - so two cells' patches meet at it; the sides lean toward that line near their
    ends. A point inside is the Coons patch of the four sides.
*/
struct CobblePatch{
    vec2 corner[4];
    std::vector<vec2> side[4];      //side k, corner k to corner k + 1, at ROAD_COBBLE_SAMPLES + 1 points

    vec2 Side(int k, float t) const{
        float f = std::max(0.0f,std::min(1.0f,t)) * ROAD_COBBLE_SAMPLES;
        int i = std::min(ROAD_COBBLE_SAMPLES - 1,(int)f);
        return side[k][i] + (side[k][i + 1] - side[k][i]) * (f - (float)i);
    }
    //(0, 0) at corner 0, (1, 0) corner 1, (1, 1) corner 2, (0, 1) corner 3 - MeshBilinear's order.
    vec2 At(float u, float v) const{
        vec2 bilinear = corner[0] * ((1.0f - u) * (1.0f - v)) + corner[1] * (u * (1.0f - v)) + corner[2] * (u * v)
                      + corner[3] * ((1.0f - u) * v);
        return Side(0,u) * (1.0f - v) + Side(2,1.0f - u) * v + Side(3,1.0f - v) * (1.0f - u) + Side(1,v) * u - bilinear;
    }
};
CobblePatch CobblePatchOf(const ChasmWorld& w, const ZoneState& z, int fine_quad){
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    const GridQuad& q = g.fine.quads[fine_quad];
    vec2 p[4];
    vec2 middle(0.0f,0.0f);
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[q.v[k]];
        middle = middle + p[k] * 0.25f;
    }
    //The road's centre lines near the cell: every laid road vertex of the cells round its corners - all that can reach
    //a point out to its half width beyond them, and the same near a corner whichever cell round it asks.
    std::vector<int> verts;
    for (int k = 0; k < 4; k++){
        for (int i = 0; i < picker.PlotQuadCount(q.v[k]); i++){
            const GridQuad& c = g.fine.quads[picker.PlotQuadCorner(q.v[k],i) / 4];
            for (int j = 0; j < 4; j++){
                if (ZoneRoadLaid(z,c.v[j]) && std::find(verts.begin(),verts.end(),c.v[j]) == verts.end()){
                    verts.push_back(c.v[j]);
                }
            }
        }
    }
    std::vector<vec2> lines, one;
    for (int v : verts){
        RoadCentreLine(w,z,v,one);      //which clears what it is given
        lines.insert(lines.end(),one.begin(),one.end());
    }
    const float reach = ROAD_HALF_WIDTH - ROAD_COBBLE_RIM;
    auto on_road = [&](const vec2& x){
        for (size_t s = 0; s + 1 < lines.size(); s += 2){
            if (SegmentDistance(x,lines[s],lines[s + 1]) <= reach){
                return true;
            }
        }
        return false;
    };
    //As far out from `from` along `dir` as the road goes, to `reach` at most; not at all where it is already past it.
    auto push = [&](const vec2& from, const vec2& dir){
        if (!on_road(from)){
            return from;
        }
        if (on_road(from + dir * reach)){
            return from + dir * reach;
        }
        float lo = 0.0f, hi = reach;
        for (int i = 0; i < 12; i++){
            float m = (lo + hi) * 0.5f;
            if (on_road(from + dir * m)){
                lo = m;
            }else{
                hi = m;
            }
        }
        return from + dir * lo;
    };
    //Each corner: kept where every cell round it is filled, else pushed out from the middle of those that are.
    bool f_out[4];
    vec2 dir[4];
    for (int k = 0; k < 4; k++){
        int v = q.v[k];
        vec2 filled(0.0f,0.0f);
        int n_filled = 0;
        for (int i = 0; i < picker.PlotQuadCount(v); i++){
            int c = picker.PlotQuadCorner(v,i) / 4;
            if (RoadCellFilled(z,g.fine.quads[c])){
                filled = filled + g.FineQuadCentre(c);
                n_filled++;
            }
        }
        f_out[k] = n_filled < picker.PlotQuadCount(v) && n_filled > 0;
        dir[k] = f_out[k] ? Unit(p[k] - filled * (1.0f / n_filled)) : vec2(0.0f,0.0f);
        f_out[k] = f_out[k] && dir[k].length() > 0.5f;
    }
    CobblePatch patch;
    for (int k = 0; k < 4; k++){
        patch.corner[k] = f_out[k] ? push(p[k],dir[k]) : p[k];
    }
    for (int k = 0; k < 4; k++){
        int k1 = (k + 1) % 4;
        int across = CellAcross(w,fine_quad,k);
        bool f_outline = (across < 0 || !RoadCellFilled(z,g.fine.quads[across])) && f_out[k] && f_out[k1];
        vec2 along = p[k1] - p[k];
        vec2 normal(-along.y,along.x);
        if (normal.dot(p[k] - middle) < 0.0f){
            normal = normal * -1.0f;
        }
        normal = Unit(normal);
        for (int i = 0; i <= ROAD_COBBLE_SAMPLES; i++){
            float t = (float)i / ROAD_COBBLE_SAMPLES;
            if (i == 0 || i == ROAD_COBBLE_SAMPLES){
                patch.side[k].push_back(patch.corner[i == 0 ? k : k1]);
            }else if (!f_outline){
                patch.side[k].push_back(patch.corner[k] + (patch.corner[k1] - patch.corner[k]) * t);
            }else{
                float w0 = std::max(0.0f,1.0f - t / ROAD_COBBLE_BLEND);
                float w1 = std::max(0.0f,1.0f - (1.0f - t) / ROAD_COBBLE_BLEND);
                vec2 d = Unit(normal * (1.0f - w0 - w1) + dir[k] * w0 + dir[k1] * w1);
                patch.side[k].push_back(push(p[k] + along * t,d));
            }
        }
    }
    return patch;
}

/*
    ONE CELL'S STONES, LAID AS THE MAP IS (the user's, 2026-10-18: a straight grid of them read as a chequerboard). A
    little grid of their own, made the way Grid.cpp makes the map's: a lattice of triangles over the cell - its
    squares, each cut along a diagonal by the hash - neighbours merged at random into quads wherever the two come out
    convex, every piece split into quads through its edges' midpoints and its middle, and the inside relaxed toward
    squares (GridSquareFit). So the stones lie in no rows. Laid over the cell's patch (CobblePatch), whose edge holds
    still and is cut the same from either side, so two cells' stones meet along it joint to joint. `quads` is four
    corners a stone, wound as the cell is.
*/
void CobbleGrid(const CobblePatch& patch, uint32_t seed, std::vector<vec2>& pos, std::vector<int>& quads){
    const int N = ROAD_COBBLE_LATTICE;
    std::vector<vec2> uv;       //where each point is in the cell: p[0] at (0, 0), p[2] at (1, 1)
    for (int j = 0; j <= N; j++){
        for (int i = 0; i <= N; i++){
            uv.push_back(vec2((float)i / N,(float)j / N));
        }
    }
    auto lattice = [&](int i, int j){ return j * (N + 1) + i; };
    std::vector<std::array<int,3>> tris;
    for (int j = 0; j < N; j++){
        for (int i = 0; i < N; i++){
            int a = lattice(i,j), b = lattice(i + 1,j), c = lattice(i + 1,j + 1), d = lattice(i,j + 1);
            if (MeshHash(seed,(uint32_t)(j * N + i),0xD1A6) & 1){
                tris.push_back({{a,b,c}});
                tris.push_back({{a,c,d}});
            }else{
                tris.push_back({{a,b,d}});
                tris.push_back({{b,c,d}});
            }
        }
    }
    /*
        The merge, in an order of the hash's: each triangle with the first neighbour still free that makes a convex quad
        with it - one with no corner near straight, which would leave a stone with a corner missing. The other half of
        its own square only for ROAD_COBBLE_SQUARES of them: that pair is always convex, so taken freely it puts nearly
        every square back and the cell is a plain grid of stones again - the chequerboard (seen, 2026-10-18).
    */
    std::vector<int> order(tris.size());
    for (size_t i = 0; i < order.size(); i++){
        order[i] = (int)i;
    }
    for (size_t i = order.size() - 1; i > 0; i--){
        std::swap(order[i],order[MeshHash(seed,(uint32_t)i,0x5EED) % (uint32_t)(i + 1)]);
    }
    auto cross = [](const vec2& a, const vec2& b){ return a.x * b.y - a.y * b.x; };
    std::vector<bool> used(tris.size(),false);
    std::vector<std::vector<int>> pieces;
    for (int t : order){
        if (used[t]){
            continue;
        }
        const std::array<int,3>& A = tris[t];
        int first = (int)(MeshHash(seed,(uint32_t)t,0x3E6E) % 3);
        for (int s = 0; s < 3 && !used[t]; s++){
            int k = (first + s) % 3;
            int u = A[k], v = A[(k + 1) % 3], x = A[(k + 2) % 3];
            for (size_t o = 0; o < tris.size() && !used[t]; o++){
                if (used[o] || (int)o == t){
                    continue;
                }
                if ((int)o / 2 == t / 2 && MeshHash(seed,(uint32_t)(t / 2),0x5A) % 100 >= ROAD_COBBLE_SQUARES){
                    continue;       //its own square's other half (two triangles a square, in order)
                }
                const std::array<int,3>& B = tris[o];
                for (int m = 0; m < 3; m++){
                    if (B[m] != v || B[(m + 1) % 3] != u){
                        continue;
                    }
                    int q[4] = {u,B[(m + 2) % 3],v,x};
                    bool f_convex = true;
                    for (int c = 0; c < 4; c++){
                        vec2 e0 = uv[q[(c + 1) % 4]] - uv[q[c]];
                        vec2 e1 = uv[q[(c + 2) % 4]] - uv[q[(c + 1) % 4]];
                        f_convex = f_convex && cross(e0,e1) > 0.2f * e0.length() * e1.length();
                    }
                    if (f_convex){
                        pieces.push_back(std::vector<int>(q,q + 4));
                        used[t] = true;
                        used[o] = true;
                    }
                    break;
                }
            }
        }
        if (!used[t]){
            used[t] = true;
            pieces.push_back(std::vector<int>(A.begin(),A.end()));
        }
    }
    //Every piece into quads: from each corner, to its edges' midpoints and the piece's middle.
    std::map<std::pair<int,int>,int> mids;
    auto mid = [&](int a, int b){
        std::pair<int,int> key(std::min(a,b),std::max(a,b));
        auto it = mids.find(key);
        if (it != mids.end()){
            return it->second;
        }
        uv.push_back((uv[a] + uv[b]) * 0.5f);
        mids[key] = (int)uv.size() - 1;
        return (int)uv.size() - 1;
    };
    quads.clear();
    for (const std::vector<int>& piece : pieces){
        int n = (int)piece.size();
        vec2 c(0.0f,0.0f);
        for (int v : piece){
            c = c + uv[v];
        }
        uv.push_back(c * (1.0f / n));
        int middle = (int)uv.size() - 1;
        for (int k = 0; k < n; k++){
            int next = mid(piece[k],piece[(k + 1) % n]);
            int prev = mid(piece[(k + n - 1) % n],piece[k]);
            quads.push_back(piece[k]);
            quads.push_back(next);
            quads.push_back(middle);
            quads.push_back(prev);
        }
    }
    //Into the world, and the inside relaxed; the patch's edge stays.
    pos.resize(uv.size());
    std::vector<bool> f_edge(uv.size());
    for (size_t i = 0; i < uv.size(); i++){
        pos[i] = patch.At(uv[i].x,uv[i].y);
        f_edge[i] = uv[i].x <= 0.0f || uv[i].x >= 1.0f || uv[i].y <= 0.0f || uv[i].y >= 1.0f;
    }
    std::vector<vec2> sum(pos.size());
    std::vector<int> count(pos.size());
    for (int pass = 0; pass < ROAD_COBBLE_RELAX; pass++){
        std::fill(sum.begin(),sum.end(),vec2(0.0f,0.0f));
        std::fill(count.begin(),count.end(),0);
        for (size_t q = 0; q + 3 < quads.size(); q += 4){
            vec2 c[4], target[4];
            for (int k = 0; k < 4; k++){
                c[k] = pos[quads[q + k]];
            }
            GridSquareFit(c,target);
            for (int k = 0; k < 4; k++){
                sum[quads[q + k]] = sum[quads[q + k]] + target[k];
                count[quads[q + k]]++;
            }
        }
        for (size_t i = 0; i < pos.size(); i++){
            if (!f_edge[i] && count[i] > 0){
                pos[i] = pos[i] + (sum[i] * (1.0f / count[i]) - pos[i]) * 0.5f;
            }
        }
    }
}
void BuildCobbles(const ChasmWorld& w, const ZoneState& z, const GridQuad& q, int fine_quad, std::vector<vertex>& out){
    std::vector<vec2> pos;
    std::vector<int> quads;
    CobbleGrid(CobblePatchOf(w,z,fine_quad),MeshHash((uint32_t)fine_quad,0xC0B1),pos,quads);
    for (size_t s = 0; s + 3 < quads.size(); s += 4){
        uint32_t hash = MeshHash((uint32_t)fine_quad,(uint32_t)(s / 4),0xC0B1);
        float lift = ROAD_LIFT + ROAD_COBBLE_LIFT * (0.7f + 0.6f * (float)((hash >> 8) & 255) / 255.0f);
        vec2 middle = (pos[quads[s]] + pos[quads[s + 1]] + pos[quads[s + 2]] + pos[quads[s + 3]]) * 0.25f;
        //On the ground of the plot nearest it - a terrace's top, if it is one (terraces_plan.md).
        int plot = q.v[0];
        for (int k = 1; k < 4; k++){
            if ((w.grid->fine.pos[q.v[k]] - middle).length() < (w.grid->fine.pos[plot] - middle).length()){
                plot = q.v[k];
            }
        }
        vec3 c[4];
        for (int k = 0; k < 4; k++){
            vec2 x = middle + (pos[quads[s + k]] - middle) * (1.0f - ROAD_COBBLE_GAP);
            c[k] = vec3(x.x,ZoneGroundHeight(w,z,plot,x) + lift,x.y);
        }
        int pick = (int)(hash % 100);
        int column = (pick < 60) ? PAL_STONE_LIGHT : (pick < 92) ? PAL_STONE : PAL_STONE_DARK;
        MeshQuad(out,c[0],c[1],c[2],c[3],vec3(0.0f,1.0f,0.0f),column);
    }
}

}

void BuildRoadCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out,
                   const ZoneHarmLook* harm){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    const vec3 up(0.0f,1.0f,0.0f);
    //Plain earth, like town ground: the road reads by its shape, and the palette has no spare column.
    const int column = PAL_PATH;
    const float h = ROAD_HALF_WIDTH;
    std::vector<vec2> segs;
    if (RoadCellFilled(z,quad)){
        BuildRoadFill(w,z,quad,out);
        if (AreaInTown(w,z,fine_quad)){
            BuildCobbles(w,z,quad,fine_quad,out);
        }
    }

    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        //A wall (Zones.h, WALLS): staked out while planned, a palisade once it stands; over a gate's road,
        //its crossbeam - the road itself is drawn below like any other.
        if (z.ground[v] == ZONE_GROUND_WALL || z.gate[v]){
            if (RoadVertexOwner(w,v) == fine_quad){
                //Harmed by zombies (step 4d): a broken plot is a wall site again, drawn as what is left of it. One
                //standing at its strength has been built again, as whole, the moment before the rules forget it.
                const ZoneHarmMark* hit = harm ? harm->Wall(v) : NULL;
                bool f_broken = hit && hit->step >= ZONE_HARM_STEPS;
                if (ZoneWallPlanned(z,v)){
                    if (f_broken){
                        BuildPalisade(w,z,v,out,hit);
                    }else{
                        BuildWallStake(w,z,v,out);
                    }
                }else if (ZoneWallStands(z,v)){
                    BuildPalisade(w,z,v,out,f_broken ? NULL : hit);
                }else{
                    BuildGate(w,z,v,out,harm);
                }
            }
            if (z.ground[v] == ZONE_GROUND_WALL){
                continue;
            }
        }
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
                float road = ZoneGroundHeight(w,z,v,mid) + ROAD_LIFT;
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
        //On its ground: a terrace's top, if it stands on one (terraces_plan.md).
        auto at = [&](const vec2& q){
            return vec3(q.x,ZoneGroundHeight(w,z,v,q) + ROAD_LIFT,q.y);
        };
        auto worn_at = [&](const vec2& q){
            return vec3(q.x,ZoneGroundHeight(w,z,v,q) + ROAD_LIFT + ROAD_WORN_LIFT,q.y);
        };
        int nb[ROAD_MAX_NEIGHBOURS];
        int n = RoadNeighbours(w,z,v,nb);
        LineThrough(w,v,nb,n,segs);
        if (!f_road && n == 0){
            continue;   //enclosed ground with no gate onto it: no path to draw
        }
        if (f_road){
            RoadStairs(w,z,v,nb,n,h,out);
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
            std::vector<vec2> left(centre.size()), right(centre.size()), sides(centre.size());
            for (size_t i = 0; i < centre.size(); i++){
                float t = (float)i / (float)(centre.size() - 1);
                //The curve's derivative: 2(1-t)(c-a) + 2t(b-c).
                vec2 d = Unit((c - a) * (1.0f - t) + (b - c) * t);
                sides[i] = vec2(-d.y,d.x);
                left[i] = centre[i] + sides[i] * h;
                right[i] = centre[i] - sides[i] * h;
            }
            for (size_t i = 0; i + 1 < centre.size(); i++){
                MeshQuad(out,at(left[i]),at(left[i + 1]),at(right[i + 1]),at(right[i]),up,column);
            }
            //Its worn middle, the same curve narrower - unless it runs along a closed area.
            if (f_road && !EdgeBesideFill(w,z,v,nb[0]) && !EdgeBesideFill(w,z,v,nb[1])){
                for (size_t i = 0; i + 1 < centre.size(); i++){
                    MeshQuad(out,worn_at(centre[i] + sides[i] * ROAD_WORN_HALF),worn_at(centre[i + 1] + sides[i + 1] * ROAD_WORN_HALF),
                             worn_at(centre[i + 1] - sides[i + 1] * ROAD_WORN_HALF),worn_at(centre[i] - sides[i] * ROAD_WORN_HALF),
                             up,PAL_PATH_WORN);
                }
            }
            if (f_road){
                EdgeDressing(w,z,v,level,{RoadEdge{left,centre},RoadEdge{right,centre}},out);
            }
            continue;
        }
        //A spoke to each midpoint, square-ended there, and its worn middle where it runs between grass.
        std::vector<RoadEdge> sides;
        bool f_worn = false;
        for (size_t i = 0; i + 1 < segs.size(); i += 2){
            vec2 d = Unit(segs[i + 1] - segs[i]);
            if (d.length() < 0.5f){
                continue;
            }
            vec2 side(-d.y,d.x);
            MeshQuad(out,at(segs[i] + side * h),at(segs[i + 1] + side * h),at(segs[i + 1] - side * h),
                     at(segs[i] - side * h),up,column);
            if (f_road && !EdgeBesideFill(w,z,v,nb[i / 2])){
                const float wh = ROAD_WORN_HALF;
                MeshQuad(out,worn_at(segs[i] + side * wh),worn_at(segs[i + 1] + side * wh),worn_at(segs[i + 1] - side * wh),
                         worn_at(segs[i] - side * wh),up,PAL_PATH_WORN);
                f_worn = true;
            }
            sides.push_back(RoadEdge{{segs[i] + side * h,segs[i + 1] + side * h},{segs[i],segs[i + 1]}});
            sides.push_back(RoadEdge{{segs[i] - side * h,segs[i + 1] - side * h},{segs[i],segs[i + 1]}});
        }
        //The joint: a round patch over the spokes' inner ends. Drawn over them, not cut against them -
        //it is the same colour on the same ground, so the overlap cannot show.
        RoadEdge cap{{},{c}};      //a dead end's or a lone plot's round end, out from its middle
        for (int i = 0; i < ROAD_JOINT_SIDES; i++){
            float a0 = 6.2831853f * i / ROAD_JOINT_SIDES;
            float a1 = 6.2831853f * (i + 1) / ROAD_JOINT_SIDES;
            vec2 p0 = c + vec2(cosf(a0),sinf(a0)) * h;
            vec2 p1 = c + vec2(cosf(a1),sinf(a1)) * h;
            MeshTri(out,at(c),at(p0),at(p1),up,column);
            cap.pts.push_back(p0);
            //...and the worn middle's own joint, so its spokes - or a dead end's one - meet round.
            if (f_worn){
                MeshTri(out,worn_at(c),worn_at(c + vec2(cosf(a0),sinf(a0)) * ROAD_WORN_HALF),
                        worn_at(c + vec2(cosf(a1),sinf(a1)) * ROAD_WORN_HALF),up,PAL_PATH_WORN);
            }
        }
        if (n <= 1){
            cap.pts.push_back(cap.pts.front());
            sides.push_back(cap);
        }
        if (f_road){
            EdgeDressing(w,z,v,level,sides,out);
        }
    }
}

bool RoadNear(const ChasmWorld& w, const ZoneState& z, int plot, const vec2& p, float clearance, bool f_line_only){
    if ((ZoneRoadLaid(z,plot) || ZoneWallStands(z,plot)) && !f_line_only){
        return true;
    }
    //Every road or wall vertex whose drawing can reach plot `plot`: its own corners' neighbours, one cell out.
    const GridPicker& picker = *w.picker;
    std::vector<vec2> segs;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        const GridQuad& q = w.grid->fine.quads[picker.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            int v = q.v[k];
            if (ZoneRoadLaid(z,v)){
                RoadCentreLine(w,z,v,segs);
            }else if (ZoneWallStands(z,v)){
                int nb[ROAD_MAX_NEIGHBOURS];
                int n = WallNeighbours(w,z,v,nb);
                LineThrough(w,v,nb,n,segs);
            }else{
                continue;
            }
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
