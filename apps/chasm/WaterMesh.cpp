#include "WaterMesh.h"
#include "Mist.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#define WATER_CELL_DIP      0.15f   //a cell a channel lowers this much at a corner gets water
#define WATER_REACH         8.0f    //how far past its edge a river still names a water vertex

/*
    A FALL'S BRINK (waterfall_plan.md): the straight line along the rim the river goes over, just past
    every rock under its water, so the water can run out to it and the sheet hang from it.
*/
#define BRINK_BACK          6.0f    //how far upstream of the rim line the river is laid out to its brink
#define BRINK_AHEAD         6.0f    //how far past the rim line a plateau plot can still be under the water
#define BRINK_ROCK_CLEAR    1.2f    //past a plot's outline: its turf and strata stand out up to 1.14
#define BRINK_DRY_EDGE      0.6f    //past the water's edge a bank's ground is above the surface, bump and all
#define BRINK_ON_LINE       1e-3f   //a point this near the brink's line is on it
#define BRINK_BESIDE        4.0f    //beside the brink's ends, the river's water near the lip is left out
//The rock looked at round a fall or spring: across, past its water's own width; out, past the rim line.
#define ROCK_SIDE           4.0f
#define ROCK_BACK           12.0f
#define ROCK_AHEAD          20.0f
/*
    THE FALL'S SHAPE. Near the brink a thrown arc - out^2 = FALL_THROW x drop - easing to FALL_CURL out,
    after which it drops straight; wider by FALL_SPREAD by its foot.
*/
#define FALL_THROW          3.0f
#define FALL_CURL           2.6f
#define FALL_SPREAD         0.30f
//The middle stands out by this share of the half-width more than the edges, once it has fallen a little.
#define FALL_BOW            0.18f
//The inner layer: from this drop down, this much nearer the wall, this share of the width, its streaks
//this far along out of step with the front's.
#define FALL_INNER_FROM     0.8f
#define FALL_INNER_BACK     0.7f
#define FALL_INNER_WIDTH    0.8f
#define FALL_INNER_PHASE    23.7f
#define FALL_BELOW_MIST     10.0f   //how far the sheet runs on down inside the mist
//World units a second squared: how fast the water speeds up as it falls, so how far its streaks stretch.
#define FALL_GRAVITY        6.0f
//|uv.x| at a sheet's edges below its top - chasm_water.glsl's SHEET_EDGE - eased to over this much drop.
#define SHEET_EDGE          1.15f
#define SHEET_EDGE_EASE     2.0f
/*
    A SPRING'S water starts at the back of its mouth (Terrain.h, TerrainSpring), on the floor the chamfer
    lifts a little there, and runs out over it to a brink of its own.
*/
#define SPRING_BACK         1.4f    //behind the rim line
#define SPRING_LIFT         0.4f    //over the mouth's floor, at the back
#define SPRING_DIP          0.1f    //under it, at the brink
#define SPRING_COLUMNS      5

