/*
    Checks for Wind.{h,cpp} - the wind field. Engine-free, like stage_test.cpp, and built and run
    by `make rules` right after it, as its own exe. -O2 there because the main level's flow solve
    takes seconds unoptimised.
*/
#include <math.h>
#include <stdio.h>
#include <chrono>

#include "Wind.h"

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

//Flat ground 100 wide with its top at 0, and a 6-wide, 3-tall step standing on it at x 0..6.
static std::vector<StageBlock> StepLevel(){
    std::vector<StageBlock> blocks;
    blocks.push_back(Box(-50.0f,-2.0f,50.0f,0.0f));
    blocks.push_back(Box(0.0f,0.0f,6.0f,3.0f));
    return blocks;
}

static WindParams Calm(float speed){
    WindParams p;
    p.speed = speed;
    p.eddy_strength = 0.0f;
    p.wave_strength = 0.0f;
    p.gust_strength = 0.0f;
    return p;
}

static void TestMeanFlow(){
    WindField w;
    WindParams p = Calm(2.5f);
    Check(w.Build(StepLevel(),p),"first build builds");
    printf("  step level: %dx%d nodes, %d iterations, residual %.2e, %.1f ms, %d components\n",
           w.Stats().nx,w.Stats().ny,w.Stats().iterations,w.Stats().residual,w.Stats().build_ms,w.Stats().components);
    Check(w.Stats().residual < 1e-4f,"the solver converges");
    Check(!w.Build(StepLevel(),p),"the same blocks again is a no-op");

    WindVec up = w.MeanFlow(-40.0f,8.0f);
    printf("  upstream (-40,8): %.3f %.3f\n",up.x,up.y);
    Check(fabsf(up.x - 2.5f) < 0.25f && fabsf(up.y) < 0.1f,"far upstream the wind is level and about `speed`");

    WindVec top = w.MeanFlow(3.0f,3.5f);
    printf("  over the step (3,3.5): %.3f %.3f\n",top.x,top.y);
    Check(top.x > 1.05f * up.x,"the flow speeds up over the top of the step");

    WindVec face = w.MeanFlow(-0.6f,1.5f);
    printf("  in front of the windward face (-0.6,1.5): %.3f %.3f\n",face.x,face.y);
    Check(face.x < 0.6f * up.x && face.y > 0.0f,"the flow slows and rises in front of the windward face");

    //Mirror the wind: the picture mirrors about the step's centre.
    WindParams m = Calm(-2.5f);
    w.SetParams(m);
    WindVec a = w.MeanFlow(-0.6f,1.5f);
    WindVec b = w.MeanFlow(6.6f,1.5f);
    Check(fabsf(a.x - (-face.x)) < 1e-4f,"a reversed wind is the same field reversed");
    Check(fabsf(b.x + face.x) < 0.02f && fabsf(b.y - face.y) < 0.02f,"and mirrors about a symmetric step");
}

static void TestCorners(){
    WindField w;
    WindParams p = Calm(2.5f);
    w.Build(StepLevel(),p);
    const std::vector<WindCorner>& c = w.Corners();
    Check(c.size() == 1,"one shedding corner for wind to the right");
    if (c.size() == 1){
        printf("  corner +x: (%.2f, %.2f) drop %.2f\n",c[0].x,c[0].y,c[0].drop);
        Check(fabsf(c[0].x - 6.0f) < 0.3f && fabsf(c[0].y - 3.0f) < 0.1f,"at the step's downwind top corner");
        Check(fabsf(c[0].drop - 3.0f) < 0.3f,"with the step's height as its drop");
    }
    p.speed = -2.5f;
    w.SetParams(p);
    Check(w.Corners().size() == 1 && fabsf(w.Corners()[0].x - 0.0f) < 0.3f,"wind to the left sheds off the other corner");

    //A floating slab 1.5 thick, 5 above the ground: its eddy is the size of its face, not of the
    //drop to the ground - found in-app, where island eddies hung in open air far below them.
    std::vector<StageBlock> slab;
    slab.push_back(Box(-50.0f,-2.0f,50.0f,0.0f));
    slab.push_back(Box(0.0f,5.0f,6.0f,6.5f));
    WindField ws;
    p.speed = 2.5f;
    ws.Build(slab,p);
    Check(ws.Corners().size() == 1 && fabsf(ws.Corners()[0].drop - 1.5f) < 0.3f,"a floating block's eddy is sized by its own face");
}

