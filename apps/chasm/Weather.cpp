#include "Weather.h"
#include "Calendar.h"
#include <algorithm>
#include <cmath>

/*
    The date's share, in days of the season (current tuning). The front moves from autumn's day 8 to winter's day 4
    (CalendarSnowCover); the storm comes up just before it starts and dies away over the day after it stops, so a
    colony a little short of the snow's reach gets its weather and then its calm. The breeze comes up over autumn's
    first days and is gone a day into winter.
*/
#define WEATHER_STORM_FROM      7.5f    //autumn, from...
#define WEATHER_STORM_UP        8.5f    //...to full
#define WEATHER_STORM_DOWN      4.0f    //winter, from...
#define WEATHER_STORM_GONE      5.5f    //...to none
#define WEATHER_BREEZE_UP       2.0f    //autumn
#define WEATHER_BREEZE_GONE     1.0f    //winter

namespace {

//0 at a, 1 at b, eased at both ends; a may be past b.
float Ease(float a, float b, float x){
    float t = std::max(0.0f,std::min(1.0f,(x - a) / (b - a)));
    return t * t * (3.0f - 2.0f * t);
}

float Hash01(int64_t k, uint32_t salt){
    uint64_t x = (uint64_t)k * 0x9E3779B97F4A7C15ull + (uint64_t)salt * 0xBF58476D1CE4E5B9ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (float)(x >> 40) / (float)(1 << 24);
}

//A gust `behind` its own front, `width` long: archer's sharp rise over the first quarter and long tail.
float GustShape(float behind, float width){
    if (behind <= 0.0f || behind >= width){
        return 0.0f;
    }
    return Ease(0.0f,0.25f * width,behind) * (1.0f - Ease(0.25f * width,width,behind));
}

float Along(const vec2& p){
    vec2 dir = WeatherWindDir();
    return dir.x * p.x + dir.y * p.y;
}

}

WeatherDate WeatherDateOf(uint64_t tick){
    CalendarDate d = CalendarDateOf(tick);
    float days = d.season_fraction * (float)CALENDAR_DAYS_PER_SEASON;
    WeatherDate w;
    if (d.season == SEASON_AUTUMN){
        w.active = Ease(WEATHER_STORM_FROM,WEATHER_STORM_UP,days);
        w.breeze = Ease(0.0f,WEATHER_BREEZE_UP,days);
    }else if (d.season == SEASON_WINTER){
        w.active = 1.0f - Ease(WEATHER_STORM_DOWN,WEATHER_STORM_GONE,days);
        w.breeze = 1.0f - Ease(0.0f,WEATHER_BREEZE_GONE,days);
    }
    return w;
}

float WeatherNear(float d){
    if (d >= WEATHER_AHEAD || d <= -WEATHER_BEHIND){
        return 0.0f;
    }
    if (d > WEATHER_BAND){
        return Ease(WEATHER_AHEAD,WEATHER_BAND,d);
    }
    if (d < -WEATHER_BAND){
        return Ease(-WEATHER_BEHIND,-WEATHER_BAND,d);
    }
    return 1.0f;
}

float WeatherFrontLine(float front_z, float x){
    return front_z - CalendarSnowWobble(x);
}

float WeatherStorm(const WeatherDate& date, float front_z, const vec2& p){
    return date.active * WeatherNear(p.y - WeatherFrontLine(front_z,p.x));
}

vec2 WeatherWindDir(){
    float l = std::sqrt(WEATHER_WIND_X * WEATHER_WIND_X + WEATHER_WIND_Z * WEATHER_WIND_Z);
    return vec2(WEATHER_WIND_X / l,WEATHER_WIND_Z / l);
}

float WeatherGusts(const vec2& p, double seconds){
    //Which gust of the train is over p, and how far behind its front p is.
    double u = ((double)WEATHER_GUST_TRAVEL * seconds - (double)Along(p)) / (double)WEATHER_GUST_SPACING;
    double k = std::floor(u);
    float behind = (float)((u - k) * (double)WEATHER_GUST_SPACING);
    int64_t n = (int64_t)k;
    if (Hash01(n,1) < WEATHER_GUST_GAPS){
        return 0.0f;
    }
    return (0.4f + 0.6f * Hash01(n,2)) * GustShape(behind,WEATHER_GUST_WIDTH);
}

float WeatherBig(const WeatherBigGust& big, const vec2& p, double seconds){
    if (!big.f_on || seconds < big.start){
        return 0.0f;
    }
    float front = big.from + (float)((seconds - big.start) * (double)WEATHER_BIG_TRAVEL);
    return GustShape(front - Along(p),WEATHER_BIG_WIDTH);
}

vec2 WeatherWind(const WeatherDate& date, float front_z, const WeatherBigGust& big, const vec2& p, double seconds,
                 float strength){
    float d = p.y - WeatherFrontLine(front_z,p.x);
    float storm = date.active * WeatherNear(d);
    float q = d / WEATHER_FRONT_WIDTH;
    float line = (q * q < 1.0f) ? date.active * (1.0f - q * q) * (1.0f - q * q) : 0.0f;
    float base = WEATHER_BREEZE_WIND * date.breeze + WEATHER_STORM_WIND * storm + WEATHER_FRONT_WIND * line;
    float gusts = WeatherGusts(p,seconds) * (WEATHER_GUST_STORM * storm + WEATHER_GUST_BREEZE * date.breeze);
    float bigs = WEATHER_BIG_GUST * WeatherBig(big,p,seconds);
    if (base + gusts + bigs <= 0.0f){
        return vec2(0.0f,0.0f);
    }
    /*
        A little turbulence: slow waves drifting downwind in the speed, and a slower swing in the direction, so a
        steady wind is not a forest bowing in step. Doubles for the clock, which runs for hours.
    */
    vec2 dir = WeatherWindDir();
    vec2 across(-dir.y,dir.x);
    double along = (double)Along(p);
    double side = (double)(across.x * p.x + across.y * p.y);
    float waves = (float)(0.15 * std::sin(0.045 * along - 0.55 * seconds + 0.3 * std::sin(0.02 * side))
                        + 0.10 * std::sin(0.08 * side + 0.35 * seconds));
    float swing = (float)(0.12 * std::sin(0.03 * along + 0.05 * side - 0.4 * seconds));
    float speed = strength * (base * (1.0f + waves) + gusts + bigs);
    return vec2((dir.x + across.x * swing) * speed,(dir.y + across.y * swing) * speed);
}
