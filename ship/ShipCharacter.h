#ifndef _SHIP_CHARACTER_H_
#define _SHIP_CHARACTER_H_

#include "AssetManager.h"
#include "Physics.h"
#include "ParticleEmitter.h"
#include "SoundSystem.h"

class ShipCharacter;

class ShipCharacter : public Object{
    public:
    ShipCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, RRandom* rrand);
    ~ShipCharacter();
    void UpdatePhysicsState() override;

    SoundSystem* soundsystem = NULL;
    ParticleEmitter* exhaust_emitter = NULL;

    void MoveForward();
    void MoveBackward();
    void TurnLeft();
    void TurnRight();

};

#endif