#include "BuildingMesh.h"
#include "MeshBuild.h"

#include <algorithm>
#include <cmath>

#define HOUSE_FOOTING       1.2f    //how far a wall runs below the house, under a slope: a plot may rise 0.7 (Zones.cpp) and the house is rounded to a half-storey
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
    EACH KIND'S LOOK (docs/buildings_plan.md): its walls, its roof, how often a ground-floor wall has a
    door and how wide, whether it has windows above the ground floor and a chimney. Plaster houses
    under red tile or shingle; a store of timber under slate, with wide doors and few windows; the
    woodcutter's hut of dark planks under thatch; the water collector of stone under shingle.
*/
struct KindLook{
    int wall;
    int roof[2];            //a building's roof is one of these, by its id
    int door_chance;        //one ground-floor wall in this many has a door
    float door_w;
    int window_every;       //a window on one wall in this many (1: all)
    int chimney_chance;     //percent of plots
};
const KindLook kind_looks[ZONE_KIND_COUNT] = {
    {PAL_WALL,{PAL_ROOF,PAL_TIMBER},DOOR_CHANCE,DOOR_W,1,0},           //none
    {PAL_WALL,{PAL_ROOF,PAL_TIMBER},DOOR_CHANCE,DOOR_W,1,CHIMNEY_CHANCE},  //house
    {PAL_TIMBER,{PAL_STONE_DARK,PAL_STONE_DARK},2,DOOR_W * 1.9f,3,0},     //store
    {PAL_BARK_DARK,{PAL_FIELD,PAL_FIELD},3,DOOR_W * 1.4f,2,25},          //woodcutter
    {PAL_STONE_LIGHT,{PAL_TIMBER,PAL_TIMBER},2,DOOR_W,2,0},               //water collector
    {PAL_WALL,{PAL_ROOF,PAL_TIMBER},DOOR_CHANCE,DOOR_W,1,0},           //field: never a building on a plot
    {PAL_TIMBER,{PAL_TIMBER,PAL_TIMBER},2,DOOR_W,2,0},                    //winch: drawn by BuildWinch instead
    {PAL_WALL,{PAL_WALL,PAL_WALL},2,DOOR_W,2,0},                          //camp: drawn by BuildCampPlot instead
    {PAL_TIMBER,{PAL_TIMBER,PAL_TIMBER},2,DOOR_W,2,0},                    //bridge: a road over the water, drawn by RoadMesh
};

/*
    A beam from a to b on the ground plane, `thick` across, from y0 up by `height`: its two long sides,
    its top, its bottom and its two ends. Posts, a deck, a gantry, a rope.
*/
void Beam(std::vector<vertex>& out, const vec2& a, const vec2& b, float y0, float height, float thick, int column){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-5f){
        return;
    }
    dir = dir / len;
    vec2 side(-dir.y * thick * 0.5f,dir.x * thick * 0.5f);
    float y1 = y0 + height;
    vec3 al(a.x + side.x,y0,a.y + side.y), ar(a.x - side.x,y0,a.y - side.y);
    vec3 bl(b.x + side.x,y0,b.y + side.y), br(b.x - side.x,y0,b.y - side.y);
    vec3 alt(al.x,y1,al.z), art(ar.x,y1,ar.z), blt(bl.x,y1,bl.z), brt(br.x,y1,br.z);
    vec3 n_side(side.x,0.0f,side.y);
    MeshQuad(out,al,bl,blt,alt,n_side,column);
    MeshQuad(out,ar,br,brt,art,n_side * -1.0f,column);
    MeshQuad(out,alt,blt,brt,art,vec3(0,1,0),column);
    MeshQuad(out,al,ar,br,bl,vec3(0,-1,0),column);
    MeshQuad(out,al,ar,art,alt,vec3(-dir.x,0.0f,-dir.y),column);
    MeshQuad(out,bl,br,brt,blt,vec3(dir.x,0.0f,dir.y),column);
}

//An upright post of `w` square at p, from y0 to y1.
void Upright(std::vector<vertex>& out, const vec2& p, float w, float y0, float y1, int column){
    Beam(out,p - vec2(w * 0.5f,0.0f),p + vec2(w * 0.5f,0.0f),y0,y1 - y0,w,column);
}

#define WINCH_GANTRY_HEIGHT 2.6f    //above the rim's ground
#define WINCH_GANTRY_SPAN   1.3f    //between its two posts
#define WINCH_POST          0.16f

