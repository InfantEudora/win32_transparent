#include <stdio.h>
#include "AssetManager.h"

#include "Debug.h"
static Debugger *debug = new Debugger("AssetManager", DEBUG_ALL);

//We copy some data from the object and store that as an asset.
void AssetManager::AddNewAsset(const char* asset_name, Object* object){
    Asset* asset = new Asset();
    asset->name = asset_name;
    if (object){
        asset->mesh = object->GetMesh();
        asset->mesh->num_references++;
        debug->Info("Added new Asset: %s\n",asset_name);
    }
    assets.push_back(asset);
}

Asset* AssetManager::GetAsset(const char* asset_name){
    for (Asset* asset:assets){
        if (asset->name.compare(asset_name) == 0){
            return asset;
        }
    }
    debug->Err("Unable to find asset %s\n",asset_name);
    return NULL;
}

//Builds a new object based on the provided asset name, if it's found.
Object* AssetManager::GetObjectFromAsset(const char* asset_name, Object* optional_target){
    Asset* asset = GetAsset(asset_name);
    if (asset){
        //Build a new object from asset, or use a provided existing object.
        Object* object = optional_target;
        if (!optional_target){
            object = new Object();
        }
        object->SetMesh(asset->mesh);
        debug->Info("Got existing Asset: %s\n",asset_name);
        return object;
    }
    return NULL;
}
