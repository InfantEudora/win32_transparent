/*
    Checks for the cave's biome (docs/cave_plan.md): its own plants and rocks, still air, and that the
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

//In the cave's box - a hanging piece by the air just under the underside it hangs from, which the
//box's top edge does not hold.
static bool InCave(const StageBiome& cave, const Boulder& r){
    return InBox(cave,r.x,(BoulderMountOf(r.kind) == BOULDER_UNDER) ? r.ground - 0.01f : r.ground);
}

//Two pieces inside one another, by the rule for their mount; pieces on different mounts never are.
static bool Overlap(const Boulder& r, const Boulder& o, const BoulderParams& params, float slack){
    const int m = BoulderMountOf(r.kind);
    if (m != BoulderMountOf(o.kind)){
        return false;
    }
    if (m == BOULDER_ON_WALL){
        return BoulderWallOverlap(r,o,params,slack);
    }
    if (fabsf(r.ground - o.ground) > 0.3f){
        return false;
    }
    float ri = params.radius[r.kind] * r.scale, rj = params.radius[o.kind] * o.scale;
    float dx = r.x - o.x, dz = r.z - o.z;
    return sqrtf(dx * dx + dz * dz) < (ri + rj) * slack;
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
    for (const Boulder& r : plain){ if (!InCave(*cave,r)) a.push_back(r); }
    for (const Boulder& r : dressed){ if (!InCave(*cave,r)) b.push_back(r); }
    bool f_same = a.size() == b.size();
    for (size_t i = 0; f_same && i < a.size(); i++){
        f_same = a[i].kind == b[i].kind && a[i].x == b[i].x && a[i].z == b[i].z && a[i].scale == b[i].scale;
    }
    snprintf(d,sizeof(d),"%zu rocks outside it before, %zu after",a.size(),b.size());
    Check(f_same,"outside the cave, the rocks are rock for rock what they were",d);

    int inside = 0, big = 0, in_front = 0, overlaps = 0;
    for (size_t i = 0; i < dressed.size(); i++){
        const Boulder& r = dressed[i];
        if (BoulderFrontZ(r,params) > params.z_front_max + 0.001f) in_front++;
        for (size_t j = i + 1; j < dressed.size(); j++){
            const Boulder& o = dressed[j];
            //Only pairs with a cave rock in them. Two clusters are never checked against each
            //other, and two in the bay at x -31 do overlap - with or without the biome.
            if (!InCave(*cave,r) && !InCave(*cave,o)) continue;
            //The clusters' own small rocks may sit a little into their big one's footprint (0.8),
            //and a mushroom or a bone nestles closer to anything (Boulders.cpp, CAVE_NESTLE 0.65).
            const bool f_nestles = r.kind > BOULDER_SMALL_1 || o.kind > BOULDER_SMALL_1;
            if (Overlap(r,o,params,f_nestles ? 0.6f : 0.75f)) overlaps++;
        }
        if (InCave(*cave,r) && r.kind <= BOULDER_SMALL_1){
            inside++;
            big += IsBigBoulder(r.kind) ? 1 : 0;
        }
    }
    snprintf(d,sizeof(d),"%i rocks in the cave, %i of them big",inside,big);
    printf("  %s\n",d);
    Check(inside >= 6 && big >= 1,"the cave floor is strewn with rubble, a big rock or two among it",d);
    snprintf(d,sizeof(d),"%i in front of her line, %i overlapping",in_front,overlaps);
    Check(in_front == 0 && overlaps == 0,"every rock and cave piece behind her walking line, none inside another",d);
}

/*
    THE CAVE'S OWN (Boulders.h, THE CAVE'S FLOOR, ROOF AND WALLS): stalactites, stalagmites, bones,
    mushrooms, bracket fungus and the two set pieces - each on what it belongs on, and none of it
    anywhere but inside the cave. The roof's top is outside the box and stays bare for whatever
    stands on it.
*/
static void TestCavePieces(const Stage& s){
    printf("the cave's own\n");
    char d[300];
    const StageBiome* cave = Cave(s);
    if (!cave){
        return;
    }
    BoulderParams params;
    std::vector<Boulder> dressed, again;
    ScatterBoulders(s.blocks,params,dressed,&s.biomes);
    ScatterBoulders(s.blocks,params,again,&s.biomes);

    int count[BOULDER_KIND_COUNT] = {};
    int outside = 0, bad_hang = 0, bad_wall = 0, bad_top = 0, in_stream = 0, far_third = 0, mouth_third = 0;
    //Thirds rather than halves: one big patch just by the middle can tip a half either way.
    const float third = (cave->Right() - cave->Left()) / 3.0f;
    const float stream_front = -1.5f + 0.3f;        //the floor's back + 0.3 (Water.cpp)
    for (const Boulder& r : dressed){
        if (r.kind <= BOULDER_SMALL_1){
            continue;       //the rocks are TestRocks'
        }
        count[r.kind]++;
        if (!InCave(*cave,r)){
            outside++;
            continue;
        }
        //Only the kinds `deep` thickens: roots thin the other way, flats are even, set pieces are one.
        if (r.kind != BOULDER_ROOT && r.kind != BOULDER_FLAT && r.kind != BOULDER_SNAKE && r.kind != BOULDER_SKULL_STICK){
            far_third += (r.x < cave->Left() + third) ? 1 : 0;
            mouth_third += (r.x > cave->Right() - third) ? 1 : 0;
        }
        const float rr = params.radius[r.kind] * r.scale;
        const int m = BoulderMountOf(r.kind);
        if (m == BOULDER_UNDER){
            //From an underside that is there, over x, its root up in the rock.
            bool f_on = false;
            for (const StageBlock& U : s.blocks){
                f_on = f_on || (U.f_alive && fabsf(U.Bottom() - r.ground) < 0.001f && r.x > U.Left() && r.x < U.Right());
            }
            if (!f_on || r.y <= r.ground) bad_hang++;
        }else if (m == BOULDER_ON_WALL){
            //On a face, turned out of it, and not in the ground.
            bool f_on = false;
            for (const StageBlock& W : s.blocks){
                if (!W.f_alive || r.ground < W.Bottom() || r.ground > W.Top()) continue;
                if (fabsf(r.x - W.Right()) < 0.05f && cosf(r.yaw - 1.5708f) > 0.8f) f_on = true;
                if (fabsf(r.x - W.Left()) < 0.05f && cosf(r.yaw + 1.5708f) > 0.8f) f_on = true;
            }
            if (!f_on || r.ground < 0.3f) bad_wall++;
        }else{
            //On the cave's floor - never the roof's top - and out of the water unless it is stone.
            if (fabsf(r.ground) > 0.001f) bad_top++;
            const bool f_stone = r.kind == BOULDER_FLAT || r.kind == BOULDER_STALAGMITE || r.kind == BOULDER_SNAKE;
            if (!f_stone && r.z - rr < stream_front) in_stream++;
        }
    }
    snprintf(d,sizeof(d),"%i stalactites, %i roots, %i stalagmites, %i flat stones; bones %i/%i/%i; "
             "mushrooms %i/%i/%i; brackets %i/%i; snake %i, skull on a stick %i",
             count[BOULDER_STALACTITE],count[BOULDER_ROOT],count[BOULDER_STALAGMITE],count[BOULDER_FLAT],
             count[BOULDER_BONE],count[BOULDER_RIBCAGE],count[BOULDER_SKULL],count[BOULDER_MUSHROOM_1],
             count[BOULDER_MUSHROOM_2],count[BOULDER_CANTHARELL],count[BOULDER_BRACKET_1],count[BOULDER_BRACKET_2],
             count[BOULDER_SNAKE],count[BOULDER_SKULL_STICK]);
    printf("  %s\n",d);
    Check(outside == 0,"none of the cave's pieces is anywhere but in the cave");
    Check(count[BOULDER_STALACTITE] >= 6 && count[BOULDER_STALAGMITE] >= 3 && count[BOULDER_ROOT] >= 1,
          "stalactites from the roof, stalagmites on the floor, a root or two",d);
    Check(count[BOULDER_BONE] + count[BOULDER_RIBCAGE] + count[BOULDER_SKULL] >= 4 &&
          count[BOULDER_MUSHROOM_1] + count[BOULDER_MUSHROOM_2] + count[BOULDER_CANTHARELL] >= 10 &&
          count[BOULDER_BRACKET_1] + count[BOULDER_BRACKET_2] >= 2,"bones, mushrooms in clusters, and bracket fungus",d);
    Check(count[BOULDER_SNAKE] == 1 && count[BOULDER_SKULL_STICK] == 1,"one of each set piece",d);
    snprintf(d,sizeof(d),"%i hanging badly, %i off a wall, %i off the floor, %i in the stream",bad_hang,bad_wall,bad_top,in_stream);
    Check(bad_hang == 0 && bad_wall == 0 && bad_top == 0,"each on what it belongs on: an underside, a wall's face, the floor",d);
    Check(in_stream == 0,"and no bone or mushroom standing in the stream",d);
    snprintf(d,sizeof(d),"%i in the far third, %i in the third by the mouth",far_third,mouth_third);
    Check(far_third > mouth_third * 3 / 2,"thicker toward the back",d);

    bool f_same = again.size() == dressed.size();
    for (size_t i = 0; f_same && i < dressed.size(); i++){
        f_same = again[i].kind == dressed[i].kind && again[i].x == dressed[i].x && again[i].y == dressed[i].y &&
                 again[i].z == dressed[i].z && again[i].yaw == dressed[i].yaw && again[i].scale == dressed[i].scale;
    }
    Check(f_same,"the same cave every time");
    //A kind whose mesh did not load is never placed - and takes nothing else with it.
    BoulderParams none = params;
    none.radius[BOULDER_SNAKE] = 0.0f;
    none.radius[BOULDER_MUSHROOM_2] = 0.0f;
    std::vector<Boulder> without;
    ScatterBoulders(s.blocks,none,without,&s.biomes);
    int stray = 0, stalactites = 0;
    for (const Boulder& r : without){
        stray += (r.kind == BOULDER_SNAKE || r.kind == BOULDER_MUSHROOM_2) ? 1 : 0;
        stalactites += (r.kind == BOULDER_STALACTITE) ? 1 : 0;
    }
    snprintf(d,sizeof(d),"%i placed anyway; %i stalactites against %i",stray,stalactites,count[BOULDER_STALACTITE]);
    Check(stray == 0 && stalactites == count[BOULDER_STALACTITE],"a piece that did not load is never placed",d);
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
    TestCavePieces(s);
    TestWind(s);
    printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
