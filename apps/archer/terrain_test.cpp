/*
    Checks for the terrain's ramps (docs/terrain_plan.md section 12): that a ramp melted into the
    field draws its slope ON the rules' line, that its ends sit into the blocks it is sealed by
    without poking out of them, that the slope is grass, and that a region with no ramps in it
    reads exactly as it did. Engine-free - the field alone, sampled through TerrainSurface, no
    mesher - and built and run by `make rules` as its own exe.
*/
#include <math.h>
#include <stdio.h>

#include "Stage.h"
#include "TerrainField.h"

static int checks = 0, failures = 0;

static void Check(bool f_ok, const char* what, const char* detail = NULL){
    checks++;
    if (!f_ok){
        failures++;
        printf("FAIL: %s%s%s\n",what,detail ? " - " : "",detail ? detail : "");
    }
}

static TerrainRegion Everywhere(){
    TerrainRegion r;
    r.x_min = -1e30f; r.x_max = 1e30f;
    r.y_min = -1e30f; r.y_max = 1e30f;
    return r;
}

static StageBlock Block(float l, float r, float bottom, float top){
    StageBlock b;
    b.x = (l + r) * 0.5f;
    b.y = (bottom + top) * 0.5f;
    b.hw = (r - l) * 0.5f;
    b.hh = (top - bottom) * 0.5f;
    b.kind = BLOCK_SOLID;
    b.f_alive = true;
    return b;
}

static StageRamp Ramp(float ax, float ay, float bx, float by){
    StageRamp r;
    r.a = v2(ax,ay);
    r.b = v2(bx,by);
    return r;
}

//Down from `from` at (x, z) to `to`, the first crossing into the solid; false if none.
static bool SurfaceY(const TerrainSurface& s, float x, float z, float from, float to, float* y_out){
    const float step = 0.002f;
    float prev_y = from;
    float prev_d = s.Distance(vec3(x,from,z));
    for (float y = from - step;y >= to;y -= step){
        float d = s.Distance(vec3(x,y,z));
        if ((prev_d > 0.0f) != (d > 0.0f)){
            *y_out = prev_y + (y - prev_y) * (-prev_d / (d - prev_d));
            return true;
        }
        prev_y = y;
        prev_d = d;
    }
    return false;
}

/*
    The test bed's shape, as the level will have it (docs/terrain_plan.md section 12): the cave's
    far wall and roof as one block at x -66, a 40 degree pitch off its top onto a shelf, a 22
    degree run from the shelf to the floor - and, mirrored, a gallery hill: a block with a ramp
    falling away to the right of it, so the high end on the LEFT is checked too.
*/
struct Bed{
    std::vector<StageBlock> blocks;
    std::vector<StageRamp>  ramps;
};

static Bed TestBed(){
    Bed bed;
    bed.blocks.push_back(Block(-106.0f,-40.0f,-4.0f,0.0f));         //the floor, top 0
    bed.blocks.push_back(Block(-66.0f,-60.0f,0.0f,11.0f));          //the far wall and the roof
    bed.blocks.push_back(Block(-72.34f,-66.0f,0.0f,8.2f));          //the shelf, run on under the pitch
    bed.ramps.push_back(Ramp(-69.34f,8.2f,-66.0f,11.0f));           //the steep pitch, 40
    bed.ramps.push_back(Ramp(-92.64f,0.0f,-72.34f,8.2f));           //the long run, 22
    bed.blocks.push_back(Block(-56.0f,-54.0f,0.0f,3.0f));           //a hill top
    bed.ramps.push_back(Ramp(-54.0f,3.0f,-47.0f,0.0f));             //falling right off it
    return bed;
}

static const float MIDDLE_IN = 0.6f;

