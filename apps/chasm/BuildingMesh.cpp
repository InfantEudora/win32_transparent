#include "BuildingMesh.h"
#include "ZoneMesh.h"
#include "MeshBuild.h"
#include "Walkers.h"

#include <algorithm>
#include <cmath>

#define FOOTING_DEPTH       0.15f   //how far a wall runs on into the ground, under the lowest of it beneath the wall
#define ROOF_RISE           0.85f   //ridge above the eaves
#define WINDOW_W            0.30f   //of a wall segment's width... in world units, capped by the segment
#define WINDOW_H            0.36f
#define WINDOW_SILL         0.42f   //of a storey's height
#define DOOR_W              0.34f
#define DOOR_H              0.72f
#define DOOR_CHANCE         4       //one ground-floor segment in this many gets a door
#define CHIMNEY_CHANCE      45      //percent of plots, of a kind with chimneys but not a house's one
#define CHIMNEY_W           0.22f
#define CHIMNEY_TOP         0.35f   //a chimney's top above the ridge
#define CHIMNEY_SIDE_SHARE  50      //percent of houses whose chimney is a stack up a wall rather than through the roof
#define CONDENSER_TANK_W    0.44f   //a condenser's brass tank through the roof, in a chimney's place (steam_plan.md)...
#define CONDENSER_TANK_TOP  0.55f   //...its top above the ridge
#define STACK_BASE_W        0.50f   //the stack's base along the wall...
#define STACK_BASE_D        0.30f   //...and out from it
#define STACK_FLUE_W        0.30f   //its flue above the eaves
#define STACK_FLUE_D        0.24f
#define STACK_CAP           0.07f   //the flue's cap, a little wider all round
#define FACE_OUT            0.012f  //windows and doors stand this far off their wall

bool BuildingUpperDoorAt(const ChasmWorld& w, const ZoneState& z, int b, int u, int& quad, vec2& at){
    if (ZoneUpperDoor(w,z,b,u) < 0){
        return false;
    }
    const GridPicker& picker = *w.picker;
    const Grid& g = *w.grid;
    quad = -1;
    for (int i = 0; i < picker.PlotQuadCount(b); i++){
        int q = picker.PlotQuadCorner(b,i) / 4;
        const GridQuad& gq = g.fine.quads[q];
        if ((gq.v[0] == u || gq.v[1] == u || gq.v[2] == u || gq.v[3] == u) && (quad < 0 || q < quad)){
            quad = q;
        }
    }
    if (quad < 0){
        return false;
    }
    at = (BuildingWallCrossing(w,b,u) + g.FineQuadCentre(quad)) * 0.5f;
    return true;
}

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
    {PAL_TIMBER,{PAL_ROOF,PAL_ROOF},2,DOOR_W * 1.6f,2,0},                 //builders' hut: planks under tile, beams and a ladder by it
    {PAL_TIMBER,{PAL_FIELD,PAL_FIELD},2,DOOR_W,2,0},                      //range: on cells, drawn by CropMesh
    {PAL_BARK,{PAL_TIMBER,PAL_TIMBER},2,DOOR_W,2,0},                      //guard tower: drawn by BuildTower instead
    {PAL_WALL,{PAL_FIELD,PAL_FIELD},2,DOOR_W * 1.4f,1,0},                 //ropewalk: plaster under thatch, a window every wall
    {PAL_STONE,{PAL_STONE_DARK,PAL_BARK_DARK},2,DOOR_W * 1.6f,2,0},       //stonecutter's hut: its own stone under slate or shingle
    {PAL_TIMBER,{PAL_TIMBER,PAL_TIMBER},2,DOOR_W,2,0},                    //mine: its portal, a prop of its own (ApplicationChasmMines.cpp)
    {PAL_STONE,{PAL_ROOF,PAL_ROOF},2,DOOR_W,2,0},                         //well: drawn by BuildWell instead
    {PAL_TIMBER,{PAL_STONE_DARK,PAL_STONE_DARK},2,DOOR_W,2,0},           //steam collector: planks under slate, its hood out over the drop
    {PAL_STONE_LIGHT,{PAL_TIMBER,PAL_TIMBER},2,DOOR_W,2,100},            //condenser: a water collector's stone, a brass tank on every plot
    {PAL_BARK_DARK,{PAL_STONE_DARK,PAL_STONE_DARK},2,DOOR_W * 1.6f,1,0}, //sail loft: tarred planks under slate, so its pale canvas shows;
                                                                          //wide doors, and a window every wall for the sewing
};
static_assert(sizeof(kind_looks) / sizeof(kind_looks[0]) == ZONE_KIND_COUNT,"a look for every kind");

/*
    A beam from a to b on the ground plane, `thick` across, from y0 up by `height`: its two long sides,
    its top, its bottom and its two ends. Posts, a deck, a gantry, a rope.
*/
void Beam(std::vector<vertex>& out, const vec2& a, const vec2& b, float y0, float height, float thick, int column,
          int row = PAL_TEMPERATE){
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
    MeshQuad(out,al,bl,blt,alt,n_side,column,row);
    MeshQuad(out,ar,br,brt,art,n_side * -1.0f,column,row);
    MeshQuad(out,alt,blt,brt,art,vec3(0,1,0),column,row);
    MeshQuad(out,al,ar,br,bl,vec3(0,-1,0),column,row);
    MeshQuad(out,al,ar,art,alt,vec3(-dir.x,0.0f,-dir.y),column,row);
    MeshQuad(out,bl,br,brt,blt,vec3(dir.x,0.0f,dir.y),column,row);
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

#define WELL_OFFSET         0.6f    //the ring's middle from the plot's point: who comes for water stands at the point, beside it
#define WELL_RADIUS         0.36f
#define WELL_RING_HEIGHT    0.55f
#define WELL_RING_SIDES     8
#define WELL_EAVES          1.45f   //above the ground
#define WELL_RIDGE          1.9f    //...and its roof's ridge: SHAPE_WELL (ApplicationChasmSelect.cpp)
#define WELL_WINDLASS       1.1f

/*
    A WELL (water_tools_plan.md) - a stand-in, while wells are a debug tool: a ring of stone with the dark water in it,
    two posts with a windlass and its rope, a little gabled roof over them, and a bucket on the rim. Off the plot's
    point by WELL_OFFSET, the way its hash turns it, so whoever comes to draw stands beside it rather than in it.
*/
void BuildWell(const ChasmWorld& w, int plot, std::vector<vertex>& out){
    vec2 p = w.grid->fine.pos[plot];
    float y = w.terrain->ground[plot];
    float yaw = (float)(MeshHash((uint32_t)plot,0x3E11u) % 6283u) * 0.001f;
    vec2 dir(std::cos(yaw),std::sin(yaw));
    vec2 side(-dir.y,dir.x);
    vec2 c = p + dir * WELL_OFFSET;
    //The ring, a stone beam along each side - a little long, so the corners close - and the water's dark inside it.
    const float step = 6.2831853f / WELL_RING_SIDES;
    const float stretch = 1.08f;
    vec3 rim[WELL_RING_SIDES];
    for (int i = 0; i < WELL_RING_SIDES; i++){
        vec2 a = c + vec2(std::cos(step * i),std::sin(step * i)) * WELL_RADIUS;
        vec2 b = c + vec2(std::cos(step * (i + 1)),std::sin(step * (i + 1))) * WELL_RADIUS;
        vec2 m = (a + b) * 0.5f;
        Beam(out,m + (a - m) * stretch,m + (b - m) * stretch,y - 0.1f,WELL_RING_HEIGHT + 0.1f,0.14f,PAL_STONE);
        rim[i] = vec3(a.x,y + WELL_RING_HEIGHT - 0.15f,a.y);
    }
    vec3 mid(c.x,y + WELL_RING_HEIGHT - 0.15f,c.y);
    for (int i = 0; i < WELL_RING_SIDES; i++){
        MeshTri(out,mid,rim[i],rim[(i + 1) % WELL_RING_SIDES],vec3(0,1,0),PAL_BARK_DARK);
    }
    //Two posts either side, the windlass across them, and its rope down into the water.
    vec2 post_a = c + side * (WELL_RADIUS + 0.02f);
    vec2 post_b = c - side * (WELL_RADIUS + 0.02f);
    Upright(out,post_a,0.1f,y,y + WELL_EAVES,PAL_BARK);
    Upright(out,post_b,0.1f,y,y + WELL_EAVES,PAL_BARK);
    Beam(out,post_a,post_b,y + WELL_WINDLASS - 0.05f,0.1f,0.1f,PAL_TIMBER);
    Upright(out,c,0.03f,y + WELL_RING_HEIGHT - 0.15f,y + WELL_WINDLASS,PAL_BARK_DARK);
    //The roof: two slopes from the eaves, front and back, to the ridge along the windlass - each drawn from under too.
    const float half_len = WELL_RADIUS + 0.2f;
    const float reach = WELL_RADIUS + 0.06f;
    vec3 ridge_a(post_a.x + side.x * 0.18f,y + WELL_RIDGE,post_a.y + side.y * 0.18f);
    vec3 ridge_b(post_b.x - side.x * 0.18f,y + WELL_RIDGE,post_b.y - side.y * 0.18f);
    for (int s = -1; s <= 1; s += 2){
        vec2 ea = c + side * half_len + dir * (reach * (float)s);
        vec2 eb = c - side * half_len + dir * (reach * (float)s);
        vec3 eave_a(ea.x,y + WELL_EAVES,ea.y);
        vec3 eave_b(eb.x,y + WELL_EAVES,eb.y);
        vec3 up(dir.x * (float)s,reach / (WELL_RIDGE - WELL_EAVES),dir.y * (float)s);
        MeshQuad(out,eave_a,eave_b,ridge_b,ridge_a,up,PAL_ROOF);
        MeshQuad(out,eave_a,eave_b,ridge_b,ridge_a,up * -1.0f,PAL_TIMBER);
    }
    //A bucket on the rim, on the side its water is drawn from.
    vec2 bucket = c - dir * (WELL_RADIUS - 0.02f) + side * 0.12f;
    Beam(out,bucket - dir * 0.08f,bucket + dir * 0.08f,y + WELL_RING_HEIGHT,0.16f,0.16f,PAL_TIMBER);
}

#define TENT_LENGTH         1.25f   //along its ridge
#define TENT_HALF_W         0.55f   //from the ridge out to a side's foot
#define TENT_HEIGHT         0.85f
#define FIRE_RING           0.26f   //the stones' circle
#define FIRE_STONES         7

/*
    A CAMP's FIRE (Zones.h, THE CAMP), in a cell of open ground beside a tent (ZoneCampFireTent): a ring of
    stones round two crossed logs and a flame, with a log to sit on. Drawn by its cell, not its tent, so it is
    drawn again whenever anything is built on a corner of the cell.
*/
void Box(std::vector<vertex>& out, const vec2& p, float w, float y0, float y1, int column, int row = PAL_TEMPERATE);

void BuildCampFire(const ChasmWorld& w, int fine_quad, int tent, std::vector<vertex>& out){
    vec2 c = ZoneCampFireAt(w,fine_quad,tent);
    vec2 dir = c - w.grid->fine.pos[tent];
    float len = dir.length();
    dir = (len > 1e-4f) ? dir * (1.0f / len) : vec2(1.0f,0.0f);
    vec2 side(-dir.y,dir.x);
    float y = w.terrain->GroundHeight(c,w.terrain->Height(tent));
    //Stones round the fire, then the logs crossed in it, then the flame - red, and yellow in its heart.
    float yaw = std::atan2(dir.y,dir.x);
    for (int i = 0; i < FIRE_STONES; i++){
        float a = yaw + 6.2831853f * (float)i / FIRE_STONES;
        Box(out,c + vec2(std::cos(a),std::sin(a)) * FIRE_RING,0.11f,y - 0.04f,y + 0.08f,PAL_STONE);
    }
    Beam(out,c - dir * 0.2f,c + dir * 0.2f,y,0.06f,0.07f,PAL_BARK_DARK);
    Beam(out,c - side * 0.2f,c + side * 0.2f,y + 0.04f,0.06f,0.07f,PAL_BARK_DARK);
    auto flame = [&](float half, float top, int column){
        vec3 apex(c.x,y + top,c.y);
        vec3 q3[4];
        for (int i = 0; i < 4; i++){
            vec2 q = c + (i & 1 ? side : dir) * ((i & 2) ? -half : half);
            q3[i] = vec3(q.x,y + 0.07f,q.y);
        }
        vec2 corner[4] = {dir,side,dir * -1.0f,side * -1.0f};
        for (int i = 0; i < 4; i++){
            vec2 o = corner[i] + corner[(i + 1) % 4];
            MeshTri(out,q3[i],q3[(i + 1) % 4],apex,vec3(o.x,0.4f,o.y),column);
        }
    };
    flame(0.13f,0.42f,PAL_ACCENT);
    flame(0.07f,0.54f,PAL_FIELD);
    //A log to sit on, beyond the fire from the tent.
    vec2 seat = c + dir * 0.55f;
    Beam(out,seat - side * 0.32f,seat + side * 0.32f,y,0.13f,0.15f,PAL_BARK);
}

/*
    A CAMP's plot (Zones.h, THE CAMP): a tent - an A-frame of canvas on a ridge pole, its door a dark
    triangle in the front end. Each faces its own way and some are a paler canvas, by the plot's hash, so a
    camp looks pitched by hand rather than laid out.
*/
void BuildCampPlot(const ChasmWorld& w, int plot, std::vector<vertex>& out){
    vec2 p = w.grid->fine.pos[plot];
    float y = w.terrain->ground[plot];
    uint32_t h = MeshHash((uint32_t)plot,0xCA4Bu);
    float yaw = ZoneCampYaw(plot);      //the way its people step out of it agrees (ZoneCampDoor)
    vec2 dir(std::cos(yaw),std::sin(yaw));
    vec2 side(-dir.y,dir.x);
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
             float half_w, float y0, float y1, int column, int surround_column, bool f_sill,
             BuildingPane* pane = NULL){
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
    if (pane){
        pane->corner[0] = vec3(l.x,y0,l.y);
        pane->corner[1] = vec3(r.x,y0,r.y);
        pane->corner[2] = vec3(r.x,y1,r.y);
        pane->corner[3] = vec3(l.x,y1,l.y);
        pane->outward = outward;
    }
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

/*
    STEPS DOWN FROM A DOOR (terraces_plan.md): where a door stands over the ground outside it - on a foundation's
    plinth, or on a floor rounded up a little - a short flight of stone straight out from it, each step a block from
    a little into the ground up to its tread, one STEP_RISE under the last. Each is measured against the ground under
    it rather than the door's, since downhill the ground goes on falling away: the flight ends at the first step that
    would stand less than STEP_LEAST over its own ground. `half_w` is the door's, as Opening was given it; `beyond` the
    plot across the wall, whose ground they go down to.
*/
#define STEP_RISE           0.18f
#define STEP_TREAD          0.20f
#define STEP_SIDE           0.06f   //past the door's jambs, each side
#define STEP_LEAST          0.07f
#define STEP_MOST           9

void DoorSteps(std::vector<vertex>& out, const ChasmWorld& w, const ZoneState& z, int beyond, const vec2& a, const vec2& b,
               const vec3& outward, float half_w, float floor_y, int column){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-4f){
        return;
    }
    dir = dir / len;
    float half = std::min(half_w,len * 0.38f) + SURROUND_W + STEP_SIDE;      //Opening's door, its jambs, and a little
    vec2 c = (a + b) * 0.5f;
    vec2 out2(outward.x,outward.z);
    //Down onto `beyond`, the plot across the wall: its ground - a terrace's top, if it is one (terraces_plan.md).
    auto ground = [&](float d){
        return std::min(ZoneGroundHeight(w,z,beyond,c + out2 * d - dir * half),ZoneGroundHeight(w,z,beyond,c + out2 * d + dir * half));
    };
    for (int i = 0; i < STEP_MOST; i++){
        float d0 = i * STEP_TREAD;
        float top = floor_y - (i + 1) * STEP_RISE;
        float g = std::min(ground(d0),ground(d0 + STEP_TREAD));
        if (top - g < STEP_LEAST){
            break;
        }
        WallSlab(out,c + out2 * d0,dir,out2,outward,-half,half,g - 0.05f,top,STEP_TREAD,column);
    }
}

/*
    A BREACHED door (zombies_plan.md, step 4d): the doorway Opening drew, `half_w` either side of the middle of a->b
    from y0 to y1, gone dark - and its leaf standing open out of it, turned out about one jamb (which, by `hash`) and
    hanging a little off its hinges.
*/
#define DOOR_LEAF_THICK     0.05f
#define DOOR_OPEN_SWING     1.25f   //radians out from the wall, and up to DOOR_OPEN_SWING_MORE more, by its hash
#define DOOR_OPEN_SWING_MORE 0.60f
#define DOOR_OPEN_SAG       0.07f   //its far end dropped, radians

void BreachedDoor(std::vector<vertex>& out, const vec2& a, const vec2& b, const vec3& outward, float half_w,
                  float y0, float y1, int column, uint32_t hash){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-4f){
        return;
    }
    dir = dir / len;
    half_w = std::min(half_w,len * 0.38f);      //as Opening has it
    vec2 c = (a + b) * 0.5f;
    vec2 out2(outward.x,outward.z);
    vec2 l = c - dir * half_w + out2 * (FACE_OUT * 2.0f);
    vec2 r = c + dir * half_w + out2 * (FACE_OUT * 2.0f);
    MeshQuad(out,vec3(l.x,y0,l.y),vec3(r.x,y0,r.y),vec3(r.x,y1,r.y),vec3(l.x,y1,l.y),outward,PAL_VOID,PAL_EFFECTS);
    //The leaf: built shut across the doorway just in front of it, then turned on its hinge.
    bool f_left = (hash & 1) != 0;
    vec2 hinge = (f_left ? l : r) + out2 * (DOOR_LEAF_THICK * 0.5f);
    vec2 across = f_left ? dir : -dir;
    size_t first = out.size();
    Beam(out,hinge,hinge + across * (2.0f * half_w),y0,y1 - y0,DOOR_LEAF_THICK,column);
    const vec3 up(0.0f,1.0f,0.0f);
    vec3 pivot(hinge.x,y0,hinge.y);
    MeshTurn(out,first,pivot,up.cross(vec3(across.x,0.0f,across.y)),DOOR_OPEN_SAG);
    //About up a turn takes `across` toward (across.y, -across.x): out of the wall, whichever side that is.
    float swing = DOOR_OPEN_SWING + DOOR_OPEN_SWING_MORE * (float)((hash >> 1) % 100) / 100.0f;
    MeshTurn(out,first,pivot,up,(out2.dot(vec2(across.y,-across.x)) > 0.0f) ? swing : -swing);
    MeshShift(out,first,vec3(0.0f,2.0f * half_w * std::sin(DOOR_OPEN_SAG) * 0.5f,0.0f));
}