/*
    A WINCH (docs/buildings_plan.md step 2): a deck on the rim, a gantry of two posts and a crossbeam
    standing at the lip, a jib from the crossbeam out over the cliff to above the landing, a rope from
    it straight down to a basket resting on the balcony, and the winding drum in a shed behind. The
    lip is halfway from the plot's vertex to its landing's - where the terrain cuts the cliff - so the
    gantry stands on the edge and the rope clears the wall's strata, which stand out less than that.
*/
void BuildWinch(const ChasmWorld& w, int plot, std::vector<vertex>& out){
    int landing = ZoneWinchLanding(w,plot);
    if (landing < 0){
        return;
    }
    const Terrain& t = *w.terrain;
    vec2 p = w.grid->fine.pos[plot];
    vec2 l = w.grid->fine.pos[landing];
    vec2 dir = l - p;
    float reach = dir.length();
    if (reach < 1e-4f){
        return;
    }
    dir = dir / reach;
    vec2 side(-dir.y,dir.x);
    float y0 = t.ground[plot];
    float yl = t.ground[landing];
    vec2 lip = p + dir * (reach * 0.5f);
    float top = y0 + WINCH_GANTRY_HEIGHT;
    //The deck, from behind the vertex to the lip.
    Beam(out,p - dir * 0.9f,lip,y0 - 0.3f,0.42f,1.5f,PAL_TIMBER);
    //The gantry: two posts at the lip, a crossbeam over them.
    vec2 post_a = lip + side * (WINCH_GANTRY_SPAN * 0.5f);
    vec2 post_b = lip - side * (WINCH_GANTRY_SPAN * 0.5f);
    Upright(out,post_a,WINCH_POST,y0 - 0.3f,top,PAL_BARK);
    Upright(out,post_b,WINCH_POST,y0 - 0.3f,top,PAL_BARK);
    Beam(out,post_a + side * 0.1f,post_b - side * 0.1f,top - 0.18f,0.18f,0.18f,PAL_BARK);
    //The jib, out to above the landing, and a brace back from it to the deck.
    Beam(out,lip - dir * 0.2f,l + dir * 0.15f,top - 0.16f,0.16f,0.16f,PAL_BARK);
    //The rope, and the basket at its foot.
    Upright(out,l,0.05f,yl + 0.55f,top - 0.16f,PAL_BARK_DARK);
    Beam(out,l - dir * 0.32f,l + dir * 0.32f,yl,0.55f,0.62f,PAL_TIMBER);
    //The drum, in a little shed behind the deck.
    vec2 shed = p - dir * 0.45f;
    Beam(out,shed - side * 0.55f,shed + side * 0.55f,y0 - 0.3f,1.25f,0.9f,PAL_BARK_DARK);
    Beam(out,shed - side * 0.65f,shed + side * 0.65f,y0 + 0.95f,0.14f,1.05f,PAL_STONE_DARK);
}

#define TENT_LENGTH         1.25f   //along its ridge
#define TENT_HALF_W         0.55f   //from the ridge out to a side's foot
#define TENT_HEIGHT         0.85f
#define FIRE_RING           0.32f   //the stones' circle
#define FIRE_STONES         7

/*
    A CAMP's plot (Zones.h, THE CAMP): a tent - an A-frame of canvas on a ridge pole, its door a dark
    triangle in the front end - or, on one plot in ZONE_CAMP_FIRE_EVERY, a fire: a ring of stones round
    two crossed logs and a flame, with a log to sit on beside it. Each tent faces its own way and some
    are a paler canvas, by the plot's hash, so a camp looks pitched by hand rather than laid out.
*/
void Box(std::vector<vertex>& out, const vec2& p, float w, float y0, float y1, int column);

