#include "Grid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <queue>
#include "RRandom.h"

//The north mountain's foot - see PlaceMountainFoot. Up here because the sides' test reads it too.
#define MOUNTAIN_BASE_DEPTH         0.11f   //of the map's depth, the foot's mean distance from the north edge
#define MOUNTAIN_BASE_SWING         0.05f   //of the depth, how far the base wanders either way
#define MOUNTAIN_TONGUE_MARGIN      40.0f   //how far past the main tip the tongue reaches, world units
#define MOUNTAIN_TONGUE_HALF_WIDTH  110.0f  //at the base's depth, either side of the tip
#define MOUNTAIN_FOOT_STEP          4.0f    //world units between the foot's points
#define MOUNTAIN_SPRING_DEPTH       10.0f   //how far into the mountain a river cut off at its foot rises

/*
    THE CHASM'S LAYOUT FROM THE SEED - grid_plan.md, "Step 10, generated chasm", for the method and
    the reasons. In short: rifts are spines (polylines with a half-width) walked from the map's edge;
    they become a distance field, and the rims are its zero contours, so a fork or a second rift is
    just more contour. Islands - shards, columns, ledges - are closed lines of their own; balconies
    are open lines leaving a rim and coming back to it; rivers are cheapest paths over the home
    side's plateau to falls chosen on its rims.

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
    bool f_ledged = false;      //the seed wants ledges, which need a wide rift

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
            if (f_closed && GridFeatureIsIsland(f.kind) &&
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
        1.4-1.9 sides and passes; a ledge end cut square by its ball did not, and the cells left
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
            A ledge is at least a spacing wide, with a drop a spacing wide behind it and
            floor beyond, all inside one wall's half of the rift - which only a wide rift has. So a
            seed that wants ledges gets one, rather than asking for them and fitting none.
        */
        float main_base = f_ledged ? rng.GetFloat(52.0f,66.0f) : rng.GetFloat(34.0f,56.0f);
        Widths(main,main_base,rng.GetFloat(13.0f,16.0f),Seed());
        main_tip = main.p.back();
        out.main_tip = main_tip;
        out.main_mouth_x = mouth_x;
        out.rifts = 1;

        /*
            ONE CHASM (user, 2026-10-06): no second rift, and no forks as wide as a rift. The forks'
            walks are kept, but as CRACKS - a few plots wide, cut into the plateau as heights
            (Terrain.cpp) rather than stamped into the field, so they make no rim lines and no
            second chasm. Walked here; trimmed to where they leave the rift once the field is made.
        */
        std::vector<Spine> forks;
        float roll = rng.GetFloat(0.0f,1.0f);
        int num_forks = roll < 0.3f ? 0 : (roll < 0.8f ? 1 : 2);
        for (int b = 0; b < num_forks; b++){
            int n = (int)main.p.size();
            int at = std::max(1,std::min(n - 2,(int)(n * rng.GetFloat(0.3f,0.7f))));
            vec2 along = Unit(main.p[at + 1] - main.p[at - 1]);
            float side = rng.Roll(0.5f) ? 1.0f : -1.0f;
            vec2 heading = Unit(along + Perp(along) * (side * rng.GetFloat(0.7f,1.4f)));
            WalkRules fr;
            fr.keep = heading;
            fr.min_dot = 0.5f;
            fr.box_lo = vec2(lo.x + 60.0f,lo.y + 0.1f * D);
            fr.box_hi = vec2(hi.x - 60.0f,hi.y - 60.0f);
            fr.length = main.r[at] + rng.GetFloat(60.0f,140.0f);
            Spine s;
            s.p = Walk(rng,main.p[at],heading,fr,step * 0.5f);
            forks.push_back(s);
        }
        //Short fissures off the rims, either side, at a steep angle to the rift.
        int num_fissures = rng.GetInt(2,5);
        for (int b = 0; b < num_fissures; b++){
            int n = (int)main.p.size();
            int at = std::max(1,std::min(n - 2,(int)(n * rng.GetFloat(0.1f,0.9f))));
            vec2 along = Unit(main.p[at + 1] - main.p[at - 1]);
            float side = rng.Roll(0.5f) ? 1.0f : -1.0f;
            vec2 heading = Unit(along * rng.GetFloat(-0.6f,0.6f) + Perp(along) * side);
            WalkRules fr;
            fr.keep = heading;
            fr.min_dot = 0.6f;
            fr.box_lo = vec2(lo.x + 60.0f,lo.y + 0.1f * D);
            fr.box_hi = vec2(hi.x - 60.0f,hi.y - 60.0f);
            fr.length = main.r[at] + rng.GetFloat(14.0f,32.0f);
            Spine s;
            s.p = Walk(rng,main.p[at],heading,fr,step * 0.25f);
            forks.push_back(s);
        }

        //--- The field: the main rift alone ------------------------------------------------------
        const float pad = 48.0f;
        rift.Init(lo,hi,2.0f,1e3f);
        StampSpine(rift,main,pad);
        uint32_t wall_seed = Seed();
        for (int j = 0; j < rift.h; j++){
            for (int i = 0; i < rift.w; i++){
                float& v = rift.At(i,j);
                if (v < pad){
                    v += 4.0f * Noise(rift.Pos(i,j),45.0f,wall_seed);
                }
            }
        }
        out.cracks.clear();
        for (const Spine& s : forks){
            AddCrack(s);
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
        return !rims.empty() && SidesWhole();
    }

    /*
        A crack from a walk that starts on the main spine: from CRACK_INSET inside the rift (so it is
        joined to the chasm whatever the plots make of the rim) to where it stops. Cut short where it
        would come back toward the rim - with the rift it would box in a piece of plateau that no one
        could walk to - and where it nears the mountain, the map's edge or another crack. Dropped if
        too little is left.
    */
    float CrackDistance(const vec2& p) const{
        float best = 1e30f;
        for (const ChasmLayout::Crack& c : out.cracks){
            for (size_t k = 0; k + 1 < c.points.size(); k++){
                best = std::min(best,SegmentDistance(p,c.points[k],c.points[k + 1]));
            }
        }
        return best;
    }

    #define CRACK_INSET         4.0f    //how far inside the rift a crack begins
    #define CRACK_MIN_LENGTH    10.0f   //of plateau it cuts, or it is not worth one
    #define CRACK_APART         30.0f   //from any other crack
    #define CRACK_EDGE_CLEAR    40.0f   //from the map's edge and the mountain's reach
    void AddCrack(const Spine& s){
        ChasmLayout::Crack c;
        size_t k = 0;
        while (k < s.p.size() && rift.Sample(s.p[k]) < -CRACK_INSET){
            k++;
        }
        float out_of_rift = 0.0f;   //the most the crack has cleared the rim by, so far
        for (; k < s.p.size(); k++){
            vec2 p = s.p[k];
            float d = rift.Sample(p);
            if (d > 0.0f && d < out_of_rift - 6.0f){
                break;      //heading back to the rim
            }
            out_of_rift = std::max(out_of_rift,d);
            if (EdgeDistance(p) < CRACK_EDGE_CLEAR || p.y < MountainReach(p.x) + CRACK_EDGE_CLEAR){
                break;
            }
            bool f_near = false;
            for (const ChasmLayout::Crack& o : out.cracks){
                for (const vec2& q : o.points){
                    f_near = f_near || (p - q).dot(p - q) < CRACK_APART * CRACK_APART;
                }
            }
            if (f_near){
                break;
            }
            c.points.push_back(p);
        }
        if (c.points.size() < 2 || out_of_rift < CRACK_MIN_LENGTH){
            return;
        }
        c.half_width = rng.GetFloat(2.0f,3.0f);
        out.cracks.push_back(c);
    }

    /*
        Where the north mountain may come down to at x, at most - its base at the deepest it swings
        to, and its tongue at its widest - so a test against it is never more open than the real
        mountain (drawn later, from its own stream: PlaceMountainFoot). Pocket spurs are left out;
        they stand clear of every rim.
    */
    float MountainReach(float x) const{
        const float D = hi.y - lo.y;
        float base = lo.y + D * (MOUNTAIN_BASE_DEPTH + MOUNTAIN_BASE_SWING);
        float tongue_z = std::min(hi.y - 0.2f * D,main_tip.y + MOUNTAIN_TONGUE_MARGIN);
        float dx = (x - main_tip.x) / (MOUNTAIN_TONGUE_HALF_WIDTH * 1.2f);
        float tongue = tongue_z - (tongue_z - (lo.y + D * MOUNTAIN_BASE_DEPTH)) * dx * dx;
        return std::max(base,tongue) + 8.0f;
    }

    /*
        THE SIDES, and that each is one piece (Grid.h, ChasmLayout's "THE SIDES"). The plateau out of
        the mountain's reach is flooded over the rift's raster, a piece at a time; a piece belongs
        to the side of the main mouth where it meets the south edge. Refused, so the rifts are drawn
        again: a piece that never reaches the south edge (boxed in by rifts and the mountain - land
        only a zeppelin could reach), a side in two pieces (a second rift from the south whose tip
        runs into the mountain), or less than a fifth of the map on either side. `side_of` keeps the
        answer per raster cell for what is placed after: -1 west, +1 east, 0 rift or mountain.
    */
    std::vector<int8_t> side_of;
    int SideAt(const vec2& p) const{
        if (side_of.empty()){
            return 0;
        }
        int i = std::max(0,std::min(rift.w - 1,(int)std::lround((p.x - lo.x) / rift.cx)));
        int j = std::max(0,std::min(rift.h - 1,(int)std::lround((p.y - lo.y) / rift.cz)));
        return side_of[(size_t)j * rift.w + i];
    }

    bool SidesWhole(){
        int w = rift.w;
        int h = rift.h;
        side_of.assign((size_t)w * h,0);
        std::vector<int> comp((size_t)w * h,-1);
        std::vector<float> reach(w);
        for (int i = 0; i < w; i++){
            reach[i] = MountainReach(rift.Pos(i,0).x);
        }
        auto open = [&](int i, int j){ return rift.At(i,j) >= 0.0f && rift.Pos(i,j).y > reach[i]; };
        std::vector<int> sizes;
        std::vector<int> piece_side;    //-1 or +1 by where it meets the south edge, 0 never, 2 both
        std::vector<int> stack;
        for (int j = 0; j < h; j++){
            for (int i = 0; i < w; i++){
                if (!open(i,j) || comp[(size_t)j * w + i] >= 0){
                    continue;
                }
                int id = (int)sizes.size();
                int size = 0;
                int side = 0;
                stack.push_back(j * w + i);
                comp[(size_t)j * w + i] = id;
                while (!stack.empty()){
                    int c = stack.back();
                    stack.pop_back();
                    size++;
                    int ci = c % w;
                    int cj = c / w;
                    if (cj == h - 1){
                        int here = (rift.Pos(ci,cj).x < out.main_mouth_x) ? -1 : 1;
                        side = (side == 0 || side == here) ? here : 2;
                    }
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
                piece_side.push_back(side);
            }
        }
        int pieces[2] = {0,0};
        int area[2] = {0,0};
        for (size_t k = 0; k < sizes.size(); k++){
            int side = piece_side[k];
            if (side != -1 && side != 1){
                return false;   //cut off from the south edge, or joining both sides
            }
            pieces[side > 0]++;
            area[side > 0] += sizes[k];
        }
        if (pieces[0] != 1 || pieces[1] != 1 || area[0] < w * h / 5 || area[1] < w * h / 5){
            return false;
        }
        for (size_t c = 0; c < comp.size(); c++){
            if (comp[c] >= 0){
                side_of[c] = (int8_t)piece_side[comp[c]];
            }
        }
        return true;
    }

    //--- Islands: shards, columns, ledges -------------------------------------------------------

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
        A ledge: a long island lying along a wall - a stretch of a rim, offset into the rift, its near
        side a drop's width in, its far side a ledge's width further - and closed with a half circle
        at each end. Built from the smoothed rim rather than from the field, because a band of the
        field deeper than a bend is round comes out with a cusp in it; an offset that does is refused
        below like any other line too sharp for the lattice (RoundEnough).
    */
    void PlaceLedges(int wanted, std::vector<GridFeature>& placed, const std::vector<GridFeature>& rims){
        out.wanted[GRID_FEATURE_LEDGE] = wanted;
        //A half circle in eight steps of 22.5 degrees, as constants.
        static const float cap_cos[9] = {1.0f,0.92387953f,0.70710678f,0.38268343f,0.0f,
                                         -0.38268343f,-0.70710678f,-0.92387953f,-1.0f};
        static const float cap_sin[9] = {0.0f,0.38268343f,0.70710678f,0.92387953f,1.0f,
                                         0.92387953f,0.70710678f,0.38268343f,0.0f};
        for (int attempt = 0; attempt < 14 * wanted && out.count[GRID_FEATURE_LEDGE] < wanted; attempt++){
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
            f.kind = GRID_FEATURE_LEDGE;
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
                out.count[GRID_FEATURE_LEDGE]++;
            }
        }
    }

    //--- Balconies ------------------------------------------------------------------------------

    /*
        A balcony: a ledge joined to a wall a step below the rim (Grid.h). Its line leaves the rim
        square at one end of a stretch of it, straight out for most of its depth, turns along it on a
        quarter circle, runs at its depth along the stretch, and comes back the same way - so its two
        ends are points of the rim's own line, where the grid fixes a vertex of both chains.

        Square, because two lines meeting at a junction are closer than the spacing near it, and the
        steeper they part the sooner they are clear: the check lets a balcony off against its own rim
        within GRID_JUNCTION_REACH of its ends, and holds it to GRID_BALCONY_RIM_SPACING beyond them -
        a balcony is shallow, "only room for a couple of things". The quarter circle is the tightest
        turn the lattice draws (RoundEnough). Everything else - the spacing from other lines, the
        map's edge, itself - holds as for any line, and there must be floor beyond it, not the far
        wall.

        At least one on the home side, which has nothing else to winch floatstone from before its
        first zeppelin; the far side may have none (gameplay_plan.md, "Floatstone").
    */
    #define BALCONY_DEPTH_MIN       18.0f   //from the rim's line to the balcony's, world units
    #define BALCONY_DEPTH_MAX       22.0f
    #define BALCONY_TURN            16.0f   //the quarter circles' radius: 12 folded cells inside the turn
    #define BALCONY_LENGTH_MIN      45.0f   //along the rim
    #define BALCONY_LENGTH_MAX      70.0f

    bool FitsBalcony(const GridLine& line, int rim_index, const std::vector<GridFeature>& placed) const{
        if (!RoundEnough(line,false)){
            return false;
        }
        if (GridLineGap(line,line,2.0f * spacing,NULL) < spacing){
            return false;
        }
        float reach = GRID_JUNCTION_REACH * T;
        float from_rim = GRID_BALCONY_RIM_SPACING * T;
        std::vector<float> arc(line.points.size(),0.0f);
        for (size_t k = 1; k < line.points.size(); k++){
            vec2 d = line.points[k] - line.points[k - 1];
            arc[k] = arc[k - 1] + std::sqrt(d.dot(d));
        }
        float total = arc.back();
        for (size_t f = 0; f < placed.size(); f++){
            if ((int)f != rim_index){
                if (GridLineGap(line,placed[f].line,0.0f,NULL) < spacing){
                    return false;
                }
                continue;
            }
            for (size_t k = 0; k < line.points.size(); k++){
                if (arc[k] >= reach && total - arc[k] >= reach && PointLineDistance(line.points[k],placed[f].line) < from_rim){
                    return false;
                }
            }
        }
        for (const vec2& q : line.points){
            if (EdgeDistance(q) < spacing){
                return false;
            }
        }
        return true;
    }

    void PlaceBalconies(int want_west, int want_east, std::vector<GridFeature>& placed, int num_rims){
        out.wanted[GRID_FEATURE_BALCONY] = want_west + want_east;
        out.count[GRID_FEATURE_BALCONY] = 0;
        out.balconies[0] = out.balconies[1] = 0;
        static const float turn_cos[5] = {1.0f,0.92387953f,0.70710678f,0.38268343f,0.0f};
        static const float turn_sin[5] = {0.0f,0.38268343f,0.70710678f,0.92387953f,1.0f};
        const int wanted[2] = {want_west,want_east};
        for (int side_index = 1; side_index >= 0; side_index--){
            int side = side_index ? 1 : -1;
            for (int attempt = 0; attempt < 40 && out.balconies[side_index] < wanted[side_index]; attempt++){
                int rim_index = rng.GetInt(0,num_rims - 1);
                const std::vector<vec2>& p = placed[rim_index].line.points;
                int n = (int)p.size();
                float depth = rng.GetFloat(BALCONY_DEPTH_MIN,BALCONY_DEPTH_MAX);
                float length = rng.GetFloat(BALCONY_LENGTH_MIN,BALCONY_LENGTH_MAX);
                //Rim points are a quarter side apart.
                int span = (int)(length / (0.25f * T));
                int first = rng.GetInt(2,std::max(2,n - span - 3));
                int last = first + span;
                if (last + 2 >= n){
                    continue;
                }
                //Into the rift: the field falls that way. On the wanted side: the plateau behind it.
                vec2 mid = p[(first + last) / 2];
                vec2 side_dir = Perp(Unit(p[(first + last) / 2 + 1] - p[(first + last) / 2 - 1]));
                float sign = rift.Sample(mid + side_dir * 6.0f) < rift.Sample(mid - side_dir * 6.0f) ? 1.0f : -1.0f;
                if (SideAt(mid - side_dir * (sign * 8.0f)) != side){
                    continue;
                }
                auto frame = [&](int k, vec2& t, vec2& nrm){
                    t = Unit(p[k + 1] - p[k - 1]);
                    nrm = Perp(t) * sign;
                };
                /*
                    Laid out in the rim's own frame - `k` rim points along, `d` out from it - so the
                    turns land exactly on the run however the rim bends. Built flat from each end's
                    tangent instead, a turn ended a unit or two off a curving rim's offset while the
                    run's first corner sat on it a few units on, and on a short balcony that jog
                    folded the cells under it (seed 2, the first sweep at these sizes).
                */
                auto at = [&](float k, float d) -> vec2 {
                    int i = std::min(std::max((int)std::floor(k),1),n - 3);
                    float u = k - (float)i;
                    vec2 ta, na, tb, nb;
                    frame(i,ta,na);
                    frame(i + 1,tb,nb);
                    return p[i] * (1.0f - u) + p[i + 1] * u + Unit(na * (1.0f - u) + nb * u) * d;
                };
                std::vector<vec2> corners;
                float leg = depth - BALCONY_TURN;
                float turn = BALCONY_TURN / (0.25f * T);   //the turn's radius in rim points
                //Out from the rim, square to it, then the quarter circle onto the run.
                corners.push_back(p[first]);
                for (float d = 4.0f; d < leg; d += 4.0f){
                    corners.push_back(at((float)first,d));
                }
                for (int a = 0; a <= 4; a++){
                    corners.push_back(at(first + turn * (1.0f - turn_cos[a]),leg + BALCONY_TURN * turn_sin[a]));
                }
                //The run, a rim point every three - from a turn's length in to a turn's length short.
                int skip = (int)std::ceil(turn) + 3;
                for (int k = first + skip; k <= last - skip; k += 3){
                    corners.push_back(at((float)k,depth));
                }
                for (int a = 4; a >= 0; a--){
                    corners.push_back(at(last - turn * (1.0f - turn_cos[a]),leg + BALCONY_TURN * turn_sin[a]));
                }
                for (float d = leg - 4.0f; d > 0.0f; d -= 4.0f){
                    corners.push_back(at((float)last,d));
                }
                corners.push_back(p[last]);
                GridFeature f;
                f.kind = GRID_FEATURE_BALCONY;
                f.line = GridSmoothLine(corners,false,0.25f * T);
                f.line.points.front() = p[first];
                f.line.points.back() = p[last];
                f.end_on_feature[0] = rim_index;
                f.end_on_feature[1] = rim_index;
                f.side = side;
                //In the rift away from its ends, and floor beyond it - not the far wall, not another rim.
                bool f_ok = true;
                for (size_t k = 0; k < f.line.points.size() && f_ok; k++){
                    vec2 q = f.line.points[k];
                    float from_rim = PointLineDistance(q,placed[rim_index].line);
                    if (from_rim > 10.0f && rift.Sample(q) > -4.0f){
                        f_ok = false;
                    }
                }
                for (int k = first + skip; k <= last - skip && f_ok; k += 6){
                    vec2 t, nrm;
                    frame(k,t,nrm);
                    if (rift.Sample(p[k] + nrm * (depth + spacing)) > -2.0f){
                        f_ok = false;
                    }
                }
                if (!f_ok || !FitsBalcony(f.line,rim_index,placed)){
                    continue;
                }
                //The balcony's outline: the line, then back along the rim to where it started.
                f.region.points = f.line.points;
                for (int k = last - 1; k > first; k--){
                    f.region.points.push_back(p[k]);
                }
                f.region.points.push_back(p[first]);
                placed.push_back(f);
                out.count[GRID_FEATURE_BALCONY]++;
                out.balconies[side_index]++;
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
        Falls first: points on a rim with the HOME side's plateau behind - rivers run only on the
        east (Grid.h, "THE SIDES") - open floor ahead (no island or balcony under the water), away
        from the map's edge and from each other. Then each one's course, the cheapest path over the
        home side from a source on its east edge, or the north edge east of the main tip - the cost
        wanders on noise, which is the meander; rims cannot be crossed, nor the far side, nor the
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
                bool f_ok = rift.Sample(fall.back) > 18.0f && SideAt(fall.back) == 1 && CrackDistance(lip) > 24.0f;
                for (float t = 6.0f; t <= 24.0f && f_ok; t += 6.0f){
                    vec2 q = lip + fall.out * t;
                    if (rift.Sample(q) > -4.0f){
                        f_ok = false;
                    }
                    for (const GridFeature& o : features){
                        if (o.kind != GRID_FEATURE_RIM && PointLineDistance(q,o.line) < 6.0f){
                            f_ok = false;
                        }
                        if (o.kind == GRID_FEATURE_BALCONY && PointInside(q,o.region)){
                            f_ok = false;   //onto a balcony
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
                vec2 q = cost.Pos(i,j);
                cost.At(i,j) = rift.Sample(q);
                //Not near a rim or a crack; not on the far side, nor round the main tip to it through
                //the mountain.
                int side = SideAt(q);
                if (cost.At(i,j) < RIVER_RIM_CLEAR + RIVER_SWING || side == -1 || (side == 0 && q.x < main_tip.x) ||
                    CrackDistance(q) < RIVER_RIM_CLEAR + RIVER_SWING){
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
            The source: on the east edge, or the north edge east of the main tip, reached, not by
            another source or a corner.
            Neither the nearest edge - a river should cross some country - nor the farthest, which
            is far only because the way there winds round something.
        */
        std::vector<int> edge_cells;
        float far = 0.0f;
        for (int pass = 0; pass < 2; pass++){
            for (int j = 0; j < h; j++){
                for (int i = 0; i < w; i++){
                    bool f_edge = i == w - 1 || (j == 0 && cost.Pos(i,j).x > main_tip.x);
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

/*
    The north mountain's foot (biomes_plan.md steps 1 and 3). A BASE about a tenth of the map's depth
    from the north edge, wandering on two octaves of noise; a TONGUE - a parabola about the main tip's
    x, reaching MOUNTAIN_TONGUE_MARGIN past the tip - which covers the rift's whole head and the plateau
    either side of it (the rift is up to 66 wide near its middle and narrows to its tip); and a SPUR
    round every pocket, reaching a valley's length past it. All joined by a smooth maximum, so the foot
    bends rather than kinks.
*/
//Pockets (step 3).
#define POCKET_RADIUS_MIN           14.0f
#define POCKET_RADIUS_MAX           22.0f
#define POCKET_VALLEY_MIN           26.0f   //from the meadow's edge to the foot: long enough to climb gently
#define POCKET_VALLEY_MAX           36.0f
#define POCKET_SPUR_SHOULDER        45.0f   //past the meadow's radius, either side, where the spur meets the base
#define POCKET_CLEAR_RIM            40.0f   //past the meadow's edge, to any rim: a pocket is in the mountain, not on the chasm
#define POCKET_CLEAR_RIVER          18.0f
#define POCKET_CLEAR_TIP            50.0f   //past the meadow's radius, in x from the main tip: clearly on one side
#define POCKET_VALLEY_LONGEST       70.0f   //from the meadow's edge to the foot, where the tongue pushes the foot south
#define POCKET_APART                60.0f   //between two meadows' edges

namespace {

float PointsDistance(const vec2& p, const std::vector<vec2>& pts){
    float best = 1e30f;
    for (size_t i = 0; i + 1 < pts.size(); i++){
        best = std::min(best,SegmentDistance(p,pts[i],pts[i + 1]));
    }
    if (pts.size() == 1){
        best = (p - pts[0]).length();
    }
    return best;
}

uint32_t PocketSeed(const ChasmLayout::Pocket& k){
    return HashInts((int)std::lround(k.centre.x * 16.0f),(int)std::lround(k.centre.y * 16.0f),0x90C4E7u);
}

}

float ChasmPocketDistance(const ChasmLayout::Pocket& k, const vec2& p, float* along){
    //The meadow: a circle with a wobbling edge, so it reads as ground and not as a stamp.
    float wobble = Noise(p,9.0f,PocketSeed(k)) * 0.15f * k.radius;
    float meadow = (p - k.centre).length() - (k.radius + wobble);
    //The valley: a band along its centre line, and how far along it p is.
    float valley = 1e30f;
    float best_along = 0.0f;
    float total = 0.0f;
    for (size_t i = 0; i + 1 < k.valley.size(); i++){
        total += (k.valley[i + 1] - k.valley[i]).length();
    }
    float walked = 0.0f;
    for (size_t i = 0; i + 1 < k.valley.size(); i++){
        vec2 a = k.valley[i];
        vec2 b = k.valley[i + 1];
        vec2 ab = b - a;
        float len = ab.length();
        float t = (len > 1e-6f) ? std::max(0.0f,std::min(1.0f,(p - a).dot(ab) / (len * len))) : 0.0f;
        float d = (p - (a + ab * t)).length() - CHASM_VALLEY_HALF_WIDTH;
        if (d < valley){
            valley = d;
            best_along = (total > 1e-6f) ? (walked + t * len) / total : 0.0f;
        }
        walked += len;
    }
    if (along){
        *along = (valley < meadow) ? best_along : 0.0f;
    }
    return std::min(meadow,valley);
}

/*
    The pockets: one on each side of the main rift that has room for it, and about one map in three a
    second on one side. Each is a meadow standing in the mountain's base depth, kept clear of every rim,
    river and other pocket and clearly to one side of the main tip - so the only way into it is its own
    valley, south through the spur's front onto the plateau on its own side. A side with no room gets
    none; the `pockets` check says how many were wanted and placed.
*/
static void PlacePockets(RRandom& rng, const vec2& lo, const vec2& hi, const std::function<float(float)>& foot,
                         ChasmLayout& out){
    out.pockets.clear();
    std::vector<int> sides = {-1,1};
    if (rng.Roll(0.35f)){
        sides.push_back(rng.Roll(0.5f) ? -1 : 1);
    }
    out.pockets_wanted = (int)sides.size();
    const vec2 tip = out.main_tip;
    std::vector<const GridLine*> rims;
    for (const GridFeature& f : out.features){
        if (f.kind == GRID_FEATURE_RIM){
            rims.push_back(&f.line);
        }
    }
    uint32_t wobble_seed = (uint32_t)rng.GetInt(0,0x7FFFFFFF);
    const int tries = 60;
    for (int side : sides){
        /*
            Across the side in a shuffled sweep rather than at random: every part of the side gets
            tried, so a side with room anywhere gets its pocket. A pocket may stand right beside the
            tongue - the mountain closes it in all the same, and the check proves it.
        */
        float offset = rng.GetFloat(0.0f,1.0f);
        for (int attempt = 0; attempt < tries; attempt++){
            ChasmLayout::Pocket k;
            k.side = side;
            //The last tries settle for a smaller meadow, closer to a rim or river: a small pocket is
            //better than a side with none.
            bool f_last = attempt >= tries * 2 / 3;
            k.radius = f_last ? rng.GetFloat(10.0f,POCKET_RADIUS_MIN) : rng.GetFloat(POCKET_RADIUS_MIN,POCKET_RADIUS_MAX);
            float clear_rim = f_last ? POCKET_CLEAR_RIM * 0.7f : POCKET_CLEAR_RIM;
            float clear_river = f_last ? POCKET_CLEAR_RIVER * 0.7f : POCKET_CLEAR_RIVER;
            //Close under the north edge first; later tries further out, the spur reaching round them -
            //past a river running along the foot, which otherwise shuts a whole side out.
            float depth = rng.GetFloat(14.0f,(attempt < tries / 3) ? 34.0f : 90.0f);
            float valley_len = rng.GetFloat(POCKET_VALLEY_MIN,POCKET_VALLEY_MAX);
            k.floor = rng.GetFloat(4.0f,9.0f);
            float x0, x1;
            float keep_off = k.radius + POCKET_CLEAR_TIP;
            if (side < 0){
                x0 = lo.x + k.radius + 40.0f;
                x1 = tip.x - keep_off;
            }else{
                x0 = tip.x + keep_off;
                x1 = hi.x - k.radius - 40.0f;
            }
            if (x1 <= x0){
                out.pocket_rejects[0]++;
                break;      //no room on this side
            }
            float u = std::fmod(offset + (float)attempt * 0.618034f,1.0f);
            k.centre = vec2(x0 + (x1 - x0) * u,lo.y + k.radius + depth);
            //The valley: due south from the meadow's edge, wandering a little, out a few units past the
            //foot - past the spur it gets, or the tongue's foot if that reaches further.
            k.valley.clear();
            float z_start = k.centre.y + k.radius * 0.6f;
            float z_end = std::max(k.centre.y + k.radius + valley_len,foot(k.centre.x)) + 8.0f;
            if (z_end - (k.centre.y + k.radius) > POCKET_VALLEY_LONGEST + 8.0f){
                out.pocket_rejects[4]++;
                continue;   //too far to climb gently
            }
            for (float z = z_start; ; z += 4.0f){
                z = std::min(z,z_end);
                float sway = Noise(vec2(k.centre.x,z),22.0f,wobble_seed) * 4.0f;
                k.valley.push_back(vec2(k.centre.x + sway,z));
                if (z >= z_end){
                    break;
                }
            }
            bool f_rim = true, f_river = true, f_apart = true;
            for (const GridLine* r : rims){
                f_rim = f_rim && PointsDistance(k.centre,r->points) > k.radius + clear_rim;
                for (const vec2& v : k.valley){
                    f_rim = f_rim && PointsDistance(v,r->points) > clear_rim;
                }
            }
            /*
                Rivers: kept off the meadow and off the valley while it is in the mountain. Out on the
                plateau a valley may cross one - a river parts the land there as it does everywhere,
                until there are bridges - and a river along the mountain's foot would otherwise shut
                that whole side out of pockets.
            */
            for (const GridRiverLine& river : out.rivers){
                f_river = f_river && PointsDistance(k.centre,river.points) > k.radius + clear_river;
                for (const vec2& v : k.valley){
                    if (v.y < foot(v.x)){
                        f_river = f_river && PointsDistance(v,river.points) > clear_river;
                    }
                }
            }
            for (const ChasmLayout::Pocket& other : out.pockets){
                f_apart = f_apart && (other.centre - k.centre).length() > other.radius + k.radius + POCKET_APART;
            }
            out.pocket_rejects[1] += f_rim ? 0 : 1;
            out.pocket_rejects[2] += (f_rim && !f_river) ? 1 : 0;
            out.pocket_rejects[3] += (f_rim && f_river && !f_apart) ? 1 : 0;
            if (f_rim && f_river && f_apart){
                out.pockets.push_back(k);
                break;
            }
        }
    }
}

static void PlaceMountainFoot(uint32_t seed, const vec2& lo, const vec2& hi, ChasmLayout& out){
    RRandom rng((int)(seed * 2246822519u ^ 0x40C7A1Bu));
    rng.Generate(4096);
    uint32_t noise_a = (uint32_t)rng.GetInt(0,0x7FFFFFFF);
    uint32_t noise_b = (uint32_t)rng.GetInt(0,0x7FFFFFFF);
    float half_width = MOUNTAIN_TONGUE_HALF_WIDTH * rng.GetFloat(0.85f,1.2f);
    const float D = hi.y - lo.y;
    const vec2 tip = out.main_tip;
    float tongue_z = std::min(hi.y - 0.2f * D,tip.y + MOUNTAIN_TONGUE_MARGIN);
    //The foot without spurs - base and tongue - which a pocket's valley has to reach past.
    auto foot_at = [&](float x){
        float swing = Noise(vec2(x,0.0f),260.0f,noise_a) * 0.75f + Noise(vec2(x,0.0f),70.0f,noise_b) * 0.25f;
        float base = lo.y + D * (MOUNTAIN_BASE_DEPTH + MOUNTAIN_BASE_SWING * swing);
        float dx = (x - tip.x) / half_width;
        float tongue = tongue_z - (tongue_z - (lo.y + D * MOUNTAIN_BASE_DEPTH)) * dx * dx;
        return -SmoothMin(-base,-tongue,24.0f);
    };
    /*
        Rivers rise at the mountain's foot, not on the north edge behind it: a river's head inside the
        mountain is cut off at the first point it leaves it, less a few units, so it springs from the
        mountain's face - and the mountain is left free for pockets, which a river crossing it had
        blocked on most seeds. A river that never leaves the mountain before its fall (one falling into
        the rift's head, under the tongue) keeps its whole course.
    */
    for (GridRiverLine& river : out.rivers){
        size_t first_out = river.points.size();
        for (size_t i = 0; i < river.points.size(); i++){
            if (river.points[i].y > foot_at(river.points[i].x)){
                first_out = i;
                break;
            }
        }
        if (first_out == 0 || first_out >= river.points.size() - 1){
            continue;
        }
        //Back up to a point a few units inside the mountain, so the spring is in its face.
        size_t keep = first_out;
        while (keep > 0 && foot_at(river.points[keep - 1].x) - river.points[keep - 1].y < MOUNTAIN_SPRING_DEPTH){
            keep--;
        }
        river.points.erase(river.points.begin(),river.points.begin() + (long)keep);
    }
    //After the base's and tongue's draws, so placing pockets left those exactly as they were.
    PlacePockets(rng,lo,hi,foot_at,out);
    out.mountain_foot.clear();
    int n = std::max(2,(int)std::ceil((hi.x - lo.x) / MOUNTAIN_FOOT_STEP) + 1);
    for (int i = 0; i < n; i++){
        float x = lo.x + (hi.x - lo.x) * (float)i / (float)(n - 1);
        //A smooth maximum: south is +z, and the foot is whichever reaches further south.
        float z = foot_at(x);
        //Each pocket's spur: to where its valley leaves the mountain, falling away either side.
        for (const ChasmLayout::Pocket& k : out.pockets){
            float spur_z = k.valley.back().y - 8.0f;
            float sx = (x - k.centre.x) / (k.radius + POCKET_SPUR_SHOULDER);
            float spur = spur_z - (spur_z - (lo.y + D * MOUNTAIN_BASE_DEPTH)) * sx * sx;
            z = -SmoothMin(-z,-spur,16.0f);
        }
        out.mountain_foot.push_back(vec2(x,z));
    }
}

/*
    The south's regions (biomes_plan.md steps 4 and 5), from a stream of their own like the mountain:
    the swamp on the east of the main mouth, the home side, and the desert on the west, the far side
    (Grid.h, ChasmLayout's regions) - each a disc centred a little past the south edge so it hugs it.
    Drawn swamp first, so adding the desert left the swamp as it was. Sized to stay well clear of the
    mountain.
*/
static void PlaceRegion(RRandom& rng, int side, float r0, float r1, const vec2& lo, const vec2& hi,
                        float mouth, ChasmLayout::Region& out){
    const float D = hi.y - lo.y;
    out.side = side;
    out.radius = rng.GetFloat(r0,r1);
    float x = (side < 0) ? lo.x + (mouth - lo.x) * rng.GetFloat(0.3f,0.6f)
                         : mouth + (hi.x - mouth) * rng.GetFloat(0.4f,0.7f);
    out.centre = vec2(x,hi.y + out.radius * 0.15f);
    //Never up to the mountain: at most halfway up the map.
    out.radius = std::min(out.radius,0.5f * D);
}

static void PlaceSouth(uint32_t seed, const vec2& lo, const vec2& hi, ChasmLayout& out){
    RRandom rng((int)(seed * 3266489917u ^ 0x5A3B0Fu));
    rng.Generate(1024);
    //The draw that once chose the sides, kept so the regions' sizes and places stay as they were.
    (void)rng.Roll(0.5f);
    PlaceRegion(rng,1,110.0f,160.0f,lo,hi,out.main_mouth_x,out.swamp);
    PlaceRegion(rng,-1,120.0f,170.0f,lo,hi,out.main_mouth_x,out.desert);
}

static float RegionMask(const ChasmLayout::Region& s, const vec2& p, uint32_t salt){
    if (s.radius <= 0.0f){
        return 0.0f;
    }
    float wobble = Noise(p,46.0f,salt) * 0.6f + Noise(p,17.0f,salt + 1) * 0.4f;
    float d = (p - s.centre).length() + wobble * 18.0f;
    float t = std::max(0.0f,std::min(1.0f,(s.radius - d) / CHASM_REGION_EDGE));
    return t * t * (3.0f - 2.0f * t);
}

float ChasmSwampMask(const ChasmLayout& layout, const vec2& p){
    return RegionMask(layout.swamp,p,0x5A3B1u);
}

float ChasmDesertMask(const ChasmLayout& layout, const vec2& p){
    return RegionMask(layout.desert,p,0xDE5E7u);
}

float ChasmRiftDistance(const ChasmLayout& layout, const vec2& p){
    const ChasmLayout::Raster& r = layout.rift;
    if (r.w < 2 || r.h < 2){
        return 1e3f;
    }
    //Field::Sample's arithmetic exactly, so a plot reads what the layout's own tests read.
    float cx = (r.hi.x - r.lo.x) / (float)(r.w - 1);
    float cz = (r.hi.y - r.lo.y) / (float)(r.h - 1);
    float fx = std::max(0.0f,std::min((float)(r.w - 1) - 0.001f,(p.x - r.lo.x) / cx));
    float fz = std::max(0.0f,std::min((float)(r.h - 1) - 0.001f,(p.y - r.lo.y) / cz));
    int ix = (int)fx;
    int iz = (int)fz;
    float tx = fx - ix;
    float tz = fz - iz;
    auto at = [&r](int i, int j){ return r.d[(size_t)j * r.w + i]; };
    float a = at(ix,iz) + (at(ix + 1,iz) - at(ix,iz)) * tx;
    float b = at(ix,iz + 1) + (at(ix + 1,iz + 1) - at(ix,iz + 1)) * tx;
    return a + (b - a) * tz;
}

float ChasmMountainFootAt(const ChasmLayout& layout, float x){
    const std::vector<vec2>& f = layout.mountain_foot;
    if (f.empty()){
        return -INFINITY;
    }
    if (x <= f.front().x){
        return f.front().y;
    }
    if (x >= f.back().x){
        return f.back().y;
    }
    float t = (x - f.front().x) / (f.back().x - f.front().x) * (float)(f.size() - 1);
    size_t i = std::min(f.size() - 2,(size_t)t);
    float u = t - (float)i;
    return f[i].y + (f[i + 1].y - f[i].y) * u;
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
    int want_ledges = rng.GetInt(0,4);
    int want_shards = rng.GetInt(0,4);
    int want_columns = rng.GetInt(0,5);
    int want_balconies_east = rng.GetInt(1,2);
    int want_balconies_west = rng.GetInt(0,2);
    b.f_ledged = want_ledges > 0;
    std::vector<GridFeature> rims;
    std::vector<GridFeature> placed;
    /*
        Rifts are drawn until they keep their spacing, leave each side one piece, and take a balcony
        on the home side - so the guarantee is in the drawing, not hoped for after it.
    */
    const int max_attempts = 30;
    out.f_rifts_ok = false;
    for (int attempt = 1; attempt <= max_attempts; attempt++){
        out.attempts = attempt;
        if (!b.TryRifts(rims)){
            continue;
        }
        placed = rims;
        b.PlaceBalconies(want_balconies_west,want_balconies_east,placed,(int)rims.size());
        if (out.balconies[1] > 0){
            out.f_rifts_ok = true;
            break;
        }
    }
    if (!out.f_rifts_ok && placed.empty()){
        placed = rims;
    }
    out.count[GRID_FEATURE_RIM] = (int)rims.size();
    if (!rims.empty()){
        b.PlaceLedges(want_ledges,placed,rims);
    }
    b.PlaceBlobs(GRID_FEATURE_SHARD,want_shards,placed);
    b.PlaceBlobs(GRID_FEATURE_COLUMN,want_columns,placed);
    out.features = placed;
    out.rift.lo = b.rift.lo;
    out.rift.hi = b.rift.hi;
    out.rift.w = b.rift.w;
    out.rift.h = b.rift.h;
    out.rift.d = b.rift.d;
    b.PlaceRivers(out.features);
    PlaceMountainFoot(seed,lo,hi,out);
    PlaceSouth(seed,lo,hi,out);
    out.generate_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    return out;
}
