#include "ShipCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("ShipCharacter",DEBUG_INFO);

ShipCharacter::ShipCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene,RRandom* rrand):Object(){
    //Build the object.
    name = "Ship";

    if (!assetmanager){
        debug->Fatal("No assetmanager given!\n");
        return;
    }
    assetmanager->GetObjectFromAsset("ship.002",this);

    AddPhysics(physicsworld);

    if (physics){
        physics->AddBoxCollider(vec3(0.5,0.4,0.5),vec3(0,0.0,0),quat().identity());


        quat r = quat(vec3(1,0,0),TYPE_PI/2);
        physics->AddCapsuleCollider(0.20,1,vec3(0,0.0,0.6),r);

        physics->SetStatic(false);
        physics->SetGravityEnabled(false);
        physics->body->rigidbody->setLinearDamping(0.5);
        physics->body->rigidbody->setUserData(this);
        physics->body->rigidbody->setIsAllowedToSleep(false);
        physics->body->rigidbody->updateMassPropertiesFromColliders();
        physics->body->rigidbody->setMass(10);
    }

    SetCollideWithMaskBits(COLLISION_CATEGORY_ASTEROID);
    SetCollisionCategoryBits(COLLISION_CATEGORY_SHIP);

    exhaust_emitter = new ParticleEmitter(physicsworld);
    exhaust_emitter->name = "Exhaust Emitter";
    exhaust_emitter->target_scene = target_scene;
    exhaust_emitter->SetRandomGenerator(rrand);
    exhaust_emitter->SetPosition(vec3(0.0,0.0,-1.0));
    AttachChild(exhaust_emitter);

    laser_emitter = new ParticleEmitter(physicsworld);
    laser_emitter->name = "Laser Emitter";
    laser_emitter->target_scene = target_scene;
    laser_emitter->SetRandomGenerator(rrand);
    laser_emitter->SetPosition(vec3(0.0,0.0,2.0));
    AttachChild(laser_emitter);

    engine_light = new PointLight();
    engine_light->name = "Engine Light";
    engine_light->color = vec3(0.0,0.5,1.0);
    engine_light->SetPosition(vec3(0.0,0.1,-1.0));
    engine_light->brightness = 5.0f;
    AttachChild(engine_light);

    laser_light = new PointLight();
    laser_light->name = "Laser Light";
    laser_light->color = vec3(1.0,0.2,0.0);
    laser_light->brightness = 5.0f;

}

ShipCharacter::~ShipCharacter(){

}

void ShipCharacter::StrafeBy(float force){
    if (physics){
        vec3 f = vec3(force,0,0);
        vec3 wp = GetPosition(STATE_ACCESS_PHYSICS);
        vec3 cm = GetCenterofMass();
        quat q = GetRotation();
        wp += q * cm;

        f = q * f;
        physics->AddWorldForceAt(f,wp);
    }
}

void ShipCharacter::MoveForwardBy(float force){
    if (physics){
        vec3 lf = vec3(0,0,force);
        physics->AddLocalForce(lf);

        //We also want a force that slightly lifts the ship up when moving forward
        quat q = GetRotation();
        vec3 wp = GetPosition(STATE_ACCESS_PHYSICS);
        wp += q * vec3(0,0,-1);
        vec3 lift = vec3(0,-force/50,0);
        lift = q * lift;
        physics->AddWorldForceAt(lift,wp);
        vec3 vel = GetVelocity();
        //debug->Info("Force applied       : %.1f, %.1f, %.1f Newton\n",f.x,f.y,f.z);

        forward_thrust = force;
    }
    exhaust_emitter->EmitParticles(8);
}

//Going to play a move forward animation based on whatever animation its in.
void ShipCharacter::MoveBackwardBy(float force){

    if (physics){
        vec3 lf = vec3(0,0,-force);
        vec3 vel = GetVelocity();
        quat q = GetRotation();
        vec3 rf = q * lf;
        //SetVelocity(vel + rf*0.00125);
        physics->AddLocalForce(vec3(0,0,-force));
        forward_thrust = -force;
    }
}

void ShipCharacter::UpdatePhysicsState(){
    if (laser_emitter && laser_light){
        //We track where the center of all particles are, so we can place a light there.
        vec3 center = vec3(0,0,0);
        int count = 0;
        for (size_t i = 0; i < laser_emitter->emitted_particles.size(); i++){
            if (!laser_emitter->emitted_particles.at(i)->IsVisible()){
                continue;
            }
            center += laser_emitter->emitted_particles.at(i)->GetPosition(STATE_ACCESS_PHYSICS);
            count++;
        }
        if (count > 0){
            center = center / (float)count;
            laser_light->SetPosition(center);
            laser_light->brightness = 1.0f * count;
            if (laser_light->brightness > 10.0f){
                laser_light->brightness = 10.0f;
            }
        }
        if (count == 0){
            laser_light->Hide();
        }else{
            laser_light->Show();
        }
    }


    Object::UpdatePhysicsState();
}

void ShipCharacter::TurnRightBy(float angle){
    quat q = quat(vec3(0,1,0),-angle);
    RotateBy(q);
}

void ShipCharacter::TurnLeftBy(float angle){
    quat q = quat(vec3(0,1,0),angle);
    RotateBy(q);
}

void ShipCharacter::ShootLaser(){
    if (laser_emitter){
        laser_emitter->EmitParticles(1);

    }
}