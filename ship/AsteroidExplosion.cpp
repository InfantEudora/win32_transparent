#include "AsteroidExplosion.h"
#include "Debug.h"
static Debugger* debug = new Debugger("AsteroidExplosion",DEBUG_INFO);

AsteroidExplosion::AsteroidExplosion(AssetManager* assetmanager_in, PhysicsWorld* physicsworld_in, Scene* target_scene_in, RRandom* rrand_in):Object(){
    name = "AsteroidExplosion";
    assetmanager = assetmanager_in;
    target_scene = target_scene_in;
    rrand = rrand_in;

    if (!assetmanager){
        debug->Fatal("No assetmanager given!\n");
        return;
    }
    assetmanager->GetObjectFromAsset("explosion",this);
    AddPhysics(physicsworld_in);
    if (physics){
        physics->SetStatic(false);
        physics->SetGravityEnabled(false);
    }

    //Particle emitter for bits of asteroid
    fragment_emitter = new ParticleEmitter(physicsworld_in);
    fragment_emitter->name = "Fragment Emitter";
    AttachChild(fragment_emitter);
    fragment_emitter->target_scene = target_scene;
    fragment_emitter->SetRandomGenerator(rrand);

    Particle* asteroid_particle = new Particle(physicsworld_in);
    assetmanager->GetObjectFromAsset("asteroid.001",asteroid_particle);
    asteroid_particle->name = "asteroid_particle";
    fragment_emitter->emission_properties = {
        .emission_rate         = 10.0f,
        .emission_direction    = vec3(1,0,0),
        .emission_spread       = 360.0f,
        .particle_size_min     = 0.2f,
        .particle_size_max     = 0.5f,
        .particle_lifetime_min = 0.5f,
        .particle_lifetime_max = 1.5f,
        .emission_speed_min    = 5.0f,
        .emission_speed_max    = 10.0f,
    };
    fragment_emitter->AddParticleType(asteroid_particle);
}

void AsteroidExplosion::StartExplosion(){
    //We take the position of the target asteroid
    if (!target_asteroid){
        return;
    }
    SetPosition(target_asteroid->GetPosition());
    SetScale(target_asteroid->GetScale() * 1.25f);
    f_explosion_started = true;
    UpdatePhysicsState();
}

void AsteroidExplosion::UpdatePhysicsState(){
    if (!f_explosion_started){
        return Object::UpdatePhysicsState();
    }
    //Modify the morph target.
    //Maybe we could misuse object animation for this... well see
    morph_factors[0] = clamp(morph_factors[0] + 0.04f,0,1);
    //Create a bunch of tiny particles
    if (morph_factors[0] < 0.8f){
        fragment_emitter->EmitParticles(1);
    }

    if (target_asteroid && (morph_factors[0] == 1.0f)){
        SetPosition(target_asteroid->GetPosition());
        target_asteroid->Destroy();
        vec3 vel = target_asteroid->GetVelocity();
        SetVelocity(vel);
        target_asteroid = NULL;
    }
    if (morph_factors[0] == 1.0f){
        vec3 s = GetScale();
        if (!f_fragments_created){
            //Based on size we decide if we create some fragments.
            debug->Info("Asteroid size = %.1f\n",s.x);
            int num_fragments = s.x / 1.2f;
            float fragment_size = s.x / num_fragments;

            if (num_fragments > 1){
                debug->Info("Creating %i fragments of size %.1f\n",num_fragments,fragment_size);
                for (int i = 0;i<num_fragments;i++){
                    Asteroid* asteroid = new Asteroid(assetmanager,target_scene->physics_world,target_scene,rrand);
                    if (asteroid){
                        asteroid->SetPosition(GetPosition() + vec3(rrand->GetFloat(-0.5,0.5),0,rrand->GetFloat(-0.5,0.5)) );
                        asteroid->SetScale(vec3(rrand->GetFloat(fragment_size-0.4f,fragment_size)));
                        asteroid->GetPhysics()->SetAngularVelocity(vec3(rrand->GetFloat(-0.5,0.5),rrand->GetFloat(-0.2,0.2),rrand->GetFloat(-0.5,0.5)));
                        asteroid->GetPhysics()->SetVelocity(vec3(rrand->GetFloat(-2,2),0,rrand->GetFloat(-2,2)));
                        asteroid->UpdatePhysicsState();
                        target_scene->AddObject(asteroid);
                    }
                }
            }else{
                debug->Info("Asteroid will not fragment further.\n");
            }
            f_fragments_created = true;
        }
        //We reduce the size
        SetScale(s*0.95f);
        if (s.length() < 0.1f){
            Destroy();
        }

    }
    Object::UpdatePhysicsState();
}

AsteroidExplosion::~AsteroidExplosion(){

}