void BuildCampPlot(const ChasmWorld& w, int plot, std::vector<vertex>& out){
    vec2 p = w.grid->fine.pos[plot];
    float y = w.terrain->ground[plot];
    uint32_t h = MeshHash((uint32_t)plot,0xCA4Bu);
    float yaw = ZoneCampYaw(plot);      //the way its people step out of it agrees (ZoneCampDoor)
    vec2 dir(std::cos(yaw),std::sin(yaw));
    vec2 side(-dir.y,dir.x);
    if (ZoneCampFire(plot)){
        //Stones round the fire, then the logs crossed in it, then the flame - red, and yellow in its heart.
        for (int i = 0; i < FIRE_STONES; i++){
            float a = yaw + 6.2831853f * (float)i / FIRE_STONES;
            Box(out,p + vec2(std::cos(a),std::sin(a)) * FIRE_RING,0.13f,y - 0.02f,y + 0.09f,PAL_STONE);
        }
        Beam(out,p - dir * 0.24f,p + dir * 0.24f,y,0.07f,0.08f,PAL_BARK_DARK);
        Beam(out,p - side * 0.24f,p + side * 0.24f,y + 0.05f,0.07f,0.08f,PAL_BARK_DARK);
        auto flame = [&](float half, float top, int column){
            vec3 apex(p.x,y + top,p.y);
            vec3 c[4];
            for (int i = 0; i < 4; i++){
                vec2 q = p + (i & 1 ? side : dir) * ((i & 2) ? -half : half);
                c[i] = vec3(q.x,y + 0.08f,q.y);
            }
            int order[4] = {0,1,2,3};
            vec2 corner[4] = {dir,side,dir * -1.0f,side * -1.0f};
            for (int i = 0; i < 4; i++){
                vec2 o = corner[i] + corner[(i + 1) % 4];
                MeshTri(out,c[order[i]],c[order[(i + 1) % 4]],apex,vec3(o.x,0.4f,o.y),column);
            }
        };
        flame(0.15f,0.48f,PAL_ACCENT);
        flame(0.08f,0.62f,PAL_FIELD);
        //A log to sit on, a little way off.
        vec2 seat = p - side * 0.85f;
        Beam(out,seat - dir * 0.4f,seat + dir * 0.4f,y,0.16f,0.18f,PAL_BARK);
        return;
    }
    int canvas = ((h >> 10) % 10 < 7) ? PAL_WALL : PAL_STONE_LIGHT;
    vec2 f = p + dir * (TENT_LENGTH * 0.5f);
    vec2 b = p - dir * (TENT_LENGTH * 0.5f);
    vec3 rf(f.x,y + TENT_HEIGHT,f.y), rb(b.x,y + TENT_HEIGHT,b.y);
    vec3 lf(f.x + side.x * TENT_HALF_W,y,f.y + side.y * TENT_HALF_W);
    vec3 lb(b.x + side.x * TENT_HALF_W,y,b.y + side.y * TENT_HALF_W);
    vec3 qf(f.x - side.x * TENT_HALF_W,y,f.y - side.y * TENT_HALF_W);
    vec3 qb(b.x - side.x * TENT_HALF_W,y,b.y - side.y * TENT_HALF_W);
    //The two sloping sides, and the two ends.
    MeshQuad(out,lb,lf,rf,rb,vec3(side.x,0.6f,side.y),canvas);
    MeshQuad(out,qb,qf,rf,rb,vec3(-side.x,0.6f,-side.y),canvas);
    MeshTri(out,lf,qf,rf,vec3(dir.x,0.0f,dir.y),canvas);
    MeshTri(out,lb,qb,rb,vec3(-dir.x,0.0f,-dir.y),canvas);
    //The door: a dark triangle a hair in front of the front end, a little under half its height.
    vec2 d = f + dir * FACE_OUT;
    vec3 dl(d.x + side.x * TENT_HALF_W * 0.38f,y,d.y + side.y * TENT_HALF_W * 0.38f);
    vec3 dr(d.x - side.x * TENT_HALF_W * 0.38f,y,d.y - side.y * TENT_HALF_W * 0.38f);
    vec3 dt(d.x,y + TENT_HEIGHT * 0.62f,d.y);
    MeshTri(out,dl,dr,dt,vec3(dir.x,0.0f,dir.y),PAL_BARK_DARK);
    //The ridge pole, standing a little proud at both ends.
    Beam(out,b - dir * 0.08f,f + dir * 0.08f,y + TENT_HEIGHT - 0.02f,0.05f,0.05f,PAL_TIMBER);
}

/*
    A slab standing out of a wall: from u0 to u1 along it (`dir`, from `c`), y0 to y1 up, `depth`
    out (`out2`). Its back is against the wall and never seen, so it has five faces.
*/
void WallSlab(std::vector<vertex>& out, const vec2& c, const vec2& dir, const vec2& out2, const vec3& outward,
              float u0, float u1, float y0, float y1, float depth, int column){
    auto P = [&](float u, float y, float d){
        vec2 q = c + dir * u + out2 * d;
        return vec3(q.x,y,q.y);
    };
    vec3 side(dir.x,0.0f,dir.y);
    MeshQuad(out,P(u0,y0,depth),P(u1,y0,depth),P(u1,y1,depth),P(u0,y1,depth),outward,column);
    MeshQuad(out,P(u0,y1,0.0f),P(u1,y1,0.0f),P(u1,y1,depth),P(u0,y1,depth),vec3(0,1,0),column);
    MeshQuad(out,P(u0,y0,0.0f),P(u1,y0,0.0f),P(u1,y0,depth),P(u0,y0,depth),vec3(0,-1,0),column);
    MeshQuad(out,P(u0,y0,0.0f),P(u0,y1,0.0f),P(u0,y1,depth),P(u0,y0,depth),side * -1.0f,column);
    MeshQuad(out,P(u1,y0,0.0f),P(u1,y1,0.0f),P(u1,y1,depth),P(u1,y0,depth),side,column);
}