static void TestWalls(){
    WindField w;
    WindParams p;       //every layer on
    p.gust_strength = 0.0f;
    w.Build(StepLevel(),p);
    float worst = 0.0f;
    for (int64_t tick = 0; tick < 1200; tick += 37){
        for (float x = -10.0f; x < 20.0f; x += 0.37f){
            //Within half a unit of a corner the grid smears the surface; that is the grid, not the
            //model, and a leaf there is kept out of the block by the distance field anyway.
            if ((fabsf(x) < 0.5f) || (fabsf(x - 6.0f) < 0.5f)){
                continue;
            }
            float ground_top = ((x > 0.0f) && (x < 6.0f)) ? 3.0f : 0.0f;
            worst = fmaxf(worst,fabsf(w.Velocity(x,ground_top + 0.05f,tick).y));
        }
        for (float y = 0.5f; y < 2.5f; y += 0.23f){
            worst = fmaxf(worst,fabsf(w.Velocity(-0.05f,y,tick).x));
            worst = fmaxf(worst,fabsf(w.Velocity(6.05f,y,tick).x));
        }
    }
    printf("  worst flow INTO a surface, 0.05 off it, away from corners: %.3f (speed 2.5)\n",worst);
    //0.05 off a wall is a twentieth of the way up the ramp, where the eddies' own flow is not quite
    //zero yet: measured 0.151 (6% of the wind) at first, 0.030 since the eddies are fitted to the
    //room along them. The check is that it stays at that scale.
    Check(worst < 0.08f * 2.5f,"nothing blows into a surface");
}

static void TestDivergence(){
    WindField w;
    WindParams p;
    p.gust_strength = 0.0f;
    w.Build(StepLevel(),p);
    //Numeric divergence against the size of the velocity gradient itself - a field with no
    //structure has neither, so the ratio is what says "divergence-free".
    const float e = 0.05f;
    double div_sum = 0.0, grad_sum = 0.0;
    int n = 0;
    for (int64_t tick = 0; tick < 600; tick += 97){
        for (float x = -12.0f; x < 24.0f; x += 0.61f){
            for (float y = 0.4f; y < 12.0f; y += 0.53f){
                if (w.Distance(x,y) < 0.5f){
                    continue;
                }
                WindVec xp = w.Velocity(x + e,y,tick), xm = w.Velocity(x - e,y,tick);
                WindVec yp = w.Velocity(x,y + e,tick), ym = w.Velocity(x,y - e,tick);
                float dudx = (xp.x - xm.x) / (2 * e), dvdy = (yp.y - ym.y) / (2 * e);
                float dudy = (yp.x - ym.x) / (2 * e), dvdx = (xp.y - xm.y) / (2 * e);
                div_sum += fabsf(dudx + dvdy);
                grad_sum += fabsf(dudx) + fabsf(dvdy) + fabsf(dudy) + fabsf(dvdx);
                n++;
            }
        }
    }
    printf("  mean |div| %.4f against mean |grad| %.4f over %d points\n",div_sum / n,grad_sum / n,n);
    Check(div_sum < 0.1 * grad_sum,"the field is (nearly) divergence-free");
}

