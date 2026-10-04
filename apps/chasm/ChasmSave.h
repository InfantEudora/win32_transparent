#ifndef _CHASM_SAVE_H_
#define _CHASM_SAVE_H_

#include <stdint.h>
#include <string>
#include <utility>
#include <vector>
#include "Grid.h"
#include "tinygltf/json.hpp"

/*
    A SAVE: the seed plus edits (README.md, "The world is a seed plus edits"). The grid's settings -
    the seed, the size, the feature lines - from which the whole world is generated again, and what
    the player painted on it. Nothing generated is stored. The world's hash is, so a save that
    generates into a different map (the generator changed since) is noticed rather than painted
    onto the wrong plots.

    JSON, one object - the same thing is a save file in saves/ and the `state` line of an input
    recording (Application::CaptureRecordingState), which is what makes a replay start from a save.
*/

#define CHASM_SAVE_VERSION  1

struct ChasmSave{
    GridSettings settings;
    std::string world_hash;                             //Grid::Hash() as hex, when saved
    std::vector<std::pair<int,int>> houses;             //plot, storeys
    std::vector<int> fields;                            //coarse cells
    std::vector<std::pair<int,int>> grounds;            //plot, ZONE_GROUND_* (step 8; absent in older saves)
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
