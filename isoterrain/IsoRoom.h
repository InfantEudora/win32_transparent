#ifndef _ISO_ROOM_H_
#define _ISO_ROOM_H_
#include "Object.h"
#include "type_int3.h"
#include "AssetManager.h"
#include <map>
#include <array>
#include "IsoDirection.h"

/*
    IsoRoom describes rooms what are/should contain cells and walls.
    It holds data for layout and how they connect.
    The room itself cannot be places in the scene
*/

class IsoRoom;
#include "IsoCell.h"

class IsoRoom{
public:
    IsoRoom();
    ~IsoRoom();

    int GetSize();
    bool IsCellInRoom(IsoCell* cell);
    void AddCell(IsoCell* cell);

    std::string name;
    std::vector<IsoCell*>cells; // Refrence to the cells the room encompases

};


#endif