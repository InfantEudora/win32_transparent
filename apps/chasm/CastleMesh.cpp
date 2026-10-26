#include "CastleMesh.h"
#include "BuildingMesh.h"
#include "MeshBuild.h"
#include "Walkers.h"

#include <algorithm>
#include <cmath>

#define CASTLE_SINK         0.15f   //walls into the ground, so a slope never shows a gap under one
//An ARCH (ArchVault, ArchFace). Its rise is one number for every side, not each side's own width, so the barrels of
//one vault - and two arches side by side - all crown at one height and meet; a side wider or narrower than usual
//gets a flatter or taller curve, as the grid's irregular plots give everything else a shape of its own.
#define CASTLE_ARCH_OPEN    0.72f   //the opening: this much of each half-side, from its middle; the rest is the corner's pier
#define CASTLE_ARCH_RISE    0.70f   //springing to crown: about half a usual opening's width, a round arch
#define CASTLE_ARCH_CROWN   0.42f   //stone over the crown, under the deck
#define CASTLE_ARCH_SEGS    5       //a quarter of the curve, crown to springing; the crown's segment is the keystone's half
#define CASTLE_RING         0.17f   //the voussoirs round an opening: how deep...
#define CASTLE_RING_OUT     0.035f  //...and how far proud of the face, a little more or less stone by stone
#define CASTLE_KEYSTONE     0.08f   //the keystone stands this much taller and further out
#define CASTLE_BATTLE_OUT   0.07f   //BATTLEMENTS along a deck's open edge: set out over the wall's face on a corbel...
#define CASTLE_CORBEL_H     0.22f   //...sloping out from this far under the deck...
#define CASTLE_BATTLE_W     0.16f   //...this thick...
#define CASTLE_BATTLE_SILL  0.36f   //...a low wall to their crenels' sills, over a villager's knee (0.86 tall)...
#define CASTLE_MERLON_H     0.30f   //...the merlons this much over it...
#define CASTLE_MERLON       0.30f   //...about this long, by turns with...
#define CASTLE_CRENEL       0.24f   //...a crenel about this long
#define CASTLE_PLINTH       0.30f   //a darker course along an outer wall's foot, over the ground...
#define CASTLE_PLINTH_OUT   0.06f   //...this proud, chamfered back to the wall
#define CASTLE_SLIT_W       0.045f  //an arrow slit in a block's outer wall, half its width...
#define CASTLE_SLIT_H       0.42f   //...and its height, in the middle of its top storey...
#define CASTLE_SLIT_FROM    1.5f    //...where it stands at least this many storeys over the ground outside
//A GATE (castle_plan.md): its passage always a two-storey wall's arch, springing this far over its level...
#define CASTLE_GATE_SPRING  (2.0f * ZONE_STOREY_HEIGHT - CASTLE_ARCH_CROWN - CASTLE_ARCH_RISE)
#define CASTLE_PORTCULLIS_IN 0.12f  //...its portcullis this far in from the arch's face...
#define CASTLE_PORTCULLIS_BAR 0.025f //...its uprights twice this wide...
#define CASTLE_PORTCULLIS_RAIL 0.02f //...its rails twice this deep...
#define CASTLE_PORTCULLIS_UP 0.78f  //...and raised, its foot this much of the arch's rise over the springing
#define CASTLE_DOOR_OFF     0.05f   //its doors, swung open, this far off the passage's wall...
#define CASTLE_DOOR_BATTEN  0.07f   //...their battens this deep...
#define CASTLE_DOOR_BATTEN_OUT 0.012f //...and this proud
#define CASTLE_STEPS        6                               //a stair's steps to a storey...
#define CASTLE_STEP         (ZONE_STOREY_HEIGHT / CASTLE_STEPS)     //...each this tall
#define CASTLE_STRING       0.12f   //a flight's open side: a string wall this thick, half a step over its treads
//A stair's BALUSTRADE (castle_plan.md "The stairs"), along the open sides of its flights and landings - the user's photo's,
//so the stairs stand out against the plain walls: on the string's middle, or this far in from a landing's edge...
#define CASTLE_BAL_IN       0.07f
#define CASTLE_BAL_BASE     0.05f   //...a base this tall...
#define CASTLE_BAL_BASE_W   0.06f   //...and half this wide...
#define CASTLE_BAL_H        0.30f   //...balusters this tall...
#define CASTLE_BAL_W        0.02f   //...half this wide, a belly...
#define CASTLE_BAL_BELLY    0.036f  //...half this wide...
#define CASTLE_BAL_BELLY_H  0.12f   //...this tall at their foot...
#define CASTLE_BAL_GAP      0.13f   //...about this far apart...
#define CASTLE_BAL_RAIL_W   0.055f  //...under a rail half this wide...
#define CASTLE_BAL_RAIL_H   0.05f   //...and this deep; where it turns or ends a newel post...
#define CASTLE_BAL_NEWEL    0.07f   //...half this wide, rising this much over the rail to its cap
#define CASTLE_BAL_NEWEL_UP 0.07f
//Under a flight or a landing a storey or more over the ground, a doorway in its open side: half this wide...
#define CASTLE_STAIR_DOOR_W 0.30f
#define CASTLE_STAIR_DOOR   1.15f   //...crowned this high at most...
#define CASTLE_STAIR_OVER_DOOR 0.28f    //...and this much under its lowest tread there
//An open TURRET (castle_plan.md "The stairs"): its steps out to this much of the way to its nearest crossing...
#define CASTLE_TURRET_REACH 0.92f
#define CASTLE_TURRET_CORE  0.34f   //...round a core this much of that wide...
#define CASTLE_TURRET_SLAB  0.12f   //...each step this much stone under its rise, so the soffit is a stepped helix...
#define CASTLE_TURRET_SEGS  16      //...the round parts this many sides; on a base this much wider than its steps...
#define CASTLE_TURRET_BASE  0.08f
#define CASTLE_TURRET_EXIT  0.34f   //...its last step on a landing to its exit, half this wide there

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
    A FLIGHT's steps over a quarter q (its point, the two edges' middles and the cell's middle), cut from its plane
    (CastleFlight): its run from foot to top in CASTLE_STEPS bands square to its way up, each a flat tread half way up
    its step - so a walker on the plane is never more than half a step off it - the first and the last going on to the
    quarter's edge, level; and between two bands a riser facing down the flight. Every quarter of the plot cuts the same
    bands, so the steps run straight across it from cell to cell.
*/
void FlightSteps(std::vector<vertex>& out, const CastleFlight& fl, const vec2* q){
    const float step = (fl.top_y - fl.foot_y) / CASTLE_STEPS;
    const float run = (fl.s_top - fl.s_foot) / CASTLE_STEPS;
    std::vector<RampPoint> quad;
    for (int i = 0; i < 4; i++){
        quad.push_back(RampPoint{q[i],fl.Along(q[i])});
    }
    const vec3 down(-fl.up.x,0.0f,-fl.up.y);
    for (int i = 0; i < CASTLE_STEPS; i++){
        float lo = (i == 0) ? -1.0e9f : fl.s_foot + run * i;
        float hi = fl.s_foot + run * (i + 1);
        std::vector<RampPoint> band = (i + 1 < CASTLE_STEPS) ? ClipRamp(ClipRamp(quad,lo,true),hi,false)
                                                              : ClipRamp(quad,lo,true);
        float tread = fl.foot_y + step * (i + 0.5f);
        for (size_t j = 1; j + 1 < band.size(); j++){
            MeshTri(out,At(band[0].at,tread),At(band[j].at,tread),At(band[j + 1].at,tread),vec3(0,1,0),PAL_STONE_LIGHT);
        }
        if (i + 1 >= CASTLE_STEPS){
            continue;
        }
        //Its riser up to the next, where the band ends - inside the quarter, if it does.
        std::vector<vec2> cross;
        for (int j = 0; j < 4; j++){
            const RampPoint& a = quad[j];
            const RampPoint& b = quad[(j + 1) % 4];
            if ((a.h < hi) != (b.h < hi)){
                cross.push_back(a.at + (b.at - a.at) * ((hi - a.h) / (b.h - a.h)));
            }
        }
        if (cross.size() == 2){
            MeshQuad(out,At(cross[0],tread),At(cross[1],tread),At(cross[1],tread + step),At(cross[0],tread + step),down,
                     PAL_STONE);
        }
    }
}

