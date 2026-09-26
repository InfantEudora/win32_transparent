#include <math.h>
#include <string.h>
#include <algorithm>
#include <chrono>

#include "Wind.h"

/*
    Like Stage.cpp, nothing in here includes an engine header - `make rules` links it into
    stage_test.exe with no window and no GPU.
*/

//Gust fronts travel faster than the mean wind. A front that moved WITH the air would carry the
//same leaves along forever; one that overtakes them rolls through the grass as a visible wave.
#define WIND_GUST_TRAVEL        1.5f
//A Gaussian psi bump A*exp(-r^2/s^2) peaks in speed at r = s/sqrt(2), where the speed is
//A * sqrt(2)/s * exp(-1/2). This is that factor, used to turn a wanted swirl speed into an A.
#define WIND_GAUSS_PEAK         0.8578f
//The solver stops when no node moved by more than this, relative to the lid's psi.
#define WIND_SOLVE_TOLERANCE    1e-5f
#define WIND_SOLVE_MAX_ITER     20000
//The level is cut this far below its lowest walkable top - everything lower is ground to the wind.
#define WIND_FLOOR_BELOW        3.0f

static bool BlocksWind(const StageBlock& b){
    if (!b.f_alive){
        return false;
    }
    return (b.kind == BLOCK_SOLID) || (b.kind == BLOCK_LEDGE) || (b.kind == BLOCK_BREAKABLE);
}

static float BoxDistance(float px, float py, const StageBlock& b){
    float dx = fabsf(px - b.x) - b.hw;
    float dy = fabsf(py - b.y) - b.hh;
    float ox = fmaxf(dx,0.0f);
    float oy = fmaxf(dy,0.0f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(dx,dy),0.0f);
}

//An integer hash for the per-gust and per-corner draws - a function of WHICH, never a stream.
static float WindHash01(int64_t a, int b){
    uint32_t h = (uint32_t)(a & 0xFFFFFFFF) * 374761393u + (uint32_t)(a >> 32) * 668265263u;
    h += (uint32_t)b * 2246822519u;
    h = (h ^ (h >> 15)) * 2246822519u;
    h = (h ^ (h >> 13)) * 3266489917u;
    h = h ^ (h >> 16);
    return (float)(h & 0xFFFFFF) / (float)0x1000000;
}

