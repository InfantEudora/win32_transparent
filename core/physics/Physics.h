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
    //Raw rp3d body type (STATIC / KINEMATIC / DYNAMIC). A KINEMATIC body is moved by setting its
    //velocities: it ignores forces, pushes dynamic bodies, and - unlike a STATIC body being
    //setTransform()ed each tick - joints attached to it see a real velocity to follow instead of a
    //position error to correct afterwards. See Scene::MoveObjectOverTicks.
    rp3d::BodyType GetBodyType();
    void SetBodyType(rp3d::BodyType type);
    void SetActive(bool active);    // ou'd think this completely disables it.
    bool IsActive();
    void SetTrigger(bool trigger);
    bool IsTrigger();

    //Position etc
    vec3 GetBodyWorldPosition();
    quat GetBodyWorldOrientation();
    void SetBodyWorldPosition(const vec3& pos);
    void SetBodyWorldOrientation(const quat& q);

    /*
        Collision filtering for the WHOLE BODY: the category it is in, and the categories it
        collides with.

        THEY LIVE HERE, NOT ON THE COLLIDER, SO THAT ORDER DOES NOT MATTER. rp3d keeps these
        per collider, so the obvious implementation - walk the colliders and set them - does
        nothing at all on a body that has none yet, and AddPhysics deliberately hands back a body
        with no colliders. Setting a filter before adding the shape is therefore the natural thing
        to write and used to be silently ignored, leaving a body that ignores a filter you can see
        set on it in the Inspector.

        So these remember, and every Add*Collider applies what is remembered to the collider it
        just made. Set them before or after, as many times as you like, in any order.

        The defaults are rp3d's own (category 0x0001, collides with everything), so a body that
        never touches them behaves exactly as an untouched rp3d body does.
    */
    void SetCollisionCategoryBits(uint32_t bits);
    void SetCollideWithMaskBits(uint32_t bits);
    uint32_t GetCollisionCategoryBits();
    uint32_t GetCollideWithMaskBits();

    //Colliders
    uint32_t GetNumColliders();
    void AddBoxCollider(const vec3& box,const vec3& pos,const quat& orientation, float density = 1.0f);
    void AddCapsuleCollider(const float radius, const float v,const vec3& pos,const quat& orientation,float density = 1.0f);
    void AddSphereCollider(const float size,const vec3& pos,const quat& orientation,float density = 1.0f);
    //Static terrain collider. heights is a row-major grid (index = z*columns+x), same layout
    //as CreateMeshFromHeightmap. cell_size_x/cell_size_z stretch the (columns-1)x(rows-1) local
    //grid to world-space spacing - pass the same values used to build the matching render mesh.
    void AddHeightFieldCollider(const std::vector<float>& heights,int columns,int rows,float cell_size_x,float cell_size_z,const vec3& pos,const quat& orientation);
    //Scales EVERY collider on this body by ratio (new scale / old scale, per axis): box half
    //extents per axis, sphere radius by the mean ratio, capsule radius by the mean of X/Z and
    //height by Y (its axis), and each collider's local offset. Then recomputes centre of mass
    //and inertia for the new shape while keeping the body's mass what it was - rp3d's own
    //shape setters only re-insert the AABB into the broadphase and leave mass properties stale.
    //Mesh/heightfield colliders are left alone (rp3d can't resize those in place).
    void ScaleColliders(const vec3& ratio);
    //A fresh, unshared copy of a primitive shape (box/sphere/capsule) with the same size, or the
    //same pointer if it's a type that can't be cloned this way - for duplicating an object so
    //its colliders can then be scaled independently of the original's.
    static rp3d::CollisionShape* CloneShape(rp3d::CollisionShape* shape);

    //Force, acceleration velocity etc.
    void AddLocalForce(const vec3& force);
    void AddWorldForceAt(const vec3& force, const vec3& point);
    void AddLocalTorque(const vec3& torque);
    void AddWorldTorque(const vec3& torque);
    float GetMass();
    void SetMass(float mass);
    vec3 GetForce();
    void SetVelocity(const vec3& v);
    vec3 GetVelocity();
    void SetAngularVelocity(const vec3& v);
    vec3 GetAngularVelocity();

    /*
        --- DAMPING ---------------------------------------------------------------------------
        Velocity bled off per second, independent of any contact: 0 leaves a body coasting
        forever, higher values bring it to rest on its own.

        A body starts at rp3d's default of 0 for both - it coasts until something stops it. The
        Add*Collider calls used to force their own values (box and capsule set both dampings to
        0.5, sphere set neither), so two bodies built the obvious way behaved differently for
        reasons nothing stated; they no longer touch damping at all - see the note above the
        collider functions in Physics.cpp. This comment said otherwise until 2026-09-23, which
        was caught by reading a board's damping back over MCP: it reported 0.0, not 0.5.
    */
    void SetLinearDamping(float damping);
    float GetLinearDamping();
    void SetAngularDamping(float damping);
    float GetAngularDamping();

    /*
        --- AXIS LOCKS ------------------------------------------------------------------------
        A per-axis multiplier on how much this body is allowed to move: 1 leaves an axis free,
        0 pins it. Applied by the solver, so a locked axis stays locked through collisions,
        joints and forces alike rather than being corrected afterwards.

        THE REASON THIS IS WORTH KNOWING ABOUT: a flat game built on a 3D solver needs it on its
        first day. Anything given a nudge out of the play plane - and a spawn impulse, a glancing
        contact or an off-centre collider will do it - drifts along the axis nobody is watching.
        In APP=Breakout a power-up capsule drifted a little over a unit out of plane during its
        fall, passed the far face of the paddle's collider, and sailed straight through a paddle
        sitting directly underneath it, generating no contact at all. Every readout in that app
        was two-dimensional, so the capsule and the paddle agreed perfectly in x and y right up
        to the miss; it took printing the third axis to see it.

            //Pin a body to the XY plane, and let it spin only about Z (the axis facing the camera)
            physics->SetLinearLockAxis(vec3(1,1,0));
            physics->SetAngularLockAxis(vec3(0,0,1));

        The capability was always there in reactphysics3d - what was missing was any way to find
        out, which is why these are one-line forwards rather than anything cleverer.
    */
    void SetLinearLockAxis(const vec3& factor);
    vec3 GetLinearLockAxis();
    void SetAngularLockAxis(const vec3& factor);
    vec3 GetAngularLockAxis();
    vec3 GetCenterofMass();
    float GetFrictionCoefficient();
    void SetFrictionCoefficient(float v);
    float GetBounciness();
    void SetBounciness(float v);

    //Joints
    void CreateBallAndSocketJoint(PhysicsBody* a, PhysicsBody* b,const vec3& wp);

    //Vehicles: a raycast-wheel suspension on this body, simulated inside reactphysics3d (its
    //VehicleConstraint - spring/damper per wheel along the contact normal, hard stop, tire
    //friction along and across the rolling direction, all solved together with the rest of the
    //world's constraints). Wheels are added on the returned constraint. Destroying this body
    //destroys the vehicle too, so only call DestroyVehicle while the body still exists.
    rp3d::VehicleConstraint* CreateVehicle(const rp3d::VehicleConstraintSettings& settings);
    void DestroyVehicle(rp3d::VehicleConstraint* vehicle);

private:
    //See the setters above. rp3d's defaults, so remembering them changes nothing by itself.
    uint32_t collision_category_bits = 0x0001;
    uint32_t collide_with_bits = 0xFFFF;

    //Stamps the remembered filter onto one collider. Called by every Add*Collider, which is what
    //makes the filter a property of the body rather than of whichever shapes existed at the time.
    void ApplyCollisionBits(rp3d::Collider* collider);
};


#endif
