#include "BoundaryMesh.h"
#include "MeshBuild.h"

#include <cmath>

//Garden wall: stone, a hedge riding on it, stone posts at its ends.
#define WALL_HEIGHT         0.36f
#define WALL_THICK          0.16f
#define HEDGE_HEIGHT        0.22f
#define HEDGE_THICK         0.26f
#define WALL_POST_W         0.24f
#define WALL_POST_HEIGHT    0.66f
//Palisade: pointed stakes, close together.
#define STAKE_SPACING       0.20f
#define STAKE_W             0.12f
#define STAKE_HEIGHT        0.95f
#define STAKE_TIP           0.18f
//Field fence: posts and two rails.
#define FENCE_POST_SPACING  0.95f
#define FENCE_POST_W        0.08f
#define FENCE_HEIGHT        0.46f
#define FENCE_RAIL_W        0.05f
#define FOOTING             0.12f   //how far everything starts below the ground's relief
//Gates (step 10): where a road runs into a wall (Zones.h, ZoneGateBetween).
#define GATE_HALF           0.62f   //the opening either side of the edge's midpoint: a road and a shoulder
#define GATE_MAX_SHARE      0.7f    //of the wall segment the opening may take, so a stub of wall remains
#define GATE_PILLAR_W       0.30f
#define GATE_PILLAR_HEIGHT  0.88f
#define GATE_POST_W         0.18f
#define GATE_POST_HEIGHT    1.35f
#define GATE_LINTEL         0.16f   //the palisade gate's crossbeam, its depth

namespace {

float GroundAt(const vec2& p, float level){
    return TerrainGroundHeight(p,level);
}

/*
    A beam from a to b, `thick` across, from heights (ya0, yb0) up by `height`, coloured `column`:
    its two long sides, its top, and its two ends.
*/
void Beam(std::vector<vertex>& out, const vec2& a, const vec2& b, float ya0, float yb0, float height,
          float thick, int column){
    vec2 dir = b - a;
    float len = dir.length();
    if (len < 1e-5f){
        return;
    }
    dir = dir / len;
    vec2 side(-dir.y * thick * 0.5f,dir.x * thick * 0.5f);
    vec3 al(a.x + side.x,ya0,a.y + side.y), ar(a.x - side.x,ya0,a.y - side.y);
    vec3 bl(b.x + side.x,yb0,b.y + side.y), br(b.x - side.x,yb0,b.y - side.y);
    vec3 alt = al + vec3(0,height,0), art = ar + vec3(0,height,0);
    vec3 blt = bl + vec3(0,height,0), brt = br + vec3(0,height,0);
    vec3 n_side(side.x,0.0f,side.y);
    MeshQuad(out,al,bl,blt,alt,n_side,column);
    MeshQuad(out,ar,br,brt,art,n_side * -1.0f,column);
    MeshQuad(out,alt,blt,brt,art,vec3(0,1,0),column);
    MeshQuad(out,al,ar,art,alt,vec3(-dir.x,0.0f,-dir.y),column);
    MeshQuad(out,bl,br,brt,blt,vec3(dir.x,0.0f,dir.y),column);
}

//A square post standing at p from y0 up by height.
void Post(std::vector<vertex>& out, const vec2& p, float y0, float height, float w, int column){
    vec2 a = p - vec2(w * 0.5f,0.0f);
    vec2 b = p + vec2(w * 0.5f,0.0f);
    Beam(out,a,b,y0,y0,height,w,column);
}

//A pointed stake: a post with a four-sided tip.
void Stake(std::vector<vertex>& out, const vec2& p, float y0, float height, int column){
    float body = height - STAKE_TIP;
    Post(out,p,y0,body,STAKE_W,column);
    float h = STAKE_W * 0.5f;
    vec3 apex(p.x,y0 + height,p.y);
    vec3 c[4] = {vec3(p.x - h,y0 + body,p.y - h),vec3(p.x + h,y0 + body,p.y - h),
                 vec3(p.x + h,y0 + body,p.y + h),vec3(p.x - h,y0 + body,p.y + h)};
    for (int i = 0; i < 4; i++){
        vec3 mid = (c[i] + c[(i + 1) % 4]) * 0.5f;
        vec3 want = vec3(mid.x - p.x,0.3f,mid.z - p.y);
        MeshTri(out,c[i],c[(i + 1) % 4],apex,want,column);
    }
}

//The fine quad across edge k (v_k -> v_k+1) of quad q, or -1 at the map's edge.
int QuadAcross(const ChasmWorld& w, int q, int k){
    const Grid& g = *w.grid;
    const GridPicker& picker = *w.picker;
    int va = g.fine.quads[q].v[k];
    int vb = g.fine.quads[q].v[(k + 1) % 4];
    for (int i = 0; i < picker.PlotQuadCount(va); i++){
        int other = picker.PlotQuadCorner(va,i) / 4;
        if (other == q){
            continue;
        }
        const GridQuad& oq = g.fine.quads[other];
        for (int j = 0; j < 4; j++){
            if (oq.v[j] == vb){
                return other;
            }
        }
    }
    return -1;
}

}

