/*
    Checks for Water.{h,cpp} and the notch it has Backdrop.cpp cut. Engine-free; built and run by
    `make rules` after fireflies_test, as its own exe.

    Against the REAL main level, because what matters is that this water, in this bay, has every
    edge of every surface inside rock or under the grass - a plane edge in the open is a strip of
    blue floating in the air, and a screenshot only finds it from the one angle it was taken at.
*/
#include <math.h>
#include <stdio.h>

#include "Water.h"

static int checks = 0, failures = 0;

static void Check(bool f_ok, const char* what, const char* detail = NULL){
    checks++;
    if (!f_ok){
        failures++;
        printf("FAIL: %s%s%s\n",what,detail ? " - " : "",detail ? detail : "");
    }
}

//Inside a block's nominal box - the collider-sized one, before the mesher rounds it outward. The
//mesher only ever ADDS round a box (see Terrain.h), so a point in here is inside the rock drawn.
static bool InBox(const StageBlock& b, float x, float y, float z, float eps = 0.001f){
    return x >= b.Left() - eps && x <= b.Right() + eps && y >= b.Bottom() - eps && y <= b.Top() + eps &&
           z >= b.Back() - eps && z <= b.Front() + eps;
}

static bool InAny(const std::vector<StageBlock>& blocks, float x, float y, float z){
    for (const StageBlock& b : blocks){
        if (InBox(b,x,y,z)){
            return true;
        }
    }
    return false;
}

//The level's one water, laid out the way the app lays it out.
struct Laid{
    Stage stage;
    StageWater w;
    int ground = -1;
    BackdropParams bank_params;
    WaterParams params;
    WaterLayout l;
    std::vector<StageBlock> bank, bank_plain, rocks;
    std::vector<BackdropTree> trees;
    bool f_ok = false;
};

static void Lay(Laid& d){
    if (d.stage.waters.empty()){
        return;
    }
    d.w = d.stage.waters[0];
    d.ground = WaterGround(d.w,d.stage.blocks);
    if (d.ground < 0){
        return;
    }
    LayoutWater(d.w,d.stage.blocks[d.ground],d.bank_params,d.params,d.l);
    //The ground bay, as the app builds its bank - with the water, and without for comparison.
    BuildBackdropBlocks(d.stage.blocks,ARCHER_CAVE_X_MIN,ARCHER_TEST_BAY_X_MAX,-1e30f,ARCHER_TEST_BAY_SPLIT_Y,
                        d.bank_params,d.bank,&d.trees,NULL,&d.stage.waters);
    BuildBackdropBlocks(d.stage.blocks,ARCHER_CAVE_X_MIN,ARCHER_TEST_BAY_X_MAX,-1e30f,ARCHER_TEST_BAY_SPLIT_Y,
                        d.bank_params,d.bank_plain);
    BuildWaterRocks(d.w,d.l,d.params,d.rocks);
    d.f_ok = true;
}

static void TestLayout(const Laid& d){
    printf("the layout\n");
    char detail[200];
    Check(d.stage.waters.size() == 1,"the main level has its one waterfall");
    Check(d.ground >= 0,"standing on a ground block");
    if (!d.f_ok){
        return;
    }
    const StageBlock& g = d.stage.blocks[d.ground];
    Check(g.hw > 5.0f && g.Top() <= 0.01f,"which is the bay's wide floor");
    const WaterLayout& l = d.l;
    snprintf(detail,sizeof(detail),"notch %.2f, wall %.2f, shelf %.2f, ground back %.2f",
             l.recess_front,l.wall_front,l.shelf_front,g.Back());
    Check(l.recess_front < l.wall_front && l.wall_front < l.shelf_front && l.shelf_front < g.Back(),
          "back to front: the notch, the wall, the shelf, the ground",detail);
    Check(fabsf(l.shelf_front - (g.Back() - d.bank_params.front_gap)) < 0.001f,
          "the shelf comes no nearer her walking line than a ridge does");
    snprintf(detail,sizeof(detail),"lands at z %.2f, pool %.2f .. %.2f",l.land_z,l.recess_front,l.pool_front);
    Check(l.land_z > l.recess_front && l.land_z < l.pool_front,"the fall lands in the pool",detail);
    snprintf(detail,sizeof(detail),"spill lands at z %.2f, gap %.2f .. %.2f",l.spill_land_z,l.shelf_front,g.Back());
    Check(l.spill_land_z > l.shelf_front && l.spill_land_z < g.Back(),
          "the spill lands in the gap behind the ground",detail);
    Check(d.w.stream_y < g.Top() && d.w.basin_y > g.Top(),
          "the pool stands over the ground's top and the stream runs under it");
    //It may run on past its own ground onto the next - the bay's into the cave's - so where its
    //edges are is TestEdges' business; here only that it starts under the shelf and goes the
    //way it was asked to.
    snprintf(detail,sizeof(detail),"stream %.2f .. %.2f, asked to end at %.2f",l.stream_l,l.stream_r,
             d.w.stream_x_end);
    Check(l.stream_r <= l.shelf_r && fabsf(l.stream_l - d.w.stream_x_end) < 0.001f,
          "the stream starts under the shelf and runs to its end",detail);
    Check(d.w.stream_x_end < ARCHER_TEST_BAY_X_MIN,"and on into the cave");
}

