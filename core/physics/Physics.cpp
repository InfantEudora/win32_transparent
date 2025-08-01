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

void Physics::SetBodyWorldOrientation(const quat& q){
	reactphysics3d::Transform t = body->rigidbody->getTransform();
	t.setOrientation(reactphysics3d::Quaternion(q.x,q.y,q.z,q.w));
	body->rigidbody->setTransform(t);
	world->WakeUpEveryone();
}

//Toggles the body to be either static or dynamic
void Physics::SetStatic(bool _static){
	if (_static){
		body->rigidbody->setType(reactphysics3d::BodyType::STATIC);
	}else{
		body->rigidbody->setType(reactphysics3d::BodyType::DYNAMIC);
	}
}

bool Physics::IsStatic(){
	reactphysics3d::BodyType type = body->rigidbody->getType();
	if (type == reactphysics3d::BodyType::STATIC){
		return true;
	}
	return false;
}

void Physics::SetActive(bool active){
	body->rigidbody->setIsActive(active);
	WakeUp();
}

bool Physics::IsActive(){
	return body->rigidbody->isActive();
}

uint32_t Physics::GetNumColliders(){
	return body->rigidbody->getNbColliders();
}

// If gravity is applied to this body.
void Physics::SetGravityEnabled(bool grav){
	body->rigidbody->enableGravity(grav);
	WakeUp();
}

bool Physics::IsGravityEnabled(){
	return body->rigidbody->isGravityEnabled();
}

bool Physics::IsSleeping(){
	return body->rigidbody->isSleeping();
}

void Physics::WakeUp(){
	body->rigidbody->setIsSleeping(false);
}

void Physics::AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation){
    reactphysics3d::BoxShape* boxShape = PhysicsWorld::physicsCommon.createBoxShape((reactphysics3d::Vector3&)box);
	reactphysics3d::Transform t = reactphysics3d::Transform::identity();
	t.setPosition((reactphysics3d::Vector3&)pos);
	t.setOrientation((reactphysics3d::Quaternion&)orientation);
	//body->collision_shape = boxShape;
	if (body->rigidbody){
		body->collider = body->rigidbody->addCollider(boxShape, t);
		body->collider->getMaterial().setMassDensity(1.0);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	debug->Info("Box Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

//Creates a Sphere collision shape of size
void Physics::AddSphereCollider(const float size,const vec3& pos,const quat& orientation){
    reactphysics3d::SphereShape* sphereShape = PhysicsWorld::physicsCommon.createSphereShape(size);
	reactphysics3d::Transform t = reactphysics3d::Transform::identity();
	t.setPosition((reactphysics3d::Vector3&)pos);
	t.setOrientation((reactphysics3d::Quaternion&)orientation);
	//body->collision_shape = sphereShape;
	if (body->rigidbody){
		body->collider = body->rigidbody->addCollider(sphereShape, t);
		body->collider->getMaterial().setMassDensity(1.0);
		//body_collider->getMaterial().setFrictionCoefficient(2);
		//body_collider->getMaterial().setBounciness(0);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	debug->Info("Sphere Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

//Add local force at centre of mass
void Physics::AddLocalForce(const vec3& force){
	reactphysics3d::Vector3 f(force.x,force.y,force.z);
	body->rigidbody->applyLocalForceAtCenterOfMass(f);
}

void Physics::SetVelocity(const vec3& v){
	body->rigidbody->setLinearVelocity(reactphysics3d::Vector3(v.x,v.y,v.z));
}

void Physics::SetAngularVelocity(const vec3& v){
	body->rigidbody->setAngularVelocity(reactphysics3d::Vector3(v.x,v.y,v.z));
}