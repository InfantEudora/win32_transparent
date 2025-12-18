#ifndef _ISO_ROAD_H_
#define _ISO_ROAD_H_

#include <vector>
#include <deque>
#include "Object.h"

class IsoRoad;
class IsoRoad : public virtual Object{
public:
    IsoRoad();
    ~IsoRoad();

    int num_lanes = 2;
    bool one_way = false;
};

#endif
