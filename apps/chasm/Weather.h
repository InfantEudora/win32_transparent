#ifndef _CHASM_WEATHER_H_
#define _CHASM_WEATHER_H_

#include <stdint.h>
#include "type_vec2.h"

/*
    THE WEATHER (docs/weather_plan.md): winter's storm, which travels with the snow's front (Calendar.h), and
    autumn's breeze. View only, like the snow: nothing saved or hashed, and the rules never ask.

    One number to agree on - the STORM at a place, 0..1 - is the date's share (the front moving) times the place's
    nearness to the front: felt from WEATHER_AHEAD south of it, full within WEATHER_BAND of it, gone WEATHER_BEHIND
    into the snow. The snow falling, the wind's strength, the score's suspense and the light all follow it.

    TWO CLOCKS. Where the storm is comes from the DATE, so it holds in a pause and hurries at 4x as the front does.
    The gusts rolling through, the flutter and the flakes run on the WEATHER CLOCK: real seconds while the game runs,
    standing still in a pause as the water does (the user's, 2026-10-18), and keeping their pace at any speed - a gust
    at 4x is still a gust, not a flash.
*/

//The storm about the front, by d: how far a place is south of the front's line (+, the snow still to come) or
//north of it (-, under it). Current tuning.
#define WEATHER_AHEAD           150.0f  //felt from this far ahead...
#define WEATHER_BAND            25.0f   //...full within this either side of the line - the front in view...
#define WEATHER_BEHIND          100.0f  //...and gone this far into the snow

/*
    The wind, units a second (current tuning): from the north-west, the sun's quarter, south with the front. Not from
    due north: the game's camera looks north, and a tree leaning straight at it hardly seems to move.
*/
#define WEATHER_WIND_X          0.8f
#define WEATHER_WIND_Z          1.0f
#define WEATHER_BREEZE_WIND     1.5f    //autumn's
#define WEATHER_STORM_WIND      3.0f    //the storm at full
#define WEATHER_FRONT_WIND      2.0f    //...and more on the front's line itself, so the line shows in the trees
#define WEATHER_FRONT_WIDTH     14.0f   //that band's half width

/*
    GUSTS: apps/archer's (Wind.cpp, GustFactor) laid on the ground - fronts rolling downwind, a sharp rise and a long
    tail - as an endless train fixed to the world, every WEATHER_GUST_SPACING along the wind, each its own strength
    from a hash of its number and some left out, so they come irregularly. Units a second on top of the wind.
*/
#define WEATHER_GUST_TRAVEL     12.0f   //how fast they roll
#define WEATHER_GUST_WIDTH      26.0f   //along the wind, rise and tail
#define WEATHER_GUST_SPACING    96.0f   //one every 8 s at a place, before the gaps
#define WEATHER_GUST_GAPS       0.3f    //the share left out
#define WEATHER_GUST_STORM      3.0f    //a gust's peak in the storm at full
#define WEATHER_GUST_BREEZE     1.2f    //...in autumn's breeze
//THE BIG GUST: one, when the front reaches the view - the score's `gust` stinger goes with it.
#define WEATHER_BIG_GUST        6.0f
#define WEATHER_BIG_WIDTH       44.0f
#define WEATHER_BIG_TRAVEL      16.0f

struct WeatherDate{
    float active = 0.0f;    //0..1: the storm's share by the date - the front on the move
    float breeze = 0.0f;    //0..1: autumn's
};
WeatherDate WeatherDateOf(uint64_t tick);

//0..1 by d (above): the storm's shape about the front.
float WeatherNear(float d);
//The front's line at x, from its mean z (CalendarSnowFrontZ): north of it lies snow.
float WeatherFrontLine(float front_z, float x);
float WeatherStorm(const WeatherDate& date, float front_z, const vec2& p);
//Unit, the way the wind blows.
vec2 WeatherWindDir();

//The big gust, once started: its front's place along the wind when it started, and the weather clock then.
struct WeatherBigGust{
    bool f_on = false;
    double start = 0.0;
    float from = 0.0f;
};
//Units a second on top of the wind at p, at the weather clock's `seconds`.
float WeatherGusts(const vec2& p, double seconds);
float WeatherBig(const WeatherBigGust& big, const vec2& p, double seconds);
//The whole wind at p: `strength` scales it all (the panel's and the tool's, 1 as tuned).
vec2 WeatherWind(const WeatherDate& date, float front_z, const WeatherBigGust& big, const vec2& p, double seconds,
                 float strength);

#endif