/*
    A window or door: a dark rectangle a hair in front of the wall a->b (facing `out`), centred at
    `t` along it, between heights y0 and y1 - set into a SURROUND of the wall's own colour that
    stands out from the wall around it (jambs and a head, and a sill under a window). The pane then
    sits back inside it, and the sun lights the surround's inner faces differently from its front:
    the depth of an inset without cutting a hole in the wall.
*/
#define SURROUND_W          0.05f   //a jamb's or head's width, world units
#define SURROUND_DEPTH      0.07f   //how far the surround stands out of the wall
#define SILL_OVERHANG       0.04f   //a sill past the jambs either side
#define SILL_DEPTH          0.11f
#define SILL_H              0.045f

void Opening(std::vector<vertex>& out, const vec2& a, const vec2& b, const vec3& outward, float t,
             float half_w, float y0, float y1, int column, int surround_column, bool f_sill){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-4f){
        return;
    }
    dir = dir / len;
    half_w = std::min(half_w,len * 0.38f);
    vec2 c = a + (b - a) * t;
    vec2 out2(outward.x,outward.z);
    vec2 off = out2 * FACE_OUT;
    vec2 l = c - dir * half_w + off;
    vec2 r = c + dir * half_w + off;
    MeshQuad(out,vec3(l.x,y0,l.y),vec3(r.x,y0,r.y),vec3(r.x,y1,r.y),vec3(l.x,y1,l.y),outward,column);
    //The surround: two jambs, a head across their tops, and under a window a sill wider than both.
    const float w = SURROUND_W;
    WallSlab(out,c,dir,out2,outward,-half_w - w,-half_w,y0,y1 + w,SURROUND_DEPTH,surround_column);
    WallSlab(out,c,dir,out2,outward,half_w,half_w + w,y0,y1 + w,SURROUND_DEPTH,surround_column);
    WallSlab(out,c,dir,out2,outward,-half_w,half_w,y1,y1 + w,SURROUND_DEPTH,surround_column);
    if (f_sill){
        WallSlab(out,c,dir,out2,outward,-half_w - w - SILL_OVERHANG,half_w + w + SILL_OVERHANG,
                 y0 - SILL_H,y0,SILL_DEPTH,surround_column);
    }
}

//A beam of square section from a at height ya to b at height yb: a pipe running down a slope.
void SlopedBeam(std::vector<vertex>& out, const vec2& a, float ya, const vec2& b, float yb, float thick, int column){
    vec2 d = b - a;
    float l = d.length();
    if (l < 1e-4f){
        return;
    }
    vec2 side(-d.y / l * thick * 0.5f,d.x / l * thick * 0.5f);
    float h = thick * 0.5f;
    vec3 al(a.x + side.x,ya,a.y + side.y), ar(a.x - side.x,ya,a.y - side.y);
    vec3 bl(b.x + side.x,yb,b.y + side.y), br(b.x - side.x,yb,b.y - side.y);
    vec3 up(0.0f,h,0.0f);
    vec3 n_side(side.x,0.0f,side.y);
    MeshQuad(out,al + up,bl + up,br + up,ar + up,vec3(0,1,0),column);
    MeshQuad(out,al - up,ar - up,br - up,bl - up,vec3(0,-1,0),column);
    MeshQuad(out,al - up,bl - up,bl + up,al + up,n_side,column);
    MeshQuad(out,ar - up,ar + up,br + up,br - up,n_side * -1.0f,column);
    MeshQuad(out,al - up,al + up,ar + up,ar - up,vec3(-d.x / l,0.0f,-d.y / l),column);
    MeshQuad(out,bl - up,br - up,br + up,bl + up,vec3(d.x / l,0.0f,d.y / l),column);
}

#define PIPE_THICK          0.16f
#define PIPE_POST_EVERY     1.4f    //world units between the posts under it
#define PIPE_OUT            0.6f    //how far past the water's edge it reaches
#define PIPE_WALL           0.9f    //where it leaves the collector, from the plot's vertex

