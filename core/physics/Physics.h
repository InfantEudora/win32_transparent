#ifndef _PHYSICS_H_
#define _PHYSICS_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

#include "PhysicsBody.h"
#include "PhysicsWorld.h"

/*
    Most of physics are wrappers around the library, but allow for easy type conversion.
*/

class Physics{
public:
    Physics(PhysicsWorld* _world);   //Aways needs a world to live in
    ~Physics();

	PhysicsWorld* world = NULL; //A reference to the world it's in.
	PhysicsBody* body = NULL;	//Holds all body stuff. Whenever physics is constructed, so it this body pointer.

    //States and flags
    void SetGravityEnabled(bool grav);
    bool IsGravityEnabled();
    bool IsSleeping();
    void WakeUp();
    void SetStatic(bool _static);
    bool IsStatic();
    void SetActive(bool active);    // ou'd think this completely disables it.
    bool IsActive();

    //Position etc
    vec3 GetBodyWorldPosition();
    quat GetBodyWorldOrientation();
    void SetBodyWorldPosition(const vec3& pos);
    void SetBodyWorldOrientation(const quat& q);

    //Colliders
    uint32_t GetNumColliders();
    void AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation);
    void AddCapsuleCollider(const float radius, const float v,const vec3& pos,const quat& orientation);
    void AddSphereCollider(const float size,const vec3& pos,const quat& orientation);

    //Force, acceleration velocity etc.
    void AddLocalForce(const vec3& force);
    void AddWorldForceAt(const vec3& force, const vec3& point);
    void AddLocalTorque(const vec3& torque);
    void AddWorldTorque(const vec3& torque);
    vec3 GetForce();
    void SetVelocity(const vec3& v);
    vec3 GetVelocity();
    void SetAngularVelocity(const vec3& v);
    vec3 GetCenterofMass();
    float GetFrictionCoefficient();
    void SetFrictionCoefficient(float v);
    float GetBounciness();
    void SetBounciness(float v);
};


#endif
