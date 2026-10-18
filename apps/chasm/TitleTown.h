#ifndef _CHASM_TITLE_TOWN_H_
#define _CHASM_TITLE_TOWN_H_

#include <vector>
#include "ChasmWorld.h"
#include "Zones.h"

/*
    THE TITLE'S TOWN (test_maps_plan.md, step 6): what the title shows, made by code rather than captured from a game
    and kept as a file. A map of the title's own kind (GRID_KIND_TITLE: flat, no chasm, the game's forest), the town
    put up on it by Zones::PlaceTitleTown through the rules as a player's would be, standing at once, and its people as
    the diorama's `person` markers. The app captures it at start (ApplicationChasmTitle.cpp, BuildTitleTown), so the
    title is drawn by the game's own code and follows every change to it: there is nothing to capture again.

    Decided by the world alone, never a random stream: the same map always gives the same town.
*/

#define TITLE_TOWN_SEED     7           //the map's
#define TITLE_TOWN_CELLS    6000        //...and its size: about 190 x 105, room round the town for the forest
#define TITLE_TOWN_RADIUS   32.0f       //what the title shows: the capture's radius round the town's middle
#define TITLE_TOWN_WALL     17.0f       //its palisade's ring, round the middle; the forest grows nothing in it

//The town's middle: the most open ground near the map's middle, where the fewest forest cores stand - from the
//forest's cores alone (ForestData::core), so the forest can ask it too, before its props are kept.
vec2 TitleTownCentre(const Grid& g, const std::vector<uint8_t>& core);
vec2 TitleTownCentre(const ChasmWorld& w);

//One of its people, as a diorama's `person` marker holds him (ApplicationChasmTitle.cpp, DioramaLife).
struct TitleTownPerson{
    int figure = 0;             //PERSON_ADULT_MALE or PERSON_ADULT_FEMALE (ApplicationChasm.h)
    vec3 at;
    vec2 forward = vec2(0.0f,1.0f);
    const char* clip = "Idle";
    float phase = 0.0f;         //how far through the clip, 0..1
    int load = 0;               //what he carries, WORKER_LOADS' order: nothing, a log, a sack, a pail, a tent
    bool f_archer = false;
    std::vector<vec3> path;     //a walker's way on from `at`, walked there and back
    float speed = 0.0f;
};
//Its people, about the town Zones::PlaceTitleTown put up on `w`.
std::vector<TitleTownPerson> TitleTownPeople(const ChasmWorld& w, const ZoneState& z);

#endif