/*
    One side of a FLIGHT's plot in a quarter, a half-edge from its crossing `a` to the cell's middle `b`, facing out
    along `n`, standing on `ga`..`gb`: its face from there up to its treads, band by band, as stepped as they are - and on
    an OPEN side (`f_open`: a flank, nothing joining it there) up past them to a STRING wall CASTLE_STRING thick, half a
    step over the plane all along, with its inner face down to the treads and a coping. The balustrade on the string is
    the caller's.
*/
void FlightSide(std::vector<vertex>& out, const CastleFlight& fl, const vec2& a, const vec2& b, const vec2& n, float ga,
                float gb, bool f_open){
    const float step = (fl.top_y - fl.foot_y) / CASTLE_STEPS;
    const float run = (fl.s_top - fl.s_foot) / CASTLE_STEPS;
    const float ta = fl.Along(a);
    const float tb = fl.Along(b);
    //Where along it a band ends or the plane turns level: the faces' corners.
    std::vector<float> at = {0.0f,1.0f};
    auto cut = [&](float t){
        if (std::fabs(tb - ta) > 1e-6f){
            float s = (t - ta) / (tb - ta);
            if (s > 1e-4f && s < 1.0f - 1e-4f){
                at.push_back(s);
            }
        }
    };
    for (int i = 0; i <= CASTLE_STEPS; i++){
        cut(fl.s_foot + run * i);
    }
    std::sort(at.begin(),at.end());
    const vec3 want(n.x,0.0f,n.y);
    const vec2 in = n * -CASTLE_STRING;
    for (size_t j = 0; j + 1 < at.size(); j++){
        float s0 = at[j];
        float s1 = at[j + 1];
        vec2 p0 = a + (b - a) * s0;
        vec2 p1 = a + (b - a) * s1;
        float g0 = ga + (gb - ga) * s0;
        float g1 = ga + (gb - ga) * s1;
        int band = (int)std::floor((fl.Along((p0 + p1) * 0.5f) - fl.s_foot) / run);
        float tread = fl.foot_y + step * (std::max(0,std::min(CASTLE_STEPS - 1,band)) + 0.5f);
        if (!f_open){
            MeshQuad(out,At(p0,g0),At(p1,g1),At(p1,tread),At(p0,tread),want,PAL_STONE);
            continue;
        }
        float c0 = fl.HeightAt(p0) + 0.5f * step;
        float c1 = fl.HeightAt(p1) + 0.5f * step;
        MeshQuad(out,At(p0,g0),At(p1,g1),At(p1,c1),At(p0,c0),want,PAL_STONE);
        MeshQuad(out,At(p0 + in,tread),At(p1 + in,tread),At(p1 + in,c1),At(p0 + in,c0),want * -1.0f,PAL_STONE);
        MeshQuad(out,At(p0,c0),At(p1,c1),At(p1 + in,c1),At(p0 + in,c0),vec3(0,1,0),PAL_STONE_LIGHT);
    }
    //Its end at the cell's middle closed: the next side may not be a string's.
    if (f_open){
        float cb = fl.HeightAt(b) + 0.5f * step;
        int band = (int)std::floor((tb - fl.s_foot) / run);
        float tread = fl.foot_y + step * (std::max(0,std::min(CASTLE_STEPS - 1,band)) + 0.5f);
        vec2 d = b - a;
        MeshQuad(out,At(b,tread),At(b + in,tread),At(b + in,cb),At(b,cb),vec3(d.x,0.0f,d.y),PAL_STONE);
    }
}

//A box along p0 -> p1, `half` either side of the line in the ground's plane, from y0..y1 at its ends up `tall`: its
//top, its bottom and its two long sides - a rail, a balustrade's base.
void Beam(std::vector<vertex>& out, const vec2& p0, const vec2& p1, float y0, float y1, float tall, float half, int column){
    vec2 d = p1 - p0;
    float l = d.length();
    if (l < 1e-5f){
        return;
    }
    vec2 s = vec2(-d.y,d.x) * (half / l);
    vec3 side(s.x,0.0f,s.y);
    MeshQuad(out,At(p0 + s,y0 + tall),At(p1 + s,y1 + tall),At(p1 - s,y1 + tall),At(p0 - s,y0 + tall),vec3(0,1,0),column);
    MeshQuad(out,At(p0 + s,y0),At(p1 + s,y1),At(p1 - s,y1),At(p0 - s,y0),vec3(0,-1,0),column);
    MeshQuad(out,At(p0 + s,y0),At(p1 + s,y1),At(p1 + s,y1 + tall),At(p0 + s,y0 + tall),side,column);
    MeshQuad(out,At(p0 - s,y0),At(p1 - s,y1),At(p1 - s,y1 + tall),At(p0 - s,y0 + tall),side * -1.0f,column);
}

//A square post at p, its sides along `along` and across it, `half` wide each way, from y0 to y1 - its sides and top.
void Post(std::vector<vertex>& out, const vec2& p, const vec2& along, float half, float y0, float y1, int column){
    vec2 u = along * half;
    vec2 v = vec2(-along.y,along.x) * half;
    vec2 c[4] = {p - u - v,p + u - v,p + u + v,p - u + v};
    for (int i = 0; i < 4; i++){
        vec2 e = c[(i + 1) % 4];
        vec2 o = (c[i] + e) * 0.5f - p;
        MeshQuad(out,At(c[i],y0),At(e,y0),At(e,y1),At(c[i],y1),vec3(o.x,0.0f,o.y),column);
    }
    MeshQuad(out,At(c[0],y1),At(c[1],y1),At(c[2],y1),At(c[3],y1),vec3(0,1,0),column);
}

/*
    A BALUSTRADE (castle_plan.md "The stairs") along a side from its crossing `a` to the cell's middle `b`, stepped in
    along `oa` and `ob` - at the crossing the way both cells step in there (BuildingWallOut), so the two halves of a side
    meet - and standing on `line(s)`, the height of what it stands on s of the way along: a base, balusters with a belly
    at their foot, a rail; and with `f_newel` a newel post at b, where it turns or ends.
*/
template <class Line>
void Balustrade(std::vector<vertex>& out, const vec2& a, const vec2& b, const vec2& oa, const vec2& ob, float in,
                Line line, bool f_newel){
    vec2 pa = a - oa * in;
    vec2 pb = b - ob * in;
    vec2 d = pb - pa;
    float len = d.length();
    if (len < 1e-4f){
        return;
    }
    vec2 along = d * (1.0f / len);
    int count = std::max(1,(int)std::lround(len / CASTLE_BAL_GAP));
    const float rail = CASTLE_BAL_BASE + CASTLE_BAL_H;
    for (int j = 0; j < count; j++){
        float s0 = (float)j / count;
        float s1 = (float)(j + 1) / count;
        vec2 p0 = pa + d * s0;
        vec2 p1 = pa + d * s1;
        float y0 = line(s0);
        float y1 = line(s1);
        Beam(out,p0,p1,y0,y1,CASTLE_BAL_BASE,CASTLE_BAL_BASE_W,PAL_STONE);
        Beam(out,p0,p1,y0 + rail,y1 + rail,CASTLE_BAL_RAIL_H,CASTLE_BAL_RAIL_W,PAL_STONE_LIGHT);
        float sm = (s0 + s1) * 0.5f;
        float ym = line(sm) + CASTLE_BAL_BASE;
        vec2 pm = pa + d * sm;
        Post(out,pm,along,CASTLE_BAL_BELLY,ym,ym + CASTLE_BAL_BELLY_H,PAL_STONE_LIGHT);
        Post(out,pm,along,CASTLE_BAL_W,ym + CASTLE_BAL_BELLY_H,line(sm) + rail,PAL_STONE_LIGHT);
    }
    if (f_newel){
        float y = line(1.0f);
        Post(out,pb,along,CASTLE_BAL_NEWEL,y,y + rail + CASTLE_BAL_RAIL_H + CASTLE_BAL_NEWEL_UP,PAL_STONE);
        Post(out,pb,along,CASTLE_BAL_NEWEL + 0.015f,y + rail + CASTLE_BAL_RAIL_H + CASTLE_BAL_NEWEL_UP,
             y + rail + CASTLE_BAL_RAIL_H + CASTLE_BAL_NEWEL_UP + 0.035f,PAL_STONE_LIGHT);
    }
}

//A piece's top in a quarter: at its plot's point, at the middles of the two edges, and at the cell's middle.
struct QuarterTop{
    float v;
    float m[2];
    float c;
};

//...and anywhere in it, by the quarter's own coordinates (MeshBilinear: its point at 0,0, the cell's middle at 1,1).
float QuarterTopAt(const QuarterTop& t, float u, float v){
    float a = t.v + (t.m[0] - t.v) * u;
    float b = t.m[1] + (t.c - t.m[1]) * u;
    return a + (b - a) * v;
}

/*
    An ARCH's curve, sampled from the crown (0) to the springing (CASTLE_ARCH_SEGS), as the distance across from its
    side's middle in a quarter's own coordinates - spaced as equal steps round a circle, so the curve is even - and one
    more sample, 1, at the plot's corner past the pier. The vault, the faces and the ring all take these samples, so
    their edges are one another's.
*/
float ArchAcross(int j){
    if (j > CASTLE_ARCH_SEGS){
        return 1.0f;
    }
    return CASTLE_ARCH_OPEN * std::cos(1.5707963f * (float)(CASTLE_ARCH_SEGS - j) / CASTLE_ARCH_SEGS);
}