static void TestOnTheLine(){
    printf("the slope is on the line\n");
    Bed bed = TestBed();
    TerrainParams params;
    TerrainSurface s;
    s.Build(bed.blocks,Everywhere(),params,&bed.ramps);
    char d[200];
    for (size_t i = 0;i < bed.ramps.size();i++){
        const StageRamp& r = bed.ramps[i];
        float worst_dip = 0.0f, worst_rise = 0.0f;
        int holes = 0;
        //Its middle, across the walk line and out to near the slab's front and back - the whole of
        //where she and the plants are. The ends are their own checks below: MIDDLE_IN from each,
        //which is the block's lip (0.22) and the union's reach (k) where a ramp meets a block.
        for (int k = 0;k <= 20;k++){
            float x = r.a.x + MIDDLE_IN + (r.b.x - r.a.x - 2.0f * MIDDLE_IN) * (float)k / 20.0f;
            float line = r.SurfaceY(x);
            for (float z = -1.2f;z <= 1.21f;z += 0.6f){
                float y = 0.0f;
                if (!SurfaceY(s,x,z,line + 1.5f,line - 1.0f,&y)){
                    holes++;
                    continue;
                }
                if (line - y > worst_dip){ worst_dip = line - y; }
                if (y - line > worst_rise){ worst_rise = y - line; }
            }
        }
        snprintf(d,sizeof(d),"ramp %i: dip %.4f rise %.4f, %i holes",(int)i,worst_dip,worst_rise,holes);
        printf("  %s\n",d);
        Check(holes == 0,"no hole over a ramp",d);
        Check(worst_dip < 0.005f,"the drawn slope never sinks below the rules' line",d);
        //Not zero: the fillet at a shallow ramp's foot is a corner so open that the union reaches a
        //unit and more up the slope - under a centimetre by MIDDLE_IN, and feet in grass anyway.
        Check(worst_rise < 0.01f,"nor stands above it, along its middle, by more than a centimetre",d);
    }
}

/*
    THE ENDS. The foot is where the union fills the inside corner - a rise, never a dip, and an
    obtuse corner's is small. The high end is cut inside the block it leans on: the block's top
    just past it must not be lifted by more than a seam between two blocks is (k/4).
*/
static void TestTheEnds(){
    printf("the ends\n");
    Bed bed = TestBed();
    TerrainParams params;
    TerrainSurface s;
    s.Build(bed.blocks,Everywhere(),params,&bed.ramps);
    char d[200];

    //The long run's foot, either side of it.
    float worst_foot = 0.0f, foot_dip = 0.0f;
    for (float x = -93.4f;x <= -91.6f;x += 0.05f){
        const StageRamp& r = bed.ramps[1];
        float ground = (x < r.a.x) ? 0.0f : r.SurfaceY(x);
        float y = 0.0f;
        if (SurfaceY(s,x,0.0f,ground + 1.5f,ground - 1.0f,&y)){
            if (y - ground > worst_foot){ worst_foot = y - ground; }
            if (ground - y > foot_dip){ foot_dip = ground - y; }
        }
    }
    snprintf(d,sizeof(d),"rise %.4f dip %.4f",worst_foot,foot_dip);
    printf("  the foot: %s\n",d);
    Check(foot_dip < 0.005f,"the foot of a ramp does not dip",d);
    Check(worst_foot < params.smooth_k * 0.25f + 0.01f,"and its fillet rises no more than a seam does (k/4)",d);

    //The high ends: the pitch into the wall's top (right), the gallery ramp into the hill's (left).
    //Walked from the end INTO the block: `dir` is which way that is. Within SEAM of the end the two
    //meet the way two blocks' tops do, lifted by the union up to about k/4; past it the block's top
    //is its own again, and anything the ramp's overrun left standing would show there.
    const float SEAM = 0.4f;
    struct End{ float x, dir, top; const char* what; } ends[] = {
        { -66.0f,   1.0f, 11.0f, "the pitch's high end, into the roof" },
        { -54.0f,  -1.0f,  3.0f, "the gallery ramp's high end, into the hill" },
        { -72.34f,  1.0f,  8.2f, "the long run's high end, into the shelf" },
    };
    for (const End& e : ends){
        float seam = 0.0f, past = 0.0f, dip = 0.0f;
        for (float u = 0.0f;u <= 1.6f;u += 0.02f){
            float y = 0.0f;
            if (SurfaceY(s,e.x + e.dir * u,0.0f,e.top + 1.5f,e.top - 1.0f,&y)){
                float rise = y - e.top;
                float& worst = (u < SEAM) ? seam : past;
                if (rise > worst){ worst = rise; }
                if (-rise > dip){ dip = -rise; }
            }
        }
        snprintf(d,sizeof(d),"%s: seam %.4f past it %.4f dip %.4f",e.what,seam,past,dip);
        printf("  %s\n",d);
        Check(seam < params.smooth_k * 0.25f + 0.03f,"a high end meets its block as a seam does",d);
        Check(past < 0.002f,"and leaves nothing standing on the block past the seam",d);
        Check(dip < 0.005f,"nor sags the block's top",d);
    }
}

