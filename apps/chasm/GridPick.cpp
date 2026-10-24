#include "GridPick.h"

#include <algorithm>
#include <cmath>

#define PICK_OUTLINE_SLACK  1e-3f   //how near the map's outline a point in no quad still picks the quad there

namespace {

float PolyArea(const vec2* p, int n){
    float a = 0.0f;
    for (int k = 0; k < n; k++){
        const vec2& u = p[k];
        const vec2& w = p[(k + 1) % n];
        a += u.x * w.y - w.x * u.y;
    }
    return a * 0.5f;
}

/*
    Crossing-number test rather than a convexity one: a quarter of a convex quad is convex in
    practice, but nothing guarantees it, and a pick that silently misses is worse than a slower test.

    WATERTIGHT: an edge's crossing is worked out from its LOWER end, whichever way round the polygon
    walks it. Two quads share an edge in opposite directions, and worked out from each one's own end
    the two crossings rounded apart - a point exactly on the edge was then outside BOTH, and the pick
    missed. A villager stopped on such a point had no plot under him, so a re-plan found him no way
    anywhere and he stood there until he died of thirst (the woutveen recording, 2026-10-08). From the
    same end, both quads get the same crossing to the bit, and every point is in exactly one of them.
*/
bool InPolygon(const vec2& pt, const vec2* p, int n){
    bool f_in = false;
    for (int i = 0, j = n - 1; i < n; j = i++){
        if ((p[i].y > pt.y) != (p[j].y > pt.y)){
            const vec2& lo = (p[i].y < p[j].y) ? p[i] : p[j];
            const vec2& hi = (p[i].y < p[j].y) ? p[j] : p[i];
            float x = lo.x + (pt.y - lo.y) * (hi.x - lo.x) / (hi.y - lo.y);
            if (pt.x < x){
                f_in = !f_in;
            }
        }
    }
    return f_in;
}

}

GridPicker::GridPicker(std::shared_ptr<const Grid> g) : grid(g){
    const GridLevel& fine = grid->fine;
    int nq = (int)fine.quads.size();

    //--- Buckets about one coarse cell across: a handful of quads each, so a pick tests few. ---
    bucket_size = std::max(0.5f,grid->settings.triangle_side * 0.5f);
    origin = grid->bounds_min;
    vec2 size = grid->bounds_max - grid->bounds_min;
    buckets_x = std::max(1,(int)std::ceil(size.x / bucket_size) + 1);
    buckets_z = std::max(1,(int)std::ceil(size.y / bucket_size) + 1);

    std::vector<int> count((size_t)buckets_x * buckets_z + 1,0);
    auto box = [&](int q, int& x0, int& z0, int& x1, int& z1){
        vec2 lo(1e30f,1e30f), hi(-1e30f,-1e30f);
        for (int k = 0; k < 4; k++){
            const vec2& p = fine.pos[fine.quads[q].v[k]];
            lo.x = std::min(lo.x,p.x); lo.y = std::min(lo.y,p.y);
            hi.x = std::max(hi.x,p.x); hi.y = std::max(hi.y,p.y);
        }
        x0 = std::max(0,(int)((lo.x - origin.x) / bucket_size));
        z0 = std::max(0,(int)((lo.y - origin.y) / bucket_size));
        x1 = std::min(buckets_x - 1,(int)((hi.x - origin.x) / bucket_size));
        z1 = std::min(buckets_z - 1,(int)((hi.y - origin.y) / bucket_size));
    };
    for (int q = 0; q < nq; q++){
        int x0, z0, x1, z1;
        box(q,x0,z0,x1,z1);
        for (int z = z0; z <= z1; z++){
            for (int x = x0; x <= x1; x++){
                count[z * buckets_x + x]++;
            }
        }
    }
    bucket_start.assign(count.size(),0);
    for (size_t i = 1; i < count.size(); i++){
        bucket_start[i] = bucket_start[i - 1] + count[i - 1];
    }
    bucket_quads.assign(bucket_start.back(),0);
    std::vector<int> fill(bucket_start.begin(),bucket_start.end() - 1);
    for (int q = 0; q < nq; q++){
        int x0, z0, x1, z1;
        box(q,x0,z0,x1,z1);
        for (int z = z0; z <= z1; z++){
            for (int x = x0; x <= x1; x++){
                bucket_quads[fill[z * buckets_x + x]++] = q;
            }
        }
    }

    //--- The quads around each vertex ---------------------------------------------------------------
    int nv = (int)fine.pos.size();
    vert_start.assign(nv + 1,0);
    for (const GridQuad& q : fine.quads){
        for (int k = 0; k < 4; k++){
            vert_start[q.v[k] + 1]++;
        }
    }
    for (int i = 1; i <= nv; i++){
        vert_start[i] += vert_start[i - 1];
    }
    vert_quads.assign(vert_start.back(),0);
    std::vector<int> vfill(vert_start.begin(),vert_start.end() - 1);
    for (int q = 0; q < nq; q++){
        for (int k = 0; k < 4; k++){
            vert_quads[vfill[fine.quads[q].v[k]]++] = q * 4 + k;
        }
    }
}

