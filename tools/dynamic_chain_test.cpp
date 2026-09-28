/*
    Headless checks for core/DynamicChain.

    Pure maths on core's vector types - no GL, no window, no engine - so this links the one source
    and the type helpers and nothing else. It checks what the loose legs rely on and what is easy
    to get subtly wrong: the pose is exact at full stiffness and a true equilibrium when it hangs,
    a limp chain really hangs and really swings at a pendulum's period, a jerked pivot leaves the
    chain behind and it settles again, and the limits hold.

    Build and run from the repo root:

        export PATH="/c/msys64/mingw64/bin:$PATH"
        g++ -std=c++17 -Wall -O1 -Icore -Icore/types tools/dynamic_chain_test.cpp \
            core/DynamicChain.cpp core/types/type_helpers.cpp -o build/dynamic_chain_test.exe
        ./build/dynamic_chain_test.exe

    Exit code 0 = all checks passed.
*/

#include <stdio.h>
#include <math.h>
#include <vector>

#include "DynamicChain.h"

static int num_failed = 0;
static int num_passed = 0;

static void Check(const char* what, bool f_ok, const char* detail_fmt = "", float a = 0.0f, float b = 0.0f){
    printf("  [%s] %-66s ",f_ok ? " ok " : "FAIL",what);
    printf(detail_fmt,a,b);
    printf("\n");
    if (f_ok){ num_passed++; }else{ num_failed++; }
}

static const float PI = 3.14159265358979f;
static const vec3 PLANE = vec3(0.0f,0.0f,1.0f);

//A chain from `root`, each segment turned `bend[i]` radians about +Z from the one before, the first
//from straight down - a leg, as the side view sees it. `depth` offsets every point after the root
//along Z, so the planar checks have something to keep.
static std::vector<vec3> Limb(const vec3& root, const std::vector<float>& lens, const std::vector<float>& bend,
                              float depth = 0.0f){
    std::vector<vec3> out(1,root);
    float angle = 0.0f;
    for (size_t i = 0; i < lens.size(); i++){
        angle += bend[i];
        vec3 dir(sinf(angle),-cosf(angle),0.0f);
        vec3 p = out.back() + dir * lens[i];
        p.z = root.z + depth * (float)(i + 1);
        out.push_back(p);
    }
    return out;
}

static float WorstLengthError(const DynamicChain& c, const std::vector<vec3>& pose){
    float worst = 0.0f;
    const std::vector<vec3>& p = c.Points();
    for (size_t i = 1; i < p.size(); i++){
        float e = fabsf(p[i].distance(p[i - 1]) - pose[i].distance(pose[i - 1]));
        if (e > worst){ worst = e; }
    }
    return worst;
}

static void TestPose(){
    printf("\nthe pose\n");
    std::vector<float> lens = { 0.45f, 0.45f, 0.18f };
    std::vector<float> bend = { 0.3f, -0.5f, 1.4f };
    DynamicChainParams p;
    p.stiffness = 1.0f;
    p.plane_normal = PLANE;
    DynamicChain c;
    float worst = 0.0f;
    for (int t = 0; t < 300; t++){
        vec3 root(0.8f * sinf(0.05f * t),0.3f * cosf(0.11f * t),0.0f);
        std::vector<vec3> pose = Limb(root,lens,bend,0.02f);
        c.Step(pose,p,{});
        for (size_t i = 0; i < pose.size(); i++){
            float e = c.Points()[i].distance(pose[i]);
            if (e > worst){ worst = e; }
        }
    }
    Check("stiffness 1 is the pose exactly, however the root moves",worst < 1e-4f,"worst %.2e",worst);

    DynamicChainParams loose;
    loose.stiffness = 0.02f;
    loose.plane_normal = PLANE;
    DynamicChain h;
    std::vector<vec3> hang = Limb(vec3(0,2,0),lens,{ 0.0f, 0.0f, 0.0f });
    for (int t = 0; t < 600; t++){
        h.Step(hang,loose,{});
    }
    float drift = 0.0f;
    for (size_t i = 0; i < hang.size(); i++){
        drift = fmaxf(drift,h.Points()[i].distance(hang[i]));
    }
    Check("a pose hanging straight down is an equilibrium: loose, it stays",drift < 1e-4f,"drift %.2e",drift);
}