//The slope is grass - the cap owns it - and its normal is the ramp's, so the light is.
static void TestGrassAndNormal(){
    printf("grass and normals\n");
    Bed bed = TestBed();
    TerrainParams params;
    TerrainSurface s;
    s.Build(bed.blocks,Everywhere(),params,&bed.ramps);
    std::vector<const StageBlock*> blocks;
    for (const StageBlock& b : bed.blocks){
        blocks.push_back(&b);
    }
    std::vector<bool> floating = TerrainFindFloating(blocks,bed.blocks);
    TerrainRampSet ramps;
    ramps.Gather(bed.ramps,Everywhere(),params);
    char d[200];
    int not_grass = 0;
    float worst_dot = 1.0f;
    for (size_t i = 0;i < bed.ramps.size();i++){
        const StageRamp& r = bed.ramps[i];
        float m = r.Slope();
        vec3 n(-m,1.0f,0.0f);
        n = n / n.length();
        for (int k = 0;k <= 8;k++){
            float x = r.a.x + MIDDLE_IN + (r.b.x - r.a.x - 2.0f * MIDDLE_IN) * (float)k / 8.0f;
            vec3 p(x,r.SurfaceY(x),0.0f);
            float cap = 0.0f, body = 0.0f;
            TerrainFieldAt(p,blocks,floating,ramps,params,&cap,&body);
            if (!(cap <= body + params.cap_eps)){
                not_grass++;
            }
            vec3 g = s.Normal(p);
            float dot = g.x * n.x + g.y * n.y + g.z * n.z;
            if (dot < worst_dot){ worst_dot = dot; }
        }
    }
    snprintf(d,sizeof(d),"%i not grass, worst normal dot %.4f",not_grass,worst_dot);
    printf("  %s\n",d);
    Check(not_grass == 0,"every point along a slope is the cap's - grass",d);
    Check(worst_dot > 0.99f,"and faces the way the ramp does",d);

    //The floor's grass lip, just under its top and out past its earth face: there in the open, gone
    //from under the long run - where it stood out of the wedge's face as a ledge.
    float lip_z = TerrainRampHalfDepth() + params.cap_lip_z * 0.5f;
    float open = s.Distance(vec3(-100.0f,-0.1f,lip_z));
    float under = s.Distance(vec3(-82.0f,-0.1f,lip_z));
    snprintf(d,sizeof(d),"in the open %.3f, under the run %.3f",open,under);
    Check(open < 0.0f && under > 0.0f,"a floor's grass lip does not run on under a ramp",d);
}

