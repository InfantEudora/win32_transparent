#include <stdio.h>
#include "AssetManager.h"

#include "Debug.h"
static Debugger *debug = new Debugger("AssetManager", DEBUG_INFO);

//We copy some data from the object and store that as an asset.
Asset* AssetManager::AddNewAsset(const char* asset_name, Object* object){
    Asset* asset = new Asset();
    asset->SetName(asset_name);
    if (object){
        asset->mesh = object->GetMesh();
        asset->mesh->num_references++;
        asset->material_names = object->material_names;
    }
    debug->Info("Added new Asset: %s\n",asset_name);
    RegisterAsset(asset);
    return asset;
}

Asset* AssetManager::AddNewAssetFromOBJFile(const char* asset_name, const char* file_name){
    Asset* asset = new Asset();
    asset->SetName(asset_name);
    Object* object = new Object();
    object->SetMesh(OBJLoader::ParseOBJFile(file_name,&loaded_materials));
    if (object){
        asset->mesh = object->GetMesh();
        asset->mesh->num_references++;
        delete object;
    }
    debug->Info("Added new Asset: %s\n",asset_name);
    RegisterAsset(asset);
    return asset;
}

void AssetManager::RegisterAsset(Asset* asset){
    for (Asset* existing:assets){
        if (existing->id == asset->id){
            debug->Err("Asset id collision: '%s' and '%s' both hash to %u - rename one\n",
                       existing->name.c_str(),asset->name.c_str(),asset->id);
            break;
        }
    }
    assets.push_back(asset);
}

Asset* AssetManager::GetAssetByID(assetid_t asset_id){
    if (asset_id == ASSETID_INVALID){
        return NULL;
    }
    for (Asset* asset:assets){
        if (asset->id == asset_id){
            return asset;
        }
    }
    debug->Err("Unable to find asset with id %u\n",asset_id);
    return NULL;
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

//Builds a new object from an already-resolved asset. Shared by both GetObjectFromAsset* below, so
//the by-name and the by-id path cannot drift apart in what they copy onto the object.
static Object* BuildObjectFromAsset(Asset* asset, Object* optional_target){
    if (!asset){
        return NULL;
    }
    //Build a new object from asset, or use a provided existing object.
    Object* object = optional_target;
    if (!optional_target){
        object = new Object();
    }
    object->SetMesh(asset->mesh);
    object->material_names = asset->material_names;
    return object;
}

//Same, addressed by the asset's stable id - what a SimCommand's spawn handler goes through.
Object* AssetManager::GetObjectFromAssetID(assetid_t asset_id, Object* optional_target){
    return BuildObjectFromAsset(GetAssetByID(asset_id),optional_target);
}

//Builds a new object based on the provided asset name, if it's found.
Object* AssetManager::GetObjectFromAsset(const char* asset_name, Object* optional_target){
    Asset* asset = GetAsset(asset_name);
    if (asset){
        Object* object = BuildObjectFromAsset(asset,optional_target);
        debug->Trace("Got existing Asset: %s\n",asset_name);
        return object;
    }
    return NULL;
}

void AssetManager::ListAssets(){
    debug->Info("List of Assets:\n");
    for (Asset* asset:assets){
        debug->Info(" - %s (id %u)\n",asset->name.c_str(),asset->id);
    }
}

Mesh* AssetManager::GetMeshFromAsset(const char* asset_name){
    Asset* asset = GetAsset(asset_name);
    if (asset){
        return asset->mesh;
    }
    return NULL;
}