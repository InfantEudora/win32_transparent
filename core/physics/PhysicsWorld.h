#ifndef _PHYSICS_WORLD_H_
#define _PHYSICS_WORLD_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "reactphysics3d.h"
#include "configuration.h"

#include "type_vec3.h"
#include "type_quat.h"
/*
    We're encapsulating reactphysics, so the interfaces will look/match that.
    Ideally. The rest of the codebase can interface with our types and whatnot to whatever logic
    this library has going on.

    Maybe later on, we'll use Box2D it we want do ever do 2D stuff. That should plugin to the same physics object.
    Since it makes sense it can only have one at a time.

    rp_ measns it's from reactphysics3d
*/

class PhysicsWorld{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    void Update(float dt);  // dt should be a constant step size
    void WakeUpEveryone();

    void SetDebugRendering(bool state);
    bool IsDebugRenderingEnabled();

    void SetGravity(const vec3& vector);
    vec3 GetGravity();

    reactphysics3d::PhysicsWorld* rp_world = NULL;
    reactphysics3d::DebugRenderer* debug_renderer = NULL;

    //Static stuff
    static reactphysics3d::PhysicsCommon* physicsCommon;     //Static because it's a singleton. It can create multiple worlds
};


#endif