//...and its height over the springing there.
float ArchRise(int j){
    if (j >= CASTLE_ARCH_SEGS){
        return 0.0f;
    }
    return CASTLE_ARCH_RISE * std::sin(1.5707963f * (float)(CASTLE_ARCH_SEGS - j) / CASTLE_ARCH_SEGS);
}

/*
    The vault under an ARCH's quarter q (its point, the two edges' middles and the cell's middle, MeshBilinear's order):
    a GROIN VAULT. Each side of the plot opens in an arch crowned at its middle, where the fine edge to that neighbour
    crosses it, and a barrel runs from it in to the plot's point along that edge; the quarter's two barrels cross along
    its diagonal, the groin. Its corner, past both openings, is the pier, and has no ceiling. Each barrel is the same
    curve as the arch it comes from, so the vault meets the faces, the next quarter's barrel and the piers edge to edge.
*/
void ArchVault(std::vector<vertex>& out, const vec2* q, float spring){
    const int n = CASTLE_ARCH_SEGS + 2;
    for (int a = 0; a + 1 < n; a++){
        for (int b = 0; b + 1 < n; b++){
            if (a >= CASTLE_ARCH_SEGS && b >= CASTLE_ARCH_SEGS){
                continue;
            }
            //The ceiling goes by the nearer barrel's axis alone - the smaller coordinate - so a cell on the diagonal
            //folds along it, which MeshQuad's split from its first corner to its third is.
            vec3 c[4];
            const int du[4] = {0,1,1,0};
            const int dv[4] = {0,0,1,1};
            for (int i = 0; i < 4; i++){
                int ja = a + du[i];
                int jb = b + dv[i];
                c[i] = At(MeshBilinear(q,ArchAcross(ja),ArchAcross(jb)),spring + ArchRise(std::min(ja,jb)));
            }
            MeshQuad(out,c[0],c[1],c[2],c[3],vec3(0,-1,0),PAL_STONE_DARK);
        }
    }
}

/*
    One side of an ARCH's plot in a quarter, a half-edge from its middle `a` to the cell's middle `b`, facing out along
    `n`: the stone over the half arch, from the curve up to `ta`..`tb` (the deck's height there, with its parapet), and
    the pier past it from its feet `fa`..`fb` (at the springing and at b) to the top. With `f_ring`, the arch's half
    ring of voussoirs, standing a little proud of the face, the one at the crown half the keystone. `seed` is the
    side's own, so both quarters of one side cut their halves of the keystone alike.
*/
void ArchFace(std::vector<vertex>& out, const vec2& a, const vec2& b, const vec2& n, float spring, float ta, float tb,
              float fa, float fb, bool f_ring, uint32_t seed){
    vec3 want(n.x,0.0f,n.y);
    auto along = [&](float s){
        return a + (b - a) * s;
    };
    auto top = [&](float s){
        return ta + (tb - ta) * s;
    };
    for (int j = 0; j < CASTLE_ARCH_SEGS; j++){
        float s0 = ArchAcross(j);
        float s1 = ArchAcross(j + 1);
        MeshQuad(out,At(along(s0),spring + ArchRise(j)),At(along(s1),spring + ArchRise(j + 1)),At(along(s1),top(s1)),
                 At(along(s0),top(s0)),want,PAL_STONE);
    }
    MeshQuad(out,At(along(CASTLE_ARCH_OPEN),fa),At(b,fb),At(b,tb),At(along(CASTLE_ARCH_OPEN),top(CASTLE_ARCH_OPEN)),want,
             PAL_STONE);
    if (!f_ring){
        return;
    }
    //The ring: each voussoir between two samples, its outer edge further out along the same direction round the
    //curve's middle - the half-side's length turns the ring's depth into the face's coordinate.
    float len = std::max(1e-4f,(b - a).length());
    vec3 middle = At(a,spring);
    for (int j = 0; j < CASTLE_ARCH_SEGS; j++){
        bool f_key = j == 0;
        float deep = CASTLE_RING + (f_key ? CASTLE_KEYSTONE : 0.0f);
        float proud = f_key ? CASTLE_RING_OUT + CASTLE_KEYSTONE * 0.5f
                            : CASTLE_RING_OUT * (0.6f + 0.8f * (float)(MeshHash(seed,(uint32_t)j) % 100) / 100.0f);
        vec3 in[2];
        vec3 rim[2];
        for (int e = 0; e < 2; e++){
            int s = j + e;
            float c = ArchAcross(s) / CASTLE_ARCH_OPEN;                     //the curve's cosine...
            float r = ArchRise(s) / CASTLE_ARCH_RISE;                       //...and sine
            in[e] = At(along(ArchAcross(s)),spring + ArchRise(s));
            float across = std::min(1.0f,c * (CASTLE_ARCH_OPEN + deep / len));
            rim[e] = At(along(across),spring + r * (CASTLE_ARCH_RISE + deep));
        }
        vec3 o(n.x * proud,0.0f,n.y * proud);
        MeshQuad(out,in[0] + o,in[1] + o,rim[1] + o,rim[0] + o,want,PAL_STONE_LIGHT);
        vec3 inward = middle - (in[0] + in[1]) * 0.5f;
        MeshQuad(out,in[0],in[1],in[1] + o,in[0] + o,inward,PAL_STONE);
        MeshQuad(out,rim[0],rim[1],rim[1] + o,rim[0] + o,inward * -1.0f,PAL_STONE_LIGHT);
        if (!f_key){
            MeshQuad(out,in[0],rim[0],rim[0] + o,in[0] + o,in[0] - in[1],PAL_STONE);      //the keystone's other half is there
        }
        MeshQuad(out,in[1],rim[1],rim[1] + o,in[1] + o,in[1] - in[0],PAL_STONE);
    }
}

/*
    What a quarter's two sides leave for the OUTLINES drawn over a whole cell at once - the BATTLEMENTS along a deck's
    open edges and the PLINTH along an outer wall's foot - so that where two meet at the cell's middle, one quarter's or
    two plots', they are mitred to each other (OutlineJoin) and nothing opens between them or overlaps.
*/
struct QuarterSide{
    bool f_battlements = false;     //an open edge of a deck: a wall-walk's
    bool f_plinth = false;          //a wall's foot on open ground
    bool f_slit = false;            //a block's outer wall tall enough for an arrow slit
    vec2 a;                         //its crossing...
    vec2 b;                         //...and the cell's middle
    vec2 n;                         //outward
    float ta = 0.0f;                //the deck's height at each
    float tb = 0.0f;
    float ga = 0.0f;                //the ground's
    float gb = 0.0f;
};
struct QuarterSides{
    QuarterSide side[2];            //toward corner k + 1, toward k + 3
};

#define CASTLE_JOIN_ON      0       //an outline goes straight on into the next plot's
#define CASTLE_JOIN_CORNER  1       //...turns round its quarter's own corner
#define CASTLE_JOIN_INSIDE  2       //...turns into an inner corner, round a quarter outside it
#define CASTLE_JOIN_END     3       //...or meets nothing and ends

/*
    How the outline along corner h's side s turns at the cell's middle: `on[k][i]` whether corner k's side i carries it,
    `n` the sides' outward normals and `at` their heights there, which must agree for two to meet. Round h's own corner;
    into an inner one, where the corner across the quarter outside turns it back; straight on into the next plot's; or an
    end. `out` the step out there that keeps each side its own distance off (MeshMitre) - the same from either side of a
    meeting, so the two meet on one line.
*/
int OutlineJoin(const bool on[4][2], const vec2 n[4][2], const float at[4][2], int h, int s, vec2& out){
    int o = 1 - s;
    if (on[h][o]){
        out = MeshMitre(n[h][s],n[h][o]);
        return CASTLE_JOIN_CORNER;
    }
    int across = (h + 2) % 4;
    if (on[across][o] && std::fabs(at[across][o] - at[h][s]) < 1e-3f){
        out = MeshMitre(n[h][s],n[across][o]);
        return CASTLE_JOIN_INSIDE;
    }
    int next = (h + ((s == 0) ? 3 : 1)) % 4;
    if (on[next][o] && std::fabs(at[next][o] - at[h][s]) < 1e-3f){
        out = MeshMitre(n[h][s],n[next][o]);
        return CASTLE_JOIN_ON;
    }
    out = n[h][s];
    return CASTLE_JOIN_END;
}

