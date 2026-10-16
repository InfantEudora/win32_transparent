#ifndef _CHASM_WORLD_H_
#define _CHASM_WORLD_H_

#include <memory>
#include <vector>
#include "Grid.h"
#include "GridPick.h"
#include "Terrain.h"
#include "TerrainMesh.h"
#include "Forest.h"
#include "Deposits.h"
#include "Mist.h"
#include "WaterMesh.h"
#include "Exploration.h"
#include "Areas.h"

/*
    Everything GENERATED for one map, as one immutable bundle: built together from the seed, swapped
    together on regeneration, so nothing can pair one grid with another grid's terrain. What the
    player does to the map lives elsewhere (Zones.h) and names its world.
*/
struct ChasmWorld{
    std::shared_ptr<const Grid> grid;
    std::shared_ptr<const GridPicker> picker;
    std::shared_ptr<const Terrain> terrain;
    std::vector<GridLine> features;     //world coordinates, in the layout's order (Grid::LineKind)
    std::shared_ptr<const TerrainMeshData> mesh;
    std::shared_ptr<const DepositData> deposits;    //seams, the mountain, outcrops (stone_plan.md step 4)
    std::shared_ptr<const ForestData> forest;
    std::shared_ptr<const MistData> mist;
    std::shared_ptr<const WaterMeshData> water;
    std::shared_ptr<const ExplorationSight> sight;  //the ground the people's lines of sight run over
    std::shared_ptr<const AreaData> areas;          //what there is to discover (areas_plan.md), on the sight's raster
};

#endif
