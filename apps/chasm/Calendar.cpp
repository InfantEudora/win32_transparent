#include "Calendar.h"
#include <algorithm>
#include <cmath>

CalendarDate CalendarDateOf(uint64_t tick){
    CalendarDate d;
    uint64_t in_year = tick % CALENDAR_YEAR_TICKS;
    uint64_t in_season = in_year % CALENDAR_SEASON_TICKS;
    uint64_t in_day = in_season % CALENDAR_DAY_TICKS;
    d.year = (int)(tick / CALENDAR_YEAR_TICKS) + 1;
    d.season = (int)(in_year / CALENDAR_SEASON_TICKS);
    d.day = (int)(in_season / CALENDAR_DAY_TICKS) + 1;
    d.day_fraction = (float)in_day / CALENDAR_DAY_TICKS;
    d.season_fraction = (float)in_season / CALENDAR_SEASON_TICKS;
    d.year_fraction = (float)in_year / CALENDAR_YEAR_TICKS;
    return d;
}

const char* CalendarSeasonName(int season){
    switch (season){
        case SEASON_SPRING: return "spring";
        case SEASON_SUMMER: return "summer";
        case SEASON_AUTUMN: return "autumn";
        case SEASON_WINTER: return "winter";
        default:            return "?";
    }
}

namespace {

//0 at a, 1 at b, eased at both ends - snow that starts and stops creeping rather than lurching.
float Ease(float a, float b, float x){
    float t = std::max(0.0f,std::min(1.0f,(x - a) / (b - a)));
    return t * t * (3.0f - 2.0f * t);
}

}

/*
    In days of the season (0..10, fractional): autumn's last two bring the first quarter of the way,
    winter's first four the rest; spring's first five take it all back. Computed from the tick's
    integer parts, so the same tick gives the same bits in any build.
*/
float CalendarSnowCover(uint64_t tick){
    CalendarDate d = CalendarDateOf(tick);
    float days = d.season_fraction * CALENDAR_DAYS_PER_SEASON;
    switch (d.season){
        case SEASON_AUTUMN:
            return 0.25f * Ease(8.0f,10.0f,days);
        case SEASON_WINTER:
            return 0.25f + 0.75f * Ease(0.0f,4.0f,days);
        case SEASON_SPRING:
            if (d.year == 1){
                return 0.0f;    //no winter came before the colony
            }
            return 1.0f - Ease(0.0f,5.0f,days);
        default:
            return 0.0f;
    }
}

float CalendarSnowFrontZ(float cover, float z_north, float z_south){
    float start = z_north - 2.0f * CALENDAR_SNOW_WOBBLE;
    float end = z_north + CALENDAR_SNOW_REACH * (z_south - z_north);
    return start + cover * (end - start);
}

//Two waves, neither a multiple of the other: about three long bends and a shorter ripple across
//the map's 768. The shader carries the same two terms.
float CalendarSnowWobble(float x){
    return CALENDAR_SNOW_WOBBLE * (0.7f * std::sin(x * 0.0245f + 1.3f) + 0.3f * std::sin(x * 0.0610f + 0.4f));
}

bool CalendarSnowAt(const vec2& p, float front_z){
    return p.y < front_z - CalendarSnowWobble(p.x);
}