namespace {

//One triangle facing `want`, with the shader's coordinates as UVs.
void WaterTri(std::vector<vertex>& out, vec3 a, vec2 ua, vec3 b, vec2 ub, vec3 c, vec2 uc, const vec3& want){
    vec3 n = (b - a).cross(c - a);
    if (n.dot(want) < 0.0f){
        std::swap(b,c);
        std::swap(ub,uc);
        n = n * -1.0f;
    }
    if (n.length() < 1e-9f){
        return;
    }
    n.normalize();
    vec3 tangent = b - a;
    tangent.normalize();
    vertex v = {};
    v.normal = n;
    v.tangent = tangent;
    v.matid = 0;
    v.pos = a; v.uv = ua; out.push_back(v);
    v.pos = b; v.uv = ub; out.push_back(v);
    v.pos = c; v.uv = uc; out.push_back(v);
}

float SmoothStep(float e0, float e1, float x){
    float t = std::max(0.0f,std::min(1.0f,(x - e0) / (e1 - e0)));
    return t * t * (3.0f - 2.0f * t);
}

//A fall's or spring's own coordinates: x along the rim (`across`), y out over the chasm (`out`).
struct Frame{
    vec2 origin;
    vec2 out;
    vec2 across;
    float X(const vec2& p) const { return (p - origin).dot(across); }
    float Y(const vec2& p) const { return (p - origin).dot(out); }
    vec2 At(float x, float y) const { return origin + across * x + out * y; }
};

/*
    Where p->q crosses the line n.p = c. Worked from the two ends in a fixed order, so two polygons that
    share the edge - in either direction - put the new point at exactly the same place: the river's
    pieces either side of a cut, and the river and its fall's sheet, meet without a crack.
*/
vec2 Crossing(vec2 a, vec2 b, const vec2& n, float c){
    if (b.x < a.x || (b.x == a.x && b.y < a.y)){
        std::swap(a,b);
    }
    float da = a.dot(n) - c;
    float db = b.dot(n) - c;
    return a + (b - a) * (da / (da - db));
}

//The part of a convex polygon where n.p <= c.
void Clip(std::vector<vec2>& poly, const vec2& n, float c){
    std::vector<vec2> kept;
    size_t m = poly.size();
    for (size_t i = 0; i < m; i++){
        const vec2& a = poly[i];
        const vec2& b = poly[(i + 1) % m];
        bool f_in_a = a.dot(n) - c <= 0.0f;
        bool f_in_b = b.dot(n) - c <= 0.0f;
        if (f_in_a){
            kept.push_back(a);
        }
        if (f_in_a != f_in_b){
            kept.push_back(Crossing(a,b,n,c));
        }
    }
    poly.swap(kept);
}

//The part between x0 and x1 (and y0 and y1) in the frame's coordinates. A side given as +-1e30 is open.
void ClipBox(std::vector<vec2>& poly, const Frame& fr, float x0, float x1, float y0, float y1){
    float ox = fr.origin.dot(fr.across);
    float oy = fr.origin.dot(fr.out);
    if (x0 > -1e29f && poly.size() >= 3) Clip(poly,-fr.across,-(x0 + ox));
    if (x1 < 1e29f && poly.size() >= 3) Clip(poly,fr.across,x1 + ox);
    if (y0 > -1e29f && poly.size() >= 3) Clip(poly,-fr.out,-(y0 + oy));
    if (y1 < 1e29f && poly.size() >= 3) Clip(poly,fr.out,y1 + oy);
}

/*
    Cell q in convex pieces, as the terrain stands it up (TerrainMesh.cpp): the whole quad if its corners
    are at one height, else each corner's quarter - the corner, its edges' midpoints, the cell's centre.
    `f_plateau` keeps only the plateau's. Both the river and a brink's apron cut the same pieces, so
    where one stops and the other starts they share every point.
*/
void CellPieces(const Grid& g, const Terrain& t, int q, bool f_plateau, std::vector<std::vector<vec2>>& pieces,
                std::vector<int>* owners = NULL){
    const GridQuad& quad = g.fine.quads[q];
    pieces.clear();
    if (owners){
        owners->clear();
    }
    bool f_level = true;
    for (int k = 1; k < 4; k++){
        f_level = f_level && t.steps[quad.v[k]] == t.steps[quad.v[0]];
    }
    vec2 p[4];
    for (int k = 0; k < 4; k++){
        p[k] = g.fine.pos[quad.v[k]];
    }
    if (f_level){
        if (!f_plateau || t.steps[quad.v[0]] == 0){
            pieces.push_back(std::vector<vec2>(p,p + 4));
            if (owners){
                owners->push_back(quad.v[0]);
            }
        }
        return;
    }
    vec2 centre = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    for (int k = 0; k < 4; k++){
        if (f_plateau && t.steps[quad.v[k]] != 0){
            continue;
        }
        vec2 next = (p[k] + p[(k + 1) % 4]) * 0.5f;
        vec2 prev = (p[k] + p[(k + 3) % 4]) * 0.5f;
        pieces.push_back({p[k],next,centre,prev});
        if (owners){
            owners->push_back(quad.v[k]);
        }
    }
}

/*
    ROCK a fall's water must clear: a plot's piece of a cell (CellPieces) as a column from its top down,
    spanning x0..x1 across and y0..y1 out - its outline plus BRINK_ROCK_CLEAR either way, for the wall's
    turf and strata standing out of it.
*/
struct Rock{
    float x0, x1;
    float y0, y1;
    float top;
    int steps;
};

void FindRocks(const Grid& g, const Terrain& t, const Frame& fr, float x0, float x1, std::vector<Rock>& rocks){
    rocks.clear();
    std::vector<std::vector<vec2>> pieces;
    std::vector<int> owners;
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        vec2 c = g.FineQuadCentre(q);
        float cx = fr.X(c);
        float cy = fr.Y(c);
        if (cx < x0 - ROCK_SIDE || cx > x1 + ROCK_SIDE || cy < -ROCK_BACK || cy > ROCK_AHEAD){
            continue;
        }
        CellPieces(g,t,q,false,pieces,&owners);
        for (size_t i = 0; i < pieces.size(); i++){
            int v = owners[i];
            if (t.steps[v] >= TERRAIN_FLOOR_STEPS){
                continue;       //the floor is under the mist, below every sheet's foot
            }
            Rock r;
            r.x0 = r.y0 = 1e30f;
            r.x1 = r.y1 = -1e30f;
            for (const vec2& p : pieces[i]){
                r.x0 = std::min(r.x0,fr.X(p));
                r.x1 = std::max(r.x1,fr.X(p));
                r.y0 = std::min(r.y0,fr.Y(p));
                r.y1 = std::max(r.y1,fr.Y(p));
            }
            if (r.x1 < x0 || r.x0 > x1){
                continue;
            }
            r.y0 -= BRINK_ROCK_CLEAR;
            r.y1 += BRINK_ROCK_CLEAR;
            r.top = t.Height(v);
            r.steps = t.steps[v];
            rocks.push_back(r);
        }
    }
}

