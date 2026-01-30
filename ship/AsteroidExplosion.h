#ifndef _ASTEROIDEXPLOSION_H_
#define _ASTEROIDEXPLOSION_H_

#include "AssetManager.h"
#include "Physics.h"
#include "ParticleEmitter.h"
#include "ShipCollisionMasks.h"
#include "Asteroid.h"

class AsteroidExplosion : public Object{
public:
    AsteroidExplosion(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, RRandom* rrand);
    ~AsteroidExplosion();
    AssetManager* assetmanager = NULL;
    Scene* target_scene = NULL;
    RRandom* rrand = NULL;


    void StartExplosion();
    float rotation_speed = 0.01f;
    Asteroid* target_asteroid = NULL;
    bool f_explosion_started = false;
    bool f_fragments_created = false;
    ParticleEmitter* fragment_emitter = NULL;

    void UpdatePhysicsState() override;
};

#endif // _ASTEROIDEXPLOSION_H_