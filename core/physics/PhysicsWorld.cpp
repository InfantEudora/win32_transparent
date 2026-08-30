#include "glad.h"
#include <vector>
#include "PhysicsWorld.h"
#include "Debug.h"

/*
    A physics world is the world where all colliders and whatnot exist to interact with each other.
    Typically you'd have a single world... but who knows.. you can have more.
*/

static Debugger *debug = new Debugger("PhysicsWorld", DEBUG_ALL);

reactphysics3d::PhysicsCommon* PhysicsWorld::physicsCommon = NULL;

PhysicsWorld::PhysicsWorld(){
	debug->Info("Using ReactPhysics Version %s\n",reactphysics3d::RP3D_VERSION.c_str());
	if (!physicsCommon){
		//Create once
		physicsCommon = new reactphysics3d::PhysicsCommon();
	}
    rp_world = physicsCommon->createPhysicsWorld();
	rp_world->setNbIterationsPositionSolver(10);
	rp_world->setNbIterationsVelocitySolver(12);
	debug_renderer = &rp_world->getDebugRenderer();
	rp_world->setIsDebugRenderingEnabled(false);
	debug_renderer->setIsDebugItemDisplayed(reactphysics3d::DebugRenderer::DebugItem::COLLISION_SHAPE,true);
	debug_renderer->setIsDebugItemDisplayed(reactphysics3d::DebugRenderer::DebugItem::CONTACT_POINT,true);
};

void PhysicsWorld::SetTestOverlapCallback(rp3d::OverlapCallback* callback){
	testoverlap_callback = callback;
}

//Wakeup every sleeping body in the world.
void PhysicsWorld::WakeUpEveryone(){
	for (uint32_t i=0;i<rp_world->getNbRigidBodies();i++){
		reactphysics3d::RigidBody* body = rp_world->getRigidBody(i);
		body->setIsSleeping(false);
	}
}

//Updates all the stuff in this world.
void PhysicsWorld::Update(float dt){
	if (rp_world && !f_test_collision_only){
		rp_world->update(dt);
	}else{
		//Just do collision detection
		if (testoverlap_callback){
			rp_world->testOverlap(*testoverlap_callback);
		}
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

//Collects the nearest hit: returning raycastInfo.hitFraction from notifyRaycastHit clips the
//ray to no farther than that fraction for the remainder of the query, so whichever hit is
//still recorded once reactphysics3d finishes checking every collider is guaranteed nearest -
//this is the standard "find nearest hit" pattern for rp3d's callback-based raycast API.
namespace {
    class NearestRaycastCallback : public reactphysics3d::RaycastCallback {
    public:
        reactphysics3d::RigidBody* exclude = NULL;
        bool hit = false;
        reactphysics3d::Vector3 point;
        reactphysics3d::Vector3 normal;

        reactphysics3d::decimal notifyRaycastHit(const reactphysics3d::RaycastInfo &info) override {
            if (exclude && info.body == exclude) {
                return -1.0f; // ignore this collider, keep casting at the same max fraction
            }
            hit = true;
            point = info.worldPoint;
            normal = info.worldNormal;
            return info.hitFraction; // clip further queries to no farther than this hit
        }
    };
}

PhysicsWorld::RaycastHit PhysicsWorld::Raycast(const vec3& from, const vec3& to, reactphysics3d::RigidBody* exclude_rigidbody){
    reactphysics3d::Ray ray((reactphysics3d::Vector3&)from,(reactphysics3d::Vector3&)to);
    NearestRaycastCallback callback;
    callback.exclude = exclude_rigidbody;
    rp_world->raycast(ray,&callback);

    RaycastHit result;
    result.hit = callback.hit;
    if (callback.hit){
        result.point = (vec3&)callback.point;
        result.normal = (vec3&)callback.normal;
    }
    return result;
}

void PhysicsWorld::SetDebugRendering(bool state){
	rp_world->setIsDebugRenderingEnabled(state);
}

bool PhysicsWorld::IsDebugRenderingEnabled(){
	return rp_world->getIsDebugRenderingEnabled();
}