/*
    The BATTLEMENTS along one side, from its crossing - stepped out along `om`, as the cell across has it - to the cell's
    middle, out along `oc`, its join's: corbelled out over the wall's face, a low wall, and over it merlons by turns with
    crenels. Half a merlon at the crossing, the next cell's the rest; at the middle half of one going on into the next
    plot's, or a whole one round a corner or at an end, its end closed.
*/
void Battlements(std::vector<vertex>& out, const QuarterSide& sd, const vec2& om, const vec2& oc, int join){
    const vec3 up(0.0f,1.0f,0.0f);
    const vec3 want(sd.n.x,0.0f,sd.n.y);
    const float O = CASTLE_BATTLE_OUT;
    const float I = CASTLE_BATTLE_OUT - CASTLE_BATTLE_W;
    auto P = [&](float s, float by){
        vec2 a = sd.a + om * by;
        vec2 b = sd.b + oc * by;
        return a + (b - a) * s;
    };
    auto T = [&](float s){
        return sd.ta + (sd.tb - sd.ta) * s;
    };
    vec2 d = sd.b - sd.a;
    float len = std::max(1e-4f,d.length());
    const vec3 along(d.x,0.0f,d.y);
    const float sill = CASTLE_BATTLE_SILL;
    MeshQuad(out,At(P(0.0f,0.0f),T(0.0f) - CASTLE_CORBEL_H),At(P(1.0f,0.0f),T(1.0f) - CASTLE_CORBEL_H),At(P(1.0f,O),T(1.0f)),
             At(P(0.0f,O),T(0.0f)),vec3(sd.n.x,-1.0f,sd.n.y),PAL_STONE);
    MeshQuad(out,At(P(0.0f,O),T(0.0f)),At(P(1.0f,O),T(1.0f)),At(P(1.0f,O),T(1.0f) + sill),At(P(0.0f,O),T(0.0f) + sill),
             want,PAL_STONE);
    MeshQuad(out,At(P(0.0f,I),T(0.0f)),At(P(1.0f,I),T(1.0f)),At(P(1.0f,I),T(1.0f) + sill),At(P(0.0f,I),T(0.0f) + sill),
             want * -1.0f,PAL_STONE);
    MeshQuad(out,At(P(0.0f,O),T(0.0f) + sill),At(P(1.0f,O),T(1.0f) + sill),At(P(1.0f,I),T(1.0f) + sill),
             At(P(0.0f,I),T(0.0f) + sill),up,PAL_STONE_LIGHT);
    bool f_end = join == CASTLE_JOIN_END;
    if (f_end){
        MeshQuad(out,At(P(1.0f,O),T(1.0f)),At(P(1.0f,I),T(1.0f)),At(P(1.0f,I),T(1.0f) + sill),At(P(1.0f,O),T(1.0f) + sill),
                 along,PAL_STONE);
        MeshTri(out,At(P(1.0f,0.0f),T(1.0f) - CASTLE_CORBEL_H),At(P(1.0f,O),T(1.0f)),At(P(1.0f,0.0f),T(1.0f)),along,PAL_STONE);
    }
    auto merlon = [&](float x0, float x1, bool f_end0, bool f_end1){
        float s0 = x0 / len;
        float s1 = x1 / len;
        vec2 o0 = P(s0,O);
        vec2 o1 = P(s1,O);
        vec2 i0 = P(s0,I);
        vec2 i1 = P(s1,I);
        float y0 = T(s0) + sill;
        float y1 = T(s1) + sill;
        float h = CASTLE_MERLON_H;
        MeshQuad(out,At(o0,y0),At(o1,y1),At(o1,y1 + h),At(o0,y0 + h),want,PAL_STONE);
        MeshQuad(out,At(i0,y0),At(i1,y1),At(i1,y1 + h),At(i0,y0 + h),want * -1.0f,PAL_STONE);
        MeshQuad(out,At(o0,y0 + h),At(o1,y1 + h),At(i1,y1 + h),At(i0,y0 + h),up,PAL_STONE_LIGHT);
        if (f_end0){
            MeshQuad(out,At(o0,y0),At(i0,y0),At(i0,y0 + h),At(o0,y0 + h),along * -1.0f,PAL_STONE);
        }
        if (f_end1){
            MeshQuad(out,At(o1,y1),At(i1,y1),At(i1,y1 + h),At(o1,y1 + h),along,PAL_STONE);
        }
    };
    //Half a merlon at the crossing and the one at the middle; between them crenels and merlons by turns, crenels at
    //both ends, stretched or squeezed to fit.
    float half = std::min(CASTLE_MERLON * 0.5f,len * 0.25f);
    float last = (join == CASTLE_JOIN_ON) ? half : std::min(CASTLE_MERLON,len * 0.4f);
    float span = len - half - last;
    if (span < CASTLE_CRENEL * 0.5f){
        merlon(0.0f,len,false,f_end);
        return;
    }
    int crenels = std::max(1,(int)std::lround((span + CASTLE_MERLON) / (CASTLE_MERLON + CASTLE_CRENEL)));
    float fit = span / (crenels * CASTLE_CRENEL + (crenels - 1) * CASTLE_MERLON);
    merlon(0.0f,half,false,true);
    float x = half;
    for (int j = 0; j < crenels; j++){
        x += CASTLE_CRENEL * fit;
        if (j + 1 < crenels){
            merlon(x,x + CASTLE_MERLON * fit,true,true);
            x += CASTLE_MERLON * fit;
        }
    }
    merlon(len - last,len,true,f_end);
}

//The PLINTH along one side's foot, stepped out as Battlements' are: a darker course over the ground, chamfered back to
//the wall, its end closed where the outline ends.
void Plinth(std::vector<vertex>& out, const QuarterSide& sd, const vec2& om, const vec2& oc, int join){
    const float O = CASTLE_PLINTH_OUT;
    vec2 pa = sd.a + om * O;
    vec2 pb = sd.b + oc * O;
    float ya = sd.ga - CASTLE_SINK;
    float yb = sd.gb - CASTLE_SINK;
    float ha = sd.ga + CASTLE_PLINTH;
    float hb = sd.gb + CASTLE_PLINTH;
    MeshQuad(out,At(pa,ya),At(pb,yb),At(pb,hb),At(pa,ha),vec3(sd.n.x,0.0f,sd.n.y),PAL_STONE_DARK);
    MeshQuad(out,At(pa,ha),At(pb,hb),At(sd.b,hb + O),At(sd.a,ha + O),vec3(sd.n.x,1.0f,sd.n.y),PAL_STONE_DARK);
    if (join == CASTLE_JOIN_END){
        vec2 d = sd.b - sd.a;
        MeshQuad(out,At(sd.b,yb),At(pb,yb),At(pb,hb),At(sd.b,hb + O),vec3(d.x,0.0f,d.y),PAL_STONE_DARK);
    }
}

//How high what stands on plot u rises as a wall, for a deck beside it to stand against: a block's or a stair's top - not
//an arch's, open under it - a stone tower's, a terrace's, a building's eaves; with none, nowhere.
float StandingTop(const ChasmWorld& w, const ZoneState& z, int u){
    float top = -1.0e9f;
    int shape = ZoneCastleShape(z,u);
    if (shape == ZONE_CASTLE_BLOCK || shape == ZONE_CASTLE_STAIR || shape == ZONE_CASTLE_LANDING){
        top = BuildingLayerHeight(w,z,u,ZoneCastleLayer(z,u));
    }
    if (ZoneTowerStone(z,u) && z.standing[u] > 0){
        top = std::max(top,BuildingTowerDeck(w,z,u));
    }
    if (ZoneTerraceStands(z,u)){
        top = std::max(top,ZoneTerraceTop(z,u));
    }
    int kind = z.KindOf(u);
    if (z.standing[u] > 0 && ZoneKindHasBody(kind)){
        top = std::max(top,(BuildingFootHalves(w,z,kind,u) + 2 * z.standing[u]) * ZONE_STOREY_HEIGHT * 0.5f);
    }
    return top;
}

//Whether fine cell `cell` is the lower-numbered of the two either side of the fine edge from plot v to plot u, or the
//only one: the one that draws something centred on the edge's crossing whole, the arrow slits.
bool FirstCellOfEdge(const ChasmWorld& w, int v, int u, int cell){
    const GridPicker& picker = *w.picker;
    for (int j = 0; j < picker.PlotQuadCount(v); j++){
        int q = picker.PlotQuadCorner(v,j) / 4;
        const GridQuad& gq = w.grid->fine.quads[q];
        if (q < cell && (gq.v[0] == u || gq.v[1] == u || gq.v[2] == u || gq.v[3] == u)){
            return false;
        }
    }
    return true;
}

