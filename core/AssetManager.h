#ifndef _ASSET_MANAGER_H_
#define _ASSET_MANAGER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <memory>
#include "Object.h"

/*
    Assets are simply obects that are re-used/loaded multiple times.
    They can also be some file loaded from disk or memory.

    If you create an asset, and load it back from asset manager, it's job is:
     - Make sure meshes are instanced.
     - All materials are unique and loaded properly.
     - Stuff is grouped somehow? I.e., you can create a group "Rocks" and load a random asset from it.
     - Later on, assets can be desribed in a JSON file.

*/

class Asset;
class AssetManager;

class Asset{
    public:
    std::string name;
    Mesh* mesh = NULL;
};

class AssetManager{
public:
    std::vector<Asset*>assets;

    void AddNewAsset(const char* asset_name, Object* object);

    Asset* GetAsset(const char* asset_name);
    Object* GetObjectFromAsset(const char* asset_name, Object* optional_target=NULL);
};

#endif