static void TestNotch(const Laid& d){
    printf("the notch\n");
    if (!d.f_ok){
        return;
    }
    char detail[200];
    const WaterLayout& l = d.l;
    Check(d.bank.size() <= d.bank_plain.size(),"the water only ever takes ridges away");
    //Wall columns come first and there are as many either way; compare them one for one.
    int notched = 0, beside_low = 0, moved_elsewhere = 0, n_wall = 0;
    for (size_t i = 0; i < d.bank.size() && i < d.bank_plain.size(); i++){
        const StageBlock& a = d.bank[i];
        const StageBlock& b = d.bank_plain[i];
        if (fabsf(b.Front() - l.wall_front) > 0.001f){
            break;              //past the wall columns, into the ridges
        }
        n_wall++;
        bool f_notched = fabsf(a.Front() - l.recess_front) < 0.001f;
        if (f_notched){
            notched++;
            if (fabsf(a.Top() - d.w.lip_y) > 0.001f){
                Check(false,"a notched column is cut to the lip");
            }
            continue;
        }
        bool f_changed = (a.Top() != b.Top()) || (a.y != b.y) || (a.z != b.z);
        //Only the two beside the notch may change, and only upward.
        if (f_changed){
            bool f_beside = (i > 0 && fabsf(d.bank[i - 1].Front() - l.recess_front) < 0.001f) ||
                            (i + 1 < d.bank.size() && fabsf(d.bank[i + 1].Front() - l.recess_front) < 0.001f);
            if (!f_beside){
                moved_elsewhere++;
            }else if (a.Top() < d.w.lip_y + d.bank_params.notch_rise - 0.001f){
                beside_low++;
            }
        }
    }
    snprintf(detail,sizeof(detail),"%i of %i wall columns",notched,n_wall);
    Check(notched >= 2,"the wall is notched over the fall",detail);
    Check(beside_low == 0,"with the columns either side standing notch_rise over the lip");
    Check(moved_elsewhere == 0,"and no other wall column moved");

    int in_pool = 0, in_channel_forward = 0;
    for (size_t i = n_wall; i < d.bank.size(); i++){
        const StageBlock& r = d.bank[i];
        if (r.Right() > l.clear_l && r.Left() < l.clear_r){
            in_pool++;
        }
        if (r.Right() > l.channel_l && r.Left() < l.channel_r && r.Front() > l.channel_back + 0.001f){
            in_channel_forward++;
        }
    }
    Check(in_pool == 0,"no ridge stands in the pool's cleft");
    Check(in_channel_forward == 0,"and the ones along the stream stand back from it");
    int trees_in_notch = 0;
    for (const BackdropTree& t : d.trees){
        trees_in_notch += (t.x > l.notch_l && t.x < l.notch_r) ? 1 : 0;
    }
    Check(trees_in_notch == 0,"no pine grows where the water runs");
}

