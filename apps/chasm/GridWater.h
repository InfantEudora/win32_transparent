#ifndef _CHASM_GRID_WATER_H_
#define _CHASM_GRID_WATER_H_

#include <vector>
#include "type_vec2.h"

/*
    WATER PLACED BY HAND (docs/water_tools_plan.md): rivers and ponds drawn onto a map as part of its SETTINGS, beside
    its kind - so a load and every replay make them again with the rest of the world, which is never saved.
    Grid::Generate puts them into the layout after its own (ChasmPlaceWater), and from there a river is a river like
    any other - its channel, banks and wet plots, its water and streaks, bridges, collectors and the ice - and a pond
    is the swamp's pools: ground pressed under the water (Terrain::BuildRelief), wet and drawn as still water.

    Written to a save only when there is any, so a save without is exactly what it was.
*/
#define GRID_PLACED_RIVER_WIDTH     4.0f    //unless given: between a branch's (2.2-3.4) and a river's (5-7.5)
#define GRID_PLACED_RIVER_NARROWEST 1.5f
#define GRID_PLACED_RIVER_WIDEST    12.0f
#define GRID_PLACED_POND_RADIUS     6.0f    //unless given: to its water's edge, a few plots across
#define GRID_PLACED_POND_SMALLEST   2.0f
#define GRID_PLACED_POND_LARGEST    30.0f
#define GRID_POND_BANK              3.0f    //past a pond's water, the ground rises back to the plateau over this

struct GridPlacedRiver{
    std::vector<vec2> points;           //world x and z (vec2::y is z), source first: a few make a course
    float width = GRID_PLACED_RIVER_WIDTH;
    bool operator==(const GridPlacedRiver& o) const{
        if (width != o.width || points.size() != o.points.size()){
            return false;
        }
        for (size_t i = 0; i < points.size(); i++){
            if (points[i].x != o.points[i].x || points[i].y != o.points[i].y){
                return false;
            }
        }
        return true;
    }
};

struct GridPlacedPond{
    vec2 centre;                        //world x and z
    float radius = GRID_PLACED_POND_RADIUS;     //to its water's edge
    bool operator==(const GridPlacedPond& o) const{
        return centre.x == o.centre.x && centre.y == o.centre.y && radius == o.radius;
    }
};

struct GridWater{
    std::vector<GridPlacedRiver> rivers;
    std::vector<GridPlacedPond> ponds;
    bool Empty() const { return rivers.empty() && ponds.empty(); }
    bool operator==(const GridWater& o) const { return rivers == o.rivers && ponds == o.ponds; }
};

/*
    HEIGHT PAINTED BY HAND (water_tools_plan.md step 5): strokes that raise or lower the relief, part of the settings
    as the water is. A stroke of one point is a hill (a hollow, lowered); of several, a ridge (a valley) along them.
    Its height is at its line, falling off smoothly to nothing at its radius; strokes add up. Terrain::BuildRelief adds
    them before the rivers flatten the relief, on the plateau only, as all relief is.
*/
#define GRID_RELIEF_HEIGHT          6.0f    //unless given
#define GRID_RELIEF_HIGHEST         20.0f   //either way (the user's): the generated hills reach 14
#define GRID_RELIEF_RADIUS          12.0f   //unless given: about six plots out from its line
#define GRID_RELIEF_NARROWEST       4.0f    //two of the relief's 2-unit raster cells
#define GRID_RELIEF_WIDEST          60.0f

struct GridReliefStroke{
    std::vector<vec2> points;           //world x and z
    float radius = GRID_RELIEF_RADIUS;
    float height = GRID_RELIEF_HEIGHT;  //up, or down below 0
    bool operator==(const GridReliefStroke& o) const{
        if (radius != o.radius || height != o.height || points.size() != o.points.size()){
            return false;
        }
        for (size_t i = 0; i < points.size(); i++){
            if (points[i].x != o.points[i].x || points[i].y != o.points[i].y){
                return false;
            }
        }
        return true;
    }
};

struct GridRelief{
    std::vector<GridReliefStroke> strokes;
    bool Empty() const { return strokes.empty(); }
    bool operator==(const GridRelief& o) const { return strokes == o.strokes; }
};

/*
    CLIFFS PAINTED BY HAND (water_tools_plan.md step 6): plots set to a depth below the plateau, in whole steps, part of
    the settings as the relief is. A stroke is the chain of plots a drag crossed, joined along fine edges as a road's
    is, kept as their positions so it outlives a new seed as the water does - and its width, in plots across; its depth
    0 takes earlier strokes back to the plateau. Terrain::BuildHeights sets them, a later stroke over an earlier one,
    on the plateau's dry plots only (TerrainCliffPlots, Terrain::cliff_open). Down only (the user, 2026-10-19).
*/
#define GRID_CLIFF_DEPTH            1       //steps, unless given: one stratum, TERRAIN_STEP
#define GRID_CLIFF_DEEPEST          4       //-24, well clear of the cloud deck
#define GRID_CLIFF_WIDTH            1       //plots across, unless given
#define GRID_CLIFF_WIDEST           5

struct GridCliffStroke{
    std::vector<vec2> points;           //world x and z: the painted chain's plots
    int width = GRID_CLIFF_WIDTH;
    int depth = GRID_CLIFF_DEPTH;
    bool operator==(const GridCliffStroke& o) const{
        if (width != o.width || depth != o.depth || points.size() != o.points.size()){
            return false;
        }
        for (size_t i = 0; i < points.size(); i++){
            if (points[i].x != o.points[i].x || points[i].y != o.points[i].y){
                return false;
            }
        }
        return true;
    }
};

struct GridCliffs{
    std::vector<GridCliffStroke> strokes;
    bool Empty() const { return strokes.empty(); }
    bool operator==(const GridCliffs& o) const { return strokes == o.strokes; }
};

/*
    The placed water onto `layout`, after its own rivers - so a chasm map's keep their indices, and with them their
    width's wander and their lakes, which the terrain seeds by index. The map is lo..hi. How a river ENDS is read from
    where it was drawn: over a RIM (a chasm map's) it FALLS at the first it crosses; ending at the map's EDGE it runs
    off it; ending in another river's water it JOINS that river; anywhere else it SINKS into a swallow hole. A pond
    that does not fit on the plateau (ChasmPondFits) is left out, and counted (ChasmLayout::ponds_dropped).
*/
struct ChasmLayout;
void ChasmPlaceWater(ChasmLayout& layout, const GridWater& water, const vec2& lo, const vec2& hi);
//Whether a pond's water and bank stay on the plateau: inside the map, off every rim and crack, south of the mountain.
bool ChasmPondFits(const ChasmLayout& layout, const vec2& centre, float radius, const vec2& lo, const vec2& hi);
//The painted strokes onto `layout`, each held to the limits above; a stroke with no point is left out.
void ChasmPlaceRelief(ChasmLayout& layout, const GridRelief& relief);
//...and the painted cliffs, the same way.
void ChasmPlaceCliffs(ChasmLayout& layout, const GridCliffs& cliffs);

#endif