/*
    The cell's outlines, one layer at a time - the pieces' own, then the ones over them: each quarter side's battlements
    and plinth, mitred at the middle to what they meet, and an arrow slit in a tall block's outer wall.
*/
void Outlines(std::vector<vertex>& out, const ChasmWorld& w, const GridQuad& quad, int fine_quad, const QuarterSides* q){
    bool battle[4][2];
    bool plinth[4][2];
    vec2 n[4][2];
    float top[4][2];
    float ground[4][2];
    for (int k = 0; k < 4; k++){
        for (int i = 0; i < 2; i++){
            const QuarterSide& sd = q[k].side[i];
            battle[k][i] = sd.f_battlements;
            plinth[k][i] = sd.f_plinth;
            n[k][i] = sd.n;
            top[k][i] = sd.tb;
            ground[k][i] = 0.0f;    //a plinth goes on along the ground whatever its height
        }
    }
    for (int k = 0; k < 4; k++){
        for (int i = 0; i < 2; i++){
            const QuarterSide& sd = q[k].side[i];
            if (!sd.f_battlements && !sd.f_plinth){
                continue;
            }
            int nb = quad.v[(k + ((i == 0) ? 1 : 3)) % 4];
            vec2 om = BuildingWallOut(w,quad.v[k],nb);
            vec2 oc;
            if (sd.f_battlements){
                Battlements(out,sd,om,oc,OutlineJoin(battle,n,top,k,i,oc));
            }
            if (sd.f_plinth){
                Plinth(out,sd,om,oc,OutlineJoin(plinth,n,ground,k,i,oc));
            }
            if (sd.f_slit && FirstCellOfEdge(w,quad.v[k],nb,fine_quad)){
                vec2 along(-om.y,om.x);
                float y0 = sd.ta - ZONE_STOREY_HEIGHT * 0.5f - CASTLE_SLIT_H * 0.5f;
                BuildingArchedOpening(out,sd.a - along * 0.5f,sd.a + along * 0.5f,vec3(om.x,0.0f,om.y),0.5f,CASTLE_SLIT_W,y0,
                                      y0 + CASTLE_SLIT_H,PAL_VOID,PAL_EFFECTS,PAL_STONE_LIGHT,false);
            }
        }
    }
}

//Whether a castle GATE opens toward its neighbour u (castle_plan.md): along its road - a road plot, or the next plot of a
//gate two plots wide - the way the walkers go through it (ZoneCastleGateWay). Every other side is solid wall.
bool GateOpensToward(const ZoneState& z, int u){
    return (z.ground[u] == ZONE_GROUND_ROAD && ZoneCastleShape(z,u) == ZONE_CASTLE_NONE) || ZoneCastleGate(z,u);
}

//The lowest-numbered neighbour the gate on v opens toward: the side its doors swing back to, and whose quarters draw them.
int GateFirstOpening(const ChasmWorld& w, const ZoneState& z, int v){
    int nb[8];
    int n = ZonePlotNeighbours(w,v,nb,8);
    int first = -1;
    for (int i = 0; i < n; i++){
        if (GateOpensToward(z,nb[i]) && (first < 0 || nb[i] < first)){
            first = nb[i];
        }
    }
    return first;
}

//A flat face seen from both sides: a portcullis's bars, a door's leaf.
void TwoSided(std::vector<vertex>& out, const vec3& a, const vec3& b, const vec3& c, const vec3& d, int column){
    vec3 face = (b - a).cross(d - a);
    MeshQuad(out,a,b,c,d,face,column);
    MeshQuad(out,a,b,c,d,face * -1.0f,column);
}

/*
    A gate's quarter open on side o only: a BARREL VAULT from its arch in along the fine edge to the plot's point - the
    arch's curve (ArchAcross), so it meets the arch's face and the next quarter's half of it - and the passage's WALL
    where the curve springs, from the road up. `foot` is the quarter's ground at its own coordinates.
*/
template <typename Foot>
void GateBarrel(std::vector<vertex>& out, const vec2* q, int o, float spring, const Foot& foot){
    //Along the passage (0 at the plot's point, 1 at the arch) and across it (0 its middle, 1 the quarter's closed side).
    auto P = [&](float along, float across){
        return (o == 0) ? MeshBilinear(q,along,across) : MeshBilinear(q,across,along);
    };
    auto F = [&](float along, float across){
        return (o == 0) ? foot(along,across) : foot(across,along);
    };
    for (int j = 0; j < CASTLE_ARCH_SEGS; j++){
        float c0 = ArchAcross(j);
        float c1 = ArchAcross(j + 1);
        float y0 = spring + ArchRise(j);
        float y1 = spring + ArchRise(j + 1);
        MeshQuad(out,At(P(0.0f,c0),y0),At(P(1.0f,c0),y0),At(P(1.0f,c1),y1),At(P(0.0f,c1),y1),vec3(0,-1,0),PAL_STONE_DARK);
    }
    const float c = CASTLE_ARCH_OPEN;
    vec2 w0 = P(0.0f,c);
    vec2 w1 = P(1.0f,c);
    vec2 in = P(0.5f,0.0f) - P(0.5f,c);
    MeshQuad(out,At(w0,F(0.0f,c)),At(w1,F(1.0f,c)),At(w1,spring),At(w0,spring),At(in,0.0f),PAL_STONE);
}

/*
    A gate's PORTCULLIS in the arch on side i, its half of it: a grille of timber a little in from the face, its uprights
    up to the arch's curve. DOWN, to the road, rails across it; UP, drawn into the wall over the arch, only the uprights'
    feet showing under the crown.
*/
void Portcullis(std::vector<vertex>& out, const ChasmWorld& w, const vec2* q, int i, float spring, float level, bool f_down){
    const Terrain& t = *w.terrain;
    auto P = [&](float along, float across){
        return (i == 0) ? MeshBilinear(q,along,across) : MeshBilinear(q,across,along);
    };
    float depth = std::max(1e-4f,(P(1.0f,0.0f) - P(0.0f,0.0f)).length());
    float along = 1.0f - CASTLE_PORTCULLIS_IN / depth;
    vec2 base = P(along,0.0f);
    vec2 edge = P(along,CASTLE_ARCH_OPEN);
    float half = (edge - base).length();
    if (half < 1e-4f){
        return;
    }
    vec2 across = (edge - base) * (1.0f / half);
    auto curve = [&](float x){
        float r = x / half;
        return spring + CASTLE_ARCH_RISE * std::sqrt(std::max(0.0f,1.0f - r * r));
    };
    float road = t.GroundHeight(base,level);
    float lift = f_down ? 0.0f : spring + CASTLE_ARCH_RISE * CASTLE_PORTCULLIS_UP - road;
    for (int j = 0; j < 3; j++){
        float x = half * (0.18f + 0.36f * j);
        if (x > half * 0.95f){
            break;
        }
        vec2 c = base + across * x;
        float bottom = t.GroundHeight(c,level) - CASTLE_SINK + lift;
        float top = curve(x + CASTLE_PORTCULLIS_BAR);
        if (bottom >= top - 0.02f){
            continue;
        }
        vec2 p0 = c - across * CASTLE_PORTCULLIS_BAR;
        vec2 p1 = c + across * CASTLE_PORTCULLIS_BAR;
        TwoSided(out,At(p0,bottom),At(p1,bottom),At(p1,top),At(p0,top),PAL_BARK_DARK);
    }
    if (!f_down){
        return;
    }
    for (int r = 0; r < 3; r++){
        float y = road + 0.3f + 0.42f * r;
        float x1 = (y <= spring) ? half
                 : half * std::sqrt(std::max(0.0f,1.0f - ((y - spring) / CASTLE_ARCH_RISE) * ((y - spring) / CASTLE_ARCH_RISE)));
        if (x1 < 0.05f){
            continue;
        }
        vec2 e = base + across * x1;
        TwoSided(out,At(base,y - CASTLE_PORTCULLIS_RAIL),At(e,y - CASTLE_PORTCULLIS_RAIL),At(e,y + CASTLE_PORTCULLIS_RAIL),
                 At(base,y + CASTLE_PORTCULLIS_RAIL),PAL_BARK_DARK);
    }
}

/*
    A gate's DOORS, the leaf in this quarter: across the passage at the plot's point, from the passage's wall to its
    middle, the road to the springing; SHUT across it, OPEN swung back on its hinge against the wall toward the arch on side
    o. Over it a timber TYMPANUM fills the arch, open or shut.
*/
template <typename Foot>
void GateDoors(std::vector<vertex>& out, const vec2* q, int o, float spring, const Foot& foot, bool f_shut){
    auto P = [&](float along, float across){
        return (o == 0) ? MeshBilinear(q,along,across) : MeshBilinear(q,across,along);
    };
    const float c = CASTLE_ARCH_OPEN;
    for (int j = 0; j < CASTLE_ARCH_SEGS; j++){
        vec2 a0 = P(0.0f,ArchAcross(j));
        vec2 a1 = P(0.0f,ArchAcross(j + 1));
        TwoSided(out,At(a0,spring),At(a1,spring),At(a1,spring + ArchRise(j + 1)),At(a0,spring + ArchRise(j)),PAL_TIMBER);
    }
    vec2 middle = P(0.0f,0.0f);
    vec2 hinge = P(0.0f,c);
    float width = (hinge - middle).length();
    if (width < 1e-4f){
        return;
    }
    vec2 inward = (middle - hinge) * (1.0f / width);
    vec2 base = hinge;
    vec2 tip = middle;
    if (!f_shut){
        vec2 wall = P(1.0f,c) - hinge;
        wall = wall * (1.0f / std::max(1e-4f,wall.length()));
        base = hinge + inward * CASTLE_DOOR_OFF;
        tip = base + wall * width;
    }
    float bottom = (o == 0) ? foot(0.0f,c) : foot(c,0.0f);
    float top = spring;     //up to the tympanum, edge to edge
    TwoSided(out,At(base,bottom),At(tip,bottom),At(tip,top),At(base,top),PAL_TIMBER);
    //Its battens, a hair proud on both faces.
    vec2 d = tip - base;
    vec2 side(-d.y,d.x);
    side = side * (CASTLE_DOOR_BATTEN_OUT / std::max(1e-4f,side.length()));
    float bands[2] = {bottom + CASTLE_SINK + 0.3f,top - 0.32f};
    for (float y : bands){
        for (int s = -1; s <= 1; s += 2){
            vec2 o2 = side * (float)s;
            TwoSided(out,At(base + o2,y),At(tip + o2,y),At(tip + o2,y + CASTLE_DOOR_BATTEN),At(base + o2,y + CASTLE_DOOR_BATTEN),
                     PAL_BARK_DARK);
        }
    }
}

