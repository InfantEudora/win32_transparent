#include "IsoCell.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoCell", DEBUG_INFO);

std::map<int,int> IsoCell::terrain_material_map;

bool DirectionIsValid(int direction){
    if ((direction > DIRECTION_NONE) && (direction <= DIRECTION_WEST)){
        return true;
    }
    return false;
}

IsoCell::IsoCell():Object(){
    //SetScale(vec3(0.5,0.5,0.5));
}

IsoCell::~IsoCell(){

}

void IsoCell::ApplyPreset(int preset){
    if (preset == 0){

    }
}

void IsoCell::SetTerrainType(int newtype){

    material_slot[0] = terrain_material_map[newtype];
    terrain_type = newtype;
}

//Place a wall at one of the 4 coordinates.
Object* IsoCell::PlaceWall(int direction){
    if (!DirectionIsValid(direction)){
        return NULL;
    }
    if (wall_indices[direction] == -1){
        //Create a new one.
        Object* wall = assetmanager->GetObjectFromAsset("wall_full.001");
        if (wall && AttachChild(wall)){
            //The last one if the index.
            int child_index = children.size() - 1;
            wall_indices[direction] = child_index;
            debug->Ok("Placed a new wall\n");
            wall->name = "Wall @ " + std::to_string(coordinate.x) + "," + std::to_string(coordinate.z);


        }else{
            debug->Ok("Unable to place new wall\n");
        }
        return wall;
    }else{
        debug->Warn("Already a prop at that location\n");
        return GetChild(wall_indices[direction]);
    }
    return NULL;
}