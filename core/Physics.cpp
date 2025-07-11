#include "glad.h"
#include <vector>
#include "Physics.h"
#include "PhysicsWorld.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Physics", DEBUG_ALL);

Physics::Physics(PhysicsWorld* _world){
	debug->Info("New Physics from world %p\n",_world);
	world = _world;
	body = new PhysicsBody();
	//Create a body for this dude.
	body->collider = NULL;
	body->rigidbody = world->rp_world->createRigidBody(reactphysics3d::Transform::identity());
}

Physics::~Physics(){

}


void Physics::SetBodyWorldPosition(const vec3& wp){
	reactphysics3d::Transform t = body->rigidbody->getTransform();
	reactphysics3d::Vector3 p = reactphysics3d::Vector3(wp.x,wp.y,wp.z);
	t.setPosition(p);
	body->rigidbody->setTransform(t);
	world->WakeUpEveryone();
}

vec3 Physics::GetBodyWorldPosition(){
	reactphysics3d::Transform t = body->rigidbody->getTransform();
	return (vec3&)t.getPosition();
}

quat Physics::GetBodyWorldOrientation(){
	reactphysics3d::Transform t = body->rigidbody->getTransform();
	return (quat&)t.getOrientation();
}

//Toggles the body to be either static or dynamic
void Physics::SetStatic(bool _static){
	if (_static){
		body->rigidbody->setType(reactphysics3d::BodyType::STATIC);
	}else{
		body->rigidbody->setType(reactphysics3d::BodyType::DYNAMIC);
	}
}

// If gravity is applied to this body.
void Physics::SetGravityEnabled(bool grav){
	body->rigidbody->enableGravity(grav);
	WakeUp();
}

bool Physics::IsGravityEnabled(){
	return body->rigidbody->isGravityEnabled();
}

void Physics::WakeUp(){
	body->rigidbody->setIsSleeping(false);
}

void Physics::AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation){
    reactphysics3d::BoxShape* boxShape = PhysicsWorld::physicsCommon.createBoxShape((reactphysics3d::Vector3&)box);
	reactphysics3d::Transform t = reactphysics3d::Transform::identity();
	t.setPosition((reactphysics3d::Vector3&)pos);
	t.setOrientation((reactphysics3d::Quaternion&)orientation);
	body->collision_shape = boxShape;
	if (body->rigidbody){
		body->collider = body->rigidbody->addCollider(body->collision_shape, t);
		body->collider->getMaterial().setMassDensity(1.0);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	debug->Info("Box collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}