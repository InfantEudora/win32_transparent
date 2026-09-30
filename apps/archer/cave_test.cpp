/*
    Checks for the cave's biome (cave_plan.md): its own plants and rocks, still air, and that the
    rest of the level is dressed exactly as it was. Engine-free; built and run by `make rules`
    after water_test, as its own exe.
*/
#include <math.h>
#include <stdio.h>

#include "Stage.h"
#include "Foliage.h"
#include "Boulders.h"
#include "Wind.h"

static int checks = 0, failures = 0;

static void Check(bool f_ok, const char* what, const char* detail = NULL){
    checks++;
    if (!f_ok){
        failures++;
        printf("FAIL: %s%s%s\n",what,detail ? " - " : "",detail ? detail : "");
    }
}

static const StageBiome* Cave(const Stage& s){
    for (const StageBiome& b : s.biomes){
        if (b.kind == BIOME_CAVE){
            return &b;
        }
    }
    return NULL;
}

static bool InBox(const StageBiome& b, float x, float y){
    return b.Contains(x,y);
}

static void TestBiome(const Stage& s){
    printf("the biome\n");
    char d[160];
    const StageBiome* cave = Cave(s);
    Check(cave != NULL,"the main level has a cave biome");
    if (!cave){
        return;
    }
    Check(fabsf(cave->Left() - ARCHER_CAVE_X_MIN) < 0.001f && fabsf(cave->Right() - ARCHER_TEST_BAY_X_MIN) < 0.001f &&
          fabsf(cave->Bottom()) < 0.001f && fabsf(cave->Top() - ARCHER_CAVE_ROOF_Y) < 0.001f,
          "its box is the inside: floor to roof, far wall to mouth");
    float w = 0.0f;
    Check(BiomeAt(&s.biomes,-55.0f,0.0f,&w) == BIOME_CAVE && w == 1.0f,"deep inside is all cave");
    int k = BiomeAt(&s.biomes,-42.0f,0.0f,&w);
    snprintf(d,sizeof(d),"weight %.3f",w);
    Check(k == BIOME_CAVE && fabsf(w - 2.0f / cave->fade_right) < 0.001f,"two units in from the mouth it is a third cave",d);
    Check(BiomeAt(&s.biomes,-30.0f,0.0f,&w) == BIOME_JUNGLE && w == 1.0f,"the bay is jungle");
    Check(BiomeAt(&s.biomes,-55.0f,12.0f) == BIOME_JUNGLE,"and so is the top of the roof");
}

