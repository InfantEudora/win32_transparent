#include "ParticleEmitter.h"

#include "Debug.h"
static Debugger *debug = new Debugger("ParticleEmitter", DEBUG_INFO);

ParticleEmitter::ParticleEmitter(PhysicsWorld* world):Object(){
    AddPhysics(world);
}

ParticleEmitter::~ParticleEmitter(){
    for (Particle* particle:particle_types){
        delete particle;
    }
    //All emitted particles will need to be destroyed
    for (Particle* particle:emitted_particles){
        particle->Destroy();
    }
}

void ParticleEmitter::UpdatePhysicsState(){
    std::vector<Particle*>::iterator it = emitted_particles.begin();
    for ( ; it != emitted_particles.end(); ) {
        Particle* particle = *it;
        if (particle->IsVisible(STATE_ACCESS_PHYSICS) == false){
            //Particle will also destroy itself next tick
            particle->Destroy();
            it = emitted_particles.erase(it);
        }else{
            ++it;
        }
    }
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

    //STATE_ACCESS_PHYSICS, not the default STATE_ACCESS_RENDERER: GetWorldPosition's renderer
    //branch reads world_transform_scale_matrix, which UpdateState() only refreshes once per
    //frame elsewhere - a frame behind any SetPosition() this emitter's caller just made (e.g.
    //ApplicationTank repositioning a one-shot impact emitter right before EmitParticles), so
    //particles would spawn at where the emitter WAS, not where it was just moved to. Rotation
    //below already reads state_physics via GetWorldRotation()/GetRotation() - this just makes
    //position consistent with that same always-fresh source instead of a stale copy.
    particle->SetPosition(GetWorldPosition(STATE_ACCESS_PHYSICS) + p);
    particle->SetScale(rrand->GetFloat(emission_properties.particle_size_min,emission_properties.particle_size_max));

    vec3 v = emission_properties.emission_direction * rrand->GetFloat(emission_properties.emission_speed_min,emission_properties.emission_speed_max);
    //We need to get a vector within the spread angle.
    float spread_rad = emission_properties.emission_spread * (TYPE_PI / 180.0f);
    float angle_x = rrand->GetFloat(-spread_rad/2,spread_rad/2);
    float angle_y = rrand->GetFloat(-spread_rad/2,spread_rad/2);
    float angle_z = rrand->GetFloat(-spread_rad/2,spread_rad/2);
    quat qx = quat(vec3(1,0,0),angle_x);
    quat qy = quat(vec3(0,1,0),angle_y);
    quat qz = quat(vec3(0,0,1),angle_z);
    quat q = qx * qy * qz;
    v = q * v;
    //Rotate speed by our parent's rotation too
    if (parent){
        quat parent_rot = parent->GetWorldRotation();
        v = parent_rot * v;
        particle->SetRotation(parent_rot);
    }

    //Add our parent's velocity too
    if (parent && parent->GetPhysics()){
        v = v + parent->GetPhysics()->GetVelocity();
    }



    particle->GetPhysics()->SetStatic(false);
    particle->GetPhysics()->SetVelocity(v);
    particle->GetPhysics()->SetActive(true);
    particle->lifetime = rrand->GetFloat(emission_properties.particle_lifetime_min,emission_properties.particle_lifetime_max);
    particle->UpdatePhysicsState();
}

void ParticleEmitter::EmitParticles(int amount){
    //debug->Info("Would emit %i particles into target scene %p\n",amount,target_scene);
    if (rrand == NULL){
        debug->Fatal("No RRandom was supplied to particle emitter. And it really wants one.\n");
    }

    int amount_extra = amount;
    //Figure out if we can maybe reuse existing particles.
    for (Particle* particle:emitted_particles){
        if (particle->IsVisible(STATE_ACCESS_PHYSICS) == false){
            SetParticle(particle);
            particle->Show();
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