//How far out the brink must be: past every plateau rock between x0 and x1 near the rim line.
float BrinkReach(const std::vector<Rock>& rocks, float x0, float x1, float least){
    float d = least;
    for (const Rock& r : rocks){
        if (r.steps == 0 && r.x1 >= x0 && r.x0 <= x1 && r.y1 - BRINK_ROCK_CLEAR < BRINK_AHEAD){
            d = std::max(d,r.y1);
        }
    }
    return d;
}

struct Brink{
    Frame frame;
    float lo = 0.0f, hi = 0.0f;     //the water's extent along the rim, from the lip
    float d = 0.0f;                 //the brink's line, out from the lip
    std::vector<Rock> rocks;
    std::vector<vec2> top;          //the river's points on the brink, as laid
    std::vector<vec2> top_uv;
};

/*
    A fall's brink. Its ends are where the bank's ground is surely above the water (BRINK_DRY_EDGE), from
    BRINK_BACK upstream out to the brink's own line - so beside it, there, the river is all under the
    bank, and is left out (BuildWaterMesh); its line is past every plateau rock between them.
*/
void MakeBrink(const Grid& g, const Terrain& t, const TerrainFall& f, Brink& b){
    b.frame.origin = f.lip;
    b.frame.out = f.out;
    b.frame.across = f.across;
    const float step = 0.05f;
    const float limit = f.width * 2.0f + 4.0f;
    b.lo = 0.0f;
    b.hi = 0.0f;
    //Widens lo..hi to the water on the line y out, marching both ways from its deepest point there.
    auto Extent = [&](float y){
        float x = 0.0f, deepest = 1e30f;
        for (float s = -limit; s <= limit; s += 0.5f){
            float e = t.WaterEdgeDistance(b.frame.At(s,y));
            if (e < deepest){
                deepest = e;
                x = s;
            }
        }
        if (deepest >= BRINK_DRY_EDGE){
            return;
        }
        float lo = x;
        while (lo > -limit && t.WaterEdgeDistance(b.frame.At(lo - step,y)) < BRINK_DRY_EDGE){
            lo -= step;
        }
        float hi = x;
        while (hi < limit && t.WaterEdgeDistance(b.frame.At(hi + step,y)) < BRINK_DRY_EDGE){
            hi += step;
        }
        b.lo = std::min(b.lo,lo);
        b.hi = std::max(b.hi,hi);
    };
    Extent(-BRINK_BACK);
    Extent(-0.5f * BRINK_BACK);
    Extent(0.0f);
    for (int pass = 0; pass < 2; pass++){
        FindRocks(g,t,b.frame,b.lo,b.hi,b.rocks);
        b.d = BrinkReach(b.rocks,b.lo,b.hi,BRINK_ROCK_CLEAR);
        Extent(b.d);
    }
    FindRocks(g,t,b.frame,b.lo,b.hi,b.rocks);
    b.d = BrinkReach(b.rocks,b.lo,b.hi,BRINK_ROCK_CLEAR);
}

