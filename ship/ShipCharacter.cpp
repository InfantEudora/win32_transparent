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
    assetmanager->GetObjectFromAsset("ship_import",this);

    AddPhysics(physicsworld);

    if (physics){
        physics->AddBoxCollider(vec3(0.8,0.4,0.8),vec3(0,0.5,0),quat().identity());


        quat r = quat(vec3(0,0,1),TYPE_PI/2);
        physics->AddCapsuleCollider(0.25,1,vec3(0,0.25,0.8),r);

        physics->SetStatic(false);
        physics->SetGravityEnabled(false);
        physics->body->rigidbody->setLinearDamping(0.5);
        physics->body->rigidbody->setUserData(this);
        physics->body->rigidbody->setIsAllowedToSleep(false);
        physics->body->rigidbody->updateMassPropertiesFromColliders();
        physics->body->rigidbody->setMass(10);
    }

    exhaust_emitter = new ParticleEmitter(physicsworld);
    exhaust_emitter->name = "Exhaust Emitter";
    exhaust_emitter->target_scene = target_scene;
    exhaust_emitter->SetRandomGenerator(rrand);
    exhaust_emitter->SetPosition(vec3(0.0,0.5,-1.0));
    AttachChild(exhaust_emitter);

}

ShipCharacter::~ShipCharacter(){

}

void ShipCharacter::MoveForward(){
    if (physics){
        vec3 lf = vec3(0,0,100);
        quat q = GetRotation();
        vec3 rf = q * lf;
        physics->AddLocalForce(lf);

        //We also want a force that slightly lifts the ship up when moving forward


        vec3 wp = GetPosition(STATE_ACCESS_PHYSICS);
        wp += q * vec3(0,0,-1);
        vec3 lift = vec3(0,-5,0);
        lift = q * lift;
        physics->AddWorldForceAt(lift,wp);




        vec3 vel = GetVelocity();
        //debug->Info("Force applied       : %.1f, %.1f, %.1f Newton\n",f.x,f.y,f.z);

        //SetVelocity(vel + rf*0.0025);

    }


    exhaust_emitter->EmitParticles(8);
}

//Going to play a move forward animation based on whatever animation its in.
void ShipCharacter::MoveBackward(){

    if (physics){
        vec3 lf = vec3(0,0,-100);
        vec3 vel = GetVelocity();
        quat q = GetRotation();
        vec3 rf = q * lf;
        SetVelocity(vel + rf*0.00125);
        physics->AddLocalForce(vec3(0,0,-100));
    }
}

void ShipCharacter::UpdatePhysicsState(){
    Object::UpdatePhysicsState();
}

void ShipCharacter::TurnRight(){

    float delta = -0.05f;
    quat q = quat(vec3(0,1,0),delta);
    RotateBy(q);
}

void ShipCharacter::TurnLeft(){
    float delta = 0.05f;
    quat q = quat(vec3(0,1,0),delta);
    RotateBy(q);
}
