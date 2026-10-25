#include "TerraceMesh.h"
#include "MeshBuild.h"
#include "Palette.h"
#include "RoadMesh.h"
#include "Walkers.h"

#include <algorithm>
#include <cmath>

#define TERRACE_LIFT        0.02f   //its top over the terrain, where the two all but meet
#define TERRACE_SINK        0.15f   //a wall's foot into the ground under it
#define TERRACE_BATTER      0.16f   //how far the face stands out for each unit down from its head: 1 in 6
#define TERRACE_FOOTING_H   0.22f   //the footing course: this high over the ground...
#define TERRACE_FOOTING_OUT 0.05f   //...and this proud of the face
#define TERRACE_CAP_H       0.08f   //a capstone: this over the top...
#define TERRACE_CAP_DROP    0.05f   //...this under it...
#define TERRACE_CAP_IN      0.16f   //...this far in over the top from the wall's line...
#define TERRACE_CAP_OUT     0.06f   //...this far out past the face...
#define TERRACE_CAP_LEN     0.45f   //...about this long...
#define TERRACE_CAP_JOINT   0.03f   //...with this joint to the next
#define TERRACE_QUOIN_FROM  0.6f    //quoins up an outer corner at least this high: blocks about...
#define TERRACE_QUOIN_H     0.2f    //...this high...
#define TERRACE_QUOIN_LONG  0.34f   //...and by turns this long along a face...
#define TERRACE_QUOIN_SHORT 0.18f   //...and this...
#define TERRACE_QUOIN_OUT   0.04f   //...standing this proud of the face - under the capstones, in front of the creepers...
#define TERRACE_QUOIN_JOINT 0.025f  //...with this joint between two
#define TERRACE_PIER_FROM   1.6f    //a buttress where a wall at least this high crosses a fine edge - one a cell along it
#define TERRACE_PIER_HALF   0.17f   //half its width
#define TERRACE_PIER_FOOT   0.34f   //how far it stands out from the face at its foot...
#define TERRACE_PIER_HEAD   0.22f   //...and at its head, from where a cap slopes back to the face...
#define TERRACE_PIER_UNDER  0.4f    //...this far under the top
#define TERRACE_CREEPER_SHARE 30u   //percent of the walls between two plots that creepers hang down...
#define TERRACE_CREEPER_FROM 0.6f   //...if at least this high...
#define TERRACE_CREEPER_MOST 1.0f   //...at most this far
#define TERRACE_CREEPER_LEAF 0.22f  //about this wide a leaf's facet along the drape
#define TERRACE_TUFT_FROM   0.4f    //tufts at the foot of a wall at least this high
#define TERRACE_DECAL       0.012f  //creepers stand this far proud of the face, as a house's windows do

#define TERRACE_JOIN_ON     0       //at the cell's centre the wall goes straight on, or ends
#define TERRACE_JOIN_CORNER 1       //...turns round an outer corner, mitred
#define TERRACE_JOIN_INSIDE 2       //...or into an inner one, mitred

/*
    ONE FACE of a retaining wall: along a quarter's outer edge, from at[0] - where the wall crosses the fine edge, the
    next cell's face going on from there - to at[1], the cell's centre. Its head is the top's edge; from there down it
    leans out by TERRACE_BATTER, each end along out[i], so the faces that meet at an end share the edge between them:
    at the crossing the way BuildingWallOut gives, which both cells work out alike; at the centre a mitre with the face it
    turns into, or straight on.
*/
struct TerraceFace{
    int high;                   //the terrace's plot
    int low;                    //the plot across, lower
    vec2 at[2];
    vec2 out[2];
    vec2 n;                     //its outward normal
    int join;                   //TERRACE_JOIN_*, at at[1]
    bool f_straight;            //the wall runs on straight across the crossing (BuildingWallOut)
    bool f_first;               //along the high plot's first edge in the cell: which way round its quoins alternate
    uint32_t seed;              //its own choices
    float top;                  //its head
    int row;                    //its biome's palette row, for the creepers
};