static void TestLimp(){
    printf("\nlimp\n");
    //Dead weight, held out sideways: it has to fall and end up hanging.
    std::vector<float> lens = { 0.5f, 0.5f };
    std::vector<vec3> out = Limb(vec3(0,3,0),lens,{ PI * 0.5f, 0.0f });
    DynamicChainParams p;
    p.stiffness = 0.0f;
    p.damping = 0.05f;
    p.plane_normal = PLANE;
    DynamicChain c;
    for (int t = 0; t < 1200; t++){
        c.Step(out,p,{});
    }
    const vec3& tip = c.Points().back();
    Check("stiffness 0 is dead weight: held out sideways, it ends up hanging",
          fabsf(tip.x) < 0.01f && fabsf(tip.y - 2.0f) < 0.01f,"tip (%.3f, %.3f)",tip.x,tip.y);
    Check("and keeps its lengths doing it",WorstLengthError(c,out) < 1e-4f,"worst %.2e",WorstLengthError(c,out));

    //A pendulum: one segment, no damping, let go at 10 degrees. Its period is 2 pi sqrt(L / g).
    const float L = 1.0f;
    std::vector<vec3> pose = Limb(vec3(0,5,0),{ L },{ 0.0f });
    DynamicChain pend;
    pend.Reset(Limb(vec3(0,5,0),{ L },{ 10.0f * PI / 180.0f }));
    DynamicChainParams q;
    q.stiffness = 0.0f;
    q.damping = 0.0f;
    q.plane_normal = PLANE;
    q.dt = 1.0f / 240.0f;
    float last_x = pend.Points()[1].x;
    std::vector<float> crossings;
    for (int t = 0; t < 240 * 10; t++){
        pend.Step(pose,q,{});
        float x = pend.Points()[1].x;
        if (last_x > 0.0f && x <= 0.0f){
            crossings.push_back((float)t * q.dt);
        }
        last_x = x;
    }
    float period = (crossings.size() >= 2) ? (crossings.back() - crossings.front()) / (float)(crossings.size() - 1) : 0.0f;
    float want = 2.0f * PI * sqrtf(L / 9.81f);
    Check("undamped, it swings at a pendulum's period",fabsf(period - want) / want < 0.03f,
          "%.3f s against %.3f",period,want);
}

static void TestJerk(){
    printf("\ninertia\n");
    std::vector<float> lens = { 0.45f, 0.45f };
    std::vector<float> bend = { 0.0f, 0.0f };
    DynamicChainParams p;
    p.stiffness = 0.03f;
    p.damping = 0.04f;
    p.plane_normal = PLANE;
    p.gravity = vec3(0.0f,-18.0f,0.0f);
    DynamicChain c;
    float first = 0.0f, most_behind = 0.0f, most_ahead = 0.0f, last = 0.0f;
    for (int t = 0; t < 900; t++){
        //Still, then moved 0.3 to the right over three ticks, then still again.
        float x = (t < 20) ? 0.0f : ((t < 23) ? 0.1f * (float)(t - 19) : 0.3f);
        c.Step(Limb(vec3(x,2,0),lens,bend),p,{});
        float dev = c.SegmentDeviation(0,PLANE);
        if (t == 23){ first = dev; }
        if (t >= 20){
            most_behind = fminf(most_behind,dev);
            most_ahead = fmaxf(most_ahead,dev);
        }
        last = dev;
    }
    Check("pulled right, the chain is left behind - it swings off the pose to the left",
          first < -0.05f,"%.3f rad",first);
    Check("then swings through and past it",most_ahead > 0.02f,"most ahead %.3f, most behind %.3f",most_ahead,most_behind);
    Check("and settles back on the pose",fabsf(last) < 1e-3f,"%.2e rad after 15 s",last);
}