/*
    A WATER COLLECTOR's pipe (the user, 2026-10-06): from its wall out over the bank's stones and down to
    the water, on posts, with an intake box where it dips in - which is what tells a collector from any
    other shed by the river. Toward the water down the slope of the water's edge distance; at a swamp
    pool, which that does not know, toward the wet ground round the plot.
*/
void BuildWaterPipe(const ChasmWorld& w, int plot, std::vector<vertex>& out){
    const Terrain& t = *w.terrain;
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    vec2 p = g.fine.pos[plot];
    float edge = t.WaterEdgeDistance(p);
    vec2 dir(0.0f,0.0f);
    float reach = 0.0f;
    const float near_river = TERRAIN_RIVER_BANK + TERRAIN_WET_MARGIN + 4.0f;
    if (edge < near_river){
        const float h = 0.5f;
        dir = vec2(t.WaterEdgeDistance(p - vec2(h,0.0f)) - t.WaterEdgeDistance(p + vec2(h,0.0f)),
                   t.WaterEdgeDistance(p - vec2(0.0f,h)) - t.WaterEdgeDistance(p + vec2(0.0f,h)));
        reach = edge + PIPE_OUT;
    }
    if (dir.length() < 1e-4f){
        //A pool: toward the wet corners of its cells, as far as the farthest and a little more.
        vec2 sum(0.0f,0.0f);
        for (int i = 0; i < picker.PlotQuadCount(plot); i++){
            const GridQuad& q = g.fine.quads[picker.PlotQuadCorner(plot,i) / 4];
            for (int k = 0; k < 4; k++){
                if (t.wet[q.v[k]]){
                    vec2 d = g.fine.pos[q.v[k]] - p;
                    sum = sum + d;
                    reach = std::max(reach,d.length() + 1.0f);
                }
            }
        }
        dir = sum;
    }
    float l = dir.length();
    if (l < 1e-4f){
        return;
    }
    dir = dir * (1.0f / l);
    reach = std::max(PIPE_WALL + 0.5f,reach);
    float y0 = t.ground[plot] + 0.55f;
    float y1 = terrain_levels[t.level[plot]].height + TERRAIN_WATER_Y + 0.12f;
    vec2 a = p + dir * PIPE_WALL;
    vec2 b = p + dir * reach;
    SlopedBeam(out,a,y0,b,y1,PIPE_THICK,PAL_TIMBER);
    for (float d = PIPE_WALL + PIPE_POST_EVERY; d < reach - 0.3f; d += PIPE_POST_EVERY){
        float f = (d - PIPE_WALL) / (reach - PIPE_WALL);
        float y = y0 + (y1 - y0) * f;
        Upright(out,p + dir * d,0.08f,y1 - 1.0f,y - PIPE_THICK * 0.5f,PAL_BARK_DARK);
    }
    //The intake, standing in the water.
    Box(out,b,0.34f,y1 - 0.7f,y1 + 0.22f,PAL_STONE_DARK);
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

#define SITE_POLE           0.09f
#define SITE_PLANK          0.07f
#define SITE_INSET          0.82f   //the poles stand this far out from the plot's vertex toward its cells' centres

/*
    A CONSTRUCTION SITE's plot (Zones.h, CONSTRUCTION): scaffolding round what is still to be built -
    poles at the corners of the plot's share of its cells, a ring of planks at every storey still to
    come - and, where nothing stands yet, a timber frame laid on the ground. It stands on what is built,
    so a house rising storey by storey has the scaffold on top of it. A camp's tent is no building: an
    unpitched one is its poles and canvas lying on the ground.
*/
//The deck's height: over its level's ground, the same the length of the bridge.
float BridgeDeckY(const ChasmWorld& w, int plot){
    return terrain_levels[w.terrain->level[plot]].height + ZONE_BRIDGE_DECK;
}

#define BRIDGE_WIDTH        1.05f   //the deck, across
#define BRIDGE_PLANK        0.12f   //its thickness
#define BRIDGE_RAIL_Y       0.42f   //the rail over the deck
#define BRIDGE_PILE         0.12f
#define BRIDGE_PILE_FOOT    1.6f    //how far below the water's surface the piles go: out of sight

//A pair of piles under a bridge, across it at p, from the river bed to `top`.
void BridgePiles(const vec2& p, const vec2& side, float top, std::vector<vertex>& out){
    float foot = terrain_levels[TERRAIN_PLATEAU].height + TERRAIN_WATER_Y - BRIDGE_PILE_FOOT;
    Upright(out,p + side * (BRIDGE_WIDTH * 0.42f),BRIDGE_PILE,foot,top,PAL_BARK_DARK);
    Upright(out,p - side * (BRIDGE_WIDTH * 0.42f),BRIDGE_PILE,foot,top,PAL_BARK_DARK);
}

//The way a bridge runs at a plot: from one of its bridge neighbours to another (or the first, alone).
vec2 BridgeRun(const ChasmWorld& w, const ZoneState& z, int plot){
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    int ends[2] = {-1,-1};
    int k = 0;
    for (int i = 0; i < n && k < 2; i++){
        if (z.building[nb[i]] == z.building[plot]){
            ends[k++] = nb[i];
        }
    }
    vec2 p = w.grid->fine.pos[plot];
    vec2 d = (k == 2) ? w.grid->fine.pos[ends[0]] - w.grid->fine.pos[ends[1]] : (k == 1) ? w.grid->fine.pos[ends[0]] - p : vec2(1.0f,0.0f);
    float l = d.length();
    return (l > 1e-4f) ? d * (1.0f / l) : vec2(1.0f,0.0f);
}

void BuildSite(const ChasmWorld& w, const ZoneState& z, int plot, std::vector<vertex>& out){
    const GridPicker& picker = *w.picker;
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    vec2 p = w.grid->fine.pos[plot];
    float ground = w.terrain->ground[plot];
    //A bridge being built: its piles already stand out of the water, waiting for the deck; on the
    //bank, a stack of its planks.
    if (z.KindOf(plot) == ZONE_KIND_BRIDGE){
        vec2 run = BridgeRun(w,z,plot);
        vec2 side(-run.y,run.x);
        float deck = BridgeDeckY(w,plot);
        if (w.terrain->wet[plot]){
            BridgePiles(p,side,deck + 0.15f,out);
        }else{
            for (int k = 0; k < 3; k++){
                vec2 o = side * (0.18f * (float)k - 0.18f);
                Beam(out,p - run * 0.6f + o,p + run * 0.6f + o,ground + 0.12f * (float)(k % 2),0.1f,0.16f,PAL_TIMBER);
            }
        }
        return;
    }
    if (z.KindOf(plot) == ZONE_KIND_CAMP){
        if (!ZoneCampFire(plot)){
            float yaw = ZoneCampYaw(plot);
            vec2 d(std::cos(yaw),std::sin(yaw));
            vec2 s(-d.y,d.x);
            Beam(out,p - d * 0.6f + s * 0.12f,p + d * 0.6f + s * 0.12f,ground,0.06f,0.06f,PAL_BARK);
            Beam(out,p - d * 0.55f - s * 0.1f,p + d * 0.5f - s * 0.1f,ground,0.06f,0.06f,PAL_BARK);
            Beam(out,p - d * 0.3f,p + d * 0.3f,ground,0.14f,0.42f,PAL_STONE_LIGHT);     //the canvas, rolled
        }
        return;
    }
    //The corners of the plot's footprint, round it in order of angle.
    std::vector<std::pair<float,vec2>> corners;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        int q = picker.PlotQuadCorner(plot,i) / 4;
        vec2 c = w.grid->FineQuadCentre(q);
        vec2 at = p + (c - p) * SITE_INSET;
        corners.push_back(std::make_pair(std::atan2(at.y - p.y,at.x - p.x),at));
    }
    std::sort(corners.begin(),corners.end(),[](const std::pair<float,vec2>& a, const std::pair<float,vec2>& b){
        return a.first < b.first;
    });
    int n = (int)corners.size();
    if (n < 3){
        return;
    }
    float foot = (float)std::lround(ground / HALF) * HALF;
    int standing = z.standing[plot];
    int planned = z.storeys[plot];
    float base = foot + standing * ZONE_STOREY_HEIGHT;
    float top = foot + planned * ZONE_STOREY_HEIGHT + 0.25f;
    for (int i = 0; i < n; i++){
        Upright(out,corners[i].second,SITE_POLE,(standing > 0) ? base - 0.05f : ground - 0.3f,top,PAL_BARK);
    }
    for (int i = 0; i < n; i++){
        const vec2& a = corners[i].second;
        const vec2& b = corners[(i + 1) % n].second;
        //A frame on the ground where nothing stands, then a ring of planks a storey up each time.
        if (standing == 0){
            Beam(out,a,b,ground - 0.05f,0.16f,0.14f,PAL_TIMBER);
        }
        for (int s = standing + 1; s <= planned; s++){
            float y = foot + s * ZONE_STOREY_HEIGHT - 0.2f;
            Beam(out,a,b,y,SITE_PLANK,SITE_PLANK * 2.0f,PAL_TIMBER);
        }
        //A brace across every other side of the lowest storey still to build.
        if (i % 2 == 0){
            vec2 mid = (a + b) * 0.5f;
            Beam(out,a + (mid - a) * 0.1f,b,base + 0.3f,SITE_PLANK,SITE_PLANK,PAL_BARK_DARK);
        }
    }
}

}

