/*
    Headless checks for core/Spline and core/SplineDeform.

    Both are pure maths on core's vector types - no GL, no window, no engine - so this links the
    two sources and the type helpers and nothing else. It checks the properties the vine and rope
    rely on and that are easy to get subtly wrong: the curve really passes through its points, a
    distance really is a distance, and the frame does not flip at an inflection.

    Build and run from the repo root:

        export PATH="/c/msys64/mingw64/bin:$PATH"
        g++ -std=c++17 -Wall -O1 -Icore -Icore/types tools/spline_test.cpp core/Spline.cpp \
            core/SplineDeform.cpp core/types/type_helpers.cpp -o build/spline_test.exe
        ./build/spline_test.exe

    Exit code 0 = all checks passed.
*/

#include <stdio.h>
#include <math.h>
#include <vector>

#include "Spline.h"
#include "SplineDeform.h"

static int num_failed = 0;
static int num_passed = 0;

static void Check(const char* what, bool f_ok, const char* detail_fmt = "", float a = 0.0f, float b = 0.0f){
    printf("  [%s] %-60s ",f_ok ? " ok " : "FAIL",what);
    printf(detail_fmt,a,b);
    printf("\n");
    if (f_ok){ num_passed++; }else{ num_failed++; }
}

static float Dist(const vec3& a, const vec3& b){
    return a.distance(b);
}

static const float PI = 3.14159265358979f;

//--- The curve itself -----------------------------------------------------------------------------

static void TestThroughPoints(){
    printf("\npasses through its points\n");
    Spline sp;
    sp.points = { vec3(0,0,0), vec3(1,2,0), vec3(3,2,1), vec3(4,-1,0), vec3(6,0,2) };
    Check("builds",sp.Build());
    float worst = 0.0f;
    for (size_t i = 0; i < sp.points.size(); i++){
        float d = Dist(sp.Evaluate((float)i),sp.points[i]);
        if (d > worst){ worst = d; }
    }
    Check("Evaluate(i) == points[i]",worst < 1e-5f,"worst %.2e",worst);
    Check("PositionAt(0) is the first point",Dist(sp.PositionAt(0.0f),sp.points[0]) < 1e-5f);
    Check("PositionAt(length) is the last point",Dist(sp.PositionAt(sp.GetLength()),sp.points.back()) < 1e-5f);
    Check("distances clamp at both ends",Dist(sp.PositionAt(-5.0f),sp.points[0]) < 1e-5f &&
                                        Dist(sp.PositionAt(1e6f),sp.points.back()) < 1e-5f);

    Spline one;
    one.points = { vec3(1,1,1) };
    Check("one point does not build",!one.Build());
    Check("...and still evaluates to its point",Dist(one.Evaluate(0.3f),vec3(1,1,1)) < 1e-6f);
}

static void TestStraightLine(){
    printf("\na straight line is a straight line\n");
    Spline sp;
    sp.points = { vec3(0,0,0), vec3(5,0,0) };
    sp.Build(0.01f);
    Check("length 5",fabsf(sp.GetLength() - 5.0f) < 1e-4f,"got %.6f",sp.GetLength());
    //Two points and reflected phantoms make both tangents the chord, so the speed is constant and
    //distance IS the parameter scaled - any curvature in the Hermite would show here.
    float worst = 0.0f;
    for (int i = 0; i <= 50; i++){
        float s = 5.0f * (float)i / 50.0f;
        float d = Dist(sp.PositionAt(s),vec3(s,0,0));
        if (d > worst){ worst = d; }
    }
    Check("PositionAt(s) == (s,0,0)",worst < 1e-4f,"worst %.2e",worst);
    SplineFrame f = sp.FrameAt(2.5f);
    Check("frame: tangent +X, normal is `up`",Dist(f.tangent,vec3(1,0,0)) < 1e-5f &&
                                            Dist(f.normal,vec3(0,1,0)) < 1e-5f);
    Check("frame: side = normal x tangent (-Z here)",Dist(f.side,vec3(0,0,-1)) < 1e-5f);
}

