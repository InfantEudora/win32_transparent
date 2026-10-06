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

//The stretch of `line` between two distances along it.
GridLine LinePiece(const GridLine& line, const std::vector<float>& s, float from, float to){
    GridLine piece;
    piece.points.push_back(LinePointAt(line,s,from));
    for (size_t k = 0; k < line.points.size(); k++){
        if (s[k] > from && s[k] < to){
            piece.points.push_back(line.points[k]);
        }
    }
    piece.points.push_back(LinePointAt(line,s,to));
    return piece;
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
    std::unordered_map<uint64_t,int> tri_at;        //sorted corners -> triangle
    std::vector<int> partner;                       //the merge: the triangle each one pairs with, or -1
    std::vector<uint8_t> f_chain;                   //on a feature's chain
    std::vector<uint8_t> near_chain;                //how many chain vertices are its neighbours
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

    int Direction(int v, int w) const{
        for (int d = 0; d < 6; d++){
            if (Step(v,d) == w){
                return d;
            }
        }
        return -1;
    }

    static uint64_t TriKey(int a, int b, int c){
        if (a > b) std::swap(a,b);
        if (b > c) std::swap(b,c);
        if (a > b) std::swap(a,b);
        return ((uint64_t)(uint32_t)a << 42) | ((uint64_t)(uint32_t)b << 21) | (uint64_t)(uint32_t)c;
    }

    //The triangle in sector s of v: between directions s and s + 1.
    int Sector(int v, int s) const{
        int a = Step(v,s % 6);
        int b = Step(v,(s + 1) % 6);
        if (a < 0 || b < 0){
            return -1;
        }
        auto it = tri_at.find(TriKey(v,a,b));
        return it == tri_at.end() ? -1 : it->second;
    }

    //The edge is the diagonal of a pair already merged - inside a quad, so not an edge any more.
    bool MergedAcross(int a, int b) const{
        auto it = edge_tri.find(EdgeKey(a,b));
        if (it == edge_tri.end()){
            return false;
        }
        int t = it->second / 3;
        int o = neighbour[it->second];
        return o != -1 && partner[t] == o;
    }

    void Commit(int v, int line, bool f_count_neighbours){
        f_chain[v] = 1;
        if (pin[v] != GRID_PIN_FIXED){
            pin[v] = line;
        }
        if (f_count_neighbours){
            for (int d = 0; d < 6; d++){
                int w = Step(v,d);
                if (w >= 0 && near_chain[w] < 255){
                    near_chain[w]++;
                }
            }
        }
    }
};

#define CHAIN_ANCHOR_SPACING    4.0f    //lattice sides between the points a chain is made to pass through
//Straying from the line costs far more than turning: a chain that saves turns cuts across curves,
//and the cells it leaves outside are squashed when it is moved onto the line.
#define CHAIN_OFF_LINE_COST     12.0f   //per step, times (distance from the line / side) squared
#define CHAIN_TURN_COST         0.25f   //a 60 degree turn
#define CHAIN_SHARP_TURN_COST   0.5f    //a 120 degree turn, only where the line itself bends that sharply
#define CHAIN_SAME_TURN_COST    0.5f    //a turn the same way as the chain's last turn, on top
#define CHAIN_TURN_STATES       9       //see TurnCost

//The line's direction at a distance along it, as a unit vector. A closed line wraps round.
vec2 LineTangent(const GridLine& line, const std::vector<float>& s, float at){
    float total = s.back();
    if (LineClosed(line) && total > 0.0f){
        if (at < 0.0f) at += total;
        if (at > total) at -= total;
    }
    size_t k = 1;
    while (k + 1 < s.size() && s[k] < at){
        k++;
    }
    vec2 d = line.points[k] - line.points[k - 1];
    float len = std::sqrt(d.dot(d));
    return len > 0.0f ? d / len : vec2(1.0f,0.0f);
}

//How far along the line its point nearest `p` is, searching from `*segment` if that is set.
float LineDistanceAt(const GridLine& line, const std::vector<float>& s, const vec2& p, int* segment){
    vec2 q = NearestOnLine(line,p,segment);
    vec2 d = q - line.points[*segment];
    return s[*segment] + std::sqrt(d.dot(d));
}

/*
    Which way the line turns between two distances along it: 0 by less than `degrees`, 1 counter-
    clockwise, 2 clockwise. By the tangents' dot and cross products against the threshold's
    cosine, so no trigonometry - its results are not promised to be the same in every build.
*/
int LineTurn(const GridLine& line, const std::vector<float>& s, float from, float to, float cos_threshold){
    vec2 a = LineTangent(line,s,from);
    vec2 b = LineTangent(line,s,to);
    if (a.dot(b) >= cos_threshold){
        return 0;
    }
    return a.x * b.y - a.y * b.x > 0.0f ? 1 : 2;
}

/*
    The cost of a chain turning from `dir_in` to `dir_out`, or a negative number when it may not.
    `state` is what the chain did before: the sign of its last turn (0 none yet, 1 counter-
    clockwise, 2 clockwise) times three, plus what its previous step was (0 straight or none, 1 a
    60 degree turn, 2 a 120 degree turn). The new state goes to `state_out`.

    Straight on and 60 degrees anywhere. 120 degrees only where the line itself bends sharply
    the same way (`line_bend`, as LineTurn gives it): that corner is one triangle inside, which
    is one quad bent through the line's own angle - right at a sharp tip, and a quad bent flat
    anywhere else.

    A turn the same way as the last costs extra. Along one side of the chain a 60 degree turn
    leaves four triangles outside the bend and two inside, and PairAlongChain can give every vertex
    the quads it wants only while bends alternate. A curve has to turn one way overall, so some of
    that is unavoidable; the cost keeps it where the line itself bends. Next to a 120 degree turn a
    same-way turn is not allowed at all: it brings the chain back beside itself.
*/
float TurnCost(int dir_in, int dir_out, int state, int line_bend, int* state_out){
    int last_sign = state / 3;
    int last_step = state % 3;
    int t = (dir_out - dir_in + 6) % 6;
    if (t == 0){
        *state_out = last_sign * 3;
        return 0.0f;
    }
    if (t == 3){
        return -1.0f;
    }
    int sign = (t == 1 || t == 2) ? 1 : 2;
    bool f_sharp = t == 2 || t == 4;
    if (f_sharp && line_bend != sign){
        return -1.0f;
    }
    if (sign == last_sign && (last_step == 2 || (f_sharp && last_step != 0))){
        return -1.0f;
    }
    *state_out = sign * 3 + (f_sharp ? 2 : 1);
    return (f_sharp ? CHAIN_SHARP_TURN_COST : CHAIN_TURN_COST) + (sign == last_sign ? CHAIN_SAME_TURN_COST : 0.0f);
}