//A beam of square section from a at height ya to b at height yb: a pipe running down a slope.
void SlopedBeam(std::vector<vertex>& out, const vec2& a, float ya, const vec2& b, float yb, float thick, int column,
                int row = PAL_TEMPERATE){
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
    MeshQuad(out,al + up,bl + up,br + up,ar + up,vec3(0,1,0),column,row);
    MeshQuad(out,al - up,ar - up,br - up,bl - up,vec3(0,-1,0),column,row);
    MeshQuad(out,al - up,bl - up,bl + up,al + up,n_side,column,row);
    MeshQuad(out,ar - up,ar + up,br + up,br - up,n_side * -1.0f,column,row);
    MeshQuad(out,al - up,al + up,ar + up,ar - up,vec3(-d.x / l,0.0f,-d.y / l),column,row);
    MeshQuad(out,bl - up,br - up,br + up,bl + up,vec3(d.x / l,0.0f,d.y / l),column,row);
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

#define HOOD_BOOM           1.7f    //the boom's height over the plot's ground
#define HOOD_PAST_LIP       1.5f    //how far past the lip the hood hangs, out over the drop
#define HOOD_WIDE           1.0f    //its mouth, open below to the updraft...
#define HOOD_NECK           0.45f   //...narrowing to its neck

/*
    A STEAM COLLECTOR's hood (steam_plan.md): from its wall a boom on a post at the lip, out over the drop, and hanging
    at its end an iron hood open below to the updraft, its neck piped back up to the shed's roof - the cowl that tells it
    from any shed on the rim. Toward its lower corners, the drop; the lip halfway to the nearest, as a winch's is. The
    hood in brass, its mouth tarnished darker (PAL_METALS).
*/
//The hood's way out over the drop from `plot` - toward its lower corners - and where its lip and its end are.
static bool SteamHoodPlace(const ChasmWorld& w, int plot, vec2& dir, vec2& lip, vec2& end){
    const Terrain& t = *w.terrain;
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    vec2 p = g.fine.pos[plot];
    vec2 sum(0.0f,0.0f);
    float nearest = 1e30f;
    for (int i = 0; i < picker.PlotQuadCount(plot); i++){
        const GridQuad& q = g.fine.quads[picker.PlotQuadCorner(plot,i) / 4];
        for (int k = 0; k < 4; k++){
            if (t.steps[q.v[k]] > t.steps[plot]){
                vec2 d = g.fine.pos[q.v[k]] - p;
                sum = sum + d;
                nearest = std::min(nearest,d.length());
            }
        }
    }
    float l = sum.length();
    if (l < 1e-4f || nearest > 1e29f){
        return false;
    }
    dir = sum * (1.0f / l);
    lip = p + dir * (nearest * 0.5f);
    end = lip + dir * HOOD_PAST_LIP;
    return true;
}

}

//Out of the file's own namespace: the plumes ask it (ApplicationChasmHearth.cpp).
bool SteamHoodTop(const ChasmWorld& w, int plot, vec3& top){
    vec2 dir;
    vec2 lip;
    vec2 end;
    if (plot < 0 || plot >= (int)w.grid->fine.pos.size() || !SteamHoodPlace(w,plot,dir,lip,end)){
        return false;
    }
    top = vec3(end.x,w.terrain->ground[plot] + HOOD_BOOM + 0.35f,end.y);
    return true;
}

//...and DoorSteps' reach: the walkers' figures go down them (ApplicationChasmWorkersView.cpp).
float BuildingDoorStepsRun(float drop){
    int n = 0;
    while (n < STEP_MOST && drop - (n + 1) * STEP_RISE >= STEP_LEAST){
        n++;
    }
    return n * STEP_TREAD;
}

namespace {

void BuildSteamHood(const ChasmWorld& w, int plot, std::vector<vertex>& out){
    vec2 dir;
    vec2 lip;
    vec2 end;
    if (!SteamHoodPlace(w,plot,dir,lip,end)){
        return;
    }
    vec2 p = w.grid->fine.pos[plot];
    float y0 = w.terrain->ground[plot];
    float boom = y0 + HOOD_BOOM;
    Upright(out,lip,0.14f,y0 - 0.4f,boom,PAL_BARK);
    Beam(out,p + dir * PIPE_WALL,end + dir * 0.2f,boom,0.16f,0.18f,PAL_BARK);
    //The hood: a wide mouth under the boom's end, its neck above, and the pipe from the neck back to the roof.
    Box(out,end,HOOD_WIDE,boom - 0.95f,boom - 0.55f,PAL_BRASS_DARK,PAL_METALS);
    Box(out,end,HOOD_NECK,boom - 0.55f,boom + 0.35f,PAL_BRASS,PAL_METALS);
    SlopedBeam(out,end - dir * 0.1f,boom + 0.3f,p + dir * (PIPE_WALL - 0.3f),boom + 0.75f,PIPE_THICK * 1.4f,PAL_BRASS,
               PAL_METALS);
}

//A box standing on (x, z) from y0 to y1, `w` across, square to the world axes - a chimney.
void Box(std::vector<vertex>& out, const vec2& p, float w, float y0, float y1, int column, int row){
    float h = w * 0.5f;
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3(p.x + ((i & 1) ? h : -h),(i & 4) ? y1 : y0,p.y + ((i & 2) ? h : -h));
    }
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,1,0),column,row);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,0,-1),column,row);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,0,1),column,row);
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column,row);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column,row);
}

#define YARD_BEAM           0.13f   //a builders' hut's beams, square
#define YARD_BEAM_LONG      1.1f
#define LADDER_W            0.34f
#define LADDER_RAIL         0.06f
#define LADDER_RUNG_EVERY   0.28f
#define LADDER_LEAN         0.55f   //its foot, out from the wall

