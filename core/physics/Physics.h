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
    void SetTrigger(bool trigger);
    bool IsTrigger();

    //Position etc
    vec3 GetBodyWorldPosition();
    quat GetBodyWorldOrientation();
    void SetBodyWorldPosition(const vec3& pos);
    void SetBodyWorldOrientation(const quat& q);

    //Colliders
    uint32_t GetNumColliders();
    void AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation, float density = 1.0f);
    void AddCapsuleCollider(const float radius, const float v,const vec3& pos,const quat& orientation,float density = 1.0f);
    void AddSphereCollider(const float size,const vec3& pos,const quat& orientation,float density = 1.0f);
    //Static terrain collider. heights is a row-major grid (index = z*columns+x), same layout
    //as CreateMeshFromHeightmap. cell_size_x/cell_size_z stretch the (columns-1)x(rows-1) local
    //grid to world-space spacing - pass the same values used to build the matching render mesh.
    void AddHeightFieldCollider(const std::vector<float>& heights,int columns,int rows,float cell_size_x,float cell_size_z,const vec3& pos,const quat& orientation);

    //Force, acceleration velocity etc.
    void AddLocalForce(const vec3& force);
    void AddWorldForceAt(const vec3& force, const vec3& point);
    void AddLocalTorque(const vec3& torque);
    void AddWorldTorque(const vec3& torque);
    float GetMass();
    vec3 GetForce();
    void SetVelocity(const vec3& v);
    vec3 GetVelocity();
    void SetAngularVelocity(const vec3& v);
    vec3 GetAngularVelocity();
    vec3 GetCenterofMass();
    float GetFrictionCoefficient();
    void SetFrictionCoefficient(float v);
    float GetBounciness();
    void SetBounciness(float v);

    //Joints
    void CreateBallAndSocketJoint(PhysicsBody* a, PhysicsBody* b,const vec3& wp);

};


#endif