static void TestCircleLength(){
    printf("\narc length on a circle\n");
    //A quarter circle of radius 2 through 9 points. Catmull-Rom is not a circle, but through this
    //many points it is within a fraction of a percent of one - and a table measuring parameter
    //instead of distance would be off by far more.
    Spline sp;
    for (int i = 0; i <= 8; i++){
        float a = 0.5f * PI * (float)i / 8.0f;
        sp.points.push_back(vec3(2.0f * cosf(a),2.0f * sinf(a),0.0f));
    }
    sp.Build(0.005f);
    float expect = PI;      //quarter of 2*pi*2
    float err = fabsf(sp.GetLength() - expect) / expect;
    Check("length within 0.2% of pi",err < 0.002f,"got %.5f, err %.4f",sp.GetLength(),err);

    //Equal distances must be equal distances: step 0.1 along it and measure the chords.
    float lo = 1e9f, hi = 0.0f;
    for (int i = 0; i < 30; i++){
        float d = Dist(sp.PositionAt(0.1f * (float)i),sp.PositionAt(0.1f * (float)(i + 1)));
        if (d < lo){ lo = d; }
        if (d > hi){ hi = d; }
    }
    Check("equal steps in s are equal on the curve",hi - lo < 0.001f,"chord %.5f..%.5f",lo,hi);

    //Parameter <-> distance round trip.
    float worst = 0.0f;
    for (int i = 0; i <= 20; i++){
        float s = sp.GetLength() * (float)i / 20.0f;
        float back = sp.DistanceAtParam(sp.ParamAtDistance(s));
        if (fabsf(back - s) > worst){ worst = fabsf(back - s); }
    }
    Check("DistanceAtParam(ParamAtDistance(s)) == s",worst < 1e-4f,"worst %.2e",worst);
}

static void TestCentripetal(){
    printf("\ncentripetal: no loop between close points\n");
    //Two points nearly on top of each other in the middle of a path. Uniform Catmull-Rom throws a
    //loop there; centripetal stays inside the neighbourhood. Measure how far the short segment
    //strays from its own chord.
    Spline sp;
    sp.points = { vec3(0,0,0), vec3(4,0,0), vec3(4.05f,0.05f,0), vec3(4,4,0) };
    sp.Build(0.001f);
    float worst = 0.0f;
    for (int i = 0; i <= 100; i++){
        vec3 p = sp.Evaluate(1.0f + (float)i / 100.0f);
        float d = Dist(p,vec3(4.025f,0.025f,0.0f));
        if (d > worst){ worst = d; }
    }
    Check("short segment stays within 0.1 of its midpoint",worst < 0.1f,"worst %.4f",worst);

    Spline uniform = sp;
    uniform.alpha = 0.0f;
    uniform.Build(0.001f);
    float worst_u = 0.0f;
    for (int i = 0; i <= 100; i++){
        vec3 p = uniform.Evaluate(1.0f + (float)i / 100.0f);
        float d = Dist(p,vec3(4.025f,0.025f,0.0f));
        if (d > worst_u){ worst_u = d; }
    }
    //Not a property of this code so much as proof that the check above can fail.
    Check("...where uniform does stray further (the check has teeth)",worst_u > worst,
          "uniform %.4f vs centripetal %.4f",worst_u,worst);

    Spline dup;
    dup.points = { vec3(0,0,0), vec3(1,0,0), vec3(1,0,0), vec3(2,1,0) };
    Check("coincident points build",dup.Build());
    SplineFrame f = dup.FrameAt(dup.GetLength() * 0.5f);
    bool f_finite = isfinite(f.position.x) && isfinite(f.normal.x) && isfinite(f.tangent.x) &&
                    fabsf(f.tangent.length() - 1.0f) < 1e-4f;
    Check("...and give a finite, unit frame",f_finite);
}

//--- The frame ------------------------------------------------------------------------------------

static float WorstOrthonormal(const Spline& sp){
    float worst = 0.0f;
    for (int i = 0; i <= 400; i++){
        SplineFrame f = sp.FrameAt(sp.GetLength() * (float)i / 400.0f);
        float e = fabsf(f.tangent.length() - 1.0f) + fabsf(f.normal.length() - 1.0f) +
                  fabsf(f.side.length() - 1.0f) + fabsf(f.tangent.dot(f.normal)) +
                  fabsf(f.tangent.dot(f.side)) + fabsf(f.normal.dot(f.side));
        if (e > worst){ worst = e; }
    }
    return worst;
}

