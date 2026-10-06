#include "Grid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <queue>
#include <unordered_map>
#include "RRandom.h"

/*
    See Grid.h for what the grid is and docs/grid_plan.md section 1 for the method. The notes here
    are about the parts that are easy to get subtly wrong: winding, which order things are created
    in, and why the random stream is drawn the way it is.
*/

namespace {

//A lattice triangle or a merged quad, before subdivision.
struct Poly{
    int n = 0;
    int v[4] = {-1,-1,-1,-1};
};

uint64_t EdgeKey(int a, int b){
    if (a > b){
        std::swap(a,b);
    }
    return ((uint64_t)(uint32_t)a << 32) | (uint64_t)(uint32_t)b;
}

float SignedArea(const std::vector<vec2>& pos, const int* v, int n){
    float a = 0.0f;
    for (int k = 0; k < n; k++){
        const vec2& p = pos[v[k]];
        const vec2& q = pos[v[(k + 1) % n]];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5f;
}

//+90 degrees in the winding's sense: with positive signed area in (x, z), corner k+1 of a square
//is corner k turned by this about the centre.
vec2 Rot90(const vec2& d){
    return vec2(-d.y,d.x);
}

vec2 RotM90(const vec2& d){
    return vec2(d.y,-d.x);
}

bool SamePoint(const vec2& a, const vec2& b){
    return a.x == b.x && a.y == b.y;
}

bool LineClosed(const GridLine& line){
    return line.points.size() > 2 && SamePoint(line.points.front(),line.points.back());
}

#define LINE_SEARCH_WINDOW  6   //segments either side of a vertex's last one, when it has one

/*
    The nearest point on `line`. With `segment` holding the segment the point was last nearest to,
    only the segments around it are searched, and it is updated: a vertex moves a fraction of a cell
    per relaxation pass, and a rim is hundreds of segments. Searching near its own segment also keeps
    a vertex on its own stretch of the line where the line comes back close to itself.
*/
vec2 NearestOnLine(const GridLine& line, const vec2& p, int* segment = NULL){
    int num_segments = (int)line.points.size() - 1;
    if (num_segments < 1){
        return line.points.empty() ? p : line.points[0];
    }
    bool f_closed = LineClosed(line);
    int first = 0;
    int count = num_segments;
    if (segment && *segment >= 0 && num_segments > 2 * LINE_SEARCH_WINDOW + 1){
        count = 2 * LINE_SEARCH_WINDOW + 1;
        first = *segment - LINE_SEARCH_WINDOW;
        if (!f_closed){
            first = std::max(0,std::min(num_segments - count,first));
        }
    }
    vec2 best = line.points[0];
    float best_d2 = 1e30f;
    int best_k = 0;
    for (int n = 0; n < count; n++){
        int k = (first + n + num_segments) % num_segments;
        vec2 a = line.points[k];
        vec2 d = line.points[k + 1] - a;
        float len2 = d.dot(d);
        float t = len2 > 0.0f ? std::max(0.0f,std::min(1.0f,(p - a).dot(d) / len2)) : 0.0f;
        vec2 q = a + d * t;
        float d2 = (p - q).dot(p - q);
        if (d2 < best_d2){
            best_d2 = d2;
            best = q;
            best_k = k;
        }
    }
    if (segment){
        *segment = best_k;
    }
    return best;
}

//Distance along the line at every point, from its first.
std::vector<float> LineArcLengths(const GridLine& line){
    std::vector<float> s(line.points.size(),0.0f);
    for (size_t k = 1; k < line.points.size(); k++){
        vec2 d = line.points[k] - line.points[k - 1];
        s[k] = s[k - 1] + std::sqrt(d.dot(d));
    }
    return s;
}

vec2 LinePointAt(const GridLine& line, const std::vector<float>& s, float at){
    size_t k = 1;
    while (k + 1 < s.size() && s[k] < at){
        k++;
    }
    float span = s[k] - s[k - 1];
    float t = span > 0.0f ? std::max(0.0f,std::min(1.0f,(at - s[k - 1]) / span)) : 0.0f;
    return line.points[k - 1] + (line.points[k] - line.points[k - 1]) * t;
}

/*
    A hand-drawn feature is corners, and the grid pins to whatever line it is given, so the corners
    would show as kinks in the rim. Smoothed with a centripetal Catmull-Rom spline - the centripetal
    kind does not loop or overshoot where corners are unevenly spaced - and resampled every
    `spacing`, so the line's segments are about as long as a fine cell. An open line keeps its end
    points exactly; they may be fixed on the map's edge.
*/
GridLine SmoothLine(const std::vector<vec2>& corners, bool f_closed, float spacing){
    std::vector<vec2> c = corners;
    if (f_closed){
        c.pop_back();
    }
    int n = (int)c.size();
    GridLine dense;
    if (n < 3){
        dense.points = corners;
        return dense;
    }
    auto control = [&](int i) -> vec2 {
        if (f_closed){
            return c[(i + n) % n];
        }
        if (i < 0){
            return c[0] * 2.0f - c[1];
        }
        if (i >= n){
            return c[n - 1] * 2.0f - c[n - 2];
        }
        return c[i];
    };
    //Knot spacing is the square root of the distance: alpha = 0.5, the centripetal spline.
    auto knot = [](const vec2& a, const vec2& b){
        vec2 d = b - a;
        return std::max(1e-4f,std::sqrt(std::sqrt(d.dot(d))));
    };
    const int samples = 16;
    int spans = f_closed ? n : n - 1;
    for (int i = 0; i < spans; i++){
        vec2 p0 = control(i - 1);
        vec2 p1 = control(i);
        vec2 p2 = control(i + 1);
        vec2 p3 = control(i + 2);
        float t0 = 0.0f;
        float t1 = t0 + knot(p0,p1);
        float t2 = t1 + knot(p1,p2);
        float t3 = t2 + knot(p2,p3);
        for (int k = 0; k < samples; k++){
            //Barry and Goldman's pyramid, which is the Catmull-Rom curve for any knot spacing.
            float t = t1 + (t2 - t1) * (float)k / (float)samples;
            vec2 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
            vec2 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
            vec2 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
            vec2 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
            vec2 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
            dense.points.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
        }
    }
    dense.points.push_back(f_closed ? dense.points[0] : c[n - 1]);
    if (!f_closed){
        dense.points[0] = c[0];
    }

    std::vector<float> s = LineArcLengths(dense);
    int segments = std::max(f_closed ? 3 : 1,(int)std::lround(s.back() / spacing));
    GridLine out;
    for (int k = 0; k < segments; k++){
        out.points.push_back(LinePointAt(dense,s,s.back() * (float)k / (float)segments));
    }
    out.points.push_back(f_closed ? out.points[0] : dense.points.back());
    return out;
}

/*
    Two neighbouring triangles as one quad. Both are wound the same way, so the edge they share
    runs u->w in `a` and w->u in `b`. Walking the outline with that edge removed gives u, q, w, p -
    which keeps the winding.
*/
Poly MergePair(const Poly& a, const Poly& b){
    Poly quad;
    quad.n = 4;
    for (int k = 0; k < 3; k++){
        int u = a.v[k];
        int w = a.v[(k + 1) % 3];
        int p = a.v[(k + 2) % 3];
        bool f_has_u = false;
        bool f_has_w = false;
        int q = -1;
        for (int m = 0; m < 3; m++){
            if (b.v[m] == u){
                f_has_u = true;
            }else if (b.v[m] == w){
                f_has_w = true;
            }else{
                q = b.v[m];
            }
        }
        if (f_has_u && f_has_w){
            quad.v[0] = u;
            quad.v[1] = q;
            quad.v[2] = w;
            quad.v[3] = p;
            break;
        }
    }
    return quad;
}

/*
    Every polygon split into quads through its centroid and edge midpoints: a quad into four, a
    triangle into three. Midpoints are shared between the two polygons on an edge, which is what
    keeps the result one connected mesh.

    `out_pos` starts as a copy of `in_pos`, so the input's vertices keep their indices - the
    "one vertex space" rule in Grid.h. New vertices are appended in polygon order, so the result
    depends only on the input, never on hash-map iteration.

    Child k is (v_k, m_k, c, m_{k-1}): it starts at the parent's corner k, so a fine quad's corner
    0 is always a vertex of its parent, and its edges 0 and 3 lie on the parent's outline. The
    coarse-edge view relies on exactly that.

    Pins carry over the same way: old vertices keep theirs and centroids are free. A chain edge -
    one `in_chain` says runs along a line - has its midpoint pinned to that line and put on it, and
    its two halves are chain edges of the result. Named edge by edge rather than worked out from
    which vertices are pinned, because two pinned vertices can share an edge that cuts across a
    bend instead of following the line.
*/
void Subdivide(const std::vector<vec2>& in_pos, const std::vector<int>& in_pin,
               const std::unordered_map<uint64_t,int>& in_chain, const std::vector<Poly>& polys,
               const std::vector<GridLine>& lines,
               std::vector<vec2>& out_pos, std::vector<int>& out_pin,
               std::unordered_map<uint64_t,int>& out_chain, std::vector<GridQuad>& out_quads){
    out_pos = in_pos;
    out_pin = in_pin;
    out_chain.clear();
    out_quads.clear();
    out_quads.reserve(polys.size() * 4);
    std::unordered_map<uint64_t,int> midpoints;
    midpoints.reserve(polys.size() * 3);

    for (int pi = 0; pi < (int)polys.size(); pi++){
        const Poly& p = polys[pi];
        vec2 c(0.0f,0.0f);
        for (int k = 0; k < p.n; k++){
            c += in_pos[p.v[k]];
        }
        c /= (float)p.n;
        int ci = (int)out_pos.size();
        out_pos.push_back(c);
        out_pin.push_back(GRID_PIN_FREE);

        int m[4] = {-1,-1,-1,-1};
        for (int k = 0; k < p.n; k++){
            int a = p.v[k];
            int b = p.v[(k + 1) % p.n];
            uint64_t key = EdgeKey(a,b);
            auto it = midpoints.find(key);
            if (it == midpoints.end()){
                vec2 mid = (in_pos[a] + in_pos[b]) * 0.5f;
                int pin = GRID_PIN_FREE;
                m[k] = (int)out_pos.size();
                auto chain = in_chain.find(key);
                if (chain != in_chain.end()){
                    pin = chain->second;
                    mid = NearestOnLine(lines[pin],mid);
                    out_chain[EdgeKey(a,m[k])] = pin;
                    out_chain[EdgeKey(m[k],b)] = pin;
                }
                out_pos.push_back(mid);
                out_pin.push_back(pin);
                midpoints[key] = m[k];
            }else{
                m[k] = it->second;
            }
        }
        for (int k = 0; k < p.n; k++){
            GridQuad q;
            q.v[0] = p.v[k];
            q.v[1] = m[k];
            q.v[2] = ci;
            q.v[3] = m[(k + p.n - 1) % p.n];
            q.parent = pi;
            out_quads.push_back(q);
        }
    }
}

//Edges, valence and the outline. An edge used by one quad is on the map's outline; one used by
//more than two means the mesh is broken, and is counted rather than trusted.
void Analyse(GridLevel& level){
    std::unordered_map<uint64_t,int> uses;
    uses.reserve(level.quads.size() * 3);
    for (const GridQuad& q : level.quads){
        for (int k = 0; k < 4; k++){
            uses[EdgeKey(q.v[k],q.v[(k + 1) % 4])]++;
        }
    }
    size_t n = level.pos.size();
    level.valence.assign(n,0);
    level.f_boundary.assign(n,0);
    level.edges.clear();
    level.edges.reserve(uses.size());
    level.num_bad_edges = 0;
    for (const auto& e : uses){
        int a = (int)(e.first >> 32);
        int b = (int)(e.first & 0xFFFFFFFFu);
        level.edges.push_back(std::make_pair(a,b));
        if (level.valence[a] < 255) level.valence[a]++;
        if (level.valence[b] < 255) level.valence[b]++;
        if (e.second == 1){
            level.f_boundary[a] = 1;
            level.f_boundary[b] = 1;
        }else if (e.second > 2){
            level.num_bad_edges++;
        }
    }
    //Sorted so the edge list - and anything drawn or hashed from it - does not depend on the hash
    //map's iteration order.
    std::sort(level.edges.begin(),level.edges.end());
}

/*
    Stalberg's relaxation: every quad pulls its corners toward its best-fit square, the pulls on a
    vertex are averaged, and the vertex moves part of the way. Repeated, the quads settle as square
    as their neighbours allow.

    Averaged rather than summed so a vertex where six quads meet moves no faster than one where
    three do - otherwise the irregular vertices, the ones that matter, would overshoot.

    A pinned vertex moves and is then put back on its line, which keeps only the part of the pull
    along it. The map's outline is pinned that way, so the map keeps its size without its edge
    cells being held to wherever the lattice happened to put them.
*/
void Relax(GridLevel& level, const std::vector<GridLine>& lines, int passes, float strength){
    size_t n = level.pos.size();
    std::vector<vec2> acc(n);
    std::vector<uint8_t> count(n);
    //Each pinned vertex's segment, found once by a whole-line search: every pinned vertex starts
    //exactly on its line, so that search cannot pick the wrong stretch of it.
    std::vector<int> segment(n,-1);
    for (size_t i = 0; i < n; i++){
        if (level.pin[i] >= 0){
            NearestOnLine(lines[level.pin[i]],level.pos[i],&segment[i]);
        }
    }
    for (int pass = 0; pass < passes; pass++){
        std::fill(acc.begin(),acc.end(),vec2(0.0f,0.0f));
        std::fill(count.begin(),count.end(),0);
        for (const GridQuad& q : level.quads){
            vec2 p[4];
            vec2 t[4];
            for (int k = 0; k < 4; k++){
                p[k] = level.pos[q.v[k]];
            }
            GridSquareFit(p,t);
            for (int k = 0; k < 4; k++){
                acc[q.v[k]] += t[k] - p[k];
                count[q.v[k]]++;
            }
        }
        for (size_t i = 0; i < n; i++){
            if (level.pin[i] == GRID_PIN_FIXED || count[i] == 0){
                continue;
            }
            level.pos[i] += acc[i] * (strength / (float)count[i]);
            if (level.pin[i] >= 0){
                level.pos[i] = NearestOnLine(lines[level.pin[i]],level.pos[i],&segment[i]);
            }
        }
    }
}

/*
    UNTANGLING what the relaxation leaves folded. Where a balcony's chain leaves its rim, the two
    chains run a cell or two apart for a moment, and a vertex there can be left on the wrong side of
    a chain once both are moved onto their lines - a quad folded over itself, which the square-fit
    relaxation cannot undo, since it fits each quad as it lies. So each corner of a folded quad is
    moved to the average of its neighbours - a pinned one then back onto its line, a fixed one not
    at all - and that is repeated until none is folded. Each pass that still finds the fold smooths
    a ring further out round it, since a fold against a chain is often held by the chain's own
    vertices beside it. A vertex's move depends only on positions and on vertex order, so it is the
    same every run.
*/
static bool QuadFolded(const GridLevel& level, const GridQuad& q){
    for (int k = 0; k < 4; k++){
        vec2 a = level.pos[q.v[(k + 1) % 4]] - level.pos[q.v[k]];
        vec2 b = level.pos[q.v[(k + 2) % 4]] - level.pos[q.v[(k + 1) % 4]];
        if (a.x * b.y - a.y * b.x <= 0.0f){
            return true;
        }
    }
    return false;
}

static int Untangle(GridLevel& level, const std::vector<GridLine>& lines, int passes){
    size_t n = level.pos.size();
    std::vector<std::vector<int>> around(n);
    for (const std::pair<int,int>& e : level.edges){
        around[e.first].push_back(e.second);
        around[e.second].push_back(e.first);
    }
    int moved = 0;
    std::vector<uint8_t> active(n,0);
    for (int pass = 0; pass < passes; pass++){
        bool f_any = false;
        for (const GridQuad& q : level.quads){
            if (QuadFolded(level,q)){
                f_any = true;
                for (int k = 0; k < 4; k++){
                    active[q.v[k]] = 1;
                }
            }
        }
        if (!f_any){
            break;
        }
        //One ring further out than last pass.
        if (pass > 0){
            std::vector<uint8_t> grown = active;
            for (size_t v = 0; v < n; v++){
                if (active[v]){
                    for (int w : around[v]){
                        grown[w] = 1;
                    }
                }
            }
            active.swap(grown);
        }
        std::vector<int> corners;
        for (size_t v = 0; v < n; v++){
            if (active[v]){
                corners.push_back((int)v);
            }
        }
        for (int v : corners){
            if (level.pin[v] == GRID_PIN_FIXED || around[v].empty()){
                continue;
            }
            vec2 sum(0.0f,0.0f);
            for (int w : around[v]){
                sum += level.pos[w];
            }
            vec2 at = sum / (float)around[v].size();
            if (level.pin[v] >= 0){
                at = NearestOnLine(lines[level.pin[v]],at);
            }
            level.pos[v] = at;
            moved++;
        }
    }
    return moved;
}

/*
    THE TRIANGLE LATTICE, with what the merge and the features need to know about it.

    Features are pinned HERE, on the lattice, before the merge - not on the coarse grid after it.
    Every lattice vertex is alike, six triangles round it, so a chain can go where its line goes
    rather than where the irregular vertices happen to be. And the merge is still open, so it can be
    steered along the chain (PairAlongChain). After subdivision a lattice chain edge is two coarse
    chain edges meeting at its midpoint, which has two quads on each side whatever the merge did;
    the same holds again at the fine level. Only the lattice vertices on the chain need care.

    Directions are numbered counter-clockwise in (x, z) from +x: 0 is +x, 1 and 2 the two diagonals
    toward +z, 3 is -x, 4 and 5 the diagonals toward -z. Odd rows sit half a side toward +x from
    even ones, so which (i, j) a diagonal reaches depends on the row.
*/
struct Lattice{
    int cols = 0;
    int rows = 0;
    float T = 0.0f;
    std::vector<vec2> pos;
    std::vector<int> pin;
    std::vector<Poly> tris;
    std::vector<int> neighbour;                     //per triangle edge t * 3 + k: the triangle across, or -1
    std::unordered_map<uint64_t,int> edge_tri;      //edge -> t * 3 + k of the first triangle on it
    std::vector<int> partner;                       //the merge: the triangle each one pairs with, or -1
    std::unordered_map<uint64_t,int> chain_edges;   //edge -> the line it runs along

    int Step(int v, int d) const{
        int i = v % (cols + 1);
        int j = v / (cols + 1);
        bool f_odd = (j & 1) != 0;
        switch (d){
            case 0: i++; break;
            case 1: if (f_odd) i++; j++; break;
            case 2: if (!f_odd) i--; j++; break;
            case 3: i--; break;
            case 4: if (!f_odd) i--; j--; break;
            case 5: if (f_odd) i++; j--; break;
        }
        if (i < 0 || i > cols || j < 0 || j > rows){
            return -1;
        }
        return j * (cols + 1) + i;
    }

};


uint64_t Fnv(uint64_t h, const void* data, size_t size){
    const uint8_t* b = (const uint8_t*)data;
    for (size_t i = 0; i < size; i++){
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

}

bool GridSettings::operator==(const GridSettings& o) const{
    return seed == o.seed && target_fine_cells == o.target_fine_cells && aspect == o.aspect &&
           triangle_side == o.triangle_side && relax_passes_coarse == o.relax_passes_coarse &&
           relax_passes_fine == o.relax_passes_fine && relax_strength == o.relax_strength;
}

const char* const grid_feature_names[GRID_FEATURE_KINDS] = {"rim","shard","column","ledge","balcony"};

GridLine GridSmoothLine(const std::vector<vec2>& corners, bool f_closed, float spacing){
    return SmoothLine(corners,f_closed,spacing);
}

/*
    Every point of `a` against every segment of `b`, with a box test first so lines far apart cost
    a comparison each. Lines are resampled every quarter side, so testing a's points rather than
    its segments misses at most an eighth of a side - well inside what the spacing leaves spare.
    For a line against itself the arc between the two places must be at least `skip_arc`, the
    shorter way round on a closed line.
*/
float GridLineGap(const GridLine& a, const GridLine& b, float skip_arc, vec2* where){
    float best = 1e30f;
    if (a.points.size() < 2 || b.points.size() < 2){
        return best;
    }
    bool f_same = &a == &b;
    std::vector<float> sa = LineArcLengths(a);
    std::vector<float> sb = LineArcLengths(b);
    bool f_closed = LineClosed(b);
    float total = sb.back();
    vec2 lo = b.points[0];
    vec2 hi = b.points[0];
    for (const vec2& p : b.points){
        lo.x = std::min(lo.x,p.x);
        lo.y = std::min(lo.y,p.y);
        hi.x = std::max(hi.x,p.x);
        hi.y = std::max(hi.y,p.y);
    }
    for (size_t i = 0; i < a.points.size(); i++){
        const vec2& p = a.points[i];
        float dx = std::max(0.0f,std::max(lo.x - p.x,p.x - hi.x));
        float dz = std::max(0.0f,std::max(lo.y - p.y,p.y - hi.y));
        if (dx * dx + dz * dz >= best * best){
            continue;
        }
        for (size_t k = 0; k + 1 < b.points.size(); k++){
            vec2 q0 = b.points[k];
            vec2 d = b.points[k + 1] - q0;
            float len2 = d.dot(d);
            float t = len2 > 0.0f ? std::max(0.0f,std::min(1.0f,(p - q0).dot(d) / len2)) : 0.0f;
            if (f_same){
                float arc = std::fabs(sa[i] - (sb[k] + (sb[k + 1] - sb[k]) * t));
                if (f_closed){
                    arc = std::min(arc,total - arc);
                }
                if (arc < skip_arc){
                    continue;
                }
            }
            vec2 e = p - (q0 + d * t);
            float dist = std::sqrt(e.dot(e));
            if (dist < best){
                best = dist;
                if (where){
                    *where = p;
                }
            }
        }
    }
    return best;
}

void GridSquareFit(const vec2 p[4], vec2 target[4]){
    vec2 c = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    //Turn every corner's offset back onto corner 0's and average: that is the square's corner 0.
    vec2 s = (p[0] - c);
    s += RotM90(p[1] - c);
    s -= (p[2] - c);
    s += Rot90(p[3] - c);
    s = s * 0.25f;
    target[0] = c + s;
    target[1] = c + Rot90(s);
    target[2] = c - s;
    target[3] = c + RotM90(s);
}

float GridQuadSquareness(const vec2 p[4]){
    vec2 t[4];
    GridSquareFit(p,t);
    vec2 c = (p[0] + p[1] + p[2] + p[3]) * 0.25f;
    float err = 0.0f;
    float size = 0.0f;
    for (int k = 0; k < 4; k++){
        vec2 e = t[k] - p[k];
        vec2 d = p[k] - c;
        err += e.dot(e);
        size += d.dot(d);
    }
    if (size <= 0.0f){
        return 0.0f;
    }
    float s = 1.0f - std::sqrt(err / size);
    return std::max(0.0f,std::min(1.0f,s));
}

void Grid::Generate(const GridSettings& s){
    auto t0 = std::chrono::steady_clock::now();
    settings = s;

    /*
        ITS OWN RRandom, never Application::rrand (README.md, "Replay from a saved state"):
        generating or previewing a world must not move the simulation's stream.

        A megabyte of stream. RRandom reads a fixed buffer and wraps at its end, and the shuffle
        plus the merge draw eight bytes per lattice triangle - about 100 KB at the default size -
        so this leaves room for much larger maps before the stream would repeat.
    */
    RRandom rng((int)s.seed);
    rng.Generate((size_t)1 << 20);

    //--- 1. The lattice -----------------------------------------------------------------------
    /*
        Sized from the target: a coarse polygon becomes four fine cells, and a merged pair of
        triangles becomes four coarse quads where a leftover triangle becomes three - about 2.1 per
        triangle with the merge rate this gets. So triangles ~ fine / 4 / 2.1.
    */
    const float T = s.triangle_side;
    const float H = T * std::sqrt(3.0f) * 0.5f;
    double want_tris = (double)s.target_fine_cells / 4.0 / 2.1;
    int cols = std::max(4,(int)std::lround(std::sqrt(want_tris * H * s.aspect / (2.0 * T))));
    int rows = std::max(2,(int)std::lround(cols * T / (H * s.aspect)));
    float width = cols * T;
    float depth = rows * H;

    /*
        Odd rows sit half a side right of even rows, so the lattice's left and right ends zig-zag.
        The rows are offset a quarter side either way of centre, which puts the zig-zag's middle on
        x = +-width/2, and then the outline is pinned flat onto the rectangle: corners fixed, every
        other outline vertex on one of its four edges, sliding along it as the grid relaxes.
    */
    const float x0 = -width * 0.5f;
    const float x1 = width * 0.5f;
    const float z0 = -depth * 0.5f;
    const float z1 = depth * 0.5f;
    lines.assign(4,GridLine());
    lines[0].points = {vec2(x0,z0),vec2(x0,z1)};
    lines[1].points = {vec2(x1,z0),vec2(x1,z1)};
    lines[2].points = {vec2(x0,z0),vec2(x1,z0)};
    lines[3].points = {vec2(x0,z1),vec2(x1,z1)};

    Lattice L;
    L.cols = cols;
    L.rows = rows;
    L.T = T;
    std::vector<vec2>& lattice = L.pos;
    std::vector<int>& lattice_pin = L.pin;
    lattice.reserve((size_t)(cols + 1) * (rows + 1));
    lattice_pin.reserve((size_t)(cols + 1) * (rows + 1));
    for (int j = 0; j <= rows; j++){
        float offset = (j & 1) ? 0.25f * T : -0.25f * T;
        for (int i = 0; i <= cols; i++){
            vec2 p(i * T + offset + x0,j * H + z0);
            int pin = GRID_PIN_FREE;
            bool f_side = i == 0 || i == cols;
            bool f_end = j == 0 || j == rows;
            if (f_side && f_end){
                pin = GRID_PIN_FIXED;
                p = vec2(i == 0 ? x0 : x1,j == 0 ? z0 : z1);
            }else if (f_side || f_end){
                pin = f_side ? (i == 0 ? 0 : 1) : (j == 0 ? 2 : 3);
                p = NearestOnLine(lines[pin],p);
            }
            lattice.push_back(p);
            lattice_pin.push_back(pin);
        }
    }
    auto at = [cols](int i, int j){ return j * (cols + 1) + i; };

    std::vector<Poly>& tris = L.tris;
    tris.reserve((size_t)cols * rows * 2);
    auto add_tri = [&](int a, int b, int c){
        Poly p;
        p.n = 3;
        p.v[0] = a;
        p.v[1] = b;
        p.v[2] = c;
        if (SignedArea(lattice,p.v,3) < 0.0f){
            std::swap(p.v[1],p.v[2]);
        }
        tris.push_back(p);
    };
    //Even rows sit at offset 0 and odd rows at half a side, so the two kinds of row pair make
    //their triangles differently.
    for (int j = 0; j < rows; j++){
        for (int i = 0; i < cols; i++){
            if ((j & 1) == 0){
                add_tri(at(i,j),at(i + 1,j),at(i,j + 1));
                add_tri(at(i + 1,j),at(i + 1,j + 1),at(i,j + 1));
            }else{
                add_tri(at(i,j),at(i + 1,j + 1),at(i,j + 1));
                add_tri(at(i,j),at(i + 1,j),at(i + 1,j + 1));
            }
        }
    }
    num_lattice_triangles = (int)tris.size();

    //--- 2. Merge random neighbours into quads ----------------------------------------------------
    std::vector<int>& neighbour = L.neighbour;
    neighbour.assign((size_t)tris.size() * 3,-1);
    {
        std::unordered_map<uint64_t,int>& first = L.edge_tri;
        first.reserve(tris.size() * 2);
        for (int t = 0; t < (int)tris.size(); t++){
            for (int k = 0; k < 3; k++){
                uint64_t key = EdgeKey(tris[t].v[k],tris[t].v[(k + 1) % 3]);
                auto it = first.find(key);
                if (it == first.end()){
                    first[key] = t * 3 + k;
                }else{
                    int other = it->second;
                    neighbour[t * 3 + k] = other / 3;
                    neighbour[other] = t;
                }
            }
        }
    }
    //The outline's edges are chains of its four lines; an edge at a corner takes the line of its
    //other end.
    for (int t = 0; t < (int)tris.size(); t++){
        for (int k = 0; k < 3; k++){
            if (neighbour[t * 3 + k] == -1){
                int a = tris[t].v[k];
                int b = tris[t].v[(k + 1) % 3];
                L.chain_edges[EdgeKey(a,b)] = lattice_pin[a] >= 0 ? lattice_pin[a] : lattice_pin[b];
            }
        }
    }
    //Visited in a seeded order, so which pairs form is random but the same for a seed.
    std::vector<int> order(tris.size());
    for (int t = 0; t < (int)tris.size(); t++){
        order[t] = t;
    }
    for (int i = (int)order.size() - 1; i > 0; i--){
        std::swap(order[i],order[rng.GetInt(0,i)]);
    }
    /*
        THE OUTER RING IS NOT RANDOM. Every outline vertex sits on a straight edge, at 180 degrees,
        so it wants exactly two coarse quads meeting there at a right angle each. Random pairs give
        some three or four - wedges of 60 or 45 degrees that no relaxation can square, and on the
        flattened sides they were the worst quads on the map. Merging the two triangles of every
        lattice cell in the outer ring gives each outline vertex exactly two (a map corner, one).
        Along the top and bottom that pairing is also the only one that does: the side cells take
        the end triangles, and each next one is then forced. Paired before the shuffle, so they
        draw nothing from the stream.
    */
    std::vector<int>& partner = L.partner;
    partner.assign(tris.size(),-1);
    for (int j = 0; j < rows; j++){
        for (int i = 0; i < cols; i++){
            if (i == 0 || i == cols - 1 || j == 0 || j == rows - 1){
                int a = (j * cols + i) * 2;
                partner[a] = a + 1;
                partner[a + 1] = a;
            }
        }
    }

    /*
        THE FEATURES are NOT pinned (2026-10-06). The terrain is a height per plot drawn as columns
        (TerrainMesh.cpp), so a rim is wherever the plots' heights change - jagged at a plot's size by
        design, and no edge of the grid has to follow it. The layout's lines are still appended, as
        data: the terrain reads its levels from them and the rivers their falls.
    */
    layout = GenerateChasmLayout(s.seed,vec2(x0,z0),vec2(x1,z1),T);
    feature_line_base = (int)lines.size();
    for (const GridFeature& f : layout.features){
        lines.push_back(f.line);
    }

    for (int t : order){
        if (partner[t] != -1){
            continue;
        }
        int candidates[3];
        int num = 0;
        for (int k = 0; k < 3; k++){
            int nb = neighbour[t * 3 + k];
            if (nb != -1 && partner[nb] == -1){
                candidates[num++] = nb;
            }
        }
        if (num == 0){
            continue;
        }
        int pick = candidates[rng.GetInt(0,num - 1)];
        partner[t] = pick;
        partner[pick] = t;
    }

    //Polygons in triangle order, not visit order, so the rest of the build does not inherit the
    //shuffle - only the pairing is random.
    std::vector<Poly> polys;
    polys.reserve(tris.size());
    num_leftover_triangles = 0;
    for (int t = 0; t < (int)tris.size(); t++){
        int o = partner[t];
        if (o == -1){
            polys.push_back(tris[t]);
            num_leftover_triangles++;
            continue;
        }
        if (o < t){
            continue;
        }
        polys.push_back(MergePair(tris[t],tris[o]));
    }

    //--- 3. and 4. The coarse level, relaxed ---------------------------------------------------------
    std::unordered_map<uint64_t,int> coarse_chain;
    Subdivide(lattice,lattice_pin,L.chain_edges,polys,lines,coarse.pos,coarse.pin,coarse_chain,coarse.quads);
    Analyse(coarse);
    Relax(coarse,lines,s.relax_passes_coarse,s.relax_strength);

    //--- 5. The fine level, relaxed ----------------------------------------------------------------
    std::vector<Poly> coarse_polys(coarse.quads.size());
    for (size_t i = 0; i < coarse.quads.size(); i++){
        coarse_polys[i].n = 4;
        for (int k = 0; k < 4; k++){
            coarse_polys[i].v[k] = coarse.quads[i].v[k];
        }
    }
    std::unordered_map<uint64_t,int> fine_chain;
    Subdivide(coarse.pos,coarse.pin,coarse_chain,coarse_polys,lines,fine.pos,fine.pin,fine_chain,fine.quads);
    Analyse(fine);
    Relax(fine,lines,s.relax_passes_fine,s.relax_strength);
    Untangle(fine,lines,40);

    //The coarse corners moved with the fine relaxation; they are the same vertices.
    for (size_t i = 0; i < coarse.pos.size(); i++){
        coarse.pos[i] = fine.pos[i];
    }

    bounds_min = vec2(1e30f,1e30f);
    bounds_max = vec2(-1e30f,-1e30f);
    for (const vec2& p : fine.pos){
        bounds_min.x = std::min(bounds_min.x,p.x);
        bounds_min.y = std::min(bounds_min.y,p.y);
        bounds_max.x = std::max(bounds_max.x,p.x);
        bounds_max.y = std::max(bounds_max.y,p.y);
    }
    generate_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

uint64_t Grid::Hash() const{
    uint64_t h = 14695981039346656037ull;
    h = Fnv(h,fine.pos.data(),fine.pos.size() * sizeof(vec2));
    for (const GridQuad& q : fine.quads){
        h = Fnv(h,q.v,sizeof(q.v));
        h = Fnv(h,&q.parent,sizeof(q.parent));
    }
    for (const GridQuad& q : coarse.quads){
        h = Fnv(h,q.v,sizeof(q.v));
    }
    //The rivers are not pinned into the grid, so nothing above would notice them move; they are
    //generated with it and belong to the world all the same.
    for (const GridRiverLine& r : layout.rivers){
        h = Fnv(h,r.points.data(),r.points.size() * sizeof(vec2));
        h = Fnv(h,&r.width,sizeof(r.width));
    }
    return h;
}

vec2 Grid::FineQuadCentre(int q) const{
    const GridQuad& quad = fine.quads[q];
    return (fine.pos[quad.v[0]] + fine.pos[quad.v[1]] + fine.pos[quad.v[2]] + fine.pos[quad.v[3]]) * 0.25f;
}

#ifdef DEBUG
/*
    --- THE PINNED HASHES ---------------------------------------------------------------------------
    What the default settings produce for a few seeds, in this build. A change to the generator
    moves them, and that is the point: if the change was meant to, re-pin from the hash the check
    prints; if it was not, the check just found a side effect.

    Pinned against the DEFAULT settings only - any other setting is a grid nobody has looked at, so
    there is nothing to compare it with and the check says so instead of failing.
*/
struct PinnedGridHash{
    uint32_t seed;
    uint64_t hash;
};
//Pinned 2026-10-05 with the chasm generated from the seed (step 10) and the rivers in the hash,
//the same in the debug and release builds. Seeds 2 and 3 re-pinned the same day when rivers came to
//rise at the mountain's foot (biomes_plan.md step 3); seed 1 has no river from the north edge. All
//three re-pinned 2026-10-05 for balconies, the sides made whole and rivers on the home side only,
//and 2026-10-06 when the features stopped being pinned (one chasm, the terrain as columns).
static const PinnedGridHash pinned_grid_hashes[] = {
    {1,0xfce10a5bd5ed576bull},
    {2,0x5c89f61c4436cc92ull},
    {3,0xbc5356fa808d3848ull},
};

#define GRID_ISSUES_MAX         200     //enough to see a pattern, few enough to draw and list
#define GRID_WORST_SQUARES      20      //the least square quads, listed for looking at

GridCheckReport RunGridChecks(const Grid& grid){
    auto t0 = std::chrono::steady_clock::now();
    GridCheckReport report;
    report.hash = grid.Hash();
    auto add_issue = [&report](const char* kind, int index, vec2 where, bool f_failure){
        if ((int)report.issues.size() < GRID_ISSUES_MAX){
            GridIssue issue;
            issue.kind = kind;
            issue.index = index;
            issue.where = where;
            issue.f_failure = f_failure;
            report.issues.push_back(issue);
        }
    };
    auto add_result = [&report](const char* name, bool f_pass, bool f_skipped, const std::string& detail){
        GridCheckResult r;
        r.name = name;
        r.f_pass = f_pass;
        r.f_skipped = f_skipped;
        r.detail = detail;
        report.results.push_back(r);
        if (!f_pass){
            report.f_pass = false;
        }
    };
    char buf[256];

    //--- Deterministic: the same settings again give the same bits ---------------------------------
    {
        Grid again;
        again.Generate(grid.settings);
        uint64_t h = again.Hash();
        snprintf(buf,sizeof(buf),"hash %016llx, regenerated %016llx",
                 (unsigned long long)report.hash,(unsigned long long)h);
        add_result("deterministic",h == report.hash,false,buf);
    }

    //--- Pinned: and the same bits as when this seed was last looked at ----------------------------
    {
        GridSettings defaults;
        defaults.seed = grid.settings.seed;
        if (!(grid.settings == defaults)){
            add_result("pinned",true,true,"not the default settings - nothing pinned to compare with");
        }else{
            const PinnedGridHash* pin = NULL;
            for (const PinnedGridHash& p : pinned_grid_hashes){
                if (p.seed == grid.settings.seed){
                    pin = &p;
                }
            }
            if (!pin){
                snprintf(buf,sizeof(buf),"seed %u is not pinned - to pin it, add {%u,0x%016llxull}",
                         grid.settings.seed,grid.settings.seed,(unsigned long long)report.hash);
                add_result("pinned",true,true,buf);
            }else{
                snprintf(buf,sizeof(buf),"pinned %016llx, got %016llx",
                         (unsigned long long)pin->hash,(unsigned long long)report.hash);
                add_result("pinned",pin->hash == report.hash,false,buf);
            }
        }
    }

    //--- Edges: none shared by more than two quads --------------------------------------------------
    {
        snprintf(buf,sizeof(buf),"coarse %i, fine %i edges on more than two quads",
                 grid.coarse.num_bad_edges,grid.fine.num_bad_edges);
        add_result("edges",grid.coarse.num_bad_edges == 0 && grid.fine.num_bad_edges == 0,false,buf);
    }

    //--- Outline: every outline vertex pinned, and on its line -------------------------------------
    //A vertex the pins missed would relax inward and dent the map's edge, which nothing else checks.
    {
        int loose = 0;
        int off = 0;
        for (size_t i = 0; i < grid.fine.pos.size(); i++){
            if (!grid.fine.f_boundary[i]){
                continue;
            }
            int pin = grid.fine.pin[i];
            if (pin == GRID_PIN_FREE){
                loose++;
                add_issue("outline",(int)i,grid.fine.pos[i],true);
            }else if (pin >= 0){
                vec2 d = NearestOnLine(grid.lines[pin],grid.fine.pos[i]) - grid.fine.pos[i];
                if (d.dot(d) > 1e-6f){
                    off++;
                    add_issue("outline",(int)i,grid.fine.pos[i],true);
                }
            }
        }
        snprintf(buf,sizeof(buf),"%i outline vertices not pinned, %i off their line",loose,off);
        add_result("outline",loose == 0 && off == 0,false,buf);
    }

    //--- Valence: every interior vertex meets 3 to 6 edges ----------------------------------------
    {
        int bad = 0;
        for (size_t i = 0; i < grid.fine.pos.size(); i++){
            if (grid.fine.f_boundary[i]){
                continue;
            }
            int v = grid.fine.valence[i];
            report.valence_hist[std::min(v,7)]++;
            if (v < 3 || v > 6){
                bad++;
                add_issue("valence",(int)i,grid.fine.pos[i],true);
            }
        }
        snprintf(buf,sizeof(buf),"%i interior vertices outside 3-6 (3:%i 4:%i 5:%i 6:%i)",bad,
                 report.valence_hist[3],report.valence_hist[4],report.valence_hist[5],report.valence_hist[6]);
        add_result("valence",bad == 0,false,buf);
    }

    //--- Folded: every fine quad convex and wound the right way -----------------------------------
    //Squareness alone would not catch this: a bow-tie can sit close to its best-fit square.
    std::vector<std::pair<float,int>> squareness;
    squareness.reserve(grid.fine.quads.size());
    {
        int folded = 0;
        double sum = 0.0;
        float worst = 1.0f;
        for (int qi = 0; qi < (int)grid.fine.quads.size(); qi++){
            const GridQuad& q = grid.fine.quads[qi];
            vec2 p[4];
            for (int k = 0; k < 4; k++){
                p[k] = grid.fine.pos[q.v[k]];
            }
            bool f_convex = true;
            for (int k = 0; k < 4; k++){
                vec2 a = p[(k + 1) % 4] - p[k];
                vec2 b = p[(k + 2) % 4] - p[(k + 1) % 4];
                if (a.x * b.y - a.y * b.x <= 0.0f){
                    f_convex = false;
                }
            }
            if (!f_convex){
                folded++;
                add_issue("folded",qi,grid.FineQuadCentre(qi),true);
            }
            float sq = GridQuadSquareness(p);
            squareness.push_back(std::make_pair(sq,qi));
            sum += sq;
            worst = std::min(worst,sq);
        }
        report.squareness_mean = squareness.empty() ? 0.0f : (float)(sum / squareness.size());
        report.squareness_min = worst;
        snprintf(buf,sizeof(buf),"%i of %i fine quads not convex",folded,(int)grid.fine.quads.size());
        add_result("folded",folded == 0,false,buf);
    }

    //--- Squareness: a figure, not a pass or fail --------------------------------------------------
    {
        int n = std::min((int)squareness.size(),GRID_WORST_SQUARES);
        std::partial_sort(squareness.begin(),squareness.begin() + n,squareness.end());
        for (int i = 0; i < n; i++){
            add_issue("squareness",squareness[i].second,grid.FineQuadCentre(squareness[i].second),false);
        }
        //The interior: quads with no pinned corner, what the grid makes of itself.
        double sum = 0.0;
        int count = 0;
        report.squareness_interior_min = 1.0f;
        for (const std::pair<float,int>& sq : squareness){
            const GridQuad& q = grid.fine.quads[sq.second];
            bool f_pinned = false;
            for (int k = 0; k < 4; k++){
                if (grid.fine.pin[q.v[k]] != GRID_PIN_FREE){
                    f_pinned = true;
                }
            }
            if (!f_pinned){
                sum += sq.first;
                count++;
                report.squareness_interior_min = std::min(report.squareness_interior_min,sq.first);
            }
        }
        report.squareness_interior_mean = count ? (float)(sum / count) : 0.0f;
        snprintf(buf,sizeof(buf),"mean %.3f, worst %.3f; interior mean %.3f, worst %.3f (the %i worst are marked)",
                 report.squareness_mean,report.squareness_min,report.squareness_interior_mean,
                 report.squareness_interior_min,n);
        add_result("squareness",true,false,buf);
    }

    /*
        No Features or Spacing checks since 2026-10-06: they proved the pinned chains, and nothing is
        pinned any more - the terrain is a height per plot (Terrain.h), its rims jagged by design.
    */

    //--- Layout: what the seed made of the chasm - a figure, and a failure only if no rift fitted --
    {
        const ChasmLayout& l = grid.layout;
        snprintf(buf,sizeof(buf),"%i rifts, %i cracks; balconies %i east, %i west; ledges %i of %i, shards %i of %i, "
                 "columns %i of %i; rivers %i of %i; %i attempts%s (%.1f ms)",
                 l.rifts,(int)l.cracks.size(),l.balconies[1],l.balconies[0],l.count[GRID_FEATURE_LEDGE],l.wanted[GRID_FEATURE_LEDGE],
                 l.count[GRID_FEATURE_SHARD],l.wanted[GRID_FEATURE_SHARD],l.count[GRID_FEATURE_COLUMN],
                 l.wanted[GRID_FEATURE_COLUMN],(int)l.rivers.size(),l.rivers_wanted,l.attempts,
                 l.f_rifts_ok ? "" : " - NONE KEPT ITS SPACING",l.generate_ms);
        //Home must have a balcony: it is the only floatstone before the first zeppelin.
        add_result("layout",l.f_rifts_ok && l.rifts > 0 && l.balconies[1] > 0,false,buf);
    }

    report.check_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    return report;
}
#endif