static void TestPlaneAndLimits(){
    printf("\nplanar, and the limits\n");
    std::vector<float> lens = { 0.45f, 0.45f, 0.18f };
    std::vector<float> bend = { 0.0f, 0.4f, 1.3f };     //the knee bent the + way
    DynamicChainParams p;
    p.stiffness = 0.02f;
    p.damping = 0.02f;
    p.plane_normal = PLANE;
    p.gravity = vec3(0.0f,-18.0f,0.0f);
    std::vector<DynamicChainLimit> limits(3);
    limits[0].lo = -1.0f; limits[0].hi = 1.0f;
    limits[1].lo = -0.6f; limits[1].hi = 0.6f; limits[1].bend_sign = 1;
    limits[2].lo = -0.3f; limits[2].hi = 0.3f;
    DynamicChain c;
    float worst_z = 0.0f, worst_len = 0.0f, worst_knee = 0.0f, worst_ankle = 0.0f, worst_hip = 0.0f;
    float min_bend = 10.0f;
    for (int t = 0; t < 1200; t++){
        //Shaken hard, back and forth and up and down, so every limit is leaned on.
        vec3 root(0.6f * sinf(0.21f * t),2.0f + 0.4f * sinf(0.37f * t),0.0f);
        std::vector<vec3> pose = Limb(root,lens,bend,0.03f);
        c.Step(pose,p,limits);
        const std::vector<vec3>& s = c.Points();
        for (size_t i = 0; i < s.size(); i++){
            worst_z = fmaxf(worst_z,fabsf(s[i].z - pose[i].z));
        }
        worst_len = fmaxf(worst_len,WorstLengthError(c,pose));
        worst_hip = fmaxf(worst_hip,fabsf(c.SegmentDeviation(0,PLANE)));
        float knee = DynamicChain::SignedAngle(s[1] - s[0],s[2] - s[1],PLANE);
        float knee_pose = DynamicChain::SignedAngle(pose[1] - pose[0],pose[2] - pose[1],PLANE);
        float ankle = DynamicChain::SignedAngle(s[2] - s[1],s[3] - s[2],PLANE);
        float ankle_pose = DynamicChain::SignedAngle(pose[2] - pose[1],pose[3] - pose[2],PLANE);
        worst_knee = fmaxf(worst_knee,fabsf(knee - knee_pose));
        worst_ankle = fmaxf(worst_ankle,fabsf(ankle - ankle_pose));
        min_bend = fminf(min_bend,knee);
    }
    Check("every point stays in the play plane, at the depth the pose has it",worst_z < 1e-5f,"worst %.2e",worst_z);
    Check("every segment keeps its length",worst_len < 1e-4f,"worst %.2e",worst_len);
    Check("the whole chain swings no further off the pose than its limit",worst_hip < 1.0f + 1e-3f,"worst %.3f of 1.000",worst_hip);
    Check("the knee bends no more or less than its limit",worst_knee < 0.6f + 1e-3f,"worst %.3f of 0.600",worst_knee);
    Check("and never past straight - it folds one way only",min_bend > -1e-4f,"least bend %.3f",min_bend);
    Check("the ankle keeps within its own",worst_ankle < 0.3f + 1e-3f,"worst %.3f of 0.300",worst_ankle);

    //Free in 3D: no plane, still hangs and keeps its lengths.
    DynamicChainParams free_p = p;
    free_p.plane_normal = vec3(0.0f,0.0f,0.0f);
    free_p.stiffness = 0.0f;
    DynamicChain f;
    std::vector<vec3> out = Limb(vec3(0,3,0),{ 0.5f, 0.5f },{ 1.2f, 0.0f },0.2f);
    for (int t = 0; t < 1500; t++){
        f.Step(out,free_p,{});
    }
    const vec3& tip = f.Points().back();
    vec3 flat(tip.x,0.0f,tip.z);
    Check("free in 3D, a limp chain hangs straight down too",flat.length() < 0.01f && WorstLengthError(f,out) < 1e-4f,
          "tip off the vertical %.3f",flat.length());
}

