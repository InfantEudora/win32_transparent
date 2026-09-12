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
	body->last_collider = NULL;
	body->rigidbody = world->rp_world->createRigidBody(reactphysics3d::Transform::identity());
	//This only works on > 0.9.0
	body->rigidbody->setIsDebugEnabled(true);
}

/*
	Gives back everything this body took. It used to give back nothing: Object::~Object called
	destroyRigidBody itself and this was empty, which left the Physics, the PhysicsBody and every
	collision shape behind on each destroyed object. Measured at 372 bytes per object with one box
	collider, growing linearly - which is a slow bleed in any app that spawns and reaps things, and
	those are exactly the apps that call DeleteDestroyedObjects.

	THE SHAPES ARE THE PART THAT IS NOT OBVIOUS. reactphysics3d keeps collision shapes in
	PhysicsCommon, not on the body, and destroyRigidBody only calls removeAllColliders - so the
	COLLIDERS go and the SHAPES stay, for the life of the process.

	And only the shapes that are ours. CloneShape copies a box, a sphere and a capsule and SHARES
	anything else, and ScaleColliders draws the same line for the same reason, so a mesh or
	heightfield shape may well be a second body's as well; freeing one here would pull the collider
	out from under that body. Those are still leaked, deliberately - they belong to static terrain
	created once, not to the spawned-and-destroyed objects this is about.

	Read the shapes BEFORE destroying the body, because destroying it takes the colliders - and the
	only route to a shape - with it.
*/
Physics::~Physics(){
	if (!body){
		return;
	}

	if (body->rigidbody && world && world->rp_world){
		std::vector<rp3d::CollisionShape*> own_shapes;
		for (uint32_t i = 0;i < body->rigidbody->getNbColliders();i++){
			rp3d::Collider* collider = body->rigidbody->getCollider(i);
			rp3d::CollisionShape* shape = collider ? collider->getCollisionShape() : NULL;
			if (!shape){
				continue;
			}
			rp3d::CollisionShapeName name = shape->getName();
			if ((name == rp3d::CollisionShapeName::BOX) ||
				(name == rp3d::CollisionShapeName::SPHERE) ||
				(name == rp3d::CollisionShapeName::CAPSULE)){
				own_shapes.push_back(shape);
			}
		}

		world->rp_world->destroyRigidBody(body->rigidbody);
		body->rigidbody = NULL;
		body->last_collider = NULL;

		if (PhysicsWorld::physicsCommon){
			for (rp3d::CollisionShape* shape:own_shapes){
				switch (shape->getName()){
					case rp3d::CollisionShapeName::BOX:
						PhysicsWorld::physicsCommon->destroyBoxShape(static_cast<rp3d::BoxShape*>(shape));
						break;
					case rp3d::CollisionShapeName::SPHERE:
						PhysicsWorld::physicsCommon->destroySphereShape(static_cast<rp3d::SphereShape*>(shape));
						break;
					case rp3d::CollisionShapeName::CAPSULE:
						PhysicsWorld::physicsCommon->destroyCapsuleShape(static_cast<rp3d::CapsuleShape*>(shape));
						break;
					default:
						break;
				}
			}
		}
	}

	delete body;
	body = NULL;
}

float Physics::GetMass(){
	return body->rigidbody->getMass();
}

//Overrides the mass set by AddBoxCollider/AddCapsuleCollider/etc's own density param. rp3d's
//setMass() alone leaves the inertia tensor at whatever the colliders computed, which would make a
//heavier body tumble like the lighter one - so the tensor is scaled along with it (inertia is
//linear in mass for a fixed shape), keeping this a physically consistent "same shape, different
//weight" knob.
void Physics::SetMass(float mass){
	if (body->rigidbody){
		float old_mass = body->rigidbody->getMass();
		if (old_mass > 0.0f && mass > 0.0f){
			rp3d::Vector3 inertia = body->rigidbody->getLocalInertiaTensor();
			body->rigidbody->setLocalInertiaTensor(inertia * (mass / old_mass));
		}
		body->rigidbody->setMass(mass);
	}
}