/*
    An open TURRET, whole (castle_plan.md "The stairs", the user's photo's curved stair): a round BASE from the ground up
    to its level; its CORE, a round pier up past its top to a cap; its STEPS round it, each a block from its tread down
    its rise and a slab more, so the soffit under them climbs in steps too; a BALUSTRADE on their outer edge, its rail
    climbing with them; and its last step on a LANDING out to its exit, its sides balustraded.
*/
void Turret(std::vector<vertex>& out, const ChasmWorld& w, const CastleTurret& tu, int plot){
    const Terrain& t = *w.terrain;
    const float TAU = 6.2831853f;
    const vec3 up(0.0f,1.0f,0.0f);
    float ground = t.GroundHeight(tu.at,t.Height(plot));
    float floor = tu.base_y + CASTLE_BAL_BASE;      //the base's top
    auto radial = [&](float a){
        return vec3(std::cos(a),0.0f,std::sin(a));
    };
    //Its base, a drum.
    float rb = tu.outer + CASTLE_TURRET_BASE;
    for (int j = 0; j < CASTLE_TURRET_SEGS; j++){
        float a0 = TAU * j / CASTLE_TURRET_SEGS;
        float a1 = TAU * (j + 1) / CASTLE_TURRET_SEGS;
        MeshQuad(out,At(tu.Round(a0,rb),ground - CASTLE_SINK),At(tu.Round(a1,rb),ground - CASTLE_SINK),At(tu.Round(a1,rb),floor),
                 At(tu.Round(a0,rb),floor),radial((a0 + a1) * 0.5f),PAL_STONE_DARK);
        MeshTri(out,At(tu.at,floor),At(tu.Round(a0,rb),floor),At(tu.Round(a1,rb),floor),up,PAL_STONE);
    }
    //Its core, to a cap a rail's height over its top.
    const float rail = CASTLE_BAL_BASE + CASTLE_BAL_H;
    float cap = tu.top_y + rail + CASTLE_BAL_RAIL_H;
    for (int j = 0; j < CASTLE_TURRET_SEGS / 2; j++){
        float a0 = TAU * j / (CASTLE_TURRET_SEGS / 2);
        float a1 = TAU * (j + 1) / (CASTLE_TURRET_SEGS / 2);
        vec3 o = radial((a0 + a1) * 0.5f);
        MeshQuad(out,At(tu.Round(a0,tu.core),floor),At(tu.Round(a1,tu.core),floor),At(tu.Round(a1,tu.core),cap),
                 At(tu.Round(a0,tu.core),cap),o,PAL_STONE);
        float rc = tu.core + 0.04f;
        MeshQuad(out,At(tu.Round(a0,rc),cap),At(tu.Round(a1,rc),cap),At(tu.Round(a1,rc),cap + 0.06f),At(tu.Round(a0,rc),cap + 0.06f),
                 o,PAL_STONE_LIGHT);
        MeshTri(out,At(tu.Round(a0,rc),cap + 0.06f),At(tu.Round(a1,rc),cap + 0.06f),At(tu.at,cap + 0.06f + tu.core),
                o + up,PAL_STONE_LIGHT);
    }
    //Its steps, and the balustrade on their outer edge - balusters on each tread, the rail climbing from step to step.
    const float step = (tu.top_y - tu.base_y) / tu.steps;
    const float turn = tu.sweep / tu.steps;
    const float rr = tu.outer - CASTLE_BAL_IN;
    //(The last step is the landing's.)
    for (int i = 0; i + 1 < tu.steps; i++){
        float a0 = tu.start + turn * i;
        float a1 = a0 + turn;
        float y = tu.base_y + step * (i + 1);
        float bottom = std::max(floor,y - step - CASTLE_TURRET_SLAB);
        vec2 i0 = tu.Round(a0,tu.core);
        vec2 o0 = tu.Round(a0,tu.outer);
        vec2 i1 = tu.Round(a1,tu.core);
        vec2 o1 = tu.Round(a1,tu.outer);
        vec3 ahead(-std::sin(a0),0.0f,std::cos(a0));
        MeshQuad(out,At(i0,y),At(o0,y),At(o1,y),At(i1,y),up,PAL_STONE_LIGHT);
        MeshQuad(out,At(i0,bottom),At(o0,bottom),At(o0,y),At(i0,y),ahead * -1.0f,PAL_STONE);
        MeshQuad(out,At(o0,bottom),At(o1,bottom),At(o1,y),At(o0,y),radial((a0 + a1) * 0.5f),PAL_STONE);
        if (bottom > floor + 1e-3f){
            MeshQuad(out,At(i0,bottom),At(o0,bottom),At(o1,bottom),At(i1,bottom),up * -1.0f,PAL_STONE);
        }
        //Balusters on the tread, and the rail from the middle of its rise to the next's.
        vec2 r0 = tu.Round(a0,rr);
        vec2 r1 = tu.Round(a1,rr);
        vec2 along = r1 - r0;
        float len = std::max(1e-4f,along.length());
        along = along * (1.0f / len);
        Beam(out,r0,r1,y,y,CASTLE_BAL_BASE,CASTLE_BAL_BASE_W,PAL_STONE);
        int count = std::max(1,(int)std::lround(len / CASTLE_BAL_GAP));
        float rail0 = y - 0.5f * step + rail;
        for (int j = 0; j < count; j++){
            float s = (j + 0.5f) / count;
            vec2 pm = r0 + (r1 - r0) * s;
            float ym = y + CASTLE_BAL_BASE;
            Post(out,pm,along,CASTLE_BAL_BELLY,ym,ym + CASTLE_BAL_BELLY_H,PAL_STONE_LIGHT);
            Post(out,pm,along,CASTLE_BAL_W,ym + CASTLE_BAL_BELLY_H,rail0 + step * s,PAL_STONE_LIGHT);
        }
        Beam(out,r0,r1,rail0,rail0 + step,CASTLE_BAL_RAIL_H,CASTLE_BAL_RAIL_W,PAL_STONE_LIGHT);
    }
    //The landing its last step comes out on, out to its exit: its last step's span and half as much again, and the exit
    //either side of its crossing - a slab under it, balustrades along its two sides, a newel at each end.
    float a_in = tu.start + tu.sweep - turn;
    float a_on = tu.start + tu.sweep + turn * 0.5f;
    vec2 side = tu.exit - tu.at;
    side = vec2(-side.y,side.x) * (CASTLE_TURRET_EXIT / std::max(1e-4f,side.length()));
    vec2 slab[6] = {tu.Round(a_in,tu.core),tu.Round(a_in,tu.outer),tu.exit - side,tu.exit + side,tu.Round(a_on,tu.outer),
                    tu.Round(a_on,tu.core)};
    float y = tu.top_y;
    float under = y - step - CASTLE_TURRET_SLAB;
    for (int j = 1; j + 1 < 6; j++){
        MeshTri(out,At(slab[0],y),At(slab[j],y),At(slab[j + 1],y),up,PAL_STONE_LIGHT);
        MeshTri(out,At(slab[0],under),At(slab[j],under),At(slab[j + 1],under),up * -1.0f,PAL_STONE);
    }
    //Its edges but the exit's: the one onto it its last riser, from the step before.
    for (int j = 0; j < 5; j++){
        vec2 o = (slab[j] + slab[j + 1]) * 0.5f - tu.at;
        vec3 face = (j == 0) ? vec3(std::sin(a_in),0.0f,-std::cos(a_in)) : vec3(o.x,0.0f,o.y);
        if (j != 2){
            MeshQuad(out,At(slab[j],under),At(slab[j + 1],under),At(slab[j + 1],y),At(slab[j],y),face,PAL_STONE);
        }
    }
    auto flat = [&](float){
        return y;
    };
    vec2 in1 = (tu.at - slab[1]);
    vec2 in4 = (tu.at - slab[4]);
    Balustrade(out,slab[2],slab[1],side * (-1.0f / CASTLE_TURRET_EXIT),in1 * (-1.0f / std::max(1e-4f,in1.length())),
               CASTLE_BAL_IN,flat,true);
    Balustrade(out,slab[3],slab[4],side * (1.0f / CASTLE_TURRET_EXIT),in4 * (-1.0f / std::max(1e-4f,in4.length())),
               CASTLE_BAL_IN,flat,true);
}

