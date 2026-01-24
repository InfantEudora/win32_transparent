#include "glad.h"
#include <vector>
#include "Physics.h"
#include "PhysicsWorld.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Physics", DEBUG_ALL);

Physics::Physics(PhysicsWorld* _world){
	//debug->Info("New Physics from world %p\n",_world);
	world = _world;
	body = new PhysicsBody();
	//Create a body for this dude.
	body->collider = NULL;
	body->rigidbody = world->rp_world->createRigidBody(reactphysics3d::Transform::identity());
	//This only works on > 0.9.0
	body->rigidbody->setIsDebugEnabled(true);
}

Physics::~Physics(){

}

float Physics::GetMass(){
	return body->rigidbody->getMass();
}

void Physics::SetBodyWorldPosition(const vec3& wp){
	reactphysics3d::Transform t = body->rigidbody->getTransform();
	reactphysics3d::Vector3 p = reactphysics3d::Vector3(wp.x,wp.y,wp.z);
	t.setPosition(p);
	bool f_active = body->rigidbody->isActive();
	body->rigidbody->setIsActive(false);
	body->rigidbody->setTransform(t);
	body->rigidbody->setIsActive(f_active);
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
	bool f_active = body->rigidbody->isActive();
	rp3d::BodyType type = body->rigidbody->getType();
	if (type == rp3d::BodyType::STATIC){
		body->rigidbody->setIsActive(false);
	}
	body->rigidbody->setTransform(t);
	if (type == rp3d::BodyType::STATIC){
		body->rigidbody->setIsActive(f_active);
	}

	world->WakeUpEveryone();
}

//Toggles the body to be either static or dynamic
void Physics::SetStatic(bool _static){
	bool f_active = body->rigidbody->isActive();
	body->rigidbody->setIsActive(false);
	if (_static){
		body->rigidbody->setType(rp3d::BodyType::STATIC);
	}else{
		body->rigidbody->setType(rp3d::BodyType::DYNAMIC);
	}
	body->rigidbody->setIsActive(f_active);
}

void Physics::SetTrigger(bool trigger){
	body->collider->setIsTrigger(trigger);
}