static uint64_t HashBytes(uint64_t h, const void* data, size_t n){
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++){
        h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

static float Smooth01(float e0, float e1, float x){
    float t = fminf(fmaxf((x - e0) / (e1 - e0),0.0f),1.0f);
    return t * t * (3.0f - 2.0f * t);
}

//Bridson's ramp: 0 at a surface, 1 from `1` on, with zero slope there so the join does not show.
static void Ramp(float q, float& r, float& dr){
    if (q >= 1.0f){
        r = 1.0f;
        dr = 0.0f;
        return;
    }
    if (q <= 0.0f){
        r = 0.0f;
        dr = 0.0f;
        return;
    }
    float q2 = q * q;
    r  = q * (15.0f / 8.0f - q2 * (10.0f / 8.0f - q2 * (3.0f / 8.0f)));
    dr = 15.0f / 8.0f - q2 * (30.0f / 8.0f - q2 * (15.0f / 8.0f));
}

//--- Build ---------------------------------------------------------------------------------------

bool WindField::Build(const std::vector<StageBlock>& blocks, const WindParams& in){
    auto t_start = std::chrono::steady_clock::now();

    std::vector<StageBlock> candidates, obstacles;
    float ex0 = 1e9f, ex1 = -1e9f;
    int end_walls = 0;
    for (const StageBlock& b : blocks){
        if (BlocksWind(b)){
            candidates.push_back(b);
            ex0 = fminf(ex0,b.Left());
            ex1 = fmaxf(ex1,b.Right());
        }
    }
    /*
        A tall, thin block at the very end of the level is its BOUNDARY - the wall that stops her -
        not terrain the wind flows over. Left in, the extrusion turns it into a cliff as high as the
        wall that the whole wind has to climb: the main level's 20-high end wall made a jet of 4.6x
        the wind over its top, and the range's 48-high pair sealed it into a box of still air.
        Tall-and-thin is what tells it from the ground slab, which also reaches the end.
    */
    for (const StageBlock& b : candidates){
        bool f_at_end = (b.Left() <= ex0 + 0.01f) || (b.Right() >= ex1 - 0.01f);
        if (f_at_end && (b.hh > 2.0f * b.hw)){
            end_walls++;
            continue;
        }
        obstacles.push_back(b);
    }

    uint64_t h = 1469598103934665603ull;
    for (const StageBlock& b : obstacles){
        float f[4] = {b.x,b.y,b.hw,b.hh};
        h = HashBytes(h,f,sizeof(f));
    }
    float g[6] = {in.cell,in.margin_x,in.lid_above,in.wall_ramp,in.eddy_min_drop,in.eddy_max_drop};
    h = HashBytes(h,g,sizeof(g));
    if (IsBuilt() && (h == built_hash)){
        SetParams(in);
        return false;
    }

    params = in;
    built_hash = h;
    stats = WindStats();
    stats.obstacles = (int)obstacles.size();
    stats.end_walls = end_walls;
    nx = ny = 0;
    corners_pos.clear();
    corners_neg.clear();
    if (obstacles.empty()){
        return true;
    }

    //The domain: the level plus a margin each side, from a little under its lowest walkable top
    //up to the lid. Below the lowest top everything is ground as far as the wind is concerned.
    float bx0 = 1e9f, bx1 = -1e9f, by1 = -1e9f, min_top = 1e9f, by0 = 1e9f;
    for (const StageBlock& b : obstacles){
        bx0 = fminf(bx0,b.Left());
        bx1 = fmaxf(bx1,b.Right());
        by0 = fminf(by0,b.Bottom());
        by1 = fmaxf(by1,b.Top());
        min_top = fminf(min_top,b.Top());
    }
    cell = fmaxf(params.cell,0.05f);
    level_x0 = bx0;
    level_x1 = bx1;
    x0 = bx0 - params.margin_x;
    y0 = fmaxf(by0,min_top - WIND_FLOOR_BELOW);
    float x1 = bx1 + params.margin_x;
    float y1 = by1 + fmaxf(params.lid_above,2.0f * cell);
    nx = (int)ceilf((x1 - x0) / cell) + 1;
    ny = (int)ceilf((y1 - y0) / cell) + 1;
    stats.nx = nx;
    stats.ny = ny;

    BuildDistance(obstacles);
    SolveMeanFlow();
    FindCorners(+1,corners_pos);
    FindCorners(-1,corners_neg);
    stats.corners = (int)Corners().size();

    stats.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t_start).count();
    return true;
}

void WindField::SetParams(const WindParams& in){
    params.speed         = in.speed;
    params.eddy_strength = in.eddy_strength;
    params.eddy_strouhal = in.eddy_strouhal;
    params.wave_strength = in.wave_strength;
    params.wave_length   = in.wave_length;
    params.gust_strength = in.gust_strength;
    params.gust_width    = in.gust_width;
    params.gust_period   = in.gust_period;
    stats.corners = (int)Corners().size();
}

void WindField::BuildDistance(const std::vector<StageBlock>& obstacles){
    int n = nx * ny;
    solid.assign(n,0);
    dist.assign(n,0.0f);
    dist_dx.assign(n,0.0f);
    dist_dy.assign(n,0.0f);

    //The ends are extruded: a node past the last block measures from the column just inside it.
    //Clamped a hair INSIDE, or a node level with the end block would sit exactly on its side face
    //and read as surface rather than solid.
    const float inset = 1e-3f;
    for (int j = 0; j < ny; j++){
        float y = y0 + j * cell;
        for (int i = 0; i < nx; i++){
            float x = fminf(fmaxf(x0 + i * cell,level_x0 + inset),level_x1 - inset);
            float d = 1e9f;
            for (const StageBlock& b : obstacles){
                d = fminf(d,BoxDistance(x,y,b));
            }
            //The bottom row is ground whatever is there - a pit with no floor gets one, which
            //is what the air above it would feel anyway.
            if (j == 0){
                d = fminf(d,0.0f);
            }
            dist[Index(i,j)] = d;
            solid[Index(i,j)] = (d <= 0.0f) ? 1 : 0;
        }
    }
    for (int j = 0; j < ny; j++){
        for (int i = 0; i < nx; i++){
            int il = std::max(i - 1,0), ir = std::min(i + 1,nx - 1);
            int jd = std::max(j - 1,0), ju = std::min(j + 1,ny - 1);
            dist_dx[Index(i,j)] = (dist[Index(ir,j)] - dist[Index(il,j)]) / ((ir - il) * cell);
            dist_dy[Index(i,j)] = (dist[Index(i,ju)] - dist[Index(i,jd)]) / ((ju - jd) * cell);
        }
    }
}

