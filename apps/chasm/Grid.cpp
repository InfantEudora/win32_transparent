#include "Grid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
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

vec2 NearestOnLine(const GridLine& line, const vec2& p){
    vec2 best = line.points[0];
    float best_d2 = (p - best).dot(p - best);
    for (size_t k = 0; k + 1 < line.points.size(); k++){
        vec2 a = line.points[k];
        vec2 d = line.points[k + 1] - a;
        float len2 = d.dot(d);
        float t = len2 > 0.0f ? std::max(0.0f,std::min(1.0f,(p - a).dot(d) / len2)) : 0.0f;
        vec2 q = a + d * t;
        float d2 = (p - q).dot(p - q);
        if (d2 < best_d2){
            best_d2 = d2;
            best = q;
        }
    }
    return best;
}

//Whether vertex v is on line `l`: pinned to it, or a fixed vertex lying on it - a corner is on
//both its edges, and is pinned to neither so that it slides along neither.
bool OnLine(const std::vector<vec2>& pos, const std::vector<int>& pin, const std::vector<GridLine>& lines,
            int v, int l, float tolerance){
    if (pin[v] == l){
        return true;
    }
    if (pin[v] != GRID_PIN_FIXED){
        return false;
    }
    vec2 d = NearestOnLine(lines[l],pos[v]) - pos[v];
    return d.dot(d) <= tolerance * tolerance;
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

    Pins carry over the same way: old vertices keep theirs, centroids are free, and an edge's
    midpoint is pinned to a line when the edge runs along it. That is decided by both ends being
    on the line and the midpoint lying near it - exactly on it for a straight line, within a
    quarter of the edge for a curve, which keeps a chord cutting across a bend from being pinned.
*/
void Subdivide(const std::vector<vec2>& in_pos, const std::vector<int>& in_pin, const std::vector<Poly>& polys,
               const std::vector<GridLine>& lines,
               std::vector<vec2>& out_pos, std::vector<int>& out_pin, std::vector<GridQuad>& out_quads){
    out_pos = in_pos;
    out_pin = in_pin;
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
                int candidates[2] = {in_pin[a],in_pin[b]};
                for (int l : candidates){
                    if (l < 0 || pin != GRID_PIN_FREE){
                        continue;
                    }
                    float len = std::sqrt((in_pos[b] - in_pos[a]).dot(in_pos[b] - in_pos[a]));
                    vec2 on = NearestOnLine(lines[l],mid);
                    if (OnLine(in_pos,in_pin,lines,a,l,1e-4f * len) && OnLine(in_pos,in_pin,lines,b,l,1e-4f * len) &&
                        (on - mid).dot(on - mid) <= 0.0625f * len * len){
                        pin = l;
                        mid = on;
                    }
                }
                m[k] = (int)out_pos.size();
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
                level.pos[i] = NearestOnLine(lines[level.pin[i]],level.pos[i]);
            }
        }
    }
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

    std::vector<vec2> lattice;
    std::vector<int> lattice_pin;
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

    std::vector<Poly> tris;
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
    std::vector<int> neighbour((size_t)tris.size() * 3,-1);
    {
        std::unordered_map<uint64_t,int> first;
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
    std::vector<int> partner(tris.size(),-1);
    for (int j = 0; j < rows; j++){
        for (int i = 0; i < cols; i++){
            if (i == 0 || i == cols - 1 || j == 0 || j == rows - 1){
                int a = (j * cols + i) * 2;
                partner[a] = a + 1;
                partner[a + 1] = a;
            }
        }
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
    Subdivide(lattice,lattice_pin,polys,lines,coarse.pos,coarse.pin,coarse.quads);
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
    Subdivide(coarse.pos,coarse.pin,coarse_polys,lines,fine.pos,fine.pin,fine.quads);
    Analyse(fine);
    Relax(fine,lines,s.relax_passes_fine,s.relax_strength);

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
//Pinned 2026-10-04 after the outline was straightened, the same in the debug and release builds.
static const PinnedGridHash pinned_grid_hashes[] = {
    {1,0x5148d67a62749b31ull},
    {2,0x060d3bc14563b087ull},
    {3,0xb0478b64d3ec39abull},
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
        snprintf(buf,sizeof(buf),"mean %.3f, worst %.3f (the %i worst are marked)",
                 report.squareness_mean,report.squareness_min,n);
        add_result("squareness",true,false,buf);
    }

    report.check_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    return report;
}
#endif