bool LowestStandingPlot(const ChasmWorld& w, const ZoneState& z, int plot);

/*
    A HOUSE'S CHIMNEY (needs_plan.md step 3, the user's 2026-10-08): every house has one, on its lowest
    standing plot, of one of two kinds by the house's hash - through the roof, partway from the ridge, as
    other kinds' chimneys are; or a stone STACK up the outside of a wall, on the first of the plot's quarters
    (counting from one by the plot's hash) with a whole outside wall - the corner across it with nothing
    standing, and no terrace's wall - whose window it takes the place of. A house with no such wall has a roof chimney. Decided
    from the zones alone, so every cell round the plot agrees which of them builds it: `quad` is that cell,
    `edge` the wall (0 toward the quarter's next corner, 1 its previous), -1 for a roof chimney.
*/
struct ChimneyPlace{
    int quad = -1;
    int edge = -1;
};

//Whether corner v stands as a house body in BuildHouseCell: its storeys, of a kind with a body (ZoneKindHasBody -
//not a winch, camp, bridge or guard tower, which are drawn apart), or an arch's.
bool CornerSolid(const ChasmWorld& w, const ZoneState& z, int v){
    (void)w;
    return (z.standing[v] > 0 && ZoneKindHasBody(z.KindOf(v))) || z.ArchBuiltOf(v) > 0;
}

bool HouseChimney(const ChasmWorld& w, const ZoneState& z, int plot, ChimneyPlace& at){
    uint32_t id = z.building[plot];
    if (!id || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_HOUSE || z.standing[plot] == 0
        || !LowestStandingPlot(w,z,plot)){
        return false;
    }
    const GridPicker& picker = *w.picker;
    const Grid& g = *w.grid;
    int n = picker.PlotQuadCount(plot);
    if (n <= 0){
        return false;
    }
    uint32_t look = MeshHash((uint32_t)plot,0x40115Eu);
    int chosen = (int)((look >> 16) % (uint32_t)n);
    at.quad = picker.PlotQuadCorner(plot,chosen) / 4;
    at.edge = -1;
    if (MeshHash(id,0xC41A7u) % 100 >= (uint32_t)CHIMNEY_SIDE_SHARE){
        return true;
    }
    for (int i = 0; i < n; i++){
        int qc = picker.PlotQuadCorner(plot,(chosen + i) % n);
        const GridQuad& q = g.fine.quads[qc / 4];
        int k = qc % 4;
        int across[2] = {q.v[(k + 1) % 4],q.v[(k + 3) % 4]};
        for (int e = 0; e < 2; e++){
            //Not up a retaining wall (ZoneTerraceStep): buried in a terrace above, hung over the drop below one, or in front
            //of an upper door.
            if (!CornerSolid(w,z,across[e]) && ZoneTerraceStep(w,z,plot,across[e]) == ZONE_STEP_OPEN){
                at.quad = qc / 4;
                at.edge = e;
                return true;
            }
        }
    }
    return true;
}

//The lowest plot of a building that stands - the one its one-a-building parts are drawn on.
bool LowestStandingPlot(const ChasmWorld& w, const ZoneState& z, int plot){
    uint32_t id = z.building[plot];
    std::vector<int> seen(1,plot);
    std::vector<int> stack(1,plot);
    while (!stack.empty()){
        int at = stack.back();
        stack.pop_back();
        if (at < plot && z.standing[at] > 0){
            return false;
        }
        int nb[8];
        int n = ZonePlotNeighbours(w,at,nb,8);
        for (int i = 0; i < n; i++){
            if (z.building[nb[i]] == id && std::find(seen.begin(),seen.end(),nb[i]) == seen.end()){
                seen.push_back(nb[i]);
                stack.push_back(nb[i]);
            }
        }
    }
    return true;
}

/*
    A DOOR in a building's wall from `plot` toward `across` - the wall `edge` of `plot`'s quarter in cell `quad`, as
    ChimneyPlace's - only where it leads somewhere people could go: open ground or an arch's passage over this floor, and
    no retaining wall between (ZoneTerraceStep, the walkers' own rule). Beside a terrace a door was drawn into its fill
    (the user's, 2026-10-23), and on a terrace's edge it would open onto the drop. On the ground floor, where the wall's
    hash gives it one in `door_chance` - an arch's passage always, a house beside a road being most likely entered from
    it. Against a terrace's wall, on the storey that opens onto its top (ZoneUpperDoor, stage 3), always - on the one
    piece of the wall where people are drawn coming and going (BuildingUpperDoorAt). `storey` is the door's.
*/
enum{
    WALL_DOOR_NONE,
    WALL_DOOR_CAN,
    WALL_DOOR_DRAWN,
};

int WallDoor(const ChasmWorld& w, const ZoneState& z, int plot, int across, int quad, int edge, int door_chance,
             int& storey){
    storey = 0;
    //Never over a cliff - the chasm's rim, where a building on the edge drew its steps all the way down to the floor (the
    //user's, 2026-10-23) - nor onto water or the mountain: no one walks there (Walkers::EdgeOpen).
    const Terrain& t = *w.terrain;
    if (!t.SameGround(plot,across) || t.wet[across] || t.Mountain(across)){
        return WALL_DOOR_NONE;
    }
    int step = ZoneTerraceStep(w,z,plot,across);
    if (step == ZONE_STEP_WALL){
        return WALL_DOOR_NONE;
    }
    if (step == ZONE_STEP_DOOR){
        int cell = -1;
        vec2 at;
        if (!BuildingUpperDoorAt(w,z,plot,across,cell,at)){
            return WALL_DOOR_NONE;      //the door is the other way: this plot is the terrace's
        }
        storey = ZoneUpperDoor(w,z,plot,across);
        return (cell == quad) ? WALL_DOOR_DRAWN : WALL_DOOR_NONE;
    }else if (z.standing[across] == 0 && z.ArchBuiltOf(across) > 0){
        //Into its passage, under its floor (ZoneArchStoreys): a whole storey of this building's under it.
        int road = BuildingFootHalves(w,z,ZONE_KIND_NONE,across);
        bool f_under = z.ArchFloorOf(across,road + 2) >= BuildingFootHalves(w,z,z.KindOf(plot),plot) + 2;
        return f_under ? WALL_DOOR_DRAWN : WALL_DOOR_NONE;
    }else if (CornerSolid(w,z,across)){
        return WALL_DOOR_NONE;      //another building, or this one standing higher: into its wall
    }
    return (MeshHash((uint32_t)plot,(uint32_t)quad,(uint32_t)edge) % (uint32_t)door_chance == 0) ? WALL_DOOR_DRAWN
                                                                                                  : WALL_DOOR_CAN;
}

/*
    A building's SURE DOOR: by their hashes alone, none of a building's ground-floor walls might draw a door - one small
    house in ten had none, and more beside a terrace, whose walls take none. One that drew none gets one on the first wall
    that can take one: its plots in index order, each one's quarters as the picker lists them, each quarter's two walls -
    never the chimney's stack's. An upper door onto a terrace (stage 3) is no way out at the ground: it still gets one.
    Decided from the zones alone, as the chimney is, so every cell agrees which builds it. `at`'s plot is -1 where none is
    needed, or none can be had (SHUT IN at the ground).
*/
struct DoorPlace{
    int plot = -1;
    int quad = -1;
    int edge = -1;
};

void BuildingSureDoor(const ChasmWorld& w, const ZoneState& z, int plot, DoorPlace& at){
    at = DoorPlace();
    uint32_t id = z.building[plot];
    if (!id || id >= z.buildings.size() || !ZoneKindHasBody(z.buildings[id].kind)){
        return;
    }
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    std::vector<int> plots(1,plot);
    for (size_t i = 0; i < plots.size(); i++){
        int nb[8];
        int n = ZonePlotNeighbours(w,plots[i],nb,8);
        for (int j = 0; j < n; j++){
            if (z.building[nb[j]] == id && std::find(plots.begin(),plots.end(),nb[j]) == plots.end()){
                plots.push_back(nb[j]);
            }
        }
    }
    std::sort(plots.begin(),plots.end());
    int door_chance = kind_looks[z.buildings[id].kind].door_chance;
    ChimneyPlace chimney;
    int chimney_plot = -1;
    for (int v : plots){
        if (!CornerSolid(w,z,v)){
            continue;
        }
        if (chimney_plot < 0){
            chimney_plot = v;       //its lowest standing plot, where its chimney is (HouseChimney)
            HouseChimney(w,z,v,chimney);
        }
        int n = picker.PlotQuadCount(v);
        for (int i = 0; i < n; i++){
            int qc = picker.PlotQuadCorner(v,i);
            const GridQuad& q = g.fine.quads[qc / 4];
            int k = qc % 4;
            int across[2] = {q.v[(k + 1) % 4],q.v[(k + 3) % 4]};
            for (int e = 0; e < 2; e++){
                if (v == chimney_plot && chimney.quad == qc / 4 && chimney.edge == e){
                    continue;
                }
                int st = 0;
                int door = WallDoor(w,z,v,across[e],qc / 4,e,door_chance,st);
                if (door == WALL_DOOR_NONE || st > 0){
                    continue;
                }
                if (door == WALL_DOOR_DRAWN){
                    at = DoorPlace();
                    return;
                }
                if (at.plot < 0){
                    at.plot = v;
                    at.quad = qc / 4;
                    at.edge = e;
                }
            }
        }
    }
}

/*
    A BUILDERS' HUT's yard (builders_plan.md): out by the wall toward its first open neighbour, a stack of
    three beams - two side by side and one on them - and a ladder leant against the wall up to the eaves.
    What tells it from the woodcutter's hut. Once a building, on its lowest standing plot. The wall toward
    a neighbour sharing an edge crosses it at BuildingWallCrossing - halfway there, but on a star's edge.
*/
void BuildBuildersYard(const ChasmWorld& w, const ZoneState& z, int plot, std::vector<vertex>& out){
    const Terrain& t = *w.terrain;
    vec2 p = w.grid->fine.pos[plot];
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    int open = -1;
    for (int i = 0; i < n; i++){
        if (z.building[nb[i]] == 0 && !t.wet[nb[i]] && (open < 0 || nb[i] < open)){
            open = nb[i];
        }
    }
    if (open < 0){
        return;     //built in on every side: nowhere to lean a ladder
    }
    vec2 dir = w.grid->fine.pos[open] - p;
    float l = dir.length();
    if (l < 1e-4f){
        return;
    }
    dir = dir * (1.0f / l);
    vec2 side(-dir.y,dir.x);
    float wall = (BuildingWallCrossing(w,plot,open) - p).length();
    float level = t.Height(plot);
    //The beams, along the wall.
    vec2 c = p + dir * (wall + 0.35f) + side * 0.4f;
    float ground = ZoneGroundHeight(w,z,open,c);       //on the plot beside it - a terrace's top, if it is one
    for (int i = 0; i < 3; i++){
        float off = (i == 2) ? 0.0f : ((i == 0) ? -0.55f : 0.55f) * YARD_BEAM;
        float y = ground - 0.03f + ((i == 2) ? YARD_BEAM : 0.0f);
        Beam(out,c + dir * off - side * (YARD_BEAM_LONG * 0.5f),c + dir * off + side * (YARD_BEAM_LONG * 0.5f),
             y,YARD_BEAM,YARD_BEAM,PAL_TIMBER);
    }
    //The ladder: two rails from the ground up to the eaves, and rungs between.
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    float eave = BuildingFootHalves(w,z,z.KindOf(plot),plot) * HALF + z.standing[plot] * ZONE_STOREY_HEIGHT - 0.1f;
    vec2 at = p + dir * wall - side * 0.45f;
    vec2 foot = at + dir * LADDER_LEAN;
    float y0 = ZoneGroundHeight(w,z,open,foot);
    for (int r = -1; r <= 1; r += 2){
        vec2 o = side * (r * LADDER_W * 0.5f);
        SlopedBeam(out,foot + o,y0,at + dir * 0.04f + o,eave,LADDER_RAIL,PAL_BARK_DARK);
    }
    for (float y = y0 + LADDER_RUNG_EVERY; y < eave - 0.1f; y += LADDER_RUNG_EVERY){
        float f = (y - y0) / std::max(0.1f,eave - y0);
        vec2 q = foot + (at + dir * 0.04f - foot) * f;
        Beam(out,q - side * (LADDER_W * 0.5f),q + side * (LADDER_W * 0.5f),y - 0.02f,0.04f,0.04f,PAL_BARK_DARK);
    }
}