/*
    Potential flow for a wind of 1. Laplace's equation for psi on the fluid nodes, with:

      - the GROUND (every solid connected to the bottom row) at psi 0;
      - a FLOATING solid at the psi the undisturbed flow would have at its centre, so the flow
        splits around it rather than all going over or all under;
      - the LID (top row) at the total flux, (lid - mean ground height) * 1;
      - the two ENDS open: dpsi/dx = 0, so the flow enters and leaves level.

    Started from the undisturbed flow (psi rising linearly from each column's ground to the lid),
    so what SOR has to find is only the disturbance around the blocks, which is local.
*/
void WindField::SolveMeanFlow(){
    int n = nx * ny;
    std::vector<int> comp(n,-1);
    std::vector<int> stack;
    std::vector<float> comp_x, comp_y;
    std::vector<int> comp_count;
    int ncomp = 0;
    for (int start = 0; start < n; start++){
        if (!solid[start] || (comp[start] >= 0)){
            continue;
        }
        comp_x.push_back(0.0f);
        comp_y.push_back(0.0f);
        comp_count.push_back(0);
        stack.push_back(start);
        comp[start] = ncomp;
        while (!stack.empty()){
            int k = stack.back();
            stack.pop_back();
            int i = k % nx, j = k / nx;
            comp_x[ncomp] += x0 + i * cell;
            comp_y[ncomp] += y0 + j * cell;
            comp_count[ncomp]++;
            int nb[4] = {(i > 0) ? k - 1 : -1, (i < nx - 1) ? k + 1 : -1,
                         (j > 0) ? k - nx : -1, (j < ny - 1) ? k + nx : -1};
            for (int m : nb){
                if ((m >= 0) && solid[m] && (comp[m] < 0)){
                    comp[m] = ncomp;
                    stack.push_back(m);
                }
            }
        }
        ncomp++;
    }
    stats.components = ncomp;
    //The bottom row is all solid by construction, so node 0 is in the ground.
    int ground = comp[0];

    //Each column's ground surface: the top of the run of ground rising from the bottom row.
    std::vector<float> surf(nx);
    float surf_sum = 0.0f;
    for (int i = 0; i < nx; i++){
        int j = 0;
        while ((j < ny - 1) && (comp[Index(i,j)] == ground)){
            j++;
        }
        surf[i] = y0 + (j - 0.5f) * cell;
        surf_sum += surf[i];
    }
    float y_lid = y0 + (ny - 1) * cell;
    float psi_lid = y_lid - surf_sum / nx;

    auto undisturbed = [&](int i, float y) -> float {
        float t = (y - surf[i]) / fmaxf(y_lid - surf[i],cell);
        return psi_lid * fminf(fmaxf(t,0.0f),1.0f);
    };

    std::vector<float> comp_psi(ncomp,0.0f);
    for (int c = 0; c < ncomp; c++){
        if (c == ground){
            continue;
        }
        float cx = comp_x[c] / comp_count[c];
        float cy = comp_y[c] / comp_count[c];
        int ci = std::min(std::max((int)lroundf((cx - x0) / cell),0),nx - 1);
        comp_psi[c] = undisturbed(ci,cy);
    }

    psi.assign(n,0.0f);
    for (int j = 0; j < ny; j++){
        for (int i = 0; i < nx; i++){
            int k = Index(i,j);
            psi[k] = solid[k] ? comp_psi[comp[k]] : undisturbed(i,y0 + j * cell);
        }
    }
    for (int i = 0; i < nx; i++){
        psi[Index(i,ny - 1)] = psi_lid;
    }

    //Over-relaxation factor: the textbook optimum for Laplace on a grid this size.
    float omega = 2.0f / (1.0f + sinf(3.14159265f / (float)std::max(nx,ny)));
    float tol = WIND_SOLVE_TOLERANCE * fmaxf(psi_lid,1e-3f);
    int it = 0;
    float max_change = 0.0f;
    for (; it < WIND_SOLVE_MAX_ITER; it++){
        max_change = 0.0f;
        for (int j = 1; j < ny - 1; j++){
            float* row = &psi[Index(0,j)];
            const float* up = row + nx;
            const float* dn = row - nx;
            const uint8_t* srow = &solid[Index(0,j)];
            for (int i = 0; i < nx; i++){
                if (srow[i]){
                    continue;
                }
                //Open ends: a mirrored ghost node, which is what dpsi/dx = 0 means on a grid.
                float l = (i > 0) ? row[i - 1] : row[i + 1];
                float r = (i < nx - 1) ? row[i + 1] : row[i - 1];
                float d = omega * (0.25f * (l + r + up[i] + dn[i]) - row[i]);
                row[i] += d;
                max_change = fmaxf(max_change,fabsf(d));
            }
        }
        if (max_change < tol){
            break;
        }
    }
    stats.iterations = it;
    stats.residual = max_change / fmaxf(psi_lid,1e-3f);

    mean_u.assign(n,0.0f);
    mean_v.assign(n,0.0f);
    for (int j = 0; j < ny; j++){
        for (int i = 0; i < nx; i++){
            int k = Index(i,j);
            if (solid[k]){
                continue;
            }
            int il = std::max(i - 1,0), ir = std::min(i + 1,nx - 1);
            int jd = std::max(j - 1,0), ju = std::min(j + 1,ny - 1);
            mean_u[k] =  (psi[Index(i,ju)] - psi[Index(i,jd)]) / ((ju - jd) * cell);
            mean_v[k] = -(psi[Index(ir,j)] - psi[Index(il,j)]) / ((ir - il) * cell);
        }
    }
}

