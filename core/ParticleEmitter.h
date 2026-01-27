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

    //Particle emission properties
    struct{
        float emission_rate         = 10.0f;      // Particles per second
        vec3  emission_direction    = vec3(0,1,0);
        float emission_spread       = 15.0f;    // Degrees of spread from the emission direction
        float particle_size_min     = 0.1f;
        float particle_size_max     = 1.0f;
        float particle_lifetime_min = 1.0f;
        float particle_lifetime_max = 3.0f;
        float emission_speed_min    = 0.1f;
        float emission_speed_max    = 1.0f;
    } emission_properties;

    private:
    RRandom* rrand = NULL;
    void SetParticle(Particle* particle);
};

#endif
