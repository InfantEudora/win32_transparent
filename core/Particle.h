#ifndef _PARTICLE_OBJECT_H_
#define _PARTICLE_OBJECT_H_

class Particle;
#include "Object.h"

//Particles are just objects with a lifetime and a managed by a particle emitter.

class Particle : public Object{
public:
    Particle(PhysicsWorld* world);
    Particle(Particle* particle);
    ~Particle();

    void UpdatePhysicsState() override;

    float lifetime = 1;
};

#endif