/*
    A shedding corner, for wind blowing toward `dir`: a solid node with open air above it and
    beside it on the downwind side, where the ground then falls at least eddy_min_drop. A vertical
    wall yields only its top node (the rest have solid above); a staircase yields one per step, and
    corners closer than a couple of units merge into the one with the bigger drop.
*/
void WindField::FindCorners(int dir, std::vector<WindCorner>& out) const {
    out.clear();
    for (int j = 1; j < ny - 1; j++){
        for (int i = 0; i < nx; i++){
            int id = i + dir;
            if ((id < 0) || (id >= nx)){
                continue;
            }
            if (!solid[Index(i,j)] || solid[Index(i,j + 1)] || solid[Index(id,j)] || solid[Index(id,j + 1)]){
                continue;
            }
            float cx = x0 + (i + 0.5f * dir) * cell;
            if ((cx < level_x0) || (cx > level_x1)){
                continue;
            }
            int k = j;
            while ((k > 0) && !solid[Index(id,k)]){
                k--;
            }
            /*
                The eddy is sized by the smaller of the drop and the FACE - how far down the solid
                side stays exposed to the air beside it. For a step on the ground the two are the
                same. For a floating island the drop is everything down to the ground, and an
                eddy that size hung in open air far below the island; its wake is really the
                height of its own downwind face.
            */
            int f = j;
            while ((f > 0) && solid[Index(i,f)] && !solid[Index(id,f)]){
                f--;
            }
            //Every end is placed off the distance field from the nearest open node, not by
            //counting nodes: a node exactly on a surface counts as solid, and counting read a
            //1.5-thick slab as 2.0.
            //The top face lies between node j and the open node above it.
            float top = (y0 + (j + 1) * cell) - fmaxf(dist[Index(i,j + 1)],0.0f);
            //The ground downwind lies below the lowest open node of that column.
            float ground = (y0 + (k + 1) * cell) - fmaxf(dist[Index(id,k + 1)],0.0f);
            //The face ends either where the block's own underside is (an open node under it)
            //or where the ground beside it closes it off.
            float bottom = solid[Index(i,f)] ? (y0 + (f + 1) * cell) - fmaxf(dist[Index(id,f + 1)],0.0f)
                                             : (y0 + f * cell) + fmaxf(dist[Index(i,f)],0.0f);
            float drop = fminf(top - ground,top - bottom);
            if (drop < params.eddy_min_drop){
                continue;
            }
            WindCorner c;
            c.x = cx;
            c.y = top;
            c.drop = fminf(drop,params.eddy_max_drop);
            out.push_back(c);
        }
    }

    //Merge neighbours, keeping the bigger drop.
    const float merge = 2.0f;
    std::vector<WindCorner> kept;
    for (const WindCorner& c : out){
        bool f_merged = false;
        for (WindCorner& o : kept){
            if ((fabsf(o.x - c.x) < merge) && (fabsf(o.y - c.y) < merge)){
                if (c.drop > o.drop){
                    o = c;
                }
                f_merged = true;
                break;
            }
        }
        if (!f_merged){
            kept.push_back(c);
        }
    }
    for (WindCorner& c : kept){
        c.seed = WindHash01((int64_t)lroundf(c.x * 10.0f) * 7919 + lroundf(c.y * 10.0f),dir + 5);
    }
    out.swap(kept);
}