static void TestLeeEddy(){
    WindField w;
    WindParams calm = Calm(2.5f);
    w.Build(StepLevel(),calm);
    float lx = 7.5f, ly = 0.6f;
    float calm_u = w.MeanFlow(lx,ly).x;

    WindParams p = calm;
    p.eddy_strength = 0.8f;
    w.SetParams(p);
    //Average over a long run - many shedding cycles.
    double sum = 0.0;
    int reversed = 0, n = 0;
    for (int64_t tick = 0; tick < 3600; tick++){
        float u = w.Velocity(lx,ly,tick).x;
        sum += u;
        reversed += (u < 0.0f);
        n++;
    }
    printf("  lee (%.1f,%.1f): calm u %.3f, with eddies mean u %.3f, reversed %d%% of ticks\n",
           lx,ly,calm_u,sum / n,100 * reversed / n);
    Check(calm_u > 0.0f,"potential flow alone never reverses in the lee");
    Check(sum / n < 0.0,"the eddies turn the flow back toward the wall along the ground in the lee");

    //Up top the eddy turns WITH the wind: over the eddy, u exceeds the calm flow on average.
    float hx = 8.0f, hy = 2.6f;
    double hsum = 0.0;
    for (int64_t tick = 0; tick < 3600; tick++){
        hsum += w.Velocity(hx,hy,tick).x;
    }
    w.SetParams(calm);
    Check(hsum / 3600.0 > w.MeanFlow(hx,hy).x,"and with the wind over its top");

    //The reversed wind reverses its eddy, off the other corner.
    p.speed = -2.5f;
    w.SetParams(p);
    double msum = 0.0;
    for (int64_t tick = 0; tick < 3600; tick++){
        msum += w.Velocity(6.0f - lx,ly,tick).x;
    }
    Check(msum / 3600.0 > 0.0,"a wind to the left spins its lee eddy the other way");

    std::vector<WindEddy> eddies;
    w.Eddies(100,eddies);
    Check(eddies.size() == WIND_EDDIES_PER_CORNER,"one bound and two shed eddies per corner");
}

static void TestGusts(){
    WindField w;
    WindParams p = Calm(2.5f);
    p.gust_strength = 0.6f;
    w.Build(StepLevel(),p);
    float lo = 1e9f, hi = 0.0f;
    double sum = 0.0;
    int n = 0;
    for (int64_t tick = 0; tick < 6000; tick += 13){
        for (float x = -50.0f; x < 50.0f; x += 1.3f){
            float g = w.GustFactor(x,tick);
            lo = fminf(lo,g);
            hi = fmaxf(hi,g);
            sum += g;
            n++;
        }
    }
    printf("  gust factor: min %.3f max %.3f mean %.3f\n",lo,hi,sum / n);
    Check(lo >= 1.0f,"a gust only ever adds");
    Check(hi > 1.25f,"gusts reach well above calm");
    Check(sum / n > 1.02,"and are about often enough to notice");

    //Follow a front: the peak moves downwind at the travel speed.
    int64_t t0 = -1;
    float x0 = 0.0f;
    for (int64_t tick = 0; tick < 6000 && t0 < 0; tick++){
        float best = 1.0f, bx = 0.0f;
        for (float x = -30.0f; x < 0.0f; x += 0.1f){
            float g = w.GustFactor(x,tick);
            if (g > best){ best = g; bx = x; }
        }
        if (best > 1.1f){
            t0 = tick;
            x0 = bx;
        }
    }
    float best = 1.0f, x1 = 0.0f;
    for (float x = x0 - 5.0f; x < x0 + 20.0f; x += 0.1f){
        float g = w.GustFactor(x,t0 + 60);
        if (g > best){ best = g; x1 = x; }
    }
    printf("  a front: x %.2f at tick %lld, x %.2f a second later\n",x0,(long long)t0,x1);
    Check(t0 >= 0 && fabsf((x1 - x0) - 1.5f * 2.5f) < 0.6f,"a gust front travels downwind at 1.5x the wind");

    Check(w.GustFactor(3.0f,1234) == w.GustFactor(3.0f,1234),"deterministic");
}

