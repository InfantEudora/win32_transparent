#include "ParticleEmitter.h"

#include "Debug.h"
static Debugger *debug = new Debugger("ParticleEmitter", DEBUG_INFO);

ParticleEmitter::ParticleEmitter(PhysicsWorld* world):Object(){
    AddPhysics(world);
}

ParticleEmitter::~ParticleEmitter(){

}

void ParticleEmitter::UpdatePhysicsState(){
    Object::UpdatePhysicsState();
}

void ParticleEmitter::AddParticleType(Particle* particle){
    particle_types.push_back(particle);
}

void ParticleEmitter::SetParticle(Particle* particle){
    vec3 p;
    p.x = rrand->GetFloat(-0.1,0.1);
    p.y = rrand->GetFloat(-0.1,0.1);
    p.z = rrand->GetFloat(-0.1,0.1);

    particle->SetPosition(GetWorldPosition() + p);
    particle->SetScale(rrand->GetFloat(0.5,1.5));

    vec3 v;
    v.x = rrand->GetFloat(-0.5,0.5);
    v.y = rrand->GetFloat(0.5,2.5);
    v.z = rrand->GetFloat(-0.5,0.5);
    particle->GetPhysics()->SetStatic(false);
    particle->GetPhysics()->SetVelocity(v);
    particle->GetPhysics()->SetActive(true);
    particle->lifetime = rrand->GetFloat(0.5,2.0);
}

void ParticleEmitter::EmitParticles(int amount){
    //debug->Info("Would emit %i particles into target scene %p\n",amount,target_scene);
    if (rrand == NULL){
        debug->Fatal("No RRandom was supplied to particle emitter. And it really wants one.\n");
    }

    int amount_extra = amount;
    //Figure out if we can maybe reuse existing particles.
    for (Particle* particle:emitted_particles){
        if (particle->IsVisible() == false){
            particle->Show();
            particle->UpdatePhysicsState();
            SetParticle(particle);
            //Extra step...

            amount_extra--;
        }

        if (amount_extra == 0){
            return;
        }
    }

    //Spawn some new ones.
    if (particle_types.size() > 0){
        Particle* ref_particle = particle_types.at(0);
        for (int i =0;i<amount_extra;i++){


            Particle* particle = new Particle(ref_particle);
            SetParticle(particle);

            target_scene->AddObject(particle);
            emitted_particles.push_back(particle);
        }
    }

}