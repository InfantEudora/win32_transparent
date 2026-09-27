/*
    Checks for Leaves.{h,cpp} - the wind-blown leaves. Engine-free; built and run by `make rules`
    after wind_test, as its own exe.
*/
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <chrono>

#include "Leaves.h"

static int checks = 0, failures = 0;

static void Check(bool f_ok, const char* what){
    checks++;
    if (!f_ok){
        failures++;
        printf("FAIL: %s\n",what);
    }
}

/*
    The wind these checks were written against. PINNED, not the default: the default is a tuning
    choice (it went 2.5 -> 0.5 on 2026-09-26) and at 0.5 a leaf's own flutter outruns the wind,
    which failed checks that were about something else entirely.
*/
static WindParams TestWind(){
    WindParams p;
    p.speed = 2.5f;
    return p;
}

static StageBlock Box(float left, float bottom, float right, float top){
    StageBlock b;
    b.x = 0.5f * (left + right);
    b.y = 0.5f * (bottom + top);
    b.hw = 0.5f * (right - left);
    b.hh = 0.5f * (top - bottom);
    return b;
}

//The wind test's level: flat ground with its top at 0, and a 3-tall step at x 0..6.
static std::vector<StageBlock> StepLevel(){
    std::vector<StageBlock> blocks;
    blocks.push_back(Box(-60.0f,-2.0f,60.0f,0.0f));
    blocks.push_back(Box(0.0f,0.0f,6.0f,3.0f));
    return blocks;
}

//A view well clear of the step, so recycling does not interfere with what is being measured.
static const float VX0 = -40.0f, VY0 = -1.0f, VX1 = 40.0f, VY1 = 20.0f;

static void TestCarried(){
    WindField w;
    WindParams p = TestWind();
    p.gust_strength = 0.0f;
    w.Build(StepLevel(),p);
    LeafSwarm s;
    s.params.count = 40;
    s.Reset(w,VX0,VY0,VX1,VY1);
    //High up, well upwind of the step, all at once.
    for (int i = 0; i < 40; i++){
        s.leaves[i].x = -30.0f + 0.3f * i;
        s.leaves[i].y = 12.0f;
        s.leaves[i].vx = 0.0f;
        s.leaves[i].vy = 0.0f;
    }
    for (int64_t tick = 0; tick < 120; tick++){
        s.Step(w,tick,VX0,VY0,VX1,VY1);
    }
    double vx = 0.0, vy = 0.0;
    for (const Leaf& l : s.leaves){
        vx += l.vx;
        vy += l.vy;
    }
    vx /= s.leaves.size();
    vy /= s.leaves.size();
    printf("  carried: mean velocity %.2f %.2f after 2 s in a %.1f wind\n",vx,vy,p.speed);
    Check(fabs(vx - p.speed) < 0.3 * p.speed,"a leaf takes on the wind's speed");
    Check(vy < 0.0 && vy > -2.0 * s.params.fall_speed,"and sinks, slowly");
}

static void TestSettle(){
    WindField w;
    WindParams p = TestWind();
    p.speed = 0.0f;
    w.Build(StepLevel(),p);
    LeafSwarm s;
    s.params.count = 80;
    s.params.rest_ticks = 100000;
    s.Reset(w,-10.0f,-1.0f,16.0f,8.0f);
    for (int64_t tick = 0; tick < 60 * 30; tick++){
        s.Step(w,tick,-10.0f,-1.0f,16.0f,8.0f);
    }
    int resting = 0, inside = 0;
    for (const Leaf& l : s.leaves){
        resting += (l.state == LEAF_RESTING);
        inside += (w.Distance(l.x,l.y) < -0.01f);
    }
    printf("  still air: %d of %zu resting after 30 s, %d inside a block\n",resting,s.leaves.size(),inside);
    Check(resting == (int)s.leaves.size(),"in still air every leaf comes to rest");
    Check(inside == 0,"and none inside a block");
    bool f_on_top = true;
    for (const Leaf& l : s.leaves){
        //Within a quarter unit: the inside corner at the foot of the step is where the grid's
        //distance field is smeared, and a leaf can come to rest leaning there.
        bool f_ground = fabsf(l.y) < 0.25f;
        bool f_step = (l.x > -0.1f) && (l.x < 6.1f) && (fabsf(l.y - 3.0f) < 0.15f);
        f_on_top = f_on_top && (f_ground || f_step);
    }
    Check(f_on_top,"each lies on the ground or on the step's top");
}