/*
    A sheet's normals from its own grid of points, rows down and columns across, turned to face out of
    the wall - so they follow both the curl and the bow. Row `up_row`'s are straight up: the brink's,
    where the river's flat normal meets it.
*/
void GridNormals(std::vector<vertex>& grid, int rows, int cols, const Frame& fr, int up_row){
    const vec3 facing(fr.out.x,0.5f,fr.out.y);
    for (int r = 0; r < rows; r++){
        for (int c = 0; c < cols; c++){
            vertex& v = grid[(size_t)r * cols + c];
            if (r == up_row){
                v.normal = vec3(0.0f,1.0f,0.0f);
                continue;
            }
            vec3 down = grid[(size_t)std::min(rows - 1,r + 1) * cols + c].pos - grid[(size_t)std::max(0,r - 1) * cols + c].pos;
            vec3 side = grid[(size_t)r * cols + std::min(cols - 1,c + 1)].pos - grid[(size_t)r * cols + std::max(0,c - 1)].pos;
            vec3 n = side.cross(down);
            if (n.length() < 1e-9f){
                n = facing;
            }
            if (n.dot(facing) < 0.0f){
                n = n * -1.0f;
            }
            v.normal = n.normalize();
        }
    }
}

//A grid of a sheet's points as triangles, each wound to face the way its normals do, as WaterTri would.
void EmitGrid(std::vector<vertex>& out, const std::vector<vertex>& grid, int rows, int cols){
    for (int r = 0; r + 1 < rows; r++){
        for (int c = 0; c + 1 < cols; c++){
            const vertex& a = grid[(size_t)r * cols + c];
            const vertex& b = grid[(size_t)r * cols + c + 1];
            const vertex& d = grid[(size_t)(r + 1) * cols + c];
            const vertex& e = grid[(size_t)(r + 1) * cols + c + 1];
            bool f_flip = (b.pos - a.pos).cross(e.pos - a.pos).dot(a.normal + e.normal) < 0.0f;
            const vertex* tri[6] = {&a,&b,&e,&a,&e,&d};
            if (f_flip){
                std::swap(tri[1],tri[2]);
                std::swap(tri[4],tri[5]);
            }
            for (int k = 0; k < 6; k++){
                out.push_back(*tri[k]);
            }
        }
    }
}