//--- Evaluation ----------------------------------------------------------------------------------

float WindField::Sample(const std::vector<float>& a, float x, float y) const {
    float fx = fminf(fmaxf((x - x0) / cell,0.0f),(float)(nx - 1) - 1e-4f);
    float fy = fminf(fmaxf((y - y0) / cell,0.0f),(float)(ny - 1) - 1e-4f);
    int i = (int)fx, j = (int)fy;
    float tx = fx - i, ty = fy - j;
    int k = Index(i,j);
    float a0 = a[k] + (a[k + 1] - a[k]) * tx;
    float a1 = a[k + nx] + (a[k + nx + 1] - a[k + nx]) * tx;
    return a0 + (a1 - a0) * ty;
}

float WindField::Distance(float x, float y) const {
    return IsBuilt() ? Sample(dist,x,y) : 1e9f;
}

WindVec WindField::DistanceGradient(float x, float y) const {
    WindVec g;
    if (IsBuilt()){
        g.x = Sample(dist_dx,x,y);
        g.y = Sample(dist_dy,x,y);
    }else{
        g.y = 1.0f;
    }
    return g;
}

WindVec WindField::MeanFlow(float x, float y) const {
    WindVec v;
    if (!IsBuilt()){
        v.x = params.speed;
        return v;
    }
    v.x = params.speed * Sample(mean_u,x,y);
    v.y = params.speed * Sample(mean_v,x,y);
    return v;
}

/*
    The three eddies of one corner.

    [0] is the BOUND eddy - the recirculation bubble that sits in the lee of a real step and stays
    there, about a drop downwind and half a drop up. It has to be the strong one: potential flow
    alone runs close to the full wind along the ground behind a step (it is symmetric fore and aft,
    and knows nothing of separation), and only a vortex held in place turns that back. It breathes
    a little at half the shedding rate, so it never looks painted on.

    [1] and [2] are SHED: each lives one shedding cycle, born at the back of the bubble, drifting
    two drops further while rising a little, with strength sin^2 of its phase. Half a cycle apart,
    and sin^2 + cos^2 = 1, so the wake's total swirl is steady while the blobs come and go.

    The sign makes all three turn the way a lee eddy does: over the top WITH the wind, back toward
    the wall along the ground.
*/
void WindField::CornerEddies(const WindCorner& c, int dir, int64_t tick, WindEddy out[WIND_EDDIES_PER_CORNER]) const {
    float speed = fabsf(params.speed);
    float h = c.drop;
    float period_ticks = h / (fmaxf(params.eddy_strouhal,0.01f) * fmaxf(speed,0.05f) * ARCHER_DT);
    //In double: a long session's tick count in float would lose the phase.
    double base = (double)tick / (double)period_ticks + (double)c.seed;
    float swirl = -(float)dir * params.eddy_strength * speed;

    /*
        The bound eddy is an ELLIPSE, twice as long as it is tall: a real recirculation bubble
        runs 2-3 drops downstream and about one drop high. As a circle it ended 1.4 drops out, and
        leaves carried over it came down beyond the reverse flow and settled there instead of
        being drawn back round. Its strength is set by the VERTICAL radius, so the reverse flow
        along the ground peaks at `swirl`, and the up- and down-draughts at its ends are gentler.
    */
    float bound_sigma = fminf(fmaxf(0.5f * h,0.5f),2.2f);
    float breathe = 1.0f + 0.2f * sinf(3.14159265f * (float)(base - floor(base)));
    out[0].x = c.x + dir * 1.2f * h;
    out[0].y = c.y - 0.55f * h;
    out[0].radius = bound_sigma;
    out[0].stretch = 2.0f;
    out[0].strength = swirl * breathe * bound_sigma / WIND_GAUSS_PEAK;

    //The shed ones leave from the back of the bubble.
    float shed_sigma = fminf(fmaxf(0.4f * h,0.5f),1.8f);
    for (int k = 1; k < WIND_EDDIES_PER_CORNER; k++){
        double b = base + 0.5 * (k - 1);
        float p = (float)(b - floor(b));
        float s = sinf(3.14159265f * p);
        out[k].x = c.x + dir * h * (2.2f + 2.0f * p);
        out[k].y = c.y - h * (0.45f - 0.2f * p);
        out[k].radius = shed_sigma;
        out[k].stretch = 1.3f;
        out[k].strength = 0.5f * swirl * s * s * shed_sigma / WIND_GAUSS_PEAK;
    }

    /*
        An eddy needs ROOM. Placed by rule, one can land against another block, or inside one -
        a step with a floating slab just downwind of it - and the wall ramp then squashes it into a
        thin sheet of fast flow along that wall: 21 units/s in a 2.5 wind on the main level. So
        each fades out as the space around its centre drops below its own radius.
    */
    //Measured against the vertical radius: the long axis runs along the ground by design.
    for (int k = 0; k < WIND_EDDIES_PER_CORNER; k++){
        float room = Sample(dist,out[k].x,out[k].y);
        out[k].strength *= Smooth01(0.25f * out[k].radius,out[k].radius,room);
    }
}

