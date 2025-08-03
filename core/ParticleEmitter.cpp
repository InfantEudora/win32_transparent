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
    //TODO: Make random things easily accessible
    vec3 p = {};
    p.x = (rand()%10)/10.0;
    p.y = (rand()%10)/100.0;
    p.z = (rand()%10)/10.0;

    float s = (rand()%100) / 100.0;

    particle->SetPosition(GetWorldPosition() + p);
    particle->SetScale(0.5 + s);

    s = ((rand()%100) / 50.0) + 0.5;

    vec3 v = vec3(0,s,0);
    particle->GetPhysics()->SetStatic(false);
    particle->GetPhysics()->SetVelocity(v);

    float l = (rand()%100 / 100.0f )+ 0.5f;
    particle->lifetime = l;
}

void ParticleEmitter::EmitParticles(int amount){
    debug->Info("Would emit %i particles into target scene %p\n",amount,target_scene);
    if (rrand == NULL){

    }

    int amount_extra = amount;
    //Figure out if we can maybe reuse existing particles.
    for (Particle* particle:emitted_particles){
        if (particle->IsVisible() == false){
            particle->Show();
            SetParticle(particle);
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