void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& picker = *w.picker;
    const GridQuad& quad = g.fine.quads[fine_quad];
    const vec3 up(0.0f,1.0f,0.0f);

    /*
        Each corner as a SOLID between two heights, counted in half-storeys (HALF) from y = 0, so heights
        compare exactly:
          - a house stands on its plot's ground (Terrain::ground - relief and all, biomes_plan.md step
            2) rounded to the nearest half-storey, and rises s storeys from there;
          - an ARCH (Zones.h, ZoneArchStoreys) is the same from one storey up, over its passage - so the
            houses either side of a road carry their upper floors across it.
        Rounding to half-storeys is what lets a row of houses on a gentle slope still share a roof, and
        where the ground has climbed a half-storey, the step is a short wall with the roof going on.
    */
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    vec2 p[4];
    int s[4];
    /*
        Which building each corner is: a roof only runs on toward a corner of the SAME building (and
        as tall), so two buildings side by side meet in a valley and each reads as its own. An arch
        belongs to whichever it bridges between - it matches either side.
    */
    const uint32_t ARCH = 0xFFFFFFFFu;
    uint32_t bid[4];
    int lo[4];      //the solid's bottom, half-storeys
    int hi[4];      //its top: the eave
    int foot[4];    //the ground it stands on
    bool solid[4];
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        p[k] = g.fine.pos[v];
        //What stands is the body; a site's storeys still to come are its scaffold, drawn once a plot.
        s[k] = z.standing[v];
        if (z.storeys[v] > z.standing[v] && picker.PlotQuadCorner(v,0) / 4 == fine_quad){
            BuildSite(w,z,v,out);
        }
        int base = 0;
        if (s[k] == 0){
            int arch = ZoneArchStoreys(w,z,v);
            if (arch > 0){
                s[k] = arch;
                base = 1;
            }
        }
        /*
            A winch is not a house body: it is drawn on its own (BuildWinch), by the first cell round
            its plot - and its corner counts as empty here, so the buildings beside it close their walls.
        */
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_WINCH){
            if (picker.PlotQuadCorner(v,0) / 4 == fine_quad){
                BuildWinch(w,v,out);
            }
            s[k] = 0;
        }
        //A camp is no house body either: a tent or a fire on each of its plots, the same way.
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_CAMP){
            if (picker.PlotQuadCorner(v,0) / 4 == fine_quad){
                BuildCampPlot(w,v,out);
            }
            s[k] = 0;
        }
        //Nor a bridge: once it stands it is a road over the water, drawn with the roads (RoadMesh).
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_BRIDGE){
            s[k] = 0;
        }
        //A water collector is a house body, and its pipe out over the bank's stones to the water.
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_WATER && picker.PlotQuadCorner(v,0) / 4 == fine_quad){
            BuildWaterPipe(w,v,out);
        }
        solid[k] = s[k] > 0;
        bid[k] = (base > 0) ? ARCH : z.building[v];
        foot[k] = (int)std::lround(t.ground[v] / HALF);
        lo[k] = foot[k] + 2 * base;
        hi[k] = foot[k] + 2 * s[k];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    vec2 m[4];      //m[k] is the midpoint of edge k -> k+1
    for (int k = 0; k < 4; k++){
        m[k] = (p[k] + p[(k + 1) % 4]) * 0.5f;
    }
    auto same = [&](int a, int b){
        return solid[a] && solid[b] && hi[a] == hi[b] && (bid[a] == bid[b] || bid[a] == ARCH || bid[b] == ARCH);
    };
    bool f_all_same = same(0,1) && same(1,2) && same(2,3) && same(3,0);

    for (int k = 0; k < 4; k++){
        if (!solid[k]){
            continue;
        }
        int plot = quad.v[k];
        float eave = hi[k] * HALF;
        int next = (k + 1) % 4;
        int prev = (k + 3) % 4;
        uint32_t look = MeshHash((uint32_t)plot,0x40115Eu);
        //An arch takes the look of the building beside it it is bridging from: the first solid
        //corner of this cell that is a building.
        uint32_t owner = bid[k];
        for (int j = 0; j < 4 && owner == ARCH; j++){
            if (solid[j] && bid[j] != ARCH){
                owner = bid[j];
            }
        }
        int kind = (owner != ARCH && owner < z.buildings.size()) ? z.buildings[owner].kind : ZONE_KIND_HOUSE;
        const KindLook& kl = kind_looks[kind];
        //One roof colour for a whole building, by its id.
        uint32_t building_look = MeshHash(owner,0xB017D1u);
        int roof_column = ((building_look % 10) < 7) ? kl.roof[0] : kl.roof[1];

        //--- The roof: the quarter P, M1, C, M0, each point at the eave or raised to the ridge ---
        //Raised where the house carries on past it at the same height: always over the plot's own
        //vertex; at an edge midpoint when the corner across it is as tall; at the cell's centre when
        //all four corners are. Each point is decided by the corners it lies between and nothing
        //else, so the cells on either side of it agree.
        vec3 P(p[k].x,eave + ROOF_RISE,p[k].y);
        vec3 M1(m[k].x,eave + (same(k,next) ? ROOF_RISE : 0.0f),m[k].y);
        vec3 M0(m[prev].x,eave + (same(k,prev) ? ROOF_RISE : 0.0f),m[prev].y);
        vec3 C(centre.x,eave + (f_all_same ? ROOF_RISE : 0.0f),centre.y);
        MeshTri(out,P,M1,C,up,roof_column);
        MeshTri(out,P,C,M0,up,roof_column);

        //--- An arch's ceiling: the passage's roof, under the bridge's floor ---------------------------
        if (lo[k] > foot[k]){
            float y = lo[k] * HALF;
            MeshQuad(out,vec3(p[k].x,y,p[k].y),vec3(m[k].x,y,m[k].y),vec3(centre.x,y,centre.y),
                     vec3(m[prev].x,y,m[prev].y),vec3(0.0f,-1.0f,0.0f),PAL_TIMBER);
        }

        /*
            --- The walls up the quarter's two outer edges, wherever this corner is solid and the one
            across is not: this corner's solid less the neighbour's. That is at most two pieces - BELOW
            the neighbour's solid (a house's side of an arch's passage, or where the neighbour stands
            higher up the slope) and ABOVE it (where the neighbour is lower). Only the upper piece meets
            the roof and gets a gable.
        */
        int neighbour[2] = {next,prev};
        vec2 edge_a[2] = {m[k],centre};
        vec2 edge_b[2] = {centre,m[prev]};
        for (int e = 0; e < 2; e++){
            int j = neighbour[e];
            int pieces[2][2];   //half-storey ranges [from, to)
            int n_pieces = 0;
            if (!solid[j]){
                pieces[n_pieces][0] = lo[k];
                pieces[n_pieces++][1] = hi[k];
            }else{
                if (lo[j] > lo[k]){
                    pieces[n_pieces][0] = lo[k];
                    pieces[n_pieces++][1] = std::min(hi[k],lo[j]);
                }
                if (hi[j] < hi[k]){
                    pieces[n_pieces][0] = std::max(lo[k],hi[j]);
                    pieces[n_pieces++][1] = hi[k];
                }
            }
            vec2 a = edge_a[e];
            vec2 b = edge_b[e];
            vec2 along = b - a;
            vec2 side(-along.y,along.x);
            if (side.dot(p[k] - (a + b) * 0.5f) > 0.0f){
                side = -side;
            }
            side = side / std::max(1e-6f,side.length());
            vec3 outward(side.x,0.0f,side.y);
            bool f_into_arch = solid[j] && lo[j] > foot[j];
            for (int pc = 0; pc < n_pieces; pc++){
                int from = pieces[pc][0];
                int to = pieces[pc][1];
                if (to <= from){
                    continue;
                }
                //A wall from this house's own ground goes on down below it, into the slope and under
                //the ground's bump: no gap shows under a house on uneven ground.
                float bottom = (from == foot[k]) ? from * HALF - HOUSE_FOOTING : from * HALF;
                float top = to * HALF;
                MeshQuad(out,vec3(a.x,bottom,a.y),vec3(b.x,bottom,b.y),vec3(b.x,top,b.y),vec3(a.x,top,a.y),
                         outward,kl.wall);
                //The gable: where the ridge carries on along this edge's far end, the wall closes up to
                //the roof's raised point rather than leaving a hole under it.
                vec3 roof_a = (e == 0) ? M1 : C;
                vec3 roof_b = (e == 0) ? C : M0;
                if (to == hi[k] && (roof_a.y > eave || roof_b.y > eave)){
                    MeshQuad(out,vec3(a.x,eave,a.y),vec3(b.x,eave,b.y),roof_b,roof_a,outward,kl.wall);
                }
                /*
                    A window on every storey of this house the piece shows whole; on the ground floor,
                    sometimes a door - and always one into an arch's passage, where a house beside a
                    road is most likely entered.
                */
                for (int st = 0; st < s[k]; st++){
                    int floor_half = foot[k] + 2 * st;
                    if (floor_half < from || floor_half + 2 > to){
                        continue;
                    }
                    float floor_y = floor_half * HALF;
                    uint32_t wall_hash = MeshHash((uint32_t)plot,(uint32_t)fine_quad,(uint32_t)e);
                    bool f_door = (st == 0) && (f_into_arch || wall_hash % (uint32_t)kl.door_chance == 0);
                    if (f_door){
                        Opening(out,a,b,outward,0.5f,kl.door_w * 0.5f,floor_y,floor_y + DOOR_H,
                                (kl.wall == PAL_BARK_DARK) ? PAL_STONE_DARK : PAL_BARK_DARK,kl.wall,false);
                    }else if ((wall_hash >> 8) % (uint32_t)kl.window_every == 0){
                        float y0 = floor_y + ZONE_STOREY_HEIGHT * WINDOW_SILL;
                        Opening(out,a,b,outward,0.5f,WINDOW_W * 0.5f,y0,y0 + WINDOW_H,
                                (kl.wall == PAL_TIMBER) ? PAL_BARK_DARK : PAL_TIMBER,kl.wall,true);
                    }
                }
            }
        }

        //--- A chimney on some plots of a kind that has them, built by one of the plot's quarters ----
        if (lo[k] == foot[k] && (look >> 8) % 100 < (uint32_t)kl.chimney_chance){
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
