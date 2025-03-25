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

Object* IsoWall::PlaceStairs(int direction){
    //We first need to assume wall is the normal height, not half.
    //Get the cell 2 adjacent to this in the supplied direction.
    //That cell cannot have a wall or a prop.

    if (!cell){
        return NULL;
    }

    //Wall can be shared, so we need to determine what cell we are on with the cursor
    //HERE TODO

    IsoCell* neighbour = cell->GetNeighbour(direction);

    //TODO: This needs to be 2 cells over, not one
    return cell->PlaceStairs((direction+2) % 4);
}