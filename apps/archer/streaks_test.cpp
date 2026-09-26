/*
    Checks for Streaks.{h,cpp} - the wind streaks. Engine-free; built and run by `make rules` after
    leaves_test, as its own exe.
*/
#include <math.h>
#include <stdio.h>

#include "Streaks.h"

static int checks = 0, failures = 0;

static void Check(bool f_ok, const char* what){
    checks++;
    if (!f_ok){
        failures++;
        printf("FAIL: %s\n",what);
    }
}

static StageBlock Box(float left, float bottom, float right, float top){
    StageBlock b;
    b.x = 0.5f * (left + right);
    b.y = 0.5f * (bottom + top);
    b.hw = 0.5f * (right - left);
    b.hh = 0.5f * (top - bottom);
    return b;
}

static std::vector<StageBlock> StepLevel(){
    std::vector<StageBlock> blocks;
    blocks.push_back(Box(-60.0f,-2.0f,60.0f,0.0f));
    blocks.push_back(Box(0.0f,0.0f,6.0f,3.0f));
    return blocks;
}

//A streak placed by hand, alive, with a long life.
static void Plant(StreakSwarm& s, int i, float x, float y){
    Streak& k = s.streaks[i];
    k.f_alive = true;
    k.x = x;
    k.y = y;
    k.z = 0.0f;
    k.age = 0;
    k.life = 100000;
    k.trail.assign({x,y});
}

static void TestTracer(){
    WindField w;
    WindParams p;
    p.gust_strength = 0.0f;
    p.wave_strength = 0.0f;
    w.Build(StepLevel(),p);
    StreakSwarm s;
    s.params.count = 1;
    s.streaks.resize(1);
    Plant(s,0,-35.0f,8.0f);
    //Long enough to fill the trail: a sample every sample_ticks, from one.
    for (int64_t tick = 0; tick < 90; tick++){
        s.Step(w,tick,-50.0f,-1.0f,-10.0f,20.0f);
    }
    float moved = s.streaks[0].x + 35.0f;
    printf("  a tracer upstream moved %.2f in 1.5 s of a %.1f wind; trail %zu points\n",
           moved,p.speed,s.streaks[0].trail.size() / 2);
    Check(fabsf(moved - 1.5f * w.MeanFlow(-35.0f,8.0f).x) < 0.2f,"a streak moves with the air, exactly");
    Check((int)s.streaks[0].trail.size() / 2 == s.params.points,"and keeps a trail of `points` samples");
}

//Total turning of a tracer's heading over its first `ticks`, in radians.
static float Turning(float eddy_strength, float x, float y, int ticks){
    WindField w;
    WindParams p;
    p.gust_strength = 0.0f;
    p.wave_strength = 0.0f;
    p.eddy_strength = eddy_strength;
    w.Build(StepLevel(),p);
    StreakSwarm s;
    s.params.count = 1;
    s.streaks.resize(1);
    Plant(s,0,x,y);
    float turn = 0.0f, last = 0.0f;
    float px = x, py = y;
    for (int64_t tick = 0; tick < ticks; tick++){
        s.Step(w,tick,-40.0f,-1.0f,40.0f,20.0f);
        if (!s.streaks[0].f_alive){
            break;
        }
        float hx = s.streaks[0].x - px, hy = s.streaks[0].y - py;
        px = s.streaks[0].x;
        py = s.streaks[0].y;
        float h = atan2f(hy,hx);
        if (tick > 0){
            float d = h - last;
            while (d > 3.14159265f){ d -= 6.2831853f; }
            while (d < -3.14159265f){ d += 6.2831853f; }
            turn += d;
        }
        last = h;
    }
    return turn;
}

