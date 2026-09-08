#ifndef _ASSET_MANAGER_H_
#define _ASSET_MANAGER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
#include "Object.h"
#include "OBJLoader.h"

/*
    Assets are simply obects that are re-used/loaded multiple times.
    They can also be some file loaded from disk or memory.

    If you create an asset, and load it back from asset manager, it's job is to make sure:
     - Loaded Meshes are instanced.
     - All materials are unique and loaded properly. (TODO)
     - Stuff is grouped somehow? I.e., you can create a group "Rocks" and load a random asset from it. (TODO)
     - Later on, assets can be desribed in a JSON file, or they can be auto loaded from a folder.
*/

class Asset;
class AssetManager;

//A stable, compact handle on an asset - what a SimCommand carries instead of a name, so a
//command stays a fixed-size POD that can be written to a replay log (see core/SimCommand.h).
typedef uint32_t assetid_t;
#define ASSETID_INVALID 0

//FNV-1a over the asset's name: the id is DERIVED from the name, never assigned. That is the whole
//point of it. An index into `assets`, or a counter handed out at load time, would be stable only
//until someone inserts or reorders an AddNewAsset call in Init() - after which an old recording
//would silently bind to the WRONG asset, and the SimCommand version field cannot catch that
//because the struct did not change. A name hash is effectively "the name in 4 bytes": independent
//of load order and of how many assets exist, and if an asset is renamed or removed the id simply
//isn't found, so the command fails loudly instead of quietly hitting something else.
static inline assetid_t AssetIDFromName(const char* name){
    uint32_t h = 2166136261u;
    for (const char* p = name; p && *p; p++){
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h ? h : 1u; //never collide with ASSETID_INVALID
}

// Class for storing data like meshes, materials etc. in prebuild configurations. A small subset from Object
class Asset{
    public:
    std::string name;
    //Kept in step with `name` by SetName below - always AssetIDFromName(name).
    assetid_t id = ASSETID_INVALID;
    Mesh* mesh = NULL;
    std::array<std::string,NUM_MATERIAL_SLOTS>material_names;

    //The only way the name is set, so the id can never drift out of step with it. There is more
    //than one place that constructs an Asset (AddNewAsset, AddNewAssetFromOBJFile), which is
    //exactly why this is a function rather than two assignments at the call sites.
    void SetName(const char* asset_name){
        name = asset_name ? asset_name : "";
        id = AssetIDFromName(name.c_str());
    }
};

class AssetManager{
public:
    std::vector<Asset*>assets;
    std::vector<Material>loaded_materials; //Only for OBJ?

    Asset* AddNewAsset(const char* asset_name, Object* object);
    Asset* AddNewAssetFromOBJFile(const char* asset_name, const char* file_name);

    void ListAssets();

    Asset* GetAsset(const char* asset_name);
    //Same lookup by the asset's stable id (see AssetIDFromName) - what a SimCommand resolves
    //through. A linear scan like GetAsset: assets number in the tens and this runs when something
    //is spawned, not per tick, so a map would buy nothing but a second thing to keep in step.
    Asset* GetAssetByID(assetid_t asset_id);
    Object* GetObjectFromAsset(const char* asset_name, Object* optional_target=NULL);
    Object* GetObjectFromAssetID(assetid_t asset_id, Object* optional_target=NULL);
    Mesh* GetMeshFromAsset(const char* asset_name); //Should not be used to get meshes for objects.
    Asset* FindAssetInGroup(std::string& asset_name, std::string& group_name);
private:
    //Registers a freshly built asset: warns if its id collides with an existing one (a name hash
    //collision would otherwise show up much later as an id resolving to the wrong asset, which
    //would be genuinely baffling) and appends it. One scan at load time.
    void RegisterAsset(Asset* asset);
};

#endif