#define TOWER_LOG_W         0.16f   //its walls' logs, side by side - the palisade's, a little slimmer for the height
#define TOWER_LOG_SPACING   0.165f
#define TOWER_LOG_SINK      0.15f   //into the ground, so a slope never shows a gap under one
#define TOWER_CORNER_W      0.22f   //the corner posts, up through the platform to the parapet's top
#define TOWER_BAND          0.07f   //a dark band of lashing round it at each storey's top
#define TOWER_DECK          0.14f   //the platform's planks
#define TOWER_DECK_OUT      0.14f   //how far the platform reaches past the walls
#define TOWER_PARAPET       0.62f   //its stakes over the deck: chest-high on a villager (0.86)
#define TOWER_STAKE_W       0.11f
#define TOWER_STAKE_TIP     0.17f   //the sharpened top

//A sharpened stake at p from y0 up by `height`, square, its tip `tip` of that: the palisade's logs (RoadMesh).
void Stake(std::vector<vertex>& out, const vec2& p, float y0, float height, float width, float tip, int column){
    float body = height - tip;
    Upright(out,p,width,y0,y0 + body,column);
    float h = width * 0.5f;
    vec3 apex(p.x,y0 + height,p.y);
    vec3 c[4] = {vec3(p.x + h,y0 + body,p.y - h),vec3(p.x + h,y0 + body,p.y + h),
                 vec3(p.x - h,y0 + body,p.y + h),vec3(p.x - h,y0 + body,p.y - h)};
    for (int i = 0; i < 4; i++){
        vec3 mid = (c[i] + c[(i + 1) % 4]) * 0.5f;
        MeshTri(out,c[i],c[(i + 1) % 4],apex,vec3(mid.x - p.x,0.3f,mid.z - p.y),column);
    }
}

//The outward normal of the side a - b of a polygon round p.
vec2 SideOut(const vec2& p, const vec2& a, const vec2& b){
    vec2 d = b - a;
    float l = d.length();
    if (l < 1e-6f){
        return vec2(0.0f,0.0f);
    }
    vec2 n(d.y / l,-d.x / l);
    return (n.dot((a + b) * 0.5f - p) < 0.0f) ? n * -1.0f : n;
}

