#include "IsoWall.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoWall", DEBUG_INFO);


IsoWall::IsoWall():Object(){

}

IsoWall::~IsoWall(){

}

void IsoWall::Lower(){
    if (cell && cell->assetmanager){
        if (cell->assetmanager->GetObjectFromAsset("wall_half.001",this)){
            debug->Ok("Wall was lowered\n");
        }else{
            debug->Err("Unable to lower wall\n");
        }
    }
}

