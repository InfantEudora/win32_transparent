#ifndef _CHASM_SAVE_H_
#define _CHASM_SAVE_H_

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "Grid.h"
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
    std::vector<ZoneSavedGround> grounds;               //plot, ZONE_GROUND_* and flags, line (step 8; absent in older saves)
    uint64_t calendar_tick = 0;                         //the date (Calendar.h); absent in older saves, which start in spring
    EconomySaved economy;                               //stocks, felled trees, workers, fields (Economy.h); absent in older saves
    std::string explored;                               //Exploration::ToString (play_mode_plan.md); absent in older saves, which load all explored
    std::vector<std::pair<int,int>> castle;             //the debug castle's pieces, plot and ZoneCastlePiece in the order painted - an arch over a piece after it (ramparts_plan.md); absent with none
};

nlohmann::json ChasmSaveToJson(const ChasmSave& s);
bool ChasmSaveFromJson(const nlohmann::json& j, ChasmSave& out, std::string& error);
//Water placed by hand (GridWater.h) as the save's "grid" block and chasm_generate hold it:
//{"rivers":[{"points":[[x,z],...],"width":w}],"ponds":[{"x":x,"z":z,"radius":r}]} - either list may be left out, a
//river's width and a pond's radius too. False, with `error`, if malformed.
nlohmann::json GridWaterToJson(const GridWater& w);
bool GridWaterFromJson(const nlohmann::json& j, GridWater& out, std::string& error);
//...and the height painted by hand: {"strokes":[{"points":[[x,z],...],"radius":r,"height":h}]}, the radius and height
//optional. False, with `error`, if malformed.
nlohmann::json GridReliefToJson(const GridRelief& r);
bool GridReliefFromJson(const nlohmann::json& j, GridRelief& out, std::string& error);
//...and the cliffs painted by hand: {"strokes":[{"points":[[x,z],...],"width":w,"depth":d}]}, the width (plots across)
//and depth (steps down) optional. False, with `error`, if malformed.
nlohmann::json GridCliffsToJson(const GridCliffs& c);
bool GridCliffsFromJson(const nlohmann::json& j, GridCliffs& out, std::string& error);

/*
    THE GAME'S FOLDERS - saves/, recordings/, reports/ - found from the exe, as main.cpp finds the assets, never from
    the working directory, so a tester's shortcut started somewhere else does not scatter them (demo_plan.md):
    apps/chasm/<name>/ for an exe in build/, and beside the exe itself for one with its assets baked in, which is
    alone in its folder.
*/
std::string ChasmFolder(const char* name);

//Files in saves/, by bare name ("village" -> saves/village.json).
std::string ChasmSaveDirectory();
std::string ChasmSavePath(const std::string& name);
bool ChasmSaveWrite(const std::string& name, const nlohmann::json& j, std::string& error);
bool ChasmSaveRead(const std::string& name, nlohmann::json& out, std::string& error);
std::vector<std::string> ChasmSaveList();
int64_t ChasmSaveWritten(const std::string& name);     //the file's time, seconds since 1970; -1 with no such save

/*
    WHAT THE LOAD SCREEN LISTS (demo_plan.md, "Save and load"): each save by what its JSON says - the title and kind
    its "meta" was given when it was written, the colony's date and people - newest first, by the file's time. A save
    without a meta (the tools', the panel's) goes by its file name.
*/
struct ChasmSaveEntry{
    std::string name;                   //the bare name ChasmSaveRead takes
    std::string title;                  //"Seed 12 - year 2, summer 4", or the file's name
    std::string kind;                   //"save", "auto", "quick", or empty with no meta
    std::string colony;
    std::string build;                  //the build that wrote it
    uint64_t calendar_tick = 0;
    int people = 0;
    int64_t written = 0;                //seconds since 1970
    bool f_test_map = false;            //of a test map (test_maps_plan.md): never what Continue opens
};
std::vector<ChasmSaveEntry> ChasmSaveEntries();

#endif