//Where a chain has got to: the vertex, the direction it arrived in (-1 at its start), and its
//turn state (see TurnCost).
struct ChainEnd{
    int v = -1;
    int dir = -1;
    int turn = 0;
};

//What ChainSegment keeps between calls for one feature: its search arrays, reset by what was
//touched rather than reallocated, and each vertex's sharp-bend reading of the line.
struct ChainSearch{
    std::vector<float> cost;
    std::vector<int> back;
    std::vector<int> touched;
    std::vector<int8_t> bend;       //per lattice vertex: LineTurn near it, -1 not worked out yet
};

/*
    The cheapest lattice path from `from` to `target` along `piece` of a feature's `line`: a step
    costs its length, more the further it strays from the line, plus its turn. Dijkstra over
    (vertex, arrival direction, turn state), so the turn rules hold along the whole chain.

    A step may not land on the outline, on a chain, or next to a chain vertex other than the one it
    left - two chains, or two parts of one, a vertex apart would share triangles and be paired
    against each other. The exceptions are the vertex before `from` (`before`), which a 120 degree
    turn at `from` comes back beside, and for a closing segment the closed chain's first vertex.
    `closing_dir` >= 0 means `target` is that first vertex and the chain must leave it in that
    direction, so the turn there is counted too.

    And the JUNCTION a balcony's chain starts and ends at, a vertex of its rim's chain: the rim's
    vertices near it (`exempt`, flagged per lattice vertex) are not counted against a step, or the
    chain could never leave the rim nor come back to it - the first step off a straight chain is
    always beside two of its vertices. With that let off, the way out over the plateau is as open as
    the way into the chasm, so the step off a junction must be `first_step` and the step onto one
    must come from `last_step` (each -1 for none): the spokes aimed along the balcony's line.
*/
bool ChainSegment(const Lattice& L, ChainSearch& search, const GridLine& line, const std::vector<float>& s,
                  const GridLine& piece, const ChainEnd& from, int before, int target, int closing_dir,
                  const std::vector<uint8_t>* exempt, int first_step, int last_step,
                  std::vector<int>& path, ChainEnd& to){
    const float INF = 1e30f;
    size_t states = L.pos.size() * 6 * CHAIN_TURN_STATES;
    if (search.cost.size() != states){
        search.cost.assign(states,INF);
        search.back.assign(states,-1);
        search.bend.assign(L.pos.size(),-1);
    }
    for (int st : search.touched){
        search.cost[st] = INF;
        search.back[st] = -1;
    }
    search.touched.clear();
    typedef std::pair<float,int> Entry;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
    auto state = [](int v, int d, int turn){ return (v * 6 + d) * CHAIN_TURN_STATES + turn; };
    int start_state = from.dir >= 0 ? state(from.v,from.dir,from.turn) : -1;

    auto bend = [&](int v) -> int {
        if (search.bend[v] < 0){
            int seg = -1;
            float at = LineDistanceAt(line,s,L.pos[v],&seg);
            search.bend[v] = (int8_t)LineTurn(line,s,at - L.T,at + L.T,0.5f);
        }
        return search.bend[v];
    };
    auto step_ok = [&](int v, int w) -> bool {
        if (L.MergedAcross(v,w)){
            return false;
        }
        if (first_step >= 0 && v == from.v && w != first_step){
            return false;
        }
        if (w == target){
            return last_step < 0 || v == last_step;
        }
        if (L.pin[w] != GRID_PIN_FREE || L.f_chain[w]){
            return false;
        }
        bool f_exempt_v = exempt && (*exempt)[v];
        int expected = (L.f_chain[v] && !f_exempt_v) ? 1 : 0;
        int near = L.near_chain[w];
        if (exempt){
            for (int d = 0; d < 6; d++){
                int x = L.Step(w,d);
                if (x >= 0 && (*exempt)[x]){
                    near--;
                }
            }
        }
        if (v == from.v && before >= 0 && L.Direction(w,before) >= 0){
            expected++;
        }
        if (closing_dir >= 0 && L.Direction(w,target) >= 0){
            expected++;
        }
        return near == expected;
    };
    auto step_cost = [&](int v, int w) -> float {
        vec2 mid = (L.pos[v] + L.pos[w]) * 0.5f;
        vec2 d = NearestOnLine(piece,mid) - mid;
        return 1.0f + CHAIN_OFF_LINE_COST * d.dot(d) / (L.T * L.T);
    };
    auto push = [&](int st, float c, int from_state){
        if (c < search.cost[st]){
            if (search.cost[st] >= INF){
                search.touched.push_back(st);
            }
            search.cost[st] = c;
            search.back[st] = from_state;
            open.push(Entry(c,st));
        }
    };

    if (from.dir < 0){
        for (int d = 0; d < 6; d++){
            int w = L.Step(from.v,d);
            if (w >= 0 && step_ok(from.v,w)){
                push(state(w,d,0),step_cost(from.v,w),-1);
            }
        }
    }else{
        push(start_state,0.0f,-1);
    }

    float best = INF;
    int best_state = -1;
    while (!open.empty()){
        Entry e = open.top();
        open.pop();
        if (e.first >= best){
            break;
        }
        if (e.first > search.cost[e.second]){
            continue;
        }
        int st = e.second;
        int v = st / (6 * CHAIN_TURN_STATES);
        int dir = (st / CHAIN_TURN_STATES) % 6;
        int turn = st % CHAIN_TURN_STATES;
        if (v == target && st != start_state){
            float total = e.first;
            if (closing_dir >= 0){
                int unused;
                float tc = TurnCost(dir,closing_dir,turn,bend(v),&unused);
                if (tc < 0.0f){
                    continue;
                }
                total += tc;
            }
            if (total < best){
                best = total;
                best_state = st;
            }
            continue;
        }
        for (int d = 0; d < 6; d++){
            int w = L.Step(v,d);
            if (w < 0 || !step_ok(v,w)){
                continue;
            }
            int next_turn;
            float tc = TurnCost(dir,d,turn,bend(v),&next_turn);
            if (tc < 0.0f){
                continue;
            }
            push(state(w,d,next_turn),e.first + step_cost(v,w) + tc,st);
        }
    }
    if (best_state < 0){
        return false;
    }
    path.clear();
    for (int st = best_state; st >= 0 && st != start_state; st = search.back[st]){
        path.push_back(st / (6 * CHAIN_TURN_STATES));
    }
    std::reverse(path.begin(),path.end());
    to.v = target;
    to.dir = (best_state / CHAIN_TURN_STATES) % 6;
    to.turn = best_state % CHAIN_TURN_STATES;
    return true;
}