/*
    Every edge of every flat surface inside rock or the ground, sampled every 0.1 along it. The
    one check here that would otherwise need a screenshot from every angle.
*/
static void TestEdges(const Laid& d){
    printf("the edges\n");
    if (!d.f_ok){
        return;
    }
    char detail[200];
    const WaterLayout& l = d.l;
    const WaterParams& p = d.params;
    const StageBlock& g = d.stage.blocks[d.ground];
    //Rock is the bank, the pool's own rocks, and every live block of the level - the stream's front
    //edge runs under the bay's floor and then the cave's.
    std::vector<StageBlock> all = d.bank;
    all.insert(all.end(),d.rocks.begin(),d.rocks.end());
    for (const StageBlock& b : d.stage.blocks){
        if (b.kind == BLOCK_SOLID && b.f_alive && !b.f_invisible){
            all.push_back(b);
        }
    }

    const float px0 = l.shelf_l + p.rim_depth * 0.5f, px1 = l.shelf_r - p.rim_depth * 0.5f;
    const float pz0 = l.recess_front - 0.3f, pz1 = l.pool_front;
    const float y = d.w.basin_y;
    const float gap = p.spill_hw + 0.3f;
    int open = 0;
    for (float z = pz0; z <= pz1; z += 0.1f){
        open += InAny(all,px0,y,z) ? 0 : 1;
        open += InAny(all,px1,y,z) ? 0 : 1;
    }
    for (float x = px0; x <= px1; x += 0.1f){
        open += InAny(all,x,y,pz0) ? 0 : 1;
        //The front edge is open where the spill goes out, and only there.
        if (fabsf(x - l.spill_x) > gap){
            open += InAny(all,x,y,pz1) ? 0 : 1;
        }
    }
    snprintf(detail,sizeof(detail),"%i points out in the open",open);
    Check(open == 0,"the pool's edges are all in rock",detail);
    Check(p.spill_hw < gap,"and the spill is narrower than the gap it goes out through");

    open = 0;
    const float sy = d.w.stream_y;
    for (float x = l.stream_l; x <= l.stream_r; x += 0.1f){
        open += InAny(all,x,sy,l.stream_front) ? 0 : 1;
        open += InAny(all,x,sy,l.stream_back) ? 0 : 1;
    }
    for (float z = l.stream_back; z <= l.stream_front; z += 0.1f){
        open += InAny(all,l.stream_r,sy,z) ? 0 : 1;
    }
    snprintf(detail,sizeof(detail),"%i points out in the open",open);
    Check(open == 0,"the stream's banks and its head are in rock",detail);
    Check(sy < g.Top() - 0.05f,"and the stream is under the grass, never over it");

    //The shelf holds the pool under its surface; the rims stand over it. The last is the dam at
    //the stream's head, which is under the grass instead.
    bool f_rims = d.rocks.size() >= 3 && d.rocks[0].Top() < y - 0.1f;
    for (size_t i = 1; i + 1 < d.rocks.size(); i++){
        f_rims = f_rims && d.rocks[i].Top() > y + 0.1f;
    }
    Check(f_rims,"the shelf is under the pool and every rim over it");
    Check(!d.rocks.empty() && d.rocks.back().Top() < g.Top() && d.rocks.back().Front() < g.Front(),
          "the dam at the stream's head stays under the grass and behind her walking line");
}

static void TestSheets(const Laid& d){
    printf("the sheets\n");
    if (!d.f_ok){
        return;
    }
    char detail[200];
    const WaterParams& p = d.params;
    std::vector<vertex> v;
    BuildWaterSheets(d.w,d.l,p,v);
    const size_t per_sheet = (size_t)(3 + p.sheet_rows) * (size_t)p.sheet_cols * 6;
    Check(v.size() == 2 * per_sheet,"two sheets, the grid they were asked for");
    if (v.size() != 2 * per_sheet){
        return;
    }
    float fall_top = -1e30f, fall_foot = 1e30f, spill_foot = 1e30f, fall_back = 1e30f;
    float uv_lo = 1e30f, uv_hi = -1e30f;
    int facing_wrong = 0, winding_wrong = 0;
    for (size_t i = 0; i < v.size(); i++){
        bool f_fall = i < per_sheet;
        const vertex& a = v[i];
        if (f_fall){
            fall_top = fmaxf(fall_top,a.pos.y);
            fall_foot = fminf(fall_foot,a.pos.y);
            fall_back = fminf(fall_back,a.pos.z);
        }else{
            spill_foot = fminf(spill_foot,a.pos.y);
        }
        uv_lo = fminf(uv_lo,fminf(a.uv.x,a.uv.y));
        uv_hi = fmaxf(uv_hi,fmaxf(a.uv.x,a.uv.y));
        //Every normal faces up or toward the camera, never into the rock.
        facing_wrong += (a.normal.y < -0.1f || a.normal.z < -0.1f) ? 1 : 0;
    }
    for (size_t i = 0; i + 2 < v.size(); i += 3){
        vec3 n = (v[i + 1].pos - v[i].pos).cross(v[i + 2].pos - v[i].pos);
        winding_wrong += (n.dot(v[i].normal) < 0.0f) ? 1 : 0;
    }
    snprintf(detail,sizeof(detail),"top %.3f, lip %.3f",fall_top,d.w.lip_y);
    Check(fall_top > d.w.lip_y && fall_top < d.w.lip_y + p.lip_lift + p.bow,"the fall starts on the lip",detail);
    Check(fabsf(fall_back - (d.l.recess_front - p.pour_back)) < 0.001f,"back on the notch's top");
    snprintf(detail,sizeof(detail),"foot %.3f, pool %.3f; spill foot %.3f, stream %.3f",
             fall_foot,d.w.basin_y,spill_foot,d.w.stream_y);
    Check(fall_foot < d.w.basin_y && spill_foot < d.w.stream_y,"and each ends under the water it falls into",detail);
    Check(uv_lo >= -1.001f && uv_hi <= 1.001f,"uv across -1..1 and down 0..1");
    Check(facing_wrong == 0,"every normal faces up or out");
    Check(winding_wrong == 0,"and every triangle winds toward its normal");
}