static void TestSpheres(){
    printf("\nspheres\n");
    /*
        Hair on a head: a limp two-segment chain rooted on top of a ball, held out sideways so it
        falls round the ball. It must drape over the surface - never inside it - keep its lengths,
        and leave the pinned root alone although the root is inside the radius's reach.
    */
    DynamicChainParams p;
    p.stiffness = 0.0f;
    p.damping = 0.05f;
    DynamicChainSphere head;
    head.centre = vec3(0.0f,2.0f,0.0f);
    head.radius = 0.5f;
    p.spheres.push_back(head);
    std::vector<vec3> pose = { vec3(0.0f,2.55f,0.0f), vec3(0.4f,2.55f,0.1f), vec3(0.8f,2.55f,0.2f) };
    DynamicChain c;
    float deepest = 0.0f;
    for (int t = 0; t < 1200; t++){
        c.Step(pose,p,{});
        for (size_t i = 1; i < c.Points().size(); i++){
            deepest = fmaxf(deepest,head.radius - c.Points()[i].distance(head.centre));
        }
    }
    Check("draped over a ball, no particle ends up inside it",deepest < 0.01f,"deepest %.4f",deepest);
    Check("and the chain keeps its lengths doing it",WorstLengthError(c,pose) < 1e-3f,"worst %.2e",
          WorstLengthError(c,pose));
    Check("the root is pinned, not pushed",c.Points()[0].distance(pose[0]) < 1e-6f);
    Check("and it has fallen round the side, not stayed out straight",c.Points().back().y < 2.3f,
          "tip y %.3f",c.Points().back().y);
}

