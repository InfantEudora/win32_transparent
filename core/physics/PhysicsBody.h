#ifndef _PHYSICS_BODY_H_
#define _PHYSICS_BODY_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "reactphysics3d.h"

//Holds all the physics data that belong to an objects body.
class PhysicsBody{
public:

    PhysicsBody();

    reactphysics3d::RigidBody* rigidbody = NULL;
	reactphysics3d::Collider* collider = NULL; // A reference to the last collider added to the body...
	reactphysics3d::CollisionShape* collision_shape = NULL;
};


#endif
