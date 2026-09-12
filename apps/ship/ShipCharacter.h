#ifndef _SHIP_CHARACTER_H_
#define _SHIP_CHARACTER_H_

#include "AssetManager.h"
#include "Physics.h"
#include "ParticleEmitter.h"
#include "SoundSystem.h"
#include "ShipCollisionMasks.h"

class ShipCharacter;

class ShipCharacter : public Object{
    public:
    ShipCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, RRandom* rrand);
    ~ShipCharacter();
    void UpdatePhysicsState() override;

    SoundSystem* soundsystem = NULL;
    ParticleEmitter* exhaust_emitter = NULL;
    ParticleEmitter* laser_emitter = NULL;
    PointLight* engine_light = NULL;
    PointLight* laser_light = NULL;
    //Forward-facing cone light at the nose. The engine has had a ConeLight class all along
    //but nothing used one, so nothing uploaded it either - see Renderer::UploadLights.
    ConeLight* headlight = NULL;

    void StrafeBy(float force);
    void MoveForwardBy(float force);
    void MoveBackwardBy(float force);
    void TurnLeftBy(float angle);
    void TurnRightBy(float angle);
    void ShootLaser();
};

#endif