static void TestLift(){
    WindField w;
    WindParams p = TestWind();
    p.speed = 0.0f;
    w.Build(StepLevel(),p);
    LeafSwarm s;
    s.params.count = 20;
    s.params.rest_ticks = 100000;
    s.Reset(w,-30.0f,-1.0f,-10.0f,6.0f);
    for (int64_t tick = 0; tick < 600; tick++){
        s.Step(w,tick,-30.0f,-1.0f,-10.0f,6.0f);
    }
    //Now a hard wind: they get up.
    p.speed = 6.0f;
    p.gust_strength = 0.0f;
    w.SetParams(p);
    int flying = 0;
    for (int64_t tick = 600; tick < 660; tick++){
        s.Step(w,tick,-30.0f,-1.0f,-10.0f,6.0f);
    }
    for (const Leaf& l : s.leaves){
        flying += (l.state == LEAF_FLYING);
    }
    printf("  a 6.0 wind over resting leaves: %d of %zu flying a second later\n",flying,s.leaves.size());
    Check(flying > (int)s.leaves.size() / 2,"a hard wind lifts resting leaves");
}

/*
    THE EDDY. Leaves dropped into the lee of the step: how many are ever CARRIED BACK against the
    wind while flying, over five seconds - the signature of being caught, since nothing but the
    eddy's reverse flow moves a leaf upwind. Measured against the same leaves with the eddies off.

    Most of the caught ones then come to rest at the foot of the lee wall, where the flow slows
    against it - which is where real leaves pile up too. A gust (off here) stirs the pile again.
    "Still airborne inside a box after N seconds" was tried first and is a poor measure: in the
    calm lee without eddies leaves simply drift slowly and stay in the box.
*/
static int CountCarriedBack(float eddy_strength){
    WindField w;
    WindParams p = TestWind();
    p.gust_strength = 0.0f;
    p.wave_strength = 0.0f;
    p.eddy_strength = eddy_strength;
    w.Build(StepLevel(),p);
    LeafSwarm s;
    s.params.count = 60;
    s.params.rest_ticks = 100000;
    s.Reset(w,VX0,VY0,VX1,VY1);
    for (int i = 0; i < 60; i++){
        s.leaves[i].x = 6.5f + 0.06f * i;
        s.leaves[i].y = 1.0f + 0.025f * i;
        s.leaves[i].vx = 0.0f;
        s.leaves[i].vy = 0.0f;
    }
    std::vector<bool> back(60,false);
    for (int64_t tick = 0; tick < 300; tick++){
        s.Step(w,tick,VX0,VY0,VX1,VY1);
        for (int i = 0; i < 60; i++){
            if ((s.leaves[i].state == LEAF_FLYING) && (s.leaves[i].vx < -0.5f)){
                back[i] = true;
            }
        }
    }
    int n = 0;
    for (bool b : back){
        n += b;
    }
    return n;
}

static void TestEddy(){
    int with = CountCarriedBack(1.0f);
    int without = CountCarriedBack(0.0f);
    printf("  dropped in the lee, carried back against the wind: %d of 60 with eddies, %d without\n",with,without);
    Check(with >= 30,"leaves are caught in the lee eddy");
    Check(without == 0,"and only the eddy carries a leaf upwind");
}

static void TestRecycle(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    LeafSwarm s;
    s.params.count = 100;
    float x0 = -20.0f, x1 = 20.0f;
    for (int64_t tick = 0; tick < 60 * 60; tick++){
        s.Step(w,tick,x0,-1.0f,x1,12.0f);
    }
    int in_view = 0;
    for (const Leaf& l : s.leaves){
        in_view += (l.x > x0 - s.params.margin) && (l.x < x1 + s.params.margin);
    }
    printf("  after a minute: %d of %zu within the view and its margins\n",in_view,s.leaves.size());
    Check(in_view == (int)s.leaves.size(),"leaves that leave the view come back into it");

    //The camera running downwind faster than the wind: leaves come in from the far side.
    LeafSwarm r;
    r.params.count = 100;
    float cx = -20.0f;
    for (int64_t tick = 0; tick < 60 * 8; tick++){
        cx += 9.0f * ARCHER_DT;
        r.Step(w,tick,cx - 15.0f,-1.0f,cx + 15.0f,12.0f);
    }
    int left_half = 0, right_half = 0;
    for (const Leaf& l : r.leaves){
        if (l.x < cx){ left_half++; } else { right_half++; }
    }
    printf("  camera running at 9 downwind of a 2.5 wind: %d left of centre, %d right\n",left_half,right_half);
    Check(right_half > (int)r.leaves.size() / 4 && left_half > (int)r.leaves.size() / 4,"the view stays filled while she runs");
}