void BuildBoundaryCell(const ChasmWorld& w, const ZoneState& z, int fine_quad, std::vector<vertex>& out){
    const Grid& g = *w.grid;
    const GridQuad& quad = g.fine.quads[fine_quad];
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    //Walls only stand on flat ground (ground zones need it), so any corner gives the level.
    float level = terrain_levels[w.terrain->level[quad.v[0]]].height;

    //--- Plot boundaries: the segment m_k -> centre, between corner k's plot and corner k+1's -----
    bool f_post_at_centre = false;
    int centre_kind = ZONE_BOUNDARY_NONE;
    for (int k = 0; k < 4; k++){
        int kind = ZoneBoundaryBetween(z,quad.v[k],quad.v[(k + 1) % 4]);
        if (kind == ZONE_BOUNDARY_NONE){
            continue;
        }
        vec2 mid = (p[k] + p[(k + 1) % 4]) * 0.5f;
        float yc = GroundAt(centre,level) - FOOTING;
        //The midpoint is shared with the cell across edge k: of the two, the lower index builds the post.
        int across = QuadAcross(w,fine_quad,k);
        bool f_post_at_mid = (across < 0 || fine_quad < across);
        /*
            A gate: the wall stops short of the midpoint, where the road comes through, and each of the
            two cells on the edge builds its own side of the opening - a pillar, or for a palisade a
            tall post and its half of the crossbeam - so the gate is whole without either knowing of
            the other. The midpoint's post goes: the opening is where it stood.
        */
        if (ZoneGateBetween(w,z,quad.v[k],quad.v[(k + 1) % 4])){
            vec2 d = centre - mid;
            float len = d.length();
            vec2 dir = d / std::max(1e-6f,len);
            vec2 jamb = mid + dir * std::min(GATE_HALF,len * GATE_MAX_SHARE);
            float yj = GroundAt(jamb,level) - FOOTING;
            if (kind == ZONE_BOUNDARY_GARDEN_WALL){
                Post(out,jamb,yj,GATE_PILLAR_HEIGHT + FOOTING,GATE_PILLAR_W,PAL_STONE);
            }else{
                Post(out,jamb,yj,GATE_POST_HEIGHT + FOOTING,GATE_POST_W,PAL_BARK_DARK);
                float top = yj + GATE_POST_HEIGHT + FOOTING - GATE_LINTEL;
                Beam(out,jamb + dir * (GATE_POST_W * 0.5f),mid,top,top,GATE_LINTEL,GATE_LINTEL,PAL_BARK_DARK);
            }
            mid = jamb;             //the rest of the segment is wall as usual, from the jamb in
            f_post_at_mid = false;  //the jamb is the post
        }
        float ym = GroundAt(mid,level) - FOOTING;
        if (kind == ZONE_BOUNDARY_GARDEN_WALL){
            Beam(out,mid,centre,ym,yc,WALL_HEIGHT + FOOTING,WALL_THICK,PAL_STONE_LIGHT);
            Beam(out,mid,centre,ym + WALL_HEIGHT + FOOTING,yc + WALL_HEIGHT + FOOTING,HEDGE_HEIGHT,HEDGE_THICK,PAL_BUSH);
            if (f_post_at_mid){
                Post(out,mid,ym,WALL_POST_HEIGHT + FOOTING,WALL_POST_W,PAL_STONE);
            }
        }else{
            vec2 d = centre - mid;
            int stakes = std::max(1,(int)std::floor(d.length() / STAKE_SPACING));
            //Stakes at both ends belong to the posts' owners; these are the ones between.
            for (int i = 1; i < stakes; i++){
                float t = (float)i / stakes;
                vec2 at = mid + d * t;
                float jitter = (float)(MeshHash((uint32_t)fine_quad,(uint32_t)k,(uint32_t)i) % 100) / 100.0f;
                Stake(out,at,GroundAt(at,level) - FOOTING,STAKE_HEIGHT + FOOTING + jitter * 0.12f,PAL_BARK);
            }
            if (f_post_at_mid){
                Stake(out,mid,ym,STAKE_HEIGHT + FOOTING + 0.08f,PAL_BARK);
            }
        }
        f_post_at_centre = true;
        centre_kind = std::max(centre_kind,kind);
    }
    if (f_post_at_centre){
        float yc = GroundAt(centre,level) - FOOTING;
        if (centre_kind == ZONE_BOUNDARY_GARDEN_WALL){
            Post(out,centre,yc,WALL_POST_HEIGHT + FOOTING,WALL_POST_W,PAL_STONE);
        }else{
            Stake(out,centre,yc,STAKE_HEIGHT + FOOTING + 0.08f,PAL_BARK);
        }
    }

    //--- Field outline: this child's edges 0 and 3 lie on its coarse cell's outline -----------------
    int cell = quad.parent;
    if (z.field.empty() || !z.field[cell]){
        return;
    }
    const int outline_edges[2] = {0,3};
    for (int e : outline_edges){
        int across = QuadAcross(w,fine_quad,e);
        if (across >= 0 && z.field[g.fine.quads[across].parent]){
            continue;   //field against field: no fence
        }
        vec2 a = p[e];
        vec2 b = p[(e + 1) % 4];
        //Edge 0 runs from the coarse corner out to the midpoint, edge 3 from the midpoint back to it;
        //posts go at even spacing from the coarse corner, so the two halves of a coarse edge line up.
        float len = (b - a).length();
        int posts = std::max(1,(int)std::ceil(len / FENCE_POST_SPACING));
        float ya = GroundAt(a,level), yb = GroundAt(b,level);
        for (int i = 0; i <= posts; i++){
            float t = (float)i / posts;
            //The coarse corner's post is built by the child whose edge 0 starts there; the midpoint's
            //by the child whose edge 0 ends there - so each post is built once.
            if ((e == 3 && i == posts) || (e == 3 && i == 0)){
                continue;
            }
            vec2 at = a + (b - a) * t;
            Post(out,at,GroundAt(at,level) - FOOTING,FENCE_HEIGHT + FOOTING,FENCE_POST_W,PAL_TIMBER);
        }
        for (int r = 0; r < 2; r++){
            float rail_y = FENCE_HEIGHT * (r == 0 ? 0.45f : 0.85f);
            Beam(out,a,b,ya + rail_y,yb + rail_y,FENCE_RAIL_W,FENCE_RAIL_W,PAL_TIMBER);
        }
    }
}
