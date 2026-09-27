/*
    Checks for Fireflies.{h,cpp}. Engine-free; built and run by `make rules` after streaks_test, as
    its own exe.
*/
#include <math.h>
#include <stdio.h>

#include "Fireflies.h"

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

static std::vector<StageBlock> StepLevel(){
    std::vector<StageBlock> blocks;
    blocks.push_back(Box(-60.0f,-2.0f,60.0f,0.0f));
    blocks.push_back(Box(0.0f,0.0f,6.0f,3.0f));
    return blocks;
}

//A row of homes on the tops from x0 to x1, one a unit - on the step where it stands, as the
//foliage the app takes them from would be. (A home under the step put its flies inside it.)
static std::vector<FireflyHome> Row(float x0, float x1){
    std::vector<FireflyHome> homes;
    for (float x = x0; x <= x1; x += 1.0f){
        FireflyHome h;
        h.x = x;
        h.y = ((x >= 0.0f) && (x <= 6.0f)) ? 3.0f : 0.0f;
        h.z = -0.5f;
        homes.push_back(h);
    }
    return homes;
}

static void TestGlow(){
    FireflySwarm s;
    float mid = s.Glow(3.0f,2);
    float peak = 0.0f;
    int maxima = 0;
    float prev = s.Glow(0.0f,3), prev2 = prev;
    for (float c = 0.0f; c < 5.0f; c += 0.005f){
        float g = s.Glow(c,3);
        peak = fmaxf(peak,g);
        if ((prev > g) && (prev > prev2) && (prev > 0.5f)){
            maxima++;
        }
        prev2 = prev;
        prev = g;
    }
    printf("  glow: %.2f between bursts, peak %.2f, %d flashes in a burst of 3\n",mid,peak,maxima);
    Check(fabsf(mid - s.params.ember) < 0.01f,"between bursts only the ember glows");
    Check(peak > 0.95f,"a flash reaches full brightness");
    Check(maxima == 3,"a burst of three is three flashes");
}

static void TestHomes(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    FireflySwarm s;
    s.SetHomes(Row(-20.0f,20.0f));
    s.params.count = 30;
    float worst_away = 0.0f, lowest = 1e9f, deepest = 1e9f;
    for (int64_t tick = 0; tick < 60 * 30; tick++){
        s.Step(w,tick,-25.0f,-1.0f,25.0f,12.0f);
        if (tick < 300){
            continue;
        }
        for (const Firefly& f : s.flies){
            if (f.home < 0){
                continue;
            }
            worst_away = fmaxf(worst_away,fabsf(f.x - (-20.0f + f.home)));
            lowest = fminf(lowest,f.y);
            deepest = fminf(deepest,w.Distance(f.x,f.y));
        }
    }
    printf("  30 s over a row of homes: furthest %.2f from home, lowest %.2f, nearest a block %.2f\n",
           worst_away,lowest,deepest);
    Check(worst_away < s.params.roam_x + 1.5f,"a firefly stays near its home, wind and all");
    Check(deepest > 0.1f,"and out of the blocks");
}

//How in step a set of flies is: 1 all flashing together, near 0 spread evenly round the clock.
static float Order(const FireflySwarm& s){
    float c = 0.0f, n = 0.0f, sn = 0.0f;
    for (const Firefly& f : s.flies){
        if (f.home < 0){
            continue;
        }
        float th = 6.2831853f * f.clock / f.period;
        c += cosf(th);
        sn += sinf(th);
        n += 1.0f;
    }
    return (n > 0.0f) ? sqrtf(c * c + sn * sn) / n : 0.0f;
}

