#include "Water.h"

#include <math.h>

//--- Hashing: the Backdrop kind, so no shared random stream is touched ---------------------------

static uint32_t Mix(uint32_t x){
    x ^= x >> 16; x *= 0x7FEB352Du;
    x ^= x >> 15; x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float HashUnit(uint32_t a, uint32_t b, uint32_t c){
    uint32_t h = Mix(a + 0x9E3779B9u);
    h = Mix(h ^ b);
    h = Mix(h ^ c);
    return (float)(h & 0xFFFFFF) / (float)0xFFFFFF;
}

static float Lerp(float a, float b, float t){
    return a + (b - a) * t;
}

//--- The layout ----------------------------------------------------------------------------------

TerrainParams WaterRockParams(){
    //The ground's own settings, with a little more of the coarse noise: it is rock, not turf, and
    //it is further from her walking line than any ground block's face.
    TerrainParams p;
    p.coarse_amp = 0.25f;
    return p;
}

int WaterGround(const StageWater& w, const std::vector<StageBlock>& blocks){
    for (size_t i = 0; i < blocks.size(); i++){
        const StageBlock& g = blocks[i];
        if (g.kind != BLOCK_SOLID || !g.f_alive || g.f_invisible || w.x < g.Left() || w.x >= g.Right()){
            continue;
        }
        //Backdrop's test for the bottom of the world: nothing under it and nothing holding it up.
        bool f_ground = true;
        for (size_t k = 0; k < blocks.size() && f_ground; k++){
            const StageBlock& o = blocks[k];
            if (k == i || !o.f_alive || o.Right() <= g.Left() || o.Left() >= g.Right()){
                continue;
            }
            f_ground = !(o.Top() <= g.Bottom() + 0.05f);
        }
        if (f_ground){
            return (int)i;
        }
    }
    return -1;
}

void LayoutWater(const StageWater& w, const StageBlock& ground, const BackdropParams& bank,
                 const WaterParams& p, WaterLayout& l){
    l.wall_front   = ground.Back() - bank.wall_gap;
    l.recess_front = l.wall_front - bank.notch_recess;
    l.shelf_l      = w.x - w.basin_hw;
    l.shelf_r      = w.x + w.basin_hw;
    //Wider than the shelf by the bank's rounding, so the cleft the wall is cut to is at least as
    //wide as the pool once the columns either side have rolled out into it.
    l.notch_l      = l.shelf_l - bank.notch_margin;
    l.notch_r      = l.shelf_r + bank.notch_margin;

    //The shelf stands where a ridge would: front_gap behind the ground, so its rock no more
    //reaches over her walking line than a ridge's does. Its back goes into the notch.
    l.shelf_front  = ground.Back() - bank.front_gap;
    l.shelf_back   = l.recess_front - 0.5f;
    //Halfway into the front rim, so the pool's front edge is inside rock along its whole length.
    l.pool_front   = l.shelf_front - p.rim_depth * 0.5f;
    //Where the sheet goes over: out past the notch's face by the bank's rounded edge.
    l.lip_z        = l.recess_front + p.lip_out;
    const float drop = sqrtf(fmaxf(w.lip_y + p.lip_lift - w.basin_y,0.01f));
    const float target = l.recess_front + p.land_at * (l.pool_front - l.recess_front);
    l.fall_reach   = fmaxf(p.min_reach,(target - l.lip_z) / drop);
    l.land_z       = l.lip_z + l.fall_reach * drop;
    //A fall too short for min_reach to land where asked lands just inside the front rim instead.
    if (l.land_z > l.pool_front - 0.15f){
        l.land_z = l.pool_front - 0.15f;
        l.fall_reach = (l.land_z - l.lip_z) / drop;
    }

    l.spill_x      = w.x + w.spill_dx;
    l.spill_land_z = l.shelf_front + p.spill_reach * sqrtf(fmaxf(w.basin_y - w.stream_y,0.0f));
    //Front edge inside the ground block (it is under its top), back edge inside the bank's wall,
    //notched or not.
    l.stream_front = ground.Back() + 0.3f;
    l.stream_back  = l.recess_front - 0.3f;
    //From under the shelf's far rim to the end - whichever way the stream runs.
    if (w.stream_x_end < l.spill_x){
        l.stream_l = w.stream_x_end;
        l.stream_r = l.shelf_r - p.rim_depth * 0.5f;
    }else{
        l.stream_l = l.shelf_l + p.rim_depth * 0.5f;
        l.stream_r = w.stream_x_end;
    }
    //No ridge stands in the pool's cleft; the ones along the stream step back from it.
    l.clear_l = l.notch_l - 0.5f;
    l.clear_r = l.notch_r + 0.5f;
    l.channel_l = l.stream_l - 0.5f;
    l.channel_r = l.stream_r + 0.5f;
    l.channel_back = l.shelf_front - p.channel;
    l.ground_bottom = ground.Bottom();
    l.ground_top = ground.Top();
    l.ground_back = ground.Back();
}

//--- The rocks -----------------------------------------------------------------------------------

static StageBlock RockBox(float left, float right, float bottom, float top, float front, float back){
    StageBlock b;
    b.kind = BLOCK_SOLID;
    b.x = (left + right) * 0.5f;
    b.hw = (right - left) * 0.5f;
    b.y = (top + bottom) * 0.5f;
    b.hh = (top - bottom) * 0.5f;
    b.depth = (front - back) * 0.5f;
    b.z = (front + back) * 0.5f;
    return b;
}

void BuildWaterRocks(const StageWater& w, const WaterLayout& l, const WaterParams& p,
                     std::vector<StageBlock>& out){
    const float shelf_top = w.basin_y - p.pool_depth;
    //The rims start inside the shelf, so the two are one piece of rock under the smooth union.
    const float rim_bottom = shelf_top - 0.3f;
    const float rim_top = w.basin_y + p.rim_height;
    out.push_back(RockBox(l.shelf_l,l.shelf_r,l.ground_bottom,shelf_top,l.shelf_front,l.shelf_back));
    //The two sides, front to back.
    out.push_back(RockBox(l.shelf_l,l.shelf_l + p.rim_depth,rim_bottom,rim_top,l.shelf_front,l.shelf_back));
    out.push_back(RockBox(l.shelf_r - p.rim_depth,l.shelf_r,rim_bottom,rim_top,l.shelf_front,l.shelf_back));
    /*
        The front, either side of the spill. The gap is wider than the spill by the rounding and
        the cap's lip on each piece's end, which roll back into it: this leaves the spill's own
        edges running into rock rather than showing, and the pool's front edge covered.
    */
    const float gap = p.spill_hw + 0.3f;
    const float front_back = l.shelf_front - p.rim_depth;
    if (l.spill_x - gap - l.shelf_l > 0.1f){
        out.push_back(RockBox(l.shelf_l,l.spill_x - gap,rim_bottom,rim_top,l.shelf_front,front_back));
    }
    if (l.shelf_r - (l.spill_x + gap) > 0.1f){
        out.push_back(RockBox(l.spill_x + gap,l.shelf_r,rim_bottom,rim_top,l.shelf_front,front_back));
    }
    /*
        The stream's head: a dam across the slot between the shelf and the ground, at the end the
        stream runs away from, or that end of it stands in the open. Its top a little under the
        ground's, so it is hidden under the grass and never reaches up to her walking line.
    */
    const bool f_left = w.stream_x_end < l.spill_x;
    const float dam_l = f_left ? l.shelf_r - p.rim_depth : l.shelf_l;
    const float dam_r = f_left ? l.shelf_r : l.shelf_l + p.rim_depth;
    out.push_back(RockBox(dam_l,dam_r,l.ground_bottom,l.ground_top - 0.05f,l.ground_back + 0.2f,l.shelf_front - 0.2f));
}

//--- The sheets ----------------------------------------------------------------------------------

/*
    One sheet: flat along +z at y0 from z0 to z_edge, then over the edge and down by `drop`,
    coming out from it by reach * sqrt(fallen) - the path of water leaving an edge with some speed
    forward, near enough, and the shape that keeps it off the rock it came over.
*/
struct SheetPath{
    float x = 0.0f, hw = 0.5f;
    float y0 = 0.0f, z0 = 0.0f, z_edge = 0.0f;
    float drop = 1.0f, reach = 1.0f;
};

static void AddSheet(const SheetPath& s, const WaterParams& p, std::vector<vertex>& out){
    const int flat_rows = 3;
    const int rows = flat_rows + (p.sheet_rows > 2 ? p.sheet_rows : 2);
    const int cols = p.sheet_cols > 1 ? p.sheet_cols : 1;
    //The centre line first, then how far along it each row is, for uv.y.
    std::vector<vec3> line(rows + 1);
    for (int r = 0; r <= rows; r++){
        if (r <= flat_rows){
            float t = (float)r / (float)flat_rows;
            line[r] = vec3(s.x,s.y0,s.z0 + (s.z_edge - s.z0) * t);
        }else{
            //Squared, so the rows bunch up over the edge where the sheet bends most.
            float t = (float)(r - flat_rows) / (float)(rows - flat_rows);
            float d = s.drop * t * t;
            line[r] = vec3(s.x,s.y0 - d,s.z_edge + s.reach * sqrtf(d));
        }
    }
    std::vector<float> along(rows + 1,0.0f);
    for (int r = 1; r <= rows; r++){
        along[r] = along[r - 1] + (line[r] - line[r - 1]).length();
    }
    const float total = along[rows] > 0.0f ? along[rows] : 1.0f;

    //The grid, wider toward the foot and bowed out in the middle.
    std::vector<vec3> pos((rows + 1) * (cols + 1));
    std::vector<vec2> uv((rows + 1) * (cols + 1));
    for (int r = 0; r <= rows; r++){
        float v = along[r] / total;
        float hw = s.hw * (1.0f + p.spread * v);
        for (int c = 0; c <= cols; c++){
            float a = -1.0f + 2.0f * (float)c / (float)cols;
            vec3 q = line[r];
            q.x += a * hw;
            //Out along the sheet's own normal would be exact; +z is what it is for every row but
            //the flat ones, and on those the bow lifts it off the rock instead, which is fine.
            float b = p.bow * (1.0f - a * a);
            if (r <= flat_rows){
                q.y += b * 0.5f;
            }else{
                q.z += b;
            }
            pos[r * (cols + 1) + c] = q;
            uv[r * (cols + 1) + c] = vec2(a,v);
        }
    }
    //Normals from the grid itself: along x across, so they face the way the sheet does - up on
    //the flat, toward the camera on the fall.
    auto P = [&](int r, int c) -> const vec3& { return pos[r * (cols + 1) + c]; };
    std::vector<vec3> nrm((rows + 1) * (cols + 1));
    std::vector<vec3> tan((rows + 1) * (cols + 1));
    for (int r = 0; r <= rows; r++){
        for (int c = 0; c <= cols; c++){
            vec3 d_along = P(r < rows ? r + 1 : r,c) - P(r > 0 ? r - 1 : r,c);
            vec3 d_across = P(r,c < cols ? c + 1 : c) - P(r,c > 0 ? c - 1 : c);
            vec3 n = d_along.cross(d_across);
            float len = n.length();
            nrm[r * (cols + 1) + c] = (len > 1e-6f) ? n * (1.0f / len) : vec3(0.0f,0.0f,1.0f);
            float tl = d_across.length();
            tan[r * (cols + 1) + c] = (tl > 1e-6f) ? d_across * (1.0f / tl) : vec3(1.0f,0.0f,0.0f);
        }
    }
    auto Emit = [&](int r, int c){
        vertex v;
        int i = r * (cols + 1) + c;
        v.pos = pos[i];
        v.normal = nrm[i];
        v.tangent = tan[i];
        v.uv = uv[i];
        v.matid = 0;
        out.push_back(v);
    };
    //Counter-clockwise from the side the normal is on.
    for (int r = 0; r < rows; r++){
        for (int c = 0; c < cols; c++){
            Emit(r,c); Emit(r + 1,c); Emit(r + 1,c + 1);
            Emit(r,c); Emit(r + 1,c + 1); Emit(r,c + 1);
        }
    }
}

void BuildWaterSheets(const StageWater& w, const WaterLayout& l, const WaterParams& p,
                      std::vector<vertex>& out){
    //From the notch's top, back from its edge, into the pool.
    SheetPath fall;
    fall.x = w.x;
    fall.hw = w.half_width;
    fall.y0 = w.lip_y + p.lip_lift;
    fall.z0 = l.recess_front - p.pour_back;
    fall.z_edge = l.lip_z;
    fall.drop = w.lip_y + p.lip_lift - w.basin_y + p.under;
    fall.reach = l.fall_reach;
    AddSheet(fall,p,out);

    //From the pool's own edge, out through the rim's gap, down behind the ground. At the pool's
    //level exactly: the pool's surface stops where this starts, so the two meet rather than fight.
    SheetPath spill;
    spill.x = l.spill_x;
    spill.hw = p.spill_hw;
    spill.y0 = w.basin_y;
    spill.z0 = l.pool_front;
    spill.z_edge = l.shelf_front;
    spill.drop = w.basin_y - w.stream_y + p.under;
    spill.reach = p.spill_reach;
    AddSheet(spill,p,out);
}

//--- The flat water ------------------------------------------------------------------------------

//A level grid over [x0,x1] x [z_back,z_front], facing up; uv.y is the distance from (sx, sz).
static void AddFlat(float x0, float x1, float z_back, float z_front, float y, float sx, float sz,
                    float cell, std::vector<vertex>& out){
    int nx = (int)ceilf((x1 - x0) / cell);
    int nz = (int)ceilf((z_front - z_back) / cell);
    nx = nx < 1 ? 1 : nx;
    nz = nz < 1 ? 1 : nz;
    auto Emit = [&](int i, int j){
        vertex v;
        float x = x0 + (x1 - x0) * (float)i / (float)nx;
        float z = z_back + (z_front - z_back) * (float)j / (float)nz;
        v.pos = vec3(x,y,z);
        v.normal = vec3(0.0f,1.0f,0.0f);
        v.tangent = vec3(1.0f,0.0f,0.0f);
        float dx = x - sx, dz = z - sz;
        v.uv = vec2(x,sqrtf(dx * dx + dz * dz));
        v.matid = 0;
        out.push_back(v);
    };
    //Counter-clockwise seen from above: j runs toward +z, i toward +x.
    for (int i = 0; i < nx; i++){
        for (int j = 0; j < nz; j++){
            Emit(i,j); Emit(i,j + 1); Emit(i + 1,j + 1);
            Emit(i,j); Emit(i + 1,j + 1); Emit(i + 1,j);
        }
    }
}

void BuildWaterFlats(const StageWater& w, const WaterLayout& l, const WaterParams& p,
                     std::vector<vertex>& out){
    //The pool: inside the rims at the sides and front, into the notch at the back. Rings from
    //where the fall comes down.
    AddFlat(l.shelf_l + p.rim_depth * 0.5f,l.shelf_r - p.rim_depth * 0.5f,
            l.recess_front - 0.3f,l.pool_front,w.basin_y,w.x,l.land_z,p.cell,out);
    //The stream, flowing away from where the spill lands.
    AddFlat(l.stream_l,l.stream_r,l.stream_back,l.stream_front,w.stream_y,
            l.spill_x,l.spill_land_z,p.cell,out);
}

//--- The foam ------------------------------------------------------------------------------------

float FoamBall::Scale() const{
    if (emitter < 0 || life <= 0){
        return 0.0f;
    }
    float f = (float)age / (float)life;
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    //In over the first eighth, as it breaks the surface; then away to nothing, fastest at the end.
    float in = f < 0.125f ? f / 0.125f : 1.0f;
    in = in * in * (3.0f - 2.0f * in);
    return radius * in * powf(1.0f - f,0.7f);
}

void FoamSwarm::Init(const std::vector<FoamEmitter>& e){
    emitters = e;
    size_t capacity = 0;
    for (const FoamEmitter& m : emitters){
        int every = m.every > 0 ? m.every : 1;
        capacity += (size_t)(m.life_max / every + 2);
    }
    balls.assign(capacity,FoamBall());
    spawns.assign(emitters.size(),0u);
}

void FoamSwarm::Step(int64_t tick){
    const float dt = ARCHER_DT;
    for (FoamBall& b : balls){
        if (b.emitter < 0){
            continue;
        }
        const FoamEmitter& e = emitters[b.emitter];
        //The burst dies away toward the steady rise; the carry is the ball's own, set at birth.
        b.vy += (e.rise - b.vy) * fminf(1.0f,e.drag * dt);
        b.x += b.vx * dt;
        b.y += b.vy * dt;
        b.z += b.vz * dt;
        if (++b.age >= b.life){
            b.emitter = -1;
        }
    }
    size_t slot = 0;
    for (size_t k = 0; k < emitters.size(); k++){
        const FoamEmitter& e = emitters[k];
        int every = e.every > 0 ? e.every : 1;
        //Offset by the emitter's index, so two with the same period do not spawn on one tick.
        if (((tick + (int64_t)k) % every) != 0){
            continue;
        }
        while (slot < balls.size() && balls[slot].emitter >= 0){
            slot++;
        }
        if (slot >= balls.size()){
            return;         //full - Init sized it for everything alive at once, so this is rare
        }
        const uint32_t n = spawns[k]++;
        const uint32_t id = (uint32_t)k;
        FoamBall& b = balls[slot];
        b.emitter = (int)k;
        b.age = 0;
        b.life = e.life_min + (int)((float)(e.life_max - e.life_min) * HashUnit(id,n,1u));
        b.life = b.life < 1 ? 1 : b.life;
        b.radius = Lerp(e.radius_min,e.radius_max,HashUnit(id,n,2u));
        b.x = e.x + e.spread_x * (2.0f * HashUnit(id,n,3u) - 1.0f);
        b.y = e.y;
        b.z = e.z + e.spread_z * (2.0f * HashUnit(id,n,4u) - 1.0f);
        b.vy = Lerp(e.burst_min,e.burst_max,HashUnit(id,n,5u));
        b.vx = e.drift_x + e.wander * (2.0f * HashUnit(id,n,6u) - 1.0f);
        b.vz = e.drift_z + e.wander * 0.5f * (2.0f * HashUnit(id,n,7u) - 1.0f);
    }
}

int FoamSwarm::Alive() const{
    int n = 0;
    for (const FoamBall& b : balls){
        n += (b.emitter >= 0) ? 1 : 0;
    }
    return n;
}

void WaterFoamEmitters(const StageWater& w, const WaterLayout& l, std::vector<FoamEmitter>& out){
    const float flow = (w.stream_x_end < l.spill_x) ? -1.0f : 1.0f;

    //The pool, where the fall comes down: the big ones, thrown up and carried out toward the rim.
    FoamEmitter pool;
    pool.x = w.x;
    pool.y = w.basin_y;
    pool.z = l.land_z;
    pool.spread_x = w.half_width * 0.85f;
    pool.spread_z = 0.2f;
    pool.every = 2;
    pool.life_min = 45; pool.life_max = 85;
    pool.radius_min = 0.14f; pool.radius_max = 0.30f;
    pool.burst_min = 1.0f; pool.burst_max = 1.9f;
    pool.rise = 0.25f;
    pool.drag = 3.5f;
    pool.drift_z = 0.3f;
    pool.wander = 0.35f;
    out.push_back(pool);

    //Where the spill lands behind the ground: smaller, and thrown high enough to show over the
    //grass, then carried off with the stream.
    FoamEmitter spill;
    spill.x = l.spill_x;
    spill.y = w.stream_y;
    spill.z = l.spill_land_z;
    spill.spread_x = 0.3f;
    spill.spread_z = 0.12f;
    spill.every = 3;
    spill.life_min = 40; spill.life_max = 70;
    spill.radius_min = 0.10f; spill.radius_max = 0.22f;
    spill.burst_min = 1.3f; spill.burst_max = 2.1f;
    spill.rise = 0.2f;
    spill.drag = 3.0f;
    spill.drift_x = 0.6f * flow;
    spill.drift_z = 0.1f;
    spill.wander = 0.25f;
    out.push_back(spill);

    //Flecks riding the stream away: no burst, no rise, just the current.
    FoamEmitter trail;
    trail.x = l.spill_x + flow * 0.8f;
    trail.y = w.stream_y + 0.02f;
    trail.z = (l.stream_front - 0.3f + l.wall_front) * 0.5f;
    trail.spread_x = 0.4f;
    trail.spread_z = 0.9f;
    trail.every = 9;
    trail.life_min = 150; trail.life_max = 240;
    trail.radius_min = 0.05f; trail.radius_max = 0.10f;
    trail.burst_min = 0.0f; trail.burst_max = 0.0f;
    trail.rise = 0.0f;
    trail.drag = 0.0f;
    trail.drift_x = 0.9f * flow;
    trail.drift_z = 0.0f;
    trail.wander = 0.1f;
    out.push_back(trail);
}