/*
    One piece's quarter of a cell: its corner k. `shape` and `layer` are the piece's - the plot's own, or the one over
    it, which stands on `under`, the top of the piece under it, rather than on the ground: an arch its pillar, a stair
    its sides. `f_covered`: a stair sits on it, so it draws no top and no battlements of its own. Returns its top, and in
    `sides` what its sides leave for the cell's outlines (Outlines).
*/
QuarterTop PieceQuarter(std::vector<vertex>& out, const ChasmWorld& w, const ZoneState& z, const GridQuad& quad,
                        const vec2* p, int k, int shape, int layer, const QuarterTop* under, bool f_covered,
                        QuarterSides& sides){
    const Terrain& t = *w.terrain;
    const vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    int v = quad.v[k];
    float level = t.Height(v);
    //Its top over its level, whole storeys (ZoneCastleBase): flat across a hill, as a terrace's (terraces_plan.md).
    float top_v = BuildingLayerHeight(w,z,v,layer);
    //Toward this cell's two other plots beside it: where the walls cross the edge - a building's and a terrace's, so a
    //piece meets them wall on wall, and round a point of five or six cells is the n-gon theirs is - and the top's
    //height there.
    int nb[2] = {quad.v[(k + 1) % 4],quad.v[(k + 3) % 4]};
    vec2 mid[2] = {BuildingWallCrossing(w,v,nb[0]),BuildingWallCrossing(w,v,nb[1])};
    float top_m[2];
    bool f_open[2];
    for (int i = 0; i < 2; i++){
        //Where the walkers step across, the tops meet: a deck at its height, the ground - a terrace's top, the hill - at its.
        int across = WALK_GROUND;
        int meet = WalkJoinAny(w,z,v,layer,nb[i],&across);
        f_open[i] = meet == WALK_NO_JOIN;
        top_m[i] = f_open[i] ? top_v : BuildingMeetHeight(w,z,v,layer,nb[i],across,meet,mid[i]);
    }
    float top_c = (top_m[0] + top_m[1]) * 0.5f;
    //A stair's FLIGHT is its plane (CastleFlight), its tops there; a LANDING a deck (castle_plan.md "The stairs").
    CastleFlight fl;
    bool f_stair = shape == ZONE_CASTLE_STAIR && CastleFlightOf(w,z,v,fl);
    bool f_landing = shape == ZONE_CASTLE_LANDING;
    if (f_stair){
        top_v = fl.HeightAt(p[k]);
        top_m[0] = fl.HeightAt(mid[0]);
        top_m[1] = fl.HeightAt(mid[1]);
        top_c = fl.HeightAt(centre);
    }
    bool f_arch = shape == ZONE_CASTLE_ARCH;
    bool f_gate = shape == ZONE_CASTLE_GATE;
    //A GATE (castle_plan.md) opens toward the road - the sides no wall goes on through - and its passage is a two-storey
    //wall's whatever its own height.
    bool f_gate_open[2] = {f_gate && GateOpensToward(z,nb[0]),f_gate && GateOpensToward(z,nb[1])};

    //--- The top: the deck, or the flight's steps ------------------------------------------------------
    if (f_stair){
        const vec2 qs[4] = {p[k],mid[0],centre,mid[1]};
        FlightSteps(out,fl,qs);
    }else if (!f_covered){
        MeshQuad(out,At(p[k],top_v),At(mid[0],top_m[0]),At(centre,top_c),At(mid[1],top_m[1]),vec3(0,1,0),
                 PAL_STONE_LIGHT);
    }
    //An arch's vault and the pier at this corner, both by the quarter's own coordinates (ArchAcross).
    const vec2 q[4] = {p[k],mid[0],centre,mid[1]};
    float spring = f_gate ? ZoneCastleBase(z,v) * ZONE_STOREY_HEIGHT + CASTLE_GATE_SPRING
                          : top_v - CASTLE_ARCH_CROWN - CASTLE_ARCH_RISE;
    auto foot = [&](float qu, float qv){
        return under ? QuarterTopAt(*under,qu,qv) : t.GroundHeight(MeshBilinear(q,qu,qv),level) - CASTLE_SINK;
    };
    if (f_gate && f_gate_open[0] != f_gate_open[1]){
        //A gate's quarter open on one side: a barrel from its arch in to the plot's point, and the passage's wall.
        GateBarrel(out,q,f_gate_open[0] ? 0 : 1,spring,foot);
    }
    if (f_arch || (f_gate && f_gate_open[0] && f_gate_open[1])){
        ArchVault(out,q,spring);
        //The pier's two faces into the passage, its foot to the springing: one along each barrel.
        const float o = CASTLE_ARCH_OPEN;
        vec2 corner = MeshBilinear(q,o,o);
        vec2 ends[2] = {MeshBilinear(q,o,1.0f),MeshBilinear(q,1.0f,o)};
        vec2 toward[2] = {MeshBilinear(q,0.0f,(o + 1.0f) * 0.5f),MeshBilinear(q,(o + 1.0f) * 0.5f,0.0f)};
        float foot_end[2] = {foot(o,1.0f),foot(1.0f,o)};
        for (int i = 0; i < 2; i++){
            vec2 face = (corner + ends[i]) * 0.5f;
            MeshQuad(out,At(corner,foot(o,o)),At(ends[i],foot_end[i]),At(ends[i],spring),At(corner,spring),
                     At(toward[i] - face,0.0f),PAL_STONE);
        }
    }

    //--- Its sides: the two half-edges of its plot's outline in this cell, each facing a neighbour --------
    //A stair's open sides take a BALUSTRADE rather than battlements (castle_plan.md "The stairs") - on a flight's string,
    //half a step over its plane, or a landing's deck - but not against a wall standing a sill's height over it.
    auto rises_to = [&](const vec2& at, float deck){
        return f_stair ? fl.HeightAt(at) + 0.5f * CASTLE_STEP : deck;
    };
    bool f_balustrade[2];
    for (int i = 0; i < 2; i++){
        f_balustrade[i] = f_open[i] && (f_stair || f_landing) && StandingTop(w,z,nb[i]) < rises_to(mid[i],top_m[i]) + CASTLE_BATTLE_SILL;
    }
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
        //A wall-walk's open edge takes battlements (Outlines) - but not against a wall standing a sill's height over it,
        //a taller block's, a tower's, a terrace's or a house's, which guards it already - and a wall's foot on open
        //ground a plinth: an arch's openings and a piece standing on another neither. A stair's take its balustrade.
        QuarterSide& sd = sides.side[i];
        sd.f_battlements = f_open[i] && !ZoneCastleStairForm(shape) && !f_covered
                           && StandingTop(w,z,nb[i]) < ta + CASTLE_BATTLE_SILL;
        sd.f_plinth = !under && !f_arch && !f_gate && ZoneCastleShape(z,nb[i]) == ZONE_CASTLE_NONE && !ZoneTowerStone(z,nb[i])
                      && z.KindOf(nb[i]) == ZONE_KIND_NONE;
        sd.a = a;
        sd.b = b;
        sd.n = n;
        sd.ta = ta;
        sd.tb = tb;
        sd.ga = t.GroundHeight(a,level);
        sd.gb = t.GroundHeight(b,level);
        sd.f_slit = sd.f_plinth && shape == ZONE_CASTLE_BLOCK && ta - sd.ga >= CASTLE_SLIT_FROM * ZONE_STOREY_HEIGHT;
        if (f_stair){
            //Stepped as its treads, and on a flank its string wall over them.
            float ba = under ? under->m[i] : t.GroundHeight(a,level) - CASTLE_SINK;
            float bb = under ? under->c : t.GroundHeight(b,level) - CASTLE_SINK;
            FlightSide(out,fl,a,b,n,ba,bb,f_open[i]);
        }else if (f_arch || f_gate_open[i]){
            //Every side opens - a gate's toward its road; a neighbour's own wall closes it where one stands. The ring of
            //stones only where the opening faces out of the castle, not into a block or the next arch.
            bool f_ring = ZoneCastleShape(z,nb[i]) == ZONE_CASTLE_NONE && !ZoneTowerStone(z,nb[i]);
            uint32_t seed = MeshHash((uint32_t)std::min(v,nb[i]),(uint32_t)std::max(v,nb[i]),(uint32_t)layer);
            ArchFace(out,a,b,n,spring,ta,tb,
                     (i == 0) ? foot(1.0f,CASTLE_ARCH_OPEN) : foot(CASTLE_ARCH_OPEN,1.0f),foot(1.0f,1.0f),f_ring,seed);
        }else{
            //Down to the piece it stands on, or the ground.
            float ba = under ? under->m[i] : t.GroundHeight(a,level) - CASTLE_SINK;
            float bb = under ? under->c : t.GroundHeight(b,level) - CASTLE_SINK;
            MeshQuad(out,At(a,ba),At(b,bb),At(b,tb),At(a,ta),want,PAL_STONE);
        }
        //A gate's portcullis in each of its arches, down or up (castle_plan.md).
        if (f_gate_open[i]){
            Portcullis(out,w,q,i,spring,level,ZonePortcullisDown(z,v));
        }
        //Under a flight or a landing standing a storey or more over the ground, a dark arched doorway in its open side -
        //as under the stairs in the user's photo - so its side is not one blank wall.
        if (f_open[i] && (f_stair || f_landing) && !under){
            float g = t.GroundHeight((a + b) * 0.5f,level);
            float low = f_stair ? std::min(fl.HeightAt(a),fl.HeightAt(b)) : std::min(ta,tb);
            float len = (b - a).length();
            if (low - g >= ZONE_STOREY_HEIGHT * 0.95f && len > 0.5f){
                float sill = g + (sd.f_plinth ? CASTLE_PLINTH : 0.0f);     //on the plinth, where there is one
                float crown = std::min(low - CASTLE_STAIR_OVER_DOOR,sill + CASTLE_STAIR_DOOR);
                BuildingArchedOpening(out,a,b,want,0.5f,std::min(CASTLE_STAIR_DOOR_W,len * 0.3f),sill,crown,PAL_VOID,PAL_EFFECTS,
                                      PAL_STONE_LIGHT,false);
            }
        }
        //The balustrade, a newel where it turns at the cell's middle or ends there - one, where both sides have it.
        if (f_balustrade[i]){
            vec2 oa = BuildingWallOut(w,v,nb[i]);
            float in = f_stair ? CASTLE_STRING * 0.5f : CASTLE_BAL_IN;
            vec2 pa = a - oa * in;
            vec2 pb = b - n * in;
            auto line = [&](float s){
                return f_stair ? fl.HeightAt(pa + (pb - pa) * s) + 0.5f * CASTLE_STEP : ta + (tb - ta) * s;
            };
            Balustrade(out,a,b,oa,n,in,line,i == 1 || !f_balustrade[1]);
        }
    }
    //...and its doors across the passage at the plot's point: each leaf drawn by the quarter beside it toward the gate's
    //first opening, so it is drawn once and both swing back the same way.
    if (f_gate && f_gate_open[0] != f_gate_open[1]){
        int o = f_gate_open[0] ? 0 : 1;
        if (nb[o] == GateFirstOpening(w,z,v)){
            GateDoors(out,q,o,spring,foot,ZoneGateDoorShut(z,v));
        }
    }
    QuarterTop top;
    top.v = top_v;
    top.m[0] = top_m[0];
    top.m[1] = top_m[1];
    top.c = top_c;
    return top;
}

}

