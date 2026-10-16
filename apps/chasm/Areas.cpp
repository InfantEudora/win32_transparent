#include "Areas.h"
#include "Exploration.h"
#include "Forest.h"
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {

//The land's kind of a plot, or -1 for none: the chasm's floor, its balconies and islands are not areas yet.
int LandKind(const Terrain& t, const ForestData& forest, int v){
    int level = t.level[v];
    if (level == TERRAIN_FLOOR || level == TERRAIN_BALCONY || level == TERRAIN_ISLAND){
        return -1;
    }
    switch (t.biome[v]){
        case TERRAIN_BIOME_MOUNTAIN:    return AREA_MOUNTAIN;
        case TERRAIN_BIOME_POCKET:      return AREA_MEADOW;
        case TERRAIN_BIOME_SWAMP:       return AREA_SWAMP;
        case TERRAIN_BIOME_DESERT:      return AREA_DESERT;
        default:
            break;
    }
    return ((size_t)v < forest.core.size() && forest.core[v]) ? AREA_FOREST : AREA_PLATEAU;
}

} //namespace

void BuildAreas(const GridPicker& picker, const Terrain& t, const ForestData& forest, const ExplorationSight& sight,
                AreaData& out){
    auto t0 = std::chrono::steady_clock::now();
    out = AreaData();
    const int width = sight.width;
    const int height = sight.height;
    //As Exploration::CellAt has it: the sight's raster is the exploration's.
    auto cell_at = [&](const vec2& p) -> int {
        int x = (int)std::floor((p.x - sight.origin.x) / EXPLORATION_CELL);
        int z = (int)std::floor((p.y - sight.origin.y) / EXPLORATION_CELL);
        return (x < 0 || z < 0 || x >= width || z >= height) ? -1 : z * width + x;
    };
    auto centre = [&](int cell){
        return sight.origin + vec2((float)(cell % width) + 0.5f,(float)(cell / width) + 0.5f) * EXPLORATION_CELL;
    };
    //Every cell within `r` of p - its middle, or p itself in it - into `cells`.
    auto disc = [&](const vec2& p, float r, std::vector<int>& cells){
        int x0 = (int)std::floor((p.x - r - sight.origin.x) / EXPLORATION_CELL);
        int x1 = (int)std::floor((p.x + r - sight.origin.x) / EXPLORATION_CELL);
        int z0 = (int)std::floor((p.y - r - sight.origin.y) / EXPLORATION_CELL);
        int z1 = (int)std::floor((p.y + r - sight.origin.y) / EXPLORATION_CELL);
        for (int z = std::max(0,z0); z <= std::min(height - 1,z1); z++){
            for (int x = std::max(0,x0); x <= std::min(width - 1,x1); x++){
                int c = z * width + x;
                if ((centre(c) - p).length() <= r){
                    cells.push_back(c);
                }
            }
        }
        int own = cell_at(p);
        if (own >= 0){
            cells.push_back(own);
        }
    };

    //RIVERS: along the water, a step at a time, as wide as it is there - stopping at a fall's lip, past which its line
    //runs on into the chasm's air.
    for (size_t r = 0; r < t.rivers.size(); r++){
        const TerrainRiver& river = t.rivers[r];
        float cut = 1e30f;
        for (const TerrainFall& f : t.falls){
            cut = (f.river == (int)r) ? std::min(cut,f.along) : cut;
        }
        Area a;
        a.kind = AREA_RIVER;
        a.which = (int)r;
        float along = 0.0f;
        for (size_t i = 0; i + 1 < river.points.size() && along < cut; i++){
            vec2 ab = river.points[i + 1] - river.points[i];
            float len = ab.length();
            int steps = std::max(1,(int)std::ceil(len / (EXPLORATION_CELL * 0.5f)));
            for (int s = 0; s <= steps && along + len * s / steps <= cut; s++){
                float f = (float)s / steps;
                float half = (river.half.size() > i + 1) ? river.HalfAt(i,f) : river.width * 0.5f;
                disc(river.points[i] + ab * f,half,a.cells);
            }
            along += len;
        }
        out.areas.push_back(a);
    }
    //WATERFALLS: round the lip.
    for (size_t i = 0; i < t.falls.size(); i++){
        const TerrainFall& f = t.falls[i];
        Area a;
        a.kind = AREA_WATERFALL;
        a.which = (int)i;
        disc(f.lip,std::max(AREA_FALL_REACH,f.width * 0.5f),a.cells);
        out.areas.push_back(a);
    }
    /*
        THE LAND: every cell by the plot under its middle, into that plot's kind's one area. By cell rather than by
        plot: plots stand further apart than a cell is wide, and an area made of their cells is full of holes.
        Past the grid's ragged edge there is no plot, and no area.
    */
    std::vector<Area> land(AREA_KINDS);
    for (int c = 0; c < width * height; c++){
        GridPick pick = picker.Pick(centre(c));
        int kind = pick.f_hit ? LandKind(t,forest,pick.plot) : -1;
        if (kind >= 0){
            land[kind].cells.push_back(c);
        }
    }
    for (int k = AREA_PLATEAU; k < AREA_KINDS; k++){
        land[k].kind = k;
        out.areas.push_back(land[k]);
    }

    //Each one's cells once, in order; an empty one - a biome this map has none of - dropped; its middle found.
    std::vector<Area> kept;
    for (Area& a : out.areas){
        std::sort(a.cells.begin(),a.cells.end());
        a.cells.erase(std::unique(a.cells.begin(),a.cells.end()),a.cells.end());
        if (a.cells.empty()){
            continue;
        }
        vec2 sum;
        for (int c : a.cells){
            sum = sum + centre(c);
        }
        vec2 mean = sum * (1.0f / (float)a.cells.size());
        float best = 1e30f;
        for (int c : a.cells){
            float d = (centre(c) - mean).length();
            if (d < best){
                best = d;
                a.middle = centre(c);
            }
        }
        kept.push_back(std::move(a));
    }
    out.areas.swap(kept);
    out.build_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

const char* AreaKindName(int kind){
    switch (kind){
        case AREA_RIVER:        return "river";
        case AREA_WATERFALL:    return "waterfall";
        case AREA_PLATEAU:      return "plateau";
        case AREA_FOREST:       return "forest";
        case AREA_MOUNTAIN:     return "mountain";
        case AREA_MEADOW:       return "meadow";
        case AREA_SWAMP:        return "swamp";
        case AREA_DESERT:       return "desert";
        default:                return "area";
    }
}

const char* AreaKindTold(int kind){
    switch (kind){
        case AREA_RIVER:        return "a river";
        case AREA_WATERFALL:    return "a waterfall";
        case AREA_PLATEAU:      return "the plateau";
        case AREA_FOREST:       return "the forest";
        case AREA_MOUNTAIN:     return "the mountain";
        case AREA_MEADOW:       return "a mountain meadow";
        case AREA_SWAMP:        return "the swamp";
        case AREA_DESERT:       return "the desert";
        default:                return "somewhere new";
    }
}

uint32_t AreaKindColour(int kind){
    switch (kind){
        case AREA_RIVER:        return 0xFF40A0FFu;
        case AREA_WATERFALL:    return 0xFFA0F0FFu;
        case AREA_PLATEAU:      return 0xFFC8D070u;
        case AREA_FOREST:       return 0xFF30A040u;
        case AREA_MOUNTAIN:     return 0xFFE8E8F0u;
        case AREA_MEADOW:       return 0xFF90E070u;
        case AREA_SWAMP:        return 0xFF708050u;
        case AREA_DESERT:       return 0xFFE8B860u;
        default:                return 0xFFFF00FFu;
    }
}