//Some note here: We need to use setIsActive false before changing transform, else it will not update properly.
//Apparently also the angular velocity gets reset on setTransform, so we need to store it and reapply it.
void Physics::SetBodyWorldPosition(const vec3& wp){
	reactphysics3d::Transform t = body->rigidbody->getTransform();
	reactphysics3d::Vector3 p = reactphysics3d::Vector3(wp.x,wp.y,wp.z);
	t.setPosition(p);
	bool f_active = body->rigidbody->isActive();
	vec3 angvel = GetAngularVelocity();
	body->rigidbody->setIsActive(false);
	body->rigidbody->setTransform(t);
	body->rigidbody->setIsActive(f_active);
	SetAngularVelocity(angvel);
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
	if (!body || !body->rigidbody){
		debug->Warn("Cannot SetStatic() without rigidbody\n");
		return;
	}
	bool f_active = body->rigidbody->isActive();
	body->rigidbody->setIsActive(false);
	if (_static){
		body->rigidbody->setType(rp3d::BodyType::STATIC);
	}else{
		body->rigidbody->setType(rp3d::BodyType::DYNAMIC);
	}
	body->rigidbody->setIsActive(f_active);
}

rp3d::BodyType Physics::GetBodyType(){
	return body->rigidbody->getType();
}

void Physics::SetBodyType(rp3d::BodyType type){
	if (!body || !body->rigidbody){
		return;
	}
	if (body->rigidbody->getType() == type){
		return;
	}
	bool f_active = body->rigidbody->isActive();
	body->rigidbody->setIsActive(false);
	body->rigidbody->setType(type);
	body->rigidbody->setIsActive(f_active);
}

/*
	Guarded like SetBounciness/GetBounciness just above, and for the same reason: last_collider is
	only ever set by an Add*Collider call, and AddPhysics deliberately creates a body and leaves
	the colliders to the caller. So a body between those two calls has none, and these two were the
	only methods here that dereferenced it anyway.

	The asymmetry is deliberate. A SET that silently does nothing leaves the caller believing it
	has a trigger, and the thing it was meant to do - stop generating contacts - looks like a
	physics bug somewhere else entirely; that is worth a line in the log. A GET has a truthful
	answer to give: a body with no collider is not a trigger.
*/
void Physics::SetTrigger(bool trigger){
	if (!body || !body->last_collider){
		debug->Err("SetTrigger on a body with no collider - add a collider first, or this does nothing\n");
		return;
	}
	body->last_collider->setIsTrigger(trigger);
}

