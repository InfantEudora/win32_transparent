#ifndef _ISO_ROAD_H_
#define _ISO_ROAD_H_

#include <vector>
#include <deque>
#include <string>
#include "Object.h"
#include "type_vec3.h"
#include "AssetManager.h"

enum RoadMarkerType {
    LANE,
    PARKING,
    CROSSING
};

enum RoadType {
    STRAIGHT,
    CURVE,
    T_JUNCTION,
    CROSS_JUNCTION,
    DEAD_END
};

inline std::string RoadTypeToString(RoadType type) {
    switch(type) {
        case RoadType::STRAIGHT:        return "Straight";
        case RoadType::CURVE:           return "Curve";
        case RoadType::T_JUNCTION:      return "T-Junction";
        case RoadType::CROSS_JUNCTION:  return "Cross-Junction";
        case RoadType::DEAD_END:        return "Dead-End";
        default:                        return "Unknown";
    }
}

class IsoRoad;
class IsoRoad : public virtual Object{
public:
    IsoRoad();
    ~IsoRoad();

    RoadType road_type = RoadType::STRAIGHT;
    int num_lanes = 2;
    bool one_way = false;

    Object* PlaceNewMarker(AssetManager* assetmanager, RoadMarkerType type, vec3 position);
    Object* PlaceDecoration(AssetManager* assetmanager,std::string& name,vec3 position);

    std::vector<Object*>road_markers;
    std::vector<Object*>street_signs;
    std::vector<Object*>decorations;

};

#endif