/*
    What a chain vertex costs with `quads` coarse quads on one side of it, when that side wants
    `ideal` (ChainIdealQuads). One too many is a 60 degree wedge. One quad where the angle wants two
    or more is a quad bent toward flat, which no relaxation recovers. Two where a long bend wanted
    three only leaves that bend's corner unsquared - it costs, but like a wedge, not like a flat quad.
*/
float FanCost(int quads, int ideal){
    int over = quads - ideal;
    if (over >= 0){
        static const float cost[4] = {0.0f,1.0f,3.0f,6.0f};
        return cost[std::min(over,3)];
    }
    return quads <= 1 ? 40.0f * (float)(-over) : (float)(-over);
}

/*
    One side of a chain as a strip: the triangles touching the chain on that side, in order along
    it. Consecutive ones share an edge through a chain vertex - a LINK, owned by that vertex.
    Merging across a link takes one quad off its owner's fan, so choosing which links to merge
    decides how many quads each chain vertex has on this side. Each triangle merges at most once,
    which makes it a matching along the strip, and dynamic programming finds the cheapest by
    FanCost exactly.

    A link may already be decided: a pair merged earlier (the outline's ring) is forced, and a
    triangle merged with something off the strip cannot take a link. For a closed chain the strip
    is a ring and the link from its last triangle back to its first is tried both ways. `fan` and
    `ideal` are per chain index; an owner's fan is counted, not taken from its links, because a
    120 degree corner's one triangle belongs to three fans and is in the strip once.
*/
void PairStrip(Lattice& L, const std::vector<int>& strip, const std::vector<int>& owner, bool f_ring,
               const std::vector<int>& fan, const std::vector<int>& ideal){
    int m = (int)strip.size();
    int nl = f_ring ? m : m - 1;
    if (nl <= 0){
        return;
    }
    /*
        A triangle may be in the strip more than once, and not only at a 120 degree corner: inside
        a small closed chain - a column a few sides across - one triangle touches chain vertices
        that are not neighbours along it. The matching sees two places and could merge it at both,
        and the second merge overwrote the first: one partner pointed at a triangle that pointed
        elsewhere, and the triangle it lost fell out of the grid as a hole (seed 39). Such a
        triangle is left for the random merge.
    */
    std::unordered_map<int,int> seen;
    for (int t : strip){
        seen[t]++;
    }
    //Which values each link may take: bit 0 unmerged, bit 1 merged.
    std::vector<uint8_t> allowed(nl);
    for (int j = 0; j < nl; j++){
        int a = strip[j];
        int b = strip[(j + 1) % m];
        if (L.partner[a] == b){
            allowed[j] = 2;
        }else if (L.partner[a] != -1 || L.partner[b] != -1 || seen[a] > 1 || seen[b] > 1){
            allowed[j] = 1;
        }else{
            allowed[j] = 3;
        }
    }

    const int C = 8;
    const float INF = 1e30f;
    auto idx = [](int j, int p, int c){ return (j * 2 + p) * C + c; };
    auto owner_cost = [&](int o, int merged){ return FanCost(fan[o] - merged,ideal[o]); };
    float best_total = INF;
    std::vector<uint8_t> best_choice;
    for (int wrap = 0; wrap <= (f_ring ? 1 : 0); wrap++){
        if (f_ring && !(allowed[nl - 1] & (1 << wrap))){
            continue;
        }
        std::vector<float> dp((size_t)(nl + 1) * 2 * C,INF);
        std::vector<int> from((size_t)(nl + 1) * 2 * C,-1);
        //p: whether triangle j is already merged by the link before it - for a ring, by the wrap.
        dp[idx(0,wrap,0)] = 0.0f;
        for (int j = 0; j < nl; j++){
            bool f_new_owner = j > 0 && owner[j] != owner[j - 1];
            for (int p = 0; p < 2; p++){
                for (int c = 0; c < C; c++){
                    float base = dp[idx(j,p,c)];
                    if (base >= INF){
                        continue;
                    }
                    int cc = c;
                    if (f_new_owner){
                        base += owner_cost(owner[j - 1],c);
                        cc = 0;
                    }
                    for (int x = 0; x < 2; x++){
                        if (!(allowed[j] & (1 << x)) || (x == 1 && p == 1)){
                            continue;
                        }
                        if (f_ring && j == nl - 1 && x != wrap){
                            continue;
                        }
                        int nc = std::min(C - 1,cc + x);
                        int to = idx(j + 1,x,nc);
                        if (base < dp[to]){
                            dp[to] = base;
                            from[to] = idx(j,p,c);
                        }
                    }
                }
            }
        }
        for (int p = 0; p < 2; p++){
            for (int c = 0; c < C; c++){
                float total = dp[idx(nl,p,c)];
                if (total >= INF){
                    continue;
                }
                total += owner_cost(owner[nl - 1],c);
                if (total < best_total){
                    best_total = total;
                    best_choice.assign(nl,0);
                    int st = idx(nl,p,c);
                    for (int j = nl; j > 0; j--){
                        best_choice[j - 1] = (uint8_t)((st / C) % 2);
                        st = from[st];
                    }
                }
            }
        }
    }
    for (int j = 0; j < (int)best_choice.size(); j++){
        if (best_choice[j]){
            int a = strip[j];
            int b = strip[(j + 1) % m];
            //Never over a merge already made, by another chain's strip or this one: a partner
            //must always point back.
            if ((L.partner[a] == -1 || L.partner[a] == b) && (L.partner[b] == -1 || L.partner[b] == a)){
                L.partner[a] = b;
                L.partner[b] = a;
            }
        }
    }
}