static void TestCone(){
    printf("\nthe 3D cone\n");
    /*
        A landing: a short limp-ish chain hanging from a root that falls at 19 u/s and stops dead in
        one tick, then stands still under ordinary gravity. Unlimited, that much energy on a 0.3
        chain swings it far past sideways; within a 0.6 rad cone it must never point further than
        that from its pose.
    */
    //Three segments, as the hair's back chain: the cones must NOT add up down it - every segment,
    //the last included, stays within the cone of where the pose points it.
    std::vector<float> lens = { 0.07f, 0.13f, 0.16f };
    std::vector<float> bend = { 0.25f, 0.0f, 0.0f };
    DynamicChainParams p;
    p.stiffness = 0.06f;
    p.damping = 0.08f;
    std::vector<DynamicChainLimit> cone(3);
    for (DynamicChainLimit& c : cone){
        c.cone = 0.6f;
    }
    float worst_free = 0.0f, worst_cone = 0.0f;
    for (int limited = 0; limited < 2; limited++){
        DynamicChain c;
        float y = 3.0f;
        for (int t = 0; t < 300; t++){
            if (t < 40){
                y -= 19.0f / 60.0f;         //falling fast, then stopped dead at t 40
            }
            std::vector<vec3> pose = Limb(vec3(0.0f,y,0.0f),lens,bend,0.05f);
            c.Step(pose,p,limited ? cone : std::vector<DynamicChainLimit>());
            for (size_t i = 1; i < pose.size(); i++){
                vec3 a = pose[i] - pose[i - 1];
                vec3 b = c.Points()[i] - c.Points()[i - 1];
                a.normalize();
                b.normalize();
                float angle = acosf(fminf(fmaxf(a.dot(b),-1.0f),1.0f));
                if (limited){ worst_cone = fmaxf(worst_cone,angle); }else{ worst_free = fmaxf(worst_free,angle); }
            }
        }
    }
    Check("unlimited, stopping dead flings it far off its pose",worst_free > 1.5f,"%.2f rad",worst_free);
    Check("within a cone NO segment points further off its pose than the cone",worst_cone < 0.6f + 1e-3f,
          "worst %.3f of 0.600",worst_cone);

    /*
        A GAME'S JUMP, as apps/archer's rules make one: still, then 18 u/s up in one tick, flight
        under 42 u/s^2 rising and 57 falling - with the chain's gravity matched to it, as the app
        does, so it is weightless in the air with her - and a dead stop on landing, back to 9.81.
        What the chain does STANDING STILL is the baseline: gravity alone sags this bent pose a long
        way at this stiffness, so the checks are against that, not against zero.
    */
    auto worst_off_pose = [&](float max_accel, bool f_jump) -> float {
        DynamicChainParams q = p;
        q.max_accel = max_accel;
        DynamicChain c;
        float y = 0.0f, v = 0.0f;
        bool f_air = false;
        float worst = 0.0f;
        for (int t = 0; t < 300; t++){
            if (f_jump && t == 20){
                v = 18.0f;
                f_air = true;
            }
            float g = 9.81f;
            if (f_air){
                g = (v > 0.0f) ? 42.0f : 56.7f;
                v -= g / 60.0f;
                y += v / 60.0f;
                if (y <= 0.0f){
                    y = 0.0f;
                    v = 0.0f;
                    f_air = false;
                    g = 9.81f;
                }
            }
            q.gravity = vec3(0.0f,-g,0.0f);
            std::vector<vec3> pose = Limb(vec3(0.0f,3.0f + y,0.0f),lens,bend,0.05f);
            c.Step(pose,q,{});
            for (size_t i = 1; i < pose.size(); i++){
                vec3 a = pose[i] - pose[i - 1];
                vec3 b = c.Points()[i] - c.Points()[i - 1];
                a.normalize();
                b.normalize();
                worst = fmaxf(worst,acosf(fminf(fmaxf(a.dot(b),-1.0f),1.0f)));
            }
        }
        return worst;
    };
    float still = worst_off_pose(0.0f,false);
    float jump_free = worst_off_pose(0.0f,true);
    float jump_capped = worst_off_pose(60.0f,true);
    float jump_tiny = worst_off_pose(0.01f,true);
    Check("a game's jump, felt in full, flings it well past its sag",jump_free > still + 0.5f,
          "%.2f rad against %.2f standing",jump_free,still);
    Check("capped at 60 u/s^2, the same jump barely moves it past its sag",jump_capped < still + 0.3f,
          "%.2f rad against %.2f standing",jump_capped,still);
    Check("a near-zero cap carries the body's motion whole - no worse than standing still",jump_tiny < still + 0.02f,
          "%.3f rad against %.3f standing",jump_tiny,still);
}