/*
    A GUARD TOWER (defence_plan.md): the palisade's look stood on end, in its JUNCTION's shape (ZoneTowerCorners) -
    a triangle, a square, a pentagon - as a house takes its plot's. Upright logs along each side, a storey of them
    for each storey that stands, with a band of lashing at each storey's top and a thick post at each corner. Whole,
    a platform of planks on top, a little wider than the walls, ringed by a PARAPET of sharpened stakes as the
    palisade's, the corner posts running up through it - and its LADDER, its door, up the side facing the plot it
    stands toward (ZoneBuilding::ladder). A wall beside it runs up to the side facing it (RoadMesh). No roof.
*/
void BuildTower(const ChasmWorld& w, const ZoneState& z, int plot, std::vector<vertex>& out){
    const Terrain& t = *w.terrain;
    vec2 p = w.grid->fine.pos[plot];
    float level = t.Height(plot);
    int standing = z.standing[plot];
    std::vector<vec2> corner;
    ZoneTowerCorners(w,plot,corner);
    int n = (int)corner.size();
    if (standing <= 0 || n < 3){
        return;
    }
    bool f_whole = standing >= z.storeys[plot];
    float foot = ZoneGroundAt(w,z,plot);       //a terrace's top, if it stands on one (terraces_plan.md)
    float top = foot + standing * ZONE_STOREY_HEIGHT;
    //The walls: logs along each side between the corner posts.
    for (int side = 0; side < n; side++){
        vec2 a = corner[side];
        vec2 b = corner[(side + 1) % n];
        float len = (b - a).length();
        int logs = std::max(1,(int)std::lround((len - TOWER_CORNER_W) / TOWER_LOG_SPACING));
        for (int i = 0; i < logs; i++){
            float f = (TOWER_CORNER_W * 0.5f + (len - TOWER_CORNER_W) * ((float)i + 0.5f) / logs) / std::max(1e-4f,len);
            vec2 at = a + (b - a) * f;
            float y0 = ZoneGroundHeight(w,z,plot,at) - TOWER_LOG_SINK;
            float jitter = (float)(MeshHash((uint32_t)plot,(uint32_t)side,(uint32_t)i) % 100) / 100.0f * 0.06f;
            Upright(out,at,TOWER_LOG_W,y0,top - jitter,PAL_BARK);
        }
        //The lashing round each storey's top, just proud of the logs.
        vec2 out_dir = SideOut(p,a,b);
        for (int s = 1; s <= standing; s++){
            float y = foot + s * ZONE_STOREY_HEIGHT - 0.28f;
            Beam(out,a + out_dir * (TOWER_LOG_W * 0.5f),b + out_dir * (TOWER_LOG_W * 0.5f),y,TOWER_BAND,0.04f,PAL_BARK_DARK);
        }
    }
    float post_top = f_whole ? top + TOWER_DECK + TOWER_PARAPET + 0.08f : top + 0.05f;
    for (int i = 0; i < n; i++){
        Upright(out,corner[i],TOWER_CORNER_W,ZoneGroundHeight(w,z,plot,corner[i]) - TOWER_LOG_SINK,post_top,PAL_BARK_DARK);
    }
    if (!f_whole){
        return;     //the rest is a site: its scaffold stands over what is built (BuildSite)
    }
    //The platform, a little wider than the walls: its corners pushed out from the middle.
    std::vector<vec2> rim(n);
    for (int i = 0; i < n; i++){
        vec2 d = corner[i] - p;
        float l = std::max(1e-4f,d.length());
        rim[i] = p + d * ((l + TOWER_DECK_OUT) / l);
    }
    float deck = top + TOWER_DECK;
    vec3 middle_top(p.x,deck,p.y);
    vec3 middle_bottom(p.x,top,p.y);
    for (int i = 0; i < n; i++){
        const vec2& a = rim[i];
        const vec2& b = rim[(i + 1) % n];
        MeshTri(out,middle_top,vec3(a.x,deck,a.y),vec3(b.x,deck,b.y),vec3(0,1,0),PAL_TIMBER);
        MeshTri(out,middle_bottom,vec3(a.x,top,a.y),vec3(b.x,top,b.y),vec3(0,-1,0),PAL_TIMBER);
        vec2 o = SideOut(p,a,b);
        MeshQuad(out,vec3(a.x,top,a.y),vec3(b.x,top,b.y),vec3(b.x,deck,b.y),vec3(a.x,deck,a.y),vec3(o.x,0.0f,o.y),PAL_TIMBER);
    }
    //The parapet: stakes round the platform's edge.
    for (int side = 0; side < n; side++){
        vec2 in = SideOut(p,rim[side],rim[(side + 1) % n]) * -1.0f;
        vec2 a = rim[side] + in * (TOWER_STAKE_W * 0.5f);
        vec2 b = rim[(side + 1) % n] + in * (TOWER_STAKE_W * 0.5f);
        float len = (b - a).length();
        int stakes = std::max(1,(int)std::lround(len / (TOWER_STAKE_W * 1.15f)));
        for (int i = 0; i < stakes; i++){
            vec2 at = a + (b - a) * (((float)i + 0.5f) / stakes);
            float jitter = (float)(MeshHash((uint32_t)plot,(uint32_t)(side + 8),(uint32_t)i) % 100) / 100.0f * 0.08f;
            Stake(out,at,deck - 0.02f,TOWER_PARAPET + jitter,TOWER_STAKE_W,TOWER_STAKE_TIP,PAL_BARK);
        }
    }
    //The ladder, its door: up the side that faces the plot it stands toward.
    int ladder = ZoneTowerLadder(z,plot);
    vec2 toward = (ladder >= 0 && ladder < (int)w.grid->fine.pos.size()) ? w.grid->fine.pos[ladder] - p : vec2(0.0f,1.0f);
    int best = 0;
    float best_dot = -1e30f;
    for (int side = 0; side < n; side++){
        float d = SideOut(p,rim[side],rim[(side + 1) % n]).dot(toward);
        if (d > best_dot){
            best_dot = d;
            best = side;
        }
    }
    vec2 dir = SideOut(p,rim[best],rim[(best + 1) % n]);
    vec2 along = rim[(best + 1) % n] - rim[best];
    along = along * (1.0f / std::max(1e-4f,along.length()));
    vec2 at = (rim[best] + rim[(best + 1) % n]) * 0.5f + dir * 0.02f;
    vec2 ladder_foot = at + dir * LADDER_LEAN;
    float y0 = ZoneGroundHeight(w,z,(ladder >= 0) ? ladder : plot,ladder_foot);
    float y1 = deck + 0.25f;
    for (int r = -1; r <= 1; r += 2){
        vec2 o = along * (r * LADDER_W * 0.5f);
        SlopedBeam(out,ladder_foot + o,y0,at + o,y1,LADDER_RAIL,PAL_BARK_DARK);
    }
    for (float y = y0 + LADDER_RUNG_EVERY; y < y1 - 0.15f; y += LADDER_RUNG_EVERY){
        float f = (y - y0) / std::max(0.1f,y1 - y0);
        vec2 q = ladder_foot + (at - ladder_foot) * f;
        Beam(out,q - along * (LADDER_W * 0.5f),q + along * (LADDER_W * 0.5f),y - 0.02f,0.04f,0.04f,PAL_BARK_DARK);
    }
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

#define SITE_FLAG_POLE      1.3f    //how far the pole stands above the scaffold
#define SITE_FLAG_W         0.55f
#define SITE_FLAG_H         0.34f

/*
    A site the player put first (ZoneBuilding::f_priority) flies a red pennant over its scaffold - once,
    on the lowest plot index of it still to build, so a house of four plots has one flag, not four. Its
    plots are found by flooding through its neighbours: a building is a few plots and always one piece
    (Zones::SplitAfterRemoval).
*/
bool SiteFlagPlot(const ChasmWorld& w, const ZoneState& z, int plot){
    uint32_t id = z.building[plot];
    std::vector<int> seen(1,plot);
    std::vector<int> stack(1,plot);
    while (!stack.empty()){
        int at = stack.back();
        stack.pop_back();
        if (at < plot && z.storeys[at] > z.standing[at]){
            return false;
        }
        int nb[8];
        int n = ZonePlotNeighbours(w,at,nb,8);
        for (int i = 0; i < n; i++){
            if (z.building[nb[i]] == id && std::find(seen.begin(),seen.end(),nb[i]) == seen.end()){
                seen.push_back(nb[i]);
                stack.push_back(nb[i]);
            }
        }
    }
    return true;
}

void SiteFlag(const ChasmWorld& w, const ZoneState& z, int plot, std::vector<vertex>& out){
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    vec2 p = w.grid->fine.pos[plot];
    float ground = ZoneGroundAt(w,z,plot);     //a terrace's top, if it stands on one
    float y0, top;
    if (z.KindOf(plot) == ZONE_KIND_BRIDGE){
        //Out on the water, from the deck's piles; on the bank, from the ground.
        y0 = w.terrain->wet[plot] ? BridgeDeckY(w,plot) : ground;
        top = std::max(y0,BridgeDeckY(w,plot)) + 0.15f;
    }else{
        //From what stands, past the scaffold's top - BuildSite's heights.
        float foot = BuildingFootHalves(w,z,z.KindOf(plot),plot) * HALF;
        y0 = (z.standing[plot] > 0) ? foot + z.standing[plot] * ZONE_STOREY_HEIGHT : ground - 0.3f;
        top = foot + z.storeys[plot] * ZONE_STOREY_HEIGHT + 0.25f;
    }
    float y1 = top + SITE_FLAG_POLE;
    Upright(out,p,SITE_POLE,y0,y1,PAL_BARK_DARK);
    Beam(out,p,p + vec2(SITE_FLAG_W,0.0f),y1 - SITE_FLAG_H,SITE_FLAG_H,0.03f,PAL_ACCENT);
}

void BuildSite(const ChasmWorld& w, const ZoneState& z, int plot, std::vector<vertex>& out){
    const GridPicker& picker = *w.picker;
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    vec2 p = w.grid->fine.pos[plot];
    float ground = ZoneGroundAt(w,z,plot);     //a terrace's top, if it stands on one
    if (z.buildings[z.building[plot]].f_priority && SiteFlagPlot(w,z,plot)){
        SiteFlag(w,z,plot,out);
    }
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
        float yaw = ZoneCampYaw(plot);
        vec2 d(std::cos(yaw),std::sin(yaw));
        vec2 s(-d.y,d.x);
        Beam(out,p - d * 0.6f + s * 0.12f,p + d * 0.6f + s * 0.12f,ground,0.06f,0.06f,PAL_BARK);
        Beam(out,p - d * 0.55f - s * 0.1f,p + d * 0.5f - s * 0.1f,ground,0.06f,0.06f,PAL_BARK);
        Beam(out,p - d * 0.3f,p + d * 0.3f,ground,0.14f,0.42f,PAL_STONE_LIGHT);     //the canvas, rolled
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
    float foot = BuildingFootHalves(w,z,z.KindOf(plot),plot) * HALF;
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

/*
    An ARCH's floors still to build (arches_plan.md step 3): SCAFFOLDING OVER THE ROAD - poles up from the road at
    the footprint's corners, as a site's, and its planks from the passage's ceiling (or what of the arch stands) up
    to the storey it is to rise to. The road stays open under it: the poles stand at the corners, the walk goes
    between.
*/
void BuildArchSite(const ChasmWorld& w, const ZoneState& z, int plot, std::vector<vertex>& out){
    const GridPicker& picker = *w.picker;
    const float HALF = ZONE_STOREY_HEIGHT * 0.5f;
    vec2 p = w.grid->fine.pos[plot];
    float ground = ZoneGroundAt(w,z,plot);     //a terrace's top, if it stands on one
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
    //A storey under its floor over the passage (ZoneArchStoreys), which goes by its sides, not the road.
    float foot = (z.ArchFloorOf(plot,BuildingFootHalves(w,z,ZONE_KIND_NONE,plot) + 2) - 2) * HALF;
    int standing = std::max(1,z.ArchStandingOf(plot));     //the passage, at least: nothing is built below it
    int planned = z.ArchOf(plot);
    float base = foot + standing * ZONE_STOREY_HEIGHT;
    float top = foot + planned * ZONE_STOREY_HEIGHT + 0.25f;
    for (int i = 0; i < n; i++){
        Upright(out,corners[i].second,SITE_POLE,ground - 0.3f,top,PAL_BARK);
    }
    for (int i = 0; i < n; i++){
        const vec2& a = corners[i].second;
        const vec2& b = corners[(i + 1) % n].second;
        //With nothing of it built, a frame at the passage's ceiling: where its first floor will go.
        if (z.ArchBuiltOf(plot) == 0){
            Beam(out,a,b,base - 0.12f,0.16f,0.14f,PAL_TIMBER);
        }
        for (int s = standing + 1; s <= planned; s++){
            float y = foot + s * ZONE_STOREY_HEIGHT - 0.2f;
            Beam(out,a,b,y,SITE_PLANK,SITE_PLANK * 2.0f,PAL_TIMBER);
        }
        if (i % 2 == 0){
            vec2 mid = (a + b) * 0.5f;
            Beam(out,a + (mid - a) * 0.1f,b,base + 0.3f,SITE_PLANK,SITE_PLANK,PAL_BARK_DARK);
        }
    }
}

}

void BuildHouseCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out,
                    BuildingFixtures* fixtures, const ZoneHarmLook* harm){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& picker = *w.picker;
    const GridQuad& quad = g.fine.quads[fine_quad];
    const vec3 up(0.0f,1.0f,0.0f);
    int fire_tent = ZoneCampFireTent(w,z,fine_quad);
    if (fire_tent >= 0){
        BuildCampFire(w,fine_quad,fire_tent,out);
    }

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
        as tall), so two buildings side by side meet in a valley and each reads as its own. An arch is
        its OWNER's (ZoneArchOwner, arches_plan.md): its roof runs on into that building and meets any
        other in a valley, and it is drawn in that building's look.
    */
    uint32_t bid[4];
    bool f_arch[4];
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
        if (s[k] == 0 && z.ArchBuiltOf(v) > 0){
            s[k] = z.ArchStandingOf(v);     //as far as it is built (arches_plan.md step 3)
            base = 1;
        }
        f_arch[k] = base > 0;
        //Its floors still to build: scaffolding over the road, drawn once, by the first cell round its plot.
        if (s[k] == 0 || f_arch[k]){
            if (z.ArchBuiltOf(v) < ZoneArchFloors(z.ArchOf(v)) && picker.PlotQuadCorner(v,0) / 4 == fine_quad){
                BuildArchSite(w,z,v,out);
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
        //A camp is no house body either: a tent on each of its plots, the same way.
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_CAMP){
            if (picker.PlotQuadCorner(v,0) / 4 == fine_quad){
                BuildCampPlot(w,v,out);
            }
            s[k] = 0;
        }
        //Nor a guard tower: logs, a platform and a parapet, drawn on its own (BuildTower) like the winch.
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_TOWER){
            if (picker.PlotQuadCorner(v,0) / 4 == fine_quad){
                BuildTower(w,z,v,out);
            }
            s[k] = 0;
        }
        //Nor a mine (stone_plan.md step 5): its portal is the worker's prop, drawn with its cart (ApplicationChasmMines.cpp).
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_MINE){
            s[k] = 0;
        }
        //Nor a well (water_tools_plan.md): a ring, posts and a roof, drawn on its own (BuildWell) like the winch.
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_WELL){
            if (picker.PlotQuadCorner(v,0) / 4 == fine_quad){
                BuildWell(w,v,out);
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
        //A steam collector likewise, and its hood out over the drop (steam_plan.md).
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_STEAM && picker.PlotQuadCorner(v,0) / 4 == fine_quad){
            BuildSteamHood(w,v,out);
        }
        //A builders' hut too, with its beams and ladder by one wall.
        if (s[k] > 0 && z.KindOf(v) == ZONE_KIND_BUILDERS && picker.PlotQuadCorner(v,0) / 4 == fine_quad
            && LowestStandingPlot(w,z,v)){
            BuildBuildersYard(w,z,v,out);
        }
        solid[k] = s[k] > 0;
        bid[k] = f_arch[k] ? z.ArchOwnerOf(v) : z.building[v];
        foot[k] = BuildingFootHalves(w,z,f_arch[k] ? ZONE_KIND_NONE : z.KindOf(v),v);
        //An arch's body starts at its floor over the passage, which goes by its sides (ZoneArchStoreys): its `foot`, a
        //storey under that.
        if (f_arch[k]){
            foot[k] = z.ArchFloorOf(v,foot[k] + 2) - 2;
        }
        lo[k] = foot[k] + 2 * base;
        hi[k] = foot[k] + 2 * s[k];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    vec2 m[4];      //m[k] is where the walls cross edge k -> k+1: its midpoint, or straight on a star's (BuildingWallCrossing)
    for (int k = 0; k < 4; k++){
        m[k] = BuildingWallCrossing(w,quad.v[k],quad.v[(k + 1) % 4]);
    }
    auto same = [&](int a, int b){
        return solid[a] && solid[b] && hi[a] == hi[b] && bid[a] == bid[b];
    };
    bool f_all_same = same(0,1) && same(1,2) && same(2,3) && same(3,0);
    //Each building's sure door (BuildingSureDoor), looked for once a cell, and only by a wall that could take one.
    uint32_t sure_id[4];
    DoorPlace sure_at[4];
    int sure_n = 0;
    auto sure_door = [&](int plot, int e){
        uint32_t id = z.building[plot];
        int i = 0;
        while (i < sure_n && sure_id[i] != id){
            i++;
        }
        if (i == sure_n){
            sure_id[i] = id;
            BuildingSureDoor(w,z,plot,sure_at[i]);
            sure_n++;
        }
        return sure_at[i].plot == plot && sure_at[i].quad == fine_quad && sure_at[i].edge == e;
    };

    for (int k = 0; k < 4; k++){
        if (!solid[k]){
            continue;
        }
        int plot = quad.v[k];
        float eave = hi[k] * HALF;
        int next = (k + 1) % 4;
        int prev = (k + 3) % 4;
        uint32_t look = MeshHash((uint32_t)plot,0x40115Eu);
        //The building this corner is - an arch its owner's - and so its look and roof colour. (An arch's look
        //was the first building among the cell's corners: a diagonal one it does not bridge, found 2026-10-10.)
        uint32_t owner = bid[k];
        int kind = (owner && owner < z.buildings.size()) ? z.buildings[owner].kind : ZONE_KIND_HOUSE;
        const KindLook& kl = kind_looks[kind];
        //Where this corner's chimney goes, if this cell builds it: a house's one (HouseChimney), or by
        //chance on a plot of another kind that has them - through the roof.
        ChimneyPlace chimney;
        bool f_chimney = false;
        if (lo[k] == foot[k] && !f_arch[k] && kind == ZONE_KIND_HOUSE){
            f_chimney = HouseChimney(w,z,plot,chimney) && chimney.quad == fine_quad;
        }else if (lo[k] == foot[k] && kind != ZONE_KIND_HOUSE && (look >> 8) % 100 < (uint32_t)kl.chimney_chance){
            int n = picker.PlotQuadCount(plot);
            f_chimney = picker.PlotQuadCorner(plot,(int)((look >> 16) % (uint32_t)n)) / 4 == fine_quad;
        }
        bool f_reports = fixtures && kind == ZONE_KIND_HOUSE && !f_arch[k];
        //One roof colour for a whole building, by its id.
        uint32_t building_look = MeshHash(owner,0xB017D1u);
        int roof_column = ((building_look % 10) < 7) ? kl.roof[0] : kl.roof[1];

        //--- The roof: the quarter P, M1, C, M0, each point at the eave or raised to the ridge ---
        //Raised where the house carries on past it at the same height: always over the plot's own
        //vertex; where the walls cross an edge when the corner across it is as tall; at the cell's centre when
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
            bool f_stack = f_chimney && chimney.edge == e;      //the stack stands where its window would
            //Its door, if it has one (WallDoor) and on which storey: by its hash, an arch's passage, or as its building's
            //sure door.
            int door_storey = 0;
            int doorway = f_arch[k] ? WALL_DOOR_NONE : WallDoor(w,z,plot,quad.v[j],fine_quad,e,kl.door_chance,door_storey);
            bool f_door_here = doorway == WALL_DOOR_DRAWN || (doorway == WALL_DOOR_CAN && sure_door(plot,e));
            //A window only clear of the ground outside: under a terrace's top it was cut by its grass.
            float outside = solid[j] ? -INFINITY
                                     : std::max(ZoneGroundHeight(w,z,quad.v[j],a),ZoneGroundHeight(w,z,quad.v[j],b));
            /*
                THE PLINTH (terraces_plan.md): a wall from this house's own floor goes on down as far as the ground
                under it, and a little into it - so no gap shows under a house on uneven ground, and nothing hangs
                far below it, which the selection's outline, drawn through the ground, showed a storey deep. What
                is below the floor is stone: a course where the floor stands over the ground, and on a foundation
                the tall downhill side of it. The ground along the wall runs straight between its ends, the ground
                mesh's own points (TerrainMesh's fans), so the lower end is its lowest.
            */
            float floor_at = foot[k] * HALF;
            float footing = std::min(floor_at,std::min(ZoneGroundHeight(w,z,plot,a),ZoneGroundHeight(w,z,plot,b)))
                            - FOOTING_DEPTH;
            int plinth = (kl.wall == PAL_STONE || kl.wall == PAL_STONE_LIGHT) ? PAL_STONE_DARK : PAL_STONE;
            for (int pc = 0; pc < n_pieces; pc++){
                int from = pieces[pc][0];
                int to = pieces[pc][1];
                if (to <= from){
                    continue;
                }
                float bottom = from * HALF;
                float top = to * HALF;
                if (from == foot[k]){
                    MeshQuad(out,vec3(a.x,footing,a.y),vec3(b.x,footing,b.y),vec3(b.x,bottom,b.y),vec3(a.x,bottom,a.y),
                             outward,plinth);
                }
                MeshQuad(out,vec3(a.x,bottom,a.y),vec3(b.x,bottom,b.y),vec3(b.x,top,b.y),vec3(a.x,top,a.y),
                         outward,kl.wall);
                //The gable: where the ridge carries on along this edge's far end, the wall closes up to
                //the roof's raised point rather than leaving a hole under it.
                vec3 roof_a = (e == 0) ? M1 : C;
                vec3 roof_b = (e == 0) ? C : M0;
                if (to == hi[k] && (roof_a.y > eave || roof_b.y > eave)){
                    MeshQuad(out,vec3(a.x,eave,a.y),vec3(b.x,eave,b.y),roof_b,roof_a,outward,kl.wall);
                }
                //A window on every storey of this house the piece shows whole; its door, if it has one, on its storey.
                for (int st = 0; st < s[k] && !f_stack; st++){
                    int floor_half = foot[k] + 2 * st;
                    if (floor_half < from || floor_half + 2 > to){
                        continue;
                    }
                    float floor_y = floor_half * HALF;
                    uint32_t wall_hash = MeshHash((uint32_t)plot,(uint32_t)fine_quad,(uint32_t)e);
                    if (st == door_storey && f_door_here){
                        int door = (kl.wall == PAL_BARK_DARK) ? PAL_STONE_DARK : PAL_BARK_DARK;
                        Opening(out,a,b,outward,0.5f,kl.door_w * 0.5f,floor_y,floor_y + DOOR_H,door,kl.wall,false);
                        //Down to the ground outside - a terrace's top, from an upper door half a storey over it.
                        DoorSteps(out,w,z,quad.v[j],a,b,outward,kl.door_w * 0.5f,floor_y,plinth);
                        if (harm && harm->Breached(bid[k])){
                            BreachedDoor(out,a,b,outward,kl.door_w * 0.5f,floor_y,floor_y + DOOR_H,door,wall_hash >> 16);
                        }
                    }else if ((wall_hash >> 8) % (uint32_t)kl.window_every == 0
                              && floor_y + ZONE_STOREY_HEIGHT * WINDOW_SILL > outside){
                        float y0 = floor_y + ZONE_STOREY_HEIGHT * WINDOW_SILL;
                        BuildingPane pane;
                        Opening(out,a,b,outward,0.5f,WINDOW_W * 0.5f,y0,y0 + WINDOW_H,
                                (kl.wall == PAL_TIMBER) ? PAL_BARK_DARK : PAL_TIMBER,kl.wall,true,
                                f_reports ? &pane : NULL);
                        if (f_reports){
                            pane.building = bid[k];
                            fixtures->panes.push_back(pane);
                        }
                    }
                }
            }
            /*
                The stack: a base as wide as a hearth up to just under the eaves, against the middle of this
                wall from its footing, then a narrower flue past the ridge with a cap on it. The base has the
                wall behind it; the flue and the cap stand clear of it above the eaves, where the roof slopes
                away, so they are closed all round - built as a slab, they showed their open backs, and the
                cap's top, level with the flue's, fought it (the user's, 2026-10-08).
            */
            if (f_stack){
                vec2 c = (a + b) * 0.5f;
                vec2 dir = along / std::max(1e-6f,along.length());
                float base_top = eave - 0.20f;
                float top = eave + ROOF_RISE + CHIMNEY_TOP;
                WallSlab(out,c,dir,side,outward,-STACK_BASE_W * 0.5f,STACK_BASE_W * 0.5f,footing,
                         base_top,STACK_BASE_D,PAL_STONE);
                //A block from u0 to u1 along the wall, d0 to d1 out from it, y0 to y1 up: all six faces.
                auto block = [&](float u0, float u1, float d0, float d1, float y0, float y1, int column){
                    auto P = [&](float u, float y, float d){
                        vec2 q = c + dir * u + side * d;
                        return vec3(q.x,y,q.y);
                    };
                    vec3 along3(dir.x,0.0f,dir.y);
                    MeshQuad(out,P(u0,y0,d1),P(u1,y0,d1),P(u1,y1,d1),P(u0,y1,d1),outward,column);
                    MeshQuad(out,P(u0,y0,d0),P(u1,y0,d0),P(u1,y1,d0),P(u0,y1,d0),outward * -1.0f,column);
                    MeshQuad(out,P(u0,y0,d0),P(u0,y1,d0),P(u0,y1,d1),P(u0,y0,d1),along3 * -1.0f,column);
                    MeshQuad(out,P(u1,y0,d0),P(u1,y1,d0),P(u1,y1,d1),P(u1,y0,d1),along3,column);
                    MeshQuad(out,P(u0,y1,d0),P(u1,y1,d0),P(u1,y1,d1),P(u0,y1,d1),vec3(0,1,0),column);
                    MeshQuad(out,P(u0,y0,d0),P(u1,y0,d0),P(u1,y0,d1),P(u0,y0,d1),vec3(0,-1,0),column);
                };
                float hw = STACK_FLUE_W * 0.5f;
                block(-hw,hw,0.0f,STACK_FLUE_D,base_top - 0.02f,top,PAL_STONE);
                block(-hw - STACK_CAP,hw + STACK_CAP,-STACK_CAP,STACK_FLUE_D + STACK_CAP,top - 0.02f,top + 0.06f,PAL_STONE_DARK);
                if (f_reports){
                    vec2 mid = c + side * (STACK_FLUE_D * 0.5f);
                    BuildingChimney ch;
                    ch.building = bid[k];
                    ch.top = vec3(mid.x,top + 0.06f,mid.y);
                    fixtures->chimneys.push_back(ch);
                }
            }
        }

        //--- A chimney through the roof, partway from the ridge toward the cell's centre ----------------
        if (f_chimney && chimney.edge < 0 && kind == ZONE_KIND_CONDENSER){
            //A condenser's tank (steam_plan.md step 3): brass, wider and taller than a chimney, banded dark at its top.
            vec2 at = p[k] + (centre - p[k]) * 0.45f;
            float top = eave + ROOF_RISE + CONDENSER_TANK_TOP;
            Box(out,at,CONDENSER_TANK_W,eave + ROOF_RISE * 0.2f,top,PAL_BRASS,PAL_METALS);
            Box(out,at,CONDENSER_TANK_W * 1.12f,top - 0.1f,top + 0.04f,PAL_BRASS_DARK,PAL_METALS);
        }else if (f_chimney && chimney.edge < 0){
            vec2 at = p[k] + (centre - p[k]) * 0.45f;
            Box(out,at,CHIMNEY_W,eave + ROOF_RISE * 0.3f,eave + ROOF_RISE + CHIMNEY_TOP,PAL_STONE);
            if (f_reports){
                BuildingChimney ch;
                ch.building = bid[k];
                ch.top = vec3(at.x,eave + ROOF_RISE + CHIMNEY_TOP,at.y);
                fixtures->chimneys.push_back(ch);
            }
        }
    }
}