static void TestCurl(){
    //Inside the lee bubble, just over the reverse flow.
    float with = Turning(1.0f,9.0f,0.8f,240);
    float without = Turning(0.0f,9.0f,0.8f,240);
    printf("  a tracer in the lee turns %.0f deg in 4 s with eddies, %.0f without\n",
           with * 57.2958f,without * 57.2958f);
    Check(fabsf(with) > 3.14159265f,"a streak in the lee curls right round");
    Check(fabsf(without) < 0.8f,"and without the eddy it barely bends");
}

static void TestGustBias(){
    WindField w;
    WindParams p;
    //Gusts every 15 s, so they cover a minority of the air - with them every 4 s they covered two
    //thirds of it, and no bias could put 1.5x that share of streaks under them.
    p.gust_strength = 0.8f;
    p.gust_period = 900;
    w.Build(StepLevel(),p);
    StreakSwarm s;
    s.params.count = 60;
    s.params.pad = 0.0f;
    const float x0 = -40.0f, x1 = 40.0f;
    int born = 0, born_in_gust = 0;
    long long samples = 0, samples_in_gust = 0;
    std::vector<bool> was_alive(60,false);
    for (int64_t tick = 0; tick < 60 * 120; tick++){
        s.Step(w,tick,x0,5.0f,x1,15.0f);
        for (int i = 0; i < 60; i++){
            const Streak& k = s.streaks[i];
            if (k.f_alive && k.age == 0){
                born++;
                born_in_gust += (w.GustFactor(k.x,tick) > 1.1f);
            }
        }
        for (float x = x0; x < x1; x += 2.0f){
            samples++;
            samples_in_gust += (w.GustFactor(x,tick) > 1.1f);
        }
    }
    float share_born = (float)born_in_gust / (float)(born > 0 ? born : 1);
    float share_area = (float)samples_in_gust / (float)samples;
    printf("  %d streaks born in 2 minutes; %.0f%% of them under a gust, which covers %.0f%% of the air\n",
           born,100.0f * share_born,100.0f * share_area);
    Check(share_born > 1.5f * share_area,"streaks gather where a gust is passing");
}

static void TestRibbons(){
    WindField w;
    WindParams p;
    w.Build(StepLevel(),p);
    StreakSwarm s;
    s.params.count = 40;
    for (int64_t tick = 0; tick < 1200; tick++){
        s.Step(w,tick,-30.0f,-1.0f,30.0f,15.0f);
    }
    std::vector<StreakVertex> verts;
    s.BuildRibbons(0.0f,5.0f,26.0f,verts);
    int expected = 0, alive = 0;
    for (const Streak& k : s.streaks){
        if (k.f_alive && k.trail.size() >= 4){
            alive++;
        }
    }
    bool f_alpha = true, f_uv = true;
    for (const StreakVertex& v : verts){
        f_alpha = f_alpha && (v.alpha >= 0.0f) && (v.alpha <= s.params.alpha + 1e-5f);
        f_uv = f_uv && (v.u >= 0.0f) && (v.u <= 1.0f) && (fabsf(fabsf(v.v) - 1.0f) < 1e-6f);
    }
    (void)expected;
    printf("  %d streaks alive, %zu ribbon vertices\n",alive,verts.size());
    Check(alive > 5,"a steady wind keeps streaks in the air");
    Check(verts.size() % 3 == 0 && !verts.empty(),"ribbons are whole triangles");
    Check(f_alpha,"alpha stays within the streak's own");
    Check(f_uv,"along 0..1, across -1 or 1");

    StreakSwarm t;
    t.params.count = 40;
    for (int64_t tick = 0; tick < 1200; tick++){
        t.Step(w,tick,-30.0f,-1.0f,30.0f,15.0f);
    }
    bool f_same = true;
    for (size_t i = 0; i < s.streaks.size(); i++){
        f_same = f_same && (s.streaks[i].x == t.streaks[i].x) && (s.streaks[i].f_alive == t.streaks[i].f_alive);
    }
    Check(f_same,"deterministic");
}

int main(){
    printf("streaks_test\n");
    TestTracer();
    TestCurl();
    TestGustBias();
    TestRibbons();
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