static void TestFrameNoFlip(){
    printf("\nrotation-minimising frame\n");
    /*
        An S in the XY plane, `up` out of the plane (+Z). The rotation-minimising normal of a PLANAR
        curve started perpendicular to its plane stays perpendicular to it all the way - the curve
        never asks it to turn. Frenet's normal would point at the centre of curvature: in the plane,
        and flipping sides at the inflection. So "normal == +Z everywhere" is exactly the property.
    */
    Spline s;
    s.points = { vec3(0,0,0), vec3(2,1.5f,0), vec3(4,0,0), vec3(6,-1.5f,0), vec3(8,0,0) };
    s.up = vec3(0,0,1);
    s.Build();
    float worst = 0.0f;
    for (int i = 0; i <= 200; i++){
        SplineFrame f = s.FrameAt(s.GetLength() * (float)i / 200.0f);
        float d = Dist(f.normal,vec3(0,0,1));
        if (d > worst){ worst = d; }
    }
    Check("planar S, up out of plane: normal stays +Z",worst < 1e-3f,"worst %.2e",worst);
    Check("frames orthonormal",WorstOrthonormal(s) < 1e-4f,"worst %.2e",WorstOrthonormal(s));

    //The same S with `up` IN the plane: the normal must stay in the plane, on one side.
    Spline t = s;
    t.up = vec3(0,1,0);
    t.Build();
    float worst_z = 0.0f;
    float min_dot = 1.0f;
    SplineFrame prev = t.FrameAt(0.0f);
    for (int i = 1; i <= 200; i++){
        SplineFrame f = t.FrameAt(t.GetLength() * (float)i / 200.0f);
        if (fabsf(f.normal.z) > worst_z){ worst_z = fabsf(f.normal.z); }
        float d = f.normal.dot(prev.normal);
        if (d < min_dot){ min_dot = d; }
        prev = f;
    }
    Check("planar S, up in plane: normal stays in plane",worst_z < 1e-3f,"worst |z| %.2e",worst_z);
    Check("...and turns smoothly (no step over 10 deg)",min_dot > cosf(10.0f * PI / 180.0f),
          "min dot %.4f",min_dot);

    //Up a wall: the first tangent IS `up`, so the frame has to pick a normal for itself.
    Spline wall;
    wall.points = { vec3(0,0,0), vec3(0,1,0), vec3(0,2,0.2f) };
    Check("vertical start builds",wall.Build());
    Check("...with an orthonormal frame",WorstOrthonormal(wall) < 1e-4f,"worst %.2e",WorstOrthonormal(wall));
}

static void TestHelixTwist(){
    printf("\nhelix: frame twist matches the known rotation-minimising rate\n");
    /*
        On a helix of radius a and pitch 2*pi*b, a rotation-minimising frame turns against the
        Frenet frame at the torsion, tau = b / (a^2 + b^2), per unit length. So after one full turn
        (length 2*pi*sqrt(a^2+b^2)) the RMF normal has rotated by 2*pi*b/sqrt(a^2+b^2) relative to
        the Frenet normal, which points at the axis. Measured as the angle between them.
    */
    float a = 1.0f, b = 0.5f;
    Spline h;
    for (int i = 0; i <= 64; i++){
        float t = 2.0f * PI * (float)i / 64.0f;
        h.points.push_back(vec3(a * cosf(t),a * sinf(t),b * t));
    }
    h.up = vec3(-1,0,0);        //Frenet normal at t = 0: toward the axis
    h.Build(0.002f);
    SplineFrame f0 = h.FrameAt(0.0f);
    SplineFrame f1 = h.FrameAt(h.GetLength());
    //Frenet normal at the end is (-1,0,0) again. The RMF normal has turned about the tangent.
    vec3 frenet_end = vec3(-1,0,0);
    frenet_end = frenet_end - f1.tangent * f1.tangent.dot(frenet_end);
    frenet_end.normalize();
    float cosang = f1.normal.dot(frenet_end);
    if (cosang > 1.0f){ cosang = 1.0f; }
    if (cosang < -1.0f){ cosang = -1.0f; }
    float got = acosf(cosang);
    float expect = 2.0f * PI * b / sqrtf(a * a + b * b);
    //expect is 2.81 rad; acos folds it into [0,pi], which it already is.
    //Not exactly (-1,0,0): the reflected phantom point makes the end tangent the CHORD to the next
    //point, half a step off the true tangent, so `up` is projected a few degrees. That is the
    //promise - up, as nearly as the tangent allows.
    vec3 up_proj = h.up - f0.tangent * f0.tangent.dot(h.up);
    up_proj.normalize();
    Check("start normal is `up` projected off the tangent",Dist(f0.normal,up_proj) < 1e-4f);
    Check("turned by 2*pi*b/sqrt(a^2+b^2) after one turn",fabsf(got - expect) < 0.03f,
          "got %.4f expect %.4f",got,expect);
}