float BuildingTowerDeck(const ChasmWorld& w, const ZoneState& z, int plot){
    return ZoneGroundAt(w,z,plot) + z.standing[plot] * ZONE_STOREY_HEIGHT + TOWER_DECK;
}

float BuildingLayerHeight(const ChasmWorld& w, const ZoneState& z, int plot, int layer){
    if (z.KindOf(plot) == ZONE_KIND_TOWER && layer == 2 * z.standing[plot]){
        return BuildingTowerDeck(w,z,plot);
    }
    //A castle piece's, over its level (ZoneCastleBase): whole storeys, so a wall over a hill is level.
    return (float)WalkNodeHalves(w,z,plot,layer) * 0.5f * ZONE_STOREY_HEIGHT;
}

float BuildingMeetHeight(const ChasmWorld& w, const ZoneState& z, int a, int la, int b, int lb, int meet, const vec2& at){
    if (la == WALK_GROUND){
        return ZoneGroundHeight(w,z,a,at);
    }
    if (lb == WALK_GROUND){
        return ZoneGroundHeight(w,z,b,at);
    }
    return (float)meet * 0.5f * ZONE_STOREY_HEIGHT;
}

float BuildingTowerReach(const ChasmWorld& w, int plot, const vec2& toward){
    std::vector<vec2> corner;
    ZoneTowerCorners(w,plot,corner);
    vec2 p = w.grid->fine.pos[plot];
    float l = toward.length();
    if (corner.size() < 3 || l < 1e-6f){
        return 0.0f;
    }
    vec2 d = toward * (1.0f / l);
    //The ray from its point along `toward`, against each side: the nearest crossing ahead.
    float best = 0.0f;
    for (size_t i = 0; i < corner.size(); i++){
        vec2 a = corner[i] - p;
        vec2 e = corner[(i + 1) % corner.size()] - corner[i];
        float den = d.x * e.y - d.y * e.x;
        if (std::fabs(den) < 1e-6f){
            continue;
        }
        float s = (a.x * e.y - a.y * e.x) / den;      //along the ray
        float u = (a.x * d.y - a.y * d.x) / den;      //along the side
        if (s > 0.0f && u >= 0.0f && u <= 1.0f && (best == 0.0f || s < best)){
            best = s;
        }
    }
    return best;
}

bool BuildingOnTower(const ChasmWorld& w, int plot, const vec2& at){
    vec2 d = at - w.grid->fine.pos[plot];
    float l = d.length();
    return l < 1e-4f || l < BuildingTowerReach(w,plot,d) + TOWER_DECK_OUT;
}

/*
    A ROPEWALK's dressing (BuildingMesh.h). Everything stands in the cells of the open plots beside it - out from
    the wall to past the walk, and to a plot's width either way along it - which no other building, road, garden
    or field draws in.
*/
#define ROPEWALK_OUT            0.72f   //the walk's line, of the way from a ropewalk plot to the open one beside it: past the wall at half (or its crossing)
#define ROPEWALK_HEIGHT         0.58f   //the strands, above the ground: waist-high on a villager (0.86)
#define ROPEWALK_POST           0.07f   //the posts, square
#define ROPEWALK_POST_EVERY     1.1f    //the longest stretch of strand between two
#define ROPEWALK_BAR            0.30f   //each post's crossbar, which the strands lie over
#define ROPEWALK_BAR_THICK      0.05f
#define ROPEWALK_STRANDS        3
#define ROPEWALK_STRAND_GAP     0.08f
#define ROPEWALK_STRAND         0.035f  //thicker than a rope would be, to be seen at all
#define ROPEWALK_WHEEL_R        0.48f   //the spinning wheel in the yard: big, since the camera sees it foreshortened
#define ROPEWALK_WHEEL_ASIDE    0.45f   //...its middle this far west of the yard's middle, the coils east
#define ROPEWALK_WHEEL_SIDES    12
#define ROPEWALK_WHEEL_FOOT     0.38f   //its stand's legs, splayed this far along the wheel either way
#define ROPEWALK_COIL_R         0.15f   //a coil of rope lying flat: its turns' radius...
#define ROPEWALK_COIL_TUBE      0.045f  //...and thickness - two turns a coil
#define ROPEWALK_COIL_SIDES     8
#define ROPEWALK_COIL_SPACING   0.36f   //between the coils' two stacks
#define ROPEWALK_COIL_STACK     3       //coils a stack
#define ROPEWALK_SHEAF_H        0.62f   //a sheaf of hemp stood against the wall
#define ROPEWALK_SHEAF_SPACING  0.19f
#define ROPEWALK_SHEAVES        4       //the most drawn
//A SAIL LOFT's yard (steam_plan.md step 4), the same ground's: sheaves as the ropewalk's, a tenter frame, bolts of canvas.
#define SAIL_SHEAF_ASIDE        0.15f   //the sheaves' first, east of the yard's middle along the wall
#define SAIL_FRAME_W            1.0f    //the TENTER FRAME, a canvas stretched to set: its width...
#define SAIL_FRAME_FOOT         0.12f   //...the cloth's foot above the ground...
#define SAIL_FRAME_TOP          0.92f   //...and its head: a little over a villager (0.86)
#define SAIL_FRAME_POST         0.06f
#define SAIL_FRAME_RAIL         0.04f
#define SAIL_FRAME_OUT          0.30f   //out from the wall, clear of the sheaves...
#define SAIL_FRAME_ASIDE        0.50f   //...and west of the yard's middle, the sheaves and bolts east
#define SAIL_PANELS             3       //the cloth's widths sewn side by side, a seam down each join
#define SAIL_SEAM               0.02f
#define SAIL_BOLT_L             0.44f   //a BOLT of canvas lying flat, folded: its length...
#define SAIL_BOLT_W             0.18f   //...width...
#define SAIL_BOLT_H             0.08f   //...and thickness
#define SAIL_BOLT_OUT           0.42f   //out from the wall, in front of the sheaves
#define SAIL_BOLT_ASIDE         0.28f   //the first stack east of the yard's middle...
#define SAIL_BOLT_SPACING       0.48f   //...the second this much further
#define SAIL_BOLT_STACK         3       //bolts a stack, two stacks