static void TestFlats(const Laid& d){
    printf("the flat water\n");
    if (!d.f_ok){
        return;
    }
    std::vector<vertex> v;
    BuildWaterFlats(d.w,d.l,d.params,v);
    Check(!v.empty() && v.size() % 3 == 0,"the pool and the stream, in triangles");
    int off_level = 0, winding_wrong = 0, near_source = 0;
    for (size_t i = 0; i < v.size(); i++){
        bool f_level = fabsf(v[i].pos.y - d.w.basin_y) < 0.001f || fabsf(v[i].pos.y - d.w.stream_y) < 0.001f;
        off_level += f_level ? 0 : 1;
        near_source += (v[i].uv.y < 0.5f) ? 1 : 0;
    }
    for (size_t i = 0; i + 2 < v.size(); i += 3){
        vec3 n = (v[i + 1].pos - v[i].pos).cross(v[i + 2].pos - v[i].pos);
        winding_wrong += (n.y <= 0.0f) ? 1 : 0;
    }
    Check(off_level == 0,"every vertex at the pool's level or the stream's");
    Check(winding_wrong == 0,"and every triangle facing up");
    Check(near_source > 0,"with the rings' source inside it");
}

static void TestFoam(const Laid& d){
    printf("the foam\n");
    if (!d.f_ok){
        return;
    }
    char detail[200];
    std::vector<FoamEmitter> e;
    WaterFoamEmitters(d.w,d.l,e);
    Check(e.size() == 3,"the pool, the spill and the trail");
    if (e.size() != 3){
        return;
    }
    FoamSwarm a, b;
    a.Init(e);
    b.Init(e);
    const int64_t n = 1200;
    float pool_high = -1e30f, spill_high = -1e30f, biggest_share = 0.0f;
    int last_alive = 0, trail_moved_wrong = 0;
    const float flow = (d.w.stream_x_end < d.l.spill_x) ? -1.0f : 1.0f;
    for (int64_t t = 0; t < n; t++){
        a.Step(t);
        b.Step(t);
        for (const FoamBall& ball : a.balls){
            if (ball.emitter < 0){
                continue;
            }
            float s = ball.Scale();
            biggest_share = fmaxf(biggest_share,s / ball.radius);
            if (ball.emitter == 0) pool_high = fmaxf(pool_high,ball.y);
            if (ball.emitter == 1) spill_high = fmaxf(spill_high,ball.y);
            if (ball.emitter == 2 && (ball.x - e[2].x) * flow < -e[2].spread_x - 0.5f) trail_moved_wrong++;
        }
        last_alive = a.Alive();
    }
    bool f_same = true;
    for (size_t i = 0; i < a.balls.size(); i++){
        f_same = f_same && a.balls[i].x == b.balls[i].x && a.balls[i].y == b.balls[i].y &&
                 a.balls[i].emitter == b.balls[i].emitter;
    }
    Check(f_same,"deterministic");
    //Every due tick made a ball: a full pool returns before counting, so this is "none dropped".
    bool f_all = true;
    for (size_t k = 0; k < e.size(); k++){
        uint32_t due = 0;
        for (int64_t t = 0; t < n; t++){
            due += (((t + (int64_t)k) % e[k].every) == 0) ? 1u : 0u;
        }
        f_all = f_all && a.spawns[k] == due;
    }
    snprintf(detail,sizeof(detail),"%i alive of %zu slots at the end",last_alive,a.balls.size());
    Check(f_all,"the pool is big enough that no ball is ever dropped",detail);
    Check(biggest_share <= 1.0001f,"no ball is ever bigger than its radius");
    const StageBlock& g = d.stage.blocks[d.ground];
    snprintf(detail,sizeof(detail),"pool %.2f over %.2f; spill %.2f over the grass at %.2f",
             pool_high,d.w.basin_y,spill_high,g.Top());
    Check(pool_high > d.w.basin_y + d.params.rim_height + 0.2f,"the pool's foam is thrown up over its rim",detail);
    Check(spill_high > g.Top() + 0.2f,"and the spill's over the grass, where it can be seen",detail);
    Check(trail_moved_wrong == 0,"the flecks ride the stream the way it runs");

    FoamBall ball;
    ball.emitter = 0; ball.radius = 1.0f; ball.life = 100;
    ball.age = 0;   float s0 = ball.Scale();
    ball.age = 30;  float s1 = ball.Scale();
    ball.age = 99;  float s2 = ball.Scale();
    snprintf(detail,sizeof(detail),"%.3f %.3f %.3f",s0,s1,s2);
    Check(s0 < 0.01f && s1 > 0.5f && s2 < 0.1f,"a ball swells in and shrinks away to nothing",detail);
}

int main(){
    printf("water_test\n");
    Laid d;
    Lay(d);
    TestLayout(d);
    TestNotch(d);
    TestEdges(d);
    TestSheets(d);
    TestFlats(d);
    TestFoam(d);
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