void WindField::Eddies(int64_t tick, std::vector<WindEddy>& out) const {
    int dir = (params.speed >= 0.0f) ? 1 : -1;
    for (const WindCorner& c : Corners()){
        WindEddy e[WIND_EDDIES_PER_CORNER];
        CornerEddies(c,dir,tick,e);
        out.insert(out.end(),e,e + WIND_EDDIES_PER_CORNER);
    }
}

//One Gaussian blob's psi and gradient, added in - elliptical, radius * stretch across. Nothing
//past three radii.
static inline void AddEddyPsi(const WindEddy& e, float x, float y, float& p, float& px, float& py){
    float dx = x - e.x, dy = y - e.y;
    float isy2 = 1.0f / (e.radius * e.radius);
    float isx2 = isy2 / (e.stretch * e.stretch);
    float r2 = dx * dx * isx2 + dy * dy * isy2;
    if (r2 > 9.0f){
        return;
    }
    float g = e.strength * expf(-r2);
    p  += g;
    px += g * (-2.0f * dx * isx2);
    py += g * (-2.0f * dy * isy2);
}

void WindField::DetailPsi(float x, float y, int64_t tick, float& p, float& px, float& py) const {
    p = px = py = 0.0f;
    float speed = fabsf(params.speed);
    if (speed < 1e-4f){
        return;
    }
    int dir = (params.speed >= 0.0f) ? 1 : -1;

    if (params.eddy_strength > 0.0f){
        for (const WindCorner& c : Corners()){
            //Cheap reject before the pair is worked out: nothing of this corner's reaches past
            //about three drops downwind or two either other way.
            float rx = (x - c.x) * dir;
            if ((rx < -2.0f * c.drop) || (rx > 6.0f * c.drop + 6.0f) || (fabsf(y - c.y) > 2.0f * c.drop + 6.0f)){
                continue;
            }
            WindEddy e[WIND_EDDIES_PER_CORNER];
            CornerEddies(c,dir,tick,e);
            for (int k = 0; k < WIND_EDDIES_PER_CORNER; k++){
                AddEddyPsi(e[k],x,y,p,px,py);
            }
        }
    }
    WavePsi(x,y,tick,p,px,py);
}

/*
    Three waves across one another, drifting downwind at 0.8 of the wind and each also evolving
    slowly on its own, so the pattern never simply translates. Wave k's phase at (x, y) is
    kx * x + ky * y + offset - SEPARABLE, which is what lets Bake take the sines per column and
    per row instead of per node.
*/
int WindField::Waves(int64_t tick, WindWave out[WIND_WAVES]) const {
    float speed = fabsf(params.speed);
    if ((speed < 1e-4f) || (params.wave_strength <= 0.0f)){
        return 0;
    }
    static const float angle[WIND_WAVES]  = {0.26f,1.22f,2.18f};
    static const float length[WIND_WAVES] = {1.0f,0.62f,0.38f};
    static const float weight[WIND_WAVES] = {1.0f,0.7f,0.5f};
    static const float phase[WIND_WAVES]  = {0.0f,2.1f,4.4f};
    float t = (float)fmod((double)tick * ARCHER_DT,100000.0);
    float drift = 0.8f * params.speed * t;
    for (int k = 0; k < WIND_WAVES; k++){
        float kk = 6.2831853f / (fmaxf(params.wave_length,0.5f) * length[k]);
        out[k].kx = kk * cosf(angle[k]);
        out[k].ky = kk * sinf(angle[k]);
        out[k].amp = params.wave_strength * speed * weight[k] / (kk * 2.2f);
        out[k].offset = -out[k].kx * drift + phase[k] + 0.15f * (k + 1) * t;
    }
    return WIND_WAVES;
}