/*
    THE REST OF THE LEVEL IS UNTOUCHED - the same plants, in the same order, with the same
    numbers. The cave's rules are multipliers of 1 and draws that are only made under 1 outside it,
    and this is the check that that is so.
*/
static void TestFoliage(const Stage& s){
    printf("the plants\n");
    char d[200];
    const StageBiome* cave = Cave(s);
    if (!cave){
        return;
    }
    std::vector<bool> grows(s.blocks.size(),true);
    FoliageParams params;
    std::vector<FoliagePlant> plain, dressed;
    ScatterFoliage(s.blocks,grows,params,plain);
    ScatterFoliage(s.blocks,grows,params,dressed,&s.biomes);
    std::vector<FoliagePlant> a, b;
    for (const FoliagePlant& p : plain){ if (!InBox(*cave,p.x,p.y)) a.push_back(p); }
    for (const FoliagePlant& p : dressed){ if (!InBox(*cave,p.x,p.y)) b.push_back(p); }
    bool f_same = a.size() == b.size();
    for (size_t i = 0; f_same && i < a.size(); i++){
        f_same = a[i].kind == b[i].kind && a[i].x == b[i].x && a[i].z == b[i].z && a[i].scale == b[i].scale;
    }
    snprintf(d,sizeof(d),"%zu plants outside it before, %zu after",a.size(),b.size());
    Check(f_same && !a.empty(),"outside the cave, the garden is plant for plant what it was",d);

    //Inside, past the fade: no grass, no flowers, a few low ferns, hardly a tall one.
    const float deep_right = cave->Right() - cave->fade_right;
    int kinds_deep[FOLIAGE_KIND_COUNT] = {}, kinds_before[FOLIAGE_KIND_COUNT] = {};
    int fade_grass = 0, fade_grass_before = 0;
    for (const FoliagePlant& p : dressed){
        if (InBox(*cave,p.x,p.y) && p.x < deep_right) kinds_deep[p.kind]++;
        if (InBox(*cave,p.x,p.y) && p.x >= deep_right && (p.kind == FOLIAGE_GRASS || p.kind == FOLIAGE_GRASS_2)) fade_grass++;
    }
    for (const FoliagePlant& p : plain){
        if (InBox(*cave,p.x,p.y) && p.x < deep_right) kinds_before[p.kind]++;
        if (InBox(*cave,p.x,p.y) && p.x >= deep_right && (p.kind == FOLIAGE_GRASS || p.kind == FOLIAGE_GRASS_2)) fade_grass_before++;
    }
    snprintf(d,sizeof(d),"deep inside: %i ferns, %i low ferns, %i flowers, %i + %i grass (jungle rules: %i, %i, %i, %i + %i)",
             kinds_deep[FOLIAGE_FERN],kinds_deep[FOLIAGE_FERN_LOW],kinds_deep[FOLIAGE_FLOWER],
             kinds_deep[FOLIAGE_GRASS],kinds_deep[FOLIAGE_GRASS_2],kinds_before[FOLIAGE_FERN],
             kinds_before[FOLIAGE_FERN_LOW],kinds_before[FOLIAGE_FLOWER],kinds_before[FOLIAGE_GRASS],
             kinds_before[FOLIAGE_GRASS_2]);
    printf("  %s\n",d);
    Check(kinds_deep[FOLIAGE_GRASS] + kinds_deep[FOLIAGE_GRASS_2] == 0 && kinds_deep[FOLIAGE_FLOWER] == 0,
          "deep inside: no grass and no flowers",d);
    Check(kinds_deep[FOLIAGE_FERN_LOW] > 0 && kinds_deep[FOLIAGE_FERN] <= kinds_deep[FOLIAGE_FERN_LOW],
          "a few low ferns, and fewer tall ones than low",d);
    snprintf(d,sizeof(d),"%i clumps in the fade, %i under jungle rules",fade_grass,fade_grass_before);
    Check(fade_grass > 0 && fade_grass < fade_grass_before,"the grass thins in from the mouth rather than stopping on a line",d);
}

static void TestRocks(const Stage& s){
    printf("the rocks\n");
    char d[200];
    const StageBiome* cave = Cave(s);
    if (!cave){
        return;
    }
    BoulderParams params;
    std::vector<Boulder> plain, dressed;
    ScatterBoulders(s.blocks,params,plain);
    ScatterBoulders(s.blocks,params,dressed,&s.biomes);
    std::vector<Boulder> a, b;
    for (const Boulder& r : plain){ if (!InBox(*cave,r.x,r.ground)) a.push_back(r); }
    for (const Boulder& r : dressed){ if (!InBox(*cave,r.x,r.ground)) b.push_back(r); }
    bool f_same = a.size() == b.size();
    for (size_t i = 0; f_same && i < a.size(); i++){
        f_same = a[i].kind == b[i].kind && a[i].x == b[i].x && a[i].z == b[i].z && a[i].scale == b[i].scale;
    }
    snprintf(d,sizeof(d),"%zu rocks outside it before, %zu after",a.size(),b.size());
    Check(f_same,"outside the cave, the rocks are rock for rock what they were",d);

    int inside = 0, big = 0, in_front = 0, overlaps = 0;
    for (size_t i = 0; i < dressed.size(); i++){
        const Boulder& r = dressed[i];
        float ri = params.radius[r.kind] * r.scale;
        if (r.z + ri > params.z_front_max + 0.001f) in_front++;
        for (size_t j = i + 1; j < dressed.size(); j++){
            const Boulder& o = dressed[j];
            //Only pairs with a cave rock in them. Two clusters are never checked against each
            //other, and two in the bay at x -31 do overlap - with or without the biome.
            if (!InBox(*cave,r.x,r.ground) && !InBox(*cave,o.x,o.ground)) continue;
            float rj = params.radius[o.kind] * o.scale;
            float dx = r.x - o.x, dz = r.z - o.z;
            //The clusters' own small rocks may sit a little into their big one's footprint (0.8).
            if (sqrtf(dx * dx + dz * dz) < (ri + rj) * 0.75f) overlaps++;
        }
        if (InBox(*cave,r.x,r.ground)){
            inside++;
            big += IsBigBoulder(r.kind) ? 1 : 0;
        }
    }
    snprintf(d,sizeof(d),"%i rocks in the cave, %i of them big",inside,big);
    printf("  %s\n",d);
    Check(inside >= 6 && big >= 1,"the cave floor is strewn with rubble, a big rock or two among it",d);
    snprintf(d,sizeof(d),"%i in front of her line, %i overlapping",in_front,overlaps);
    Check(in_front == 0 && overlaps == 0,"every rock behind her walking line, none inside another",d);
}

