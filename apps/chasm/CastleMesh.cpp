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
    if (shape == ZONE_CASTLE_BLOCK || shape == ZONE_CASTLE_STAIR){
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
    int me = WalkNodeHalves(w,z,v,layer);
    //Toward this cell's two other plots beside it: where the walls cross the edge - a building's and a terrace's, so a
    //piece meets them wall on wall, and round a point of five or six cells is the n-gon theirs is - and the top's
    //height there.
    int nb[2] = {quad.v[(k + 1) % 4],quad.v[(k + 3) % 4]};
    vec2 mid[2] = {BuildingWallCrossing(w,v,nb[0]),BuildingWallCrossing(w,v,nb[1])};
    float top_m[2];
    bool f_open[2];
    bool f_climbs[2];       //a stair's top: the side it climbs to
    for (int i = 0; i < 2; i++){
        //Where the walkers step across, the tops meet: a deck at its height, the ground - a terrace's top, the hill - at its.
        int across = WALK_GROUND;
        int meet = WalkJoinAny(w,z,v,layer,nb[i],&across);
        f_open[i] = meet == WALK_NO_JOIN;
        f_climbs[i] = meet == me + 1;
        top_m[i] = f_open[i] ? top_v : BuildingMeetHeight(w,z,v,layer,nb[i],across,meet,mid[i]);
    }
    float top_c = (top_m[0] + top_m[1]) * 0.5f;
    bool f_stair = shape == ZONE_CASTLE_STAIR;
    bool f_arch = shape == ZONE_CASTLE_ARCH;

    //--- The top: the deck, or the stair's ramp in steps ------------------------------------------------
    if (f_stair){
        RampPoint q[4] = {{p[k],top_v},{mid[0],top_m[0]},{centre,top_c},{mid[1],top_m[1]}};
        SteppedTri(out,q[0],q[1],q[2],PAL_STONE_LIGHT);
        SteppedTri(out,q[0],q[2],q[3],PAL_STONE_LIGHT);
    }else if (!f_covered){
        MeshQuad(out,At(p[k],top_v),At(mid[0],top_m[0]),At(centre,top_c),At(mid[1],top_m[1]),vec3(0,1,0),
                 PAL_STONE_LIGHT);
    }
    //An arch's vault and the pier at this corner, both by the quarter's own coordinates (ArchAcross).
    const vec2 q[4] = {p[k],mid[0],centre,mid[1]};
    float spring = top_v - CASTLE_ARCH_CROWN - CASTLE_ARCH_RISE;
    auto foot = [&](float qu, float qv){
        return under ? QuarterTopAt(*under,qu,qv) : t.GroundHeight(MeshBilinear(q,qu,qv),level) - CASTLE_SINK;
    };
    if (f_arch){
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
        //ground a plinth: an arch's openings and a piece standing on another neither. A stair's sides stand half a step
        //over its ramp, as high as its treads go, but where it reaches the top it climbs to.
        QuarterSide& sd = sides.side[i];
        sd.f_battlements = f_open[i] && !f_stair && !f_covered && StandingTop(w,z,nb[i]) < ta + CASTLE_BATTLE_SILL;
        sd.f_plinth = !under && !f_arch && ZoneCastleShape(z,nb[i]) == ZONE_CASTLE_NONE && !ZoneTowerStone(z,nb[i])
                      && z.KindOf(nb[i]) == ZONE_KIND_NONE;
        sd.a = a;
        sd.b = b;
        sd.n = n;
        sd.ta = ta;
        sd.tb = tb;
        sd.ga = t.GroundHeight(a,level);
        sd.gb = t.GroundHeight(b,level);
        sd.f_slit = sd.f_plinth && shape == ZONE_CASTLE_BLOCK && ta - sd.ga >= CASTLE_SLIT_FROM * ZONE_STOREY_HEIGHT;
        if (f_stair && !f_climbs[i]){
            ta += 0.5f * CASTLE_STEP;
            tb += 0.5f * CASTLE_STEP;
        }
        if (f_arch){
            //Every side opens; a neighbour's own wall closes it where one stands. The ring of stones only where the
            //opening faces out of the castle, not into a block or the next arch.
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
    }
    QuarterTop top;
    top.v = top_v;
    top.m[0] = top_m[0];
    top.m[1] = top_m[1];
    top.c = top_c;
    return top;
}

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
        QuarterTop top = PieceQuarter(out,w,z,quad,p,k,shape,ZoneCastleLayer(z,v),NULL,ZoneCastleOverSits(z,v),own[k]);
        //The piece over it, standing on its top: an arch on its pillars, a stair on its sides.
        if (ZoneCastleOverShape(z,v) != ZONE_CASTLE_NONE){
            PieceQuarter(out,w,z,quad,p,k,ZoneCastleOverShape(z,v),ZoneCastleOverLayer(z,v),&top,false,over[k]);
        }
    }
    Outlines(out,w,quad,fine_quad,own);
    Outlines(out,w,quad,fine_quad,over);
}
