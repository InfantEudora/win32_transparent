#ifndef _CHASM_SAVE_H_
#define _CHASM_SAVE_H_

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "Grid.h"
#include "Walkers.h"
#include "Zones.h"
#include "Economy.h"
#include "tinygltf/json.hpp"

/*
    A SAVE: the seed plus edits (README.md, "The world is a seed plus edits"). The grid's settings -
    the seed and the size - from which the whole world, chasm and rivers included, is generated again
    - and what the player painted on it. Nothing generated is stored (the chasm's lines were, until
    they came from the seed too; an old save's "features" key is ignored). The world's hash is, so a save that
    generates into a different map (the generator changed since) is noticed rather than painted
    onto the wrong plots.

    JSON, one object - the same thing is a save file in saves/ and the `state` line of an input
    recording (Application::CaptureRecordingState), which is what makes a replay start from a save.
*/

/*
    2: buildings as things (docs/buildings_plan.md) - a list of buildings, each with its id, kind, plots
    or cells and crop, instead of loose house plots and field cells. A version 1 save still loads: each
    of its house plots becomes a one-plot house, each field cell a one-cell field.
*/
#define CHASM_SAVE_VERSION  2

struct ChasmSave{
    GridSettings settings;
    std::string world_hash;                             //Grid::Hash() as hex, when saved
    std::vector<ZoneSavedBuilding> buildings;           //version 2
    uint32_t next_building = 1;                         //the next id the zones would hand out
    uint32_t stroke = 0;                                //the drag in progress, and its building
    uint32_t stroke_building = 0;
    std::vector<std::pair<int,int>> grounds;            //plot, ZONE_GROUND_* (step 8; absent in older saves)
    std::vector<Walker> walkers;                        //step 10, with their paths (Walkers.h); absent in older saves
    uint64_t calendar_tick = 0;                         //the date (Calendar.h); absent in older saves, which start in spring
    EconomySaved economy;                               //stocks, felled trees, workers, fields (Economy.h); absent in older saves
};

nlohmann::json ChasmSaveToJson(const ChasmSave& s);
bool ChasmSaveFromJson(const nlohmann::json& j, ChasmSave& out, std::string& error);

//Files in apps/chasm/saves/, by bare name ("village" -> saves/village.json). Relative to the working
//directory, which is the app's folder when started as CLAUDE.md describes - like recordings/.
std::string ChasmSavePath(const std::string& name);
bool ChasmSaveWrite(const std::string& name, const nlohmann::json& j, std::string& error);
bool ChasmSaveRead(const std::string& name, nlohmann::json& out, std::string& error);
std::vector<std::string> ChasmSaveList();

#endif
