#ifndef _CHASM_CLIMATE_H_
#define _CHASM_CLIMATE_H_

#include <stdint.h>
#include "type_vec2.h"

class Grid;
class Terrain;

/*
    THE CLIMATE (docs/steam_plan.md, "The chasm's heat, shown"): a temperature for a place and a date, in degrees, in
    two parts.

      - The AIR: the year's curve, warm in summer and below freezing in midwinter, a little colder to the north and
        colder still in winter's storm - and never above freezing where snow lies, so the readout and the white
        ground agree.
      - The CHASM'S WARMTH on top: the most at the open chasm's edge, fading inland, and more for every step down
        below the rim. Those steps are the ones a steam collector's yield is counted in: the warmth is WHY it
        gathers more lower down, and the readout beside the season is how the player sees that before building.

    A function of the date and the map alone, as the snow is: nothing saved, hashed or stepped, the same in a replay
    and in a pause. Only the view reads it today. The rules may later, since nothing here touches the weather's own
    clock - the storm it is given is the date's (WeatherStorm), not a gust.

    THE SNOW DOES NOT FOLLOW IT YET: the front lays snow by the date alone, so a northern balcony can read above
    freezing under snow. Letting the snow follow the heat is steam_plan.md's open question.
*/

//The air, degrees (current tuning). The year is a smooth curve, coldest a few days into winter - just as the snow's
//front has swept south - and warmest the same few days into summer.
#define CLIMATE_SUMMER         20.0f   //at its warmest
#define CLIMATE_WINTER         -8.0f   //at its coldest
#define CLIMATE_COLDEST        0.825f  //of the year: winter's third day
#define CLIMATE_NORTH          4.0f    //the north edge this much colder than the south, the middle of the map as tuned
#define CLIMATE_STORM          5.0f    //colder by this in the storm at full
#define CLIMATE_UNDER_SNOW     -1.0f   //the warmest the air is where snow lies

//The chasm's warmth, degrees on top of the air (current tuning).
#define CLIMATE_EDGE           5.0f    //at the open chasm's edge, and over all of it...
#define CLIMATE_EDGE_REACH     24.0f   //...gone this far inland from the rim
#define CLIMATE_PER_STEP       3.0f    //and this more for every TERRAIN_STEP a terrace stands below the rim

//Degrees, before the north, the storm and the snow.
float ClimateYear(uint64_t tick);
/*
    The air at a place: `north` is 0 at the map's south edge and 1 at its north, `storm` the storm there (0..1), and
    `f_snow` whether snow lies there.
*/
float ClimateAir(uint64_t tick, float north, float storm, bool f_snow);
/*
    How many steps of TERRAIN_STEP a plot stands below the rim, counted on a terrace only - a balcony or an island.
    A plot on the plateau is 0 however low it lies, so a river's channel or a cliff painted on the plateau warms
    nothing. A steam collector's yield is counted in these.
*/
int ClimateStepsDown(const Terrain& t, int plot);
//The chasm's warmth at p, standing on `plot`: by p's distance from the rim, and by the plot's steps down.
float ClimateChasm(const Grid& g, const Terrain& t, int plot, const vec2& p);

#endif