/*
    Builds both sides' strips for a chain (lattice vertices in order) and pairs each. `ideal` holds
    the quads each chain vertex wants on side 0 and side 1 (ChainIdealQuads). A chain's end vertices
    are not costed: an end on the outline already has the ring's pairing round it.

    At a 120 degree corner the inside is one triangle, T, and the triangle across from it, X,
    touches the chain vertices either side of the corner. Walking the strip gives X, T, X: T goes
    from the strip (it cannot merge - its third edge is a chord between two chain vertices) and X
    stays once, linked on each side to its own fan's next triangle.
*/
void PairAlongChain(Lattice& L, const std::vector<int>& chain, bool f_closed, const std::vector<int> ideal[2]){
    int n = (int)chain.size();
    int first = f_closed ? 0 : 1;
    int last = f_closed ? n - 1 : n - 2;
    if (last < first){
        return;
    }
    for (int side = 0; side < 2; side++){
        std::vector<int> strip;
        std::vector<int> owner;
        std::vector<int> fan_size(n,1);
        for (int k = first; k <= last; k++){
            int v = chain[k];
            int d_out = L.Direction(v,chain[(k + 1) % n]);
            int d_back = L.Direction(v,chain[(k + n - 1) % n]);
            if (d_out < 0 || d_back < 0){
                return;
            }
            //Side 0 is counter-clockwise of the way the chain goes, side 1 clockwise; each fan is
            //listed from the edge the chain came in by.
            std::vector<int> fan;
            if (side == 0){
                for (int sec = d_back + 5; ; sec += 5){
                    fan.push_back(L.Sector(v,sec % 6));
                    if (sec % 6 == d_out){
                        break;
                    }
                }
            }else{
                for (int sec = d_back; ; sec++){
                    fan.push_back(L.Sector(v,sec % 6));
                    if ((sec + 1) % 6 == d_out){
                        break;
                    }
                }
            }
            for (int t : fan){
                if (t < 0){
                    return;     //a fan off the lattice: leave this chain unpaired
                }
            }
            fan_size[k] = (int)fan.size();
            size_t start = 0;
            if (!strip.empty() && strip.back() == fan[0]){
                start = 1;
            }else if (!strip.empty()){
                return;         //fans that do not meet - not a chain a strip can describe
            }
            for (size_t i = start; i < fan.size(); i++){
                if (strip.size() >= 2 && strip[strip.size() - 2] == fan[i]){
                    //X after a corner's T: drop T and its link, and carry on from X.
                    strip.pop_back();
                    owner.pop_back();
                    continue;
                }
                if (!strip.empty()){
                    owner.push_back(k);
                }
                strip.push_back(fan[i]);
            }
        }
        bool f_ring = false;
        if (f_closed && strip.size() > 2 && strip.back() == strip.front()){
            strip.pop_back();
            f_ring = true;
        }
        PairStrip(L,strip,owner,f_ring,fan_size,ideal[side]);
    }
}

/*
    How many quads each chain vertex wants on each side. Two on a straight stretch. At a sharp bend -
    more than 45 degrees between the midpoints of the vertex's two chain edges - one inside and three
    or four outside, the line's angle on each side over 90 degrees.

    And on the outside of a long bend, one more for every 90 degrees it turns in all, even where no
    single vertex turns much. The cells along a chain turn with it; a closed line turns them a
    whole circle, and with two quads a side everywhere the grid outside has to twist round to meet
    them, which shears the ring of cells beside the chain. An extra quad on the outside is a corner
    of the turn - four of them square a shard off as a square's corners would. `ref` is the
    direction the cells were last squared to; it turns exactly 90 degrees per extra quad, and a
    wiggle that comes back never reaches 45 degrees from it, so only a real bend adds any.
*/
void ChainIdealQuads(const Lattice& L, const std::vector<int>& chain, bool f_closed, const GridLine& line,
                     const std::vector<float>& s, std::vector<int> ideal[2]){
    int n = (int)chain.size();
    ideal[0].assign(n,2);
    ideal[1].assign(n,2);
    if (n < 3){
        return;
    }
    float total = s.back();
    std::vector<float> at(n);
    int seg = -1;
    for (int k = 0; k < n; k++){
        at[k] = LineDistanceAt(line,s,L.pos[chain[k]],&seg);
    }
    //Halfway along the line from at[a] to at[b], forward - across the seam on a closed line.
    auto between = [&](float a, float b){
        if (f_closed && b < a){
            b += total;
        }
        return 0.5f * (a + b);
    };
    const float cos45 = 0.70710678f;
    int first = f_closed ? 0 : 1;
    vec2 ref = LineTangent(line,s,between(at[(first + n - 1) % n],at[first]));
    for (int k = first; k < n; k++){
        if (!f_closed && k == n - 1){
            break;
        }
        float from = between(at[(k + n - 1) % n],at[k]);
        float to = between(at[k],at[(k + 1) % n]);
        vec2 a = LineTangent(line,s,from);
        vec2 b = LineTangent(line,s,to);
        float dot = a.dot(b);
        float cross = a.x * b.y - a.y * b.x;
        //Turning counter-clockwise by more than 45 degrees narrows side 0 to under 135: one quad.
        //More than 135 widens the other side past 315: four.
        int quads_ccw = 2;
        int quads_cw = 2;
        if (dot < cos45){
            bool f_very = dot < -cos45;
            if (cross > 0.0f){
                quads_ccw = 1;
                quads_cw = f_very ? 4 : 3;
            }else{
                quads_cw = 1;
                quads_ccw = f_very ? 4 : 3;
            }
        }
        //The sharp bend's extra quads square the cells off by 90 degrees each.
        for (int e = 2; e < quads_cw; e++){
            ref = Rot90(ref);
        }
        for (int e = 2; e < quads_ccw; e++){
            ref = RotM90(ref);
        }
        //The long bend's: the outside of a turn of more than 45 degrees since the cells were last
        //squared off.
        if (ref.dot(b) < cos45){
            if (ref.x * b.y - ref.y * b.x > 0.0f){
                if (quads_cw < 4){
                    quads_cw++;
                    ref = Rot90(ref);
                }
            }else if (quads_ccw < 4){
                quads_ccw++;
                ref = RotM90(ref);
            }
        }
        ideal[0][k] = quads_ccw;
        ideal[1][k] = quads_cw;
    }
}

