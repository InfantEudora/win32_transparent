#ifndef _ISO_PATH_H_
#define _ISO_PATH_H_

#include <vector>
#include <deque>
#include "IsoCell.h"

class IsoPath{
public:
    IsoPath();
    ~IsoPath();

    void Clear();
    bool Empty() const;

    // Cells are ordered from start -> end
    std::vector<IsoCell*> cells;

    // Pop the next cell in the path and return it (or NULL if empty)
    IsoCell* PopNext();

    // Build a path that only traverses cells that have roads (object_road != NULL).
    // Returns true on success and fills out.path
    static bool BuildPath(IsoTerrain* terrain, IsoCell* start, IsoCell* end, IsoPath& out);

    // Find the closest cell (in Manhattan grid sense) reachable from `start` that has a road.
    // Returns NULL if none found.
    static IsoCell* FindClosestRoadCell(IsoTerrain* terrain, IsoCell* start);
};

#endif