static void TestMainLevel(){
    Stage s;
    WindField w;
    WindParams p;
    w.Build(s.blocks,p);
    const WindStats& st = w.Stats();
    printf("  main level: %d obstacles, %dx%d nodes (%.0f..%.0f, %.0f..%.0f), %d components, %d iterations, residual %.2e, %.1f ms, %d corners\n",
           st.obstacles,st.nx,st.ny,w.MinX(),w.MaxX(),w.MinY(),w.MaxY(),st.components,st.iterations,st.residual,st.build_ms,st.corners);
    Check(st.residual < 1e-4f,"the main level's flow converges");
    Check(st.corners > 0,"the main level has shedding corners");
    Check(st.end_walls == 2,"both of the main level's end walls are left out");

    bool f_finite = true;
    float fastest = 0.0f;
    for (float x = w.MinX(); x < w.MaxX(); x += 0.7f){
        for (float y = w.MinY(); y < w.MaxY(); y += 0.7f){
            WindVec v = w.Velocity(x,y,777);
            f_finite = f_finite && isfinite(v.x) && isfinite(v.y);
            fastest = fmaxf(fastest,sqrtf(v.x * v.x + v.y * v.y));
        }
    }
    printf("  fastest wind anywhere at tick 777: %.2f (speed %.2f)\n",fastest,p.speed);
    Check(f_finite,"no NaN anywhere in the field");
    Check(fastest < 4.0f * p.speed,"no jets");

    auto t0 = std::chrono::steady_clock::now();
    volatile float sink = 0.0f;
    const int N = 100000;
    for (int k = 0; k < N; k++){
        WindVec v = w.Velocity(w.MinX() + (k * 0.618f - floorf(k * 0.618f)) * (w.MaxX() - w.MinX()),
                               1.0f + (k % 17) * 0.8f,k);
        sink = sink + v.x;
    }
    float us = std::chrono::duration<float,std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
    printf("  Velocity(): %.3f us per sample\n",us);

    //The renderer's grid: the same numbers as Velocity(), for a view-sized patch, cheaply.
    {
        const int gw = 96, gh = 56;
        const float gx0 = -20.0f, gy0 = -2.0f, gstep = 0.5f;
        std::vector<float> grid(2 * gw * gh);
        float worst = 0.0f;
        for (int64_t tick : {0,777,5000}){
            w.Bake(tick,gx0,gy0,gstep,gw,gh,grid.data());
            for (int j = 0; j < gh; j += 3){
                for (int i = 0; i < gw; i += 3){
                    WindVec v = w.Velocity(gx0 + i * gstep,gy0 + j * gstep,tick);
                    worst = fmaxf(worst,fabsf(v.x - grid[2 * (j * gw + i)]));
                    worst = fmaxf(worst,fabsf(v.y - grid[2 * (j * gw + i) + 1]));
                }
            }
        }
        printf("  Bake vs Velocity: worst difference %.2e\n",worst);
        Check(worst < 1e-4f,"a baked grid is the field itself");
        auto b0 = std::chrono::steady_clock::now();
        for (int k = 0; k < 20; k++){
            w.Bake(k * 7,gx0,gy0,gstep,gw,gh,grid.data());
        }
        float ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - b0).count() / 20;
        printf("  Bake %dx%d: %.3f ms\n",gw,gh,ms);
    }

    //Every level builds.
    for (int level : {STAGE_LEVEL_RANGE,STAGE_LEVEL_ROPE}){
        Stage o;
        o.SetLevel(level);
        WindField wo;
        wo.Build(o.blocks,p);
        printf("  level %d: %dx%d, %.1f ms, %d corners\n",level,wo.Stats().nx,wo.Stats().ny,wo.Stats().build_ms,wo.Stats().corners);
        Check(wo.Stats().residual < 1e-4f,"every level's flow converges");
    }
}

int main(){
    printf("wind_test\n");
    TestMeanFlow();
    TestCorners();
    TestWalls();
    TestDivergence();
    TestLeeEddy();
    TestGusts();
    TestMainLevel();
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