static void TestClosest(){
    printf("\nclosest point\n");
    Spline sp;
    sp.points = { vec3(0,0,0), vec3(2,1,0), vec3(4,0,0) };
    sp.Build();
    float s = sp.ClosestDistance(vec3(2,3,0));
    Check("from above the apex, finds the apex",Dist(sp.PositionAt(s),vec3(2,1,0)) < 1e-3f);
    float s2 = sp.ClosestDistance(vec3(-3,0,0));
    Check("from beyond the start, finds the start",s2 < 1e-3f,"s %.4f",s2);
}

//--- The deform -----------------------------------------------------------------------------------

//A square tube along +Z, 1 long, 0.2 across: four quads, no caps.
static void MakeTube(std::vector<vertex>& out){
    out.clear();
    vec2 c[4] = { vec2(0.1f,0.1f), vec2(-0.1f,0.1f), vec2(-0.1f,-0.1f), vec2(0.1f,-0.1f) };
    for (int i = 0; i < 4; i++){
        vec2 a = c[i];
        vec2 b = c[(i + 1) % 4];
        vertex q[4];
        q[0].pos = vec3(a.x,a.y,0); q[1].pos = vec3(b.x,b.y,0);
        q[2].pos = vec3(b.x,b.y,1); q[3].pos = vec3(a.x,a.y,1);
        vec3 n = vec3(a.x + b.x,a.y + b.y,0);
        n.normalize();
        for (int k = 0; k < 4; k++){
            q[k].normal = n;
            q[k].tangent = vec3(0,0,1);
            q[k].uv = vec2(0,0);
            q[k].matid = 3;
        }
        //Outward-facing, counter-clockwise seen from outside.
        out.push_back(q[0]); out.push_back(q[1]); out.push_back(q[2]);
        out.push_back(q[0]); out.push_back(q[2]); out.push_back(q[3]);
    }
}

