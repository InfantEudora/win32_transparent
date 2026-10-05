#include "Grid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include "RRandom.h"

/*
    THE CHASM'S LAYOUT FROM THE SEED - grid_plan.md, "Step 10, generated chasm", for the method and
    the reasons. In short: rifts are spines (polylines with a half-width) walked from the map's edge;
    they become a distance field, and the rims are its zero contours, so a fork or a second rift is
    just more contour. Shards, columns and terraces are closed contours of their own small fields,
    and rivers are cheapest paths over the plateau to falls chosen on the rims.

    Everything is kept at least GRID_FEATURE_SPACING sides from everything else, and the rifts are
    redrawn until they are. No trigonometry anywhere: a turn is a nudge along the perpendicular,
    renormalised, so the results rest only on + - * / and sqrt, which give the same bits in every
    build (Grid.cpp's pinned hashes are checked in debug and release alike).
*/

namespace {

//--- Noise -----------------------------------------------------------------------------------------

uint32_t HashInts(int x, int z, uint32_t seed){
    uint32_t h = seed * 0x9E3779B1u ^ (uint32_t)x * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

//Smooth value noise, -1..1, varying over about `wavelength`.
float Noise(const vec2& p, float wavelength, uint32_t seed){
    float fx = p.x / wavelength;
    float fz = p.y / wavelength;
    float ix = std::floor(fx);
    float iz = std::floor(fz);
    float tx = fx - ix;
    float tz = fz - iz;
    tx = tx * tx * (3.0f - 2.0f * tx);
    tz = tz * tz * (3.0f - 2.0f * tz);
    int x = (int)ix;
    int z = (int)iz;
    auto v = [seed](int a, int b){ return (float)(HashInts(a,b,seed) & 0xFFFFu) / 32767.5f - 1.0f; };
    float a = v(x,z) + (v(x + 1,z) - v(x,z)) * tx;
    float b = v(x,z + 1) + (v(x + 1,z + 1) - v(x,z + 1)) * tx;
    return a + (b - a) * tz;
}

vec2 Perp(const vec2& d){
    return vec2(-d.y,d.x);
}

vec2 Unit(const vec2& d){
    float len = std::sqrt(d.dot(d));
    return len > 1e-12f ? d / len : vec2(1.0f,0.0f);
}

//Polynomial smooth minimum: the two fields joined by a fillet about `k` wide.
float SmoothMin(float a, float b, float k){
    float h = std::max(k - std::fabs(a - b),0.0f) / k;
    return std::min(a,b) - h * h * k * 0.25f;
}

float SmoothMax(float a, float b, float k){
    return -SmoothMin(-a,-b,k);
}

//--- A field on a raster ---------------------------------------------------------------------------

/*
    A scalar on a raster whose outer samples lie EXACTLY on lo and hi, so a contour that runs off
    the raster ends exactly on its edge - which for the rifts is the map's edge, where an open rim
    has to end.
*/
struct Field{
    vec2 lo;
    vec2 hi;
    int w = 0;
    int h = 0;
    float cx = 1.0f;
    float cz = 1.0f;
    std::vector<float> d;

    void Init(const vec2& lo_, const vec2& hi_, float cell, float value){
        lo = lo_;
        hi = hi_;
        w = std::max(2,(int)std::lround((hi.x - lo.x) / cell) + 1);
        h = std::max(2,(int)std::lround((hi.y - lo.y) / cell) + 1);
        cx = (hi.x - lo.x) / (float)(w - 1);
        cz = (hi.y - lo.y) / (float)(h - 1);
        d.assign((size_t)w * h,value);
    }
    vec2 Pos(int i, int j) const{
        return vec2(i == w - 1 ? hi.x : lo.x + i * cx,j == h - 1 ? hi.y : lo.y + j * cz);
    }
    float& At(int i, int j){
        return d[(size_t)j * w + i];
    }
    float At(int i, int j) const{
        return d[(size_t)j * w + i];
    }
    float Sample(const vec2& p) const{
        float fx = std::max(0.0f,std::min((float)(w - 1) - 0.001f,(p.x - lo.x) / cx));
        float fz = std::max(0.0f,std::min((float)(h - 1) - 0.001f,(p.y - lo.y) / cz));
        int ix = (int)fx;
        int iz = (int)fz;
        float tx = fx - ix;
        float tz = fz - iz;
        float a = At(ix,iz) + (At(ix + 1,iz) - At(ix,iz)) * tx;
        float b = At(ix,iz + 1) + (At(ix + 1,iz + 1) - At(ix,iz + 1)) * tx;
        return a + (b - a) * tz;
    }
    vec2 Gradient(const vec2& p) const{
        return vec2((Sample(p + vec2(cx,0.0f)) - Sample(p - vec2(cx,0.0f))) / (2.0f * cx),
                    (Sample(p + vec2(0.0f,cz)) - Sample(p - vec2(0.0f,cz))) / (2.0f * cz));
    }
};

//--- Spines ----------------------------------------------------------------------------------------

struct Spine{
    std::vector<vec2> p;
    std::vector<float> r;   //half-width at each point
};

//Distance to the tube round the spine (negative inside), into `f` by minimum, within `pad` of it.
void StampSpine(Field& f, const Spine& s, float pad){
    for (size_t k = 0; k + 1 < s.p.size(); k++){
        vec2 a = s.p[k];
        vec2 b = s.p[k + 1];
        float ra = s.r[k];
        float rb = s.r[k + 1];
        float reach = std::max(ra,rb) + pad;
        int i0 = std::max(0,(int)std::floor((std::min(a.x,b.x) - reach - f.lo.x) / f.cx));
        int i1 = std::min(f.w - 1,(int)std::ceil((std::max(a.x,b.x) + reach - f.lo.x) / f.cx));
        int j0 = std::max(0,(int)std::floor((std::min(a.y,b.y) - reach - f.lo.y) / f.cz));
        int j1 = std::min(f.h - 1,(int)std::ceil((std::max(a.y,b.y) + reach - f.lo.y) / f.cz));
        vec2 ab = b - a;
        float len2 = ab.dot(ab);
        for (int j = j0; j <= j1; j++){
            for (int i = i0; i <= i1; i++){
                vec2 p = f.Pos(i,j);
                float t = len2 > 0.0f ? std::max(0.0f,std::min(1.0f,(p - a).dot(ab) / len2)) : 0.0f;
                vec2 e = p - (a + ab * t);
                float dist = std::sqrt(e.dot(e)) - (ra + (rb - ra) * t);
                float& cell = f.At(i,j);
                cell = std::min(cell,dist);
            }
        }
    }
}

struct WalkRules{
    vec2 keep;              //the heading may not turn further than min_dot from this
    float min_dot = 0.766f; //cos 40 degrees
    vec2 box_lo;            //the walk steers back inside this box, and stops if it leaves anyway
    vec2 box_hi;
    float stop_z = -1e30f;  //stops on reaching z <= this (walking north)
    float length = 0.0f;
    int straight_steps = 0; //steps on its first heading before it may turn - a mouth meets its edge square
};

/*
    A spine's points: steps of `step` from `start`, the heading turning on a curvature that drifts,
    so bends come in runs rather than as a zig-zag.
*/
std::vector<vec2> Walk(RRandom& rng, const vec2& start, const vec2& heading, const WalkRules& w, float step){
    std::vector<vec2> pts(1,start);
    vec2 h = Unit(heading);
    float curve = 0.0f;
    float walked = 0.0f;
    int n = 0;
    while (walked < w.length){
        if (n >= w.straight_steps){
            curve = curve * 0.75f + rng.GetFloat(-0.12f,0.12f);
            h = Unit(h + Perp(h) * curve);
        }
        n++;
        vec2 p = pts.back();
        //Back toward the box: a nudge per step, not a wall.
        const float margin = 30.0f;
        if (p.x < w.box_lo.x + margin) h = Unit(h + vec2(0.35f,0.0f));
        if (p.x > w.box_hi.x - margin) h = Unit(h + vec2(-0.35f,0.0f));
        if (p.y < w.box_lo.y + margin) h = Unit(h + vec2(0.0f,0.35f));
        if (p.y > w.box_hi.y - margin && n > w.straight_steps) h = Unit(h + vec2(0.0f,-0.35f));
        for (int k = 0; k < 8 && h.dot(w.keep) < w.min_dot; k++){
            h = Unit(h + w.keep * 0.35f);
            curve *= 0.5f;
        }
        p = p + h * step;
        pts.push_back(p);
        walked += step;
        if (p.y <= w.stop_z){
            break;
        }
        bool f_out = p.x < w.box_lo.x || p.x > w.box_hi.x || p.y < w.box_lo.y || p.y > w.box_hi.y;
        if (f_out && n > w.straight_steps){
            break;
        }
    }
    return pts;
}

/*
    Half-widths along a spine: `base` wandering on noise, narrowing over the last stretch to a
    round tip of `tip`.
*/
void Widths(Spine& s, float base, float tip, uint32_t noise_seed){
    std::vector<float> arc(s.p.size(),0.0f);
    for (size_t k = 1; k < s.p.size(); k++){
        vec2 d = s.p[k] - s.p[k - 1];
        arc[k] = arc[k - 1] + std::sqrt(d.dot(d));
    }
    float total = std::max(arc.back(),1.0f);
    s.r.resize(s.p.size());
    for (size_t k = 0; k < s.p.size(); k++){
        float r = base * (1.0f + 0.28f * Noise(vec2(arc[k],0.0f),110.0f,noise_seed));
        float u = arc[k] / total;
        if (u > 0.7f){
            float t = (u - 0.7f) / 0.3f;
            t = t * t * (3.0f - 2.0f * t);
            r = r + (tip - r) * t;
        }
        s.r[k] = r;
    }
}

//--- Contours --------------------------------------------------------------------------------------

struct Contour{
    std::vector<vec2> p;
    bool f_closed = false;
};

/*
    The zero contour of `f` by marching squares, inside where the field is negative. Each crossing
    is a point on a raster edge, and the segments link through the edges they share: an edge on the
    raster's outline belongs to one cell only, which is where an open contour starts and ends.
    Saddles are split by the cell's centre value. Edges are visited in index order, so the result
    depends on nothing but the field.
*/
std::vector<Contour> Contours(const Field& f){
    const int w = f.w;
    const int h = f.h;
    const int num_h = h * (w - 1);              //(i,j)-(i+1,j)
    const int num_edges = num_h + (h - 1) * w;  //then (i,j)-(i,j+1)
    auto h_edge = [w](int i, int j){ return j * (w - 1) + i; };
    auto v_edge = [w,num_h](int i, int j){ return num_h + j * w + i; };
    auto inside = [&f](int i, int j){ return f.At(i,j) < 0.0f; };
    auto point = [&](int e) -> vec2 {
        int i0, j0, i1, j1;
        if (e < num_h){
            i0 = e % (w - 1);
            j0 = e / (w - 1);
            i1 = i0 + 1;
            j1 = j0;
        }else{
            i0 = (e - num_h) % w;
            j0 = (e - num_h) / w;
            i1 = i0;
            j1 = j0 + 1;
        }
        float a = f.At(i0,j0);
        float b = f.At(i1,j1);
        float t = a / (a - b);
        vec2 pa = f.Pos(i0,j0);
        vec2 pb = f.Pos(i1,j1);
        return pa + (pb - pa) * t;
    };
    std::vector<int> nb((size_t)num_edges * 2,-1);
    auto link = [&nb](int a, int b){
        (nb[a * 2] < 0 ? nb[a * 2] : nb[a * 2 + 1]) = b;
        (nb[b * 2] < 0 ? nb[b * 2] : nb[b * 2 + 1]) = a;
    };
    for (int j = 0; j + 1 < h; j++){
        for (int i = 0; i + 1 < w; i++){
            bool c[4] = {inside(i,j),inside(i + 1,j),inside(i + 1,j + 1),inside(i,j + 1)};
            int e[4] = {h_edge(i,j),v_edge(i + 1,j),h_edge(i,j + 1),v_edge(i,j)};
            //Edge k joins corners k and k+1 (e2 is c3-c2 and e3 is c0-c3, the same pairs).
            bool cut[4] = {c[0] != c[1],c[1] != c[2],c[2] != c[3],c[3] != c[0]};
            int num = (int)cut[0] + (int)cut[1] + (int)cut[2] + (int)cut[3];
            if (num == 2){
                int a = -1;
                int b = -1;
                for (int k = 0; k < 4; k++){
                    if (cut[k]){
                        (a < 0 ? a : b) = e[k];
                    }
                }
                link(a,b);
            }else if (num == 4){
                float centre = (f.At(i,j) + f.At(i + 1,j) + f.At(i + 1,j + 1) + f.At(i,j + 1)) * 0.25f;
                //Cut off the two corners that are NOT joined through the centre.
                bool f_join_inside = centre < 0.0f;
                bool f_cut_odd = (c[0] == f_join_inside);  //corners 0 and 2 joined: cut off 1 and 3
                if (f_cut_odd){
                    link(e[0],e[1]);    //corner 1
                    link(e[2],e[3]);    //corner 3
                }else{
                    link(e[3],e[0]);    //corner 0
                    link(e[1],e[2]);    //corner 2
                }
            }
        }
    }
    std::vector<Contour> out;
    std::vector<uint8_t> seen(num_edges,0);
    auto walk = [&](int start, bool f_closed){
        Contour c;
        c.f_closed = f_closed;
        int prev = -1;
        int at = start;
        while (at >= 0 && !seen[at]){
            seen[at] = 1;
            c.p.push_back(point(at));
            //On along whichever neighbour it did not come from; an open contour's last edge has
            //only that one, so the walk ends there.
            int next = nb[at * 2] != prev ? nb[at * 2] : nb[at * 2 + 1];
            prev = at;
            at = next;
        }
        if (f_closed && !c.p.empty()){
            c.p.push_back(c.p.front());
        }
        out.push_back(c);
    };
    for (int e = 0; e < num_edges; e++){
        if (!seen[e] && nb[e * 2] >= 0 && nb[e * 2 + 1] < 0){
            walk(e,false);
        }
    }
    for (int e = 0; e < num_edges; e++){
        if (!seen[e] && nb[e * 2 + 1] >= 0){
            walk(e,true);
        }
    }
    return out;
}

float Length(const std::vector<vec2>& p){
    float s = 0.0f;
    for (size_t k = 0; k + 1 < p.size(); k++){
        vec2 d = p[k + 1] - p[k];
        s += std::sqrt(d.dot(d));
    }
    return s;
}

/*
    A contour as a feature line: resampled to corners about `corner` apart - which irons out the
    raster's steps - then smoothed exactly as the grid would smooth a hand-drawn line.
*/
GridLine ContourLine(const Contour& c, float corner, float spacing){
    std::vector<float> s(c.p.size(),0.0f);
    for (size_t k = 1; k < c.p.size(); k++){
        vec2 d = c.p[k] - c.p[k - 1];
        s[k] = s[k - 1] + std::sqrt(d.dot(d));
    }
    float total = s.back();
    int n = std::max(c.f_closed ? 6 : 2,(int)std::lround(total / corner));
    std::vector<vec2> corners;
    size_t k = 1;
    for (int i = 0; i < n; i++){
        float at = total * (float)i / (float)n;
        while (k + 1 < s.size() && s[k] < at){
            k++;
        }
        float span = s[k] - s[k - 1];
        float t = span > 0.0f ? (at - s[k - 1]) / span : 0.0f;
        corners.push_back(c.p[k - 1] + (c.p[k] - c.p[k - 1]) * t);
    }
    corners.push_back(c.f_closed ? corners.front() : c.p.back());
    if (!c.f_closed){
        corners.front() = c.p.front();
    }
    return GridSmoothLine(corners,c.f_closed,spacing);
}

float SegmentDistance(const vec2& p, const vec2& a, const vec2& b){
    vec2 d = b - a;
    float len2 = d.dot(d);
    float t = len2 > 0.0f ? std::max(0.0f,std::min(1.0f,(p - a).dot(d) / len2)) : 0.0f;
    vec2 e = p - (a + d * t);
    return std::sqrt(e.dot(e));
}

float PointLineDistance(const vec2& p, const GridLine& line){
    float best = 1e30f;
    for (size_t k = 0; k + 1 < line.points.size(); k++){
        best = std::min(best,SegmentDistance(p,line.points[k],line.points[k + 1]));
    }
    return best;
}

//Even-odd, for a closed line.
bool PointInside(const vec2& p, const GridLine& line){
    bool f_in = false;
    const std::vector<vec2>& q = line.points;
    for (size_t i = 0, j = q.size() - 1; i < q.size(); j = i++){
        if ((q[i].y > p.y) != (q[j].y > p.y)){
            float x = q[j].x + (p.y - q[j].y) * (q[i].x - q[j].x) / (q[i].y - q[j].y);
            if (p.x < x){
                f_in = !f_in;
            }
        }
    }
    return f_in;
}

//--- The layout ------------------------------------------------------------------------------------

struct Builder{
    RRandom& rng;
    vec2 lo;
    vec2 hi;
    float T;
    float spacing;      //GRID_FEATURE_SPACING in world units
    ChasmLayout& out;
    Field rift;         //the rifts' field, once one attempt has kept its spacing
    vec2 main_tip;
    bool f_terraced = false;    //the seed wants terraces, which need a wide rift

    Builder(RRandom& r, const vec2& l, const vec2& h, float side, ChasmLayout& o)
        : rng(r), lo(l), hi(h), T(side), spacing(GRID_FEATURE_SPACING * side), out(o){}

    uint32_t Seed(){
        return (uint32_t)rng.GetInt(0,0x3FFFFFFF);
    }

    //Distance from p to the map's edge.
    float EdgeDistance(const vec2& p) const{
        return std::min(std::min(p.x - lo.x,hi.x - p.x),std::min(p.y - lo.y,hi.y - p.y));
    }

    /*
        Whether a new line keeps its distance from itself, from every line placed so far and from
        the map's edge (away from its own ends, for an open line ending there).
    */
    bool Fits(const GridLine& line, const std::vector<GridFeature>& placed) const{
        if (line.points.size() < 4){
            return false;
        }
        bool f_closed = line.points.front().x == line.points.back().x && line.points.front().y == line.points.back().y;
        float total = Length(line.points);
        /*
            Across itself. Nearby points along the line are always close, so pairs nearer than two
            spacings along it are skipped - but on a small closed line every pair is that near, and
            nothing would be tested at all. So a closed line skips at most 0.4 of its length - on a
            circle that is still 1.9 radii across, so a column passes on its diameter - and must be
            long enough to go round one.
        */
        float skip = 2.0f * spacing;
        if (f_closed){
            if (total < 3.0f * spacing){
                return false;
            }
            skip = std::min(skip,0.4f * total);
        }
        if (!RoundEnough(line,f_closed)){
            return false;
        }
        if (GridLineGap(line,line,skip,NULL) < spacing){
            return false;
        }
        for (const GridFeature& f : placed){
            if (GridLineGap(line,f.line,0.0f,NULL) < spacing){
                return false;
            }
            /*
                Far enough apart is not enough: a shard inside a shard keeps its distance and turns
                the levels inside out, since the terrain counts closed lines by parity. Nothing
                closed may stand inside another closed line, either way round.
            */
            if (f_closed && f.kind != GRID_FEATURE_RIM &&
                (PointInside(line.points[0],f.line) || PointInside(f.line.points[0],line))){
                return false;
            }
        }
        float arc = 0.0f;
        for (size_t k = 0; k < line.points.size(); k++){
            if (k > 0){
                vec2 d = line.points[k] - line.points[k - 1];
                arc += std::sqrt(d.dot(d));
            }
            if (!f_closed && (arc < 2.0f * spacing || total - arc < 2.0f * spacing)){
                continue;
            }
            if (EdgeDistance(line.points[k]) < spacing){
                return false;
            }
        }
        return true;
    }

    /*
        No bend tighter than the lattice can follow: the line may turn at most about 80 degrees
        within two sides of its length, a radius of about 1.4 sides. A rift's tip is a radius of
        1.4-1.9 sides and passes; a terrace end cut square by its ball did not, and the cells left
        outside its chain folded.
    */
    bool RoundEnough(const GridLine& line, bool f_closed) const{
        const std::vector<vec2>& p = line.points;
        size_t n = p.size();
        //Points are a quarter side apart, so four of them either way is a side.
        const int reach = 4;
        for (size_t k = 0; k < n; k++){
            long a = (long)k - reach;
            long b = (long)k + reach;
            if (f_closed){
                long m = (long)n - 1;   //the last point repeats the first
                a = ((a % m) + m) % m;
                b = b % m;
            }else if (a < 0 || b + 1 >= (long)n){
                continue;
            }
            vec2 t0 = Unit(p[a + 1] - p[a]);
            vec2 t1 = Unit(p[b + 1] - p[b]);
            if (t0.dot(t1) < 0.17f){
                return false;
            }
        }
        return true;
    }

    //--- Rifts --------------------------------------------------------------------------------
    /*
        One attempt at the rifts: the main one, its forks, perhaps a second. Fills `rims` and the
        field, and says whether they keep their spacing and leave plateau enough on both sides.
    */
    bool TryRifts(std::vector<GridFeature>& rims){
        const float W = hi.x - lo.x;
        const float D = hi.y - lo.y;
        const float step = 3.0f * T;
        const vec2 north(0.0f,-1.0f);
        out.rifts = 0;
        out.forks = 0;

        //The main rift: in from the middle of the south edge, north to a tip short of the north edge.
        Spine main;
        float mouth_x = lo.x + W * rng.GetFloat(0.3f,0.7f);
        WalkRules rules;
        rules.keep = north;
        rules.min_dot = 0.766f;
        rules.box_lo = vec2(lo.x + 0.18f * W,lo.y);
        rules.box_hi = vec2(hi.x - 0.18f * W,hi.y + 2.0f * step);
        rules.stop_z = lo.y + D * rng.GetFloat(0.12f,0.32f);
        rules.length = 4.0f * D;
        rules.straight_steps = 2;
        main.p = Walk(rng,vec2(mouth_x,hi.y + 16.0f),north,rules,step);
        /*
            A terrace is a ledge at least a spacing wide, a crevice a spacing wide behind it and
            floor beyond, all inside one wall's half of the rift - which only a wide rift has. So a
            seed that wants terraces gets one, rather than asking for them and fitting none.
        */
        float main_base = f_terraced ? rng.GetFloat(52.0f,66.0f) : rng.GetFloat(34.0f,56.0f);
        Widths(main,main_base,rng.GetFloat(13.0f,16.0f),Seed());
        main_tip = main.p.back();
        out.rifts = 1;

        //Forks, off the main spine's middle, at 35-60 degrees to it, each to its own tip.
        std::vector<Spine> forks;
        float roll = rng.GetFloat(0.0f,1.0f);
        int num_forks = roll < 0.45f ? 0 : (roll < 0.85f ? 1 : 2);
        for (int b = 0; b < num_forks; b++){
            int n = (int)main.p.size();
            int at = std::max(1,std::min(n - 2,(int)(n * rng.GetFloat(0.3f,0.7f))));
            vec2 along = Unit(main.p[at + 1] - main.p[at - 1]);
            float side = rng.Roll(0.5f) ? 1.0f : -1.0f;
            vec2 heading = Unit(along + Perp(along) * (side * rng.GetFloat(0.7f,1.4f)));
            WalkRules fr;
            fr.keep = heading;
            fr.min_dot = 0.5f;
            fr.box_lo = vec2(lo.x + 55.0f,lo.y + 0.1f * D);
            fr.box_hi = vec2(hi.x - 55.0f,hi.y - 55.0f);
            fr.length = rng.GetFloat(140.0f,300.0f);
            Spine s;
            s.p = Walk(rng,main.p[at],heading,fr,step);
            float base = std::max(18.0f,main.r[at] * rng.GetFloat(0.45f,0.65f));
            Widths(s,base,rng.GetFloat(13.0f,16.0f),Seed());
            /*
                Kept only if its tip stands well clear of the main rift: a fork that turned back
                along it, or was cut short by the map's margin, only widens the main rift - and
                would be counted as a fork nobody can see.
            */
            vec2 tip = s.p.back();
            float clear = 1e30f;
            for (size_t k = 0; k + 1 < main.p.size(); k++){
                clear = std::min(clear,SegmentDistance(tip,main.p[k],main.p[k + 1]) - std::max(main.r[k],main.r[k + 1]));
            }
            if (Length(s.p) >= 60.0f && clear > 2.0f * spacing){
                forks.push_back(s);
                out.forks++;
            }
        }

        //A second rift, about one seed in three, in from the south, west or east.
        Spine second;
        bool f_second = rng.Roll(0.35f);
        if (f_second){
            int edge = rng.GetInt(0,2);
            WalkRules sr;
            sr.box_lo = vec2(lo.x + 55.0f,lo.y + 0.12f * D);
            sr.box_hi = vec2(hi.x - 55.0f,hi.y - 55.0f);
            sr.length = rng.GetFloat(130.0f,280.0f);
            sr.straight_steps = 2;
            vec2 start;
            vec2 heading;
            if (edge == 0){
                //The south edge, on whichever side of the main mouth has more room.
                bool f_east = (hi.x - mouth_x) > (mouth_x - lo.x);
                float a = f_east ? mouth_x + 200.0f : lo.x + 90.0f;
                float b = f_east ? hi.x - 90.0f : mouth_x - 200.0f;
                if (b <= a){
                    f_second = false;
                }
                start = vec2(rng.GetFloat(a,std::max(a,b)),hi.y + 16.0f);
                heading = north;
                sr.box_hi.y = hi.y + 2.0f * step;
                sr.stop_z = lo.y + D * rng.GetFloat(0.3f,0.55f);
            }else{
                float z = lo.y + D * rng.GetFloat(0.4f,0.8f);
                start = vec2(edge == 1 ? lo.x - 16.0f : hi.x + 16.0f,z);
                heading = vec2(edge == 1 ? 1.0f : -1.0f,0.0f);
                if (edge == 1){
                    sr.box_lo.x = lo.x - 2.0f * step;
                }else{
                    sr.box_hi.x = hi.x + 2.0f * step;
                }
            }
            sr.keep = heading;
            sr.min_dot = 0.55f;
            second.p = Walk(rng,start,heading,sr,step);
            Widths(second,rng.GetFloat(22.0f,36.0f),rng.GetFloat(13.0f,16.0f),Seed());
            if (f_second){
                out.rifts++;
            }
        }

        //--- The field: the main rift and its forks joined smoothly, the second apart -----------
        const float pad = 48.0f;
        rift.Init(lo,hi,2.0f,1e3f);
        StampSpine(rift,main,pad);
        for (const Spine& s : forks){
            Field branch;
            branch.Init(lo,hi,2.0f,1e3f);
            StampSpine(branch,s,pad);
            for (size_t i = 0; i < rift.d.size(); i++){
                rift.d[i] = SmoothMin(rift.d[i],branch.d[i],28.0f);
            }
        }
        if (f_second){
            StampSpine(rift,second,pad);
        }
        uint32_t wall_seed = Seed();
        for (int j = 0; j < rift.h; j++){
            for (int i = 0; i < rift.w; i++){
                float& v = rift.At(i,j);
                if (v < pad){
                    v += 4.0f * Noise(rift.Pos(i,j),45.0f,wall_seed);
                }
            }
        }

        //--- The rims, and whether they will do ------------------------------------------------
        rims.clear();
        for (const Contour& c : Contours(rift)){
            if (c.p.size() < 2){
                continue;
            }
            if (c.f_closed && Length(c.p) < 80.0f){
                return false;   //a blob of noise, not a rift
            }
            GridFeature f;
            f.kind = GRID_FEATURE_RIM;
            f.line = ContourLine(c,1.5f * T,0.25f * T);
            if (!c.f_closed){
                for (int end = 0; end < 2; end++){
                    vec2 p = end == 0 ? f.line.points.front() : f.line.points.back();
                    if (p.y <= lo.y){
                        return false;   //no rift may reach the north edge
                    }
                    f.f_end_on_outline[end] = true;
                }
                //Smoothing can bow a point past an edge it runs close to; held inside.
                for (vec2& p : f.line.points){
                    p.x = std::max(lo.x,std::min(hi.x,p.x));
                    p.y = std::max(lo.y,std::min(hi.y,p.y));
                }
            }
            if (!Fits(f.line,rims)){
                return false;
            }
            rims.push_back(f);
        }
        return !rims.empty() && PlateauBothSides();
    }

    /*
        Enough plateau either side of the main rift: the plateau cut from the main tip straight to
        the north edge, and the two largest pieces left at least a fifth of the map each. Without
        the cut the plateau is one piece round the tip, and the test would say nothing.
    */
    bool PlateauBothSides() const{
        int w = rift.w;
        int h = rift.h;
        std::vector<int> comp((size_t)w * h,-1);
        int cut_i = std::max(0,std::min(w - 1,(int)std::lround((main_tip.x - lo.x) / rift.cx)));
        int cut_j = std::max(0,std::min(h - 1,(int)std::lround((main_tip.y - lo.y) / rift.cz)));
        auto open = [&](int i, int j){ return rift.At(i,j) >= 0.0f && !(i == cut_i && j <= cut_j); };
        std::vector<int> sizes;
        std::vector<int> stack;
        int total = 0;
        for (int j = 0; j < h; j++){
            for (int i = 0; i < w; i++){
                if (!open(i,j) || comp[(size_t)j * w + i] >= 0){
                    continue;
                }
                int id = (int)sizes.size();
                int size = 0;
                stack.push_back(j * w + i);
                comp[(size_t)j * w + i] = id;
                while (!stack.empty()){
                    int c = stack.back();
                    stack.pop_back();
                    size++;
                    int ci = c % w;
                    int cj = c / w;
                    const int di[4] = {1,-1,0,0};
                    const int dj[4] = {0,0,1,-1};
                    for (int k = 0; k < 4; k++){
                        int ni = ci + di[k];
                        int nj = cj + dj[k];
                        if (ni < 0 || nj < 0 || ni >= w || nj >= h || !open(ni,nj) || comp[(size_t)nj * w + ni] >= 0){
                            continue;
                        }
                        comp[(size_t)nj * w + ni] = id;
                        stack.push_back(nj * w + ni);
                    }
                }
                sizes.push_back(size);
            }
        }
        total = w * h;
        std::sort(sizes.begin(),sizes.end());
        return sizes.size() >= 2 && sizes[sizes.size() - 2] >= total / 5;
    }

    //--- Shards, columns, terraces ---------------------------------------------------------------

    //The one closed contour of a small field, or nothing if it made none or several.
    bool ClosedLine(const Field& f, GridLine& line){
        std::vector<Contour> cs = Contours(f);
        if (cs.size() != 1 || !cs[0].f_closed){
            return false;
        }
        float corner = std::min(1.5f * T,Length(cs[0].p) / 10.0f);
        line = ContourLine(cs[0],corner,0.25f * T);
        return true;
    }

    void PlaceBlobs(int kind, int wanted, std::vector<GridFeature>& placed){
        out.wanted[kind] = wanted;
        //Where the floor is deep enough for at least a column with room round it.
        std::vector<int> deep;
        for (int j = 0; j < rift.h; j++){
            for (int i = 0; i < rift.w; i++){
                if (rift.At(i,j) < -(spacing + 10.0f)){
                    deep.push_back(j * rift.w + i);
                }
            }
        }
        for (int attempt = 0; attempt < 14 * wanted && out.count[kind] < wanted && !deep.empty(); attempt++){
            int c = deep[rng.GetInt(0,(int)deep.size() - 1)];
            vec2 centre = rift.Pos(c % rift.w,c / rift.w);
            float a, b, bump;
            if (kind == GRID_FEATURE_COLUMN){
                a = b = rng.GetFloat(14.0f,17.0f);
                bump = 1.0f;
            }else{
                a = rng.GetFloat(18.0f,35.0f);
                b = rng.GetFloat(12.0f,std::min(a,22.0f));
                bump = 2.0f;
            }
            uint32_t seed = Seed();
            if (rift.Sample(centre) > -(spacing + b)){
                continue;
            }
            //Long along the rift, as the walls run.
            vec2 along = Unit(Perp(rift.Gradient(centre)));
            vec2 across = Perp(along);
            Field blob;
            float reach = a + 10.0f;
            blob.Init(centre - vec2(reach,reach),centre + vec2(reach,reach),1.0f,0.0f);
            for (int j = 0; j < blob.h; j++){
                for (int i = 0; i < blob.w; i++){
                    vec2 p = blob.Pos(i,j);
                    vec2 q = p - centre;
                    float x = q.dot(along) / a;
                    float y = q.dot(across) / b;
                    blob.At(i,j) = (std::sqrt(x * x + y * y) - 1.0f) * b + bump * Noise(p,16.0f,seed);
                }
            }
            GridFeature f;
            f.kind = kind;
            if (ClosedLine(blob,f.line) && Fits(f.line,placed)){
                placed.push_back(f);
                out.count[kind]++;
            }
        }
    }

    /*
        A terrace: a stretch of a rim, offset into the rift - its near side a crevice's width in,
        its far side a ledge's width further - and closed with a half circle at each end. Built from
        the smoothed rim rather than from the field, because a band of the field deeper than a bend
        is round comes out with a cusp in it; an offset that does is refused below like any other
        line too sharp for the lattice (RoundEnough).
    */
    void PlaceTerraces(int wanted, std::vector<GridFeature>& placed, const std::vector<GridFeature>& rims){
        out.wanted[GRID_FEATURE_TERRACE] = wanted;
        //A half circle in eight steps of 22.5 degrees, as constants.
        static const float cap_cos[9] = {1.0f,0.92387953f,0.70710678f,0.38268343f,0.0f,
                                         -0.38268343f,-0.70710678f,-0.92387953f,-1.0f};
        static const float cap_sin[9] = {0.0f,0.38268343f,0.70710678f,0.92387953f,1.0f,
                                         0.92387953f,0.70710678f,0.38268343f,0.0f};
        for (int attempt = 0; attempt < 14 * wanted && out.count[GRID_FEATURE_TERRACE] < wanted; attempt++){
            const GridFeature& rim = rims[rng.GetInt(0,(int)rims.size() - 1)];
            const std::vector<vec2>& p = rim.line.points;
            int n = (int)p.size();
            float gap = spacing + 4.0f;
            float width = rng.GetFloat(24.0f,32.0f);
            float length = rng.GetFloat(60.0f,140.0f);
            //Rim points are a quarter side apart; a corner every three of them.
            int span = (int)(length / (0.25f * T));
            int first = rng.GetInt(1,std::max(1,n - span - 2));
            if (first + span + 1 >= n){
                continue;
            }
            //Which side of the rim is the rift: the field falls into it.
            vec2 mid = p[first + span / 2];
            vec2 side = Perp(Unit(p[first + span / 2 + 1] - p[first + span / 2 - 1]));
            float sign = rift.Sample(mid + side * 6.0f) < rift.Sample(mid - side * 6.0f) ? 1.0f : -1.0f;
            std::vector<vec2> inner;
            std::vector<vec2> outer;
            std::vector<vec2> along;
            std::vector<vec2> in;
            for (int k = first; k <= first + span; k += 3){
                vec2 t = Unit(p[k + 1] - p[k - 1]);
                vec2 nrm = Perp(t) * sign;
                inner.push_back(p[k] + nrm * gap);
                outer.push_back(p[k] + nrm * (gap + width));
                along.push_back(t);
                in.push_back(nrm);
            }
            if (inner.size() < 4){
                continue;
            }
            std::vector<vec2> corners = inner;
            //The far end: from the near side round to the far side, bulging on along the rim.
            vec2 c = (inner.back() + outer.back()) * 0.5f;
            for (int s = 1; s < 8; s++){
                corners.push_back(c - in.back() * (0.5f * width * cap_cos[s]) + along.back() * (0.5f * width * cap_sin[s]));
            }
            for (int k = (int)outer.size() - 1; k >= 0; k--){
                corners.push_back(outer[k]);
            }
            c = (inner.front() + outer.front()) * 0.5f;
            for (int s = 1; s < 8; s++){
                corners.push_back(c + in.front() * (0.5f * width * cap_cos[s]) - along.front() * (0.5f * width * cap_sin[s]));
            }
            corners.push_back(corners.front());
            GridFeature f;
            f.kind = GRID_FEATURE_TERRACE;
            f.line = GridSmoothLine(corners,true,0.25f * T);
            //All of it in the rift, and floor on beyond its far side, not the far wall.
            bool f_in_rift = true;
            for (const vec2& q : f.line.points){
                if (rift.Sample(q) > -spacing * 0.5f){
                    f_in_rift = false;
                    break;
                }
            }
            vec2 beyond = outer[outer.size() / 2] + in[in.size() / 2] * spacing;
            if (f_in_rift && rift.Sample(beyond) < -2.0f && Fits(f.line,placed)){
                placed.push_back(f);
                out.count[GRID_FEATURE_TERRACE]++;
            }
        }
    }

    //--- Rivers ----------------------------------------------------------------------------------

    //How far a river's line may be swung off its path, and how far its path keeps from a rim -
    //clear of the bank, the wet margin and half the widest river.
    #define RIVER_SWING         10.0f
    #define RIVER_RIM_CLEAR     14.0f

    struct Fall{
        vec2 lip;
        vec2 out;
        vec2 back;
    };

    /*
        Falls first: points on a rim with plateau behind, open floor ahead (no shard or terrace
        under the water), away from the map's edge and from each other. Then each one's course,
        the cheapest path over the plateau from a source on the west, east or north edge - the
        cost wanders on noise, which is the meander; rims cannot be crossed, and neither can the
        ground beside a river already placed.
    */
    void PlaceRivers(const std::vector<GridFeature>& features){
        int wanted = rng.GetInt(2,5);
        out.rivers_wanted = wanted;
        std::vector<Fall> candidates;
        for (const GridFeature& f : features){
            if (f.kind != GRID_FEATURE_RIM){
                continue;
            }
            for (size_t k = 0; k < f.line.points.size(); k += 4){
                vec2 lip = f.line.points[k];
                if (EdgeDistance(lip) < 60.0f){
                    continue;
                }
                vec2 g = rift.Gradient(lip);
                if (g.dot(g) < 0.09f){
                    continue;
                }
                Fall fall;
                fall.lip = lip;
                fall.out = -Unit(g);
                fall.back = lip - fall.out * 24.0f;
                bool f_ok = rift.Sample(fall.back) > 18.0f;
                for (float t = 6.0f; t <= 24.0f && f_ok; t += 6.0f){
                    vec2 q = lip + fall.out * t;
                    if (rift.Sample(q) > -4.0f){
                        f_ok = false;
                    }
                    for (const GridFeature& o : features){
                        if (o.kind != GRID_FEATURE_RIM && PointLineDistance(q,o.line) < 6.0f){
                            f_ok = false;
                        }
                    }
                }
                if (f_ok){
                    candidates.push_back(fall);
                }
            }
        }
        for (int i = (int)candidates.size() - 1; i > 0; i--){
            std::swap(candidates[i],candidates[rng.GetInt(0,i)]);
        }

        Field cost;
        cost.Init(lo,hi,4.0f,0.0f);
        std::vector<uint8_t> blocked(cost.d.size(),0);
        for (int j = 0; j < cost.h; j++){
            for (int i = 0; i < cost.w; i++){
                cost.At(i,j) = rift.Sample(cost.Pos(i,j));
                if (cost.At(i,j) < RIVER_RIM_CLEAR + RIVER_SWING){
                    blocked[(size_t)j * cost.w + i] = 1;
                }
            }
        }
        /*
            What a step costs per unit, per cell, worked out once rather than on every step of
            every search. Two scales of noise: the river's broad swings, and the wiggle in them -
            with the broad one alone the path ran dead straight along the raster's axes wherever
            the cost was nearly flat. Dearer close to a rim, and close to the map's edge, which a
            river reaches but should not follow.
        */
        uint32_t meander = Seed();
        std::vector<float> rate(cost.d.size(),1.0f);
        for (int j = 0; j < cost.h; j++){
            for (int i = 0; i < cost.w; i++){
                vec2 p = cost.Pos(i,j);
                float c = 1.0f + 1.6f * (0.5f + 0.5f * Noise(p,80.0f,meander)) +
                          0.9f * (0.5f + 0.5f * Noise(p,28.0f,meander ^ 0x51F15EEDu));
                float rim = cost.At(i,j);
                if (rim < 34.0f){
                    c += (34.0f - rim) * 0.08f;
                }
                float edge = EdgeDistance(p);
                if (edge < 60.0f){
                    c *= 1.0f + (60.0f - edge) / 12.0f;
                }
                rate[(size_t)j * cost.w + i] = c;
            }
        }
        std::vector<Fall> falls;
        std::vector<vec2> sources;
        for (const Fall& fall : candidates){
            if ((int)out.rivers.size() >= wanted){
                break;
            }
            bool f_far = true;
            for (const Fall& o : falls){
                vec2 d = o.lip - fall.lip;
                if (d.dot(d) < 80.0f * 80.0f){
                    f_far = false;
                }
            }
            if (!f_far){
                continue;
            }
            GridRiverLine river;
            river.width = rng.GetFloat(5.0f,7.5f);
            if (!Course(cost,blocked,rate,fall,sources,river)){
                continue;
            }
            falls.push_back(fall);
            sources.push_back(river.points.front());
            //Nothing else comes within reach of it: its banks, its wet margin and another's.
            const float keep_off = 24.0f + RIVER_SWING;
            for (size_t k = 0; k + 1 < river.points.size(); k++){
                vec2 a = river.points[k];
                vec2 b = river.points[k + 1];
                int i0 = std::max(0,(int)std::floor((std::min(a.x,b.x) - keep_off - lo.x) / cost.cx));
                int i1 = std::min(cost.w - 1,(int)std::ceil((std::max(a.x,b.x) + keep_off - lo.x) / cost.cx));
                int j0 = std::max(0,(int)std::floor((std::min(a.y,b.y) - keep_off - lo.y) / cost.cz));
                int j1 = std::min(cost.h - 1,(int)std::ceil((std::max(a.y,b.y) + keep_off - lo.y) / cost.cz));
                for (int j = j0; j <= j1; j++){
                    for (int i = i0; i <= i1; i++){
                        if (SegmentDistance(cost.Pos(i,j),a,b) < keep_off){
                            blocked[(size_t)j * cost.w + i] = 1;
                        }
                    }
                }
            }
            out.rivers.push_back(river);
        }
    }

    bool Course(const Field& cost, const std::vector<uint8_t>& blocked, const std::vector<float>& rate, const Fall& fall,
                const std::vector<vec2>& sources, GridRiverLine& river){
        int w = cost.w;
        int h = cost.h;
        auto cell_of = [&](const vec2& p){
            int i = std::max(0,std::min(w - 1,(int)std::lround((p.x - lo.x) / cost.cx)));
            int j = std::max(0,std::min(h - 1,(int)std::lround((p.y - lo.y) / cost.cz)));
            return j * w + i;
        };
        int start = cell_of(fall.back);
        if (blocked[start]){
            return false;
        }
        const float INF = 1e30f;
        std::vector<float> dist(cost.d.size(),INF);
        std::vector<int> back(cost.d.size(),-1);
        typedef std::pair<float,int> Entry;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> open;
        dist[start] = 0.0f;
        open.push(Entry(0.0f,start));
        const int di[8] = {1,-1,0,0,1,1,-1,-1};
        const int dj[8] = {0,0,1,-1,1,-1,1,-1};
        while (!open.empty()){
            Entry e = open.top();
            open.pop();
            if (e.first > dist[e.second]){
                continue;
            }
            int ci = e.second % w;
            int cj = e.second / w;
            for (int k = 0; k < 8; k++){
                int ni = ci + di[k];
                int nj = cj + dj[k];
                if (ni < 0 || nj < 0 || ni >= w || nj >= h){
                    continue;
                }
                int n = nj * w + ni;
                if (blocked[n]){
                    continue;
                }
                vec2 d = cost.Pos(ni,nj) - cost.Pos(ci,cj);
                float len = std::sqrt(d.dot(d));
                float c = 0.5f * (rate[e.second] + rate[n]);
                float nd = e.first + len * c;
                if (nd < dist[n]){
                    dist[n] = nd;
                    back[n] = e.second;
                    open.push(Entry(nd,n));
                }
            }
        }
        /*
            The source: on the west, east or north edge, reached, not by another source or a corner.
            Neither the nearest edge - a river should cross some country - nor the farthest, which
            is far only because the way there winds round something.
        */
        std::vector<int> edge_cells;
        float far = 0.0f;
        for (int pass = 0; pass < 2; pass++){
            for (int j = 0; j < h; j++){
                for (int i = 0; i < w; i++){
                    bool f_edge = i == 0 || i == w - 1 || j == 0;
                    int c = j * w + i;
                    if (!f_edge || dist[c] >= INF){
                        continue;
                    }
                    vec2 p = cost.Pos(i,j);
                    bool f_corner = std::min(p.x - lo.x,hi.x - p.x) < 40.0f && std::min(p.y - lo.y,hi.y - p.y) < 40.0f;
                    bool f_crowded = false;
                    for (const vec2& s : sources){
                        vec2 d = s - p;
                        if (d.dot(d) < 60.0f * 60.0f){
                            f_crowded = true;
                        }
                    }
                    if (f_corner || f_crowded){
                        continue;
                    }
                    if (pass == 0){
                        far = std::max(far,dist[c]);
                    }else if (dist[c] >= 0.4f * far && dist[c] <= 0.8f * far){
                        edge_cells.push_back(c);
                    }
                }
            }
        }
        if (edge_cells.empty()){
            return false;
        }
        /*
            Of those, one of the most direct quarter - path cost over straight distance - so a river
            heads off across the country from its edge instead of running along beside it, which
            the cost's own edge penalty alone did not stop. Sorted by index on ties, for the same
            order in every build.
        */
        std::vector<std::pair<float,int>> direct;
        for (int c : edge_cells){
            vec2 d = cost.Pos(c % w,c / w) - fall.back;
            direct.push_back(std::make_pair(dist[c] / std::max(1.0f,std::sqrt(d.dot(d))),c));
        }
        std::sort(direct.begin(),direct.end());
        int keep = std::max(1,(int)direct.size() / 4);
        int source = direct[rng.GetInt(0,keep - 1)].second;
        std::vector<vec2> path;
        for (int c = source; c >= 0; c = back[c]){
            path.push_back(cost.Pos(c % w,c / w));
        }
        //Every fourth cell, which the terrain's corner cutting then rounds.
        river.points.clear();
        for (size_t k = 0; k + 1 < path.size(); k += 4){
            river.points.push_back(path[k]);
        }
        /*
            The meander. A cheapest path is straight wherever it can be, whatever the noise in its
            cost, so the river is swung either side of it on noise along its length - up to
            RIVER_SWING, which the search kept clear for it from rims and other rivers - fading to
            nothing at the source, which stays on the map's edge, and before the fall, whose
            approach must stay square to the rim.
        */
        uint32_t swing_seed = Seed();
        size_t num = river.points.size();
        std::vector<float> arc(num,0.0f);
        for (size_t k = 1; k < num; k++){
            vec2 d = river.points[k] - river.points[k - 1];
            arc[k] = arc[k - 1] + std::sqrt(d.dot(d));
        }
        std::vector<vec2> swung = river.points;
        for (size_t k = 1; k + 1 < num; k++){
            float fade = std::min(1.0f,std::min(arc[k],arc[num - 1] - arc[k]) / 40.0f);
            vec2 side = Perp(Unit(river.points[k + 1] - river.points[k - 1]));
            swung[k] = river.points[k] + side * (RIVER_SWING * fade * Noise(vec2(arc[k],0.0f),70.0f,swing_seed));
        }
        river.points = swung;
        river.points.push_back(fall.back);
        river.points.push_back(fall.lip);
        river.points.push_back(fall.lip + fall.out * 10.0f);
        return true;
    }
};

}

ChasmLayout GenerateChasmLayout(uint32_t seed, const vec2& lo, const vec2& hi, float side){
    auto t0 = std::chrono::steady_clock::now();
    ChasmLayout out;
    /*
        Its own stream, from the seed but not the grid's: the grid draws from RRandom(seed), and a
        layout drawing from that too would shift the grid's merge with every change to the layout.
    */
    RRandom rng((int)(seed * 2654435761u ^ 0x5EEDC4A5u));
    rng.Generate((size_t)1 << 18);
    Builder b(rng,lo,hi,side,out);

    //What to stand in the chasm, drawn first because the rifts are drawn to suit it.
    int want_terraces = rng.GetInt(0,4);
    int want_shards = rng.GetInt(0,4);
    int want_columns = rng.GetInt(0,5);
    b.f_terraced = want_terraces > 0;
    std::vector<GridFeature> rims;
    const int max_attempts = 30;
    out.f_rifts_ok = false;
    for (int attempt = 1; attempt <= max_attempts; attempt++){
        out.attempts = attempt;
        if (b.TryRifts(rims)){
            out.f_rifts_ok = true;
            break;
        }
    }
    std::vector<GridFeature> placed = rims;
    out.count[GRID_FEATURE_RIM] = (int)rims.size();
    if (!rims.empty()){
        b.PlaceTerraces(want_terraces,placed,rims);
    }
    b.PlaceBlobs(GRID_FEATURE_SHARD,want_shards,placed);
    b.PlaceBlobs(GRID_FEATURE_COLUMN,want_columns,placed);
    out.features = placed;
    b.PlaceRivers(out.features);
    out.generate_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}
