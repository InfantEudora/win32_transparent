#include "IsoCell.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoCell", DEBUG_INFO);


IsoCell::IsoCell():Object(){
    SetScale(vec3(0.5,0.5,0.5));
}

IsoCell::~IsoCell(){

}

void IsoCell::ApplyPreset(int preset){
    if (preset == 0){

    }
}

void IsoCell::SetTerrainType(int newtype){
    if (newtype == CELL_TERRAIN_NONE){
        material_slot[0] = -1;
    }else if (newtype == CELL_TERRAIN_GRASS){
        //TODO: Somehow get the proper material from renderer:
    }
}