void GridPicker::Quarter(int q, int k, vec2 out[4]) const{
    const GridLevel& fine = grid->fine;
    const GridQuad& quad = fine.quads[q];
    vec2 p[4];
    for (int i = 0; i < 4; i++){
        p[i] = fine.pos[quad.v[i]];
    }
    vec2 c = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    out[0] = p[k];
    out[1] = (p[k] + p[(k + 1) % 4]) * 0.5f;
    out[2] = c;
    out[3] = (p[k] + p[(k + 3) % 4]) * 0.5f;
}

GridPick GridPicker::Pick(const vec2& p) const{
    GridPick pick;
    pick.at = p;
    int bx = (int)std::floor((p.x - origin.x) / bucket_size);
    int bz = (int)std::floor((p.y - origin.y) / bucket_size);
    if (bx < 0 || bz < 0 || bx >= buckets_x || bz >= buckets_z){
        return pick;
    }
    const GridLevel& fine = grid->fine;
    int b = bz * buckets_x + bx;
    auto take = [&](int q){
        pick.f_hit = true;
        pick.fine_quad = q;
        pick.coarse_quad = fine.quads[q].parent;
        //Which quarter. The nearest corner is the answer when a point lands exactly on a
        //quarter's edge and the crossing test gives it to neither side.
        int nearest = 0;
        float best = 1e30f;
        for (int k = 0; k < 4; k++){
            vec2 quarter[4];
            Quarter(q,k,quarter);
            if (InPolygon(p,quarter,4)){
                nearest = k;
                best = -1.0f;
                break;
            }
            vec2 d = fine.pos[fine.quads[q].v[k]] - p;
            if (d.dot(d) < best){
                best = d.dot(d);
                nearest = k;
            }
        }
        pick.corner = nearest;
        pick.plot = fine.quads[q].v[nearest];
    };
    for (int i = bucket_start[b]; i < bucket_start[b + 1]; i++){
        int q = bucket_quads[i];
        vec2 corners[4];
        for (int k = 0; k < 4; k++){
            corners[k] = fine.pos[fine.quads[q].v[k]];
        }
        if (InPolygon(p,corners,4)){
            take(q);
            return pick;
        }
    }
    /*
        ON THE OUTLINE: watertight between two quads, the crossing test gives a point on their shared edge to one of them
        - but on the map's outline there is no other, and on two of its sides a point exactly on it is in none. A plot on
        the outline has its point there: someone ordered onto one stood on it, picked no plot, and found no way anywhere,
        not even home (a bug report, 2026-10-24: Kaat De Wit, posted on the east edge). So a point in no quad, within
        PICK_OUTLINE_SLACK of one's edge, is that quad's - the nearest. Inside the map every point is in a quad already.
    */
    int best_q = -1;
    float best_d = PICK_OUTLINE_SLACK;
    for (int i = bucket_start[b]; i < bucket_start[b + 1]; i++){
        int q = bucket_quads[i];
        for (int k = 0; k < 4; k++){
            const vec2& a = fine.pos[fine.quads[q].v[k]];
            vec2 ab = fine.pos[fine.quads[q].v[(k + 1) % 4]] - a;
            float t = std::max(0.0f,std::min(1.0f,(p - a).dot(ab) / std::max(1e-12f,ab.dot(ab))));
            float d = (a + ab * t - p).length();
            if (d <= best_d && (best_q < 0 || d < best_d)){
                best_d = d;
                best_q = q;
            }
        }
    }
    if (best_q >= 0){
        take(best_q);
    }
    return pick;
}

void GridPicker::PlotOutline(int v, std::vector<vec2>& segments) const{
    for (int i = vert_start[v]; i < vert_start[v + 1]; i++){
        vec2 quarter[4];
        Quarter(vert_quads[i] / 4,vert_quads[i] % 4,quarter);
        //The two edges of the quarter that do not touch the vertex: midpoint-centre-midpoint.
        segments.push_back(quarter[1]);
        segments.push_back(quarter[2]);
        segments.push_back(quarter[2]);
        segments.push_back(quarter[3]);
    }
}

float GridPicker::PlotArea(int v) const{
    float a = 0.0f;
    for (int i = vert_start[v]; i < vert_start[v + 1]; i++){
        vec2 quarter[4];
        Quarter(vert_quads[i] / 4,vert_quads[i] % 4,quarter);
        a += PolyArea(quarter,4);
    }
    return a;
}

/*
    A coarse quad's children are fine quads 4c..4c+3 - Subdivide in Grid.cpp writes a polygon's
    children together, and every coarse polygon is a quad. Each child's edges 0 and 3 lie on the
    parent's outline.
*/
void GridPicker::CoarseOutline(int c, std::vector<vec2>& segments) const{
    const GridLevel& fine = grid->fine;
    for (int q = c * 4; q < c * 4 + 4; q++){
        const GridQuad& quad = fine.quads[q];
        segments.push_back(fine.pos[quad.v[0]]);
        segments.push_back(fine.pos[quad.v[1]]);
        segments.push_back(fine.pos[quad.v[3]]);
        segments.push_back(fine.pos[quad.v[0]]);
    }
}

float GridPicker::FineArea(int q) const{
    const GridLevel& fine = grid->fine;
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = fine.pos[fine.quads[q].v[k]];
    }
    return PolyArea(p,4);
}

float GridPicker::CoarseArea(int c) const{
    float a = 0.0f;
    for (int q = c * 4; q < c * 4 + 4; q++){
        a += FineArea(q);
    }
    return a;
}