bool Physics::IsTrigger(){
	return body->collider->getIsTrigger();
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

void Physics::AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation,float density){
    reactphysics3d::BoxShape* boxShape = PhysicsWorld::physicsCommon->createBoxShape((reactphysics3d::Vector3&)box);
	reactphysics3d::Transform t = reactphysics3d::Transform::identity();
	t.setPosition((reactphysics3d::Vector3&)pos);
	t.setOrientation((reactphysics3d::Quaternion&)orientation);
	//body->collision_shape = boxShape;
	if (body->rigidbody){
		body->collider = body->rigidbody->addCollider(boxShape, t);
		body->collider->getMaterial().setMassDensity(density);
		body->collider->getMaterial().setFrictionCoefficient(1.0);
		body->rigidbody->setAngularDamping(0.5);
		body->rigidbody->setLinearDamping(0.5);
		body->rigidbody->updateMassPropertiesFromColliders();

	}
	//debug->Info("Box Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

//Creates a Sphere collision shape of size
void Physics::AddSphereCollider(const float size,const vec3& pos,const quat& orientation,float density){
    rp3d::SphereShape* sphereShape = PhysicsWorld::physicsCommon->createSphereShape(size);
	rp3d::Transform t = rp3d::Transform::identity();
	t.setPosition((rp3d::Vector3&)pos);
	t.setOrientation((rp3d::Quaternion&)orientation);
	//body->collision_shape = sphereShape;
	if (body->rigidbody){
		body->collider = body->rigidbody->addCollider(sphereShape, t);
		body->collider->getMaterial().setMassDensity(density);
		//body_collider->getMaterial().setFrictionCoefficient(2);
		//body_collider->getMaterial().setBounciness(0);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	//debug->Info("Sphere Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}


void Physics::AddCapsuleCollider(const float radius, const float height,const vec3& pos,const quat& orientation,float density){
    rp3d::CapsuleShape* capsuleShape = PhysicsWorld::physicsCommon->createCapsuleShape(radius,height);
	rp3d::Transform t = rp3d::Transform::identity();
	t.setPosition((rp3d::Vector3&)pos);
	t.setOrientation((rp3d::Quaternion&)orientation);
	if (body->rigidbody){
		body->collider = body->rigidbody->addCollider(capsuleShape, t);
		body->collider->getMaterial().setMassDensity(density);
		//body_collider->getMaterial().setFrictionCoefficient(2);
		//body_collider->getMaterial().setBounciness(0);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	//debug->Info("Capsule Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

//Add local force at centre of mass
void Physics::AddLocalForce(const vec3& force){
	rp3d::Vector3 f(force.x,force.y,force.z);
	body->rigidbody->applyLocalForceAtCenterOfMass(f);
}

//Add a world force add a world point
void Physics::AddWorldForceAt(const vec3& force, const vec3& point){
	rp3d::Vector3 f(force.x,force.y,force.z);
	body->rigidbody->applyWorldForceAtWorldPosition((rp3d::Vector3&)f,(rp3d::Vector3&)point);
}

void Physics::SetVelocity(const vec3& v){
	body->rigidbody->setLinearVelocity(rp3d::Vector3(v.x,v.y,v.z));
}

void Physics::SetAngularVelocity(const vec3& v){
	body->rigidbody->setAngularVelocity(rp3d::Vector3(v.x,v.y,v.z));
}

//Return the velocity
vec3 Physics::GetVelocity(){
	rp3d::Vector3 v = body->rigidbody->getLinearVelocity();
	return vec3(v.x,v.y,v.z);
}

vec3 Physics::GetAngularVelocity(){
	rp3d::Vector3 v = body->rigidbody->getAngularVelocity();
	return vec3(v.x,v.y,v.z);
}

vec3 Physics::GetForce(){
	rp3d::Vector3 v = body->rigidbody->getForce();
	return vec3(v.x,v.y,v.z);
}

vec3 Physics::GetCenterofMass(){
	rp3d::Vector3 v = body->rigidbody->getLocalCenterOfMass();
	return vec3(v.x,v.y,v.z);
}

//Add local torque at centre of mass
void Physics::AddLocalTorque(const vec3& torque){
	reactphysics3d::Vector3 f(torque.x,torque.y,torque.z);
	body->rigidbody->applyLocalTorque(f);
}

//Add world torque at centre of mass
void Physics::AddWorldTorque(const vec3& torque){
	reactphysics3d::Vector3 f(torque.x,torque.y,torque.z);
	body->rigidbody->applyWorldTorque(f);
}

void Physics::SetFrictionCoefficient(float f){
	if (f < 0){
		f = 0;
	}
	if (body->collider){
		body->collider->getMaterial().setFrictionCoefficient(f);
	}
}

float Physics::GetFrictionCoefficient(){
	if (body->collider){
		return body->collider->getMaterial().getFrictionCoefficient();
	}
	return 0;
}

void Physics::SetBounciness(float f){
	f = clamp(f,0.0,1.0);
	if (body->collider){
		body->collider->getMaterial().setBounciness(f);
	}
}

float Physics::GetBounciness(){
	if (body->collider){
		return body->collider->getMaterial().getBounciness();
	}
	return 0;
}

void Physics::CreateBallAndSocketJoint(PhysicsBody* body1, PhysicsBody* body2, const vec3& wp){
	const rp3d::Vector3 anchorPoint = (rp3d::Vector3&)wp;

	// Create the joint info object
	rp3d::BallAndSocketJointInfo jointInfo(body1->rigidbody, body2->rigidbody, anchorPoint);

	// Create the joint in the physics world
	rp3d::BallAndSocketJoint* joint;
	joint = dynamic_cast<rp3d::BallAndSocketJoint*>(world->rp_world->createJoint(jointInfo));
	debug->Info("Physics created a joint at %.2f %.2f %.2f\n",anchorPoint.x,anchorPoint.y,anchorPoint.z);
}