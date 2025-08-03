#ifndef _PARTICLE_EMITTER_H_
#define _PARTICLE_EMITTER_H_

class ParticleEmitter;
#include "Particle.h"
#include "Scene.h"
#include "RRandom.h"

class ParticleEmitter : public Object{
public:
    ParticleEmitter(PhysicsWorld* world);
    ~ParticleEmitter();

    void UpdatePhysicsState() override;
    void AddParticleType(Particle* particle);
    void EmitParticles(int amount);
    void SetRandomGenerator(RRandom* gen){rrand = gen;}

    std::vector<Particle*>particle_types;    // Objects that it is allowed to instantiate
    std::vector<Particle*>emitted_particles;
    Scene* target_scene = NULL;             // Scene where the particles should be emitted in.

    float particle_initial_size = 0.5f;


    private:
    RRandom* rrand = NULL;
    void SetParticle(Particle* particle);
};

#endif
