#ifndef _FARSIDE_FIELD_H_
#define _FARSIDE_FIELD_H_

#include "type_vec3.h"
#include "type_quat.h"
#include <vector>

/*
    The play field's shapes and the three questions asked of them: how high is the ground here,
    is the straight line between two points blocked, and where does a ship that has flown into
    something end up. Plain geometry with no engine objects in it, so the rules read apart from how
    anything is drawn. See docs/README.md, "Geometry" and "Line of sight".

    One world unit is one player-ship length, y is up, and the plane ships fly over is y = 0: the
    waterline. A body sunk through it has its centre below 0, and only the part above 0 is ground.
*/

struct FieldSphere{
    vec3 centre = {};
    float radius = 1.0f;
    //Its spin, for whatever is landed on it: a landed ship is kept in this frame.
    quat rotation = quat(0,0,0,1);
    //Ground (planets, moons): a ship holds its height over the part above the waterline and can
    //land on it. Otherwise an obstacle (asteroids) that a ship is pushed off and flies around.
    bool f_ground = false;
};

//An oriented box: hulls, stations and wrecks, which are obstacles and cover but never ground.
struct FieldBox{
    vec3 centre = {};
    vec3 half = {1,1,1};
    quat rotation = quat(0,0,0,1);
};

struct GroundSample{
    float height = 0.0f;        //y of the ground: 0 over open space, the cap's surface over a body
    vec3 normal = {0,1,0};
    int sphere = -1;            //the ground sphere it is on, -1 for the plane
};

class Field{
public:
    std::vector<FieldSphere> spheres;
    std::vector<FieldBox> boxes;

    void Clear(){ spheres.clear(); boxes.clear(); }

    //The highest ground under (x,z). Where two bodies overlap the higher surface wins, and the
    //plane is the floor of everything.
    GroundSample Ground(float x, float z) const;
    //The top of the tallest ground body, the plane if none rises above it. The ceiling sits a
    //margin above this (README, "Altitude").
    float HighestGround() const;

    //Whether anything stands between a and b. Spheres and boxes both block; a point exactly on
    //a surface does not count as inside it, so a post on the ground can still see past its feet.
    bool Blocked(const vec3& a, const vec3& b) const;

    //Moves a ship out of every sphere and box it overlaps: `radius` clear of obstacles, only
    //`ground_radius` (its landed clearance) clear of ground. Returns how many it was pushed out
    //of; `pushes` collects the outward normals so the caller can stop the velocity going into
    //them.
    int PushOut(vec3& p, float radius, float ground_radius, std::vector<vec3>* pushes = nullptr) const;
};

#endif