/*
    Pins one feature's line into the lattice: chooses its chain, commits it, pairs the triangles
    along it, and moves its vertices onto the line.

    The chain is made to pass near points every CHAIN_ANCHOR_SPACING sides along the line, and
    found between them by ChainSegment against just that stretch of the line - so it follows the
    line in order even where the line doubles back close to itself, as a rim's tip does.

    An open line's end on the map's edge takes the outline vertex nearest it, moved exactly to it
    and fixed. A balcony's end on its rim (`end_on_line`, the rim's line, pinned before it) takes the
    nearest vertex of the rim's chain the same way: moved exactly to the end - a point of the rim's
    own line, so it stays on the rim - and fixed, a vertex of both chains. Returns false if the chain
    could not be finished; what was found stays pinned.
*/
bool PinFeature(Lattice& L, const GridLine& line, int line_index, const bool f_end_on_outline[2],
                const int end_on_line[2]){
    bool f_closed = LineClosed(line);
    std::vector<float> s = LineArcLengths(line);
    float total = s.back();
    int segments = std::max(f_closed ? 3 : 1,(int)std::lround(total / (CHAIN_ANCHOR_SPACING * L.T)));

    auto snap = [&](const vec2& p, bool f_outline) -> int {
        int best = -1;
        float best_d2 = 1e30f;
        for (int v = 0; v < (int)L.pos.size(); v++){
            bool f_ok = f_outline ? (L.pin[v] >= 0 && L.pin[v] < 4)
                                  : (L.pin[v] == GRID_PIN_FREE && !L.f_chain[v] && L.near_chain[v] == 0);
            if (!f_ok){
                continue;
            }
            vec2 d = L.pos[v] - p;
            if (d.dot(d) < best_d2){
                best_d2 = d.dot(d);
                best = v;
            }
        }
        return best;
    };
    //The junctions' rim vertices and their neighbours on the rim's chain, and the spoke out of each
    //junction: see ChainSegment.
    std::vector<uint8_t> exempt;
    int junction_spoke[2] = {-1,-1};
    auto snap_end = [&](int end) -> int {
        vec2 p = end == 0 ? line.points.front() : line.points.back();
        if (end_on_line[end] >= 0){
            int best = -1;
            float best_d2 = 1e30f;
            for (int v = 0; v < (int)L.pos.size(); v++){
                if (L.pin[v] != end_on_line[end]){
                    continue;
                }
                vec2 d = L.pos[v] - p;
                if (d.dot(d) < best_d2){
                    best_d2 = d.dot(d);
                    best = v;
                }
            }
            if (best >= 0){
                L.pos[best] = p;
                L.pin[best] = GRID_PIN_FIXED;
                if (exempt.empty()){
                    exempt.assign(L.pos.size(),0);
                }
                //The junction and the rim's vertices up to two steps from it: where the rim bends
                //round the junction, a second one along it can sit beside the way out too.
                exempt[best] = 1;
                for (int d = 0; d < 6; d++){
                    int w = L.Step(best,d);
                    if (w < 0){
                        continue;
                    }
                    for (int dd = -1; dd < 6; dd++){
                        int x = (dd < 0) ? w : L.Step(w,dd);
                        if (x >= 0 && L.f_chain[x] && L.pin[x] == end_on_line[end]){
                            exempt[x] = 1;
                        }
                    }
                }
                /*
                    The way out. The rim's chain was paired along both its sides when it was pinned,
                    and at a vertex of a straight chain both lattice edges into one side can be the
                    diagonals of those quads - which a chain may not run along, so the balcony could
                    not leave the rim into the chasm at all and went round over the plateau instead.
                    The spoke that points the way the balcony's line leaves is freed: the pair of
                    triangles it divides is split, and the balcony's own pairing merges along it.
                */
                size_t n_pts = line.points.size();
                vec2 inward = line.points[end == 0 ? std::min<size_t>(4,n_pts - 1) : n_pts - 1 - std::min<size_t>(4,n_pts - 1)] - p;
                int spoke = -1;
                float best_dot = -2.0f;
                for (int d = 0; d < 6; d++){
                    int w = L.Step(best,d);
                    if (w < 0 || L.f_chain[w] || L.pin[w] != GRID_PIN_FREE){
                        continue;
                    }
                    vec2 e = L.pos[w] - L.pos[best];
                    float dot = e.dot(inward) / std::sqrt(std::max(1e-12f,e.dot(e) * inward.dot(inward)));
                    if (dot > best_dot){
                        best_dot = dot;
                        spoke = w;
                    }
                }
                junction_spoke[end] = spoke;
                if (spoke >= 0 && L.MergedAcross(best,spoke)){
                    int te = L.edge_tri.find(EdgeKey(best,spoke))->second;
                    int t = te / 3;
                    int o = L.neighbour[te];
                    L.partner[t] = -1;
                    L.partner[o] = -1;
                }
            }
            return best;
        }
        if (f_end_on_outline[end]){
            int v = snap(p,true);
            if (v >= 0){
                L.pos[v] = p;
                L.pin[v] = GRID_PIN_FIXED;
            }
            return v;
        }
        return snap(p,false);
    };

    std::vector<int> chain;
    ChainEnd at;
    at.v = snap_end(0);
    if (at.v < 0){
        return false;
    }
    //The far end's junction is found now too, so the whole chain knows both rims' neighbours.
    int far_end = -1;
    if (!f_closed && end_on_line[1] >= 0){
        far_end = snap_end(1);
        if (far_end < 0){
            return false;
        }
    }
    //A junction is already counted by its neighbours, as a vertex of the rim's chain.
    L.Commit(at.v,line_index,exempt.empty() || !exempt[at.v]);
    chain.push_back(at.v);
    float at_s = 0.0f;
    bool f_complete = true;
    ChainSearch search;
    std::vector<int> path;
    for (int k = 1; k <= segments; k++){
        float next_s = total * (float)k / (float)segments;
        bool f_last = k == segments;
        int target;
        int closing_dir = -1;
        if (f_last && f_closed){
            target = chain[0];
            if (chain.size() < 2){
                f_complete = false;
                break;
            }
            closing_dir = L.Direction(chain[0],chain[1]);
        }else if (f_last){
            target = (far_end >= 0) ? far_end : snap_end(1);
        }else{
            target = snap(LinePointAt(line,s,next_s),false);
        }
        if (target < 0 || target == at.v){
            continue;
        }
        ChainEnd next;
        int before = chain.size() >= 2 ? chain[chain.size() - 2] : -1;
        int first_step = (chain.size() == 1) ? junction_spoke[0] : -1;
        int last_step = (f_last && far_end >= 0) ? junction_spoke[1] : -1;
        if (!ChainSegment(L,search,line,s,LinePiece(line,s,at_s,next_s),at,before,target,closing_dir,
                          exempt.empty() ? NULL : &exempt,first_step,last_step,path,next)){
            if (f_last){
                f_complete = false;
            }
            continue;   //try for the next point along instead
        }
        for (int v : path){
            L.chain_edges[EdgeKey(chain.back(),v)] = line_index;
            if (v != chain[0]){
                L.Commit(v,line_index,exempt.empty() || !exempt[v]);
                chain.push_back(v);
            }
        }
        at = next;
        at_s = next_s;
    }

    std::vector<int> ideal[2];
    ChainIdealQuads(L,chain,f_closed && f_complete,line,s,ideal);
    PairAlongChain(L,chain,f_closed && f_complete,ideal);
    /*
        The WEDGE at each junction: the one lattice triangle between the rim's edge out of the
        junction and the balcony's first edge, where the two chains part at 60 degrees. Merged with
        anything, its quad would take the rim vertex beside it through the whole angle there - a
        corner bent flat, which no relaxation recovers (FanCost). Left a triangle, it splits into
        three quads with a corner each, as a leftover from the merge does; the random merge cannot
        take it, since every edge of it touches a chain.
    */
    for (int end = 0; end < 2; end++){
        int junction = (end == 0) ? (chain.empty() ? -1 : chain.front()) : far_end;
        int spoke = junction_spoke[end];
        if (junction < 0 || spoke < 0 || end_on_line[end] < 0){
            continue;
        }
        for (int d = 0; d < 6; d++){
            int r = L.Step(junction,d);
            if (r < 0 || !L.f_chain[r] || L.pin[r] != end_on_line[end] || L.Direction(spoke,r) < 0){
                continue;
            }
            auto it = L.tri_at.find(Lattice::TriKey(junction,spoke,r));
            if (it == L.tri_at.end()){
                continue;
            }
            int t = it->second;
            if (L.partner[t] >= 0){
                L.partner[L.partner[t]] = -1;
            }
            L.partner[t] = -1;
        }
    }
    for (int v : chain){
        if (L.pin[v] == line_index){
            L.pos[v] = NearestOnLine(line,L.pos[v]);
        }
    }
    return f_complete;
}

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
        L.tri_at[Lattice::TriKey(a,b,c)] = (int)tris.size();
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
        THE FEATURES, pinned before the random merge so the merge along them can be chosen (see
        Lattice). The layout comes from the seed, on this rectangle, already smoothed; its lines
        are appended as they are, so the grid pins exactly what the terrain will read.
    */
    layout = GenerateChasmLayout(s.seed,vec2(x0,z0),vec2(x1,z1),T);
    feature_line_base = (int)lines.size();
    L.f_chain.assign(lattice.size(),0);
    L.near_chain.assign(lattice.size(),0);
    num_unfinished_features = 0;
    std::vector<vec2> unpinned = lattice;
    for (const GridFeature& f : layout.features){
        lines.push_back(f.line);
        if (f.line.points.size() < 2){
            continue;
        }
        int end_on_line[2];
        for (int end = 0; end < 2; end++){
            end_on_line[end] = (f.end_on_feature[end] >= 0) ? feature_line_base + f.end_on_feature[end] : -1;
        }
        if (!PinFeature(L,lines.back(),(int)lines.size() - 1,f.f_end_on_outline,end_on_line)){
            num_unfinished_features++;
        }
    }
    /*
        A chain's vertices were moved onto their line, up to half a side, and the lattice beside
        them was not: the row of triangles along the chain took the whole move, squashed on one
        side and stretched on the other. Spread it instead - every free vertex takes the average
        move of its neighbours, repeated, which is the smooth (harmonic) blend between the chains'
        moves and the outline's none. The cells near a chain then move with it.
    */
    if (!layout.features.empty()){
        size_t n = lattice.size();
        std::vector<vec2> move(n,vec2(0.0f,0.0f));
        for (size_t v = 0; v < n; v++){
            move[v] = lattice[v] - unpinned[v];
        }
        std::vector<vec2> next = move;
        for (int pass = 0; pass < 40; pass++){
            for (size_t v = 0; v < n; v++){
                if (lattice_pin[v] != GRID_PIN_FREE){
                    continue;
                }
                vec2 sum(0.0f,0.0f);
                int count = 0;
                for (int d = 0; d < 6; d++){
                    int w = L.Step((int)v,d);
                    if (w >= 0){
                        sum += move[w];
                        count++;
                    }
                }
                next[v] = sum / (float)count;
            }
            move.swap(next);
        }
        for (size_t v = 0; v < n; v++){
            if (lattice_pin[v] == GRID_PIN_FREE){
                lattice[v] = unpinned[v] + move[v];
            }
        }
    }

    for (int t : order){
        if (partner[t] != -1){
            continue;
        }
        //Not across an edge at a chain vertex: those merges were decided along the chain.
        int candidates[3];
        int num = 0;
        for (int k = 0; k < 3; k++){
            int nb = neighbour[t * 3 + k];
            int a = tris[t].v[k];
            int b = tris[t].v[(k + 1) % 3];
            if (nb != -1 && partner[nb] == -1 && !L.f_chain[a] && !L.f_chain[b]){
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
//three re-pinned 2026-10-05 for balconies, the sides made whole and rivers on the home side only.
static const PinnedGridHash pinned_grid_hashes[] = {
    {1,0x86d9266f0a4f6b5full},
    {2,0x1ab301acefd3ee27ull},
    {3,0x191e6b2f9851876bull},
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

    //--- Features: each one's chain whole, on its line, and fixed where it meets the map's edge -----
    {
        bool f_pass = grid.num_unfinished_features == 0;
        std::string detail;
        if (grid.num_unfinished_features){
            snprintf(buf,sizeof(buf),"%i chains could not be finished; ",grid.num_unfinished_features);
            detail += buf;
        }
        size_t nv = grid.fine.pos.size();
        for (int li = grid.feature_line_base; li < (int)grid.lines.size(); li++){
            const GridLine& line = grid.lines[li];
            if (line.points.size() < 2){
                continue;
            }
            bool f_closed = LineClosed(line);
            //On the chain: pinned to the line, or the fixed vertex at one of an open line's ends.
            auto at_end = [&](int v, int end) -> bool {
                vec2 d = grid.fine.pos[v] - (end == 0 ? line.points.front() : line.points.back());
                return d.dot(d) < 1e-6f;
            };
            //A fixed vertex on the line is a junction, where a balcony's end meets this rim.
            auto on_line = [&](int v) -> bool {
                vec2 d = NearestOnLine(line,grid.fine.pos[v]) - grid.fine.pos[v];
                return d.dot(d) < 1e-6f;
            };
            auto member = [&](int v) -> bool {
                if (grid.fine.pin[v] == li){
                    return true;
                }
                if (grid.fine.pin[v] != GRID_PIN_FIXED){
                    return false;
                }
                return (!f_closed && (at_end(v,0) || at_end(v,1))) || on_line(v);
            };
            std::vector<int> degree(nv,0);
            std::vector<int> root(nv);
            for (size_t i = 0; i < nv; i++){
                root[i] = (int)i;
            }
            std::function<int(int)> find = [&](int v){ return root[v] == v ? v : (root[v] = find(root[v])); };
            for (const std::pair<int,int>& e : grid.fine.edges){
                if (member(e.first) && member(e.second)){
                    degree[e.first]++;
                    degree[e.second]++;
                    root[find(e.first)] = find(e.second);
                }
            }
            int vertices = 0;
            int components = 0;
            int ends = 0;
            int branches = 0;
            int off = 0;
            std::vector<int> end_vertices;
            std::vector<int> piece_roots;
            for (size_t i = 0; i < nv; i++){
                if (!member((int)i)){
                    continue;
                }
                vertices++;
                if (find((int)i) == (int)i){
                    components++;
                    piece_roots.push_back((int)i);
                }
                if (degree[i] <= 1){
                    ends++;
                    end_vertices.push_back((int)i);
                }
                if (degree[i] > 2){
                    branches++;
                    add_issue("feature",(int)i,grid.fine.pos[i],true);
                }
                vec2 d = NearestOnLine(line,grid.fine.pos[i]) - grid.fine.pos[i];
                if (d.dot(d) > 1e-6f){
                    off++;
                    add_issue("feature",(int)i,grid.fine.pos[i],true);
                }
            }
            //An end on the map's edge, or a balcony's on its rim, must be a fixed vertex exactly there.
            int loose_ends = 0;
            if (!f_closed){
                for (int end = 0; end < 2; end++){
                    vec2 p = end == 0 ? line.points.front() : line.points.back();
                    bool f_on_edge = grid.LineEndsOn(li,end) >= 0;
                    for (int o = 0; o < grid.feature_line_base; o++){
                        vec2 d = NearestOnLine(grid.lines[o],p) - p;
                        if (d.dot(d) < 1e-6f){
                            f_on_edge = true;
                        }
                    }
                    if (!f_on_edge){
                        continue;
                    }
                    bool f_fixed = false;
                    for (size_t i = 0; i < grid.coarse.pos.size(); i++){
                        if (grid.fine.pin[i] == GRID_PIN_FIXED && at_end((int)i,end) && degree[i] == 1){
                            f_fixed = true;
                        }
                    }
                    if (!f_fixed){
                        loose_ends++;
                        add_issue("feature",-1,p,true);
                    }
                }
            }
            bool f_whole = components == 1 && branches == 0 && (f_closed ? ends == 0 : ends == 2);
            if (!f_whole){
                //Where it breaks: every loose end, and a vertex of every piece.
                for (int v : end_vertices){
                    add_issue("feature",v,grid.fine.pos[v],true);
                }
                if (components > 1){
                    for (int v : piece_roots){
                        add_issue("feature",v,grid.fine.pos[v],true);
                    }
                }
            }

            //The quads touching the chain, and how square they are.
            GridChainFigures fig;
            fig.line = li;
            fig.vertices = vertices;
            fig.squareness_min = 1.0f;
            double sum = 0.0;
            std::vector<std::pair<float,int>> worst;
            for (int qi = 0; qi < (int)grid.fine.quads.size(); qi++){
                const GridQuad& q = grid.fine.quads[qi];
                bool f_touches = false;
                vec2 p[4];
                for (int k = 0; k < 4; k++){
                    p[k] = grid.fine.pos[q.v[k]];
                    if (member(q.v[k])){
                        f_touches = true;
                    }
                }
                if (!f_touches){
                    continue;
                }
                float sq = GridQuadSquareness(p);
                sum += sq;
                fig.quads++;
                fig.squareness_min = std::min(fig.squareness_min,sq);
                worst.push_back(std::make_pair(sq,qi));
            }
            fig.squareness_mean = fig.quads ? (float)(sum / fig.quads) : 0.0f;
            report.chains.push_back(fig);
            int n = std::min((int)worst.size(),5);
            std::partial_sort(worst.begin(),worst.begin() + n,worst.end());
            for (int i = 0; i < n; i++){
                add_issue("feature_squareness",worst[i].second,grid.FineQuadCentre(worst[i].second),false);
            }

            if (!f_whole || off || loose_ends){
                f_pass = false;
            }
            snprintf(buf,sizeof(buf),"line %i: %i vertices, %s%s, %i off the line, %i loose ends; quads %i, mean %.3f, worst %.3f. ",
                     li,vertices,f_closed ? "closed" : "open",f_whole ? "" : " NOT WHOLE",off,loose_ends,
                     fig.quads,fig.squareness_mean,fig.squareness_min);
            detail += buf;
            if (!f_whole){
                snprintf(buf,sizeof(buf),"(%i pieces, %i ends, %i branches) ",components,ends,branches);
                detail += buf;
            }
        }
        if (detail.empty()){
            detail = "no features";
        }
        add_result("features",f_pass,false,detail);
    }

    /*
        --- Spacing: no two feature lines closer than GRID_FEATURE_SPACING ------------------------
        The generator keeps this as it places each line; this is the proof, on the lines the grid
        actually pinned. Closer than that and two chains can share a lattice triangle, or a fine
        cell can see three levels. A line against the map's edge is not counted near its own ends
        there, which sit on it by design - and a balcony against its own rim not within
        GRID_JUNCTION_REACH of its ends, which meet the rim by design, and held to
        GRID_BALCONY_RIM_SPACING beyond them.
    */
    {
        const float want = GRID_FEATURE_SPACING * grid.settings.triangle_side;
        const float want_balcony = GRID_BALCONY_RIM_SPACING * grid.settings.triangle_side;
        const float skip = 2.0f * want;
        float closest = 1e30f;
        vec2 closest_at(0.0f,0.0f);
        std::string closest_what = "nothing";
        int bad = 0;
        int base = grid.feature_line_base;
        float closest_balcony = 1e30f;
        auto consider = [&](float gap, const vec2& at, const std::string& what, float limit){
            if (gap < limit){
                bad++;
                add_issue("spacing",-1,at,true);
            }
            if (gap < closest){
                closest = gap;
                closest_at = at;
                closest_what = what;
            }
        };
        for (int a = base; a < (int)grid.lines.size(); a++){
            const GridLine& la = grid.lines[a];
            if (la.points.size() < 2){
                continue;
            }
            for (int b = a; b < (int)grid.lines.size(); b++){
                vec2 at;
                float gap;
                //A balcony and the rim it ends on: the balcony's points away from its ends only.
                int balcony = -1;
                int rim = -1;
                if (grid.LineEndsOn(a,0) == b || grid.LineEndsOn(a,1) == b){
                    balcony = a;
                    rim = b;
                }else if (grid.LineEndsOn(b,0) == a || grid.LineEndsOn(b,1) == a){
                    balcony = b;
                    rim = a;
                }
                if (balcony >= 0){
                    const GridLine& lb = grid.lines[balcony];
                    std::vector<float> sb = LineArcLengths(lb);
                    float reach = GRID_JUNCTION_REACH * grid.settings.triangle_side;
                    gap = 1e30f;
                    for (size_t k = 0; k < lb.points.size(); k++){
                        if (sb[k] < reach || sb.back() - sb[k] < reach){
                            continue;
                        }
                        vec2 d = NearestOnLine(grid.lines[rim],lb.points[k]) - lb.points[k];
                        float g = std::sqrt(d.dot(d));
                        if (g < gap){
                            gap = g;
                            at = lb.points[k];
                        }
                    }
                }else{
                    gap = GridLineGap(la,grid.lines[b],skip,&at);
                }
                snprintf(buf,sizeof(buf),"lines %i and %i",a,b);
                if (balcony >= 0){
                    closest_balcony = std::min(closest_balcony,gap);
                }
                consider(gap,at,buf,balcony >= 0 ? want_balcony : want);
            }
            //The map's edge, away from an open line's ends.
            std::vector<float> s = LineArcLengths(la);
            bool f_closed = LineClosed(la);
            bool f_balcony = grid.LineEndsOn(a,0) >= 0 || grid.LineEndsOn(a,1) >= 0;
            for (int o = 0; o < base; o++){
                for (size_t k = 0; k < la.points.size(); k++){
                    if (!f_closed && !f_balcony && (s[k] < skip || s.back() - s[k] < skip)){
                        continue;
                    }
                    vec2 d = NearestOnLine(grid.lines[o],la.points[k]) - la.points[k];
                    snprintf(buf,sizeof(buf),"line %i and the map's edge",a);
                    consider(std::sqrt(d.dot(d)),la.points[k],buf,want);
                }
            }
        }
        snprintf(buf,sizeof(buf),"closest %.1f (%s), at least %.1f wanted (%.1f a balcony from its rim, closest %.1f); "
                 "%i places closer",closest,closest_what.c_str(),want,want_balcony,
                 closest_balcony < 1e29f ? closest_balcony : 0.0f,bad);
        add_result("spacing",bad == 0,false,buf);
        //The closest place is marked even when it passes - worth looking at, not wrong.
        if (bad == 0 && closest < 1e29f){
            add_issue("spacing",-1,closest_at,false);
        }
    }

    //--- Layout: what the seed made of the chasm - a figure, and a failure only if no rift fitted --
    {
        const ChasmLayout& l = grid.layout;
        snprintf(buf,sizeof(buf),"%i rifts, %i forks; balconies %i east, %i west; ledges %i of %i, shards %i of %i, "
                 "columns %i of %i; rivers %i of %i; %i attempts%s (%.1f ms)",
                 l.rifts,l.forks,l.balconies[1],l.balconies[0],l.count[GRID_FEATURE_LEDGE],l.wanted[GRID_FEATURE_LEDGE],
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