/*
    One sheet of falling water, hanging from its TOP row - points on the brink's line, by x, with their
    UVs - and running down into the mist. A spring's has a LEAD row too, `lead` behind the brink and
    `lead_lift` above it, the water running out of its mouth.

    Its shape is the curl (FALL_THROW, FALL_CURL), pushed further out wherever a column would stand in
    its way: a row whose distance out falls inside a rock's span goes past it. Normals follow the curl
    down - flat across - so it rolls rather than bands, and the top row's is straight up, the river's.

    uv.x is the top's across, eased to -SHEET_EDGE..SHEET_EDGE over the first SHEET_EDGE_EASE of drop so
    the shader knows where the edges are; uv.y carries on the top's along in FLOW TIME, the river's flow
    times the seconds the water takes to get there, so its streaks pour on over the edge and stretch as
    it speeds up. Fills in `shape` but for its foot's fall, and says whether rock pushed it out.
*/
bool Sheet(std::vector<vertex>& out, const Frame& fr, float lo, float hi, float brink, float top_y,
           const std::vector<vec2>& top, const std::vector<vec2>& top_uv, const std::vector<Rock>& rocks,
           float lead, float lead_lift, WaterSheetShape& shape){
    const float bottom = CHASM_MIST_TOP - FALL_BELOW_MIST;
    const float total = top_y - bottom;
    static const float drops[] = {0.0f,0.1f,0.35f,0.8f,1.5f,2.5f,4.0f,6.0f,9.0f,13.0f,18.0f,25.0f,33.0f,42.0f};
    std::vector<float> drop;
    std::vector<float> reach;
    if (lead > 0.0f){
        drop.push_back(-lead_lift);
        reach.push_back(-lead);
    }
    int brink_row = (int)drop.size();
    for (float d : drops){
        if (d < total - 1.0f){
            drop.push_back(d);
            reach.push_back(FALL_CURL * std::tanh(std::sqrt(FALL_THROW * d) / FALL_CURL));
        }
    }
    drop.push_back(total);
    reach.push_back(FALL_CURL);
    int rows = (int)drop.size();
    bool f_pushed = false;
    std::vector<uint8_t> f_row_pushed(rows,0);
    for (int r = brink_row + 1; r < rows; r++){
        reach[r] = std::max(reach[r],reach[r - 1]);
        //The rock beside the segment down to the next row as well as at this one.
        float y = top_y - drop[std::min(rows - 1,r + 1)];
        for (int pass = 0; pass < 4; pass++){
            bool f_moved = false;
            for (const Rock& k : rocks){
                float at = brink + reach[r];
                if (k.top > y && k.x1 >= lo && k.x0 <= hi && at >= k.y0 && at < k.y1){
                    reach[r] = k.y1 - brink;
                    f_moved = true;
                    f_pushed = true;
                    f_row_pushed[r] = 1;
                }
            }
            if (!f_moved){
                break;
            }
        }
    }
    //Seconds from the brink, by the centre's path and the water's speed as it falls.
    std::vector<float> secs(rows,0.0f);
    for (int r = brink_row + 1; r < rows; r++){
        float dr = reach[r] - reach[r - 1];
        float dd = drop[r] - drop[r - 1];
        float v0 = std::sqrt(WATER_RIVER_FLOW * WATER_RIVER_FLOW + 2.0f * FALL_GRAVITY * std::max(0.0f,drop[r - 1]));
        float v1 = std::sqrt(WATER_RIVER_FLOW * WATER_RIVER_FLOW + 2.0f * FALL_GRAVITY * std::max(0.0f,drop[r]));
        secs[r] = secs[r - 1] + std::sqrt(dr * dr + dd * dd) / (0.5f * (v0 + v1));
    }
    for (int r = brink_row - 1; r >= 0; r--){
        float dr = reach[r + 1] - reach[r];
        float dd = drop[r + 1] - drop[r];
        secs[r] = secs[r + 1] - std::sqrt(dr * dr + dd * dd) / WATER_RIVER_FLOW;
    }
    const float mid = 0.5f * (lo + hi);
    const float half = std::max(1e-3f,0.5f * (hi - lo));
    const int cols = (int)top.size();
    //Where column c's water is at row r: spread from the middle, and bowed out in it (FALL_BOW).
    auto Place = [&](int r, float x, float scale, float out_by){
        float spread = (1.0f + FALL_SPREAD * std::max(0.0f,drop[r]) / total) * scale;
        float u = (x - mid) / half;
        float bow = FALL_BOW * half * std::max(0.0f,1.0f - u * u) * SmoothStep(0.3f,5.0f,drop[r]);
        vec2 p = fr.At(mid + (x - mid) * spread,brink + out_by + bow * scale);
        return vec3(p.x,top_y - drop[r],p.y);
    };
    std::vector<vertex> front((size_t)rows * cols);
    for (int r = 0; r < rows; r++){
        float ease = SmoothStep(0.0f,SHEET_EDGE_EASE,drop[r]);
        for (int c = 0; c < cols; c++){
            float x = fr.X(top[c]);
            vertex& v = front[(size_t)r * cols + c];
            v = vertex();
            //The top row is the river's own points, to the bit.
            v.pos = (r == brink_row) ? vec3(top[c].x,top_y,top[c].y) : Place(r,x,1.0f,reach[r]);
            float edge_x = SHEET_EDGE * (x - mid) / half;
            v.uv = vec2(top_uv[c].x + (edge_x - top_uv[c].x) * ease,top_uv[c].y + WATER_RIVER_FLOW * secs[r]);
            v.tangent = vec3(fr.across.x,0.0f,fr.across.y);
            v.matid = 0;
        }
    }
    GridNormals(front,rows,cols,fr,brink_row);
    EmitGrid(out,front,rows,cols);

    /*
        THE INNER LAYER: narrower and a little nearer the wall, from under the curl down, its streaks out
        of step with the front's - seen through the front's strands and past its ragged edges, it gives
        the fall a depth. Not pulled back where rock pushed the front out.
    */
    std::vector<int> inner_rows;
    for (int r = brink_row; r < rows; r++){
        if (drop[r] >= FALL_INNER_FROM){
            inner_rows.push_back(r);
        }
    }
    int irows = (int)inner_rows.size();
    if (irows >= 2){
        std::vector<vertex> inner((size_t)irows * cols);
        for (int i = 0; i < irows; i++){
            int r = inner_rows[i];
            float in_by = f_row_pushed[r] ? reach[r] : std::max(0.0f,reach[r] - FALL_INNER_BACK);
            for (int c = 0; c < cols; c++){
                float x = fr.X(top[c]);
                vertex& v = inner[(size_t)i * cols + c];
                v = vertex();
                v.pos = Place(r,x,FALL_INNER_WIDTH,in_by);
                v.uv = vec2(SHEET_EDGE * (x - mid) / half,top_uv[c].y + FALL_INNER_PHASE + WATER_RIVER_FLOW * secs[r]);
                v.tangent = vec3(fr.across.x,0.0f,fr.across.y);
                v.matid = 0;
            }
        }
        GridNormals(inner,irows,cols,fr,-1);
        EmitGrid(out,inner,irows,cols);
    }

    shape.origin = fr.origin;
    shape.out = fr.out;
    shape.mid = mid;
    shape.half = half;
    shape.top_y = top_y;
    shape.front = brink + reach[rows - 1] + FALL_BOW * half;
    vec2 foot = fr.At(mid,brink + reach[rows - 1]);
    shape.foot.lip = foot - fr.out * FOAM_SHEET_OUT;
    return f_pushed;
}

}