/*
    THE PADDING, which is what stops a zoom-out showing the swarm as a box: leaves live in the view
    grown by `pad` on every side, at a density, and a zoom resizes the swarm without moving anyone.
*/
static void TestZoom(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    LeafSwarm s;
    s.params.density = 0.15f;
    s.params.pad = 1.5f;
    float cx = -25.0f, cy = 6.0f, hw = 10.0f, hh = 6.0f;
    for (int64_t tick = 0; tick < 300; tick++){
        s.Step(w,tick,cx - hw,cy - hh,cx + hw,cy + hh);
    }
    size_t before = s.leaves.size();
    //20 x 12 view, grown by 1.5 of that on every side: 80 x 48.
    printf("  a 20 x 12 view padded 1.5: %zu leaves (density 0.15 over 80 x 48 = %.0f)\n",before,0.15f * 80.0f * 48.0f);
    Check(before == (size_t)(0.15f * 80.0f * 48.0f),"the swarm holds its density over the padded region");

    //Leaves in the padding are there, and they move even though they step less often.
    int far = 0, moved = 0;
    std::vector<Leaf> snap = s.leaves;
    for (int64_t tick = 300; tick < 360; tick++){
        s.Step(w,tick,cx - hw,cy - hh,cx + hw,cy + hh);
    }
    for (size_t i = 0; i < snap.size(); i++){
        const Leaf& a = snap[i];
        if ((a.x < cx - hw - 3.0f || a.x > cx + hw + 3.0f) && a.state == LEAF_FLYING && s.leaves[i].spawns == a.spawns){
            far++;
            moved += (fabsf(s.leaves[i].x - a.x) > 0.5f);
        }
    }
    printf("  %d flying leaves out in the padding, %d of them moved in a second\n",far,moved);
    Check(far > 50,"the padding is populated");
    Check(moved > far * 3 / 4,"and leaves out there still blow along");

    //Zoom out 2x: the swarm grows, the leaves already there are not moved by it, the new ones
    //fade in.
    snap = s.leaves;
    s.Step(w,360,cx - 2.0f * hw,cy - 2.0f * hh,cx + 2.0f * hw,cy + 2.0f * hh);
    bool f_kept = true;
    for (size_t i = 0; i < snap.size() && i < s.leaves.size(); i++){
        if (s.leaves[i].spawns == snap[i].spawns){
            f_kept = f_kept && (fabsf(s.leaves[i].x - snap[i].x) < 0.5f) && (fabsf(s.leaves[i].y - snap[i].y) < 0.5f);
        }
    }
    bool f_fading_in = s.leaves.size() > snap.size();
    for (size_t i = snap.size(); i < s.leaves.size(); i++){
        f_fading_in = f_fading_in && (s.leaves[i].fade < 0.2f);
    }
    printf("  zoomed out 2x: %zu -> %zu leaves\n",snap.size(),s.leaves.size());
    Check(f_kept,"a zoom moves none of the leaves already there");
    Check(f_fading_in,"and the new ones fade in rather than pop");
}

static void TestDeterministic(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    LeafSwarm a, b;
    for (int64_t tick = 0; tick < 600; tick++){
        a.Step(w,tick,-20.0f,-1.0f,20.0f,12.0f);
        b.Step(w,tick,-20.0f,-1.0f,20.0f,12.0f);
    }
    bool f_same = a.leaves.size() == b.leaves.size();
    for (size_t i = 0; f_same && i < a.leaves.size(); i++){
        f_same = (a.leaves[i].x == b.leaves[i].x) && (a.leaves[i].y == b.leaves[i].y) &&
                 (a.leaves[i].angle == b.leaves[i].angle);
    }
    Check(f_same,"the same wind and view blow the same leaves");
}

static void TestMainLevelCost(){
    Stage st;
    WindField w;
    WindParams p = TestWind();
    w.Build(st.blocks,p);
    //As the app runs it: a normal view, padded 1.5, at the app's density.
    LeafSwarm s;
    s.params.density = 0.2f;
    s.params.pad = 1.5f;
    for (int64_t tick = 0; tick < 60; tick++){
        s.Step(w,tick,-10.0f,-2.0f,30.0f,20.0f);
    }
    auto t0 = std::chrono::steady_clock::now();
    for (int64_t tick = 60; tick < 660; tick++){
        s.Step(w,tick,-10.0f,-2.0f,30.0f,20.0f);
    }
    float ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count() / 600;
    printf("  main level: %zu leaves, %.3f ms a tick\n",s.leaves.size(),ms);
}

int main(){
    printf("leaves_test\n");
    TestCarried();
    TestSettle();
    TestLift();
    TestEddy();
    TestRecycle();
    TestZoom();
    TestDeterministic();
    TestMainLevelCost();
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