bool CastleFlightOf(const ChasmWorld& w, const ZoneState& z, int plot, CastleFlight& out){
    if (!ZoneCastleOverSits(z,plot) && ZoneCastleShape(z,plot) != ZONE_CASTLE_STAIR){
        return false;
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    out.at = pos[plot];
    out.up = ZoneStairUpDir(w,z,plot);
    if (out.up.dot(out.up) < 1e-6f){
        out.up = vec2(1.0f,0.0f);       //a stair alone in the open: any way, the same every time
    }
    int top = ZoneStairTop(z,plot);
    out.top_y = top * ZONE_STOREY_HEIGHT;
    out.foot_y = (top - 1) * ZONE_STOREY_HEIGHT;
    //Up at the nearest crossing on its way up, down at the nearest on its way down.
    float s_top = 1.0e9f;
    float s_foot = -1.0e9f;
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    for (int i = 0; i < n; i++){
        vec2 d = pos[nb[i]] - out.at;
        float l = d.length();
        if (l < 1e-5f){
            continue;
        }
        float along = d.dot(out.up) / l;
        float s = out.Along(BuildingWallCrossing(w,plot,nb[i]));
        if (along > ZONE_STAIR_SIDE){
            s_top = std::min(s_top,s);
        }else if (along < -ZONE_STAIR_SIDE){
            s_foot = std::max(s_foot,s);
        }
    }
    out.s_top = (s_top < 1.0e8f) ? s_top : 0.9f;
    out.s_foot = (s_foot > -1.0e8f) ? s_foot : -0.9f;
    //Never steeper than a short plot makes it.
    const float SHORTEST = 0.8f;
    if (out.s_top - out.s_foot < SHORTEST){
        float m = (out.s_top + out.s_foot) * 0.5f;
        out.s_top = m + SHORTEST * 0.5f;
        out.s_foot = m - SHORTEST * 0.5f;
    }
    return true;
}

bool CastleTurretOf(const ChasmWorld& w, const ZoneState& z, int plot, CastleTurret& out){
    if (ZoneCastleShape(z,plot) != ZONE_CASTLE_TURRET){
        return false;
    }
    const std::vector<vec2>& pos = w.grid->fine.pos;
    out.at = pos[plot];
    vec2 up = ZoneStairUpDir(w,z,plot);
    if (up.dot(up) < 1e-6f){
        up = vec2(1.0f,0.0f);
    }
    //Its exit the crossing to the stair it climbs to, or the one most up its way; its reach the nearest crossing's.
    int to = ZoneStairUp(z,plot);
    float nearest = 1.0e9f;
    float most = -2.0f;
    out.exit = out.at + up * 0.9f;
    int nb[8];
    int n = ZonePlotNeighbours(w,plot,nb,8);
    for (int i = 0; i < n; i++){
        vec2 c = BuildingWallCrossing(w,plot,nb[i]);
        nearest = std::min(nearest,(c - out.at).length());
        vec2 d = pos[nb[i]] - out.at;
        float along = d.dot(up) / std::max(1e-5f,d.length());
        if ((to >= 0) ? nb[i] == to : along > most){
            most = along;
            out.exit = c;
        }
    }
    out.outer = std::max(0.5f,std::min(1.1f,nearest * CASTLE_TURRET_REACH));
    out.core = out.outer * CASTLE_TURRET_CORE;
    out.base_y = ZoneCastleBase(z,plot) * ZONE_STOREY_HEIGHT;
    out.top_y = ZoneStairTop(z,plot) * ZONE_STOREY_HEIGHT;
    //Half a turn a storey, the odd number of half turns nearest, so the foot faces away from the exit.
    int storeys = std::max(1,ZoneCastleStoreys(z,plot));
    out.steps = CASTLE_STEPS * storeys;
    out.sweep = 3.14159265f * (float)((storeys % 2) ? storeys : storeys + 1);
    vec2 e = out.exit - out.at;
    out.start = std::atan2(e.y,e.x) - out.sweep;
    return true;
}

void BuildCastleCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
    }
    //What each quarter's sides leave for the outlines, the pieces' own and the ones over them apart.
    QuarterSides own[4];
    QuarterSides over[4];
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        //A stone tower, the castle's own tower (castle_plan.md): its quarter here, so it and the walls share their edges.
        if (ZoneTowerStone(z,v)){
            BuildStoneTowerQuarter(w,z,fine_quad,k,out);
            continue;
        }
        int shape = ZoneCastleShape(z,v);
        if (shape == ZONE_CASTLE_NONE){
            continue;
        }
        //An open turret is round, not its tile: drawn whole, by the first of its plot's cells (castle_plan.md "The stairs").
        if (shape == ZONE_CASTLE_TURRET){
            int first = fine_quad;
            for (int j = 0; j < w.picker->PlotQuadCount(v); j++){
                first = std::min(first,w.picker->PlotQuadCorner(v,j) / 4);
            }
            CastleTurret tu;
            if (first == fine_quad && CastleTurretOf(w,z,v,tu)){
                Turret(out,w,tu,v);
            }
            continue;
        }
        QuarterTop top = PieceQuarter(out,w,z,quad,p,k,shape,ZoneCastleLayer(z,v),NULL,ZoneCastleOverSits(z,v),own[k]);
        //The piece over it, standing on its top: an arch on its pillars, a stair on its sides.
        if (ZoneCastleOverShape(z,v) != ZONE_CASTLE_NONE){
            PieceQuarter(out,w,z,quad,p,k,ZoneCastleOverShape(z,v),ZoneCastleOverLayer(z,v),&top,false,over[k]);
        }
    }
    Outlines(out,w,quad,fine_quad,own);
    Outlines(out,w,quad,fine_quad,over);
}