static void TestDeform(){
    printf("\ndeform along a curve\n");
    std::vector<vertex> tube;
    MakeTube(tube);

    Spline straight;
    straight.points = { vec3(0,0,0), vec3(0,0,3.2f) };
    straight.Build();
    std::vector<vertex> out;
    SplineDeformParams p;
    int n = DeformAlongSpline(straight,tube,p,out);
    Check("3.2 of curve, 1-long tile: 3 copies",n == 3,"got %.0f",(float)n);
    Check("vertex count = copies * tile",out.size() == tube.size() * 3);
    float zmax = 0.0f;
    for (size_t i = 0; i < out.size(); i++){
        if (out[i].pos.z > zmax){ zmax = out[i].pos.z; }
    }
    Check("stretched to end exactly at the curve's end",fabsf(zmax - 3.2f) < 1e-4f,"zmax %.5f",zmax);
    Check("matid carried over",out[0].matid == 3);

    //A curved path: every copy's end ring must meet the next copy's start ring, which is the
    //property the authoring rule exists for.
    Spline bend;
    bend.points = { vec3(0,0,0), vec3(1,0,2), vec3(3,1,3), vec3(5,0,3) };
    bend.Build();
    out.clear();
    n = DeformAlongSpline(bend,tube,p,out);
    float worst = 0.0f;
    size_t per = tube.size();
    for (int c = 0; c + 1 < n; c++){
        //Tile vertices at z=1 in copy c, against the same (x,y) at z=0 in copy c+1.
        for (size_t i = 0; i < per; i++){
            if (tube[i].pos.z < 0.5f){ continue; }
            for (size_t j = 0; j < per; j++){
                if (tube[j].pos.z > 0.5f || tube[j].pos.x != tube[i].pos.x || tube[j].pos.y != tube[i].pos.y){
                    continue;
                }
                float d = Dist(out[c * per + i].pos,out[(c + 1) * per + j].pos);
                if (d > worst){ worst = d; }
            }
        }
    }
    Check("seams close on a bend",worst < 1e-4f,"worst gap %.2e",worst);

    //Winding survives: a flat normal recomputed from the corners must point OUT of the tube, away
    //from the curve. If the frame were left-handed every face would turn inside out.
    SplineDeformParams pf = p;
    pf.f_flat_normals = true;
    out.clear();
    DeformAlongSpline(bend,tube,pf,out);
    int inward = 0;
    for (size_t i = 0; i + 2 < out.size(); i += 3){
        vec3 centre = (out[i].pos + out[i + 1].pos + out[i + 2].pos) / 3.0f;
        vec3 axis = bend.PositionAt(bend.ClosestDistance(centre));
        if (out[i].normal.dot(centre - axis) <= 0.0f){ inward++; }
    }
    Check("faces point outward (winding preserved)",inward == 0,"%.0f inward",(float)inward);

    //Taper: at the very end the cross-section is scaled to taper_end_scale.
    SplineDeformParams pt = p;
    pt.taper_end_length = 1.0f;
    pt.taper_end_scale = 0.25f;
    out.clear();
    DeformAlongSpline(straight,tube,pt,out);
    float end_r = 0.0f, mid_r = 0.0f;
    for (size_t i = 0; i < out.size(); i++){
        float r = sqrtf(out[i].pos.x * out[i].pos.x + out[i].pos.y * out[i].pos.y);
        if (fabsf(out[i].pos.z - 3.2f) < 1e-3f){ end_r = r; }
        if (fabsf(out[i].pos.z - 1.0667f) < 1e-2f){ mid_r = r; }
    }
    Check("taper: end at 0.25 of the middle",fabsf(end_r / mid_r - 0.25f) < 1e-3f,
          "end %.4f mid %.4f",end_r,mid_r);

    /*
        An overlay: the same tube, fatter, and overhanging the period at both ends the way a wrap's
        slanted strands do (z -0.05 .. 1.05 against the trunk's 0 .. 1). Measured on its own it is
        1.1 long and lays down different copies; given the trunk's period it must lay the SAME
        copies at the SAME distances, which is what keeps it on the trunk.
    */
    std::vector<vertex> overlay = tube;
    for (size_t i = 0; i < overlay.size(); i++){
        overlay[i].pos = vec3(overlay[i].pos.x * 1.5f,overlay[i].pos.y * 1.5f,-0.05f + overlay[i].pos.z * 1.1f);
    }
    Spline longer;
    longer.points = { vec3(0,0,0), vec3(0,1,3), vec3(0,0,6.3f) };
    longer.Build();
    std::vector<vertex> trunk_out, own_out, lock_out;
    int trunk_n = DeformAlongSpline(longer,tube,p,trunk_out);
    int own_n = DeformAlongSpline(longer,overlay,p,own_out);
    float tz0 = 0.0f, tlen = 0.0f;
    SplineDeformMeasure(tube,tz0,tlen);
    SplineDeformParams po = p;
    po.tile_start = tz0;
    po.tile_length = tlen;
    int lock_n = DeformAlongSpline(longer,overlay,po,lock_out);
    Check("measured on its own, the overhang lays a different count",own_n != trunk_n,
          "own %.0f trunk %.0f",(float)own_n,(float)trunk_n);
    Check("given the trunk's period, the same count",lock_n == trunk_n,
          "overlay %.0f trunk %.0f",(float)lock_n,(float)trunk_n);
    //The same overlay vertex in neighbouring copies sits exactly one trunk period apart along the
    //curve - the stretch included.
    float period = longer.GetLength() / (float)trunk_n;
    float worst_step = 0.0f;
    int compared = 0;
    //Interior copies only: the first and last copies' overhang is off the curve's ends, where a
    //closest-point search can only answer 0 or the length.
    for (int c = 1; c + 2 < lock_n; c++){
        for (size_t i = 0; i < per; i += 5){
            compared++;
            float a = longer.ClosestDistance(lock_out[c * per + i].pos);
            float b = longer.ClosestDistance(lock_out[(c + 1) * per + i].pos);
            float e = fabsf((b - a) - period);
            if (e > worst_step){ worst_step = e; }
        }
    }
    Check("...one trunk period apart, copy to copy",compared > 0 && worst_step < 2e-3f,
          "worst %.2e over %.0f",worst_step,(float)compared);

    //The first copy's overhang runs on BEFORE the curve's start, straight back along its
    //tangent - not squashed onto the end ring, which is what clamping the frame did.
    SplineFrame start = longer.FrameAt(0.0f);
    float stretch_o = longer.GetLength() / ((float)lock_n * tlen);
    float worst_over = 0.0f;
    for (size_t i = 0; i < per; i++){
        if (overlay[i].pos.z > -0.01f){
            continue;
        }
        float along = (lock_out[i].pos - start.position).dot(start.tangent);
        float want = overlay[i].pos.z * stretch_o;
        if (fabsf(along - want) > worst_over){ worst_over = fabsf(along - want); }
    }
    Check("overhang past the start continues along the tangent",worst_over < 1e-4f,
          "worst %.2e",worst_over);
}

int main(){
    printf("core/Spline + core/SplineDeform\n");
    TestThroughPoints();
    TestStraightLine();
    TestCircleLength();
    TestCentripetal();
    TestFrameNoFlip();
    TestHelixTwist();
    TestClosest();
    TestDeform();
    printf("\n%i passed, %i failed\n",num_passed,num_failed);
    return num_failed ? 1 : 0;
}
