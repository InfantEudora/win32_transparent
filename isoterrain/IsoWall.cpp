#include "IsoWall.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoWall", DEBUG_INFO);


IsoWall::IsoWall():Object(){

}

IsoWall::~IsoWall(){

}

void IsoWall::ChangeToDoor(AssetManager* assetmanager, const std::string& asset_name){
    if (assetmanager){
        if (assetmanager->GetObjectFromAsset(asset_name.c_str(),this)){
            debug->Info("Changed to door...\n");
        }
    }
}