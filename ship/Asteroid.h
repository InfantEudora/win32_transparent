#ifndef _ASTEROID_H_
#define _ASTEROID_H_

#include "AssetManager.h"
#include "Physics.h"
#include "ParticleEmitter.h"
#include "ShipCollisionMasks.h"

class Asteroid : public Object{
public:
    Asteroid(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, RRandom* rrand);
    ~Asteroid();
    float rotation_speed = 0.01f;
    float health = 10.0f;
};

#endif // _ASTEROID_H_