namespace {

//A bar of square section `thick` across from a to b, any way up: the wheel's rim and spokes, a coil's turns.
void Rod(std::vector<vertex>& out, const vec3& a, const vec3& b, float thick, int column, int row = PAL_TEMPERATE){
    vec3 d = b - a;
    float l = d.length();
    if (l < 1e-5f){
        return;
    }
    d = d * (1.0f / l);
    vec3 s = d.cross(std::fabs(d.y) < 0.9f ? vec3(0.0f,1.0f,0.0f) : vec3(1.0f,0.0f,0.0f));
    s.normalize();
    vec3 r = s.cross(d);
    s = s * (thick * 0.5f);
    r = r * (thick * 0.5f);
    vec3 c[4] = {s + r,r - s,(s + r) * -1.0f,s - r};
    for (int i = 0; i < 4; i++){
        const vec3& c0 = c[i];
        const vec3& c1 = c[(i + 1) % 4];
        MeshQuad(out,a + c0,a + c1,b + c1,b + c0,c0 + c1,column,row);
    }
    MeshQuad(out,a + c[0],a + c[1],a + c[2],a + c[3],d * -1.0f,column,row);
    MeshQuad(out,b + c[0],b + c[1],b + c[2],b + c[3],d,column,row);
}

//Open ground a ropewalk's walk and yard may stand on: nothing built, laid or planted there or in a field round it,
//and dry.
bool RopewalkOpen(const ChasmWorld& w, const ZoneState& z, int v){
    if (z.building[v] != 0 || (!z.ground.empty() && z.ground[v] != ZONE_GROUND_NONE) || w.terrain->wet[v]
        || w.grid->fine.f_boundary[v]){
        return false;
    }
    for (int i = 0; i < w.picker->PlotQuadCount(v) && !z.field.empty(); i++){
        if (z.field[w.grid->fine.quads[w.picker->PlotQuadCorner(v,i) / 4].parent]){
            return false;
        }
    }
    return true;
}

//Whether another building that draws on the open ground beside it - a ropewalk, a sail loft, a builders' hut - stands
//next to open plot v, so that a yard there could land on what it draws.
bool OpenGroundShared(const ChasmWorld& w, const ZoneState& z, int v, uint32_t id){
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    for (int j = 0; j < n; j++){
        uint32_t b = z.building[nb[j]];
        if (b && b != id && b < z.buildings.size()
            && (z.buildings[b].kind == ZONE_KIND_ROPEWALK || z.buildings[b].kind == ZONE_KIND_SAIL_LOFT
                || z.buildings[b].kind == ZONE_KIND_BUILDERS)){
            return true;
        }
    }
    return false;
}

/*
    THE YARD of a workshop dressed on its open ground (a ropewalk, a sail loft): out from its standing `plots` (in
    order) toward the open plot most toward the camera (south) - the first of equals - since a yard behind the building
    is behind its roof too; an inside corner less readily, and ground another such workshop or a builders' hut touches
    only if there is no other. False if it is built in on every side.
*/
bool YardSpot(const ChasmWorld& w, const ZoneState& z, uint32_t id, const std::vector<int>& plots, int& yard_plot,
              int& yard_open){
    const Grid& g = *w.grid;
    auto ours = [&](int v){ return std::binary_search(plots.begin(),plots.end(),v); };
    yard_plot = -1;
    yard_open = -1;
    float yard_score = 0.0f;
    for (int p : plots){
        int nb[8];
        int n = ZonePlotNeighbours(w,p,nb,8);
        for (int j = 0; j < n; j++){
            vec2 d = g.fine.pos[nb[j]] - g.fine.pos[p];
            float l = d.length();
            if (l < 1e-4f || !RopewalkOpen(w,z,nb[j])){
                continue;
            }
            //Shared ground last: another's walk or yard may be there. And an inside corner, walled on two sides,
            //only well toward the camera: what stands in a yard barely fits in one.
            int walls = 0;
            int around[8];
            int m = ZonePlotNeighbours(w,nb[j],around,8);
            for (int q = 0; q < m; q++){
                walls += ours(around[q]) ? 1 : 0;
            }
            float score = d.y / l - (OpenGroundShared(w,z,nb[j],id) ? 10.0f : 0.0f) - (walls > 1 ? 0.6f : 0.0f);
            if (yard_plot < 0 || score > yard_score + 1e-4f){
                yard_plot = p;
                yard_open = nb[j];
                yard_score = score;
            }
        }
    }
    return yard_plot >= 0;
}

//An end of a stretch of the walk: out from a ropewalk plot toward the open plot beside it.
struct WalkEnd{
    int plot = -1;
    int open = -1;
    vec2 at;
    float y = 0.0f;             //the ground under it
    vec2 along;                 //the stretches meeting here, summed, each pointing away from it
    int stretches = 0;
};

//A T-post at p on the ground y, its crossbar across `along`, under the strands.
void WalkPost(std::vector<vertex>& out, const vec2& p, float y, const vec2& along){
    vec2 across(-along.y,along.x);
    float bar_top = y + ROPEWALK_HEIGHT - ROPEWALK_STRAND * 0.5f;
    Upright(out,p,ROPEWALK_POST,y - 0.1f,bar_top - ROPEWALK_BAR_THICK,PAL_TIMBER);
    Beam(out,p - across * (ROPEWALK_BAR * 0.5f),p + across * (ROPEWALK_BAR * 0.5f),bar_top - ROPEWALK_BAR_THICK,
         ROPEWALK_BAR_THICK,ROPEWALK_BAR_THICK,PAL_TIMBER);
}

/*
    The spinning wheel at c, its middle ROPEWALK_WHEEL_R over the ground y, in the upright plane through `in`:
    a rim, three spokes across it, a hub on an axle between two A-frames, a crank on the side `crank` faces.
*/
void SpinningWheel(std::vector<vertex>& out, const vec2& c, float y, const vec2& in, const vec2& crank){
    const vec3 up(0.0f,1.0f,0.0f);
    vec3 w(in.x,0.0f,in.y);
    vec3 n(crank.x,0.0f,crank.y);
    vec3 mid(c.x,y + 0.1f + ROPEWALK_WHEEL_R,c.y);
    auto rim = [&](float a){ return mid + (w * std::cos(a) + up * std::sin(a)) * ROPEWALK_WHEEL_R; };
    const float turn = 6.2831853f / ROPEWALK_WHEEL_SIDES;
    for (int i = 0; i < ROPEWALK_WHEEL_SIDES; i++){
        Rod(out,rim(turn * i),rim(turn * (i + 1)),0.06f,PAL_TIMBER);
    }
    for (int i = 0; i < 3; i++){
        float a = 3.1415927f / 3.0f * i + 0.3f;
        Rod(out,rim(a),rim(a + 3.1415927f),0.03f,PAL_BARK_DARK);
    }
    Rod(out,mid - n * 0.06f,mid + n * 0.06f,0.09f,PAL_BARK_DARK);
    Rod(out,mid - n * 0.13f,mid + n * 0.13f,0.035f,PAL_TIMBER);
    for (int side = -1; side <= 1; side += 2){
        vec3 top = mid + n * (0.11f * side);
        for (int leg = -1; leg <= 1; leg += 2){
            vec3 foot = top + w * (ROPEWALK_WHEEL_FOOT * leg);
            foot.y = y - 0.05f;
            Rod(out,top,foot,0.045f,PAL_BARK);
        }
    }
    vec3 arm = mid + n * 0.13f;
    Rod(out,arm,arm + up * 0.16f,0.03f,PAL_TIMBER);
    Rod(out,arm + up * 0.16f,arm + up * 0.16f + n * 0.09f,0.03f,PAL_BARK_DARK);
}

//A coil of rope lying flat at p on the ground y: two turns of it, one on the other, turned by `spin`.
void RopeCoil(std::vector<vertex>& out, const vec2& p, float y, float spin){
    const float turn = 6.2831853f / ROPEWALK_COIL_SIDES;
    const float reach = ROPEWALK_COIL_TUBE * 0.4f;     //each piece a little past its corner, so none gap
    for (int t = 0; t < 2; t++){
        float h = y + ROPEWALK_COIL_TUBE * (0.5f + t);
        float r = ROPEWALK_COIL_R - 0.012f * t;
        for (int i = 0; i < ROPEWALK_COIL_SIDES; i++){
            float a0 = spin + turn * i, a1 = a0 + turn;
            vec3 a(p.x + r * std::cos(a0),h,p.y + r * std::sin(a0));
            vec3 b(p.x + r * std::cos(a1),h,p.y + r * std::sin(a1));
            vec3 d = b - a;
            d.normalize();
            Rod(out,a - d * reach,b + d * reach,ROPEWALK_COIL_TUBE,PAL_ROPE,PAL_CROPS);
        }
    }
}

//A sheaf of hemp stood at p on the ground y, leant back toward `wall` on it: stalks, a band, the heads.
void HempSheaf(std::vector<vertex>& out, const vec2& p, float y, const vec2& wall){
    vec3 foot(p.x,y - 0.05f,p.y);
    vec3 lean(wall.x * 0.12f,ROPEWALK_SHEAF_H,wall.y * 0.12f);
    Rod(out,foot,foot + lean * 0.68f,0.11f,PAL_HEMP,PAL_CROPS);
    Rod(out,foot + lean * 0.66f,foot + lean,0.15f,PAL_HEMP_HEADS,PAL_CROPS);
    Rod(out,foot + lean * 0.30f,foot + lean * 0.37f,0.125f,PAL_ROPE,PAL_CROPS);
}

/*
    A sail loft's TENTER FRAME at c on the ground y, across `in`: two posts, a head rail and a foot rail and - while the
    loft has anything to work - a canvas stretched between, both its faces, the seams between its widths in the shade.
*/
void TenterFrame(std::vector<vertex>& out, const vec2& c, float y, const vec2& in, bool f_canvas){
    float half = SAIL_FRAME_W * 0.5f;
    vec2 a = c - in * half;
    vec2 b = c + in * half;
    float head = y + SAIL_FRAME_TOP;
    float foot = y + SAIL_FRAME_FOOT;
    Upright(out,a,SAIL_FRAME_POST,y - 0.1f,head + 0.08f,PAL_TIMBER);
    Upright(out,b,SAIL_FRAME_POST,y - 0.1f,head + 0.08f,PAL_TIMBER);
    vec2 reach = in * (half + SAIL_FRAME_POST);
    Beam(out,c - reach,c + reach,head,SAIL_FRAME_RAIL,SAIL_FRAME_RAIL,PAL_TIMBER);
    Beam(out,c - reach,c + reach,foot - SAIL_FRAME_RAIL,SAIL_FRAME_RAIL,SAIL_FRAME_RAIL,PAL_TIMBER);
    if (!f_canvas){
        return;
    }
    vec3 n(-in.y,0.0f,in.x);
    vec2 ca = a + in * (SAIL_FRAME_POST * 0.5f);
    vec2 cb = b - in * (SAIL_FRAME_POST * 0.5f);
    vec3 p0(ca.x,foot,ca.y), p1(cb.x,foot,cb.y), p2(cb.x,head,cb.y), p3(ca.x,head,ca.y);
    MeshQuad(out,p0,p1,p2,p3,n,PAL_CANVAS,PAL_CROPS);
    MeshQuad(out,p0,p1,p2,p3,n * -1.0f,PAL_CANVAS,PAL_CROPS);
    for (int s = 1; s < SAIL_PANELS; s++){
        vec2 m = ca + (cb - ca) * ((float)s / SAIL_PANELS);
        vec2 l = m - in * (SAIL_SEAM * 0.5f);
        vec2 r = m + in * (SAIL_SEAM * 0.5f);
        for (int side = -1; side <= 1; side += 2){
            vec3 off = n * (FACE_OUT * side);
            MeshQuad(out,vec3(l.x,foot,l.y) + off,vec3(r.x,foot,r.y) + off,vec3(r.x,head,r.y) + off,vec3(l.x,head,l.y) + off,
                     n * (float)side,PAL_CANVAS_DARK,PAL_CROPS);
        }
    }
}

//A bolt of canvas lying folded at p on the ground y, its length along `along`, tied round its middle.
void CanvasBolt(std::vector<vertex>& out, const vec2& p, float y, const vec2& along){
    vec2 h = along * (SAIL_BOLT_L * 0.5f);
    Beam(out,p - h,p + h,y,SAIL_BOLT_H,SAIL_BOLT_W,PAL_CANVAS,PAL_CROPS);
    vec2 t = along * 0.02f;
    Beam(out,p - t,p + t,y - 0.004f,SAIL_BOLT_H + 0.008f,SAIL_BOLT_W + 0.008f,PAL_ROPE,PAL_CROPS);
}

}

