#ifndef _DOZER_CHARACTER_H_
#define _DOZER_CHARACTER_H_

#include "AssetManager.h"
#include "Physics.h"
#include "ParticleEmitter.h"
#include "SoundSystem.h"

/*
    A character that you control.
*/enum engine_state{
    ENGINE_STOPPED = 0,
    ENGINE_STARTING = 1,
    ENGINE_IDLE = 2,
    ENGINE_RUNNING = 3,
    ENGINE_STALLING = 4
};

enum collision_category_bits{
    COLLISION_CATEGORY_FLOOR = 0x0001,
    COLLISION_CATEGORY_OBJECTS = 0x0002,
    COLLISION_CATEGORY_SMOKE = 0x0004
};

class DozerCharacter;

class DozerCharacter : public Object{
    public:
    DozerCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, RRandom* rrand);
    ~DozerCharacter();

    void UpdatePhysicsState() override;

    SoundSystem* soundsystem = NULL;

    Object* armobject = NULL;
    //Child
    Object* body = NULL;
    Object* tracks = NULL;
    Object* exhaust = NULL;
    ParticleEmitter* smoke_emitter = NULL;

    void StartEngine();

    void MoveForward();
    void MoveBackward();
    void TurnLeft();
    void TurnRight();
    void ArmUp();
    void ArmDown();

    int engine_state = ENGINE_STOPPED;

    bool throttle = false;
    bool arm_moving = false;
    float arm_movement = 0;
    float belt_tension = 0;
    float belt_tenstion_max = 1;
    float arm_torque = 0;
    float arm_torque_max = 100;

    rp3d::HingeJoint* joint = NULL;
};

#endif