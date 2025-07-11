#ifndef _PHYSICS_H_
#define _PHYSICS_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

#include "PhysicsBody.h"
#include "PhysicsWorld.h"

class Physics{
public:
    Physics(PhysicsWorld* _world);   //Aways needs a world to live in
    ~Physics();

	PhysicsWorld* world = NULL; //A reference to the world it's in.
	PhysicsBody* body = NULL;	//Holds all body stuff. Whenever physics is constructed, so it this body pointer.

    uint32_t GetNumColliders();
    void SetGravityEnabled(bool grav);
    bool IsGravityEnabled();
    void WakeUp();
    void SetStatic(bool _static);

    vec3 GetBodyWorldPosition();
    quat GetBodyWorldOrientation();
    void SetBodyWorldPosition(const vec3& pos);
    void SetBodyWorldOrientation(const quat& q);


    void AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation);

};


#endif