bool Physics::IsTrigger(){
	if (!body || !body->last_collider){
		return false;
	}
	return body->last_collider->getIsTrigger();
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

/*
	Collision filtering. See the block above the declarations in Physics.h for why the bits are
	kept here rather than only pushed at the colliders.

	The body is deactivated across the change because rp3d caches broad-phase pairs: an already
	overlapping pair keeps its old filter until the pair is rebuilt, so a filter changed on a
	live body can take effect a tick late or not at all. Cheaper than reasoning about when it
	matters.
*/
void Physics::ApplyCollisionBits(rp3d::Collider* collider){
	if (!collider){
		return;
	}
	collider->setCollisionCategoryBits((unsigned short)collision_category_bits);
	collider->setCollideWithMaskBits((unsigned short)collide_with_bits);
}

void Physics::SetCollisionCategoryBits(uint32_t bits){
	collision_category_bits = bits;
	if (!body || !body->rigidbody){
		return;     //remembered; whatever collider comes next will get it
	}
	bool f_active = body->rigidbody->isActive();
	body->rigidbody->setIsActive(false);
	for (uint32_t i = 0;i < body->rigidbody->getNbColliders();i++){
		body->rigidbody->getCollider(i)->setCollisionCategoryBits((unsigned short)bits);
	}
	body->rigidbody->setIsActive(f_active);
}

void Physics::SetCollideWithMaskBits(uint32_t bits){
	collide_with_bits = bits;
	if (!body || !body->rigidbody){
		return;
	}
	bool f_active = body->rigidbody->isActive();
	body->rigidbody->setIsActive(false);
	for (uint32_t i = 0;i < body->rigidbody->getNbColliders();i++){
		body->rigidbody->getCollider(i)->setCollideWithMaskBits((unsigned short)bits);
	}
	body->rigidbody->setIsActive(f_active);
}

uint32_t Physics::GetCollisionCategoryBits(){
	return collision_category_bits;
}

uint32_t Physics::GetCollideWithMaskBits(){
	return collide_with_bits;
}

void Physics::AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation,float density){
    reactphysics3d::BoxShape* boxShape = PhysicsWorld::physicsCommon->createBoxShape((reactphysics3d::Vector3&)box);
	reactphysics3d::Transform t = reactphysics3d::Transform::identity();
	t.setPosition((reactphysics3d::Vector3&)pos);
	t.setOrientation((reactphysics3d::Quaternion&)orientation);
	//body->collision_shape = boxShape;
	if (body->rigidbody){
		body->last_collider = body->rigidbody->addCollider(boxShape, t);
		ApplyCollisionBits(body->last_collider);
		//Density is a parameter, so it is the caller's. Everything else is left at reactphysics3d's
		//own defaults - see the note above AddSphereCollider.
		body->last_collider->getMaterial().setMassDensity(density);
		body->rigidbody->updateMassPropertiesFromColliders();

	}
	//debug->Info("Box Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

/*
	A note that belongs to all four Add*Collider functions.

	THEY SET NOTHING BUT THE DENSITY YOU PASSED. Friction, bounciness and the two dampings are left
	wherever reactphysics3d puts them, which is friction 0.3 and bounciness 0.5 (PhysicsWorld's
	WorldSettings) and damping 0.0 (RigidBodyComponents). If a body wants something else, it says
	so - SetFrictionCoefficient, SetBounciness, SetLinearDamping, SetAngularDamping.

	It was not always so. AddBoxCollider used to force friction to 1.0 and both dampings to 0.5,
	AddCapsuleCollider the two dampings, and AddSphereCollider nothing at all - so two bodies built
	the obvious way behaved differently for reasons nothing stated, a shape swap from box to sphere
	silently changed how a body moved, and two colliders on one body could disagree about whether
	the body was damped depending on which was added last (damping is per BODY, friction per
	COLLIDER - so the last box added set the damping for everything on it). None of those numbers
	was a decision anybody made; they were one app's tuning that never left.

	The cost of removing them is that bodies slide further and tumble longer than they used to.
	That is the library's behaviour, it is now visible at the call site, and an app that wants the
	old feel asks for it in one line.
*/

//Creates a Sphere collision shape of size
void Physics::AddSphereCollider(const float size,const vec3& pos,const quat& orientation,float density){
    rp3d::SphereShape* sphereShape = PhysicsWorld::physicsCommon->createSphereShape(size);
	rp3d::Transform t = rp3d::Transform::identity();
	t.setPosition((rp3d::Vector3&)pos);
	t.setOrientation((rp3d::Quaternion&)orientation);
	//body->collision_shape = sphereShape;
	if (body->rigidbody){
		body->last_collider = body->rigidbody->addCollider(sphereShape, t);
		ApplyCollisionBits(body->last_collider);
		body->last_collider->getMaterial().setMassDensity(density);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	//debug->Info("Sphere Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

//Static terrain collider from a heightmap grid. reactphysics3d only allows concave shapes
//(like this one) on static bodies - call SetStatic(true) on this Physics before using this.
void Physics::AddHeightFieldCollider(const std::vector<float>& heights,int columns,int rows,float cell_size_x,float cell_size_z,const vec3& pos,const quat& orientation){
    std::vector<rp3d::Message> messages;
    rp3d::HeightField* heightfield = PhysicsWorld::physicsCommon->createHeightField(
        columns,rows,heights.data(),rp3d::HeightField::HeightDataType::HEIGHT_FLOAT_TYPE,messages);

    for (rp3d::Message& msg : messages){
        debug->Warn("AddHeightFieldCollider: %s\n",msg.text.c_str());
    }
    if (!heightfield){
        debug->Err("AddHeightFieldCollider: createHeightField failed\n");
        return;
    }

    rp3d::HeightFieldShape* heightfieldShape = PhysicsWorld::physicsCommon->createHeightFieldShape(
        heightfield,rp3d::Vector3(cell_size_x,1.0f,cell_size_z));

    rp3d::Transform t = rp3d::Transform::identity();
    t.setPosition((rp3d::Vector3&)pos);
    t.setOrientation((rp3d::Quaternion&)orientation);
    if (body->rigidbody){
        body->last_collider = body->rigidbody->addCollider(heightfieldShape, t);
        ApplyCollisionBits(body->last_collider);
    }
}


void Physics::AddCapsuleCollider(const float radius, const float height,const vec3& pos,const quat& orientation,float density){
    rp3d::CapsuleShape* capsuleShape = PhysicsWorld::physicsCommon->createCapsuleShape(radius,height);
	rp3d::Transform t = rp3d::Transform::identity();
	t.setPosition((rp3d::Vector3&)pos);
	t.setOrientation((rp3d::Quaternion&)orientation);
	if (body->rigidbody){
		body->last_collider = body->rigidbody->addCollider(capsuleShape, t);
		ApplyCollisionBits(body->last_collider);
		body->last_collider->getMaterial().setMassDensity(density);
		body->rigidbody->updateMassPropertiesFromColliders();
	}
	//debug->Info("Capsule Collider: Object's mass: %.1f kg\n",body->rigidbody->getMass());
}

void Physics::ScaleColliders(const vec3& ratio){
	if (!body->rigidbody){
		return;
	}
	rp3d::RigidBody* rb = body->rigidbody;
	bool f_changed = false;
	for (uint32_t i = 0; i < rb->getNbColliders(); i++){
		rp3d::Collider* collider = rb->getCollider(i);
		rp3d::CollisionShape* shape = collider->getCollisionShape();
		switch (shape->getName()){
			case rp3d::CollisionShapeName::BOX:{
				rp3d::BoxShape* box = static_cast<rp3d::BoxShape*>(shape);
				rp3d::Vector3 he = box->getHalfExtents();
				box->setHalfExtents(rp3d::Vector3(he.x * ratio.x,he.y * ratio.y,he.z * ratio.z));
				break;
			}
			case rp3d::CollisionShapeName::SPHERE:{
				rp3d::SphereShape* sphere = static_cast<rp3d::SphereShape*>(shape);
				sphere->setRadius(sphere->getRadius() * (ratio.x + ratio.y + ratio.z) / 3.0f);
				break;
			}
			case rp3d::CollisionShapeName::CAPSULE:{
				rp3d::CapsuleShape* capsule = static_cast<rp3d::CapsuleShape*>(shape);
				capsule->setRadius(capsule->getRadius() * (ratio.x + ratio.z) * 0.5f);
				capsule->setHeight(capsule->getHeight() * ratio.y);
				break;
			}
			default:
				debug->Warn("ScaleColliders: collider %u is a mesh/heightfield shape, not rescaled\n",i);
				continue;
		}
		//The collider's offset from the body origin scales with the object too.
		rp3d::Transform t = collider->getLocalToBodyTransform();
		rp3d::Vector3 p = t.getPosition();
		t.setPosition(rp3d::Vector3(p.x * ratio.x,p.y * ratio.y,p.z * ratio.z));
		collider->setLocalToBodyTransform(t);
		f_changed = true;
	}
	if (f_changed){
		//New centre of mass and inertia for the new shape, at the mass the body already had -
		//updateMassPropertiesFromColliders() would otherwise reset the mass to density*volume,
		//and SetMass() rescales the freshly computed tensor to the preserved mass.
		float mass = rb->getMass();
		rb->updateMassPropertiesFromColliders();
		if (mass > 0.0f){
			SetMass(mass);
		}
	}
}

rp3d::CollisionShape* Physics::CloneShape(rp3d::CollisionShape* shape){
	if (!shape || !PhysicsWorld::physicsCommon){
		return shape;
	}
	switch (shape->getName()){
		case rp3d::CollisionShapeName::BOX:
			return PhysicsWorld::physicsCommon->createBoxShape(static_cast<rp3d::BoxShape*>(shape)->getHalfExtents());
		case rp3d::CollisionShapeName::SPHERE:
			return PhysicsWorld::physicsCommon->createSphereShape(static_cast<rp3d::SphereShape*>(shape)->getRadius());
		case rp3d::CollisionShapeName::CAPSULE:{
			rp3d::CapsuleShape* capsule = static_cast<rp3d::CapsuleShape*>(shape);
			return PhysicsWorld::physicsCommon->createCapsuleShape(capsule->getRadius(),capsule->getHeight());
		}
		default:
			return shape;
	}
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

//--- Damping and axis locks -------------------------------------------------------------------
//Plain forwards. See the header for why they are worth having: the damping pair exists mostly to
//undo what Add*Collider sets without saying so, and the lock pair is the thing a flat game needs
//and cannot find.

void Physics::SetLinearDamping(float damping){
	body->rigidbody->setLinearDamping(damping);
}

float Physics::GetLinearDamping(){
	return (float)body->rigidbody->getLinearDamping();
}

void Physics::SetAngularDamping(float damping){
	body->rigidbody->setAngularDamping(damping);
}

float Physics::GetAngularDamping(){
	return (float)body->rigidbody->getAngularDamping();
}

void Physics::SetLinearLockAxis(const vec3& factor){
	body->rigidbody->setLinearLockAxisFactor(rp3d::Vector3(factor.x,factor.y,factor.z));
}

vec3 Physics::GetLinearLockAxis(){
	const rp3d::Vector3& f = body->rigidbody->getLinearLockAxisFactor();
	return vec3(f.x,f.y,f.z);
}

void Physics::SetAngularLockAxis(const vec3& factor){
	body->rigidbody->setAngularLockAxisFactor(rp3d::Vector3(factor.x,factor.y,factor.z));
}

vec3 Physics::GetAngularLockAxis(){
	const rp3d::Vector3& f = body->rigidbody->getAngularLockAxisFactor();
	return vec3(f.x,f.y,f.z);
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
	if (body->last_collider){
		body->last_collider->getMaterial().setFrictionCoefficient(f);
	}
}

float Physics::GetFrictionCoefficient(){
	if (body->last_collider){
		return body->last_collider->getMaterial().getFrictionCoefficient();
	}
	return 0;
}

void Physics::SetBounciness(float f){
	f = clamp(f,0.0,1.0);
	if (body->last_collider){
		body->last_collider->getMaterial().setBounciness(f);
	}
}

float Physics::GetBounciness(){
	if (body->last_collider){
		return body->last_collider->getMaterial().getBounciness();
	}
	return 0;
}

rp3d::VehicleConstraint* Physics::CreateVehicle(const rp3d::VehicleConstraintSettings& settings){
	if (!body || !body->rigidbody || !world || !world->rp_world){
		debug->Err("CreateVehicle: no rigidbody/world\n");
		return NULL;
	}
	return world->rp_world->createVehicle(body->rigidbody,settings);
}

void Physics::DestroyVehicle(rp3d::VehicleConstraint* vehicle){
	if (vehicle && world && world->rp_world){
		world->rp_world->destroyVehicle(vehicle);
	}
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