//A tuft of three blades at the foot of a wall, faced both ways.
static void Tuft(std::vector<vertex>& out, const vec2& at, float y, uint32_t h, int column, int row){
    float tall = 0.16f + 0.12f * (float)(h % 100u) / 100.0f;
    for (int i = 0; i < 3; i++){
        float a = (float)(h % 628u) * 0.01f + (float)i * 2.094f;
        vec2 d(std::cos(a),std::sin(a));
        vec2 s(-d.y * 0.035f,d.x * 0.035f);
        vec3 b0(at.x - s.x,y,at.y - s.y);
        vec3 b1(at.x + s.x,y,at.y + s.y);
        vec3 tip(at.x + d.x * 0.09f,y + tall,at.y + d.y * 0.09f);
        vec3 face = (b1 - b0).cross(tip - b0);
        MeshTri(out,b0,b1,tip,face,column,row);
        MeshTri(out,b0,b1,tip,face * -1.0f,column,row);
    }
}

static void BuildTerraceFace(const ChasmWorld& w, const ZoneState& z, const TerraceFace& f, std::vector<vertex>& out){
    const Terrain& t = *w.terrain;
    const vec3 up(0.0f,1.0f,0.0f);
    const vec3 n3(f.n.x,0.0f,f.n.y);
    float len = (f.at[1] - f.at[0]).length();
    if (len < 1e-4f){
        return;
    }
    vec2 dir = (f.at[1] - f.at[0]) * (1.0f / len);
    const vec3 dir3(dir.x,0.0f,dir.y);
    float level = t.Height(f.high);
    bool f_onto = ZoneTerraceStands(z,f.low);      //what is below is a lower terrace's top, not the ground
    auto foot = [&](const vec2& x){
        return f_onto ? ZoneTerraceTop(z,f.low) : t.GroundHeight(x,level) - TERRACE_SINK;
    };
    //The face at end i, height y; and s of the way along it from at[0].
    auto at = [&](int i, float y){
        return f.at[i] + f.out[i] * (TERRACE_BATTER * std::max(0.0f,f.top - y));
    };
    auto along = [&](float s, float y){
        vec2 a = at(0,y);
        return a + (at(1,y) - a) * s;
    };
    auto V = [](const vec2& q, float y){
        return vec3(q.x,y,q.y);
    };
    //Each end's foot, where it meets what is below as far out as it leans, and the ground's own height there.
    float yb[2];
    float ground[2];
    for (int i = 0; i < 2; i++){
        yb[i] = foot(f.at[i] + f.out[i] * (TERRACE_BATTER * std::max(0.0f,f.top - foot(f.at[i]))));
        ground[i] = f_onto ? ZoneTerraceTop(z,f.low) + TERRACE_LIFT : yb[i] + TERRACE_SINK;
    }
    float least = std::min(f.top - ground[0],f.top - ground[1]);
    float lo = f.top - TERRACE_CAP_DROP;

    int kind = z.KindOf(f.low);
    bool f_body = z.standing[f.low] > 0 && ZoneKindHasBody(kind);
    bool f_castle = ZoneCastleShape(z,f.low) != ZONE_CASTLE_NONE;
    //Against a building standing over the top the wall is the building's from there up: no capstones along its foot,
    //where its upper door may open onto the terrace (terraces_plan.md stage 3)...
    bool f_cap = !(f_body && !f_onto
                   && (BuildingFootHalves(w,z,kind,f.low) + 2 * z.standing[f.low]) * ZONE_STOREY_HEIGHT * 0.5f > f.top);
    //...nor against a castle piece standing to the top: its deck goes on onto the terrace (ramparts_plan.md).
    if (f_castle && WalkNodeHalves(w,z,f.low,ZoneCastleLayer(z,f.low)) * ZONE_STOREY_HEIGHT * 0.5f
                    >= f.top - TERRACE_LIFT - 1e-3f){
        f_cap = false;
    }
    //A road's stair or a building's upper door against it: nothing hung on the face there.
    int step = ZoneTerraceStep(w,z,f.high,f.low);
    bool f_way = (step == ZONE_STEP_STAIR || step == ZONE_STEP_DOOR);

    //The face, battered.
    MeshQuad(out,V(at(0,yb[0]),yb[0]),V(at(1,yb[1]),yb[1]),V(f.at[1],f.top),V(f.at[0],f.top),n3,PAL_STONE);

    //The footing course, proud of the face, tapering out where the wall comes down to nothing.
    float yf[2];
    vec2 fo[2][2];      //fo[i][0] the face at the footing's head, fo[i][1] its own face there
    for (int i = 0; i < 2; i++){
        yf[i] = std::max(yb[i],std::min(ground[i] + TERRACE_FOOTING_H,lo));
        fo[i][0] = at(i,yf[i]);
        fo[i][1] = fo[i][0] + f.out[i] * TERRACE_FOOTING_OUT;
    }
    vec2 fb0 = at(0,yb[0]) + f.out[0] * TERRACE_FOOTING_OUT;
    vec2 fb1 = at(1,yb[1]) + f.out[1] * TERRACE_FOOTING_OUT;
    MeshQuad(out,V(fb0,yb[0]),V(fb1,yb[1]),V(fo[1][1],yf[1]),V(fo[0][1],yf[0]),n3,PAL_STONE_DARK);
    MeshQuad(out,V(fo[0][0],yf[0]),V(fo[1][0],yf[1]),V(fo[1][1],yf[1]),V(fo[0][1],yf[0]),up,PAL_STONE_DARK);

    //Capstones along the head, overhanging the face. A joint at the crossing, where the next cell's stones go on, and at
    //the centre going straight on; round a corner the two end stones meet on the mitre.
    if (f_cap){
        float hi = f.top + TERRACE_CAP_H;
        vec2 outer[2];
        vec2 inner[2];
        vec2 under[2];
        for (int i = 0; i < 2; i++){
            outer[i] = f.at[i] + f.out[i] * TERRACE_CAP_OUT;
            inner[i] = f.at[i] - f.out[i] * TERRACE_CAP_IN;
            under[i] = at(i,lo);
        }
        auto lerp = [](const vec2* e, float s){
            return e[0] + (e[1] - e[0]) * s;
        };
        //Over a road's stair, which climbs the wall where the wall crosses the edge, a gap as wide as the road.
        float begin = (step == ZONE_STEP_STAIR) ? std::min(1.0f,ROAD_HALF_WIDTH / len) : 0.0f;
        int count = std::max(1,(int)std::lround(len * (1.0f - begin) / TERRACE_CAP_LEN));
        float half = TERRACE_CAP_JOINT * 0.5f / len;
        for (int k = 0; k < count; k++){
            float s0 = begin + (1.0f - begin) * k / count + half;
            float s1 = begin + (1.0f - begin) * (k + 1) / count
                     - ((k + 1 < count || f.join == TERRACE_JOIN_ON) ? half : 0.0f);
            if (s1 <= s0){
                continue;
            }
            vec2 o0 = lerp(outer,s0);
            vec2 o1 = lerp(outer,s1);
            vec2 i0 = lerp(inner,s0);
            vec2 i1 = lerp(inner,s1);
            MeshQuad(out,V(o0,hi),V(o1,hi),V(i1,hi),V(i0,hi),up,PAL_STONE_LIGHT);
            MeshQuad(out,V(o0,lo),V(o1,lo),V(o1,hi),V(o0,hi),n3,PAL_STONE_LIGHT);
            MeshQuad(out,V(i0,f.top),V(i1,f.top),V(i1,hi),V(i0,hi),n3 * -1.0f,PAL_STONE_LIGHT);
            MeshQuad(out,V(lerp(under,s0),lo),V(lerp(under,s1),lo),V(o1,lo),V(o0,lo),up * -1.0f,PAL_STONE_LIGHT);
            MeshQuad(out,V(o0,lo),V(i0,lo),V(i0,hi),V(o0,hi),dir3 * -1.0f,PAL_STONE_LIGHT);
            if (s1 < 1.0f){
                MeshQuad(out,V(o1,lo),V(i1,lo),V(i1,hi),V(o1,hi),dir3,PAL_STONE_LIGHT);
            }
        }
    }

    //Quoins up an outer corner: lighter blocks standing proud of the face - in front of any creeper hung there, which
    //they would otherwise fight for the same depth - long and short by turns, the corner's other face the other way
    //round, with a joint between each and the next. Out along the mitre at the corner, so the two faces' blocks share
    //the arris. No undersides: the camera is always above them.
    if (f.join == TERRACE_JOIN_CORNER && f.top - ground[1] >= TERRACE_QUOIN_FROM){
        float y0 = yf[1];
        float y1 = f_cap ? lo : f.top;
        int count = (int)((y1 - y0) / TERRACE_QUOIN_H);
        float block = (count > 0) ? (y1 - y0) / count : 0.0f;
        float joint = TERRACE_QUOIN_JOINT * 0.5f;
        vec2 out_corner = f.out[1] * TERRACE_QUOIN_OUT;
        vec2 out_face = f.n * TERRACE_QUOIN_OUT;
        for (int b = 0; b < count; b++){
            float ya = y0 + block * b + ((b > 0) ? joint : 0.0f);
            float yc = y0 + block * (b + 1) - ((b + 1 < count) ? joint : 0.0f);
            float l = ((b + (f.f_first ? 1 : 0)) % 2) ? TERRACE_QUOIN_LONG : TERRACE_QUOIN_SHORT;
            float s = std::max(0.0f,1.0f - l / len);
            vec2 ia = along(s,ya);      //its inner end, on the face...
            vec2 ic = along(s,yc);
            vec2 ca = at(1,ya);         //...and its corner, on the arris
            vec2 cc = at(1,yc);
            MeshQuad(out,V(ia + out_face,ya),V(ca + out_corner,ya),V(cc + out_corner,yc),V(ic + out_face,yc),n3,PAL_STONE_LIGHT);
            MeshQuad(out,V(ic,yc),V(cc,yc),V(cc + out_corner,yc),V(ic + out_face,yc),up,PAL_STONE_LIGHT);
            MeshQuad(out,V(ia,ya),V(ia + out_face,ya),V(ic + out_face,yc),V(ic,yc),dir3 * -1.0f,PAL_STONE_LIGHT);
        }
    }

    //A buttress where a tall wall crosses the fine edge: this cell's half of it, the next cell's the other. Not where
    //anything stands against the wall, which it would go through, nor where the wall bends there - round a point of
    //three or four cells it crosses at the midpoint - which would twist its two halves apart.
    if (!f_way && !f_body && !f_castle && f.f_straight && f.top - ground[0] >= TERRACE_PIER_FROM){
        float sw = std::min(0.5f,TERRACE_PIER_HALF / len);
        float y_cap = f.top - TERRACE_PIER_UNDER;
        float y_head = y_cap - TERRACE_PIER_HEAD;
        float yb_side = yb[0] + (yb[1] - yb[0]) * sw;
        vec2 base0 = at(0,yb[0]) + f.out[0] * TERRACE_PIER_FOOT;
        vec2 base1 = along(sw,yb_side) + f.n * TERRACE_PIER_FOOT;
        float f0 = foot(base0);
        float f1 = foot(base1);
        vec2 head0 = at(0,y_head) + f.out[0] * TERRACE_PIER_HEAD;
        vec2 head1 = along(sw,y_head) + f.n * TERRACE_PIER_HEAD;
        MeshQuad(out,V(base0,f0),V(base1,f1),V(head1,y_head),V(head0,y_head),n3,PAL_STONE);
        MeshQuad(out,V(head0,y_head),V(head1,y_head),V(along(sw,y_cap),y_cap),V(at(0,y_cap),y_cap),n3 + up,PAL_STONE_LIGHT);
        MeshQuad(out,V(along(sw,f1),f1),V(base1,f1),V(head1,y_head),V(along(sw,y_cap),y_cap),dir3,PAL_STONE);
    }

    //Creepers hanging from under the capstones down some walls - chosen by the two plots, so a wall is hung along its
    //whole length - with a ragged hem. The leaf columns keep summer's all year, so they read as ivy.
    uint32_t pair = MeshHash((uint32_t)std::min(f.high,f.low),(uint32_t)std::max(f.high,f.low),41u);
    if (f_cap && !f_way && pair % 100u < TERRACE_CREEPER_SHARE && least >= TERRACE_CREEPER_FROM){
        float most = std::min(TERRACE_CREEPER_MOST,least - 0.25f);
        int count = std::max(2,(int)std::lround(len / TERRACE_CREEPER_LEAF));
        //How far it hangs at each step along: at the crossing the edge's own, so the drapes either side of it meet.
        auto drop = [&](int i){
            uint32_t h = (i == 0) ? MeshHash(pair,7u) : MeshHash(f.seed,(uint32_t)i);
            return most * (0.3f + 0.7f * (float)(h % 1000u) / 1000.0f);
        };
        auto lift = [&](float s){
            return (f.out[0] + (f.out[1] - f.out[0]) * s) * TERRACE_DECAL;
        };
        float d0 = drop(0);
        for (int i = 0; i < count; i++){
            float s0 = (float)i / count;
            float s1 = (float)(i + 1) / count;
            float sm = (s0 + s1) * 0.5f;
            float d1 = drop(i + 1);
            float dm = std::max(d0,d1) + TERRACE_CREEPER_LEAF * 0.6f;
            int leaf = (MeshHash(f.seed,(uint32_t)i,3u) % 3u) ? PAL_LEAF_DARK : PAL_LEAF;
            vec3 a = V(along(s0,lo - d0) + lift(s0),lo - d0);
            vec3 b = V(along(s1,lo - d1) + lift(s1),lo - d1);
            MeshQuad(out,a,b,V(along(s1,lo) + lift(s1),lo),V(along(s0,lo) + lift(s0),lo),n3,leaf,f.row);
            MeshTri(out,a,b,V(along(sm,lo - dm) + lift(sm),lo - dm),n3,leaf,f.row);
            d0 = d1;
        }
    }

    //Tufts along the foot, where it stands on open ground.
    if (kind == ZONE_KIND_NONE && z.ground[f.low] == ZONE_GROUND_NONE && !f_castle && least >= TERRACE_TUFT_FROM){
        int tufts = (int)(MeshHash(f.seed,99u) % 3u);
        for (int k = 0; k < tufts; k++){
            uint32_t h = MeshHash(f.seed,(uint32_t)k,17u);
            float s = 0.15f + 0.7f * (float)(h % 1000u) / 1000.0f;
            vec2 base = along(s,yb[0] + (yb[1] - yb[0]) * s) + f.n * (TERRACE_FOOTING_OUT + 0.06f);
            float y = f_onto ? ZoneTerraceTop(z,f.low) + TERRACE_LIFT : foot(base) + TERRACE_SINK;
            Tuft(out,base,y,h,PAL_BUSH,f.row);
        }
    }
}

void BuildTerraceCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const Terrain& t = *w.terrain;
    const GridQuad& quad = g.fine.quads[fine_quad];
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    //m[k] where the quarters meet on edge k -> k+1: where a building's wall crosses it, not the ground's midpoint
    //(TerrainMesh), so round a point of five or six cells a terrace has the building's n-gon and not a star.
    vec2 m[4];
    for (int k = 0; k < 4; k++){
        m[k] = BuildingWallCrossing(w,quad.v[k],quad.v[(k + 1) % 4]);
    }
    const vec3 up(0.0f,1.0f,0.0f);
    //One of the grass's shades for the cell, as the terrain picks one a coarse cell.
    int grass = PAL_GRASS_0 + (int)(MeshHash((uint32_t)quad.parent,77u) % PAL_GRASS_COUNT);
    auto site = [&](int v){
        return ZoneTerraced(z,v) && !ZoneTerraceStands(z,v);
    };
    auto biome_row = [&](int v){
        return (t.biome[v] == TERRAIN_BIOME_SWAMP) ? PAL_SWAMP : (t.biome[v] == TERRAIN_BIOME_DESERT) ? PAL_DESERT
             : PAL_TEMPERATE;
    };
    bool f_staked = false;
    for (int k = 0; k < 4; k++){
        int v = quad.v[k];
        if (!ZoneTerraced(z,v)){
            continue;
        }
        int prev = (k + 3) % 4;
        if (site(v)){
            //Staked out: a cord along each edge to anything but more of the same site.
            int across[2] = {quad.v[(k + 1) % 4],quad.v[prev]};
            vec2 edge_a[2] = {m[k],centre};
            vec2 edge_b[2] = {centre,m[prev]};
            float level = t.Height(v);
            for (int e = 0; e < 2; e++){
                int u = across[e];
                if (site(u) && z.terrace[u] == z.terrace[v]){
                    continue;
                }
                const vec2& a = edge_a[e];
                const vec2& b = edge_b[e];
                MeshCord(out,vec3(a.x,t.GroundHeight(a,level) + MESH_SITE_CORD,a.y),
                         vec3(b.x,t.GroundHeight(b,level) + MESH_SITE_CORD,b.y),PAL_WALL);
                f_staked = true;
            }
            continue;
        }
        float top = ZoneTerraceTop(z,v) + TERRACE_LIFT;
        MeshQuad(out,vec3(p[k].x,top,p[k].y),vec3(m[k].x,top,m[k].y),vec3(centre.x,top,centre.y),
                 vec3(m[prev].x,top,m[prev].y),up,grass,biome_row(v));
    }
    /*
        The walls, a spoke at a time: spoke j runs from the centre out to m[j], between corners j and j + 1. A standing
        terrace walls it off from the corner across unless that is a terrace as high or higher; of two terraces of
        different heights the higher draws it. high[j] is the corner whose wall it is, -1 none, and normal[j] faces the
        other.
    */
    auto walled = [&](int a, int b){
        int va = quad.v[a];
        int vb = quad.v[b];
        return ZoneTerraceStands(z,va) && !(ZoneTerraceStands(z,vb) && z.terrace[vb] >= z.terrace[va]);
    };
    int high[4];
    vec2 normal[4];
    for (int j = 0; j < 4; j++){
        int a = j;
        int b = (j + 1) % 4;
        high[j] = walled(a,b) ? a : walled(b,a) ? b : -1;
        if (high[j] < 0){
            continue;
        }
        int low = (high[j] == a) ? b : a;
        vec2 d = m[j] - centre;
        vec2 side(-d.y,d.x);
        if (side.dot(p[low] - (m[j] + centre) * 0.5f) < 0.0f){
            side = side * -1.0f;
        }
        normal[j] = side * (1.0f / std::max(1e-6f,side.length()));
    }
    for (int j = 0; j < 4; j++){
        if (high[j] < 0){
            continue;
        }
        int h = high[j];
        int l = (h == j) ? (j + 1) % 4 : j;
        //Each side's other spoke, and the corner across it.
        int h_other = (h == j) ? (j + 3) % 4 : (j + 1) % 4;
        int l_other = (l == j) ? (j + 3) % 4 : (j + 1) % 4;
        int across_h = (h == j) ? (j + 3) % 4 : (j + 2) % 4;
        int across_l = (l == j) ? (j + 3) % 4 : (j + 2) % 4;
        TerraceFace f;
        f.high = quad.v[h];
        f.low = quad.v[l];
        f.at[0] = m[j];
        f.at[1] = centre;
        f.n = normal[j];
        f.out[0] = BuildingWallOut(w,f.high,f.low,&f.f_straight);
        //At the centre: round the high corner's other side, into the low corner's other side, or straight on.
        if (high[h_other] == h){
            f.join = TERRACE_JOIN_CORNER;
            f.out[1] = MeshMitre(normal[j],normal[h_other]);
        }
        else if (high[l_other] == across_l){
            f.join = TERRACE_JOIN_INSIDE;
            f.out[1] = MeshMitre(normal[j],normal[l_other]);
        }
        else{
            f.join = TERRACE_JOIN_ON;
            int opposite = (j + 2) % 4;
            f.out[1] = (high[opposite] == across_h) ? MeshMitre(normal[j],normal[opposite]) : normal[j];
        }
        f.f_first = (h == j);
        f.seed = MeshHash((uint32_t)fine_quad,(uint32_t)j,13u);
        f.top = ZoneTerraceTop(z,f.high) + TERRACE_LIFT;
        f.row = biome_row(f.high);
        BuildTerraceFace(w,z,f,out);
    }
    //A stake at the cell's middle wherever an outline turns through it.
    if (f_staked){
        float level = t.Height(quad.v[0]);
        float ground = t.GroundHeight(centre,level);
        MeshPost(out,centre,0.04f,ground - 0.1f,ground + MESH_SITE_STAKE,PAL_TIMBER);
    }
}
