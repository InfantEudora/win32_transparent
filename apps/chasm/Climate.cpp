#include "Climate.h"
#include "Calendar.h"
#include "Grid.h"
#include "Terrain.h"
#include <algorithm>
#include <cmath>

float ClimateYear(uint64_t tick){
    float year = CalendarDateOf(tick).year_fraction;
    float mid = 0.5f * (CLIMATE_SUMMER + CLIMATE_WINTER);
    float swing = 0.5f * (CLIMATE_SUMMER - CLIMATE_WINTER);
    return mid - swing * std::cos(6.2831853f * (year - CLIMATE_COLDEST));
}

float ClimateAir(uint64_t tick, float north, float storm, bool f_snow){
    north = std::max(0.0f,std::min(1.0f,north));
    float t = ClimateYear(tick) - CLIMATE_NORTH * (north - 0.5f) - CLIMATE_STORM * std::max(0.0f,std::min(1.0f,storm));
    return f_snow ? std::min(t,CLIMATE_UNDER_SNOW) : t;
}

int ClimateStepsDown(const Terrain& t, int plot){
    if (plot < 0 || plot >= (int)t.level.size()){
        return 0;
    }
    //A cliff painted by hand is ground the terrain calls a balcony too - it touches the plateau - but it is no terrace.
    if (plot < (int)t.cliff.size() && t.cliff[plot] > 0){
        return 0;
    }
    int level = t.level[plot];
    return (level == TERRAIN_BALCONY || level == TERRAIN_ISLAND) ? (int)t.steps[plot] : 0;
}

float ClimateChasm(const Grid& g, const Terrain& t, int plot, const vec2& p){
    //Eased off inland, so the readout does not tick down evenly a unit at a time but holds near the edge.
    float d = std::max(0.0f,ChasmRiftDistance(g.layout,p));
    float k = std::max(0.0f,1.0f - d / CLIMATE_EDGE_REACH);
    return CLIMATE_EDGE * k * k * (3.0f - 2.0f * k) + CLIMATE_PER_STEP * (float)ClimateStepsDown(t,plot);
}