void WindField::WavePsi(float x, float y, int64_t tick, float& p, float& px, float& py) const {
    WindWave w[WIND_WAVES];
    int n = Waves(tick,w);
    for (int k = 0; k < n; k++){
        float arg = w[k].kx * x + w[k].ky * y + w[k].offset;
        float s = sinf(arg), co = cosf(arg);
        p  += w[k].amp * s;
        px += w[k].amp * co * w[k].kx;
        py += w[k].amp * co * w[k].ky;
    }
}

float WindField::GustFactor(float x, int64_t tick) const {
    float speed = fabsf(params.speed);
    if ((speed < 0.05f) || (params.gust_strength <= 0.0f)){
        return 1.0f;
    }
    int dir = (params.speed >= 0.0f) ? 1 : -1;
    float w = fmaxf(params.gust_width,1.0f);
    int64_t period = std::max(params.gust_period,30);
    float lo = IsBuilt() ? MinX() : x - 100.0f;
    float hi = IsBuilt() ? MaxX() : x + 100.0f;
    float front_speed = WIND_GUST_TRAVEL * speed * ARCHER_DT;       //units per tick
    int64_t travel = (int64_t)((hi - lo + 2.0f * w) / front_speed) + 1;

    float sum = 0.0f;
    int64_t kmax = (tick + period) / period + 1;
    int64_t kmin = (tick - travel) / period - 2;
    for (int64_t k = kmin; k <= kmax; k++){
        float jitter = WindHash01(k,1) - 0.5f;
        int64_t start = k * period + (int64_t)(jitter * 0.6f * (float)period);
        if (start > tick){
            continue;
        }
        float front = ((dir > 0) ? lo - 0.3f * w : hi + 0.3f * w) + dir * front_speed * (float)(tick - start);
        //How far behind the front this point is: a sharp rise, then a long tail.
        float s = (front - x) * dir;
        if ((s <= 0.0f) || (s >= w)){
            continue;
        }
        float shape = Smooth01(0.0f,0.25f * w,s) * (1.0f - Smooth01(0.25f * w,w,s));
        sum += params.gust_strength * (0.4f + 0.6f * WindHash01(k,2)) * shape;
    }
    return 1.0f + sum;
}

WindVec WindField::Velocity(float x, float y, int64_t tick) const {
    float gust = GustFactor(x,tick);
    WindVec v = MeanFlow(x,y);
    if (IsBuilt()){
        float d = Sample(dist,x,y);
        if (d > 0.0f){
            float r, dr;
            Ramp(d / fmaxf(params.wall_ramp,1e-3f),r,dr);
            dr /= fmaxf(params.wall_ramp,1e-3f);
            float p, px, py;
            DetailPsi(x,y,tick,p,px,py);
            //The gradient of ramp * psi, by the product rule. The psi * ramp' term is what turns
            //an eddy's squashed edge into flow ALONG the wall rather than into it.
            float gx = r * px + p * dr * Sample(dist_dx,x,y);
            float gy = r * py + p * dr * Sample(dist_dy,x,y);
            v.x += gy;
            v.y -= gx;
        }
    }
    v.x *= gust;
    v.y *= gust;
    return v;
}

