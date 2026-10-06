#ifndef _CHASM_CALENDAR_H_
#define _CHASM_CALENDAR_H_

#include <stdint.h>
#include "type_vec2.h"

/*
    THE CALENDAR (gameplay_plan.md, P1): days, seasons and years, counted in ticks.

    Simulation state of one number - the ticks since the colony's first dawn - advanced by the tick
    and nothing else, saved and hashed with the zones. Everything else here is a FUNCTION OF THAT
    NUMBER: the date, the season, how far the snow has come. So a replay, a load and the view all
    agree on the weather without any of it being stored.

    THE PACE (gameplay_plan.md, "Pace"): a year is 30 minutes at normal speed, so a season 7.5
    minutes. Ten days a season makes a day 45 seconds - long enough that a walk across a village and
    back fits in one, which is what "daily" errands (P4, P5) will want. A colony starts on the first
    day of spring, year 1. The speed control changes how many ticks a second run, never what a tick
    is, so none of this depends on it.
*/

#define CALENDAR_TICKS_PER_SECOND   50          //the engine's tick (Application::physics_tps); checked at Init
#define CALENDAR_DAY_TICKS          2250        //45 s
#define CALENDAR_DAYS_PER_SEASON    10
#define CALENDAR_SEASONS            4
#define CALENDAR_SEASON_TICKS       (CALENDAR_DAY_TICKS * CALENDAR_DAYS_PER_SEASON)    //7.5 minutes
#define CALENDAR_YEAR_TICKS         (CALENDAR_SEASON_TICKS * CALENDAR_SEASONS)         //30 minutes

#define SEASON_SPRING   0
#define SEASON_SUMMER   1
#define SEASON_AUTUMN   2
#define SEASON_WINTER   3

struct CalendarDate{
    int year = 1;                   //from 1
    int season = SEASON_SPRING;
    int day = 1;                    //of the season, from 1
    float day_fraction = 0.0f;      //how far through the day, 0..1
    float season_fraction = 0.0f;   //how far through the season, 0..1
    float year_fraction = 0.0f;     //how far through the year, 0..1
};

CalendarDate CalendarDateOf(uint64_t tick);
const char* CalendarSeasonName(int season);

/*
    --- Winter's snow (gameplay_plan.md, "Seasons": winter pushes the snow down from the north) ----
    A FRONT running east-west, north of which everything is under snow. `cover` says how far south it
    has come: 0 is no snow beyond the mountain's own (Terrain::SnowLine, which stays all year), 1 is
    the furthest a winter goes - CALENDAR_SNOW_REACH of the map's depth from its north edge, which
    leaves the swamp and the desert in the south bare.

    Over a year: none through spring's second half, summer and most of autumn; the first snow edges
    in over autumn's last two days, the front sweeps south through winter's first four and holds
    there; spring's first five melt it back north. THE FIRST SPRING HAS NO SNOW - there was no winter
    before the colony - so a new game does not start buried.

    The front wanders north and south along its length (CalendarSnowWobble), so it does not cut the
    map along a ruler. shaders/chasm_ground.frag draws the same front from the same formula; this is
    the one the rules use (fields under snow do not grow, P3).
*/
#define CALENDAR_SNOW_REACH         0.65f       //of the map's depth, from the north edge
#define CALENDAR_SNOW_WOBBLE        14.0f       //world units the front wanders north and south

float CalendarSnowCover(uint64_t tick);
//The front's mean z for that cover, on a map from z_north (bounds_min.y) to z_south (bounds_max.y).
//With no cover it is past the north edge, by more than the wobble, so no ground is under it.
float CalendarSnowFrontZ(float cover, float z_north, float z_south);
//North (+) or south (-) of the mean line the front runs at x. Smooth, a few bends across the map.
float CalendarSnowWobble(float x);
bool CalendarSnowAt(const vec2& p, float front_z);

#endif
