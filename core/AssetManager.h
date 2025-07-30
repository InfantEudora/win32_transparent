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

// Class for storing data like meshes, materials etc. in prebuild configurations. A small subset from Object
class Asset{
    public:
    std::string name;
    Mesh* mesh = NULL;
    std::array<std::string,NUM_MATERIAL_SLOTS>material_names;
};

class AssetManager{
public:
    std::vector<Asset*>assets;
    std::vector<Material>loaded_materials;

    Asset* AddNewAsset(const char* asset_name, Object* object);
    Asset* AddNewAsset(const char* asset_name, const char* file_name);

    void ListAssets();

    Asset* GetAsset(const char* asset_name);
    Object* GetObjectFromAsset(const char* asset_name, Object* optional_target=NULL);
    Asset* FindAssetInGroup(std::string& asset_name, std::string& group_name);
};

#endif