void BuildRopewalkYard(const ChasmWorld& w, const ZoneState& z, uint32_t id, int coils, int sheaves,
                       std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridPicker& picker = *w.picker;
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_ROPEWALK){
        return;
    }
    std::vector<int> plots;     //what of it stands, in order
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id && z.standing[v] > 0){
            plots.push_back((int)v);
        }
    }
    if (plots.empty()){
        return;
    }
    auto ours = [&](int v){ return std::binary_search(plots.begin(),plots.end(),v); };
    std::vector<WalkEnd> ends;
    auto end_of = [&](int plot, int open){
        for (size_t i = 0; i < ends.size(); i++){
            if (ends[i].plot == plot && ends[i].open == open){
                return (int)i;
            }
        }
        WalkEnd e;
        e.plot = plot;
        e.open = open;
        //As far past the wall as past a midpoint, wherever the wall crosses (BuildingWallCrossing).
        e.at = BuildingWallCrossing(w,plot,open) + (g.fine.pos[open] - g.fine.pos[plot]) * (ROPEWALK_OUT - 0.5f);
        e.y = ZoneGroundHeight(w,z,open,e.at);     //a terrace's top beside it, if it is one
        ends.push_back(e);
        return (int)ends.size() - 1;
    };
    /*
        THE STRETCHES: one for each two plots of it side by side, in the cell beyond their edge whose other two
        plots are open - the strands from out by one plot to out by the other, the wall behind them. Of two such
        cells, the one toward the camera (south, +z), so the building does not hide its walk. Each cell's far
        half is those two open plots' own ground, which nothing else draws on.
    */
    struct Stretch{
        int a, b, c, d;
        float south;
    };
    std::vector<Stretch> found;
    for (int a : plots){
        for (int i = 0; i < picker.PlotQuadCount(a); i++){
            int qc = picker.PlotQuadCorner(a,i);
            const GridQuad& q = g.fine.quads[qc / 4];
            int k = qc % 4;
            for (int turn = 1; turn <= 3; turn += 2){
                Stretch s;
                s.a = a;
                s.b = q.v[(k + turn) % 4];
                s.d = q.v[(k + 2) % 4];
                s.c = q.v[(k + 4 - turn) % 4];
                if (s.b > a && ours(s.b) && RopewalkOpen(w,z,s.c) && RopewalkOpen(w,z,s.d)){
                    s.south = (g.fine.pos[s.c].y + g.fine.pos[s.d].y) - (g.fine.pos[s.a].y + g.fine.pos[s.b].y);
                    found.push_back(s);
                }
            }
        }
    }
    std::sort(found.begin(),found.end(),[](const Stretch& l, const Stretch& r){
        return (l.a != r.a) ? l.a < r.a : (l.b != r.b) ? l.b < r.b : l.south > r.south;
    });
    std::vector<std::pair<int,int>> stretches;
    for (size_t i = 0; i < found.size(); i++){
        if (i > 0 && found[i].a == found[i - 1].a && found[i].b == found[i - 1].b){
            continue;       //the pair's other cell, further from the camera
        }
        int e0 = end_of(found[i].a,found[i].c);
        int e1 = end_of(found[i].b,found[i].d);
        vec2 d = ends[e1].at - ends[e0].at;
        float l = d.length();
        if (l < 1e-3f){
            continue;
        }
        d = d * (1.0f / l);
        ends[e0].along = ends[e0].along + d;
        ends[e1].along = ends[e1].along + d * -1.0f;
        ends[e0].stretches++;
        ends[e1].stretches++;
        stretches.push_back(std::make_pair(e0,e1));
    }
    //--- The walk: the strands along each stretch, a post at each end and as often as they need between ---
    for (const std::pair<int,int>& s : stretches){
        const WalkEnd& e0 = ends[s.first];
        const WalkEnd& e1 = ends[s.second];
        vec2 d = e1.at - e0.at;
        float l = d.length();
        vec2 along = d * (1.0f / l);
        vec2 across(-along.y,along.x);
        for (int i = 0; i < ROPEWALK_STRANDS; i++){
            vec2 off = across * (ROPEWALK_STRAND_GAP * (i - (ROPEWALK_STRANDS - 1) * 0.5f));
            Rod(out,vec3(e0.at.x + off.x,e0.y + ROPEWALK_HEIGHT,e0.at.y + off.y),
                vec3(e1.at.x + off.x,e1.y + ROPEWALK_HEIGHT,e1.at.y + off.y),ROPEWALK_STRAND,PAL_ROPE,PAL_CROPS);
        }
        int between = (int)std::ceil(l / ROPEWALK_POST_EVERY);
        for (int j = 1; j < between; j++){
            float f = (float)j / between;
            vec2 p = e0.at + d * f;
            float line = e0.y + (e1.y - e0.y) * f;      //the ground the strands keep their height over
            float ground = ZoneGroundHeight(w,z,e0.open,p);
            if (ground < line){
                Upright(out,p,ROPEWALK_POST,ground - 0.1f,line,PAL_TIMBER);     //down into a dip
            }
            WalkPost(out,p,line,along);
        }
    }
    for (const WalkEnd& e : ends){
        if (e.stretches > 0){
            vec2 along = e.along;
            along.normalize();
            WalkPost(out,e.at,e.y,along);
        }
    }

    /*
        --- THE YARD (YardSpot): the wheel, the coils and the sheaves. The sheaves against the wall, inside the walk;
        the wheel and the coils beyond it, clear of its crossbars.
    */
    int yard_plot = -1, yard_open = -1;
    if (!YardSpot(w,z,id,plots,yard_plot,yard_open)){
        return;     //built in on every side
    }
    vec2 o = g.fine.pos[yard_open] - g.fine.pos[yard_plot];
    float reach = o.length();
    o = o * (1.0f / reach);
    vec2 u(-o.y,o.x);           //along the wall, eastward
    if (u.x < 0.0f){
        u = u * -1.0f;
    }
    vec2 wall = BuildingWallCrossing(w,yard_plot,yard_open);
    //The walk's far side, out from the wall - or with no walk past the yard, just clear of the sheaves.
    bool f_walk_here = false;
    for (const WalkEnd& e : ends){
        f_walk_here = f_walk_here || (e.plot == yard_plot && e.open == yard_open);
    }
    float walk = f_walk_here ? reach * (ROPEWALK_OUT - 0.5f) + ROPEWALK_BAR * 0.5f : 0.2f;
    for (int i = 0; i < std::min(sheaves,ROPEWALK_SHEAVES); i++){
        vec2 p = wall + o * 0.1f + u * (ROPEWALK_SHEAF_SPACING * (i - (ROPEWALK_SHEAVES - 1) * 0.5f));
        HempSheaf(out,p,ZoneGroundHeight(w,z,yard_open,p),o * -1.0f);
    }
    //The wheel always faces the camera, across or along the walk as that runs: out past the walk by as much of
    //it as reaches that way.
    const vec2 in(1.0f,0.0f), crank(0.0f,1.0f);
    float depth = std::fabs(in.dot(o)) * ROPEWALK_WHEEL_R + std::fabs(crank.dot(o)) * 0.15f;
    vec2 wheel = wall + o * (walk + 0.05f + depth) - u * ROPEWALK_WHEEL_ASIDE;
    SpinningWheel(out,wheel,ZoneGroundHeight(w,z,yard_open,wheel),in,crank);
    uint32_t hash = MeshHash(id,0x726f7065u);
    for (int i = 0; i < std::min(coils,2 * ROPEWALK_COIL_STACK); i++){
        int stack = i / ROPEWALK_COIL_STACK;
        int layer = i % ROPEWALK_COIL_STACK;
        vec2 base = wall + o * (walk + 0.05f + ROPEWALK_COIL_R) + u * (0.15f + ROPEWALK_COIL_SPACING * stack);
        uint32_t lie = MeshHash(hash,(uint32_t)i);
        vec2 p = base + vec2((lie % 100) / 100.0f - 0.5f,((lie >> 8) % 100) / 100.0f - 0.5f) * 0.04f;
        RopeCoil(out,p,ZoneGroundHeight(w,z,yard_open,base) + layer * 2.0f * ROPEWALK_COIL_TUBE,((lie >> 16) % 100) / 100.0f);
    }
}

void BuildSailLoftYard(const ChasmWorld& w, const ZoneState& z, uint32_t id, int bolts, int sheaves,
                       std::vector<vertex>& out){
    const Grid& g = *w.grid;
    if (id == 0 || id >= z.buildings.size() || z.buildings[id].kind != ZONE_KIND_SAIL_LOFT){
        return;
    }
    std::vector<int> plots;     //what of it stands, in order
    for (size_t v = 0; v < z.building.size(); v++){
        if (z.building[v] == id && z.standing[v] > 0){
            plots.push_back((int)v);
        }
    }
    int yard_plot = -1, yard_open = -1;
    if (plots.empty() || !YardSpot(w,z,id,plots,yard_plot,yard_open)){
        return;     //nothing standing, or built in on every side
    }
    vec2 o = g.fine.pos[yard_open] - g.fine.pos[yard_plot];
    o = o * (1.0f / std::max(1e-4f,o.length()));
    vec2 u(-o.y,o.x);           //along the wall, eastward
    if (u.x < 0.0f){
        u = u * -1.0f;
    }
    vec2 wall = BuildingWallCrossing(w,yard_plot,yard_open);
    for (int i = 0; i < std::min(sheaves,ROPEWALK_SHEAVES); i++){
        vec2 p = wall + o * 0.1f + u * (SAIL_SHEAF_ASIDE + ROPEWALK_SHEAF_SPACING * i);
        HempSheaf(out,p,ZoneGroundHeight(w,z,yard_open,p),o * -1.0f);
    }
    //The frame faces the camera whichever way the yard lies, as the ropewalk's wheel does: out from the wall by as much
    //of it as reaches that way. Its canvas only while there is something to work - a bare frame is a loft out of hemp.
    const vec2 in(1.0f,0.0f);
    vec2 frame = wall + o * (SAIL_FRAME_OUT + std::fabs(in.dot(o)) * SAIL_FRAME_W * 0.5f) - u * SAIL_FRAME_ASIDE;
    TenterFrame(out,frame,ZoneGroundHeight(w,z,yard_open,frame),in,bolts > 0 || sheaves > 0);
    uint32_t hash = MeshHash(id,0x7361696cu);
    for (int i = 0; i < std::min(bolts,2 * SAIL_BOLT_STACK); i++){
        int stack = i / SAIL_BOLT_STACK;
        int layer = i % SAIL_BOLT_STACK;
        vec2 base = wall + o * SAIL_BOLT_OUT + u * (SAIL_BOLT_ASIDE + SAIL_BOLT_SPACING * stack);
        uint32_t lie = MeshHash(hash,(uint32_t)i);
        vec2 p = base + vec2((lie % 100) / 100.0f - 0.5f,((lie >> 8) % 100) / 100.0f - 0.5f) * 0.03f;
        float turn = (((lie >> 16) % 100) / 100.0f - 0.5f) * 0.3f;     //not laid quite square on the one under it
        vec2 along = u * std::cos(turn) + o * std::sin(turn);
        CanvasBolt(out,p,ZoneGroundHeight(w,z,yard_open,base) + layer * SAIL_BOLT_H,along);
    }
}