static float OrderAfter(float sync, float seconds){
    WindField w;
    WindParams p = TestWind();
    p.speed = 0.0f;
    w.Build(StepLevel(),p);
    FireflySwarm s;
    s.SetHomes(Row(-22.0f,-18.0f));     //one cluster, all within each other's reach
    s.params.count = 20;
    s.params.sync = sync;
    int64_t ticks = (int64_t)(seconds * 60.0f);
    for (int64_t tick = 0; tick < ticks; tick++){
        s.Step(w,tick,-30.0f,-1.0f,-10.0f,10.0f);
    }
    return Order(s);
}

static void TestSync(){
    float start = OrderAfter(0.35f,0.1f);
    float with = OrderAfter(0.35f,60.0f);
    float without = OrderAfter(0.0f,60.0f);
    printf("  a cluster's order: %.2f at the start, %.2f after a minute coupled, %.2f uncoupled\n",
           start,with,without);
    Check(with > 0.8f,"a cluster falls into step");
    Check(with > without + 0.3f,"because they see each other flash");
}

static void TestMove(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    FireflySwarm s;
    s.SetHomes(Row(-40.0f,40.0f));
    s.params.count = 20;
    for (int64_t tick = 0; tick < 600; tick++){
        s.Step(w,tick,-40.0f,-1.0f,-20.0f,10.0f);
    }
    //The view moves right, clean off the first homes.
    for (int64_t tick = 600; tick < 900; tick++){
        s.Step(w,tick,20.0f,-1.0f,40.0f,10.0f);
    }
    int moved = 0;
    for (const Firefly& f : s.flies){
        moved += (f.home >= 0) && (f.x > 10.0f) && (f.fade > 0.9f);
    }
    printf("  5 s after the view moves 60 units: %d of 20 living in the new view\n",moved);
    Check(moved == 20,"fireflies follow the view to homes in it, fading between");
}

static void TestLights(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    FireflySwarm s;
    s.SetHomes(Row(-30.0f,30.0f));
    s.params.count = 40;
    for (int64_t tick = 0; tick < 600; tick++){
        s.Step(w,tick,-30.0f,-1.0f,30.0f,10.0f);
    }
    std::vector<FireflyLight> lights;
    s.LightGroup(3,-30.0f,-1.0f,30.0f,10.0f,lights);
    float total = 0.0f, lit = 0.0f;
    for (const Firefly& f : s.flies){
        if (f.x >= -30.0f && f.x <= 30.0f && f.y >= -1.0f && f.y <= 10.0f){
            total += f.brightness;
        }
    }
    bool f_banded = true;
    for (int b = 0; b < 3; b++){
        lit += lights[b].intensity;
        float lo = -30.0f + 20.0f * b, hi = lo + 20.0f;
        f_banded = f_banded && (lights[b].x >= lo - 1e-3f) && (lights[b].x <= hi + 1e-3f);
    }
    printf("  light group: %.2f %.2f %.2f, summing to %.2f of the flies' %.2f\n",
           lights[0].intensity,lights[1].intensity,lights[2].intensity,lit,total);
    Check(fabsf(lit - total) < 1e-3f,"the lights carry all the glow in view");
    Check(f_banded,"each light stays in its band");
}

static void TestDeterministic(){
    WindField w;
    WindParams p = TestWind();
    w.Build(StepLevel(),p);
    FireflySwarm a, b;
    a.SetHomes(Row(-20.0f,20.0f));
    b.SetHomes(Row(-20.0f,20.0f));
    for (int64_t tick = 0; tick < 600; tick++){
        a.Step(w,tick,-25.0f,-1.0f,25.0f,12.0f);
        b.Step(w,tick,-25.0f,-1.0f,25.0f,12.0f);
    }
    bool f_same = true;
    for (size_t i = 0; i < a.flies.size(); i++){
        f_same = f_same && (a.flies[i].x == b.flies[i].x) && (a.flies[i].brightness == b.flies[i].brightness);
    }
    Check(f_same,"deterministic");
}

int main(){
    printf("fireflies_test\n");
    TestGlow();
    TestHomes();
    TestSync();
    TestMove();
    TestLights();
    TestDeterministic();
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