static void TestHousekeeping(){
    printf("\nresets and repeats\n");
    std::vector<float> lens = { 0.45f, 0.45f };
    std::vector<float> bend = { 0.2f, 0.3f };
    DynamicChainParams p;
    p.plane_normal = PLANE;
    DynamicChain c;
    for (int t = 0; t < 30; t++){
        c.Step(Limb(vec3(0.02f * t,2,0),lens,bend),p,{});
    }
    std::vector<vec3> far = Limb(vec3(8.0f,2,0),lens,bend);
    c.Step(far,p,{});
    float e = 0.0f;
    for (size_t i = 0; i < far.size(); i++){
        e = fmaxf(e,c.Points()[i].distance(far[i]));
    }
    Check("a root that jumps further than `teleport` starts again from the pose",e < 1e-6f,"off by %.2e",e);

    DynamicChain a, b;
    for (int t = 0; t < 500; t++){
        std::vector<vec3> pose = Limb(vec3(0.5f * sinf(0.1f * t),2,0),lens,bend);
        a.Step(pose,p,{});
        b.Step(pose,p,{});
    }
    bool f_same = true;
    for (size_t i = 0; i < a.Points().size(); i++){
        const vec3& u = a.Points()[i];
        const vec3& v = b.Points()[i];
        if (u.x != v.x || u.y != v.y || u.z != v.z){ f_same = false; }
    }
    Check("the same ticks give the same chain, bit for bit - it can be replayed",f_same);

    //Turning round: a chain hanging off-centre, the pose turned about a vertical axis a metre
    //away. Carried through each tick's turn it stays on the pose; left alone it is flung.
    DynamicChainParams t;
    t.stiffness = 0.05f;
    t.damping = 0.05f;
    t.gravity = vec3(0.0f,-18.0f,0.0f);
    DynamicChain carried, left;
    std::vector<vec3> hang0 = Limb(vec3(1.0f,2.0f,0.0f),lens,{ 0.0f, 0.0f });
    float worst_carried = 0.0f, worst_left = 0.0f;
    for (int i = 0; i <= 30; i++){
        float a = PI * (float)i / 30.0f;        //half a turn in half a second
        quat q(vec3(0.0f,1.0f,0.0f),a);
        std::vector<vec3> pose(hang0.size());
        for (size_t j = 0; j < pose.size(); j++){
            pose[j] = q * hang0[j];
        }
        if (i > 0){
            carried.Carry(quat(vec3(0.0f,1.0f,0.0f),PI / 30.0f),vec3(0.0f,0.0f,0.0f));
        }
        carried.Step(pose,t,{});
        left.Step(pose,t,{});
        for (size_t j = 0; j < pose.size(); j++){
            worst_carried = fmaxf(worst_carried,carried.Points()[j].distance(pose[j]));
            worst_left = fmaxf(worst_left,left.Points()[j].distance(pose[j]));
        }
    }
    Check("carried through a turn, the chain turns with the pose rather than trailing it",
          worst_carried < 1e-4f && worst_left > 0.05f,"off by %.2e carried, %.3f left alone",worst_carried,worst_left);

    //Following the pose's own motion: a soft chain on a waving limb, no gravity so the pose is its
    //own equilibrium. With follow the clip's wave arrives exactly; without, it is smeared.
    DynamicChainParams w;
    w.stiffness = 0.02f;
    w.damping = 0.05f;
    w.gravity = vec3(0.0f,0.0f,0.0f);
    w.plane_normal = PLANE;
    DynamicChainParams wf = w;
    wf.follow = 1.0f;
    DynamicChain smeared, followed, tilted;
    float worst_smeared = 0.0f, worst_followed = 0.0f, worst_tilted = 0.0f;
    for (int i = 0; i < 240; i++){
        float wave = 0.5f * sinf(2.0f * PI * 1.5f * (float)i / 60.0f);
        std::vector<vec3> pose = Limb(vec3(0,2,0),lens,{ wave, 0.6f * wave });
        smeared.Step(pose,w,{});
        followed.Step(pose,wf,{});
        //The same limb held still in its frame while the FRAME tilts: that is the body swinging,
        //and following the pose must not follow that.
        quat tilt(PLANE,wave);
        std::vector<vec3> held = Limb(vec3(0,2,0),lens,{ 0.0f, 0.0f });
        for (size_t j = 1; j < held.size(); j++){
            held[j] = held[0] + tilt * (held[j] - held[0]);
        }
        tilted.Step(held,wf,{},tilt);
        for (size_t j = 0; j < pose.size(); j++){
            worst_smeared = fmaxf(worst_smeared,smeared.Points()[j].distance(pose[j]));
            worst_followed = fmaxf(worst_followed,followed.Points()[j].distance(pose[j]));
            worst_tilted = fmaxf(worst_tilted,tilted.Points()[j].distance(held[j]));
        }
    }
    Check("following the pose, a soft chain plays the clip's own motion exactly",
          worst_followed < 1e-4f && worst_smeared > 0.05f,"off by %.2e followed, %.3f not",worst_followed,worst_smeared);
    Check("but the frame's own motion - the body swinging - is still left to inertia",
          worst_tilted > 0.05f,"trails by up to %.3f",worst_tilted);
}

int main(void){
    printf("--- core/DynamicChain ---\n");
    TestPose();
    TestLimp();
    TestJerk();
    TestPlaneAndLimits();
    TestSpheres();
    TestCone();
    TestHousekeeping();
    printf("\n%i passed, %i failed\n",num_passed,num_failed);
    return num_failed ? 1 : 0;
}
