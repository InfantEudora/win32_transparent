#include "glad.h"
#include <vector>
#include "PhysicsWorld.h"
#include "Debug.h"

/*
    A physics world is the world where all colliders and whatnot exist to interact with each other.
    Typically you'd have a single world... but who knows.. you can have more.
*/

static Debugger *debug = new Debugger("PhysicsWorld", DEBUG_ALL);

reactphysics3d::PhysicsCommon PhysicsWorld::physicsCommon;

PhysicsWorld::PhysicsWorld(){
    rp_world = physicsCommon.createPhysicsWorld();
	rp_world->setIsDebugRenderingEnabled(false);
};

//Wakeup every sleeping body in the world.
void PhysicsWorld::WakeUpEveryone(){
	for (uint32_t i=0;i<rp_world->getNbRigidBodies();i++){
		reactphysics3d::RigidBody* body = rp_world->getRigidBody(i);
		body->setIsSleeping(false);
	}
}

//Updates all the stuff in this world.
void PhysicsWorld::Update(float dt){
	if (rp_world){
		rp_world->update(dt);
	}
}

void PhysicsWorld::SetGravity(const vec3& vector){
	if (!rp_world){
		debug->Err("No world to set gravity on.\n");
        return;
	}
	rp_world->setGravity((reactphysics3d::Vector3&)vector);
}

vec3 PhysicsWorld::GetGravity(){
	if (!rp_world){
		debug->Err("No world to get gravity from\n");
        return vec3();
	}
	reactphysics3d::Vector3 g = rp_world->getGravity();
	return (vec3&)g;
}

void PhysicsWorld::SetDebugRendering(bool state){
	rp_world->setIsDebugRenderingEnabled(state);
}

bool PhysicsWorld::IsDebugRenderingEnabled(){
	return rp_world->getIsDebugRenderingEnabled();
}