/*
    A REGION WITH NO RAMP IN IT IS UNCHANGED, bit for bit: the ramps add up after every block, so
    with none - or with the level's all outside the region - not one sample moves.
*/
static void TestUnchangedWithout(){
    printf("unchanged without ramps\n");
    Bed bed = TestBed();
    TerrainParams params;
    TerrainRegion right;                    //the bed's right half, which no ramp's middle is in
    right.x_min = -46.0f; right.x_max = 1e30f;
    right.y_min = -1e30f; right.y_max = 1e30f;
    std::vector<StageBlock> blocks;
    blocks.push_back(Block(-46.0f,-30.0f,-4.0f,0.0f));
    blocks.push_back(Block(-40.0f,-38.0f,0.0f,2.0f));
    TerrainSurface without, outside, empty;
    without.Build(blocks,right,params);
    outside.Build(blocks,right,params,&bed.ramps);
    std::vector<StageRamp> none;
    empty.Build(blocks,right,params,&none);
    int moved = 0, samples = 0;
    for (float x = -47.0f;x <= -29.0f;x += 0.37f){
        for (float y = -5.0f;y <= 3.5f;y += 0.29f){
            for (float z = -2.0f;z <= 2.0f;z += 0.5f){
                vec3 p(x,y,z);
                float a = without.Distance(p);
                if (a != outside.Distance(p) || a != empty.Distance(p)){
                    moved++;
                }
                samples++;
            }
        }
    }
    char d[120];
    snprintf(d,sizeof(d),"%i of %i samples moved",moved,samples);
    Check(moved == 0,"no ramp in the region, no change to the field",d);
    Check(!TerrainRegion(right).Contains(bed.ramps[2]) && Everywhere().Contains(bed.ramps[2]),
          "a ramp belongs to the region its middle is in");
}

/*
    ACROSS THE BAYS' SPLIT. The pitch's middle is above it and the shelf's below, so the ground bay
    melts the shelf and not the pitch - and still has to cut the shelf's grass out from under the
    pitch, or its lip stands out of the slope's earth face as a ledge, drawn by the other mesh.
*/
static void TestAcrossBays(){
    printf("across the bays\n");
    Bed bed = TestBed();
    TerrainParams params;
    TerrainRegion ground;
    ground.x_min = -1e30f; ground.x_max = 1e30f;
    ground.y_min = -1e30f; ground.y_max = 6.0f;
    TerrainSurface s;
    s.Build(bed.blocks,ground,params,&bed.ramps);
    float lip_z = TerrainRampHalfDepth() + params.cap_lip_z * 0.5f;
    float open = s.Distance(vec3(-71.0f,8.1f,lip_z));
    float under = s.Distance(vec3(-67.5f,8.1f,lip_z));
    char d[120];
    snprintf(d,sizeof(d),"the shelf's lip in the open %.3f, under the pitch %.3f",open,under);
    Check(!ground.Contains(bed.ramps[0]) && open < 0.0f && under > 0.0f,
          "a bay cuts its grass from under a slope the other bay draws",d);
    //Right to the wall, past the pitch's high end - where the last of the lip poked out beside the
    //cut - and not the roof's own grass up there, which is the top the pitch leads onto.
    float stub = s.Distance(vec3(-65.9f,8.1f,lip_z));
    std::vector<const StageBlock*> blocks;
    for (const StageBlock& b : bed.blocks){
        blocks.push_back(&b);
    }
    std::vector<bool> floating = TerrainFindFloating(blocks,bed.blocks);
    TerrainRampSet ramps;
    ramps.Gather(bed.ramps,ground,params);
    float cap = 0.0f, body = 0.0f;
    float roof = TerrainFieldAt(vec3(-65.9f,11.0f - 0.05f,lip_z),blocks,floating,ramps,params,&cap,&body);
    snprintf(d,sizeof(d),"the shelf's lip by the wall %.3f; the roof's lip at the pitch's top %.3f (cap %.3f body %.3f)",
             stub,roof,cap,body);
    Check(stub > 0.0f,"the shelf's lip is gone right up to the wall",d);
    Check(roof < 0.0f && cap <= body + params.cap_eps,"and the roof's grass at the top of the pitch is still there",d);
}

int main(){
    TestOnTheLine();
    TestTheEnds();
    TestGrassAndNormal();
    TestUnchangedWithout();
    TestAcrossBays();
    printf("terrain_test: %i checks, %i failed\n",checks,failures);
    return failures ? 1 : 0;
}