void BuildWaterMesh(const Grid& g, const Terrain& t, WaterMeshData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = WaterMeshData();
    const vec3 up(0.0f,1.0f,0.0f);

    std::vector<Brink> brinks(t.falls.size());
    for (size_t i = 0; i < t.falls.size(); i++){
        MakeBrink(g,t,t.falls[i],brinks[i]);
    }

    /*
        A piece of the rivers' surface: named by the river at every point (or left out), fanned, and its
        points on a brink's line kept for that fall's sheet.
    */
    auto Lay = [&](const std::vector<vec2>& poly, Brink* brink){
        if (poly.size() < 3){
            return;
        }
        std::vector<vec2> uv(poly.size());
        for (size_t i = 0; i < poly.size(); i++){
            int river;
            float along, across;
            if (!t.RiverCoords(poly[i],WATER_REACH,river,along,across)){
                return;
            }
            uv[i] = vec2(across,along);
        }
        for (size_t i = 1; i + 1 < poly.size(); i++){
            WaterTri(out.flat,
                     vec3(poly[0].x,TERRAIN_WATER_Y,poly[0].y),uv[0],
                     vec3(poly[i].x,TERRAIN_WATER_Y,poly[i].y),uv[i],
                     vec3(poly[i + 1].x,TERRAIN_WATER_Y,poly[i + 1].y),uv[i + 1],up);
        }
        if (brink){
            for (size_t i = 0; i < poly.size(); i++){
                if (std::fabs(brink->frame.Y(poly[i]) - brink->d) < BRINK_ON_LINE){
                    brink->top.push_back(poly[i]);
                    brink->top_uv.push_back(uv[i]);
                }
            }
        }
    };
    //Whether a piece reaches into a brink's strip - between its ends, give or take `side`, from BRINK_BACK
    //upstream out to y1.
    auto InStrip = [](const std::vector<vec2>& poly, const Brink& b, float y1, float side){
        float x0 = 1e30f, x1 = -1e30f, y_hi = -1e30f, y_lo = 1e30f;
        for (const vec2& p : poly){
            x0 = std::min(x0,b.frame.X(p));
            x1 = std::max(x1,b.frame.X(p));
            y_lo = std::min(y_lo,b.frame.Y(p));
            y_hi = std::max(y_hi,b.frame.Y(p));
        }
        return x1 >= b.lo - side && x0 <= b.hi + side && y_hi >= -BRINK_BACK && y_lo <= y1;
    };

    //--- The rivers' surface ------------------------------------------------------------------------
    std::vector<std::vector<vec2>> pieces;
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        const GridQuad& quad = g.fine.quads[q];
        int num_high = 0;
        float dip = 0.0f;
        for (int k = 0; k < 4; k++){
            num_high += (t.steps[quad.v[k]] == 0) ? 1 : 0;
            dip = std::max(dip,t.RiverDip(g.fine.pos[quad.v[k]]));
        }
        if (num_high == 0 || dip < WATER_CELL_DIP){
            continue;
        }
        /*
            The plateau part of the cell, as the terrain draws it, so the water stops at the lip - except
            near a fall, from BRINK_BACK upstream on: between the brink's ends its apron (below) takes
            over, and beside them the bank's ground is above the water, which is left out - laid to the
            plots' outline it stood out of the cliff face, wherever the wall under the turf sits back.
        */
        CellPieces(g,t,q,true,pieces);
        for (const std::vector<vec2>& piece : pieces){
            const Brink* near = NULL;
            for (const Brink& b : brinks){
                if (InStrip(piece,b,b.d + BRINK_BESIDE,BRINK_BESIDE)){
                    near = &b;
                }
            }
            if (!near){
                Lay(piece,NULL);
                continue;
            }
            std::vector<vec2> part = piece;
            ClipBox(part,near->frame,-1e30f,1e30f,-1e30f,-BRINK_BACK);
            Lay(part,NULL);
        }
    }
    /*
        A fall's APRON: between the brink's ends, from BRINK_BACK upstream out to its line, every cell's
        every piece - over the drop where the jagged rim falls short of the brink, as water overhangs a
        real lip. Its points on the line are the sheet's top row.
    */
    for (Brink& b : brinks){
        for (int q = 0; q < (int)g.fine.quads.size(); q++){
            vec2 c = g.FineQuadCentre(q);
            float cy = b.frame.Y(c);
            float cx = b.frame.X(c);
            if (cy < -BRINK_BACK - 4.0f || cy > b.d + 4.0f || cx < b.lo - 4.0f || cx > b.hi + 4.0f){
                continue;
            }
            CellPieces(g,t,q,false,pieces);
            for (std::vector<vec2>& piece : pieces){
                if (!InStrip(piece,b,b.d,0.0f)){
                    continue;
                }
                ClipBox(piece,b.frame,b.lo,b.hi,-BRINK_BACK,b.d);
                Lay(piece,&b);
            }
        }
    }

    //--- The swamp's pools ---------------------------------------------------------------------------
    for (int q = 0; q < (int)g.fine.quads.size(); q++){
        const GridQuad& quad = g.fine.quads[q];
        bool f_pool = false;
        bool f_plateau = true;
        for (int k = 0; k < 4; k++){
            int v = quad.v[k];
            f_plateau = f_plateau && t.level[v] == TERRAIN_PLATEAU;
            f_pool = f_pool || (t.biome[v] == TERRAIN_BIOME_SWAMP && t.ground[v] < TERRAIN_WATER_Y);
        }
        if (!f_plateau || !f_pool){
            continue;
        }
        vec3 c[4];
        vec2 uv[4];
        for (int k = 0; k < 4; k++){
            vec2 p = g.fine.pos[quad.v[k]];
            c[k] = vec3(p.x,TERRAIN_WATER_Y,p.y);
            //uv.x wanders gently inside -0.5..0.5: the streaks get a second direction to vary in (a
            //constant one ruled them into stripes), and stay clear of the river edges' darkening.
            uv[k] = vec2(0.5f * std::sin(p.x * 0.13f + p.y * 0.05f),p.y + p.x * 0.3f);
        }
        WaterTri(out.pools,c[0],uv[0],c[1],uv[1],c[2],uv[2],up);
        WaterTri(out.pools,c[0],uv[0],c[2],uv[2],c[3],uv[3],up);
    }

    //--- The falls: hung from their brinks ----------------------------------------------------------
    for (size_t i = 0; i < brinks.size(); i++){
        Brink& b = brinks[i];
        //The top row by x, each point once - the pieces either side of a cut both have it.
        std::vector<size_t> order(b.top.size());
        for (size_t k = 0; k < order.size(); k++){
            order[k] = k;
        }
        std::sort(order.begin(),order.end(),[&](size_t l, size_t r){ return b.frame.X(b.top[l]) < b.frame.X(b.top[r]); });
        std::vector<vec2> top, top_uv;
        for (size_t k : order){
            if (!top.empty() && std::fabs(b.frame.X(b.top[k]) - b.frame.X(top.back())) < BRINK_ON_LINE){
                continue;
            }
            top.push_back(b.top[k]);
            top_uv.push_back(b.top_uv[k]);
        }
        if (top.size() < 2){
            continue;
        }
        WaterSheetShape shape;
        shape.foot = t.falls[i];
        if (Sheet(out.sheets,b.frame,b.lo,b.hi,b.d,TERRAIN_WATER_Y,top,top_uv,b.rocks,0.0f,0.0f,shape)){
            out.sheets_pushed++;
        }
        out.shapes.push_back(shape);
        out.brinks++;
    }
    //--- The springs: the same sheet, out of a mouth in the wall -----------------------------------
    for (const TerrainSpring& s : t.springs){
        if (s.plots <= 0){
            continue;
        }
        Frame fr;
        fr.origin = s.lip;
        fr.out = s.out;
        fr.across = s.across;
        float half = s.width * 0.46f;
        std::vector<Rock> rocks;
        FindRocks(g,t,fr,-half,half,rocks);
        float brink = BrinkReach(rocks,-half,half,SPRING_BACK);
        std::vector<vec2> top, top_uv;
        for (int c = 0; c < SPRING_COLUMNS; c++){
            float k = (float)c / (float)(SPRING_COLUMNS - 1);
            top.push_back(fr.At(-half + 2.0f * half * k,brink));
            top_uv.push_back(vec2(SHEET_EDGE * (2.0f * k - 1.0f),brink + SPRING_BACK));
        }
        WaterSheetShape shape;
        shape.foot.out = s.out;
        shape.foot.across = s.across;
        shape.foot.width = s.width;
        if (Sheet(out.sheets,fr,-half,half,brink,s.floor_y - SPRING_DIP,top,top_uv,rocks,brink + SPRING_BACK,
                  SPRING_LIFT + SPRING_DIP,shape)){
            out.sheets_pushed++;
        }
        out.shapes.push_back(shape);
    }
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}