/*
    The field on a grid, for the renderer. The same sum Velocity() makes, but everything that
    depends only on the tick is worked out ONCE - the eddies (with their room check), and the
    gust per column - instead of once per sample. Velocity() pays ~1 us a sample mostly in exactly
    that; a view-sized grid of a few thousand nodes has to cost well under a millisecond.
*/
void WindField::Bake(int64_t tick, float bx, float by, float step, int w, int h, float* out) const {
    std::vector<WindEddy> eddies;
    if (IsBuilt() && (fabsf(params.speed) >= 1e-4f) && (params.eddy_strength > 0.0f)){
        Eddies(tick,eddies);
        //Only those that reach the grid.
        float x1 = bx + (w - 1) * step, y1 = by + (h - 1) * step;
        size_t kept = 0;
        for (const WindEddy& e : eddies){
            float rx = 3.0f * e.radius * e.stretch, ry = 3.0f * e.radius;
            if ((e.strength != 0.0f) && (e.x + rx >= bx) && (e.x - rx <= x1) && (e.y + ry >= by) && (e.y - ry <= y1)){
                eddies[kept++] = e;
            }
        }
        eddies.resize(kept);
    }
    std::vector<float> gust(w);
    for (int i = 0; i < w; i++){
        gust[i] = GustFactor(bx + i * step,tick);
    }
    //sin/cos of each wave's x part per column and its y part (with the offset) per row; a node
    //then needs only the angle-sum identities.
    WindWave waves[WIND_WAVES];
    int nwaves = Waves(tick,waves);
    std::vector<float> col_sc(2 * WIND_WAVES * w), row_sc(2 * WIND_WAVES * h);
    for (int k = 0; k < nwaves; k++){
        for (int i = 0; i < w; i++){
            float a = waves[k].kx * (bx + i * step);
            col_sc[2 * (k * w + i)] = sinf(a);
            col_sc[2 * (k * w + i) + 1] = cosf(a);
        }
        for (int j = 0; j < h; j++){
            float a = waves[k].ky * (by + j * step) + waves[k].offset;
            row_sc[2 * (k * h + j)] = sinf(a);
            row_sc[2 * (k * h + j) + 1] = cosf(a);
        }
    }
    float inv_ramp = 1.0f / fmaxf(params.wall_ramp,1e-3f);
    for (int j = 0; j < h; j++){
        float y = by + j * step;
        for (int i = 0; i < w; i++){
            float x = bx + i * step;
            WindVec v = MeanFlow(x,y);
            if (IsBuilt()){
                float d = Sample(dist,x,y);
                if (d > 0.0f){
                    float r, dr;
                    Ramp(d * inv_ramp,r,dr);
                    dr *= inv_ramp;
                    float p = 0.0f, px = 0.0f, py = 0.0f;
                    for (const WindEddy& e : eddies){
                        AddEddyPsi(e,x,y,p,px,py);
                    }
                    for (int k = 0; k < nwaves; k++){
                        float sa = col_sc[2 * (k * w + i)], ca = col_sc[2 * (k * w + i) + 1];
                        float sb = row_sc[2 * (k * h + j)], cb = row_sc[2 * (k * h + j) + 1];
                        float sn = sa * cb + ca * sb, cs = ca * cb - sa * sb;
                        p  += waves[k].amp * sn;
                        px += waves[k].amp * cs * waves[k].kx;
                        py += waves[k].amp * cs * waves[k].ky;
                    }
                    float gx = r * px + p * dr * Sample(dist_dx,x,y);
                    float gy = r * py + p * dr * Sample(dist_dy,x,y);
                    v.x += gy;
                    v.y -= gx;
                }
            }
            out[2 * (j * w + i)]     = v.x * gust[i];
            out[2 * (j * w + i) + 1] = v.y * gust[i];
        }
    }
}

void WindField::TraceStreamline(float x, float y, int64_t tick, float step, int max_points, std::vector<float>& out) const {
    if (!IsBuilt()){
        return;
    }
    float stall = 0.02f * fmaxf(fabsf(params.speed),0.05f);
    for (int n = 0; n < max_points; n++){
        out.push_back(x);
        out.push_back(y);
        //Midpoint rule on the DIRECTION field: every point is one step apart, however slow the
        //air is, so a lazy eddy draws as a loop rather than a dot.
        WindVec a = Velocity(x,y,tick);
        float la = sqrtf(a.x * a.x + a.y * a.y);
        if (la < stall){
            return;
        }
        float mx = x + 0.5f * step * a.x / la, my = y + 0.5f * step * a.y / la;
        WindVec b = Velocity(mx,my,tick);
        float lb = sqrtf(b.x * b.x + b.y * b.y);
        if (lb < stall){
            return;
        }
        x += step * b.x / lb;
        y += step * b.y / lb;
        if ((x < MinX()) || (x > MaxX()) || (y < MinY()) || (y > MaxY()) || (Sample(dist,x,y) < 0.0f)){
            return;
        }
    }
}