/*
    STILL AIR. The cave, solid to the wind: nothing moves inside it, the wind goes over the roof
    instead - and without the biome it blew straight through, in at the far end and out at the
    mouth, which is what the debug view showed.
*/
static void TestWind(const Stage& s){
    printf("the air\n");
    char d[200];
    WindParams p;
    p.gust_strength = 0.0f;         //the flow, not whichever gust is passing
    WindField open, still;
    open.Build(s.blocks,p);
    still.Build(WindBlocks(s.blocks,s.biomes),p);
    float open_inside = 0.0f, still_inside = 0.0f;
    for (float x = -62.0f; x < -42.0f; x += 1.0f){
        for (float y = 0.5f; y < 8.5f; y += 1.0f){
            WindVec a = open.Velocity(x,y,500);
            WindVec b = still.Velocity(x,y,500);
            open_inside = fmaxf(open_inside,sqrtf(a.x * a.x + a.y * a.y));
            still_inside = fmaxf(still_inside,sqrtf(b.x * b.x + b.y * b.y));
        }
    }
    snprintf(d,sizeof(d),"fastest inside: %.3f through it before, %.3f still (wind %.2f)",open_inside,still_inside,p.speed);
    printf("  %s\n",d);
    Check(open_inside > 0.3f * p.speed,"without the biome the wind blows through the cave",d);
    Check(still_inside < 0.02f * p.speed,"with it, the air in the cave is still",d);
    Check(still.Distance(-55.0f,4.0f) < 0.0f,"and the cave is inside an obstacle, so leaves, streaks and fireflies keep out");
    WindVec over = still.MeanFlow(-55.0f,13.0f);
    snprintf(d,sizeof(d),"over the roof: %.2f, %.2f",over.x,over.y);
    Check(over.x > 0.5f * p.speed,"the wind goes over the roof instead",d);
    float fastest = 0.0f;
    for (float x = still.MinX(); x < still.MaxX(); x += 0.7f){
        for (float y = still.MinY(); y < still.MaxY(); y += 0.7f){
            WindVec v = still.Velocity(x,y,777);
            fastest = fmaxf(fastest,sqrtf(v.x * v.x + v.y * v.y));
        }
    }
    snprintf(d,sizeof(d),"fastest anywhere %.2f",fastest);
    Check(fastest < 4.0f * p.speed,"and no jet over it",d);
    Check(WindField::KeyFor(WindBlocks(s.blocks,s.biomes),p) != WindField::KeyFor(s.blocks,p),
          "a still-air biome is part of what the field is keyed on");
}

int main(){
    printf("cave_test\n");
    Stage s;
    TestBiome(s);
    TestFoliage(s);
    TestRocks(s);
    TestWind(s);
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
