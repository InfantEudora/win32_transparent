#ifndef _PINBALL_MECHANISMS_H_
#define _PINBALL_MECHANISMS_H_

#include "AssetManager.h"
#include "Object.h"
#include "Physics.h"
#include "Scene.h"

#include "Table.h"

/*
    The two things on a pinball table that MOVE under the player's hand: a flipper and the
    plunger. Both are an Object that IS the moving part (the bat, the plunger tip) with a rigid
    body, a joint back to a static anchor body, and a motor - built the way apps/ship/HingedDoor
    builds its door, which pinball_design.md 2 names as the template. Same discipline in the
    destructor: the joint holds raw body pointers and has to go before either body does.

    --- WHY A JOINT AND NOT A KINEMATIC BODY ---------------------------------------------------
    A bat could be a kinematic body driven to an angle each tick. It would arrive exactly where
    it was told and it would push the ball at exactly the speed it swept - which is the problem.
    A flipper's feel is the motor STRUGGLING: it accelerates hard, it is slowed by a ball it meets
    on the way, it stalls against a ball pinned in the corner. A hinge with a torque-capped motor
    gives all of that for free, and it gives the solver a real body with a real velocity to
    collide the ball against. The same argument makes the plunger a spring rather than a scripted
    push.

    --- THREADS --------------------------------------------------------------------------------
    Construction on the render thread from Init, like every other body in the app. Everything
    else - SetFlip, SetPull, UpdatePhysicsState - is physics thread only, from inside a tick.
    Reading GetAngleDegrees / GetTravel is safe from a tick boundary (see Scene::AtTickBoundary).
*/

class Flipper : public Object{
public:
    /*
        `part` is the node in parts.glb to take the bat mesh from; `fallback` is used when it is
        not there. Either way the bat's PIVOT IS THE OBJECT ORIGIN and the bat lies along local +X
        - that is what the hinge is placed on and what the Blender part is modelled to
        (pinball_design.md 3.2). The mirrored bat is the same bat turned through 180 - angle; see
        the note in ApplicationPinball::AddFlipper's predecessor, now here.
    */
    Flipper(const char* name, AssetManager* assets, const char* part, Mesh* fallback, int material,
            PhysicsWorld* world, Scene* scene,
            const vec3& pivot, float length, float rest_degrees, float up_degrees, bool f_mirrored);
    ~Flipper();

    //Physics thread, once per tick: drive toward up while true, back to rest while false.
    void SetFlip(bool f_flip);
    bool IsFlipping(){ return f_flipping; }

    //Degrees away from rest, positive toward up, for both hands. Measured off the bat's own
    //rotation rather than read from the joint, so it does not depend on which way round rp3d
    //signs its hinge angle.
    float GetAngleDegrees();
    float GetSweepDegrees(){ return up_degrees - rest_degrees; }

    //Live tuning, read every tick by SetFlip. The panel writes these with physics_mutex held.
    float motor_speed   = PIN_FLIPPER_MOTOR_SPEED;
    float motor_torque  = PIN_FLIPPER_MOTOR_TORQUE;
    float return_speed  = PIN_FLIPPER_RETURN_SPEED;
    float return_torque = PIN_FLIPPER_RETURN_TORQUE;

    vec3  pivot = {};
    float length = 0.0f;
    float rest_degrees = 0.0f;
    float up_degrees = 0.0f;
    bool  f_mirrored = false;

    Object* anchor = NULL;              //the static body the hinge hangs off; no collider
    rp3d::HingeJoint* hinge = NULL;

private:
    PhysicsWorld* physics_world = NULL;
    quat  rest_rotation;
    //+1 when a positive rotation about +Y carries the bat from rest to up (the left hand), -1
    //when it is a negative one (the mirrored right hand). Multiplies the motor speed and picks
    //which side of zero the joint limit sits on.
    float direction = 1.0f;
    bool  f_flipping = false;
};

class Plunger : public Object{
public:
    //`rest_position` is the tip's centre at rest; the slider runs from there along +Z (toward
    //the player) for `travel`. The tip mesh lies along its own +Y like MakeCylinder's, and is
    //turned onto +Z here - the collider is placed in that turned frame.
    Plunger(const char* name, AssetManager* assets, const char* part, Mesh* fallback, int material,
            PhysicsWorld* world, Scene* scene, const vec3& rest_position, float travel);
    ~Plunger();

    //Physics thread, once per tick: pull back while true; let the spring have it while false.
    void SetPull(bool f_pull);
    bool IsPulling(){ return f_pulling; }

    //How far back from rest the tip is, 0..travel.
    float GetTravel();

    //The spring, applied once per tick like HingedDoor's. Forces are cleared by rp3d after every
    //step, so this lasts exactly the tick that follows.
    void UpdatePhysicsState() override;

    float spring     = PIN_PLUNGER_SPRING;
    float damping    = PIN_PLUNGER_DAMPING;
    float pull_speed = PIN_PLUNGER_PULL_SPEED;
    float pull_force = PIN_PLUNGER_PULL_FORCE;

    vec3  rest_position = {};
    float travel = 0.0f;

    Object* anchor = NULL;
    rp3d::SliderJoint* slider = NULL;

private:
    PhysicsWorld* physics_world = NULL;
    bool f_pulling = false;
};

#endif
