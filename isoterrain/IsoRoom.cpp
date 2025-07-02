#include "IsoRoom.h"

#include "Debug.h"
static Debugger* debug = new Debugger("IsoRoom", DEBUG_INFO);


IsoRoom::IsoRoom(){

}

IsoRoom::~IsoRoom(){

}

int IsoRoom::GetSize(){
    return cells.size();
}

bool IsoRoom::IsCellInRoom(IsoCell* _cell){
    for (IsoCell* cell:cells){
        if (cell == _cell){
            return true;
        }
    }
    return false;
}

// Checks if the cell is not already in this room
void IsoRoom::AddCell(IsoCell* cell){
    if (!cell){
        return;
    }
    if (IsCellInRoom(cell)){
        return;
    }
    cells.push_back(cell);
}