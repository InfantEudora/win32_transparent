#include "DozerCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("DozerCharacter",DEBUG_INFO);

DozerCharacter::DozerCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld):Object(){
    //Build the object.
    Object* body = assetmanager->GetObjectFromAsset("Body",this);
    body->name = "Body";
    Object* tracks = assetmanager->GetObjectFromAsset("Tracks");
    tracks->name = "Tracks";
    AttachChild(tracks);
    //Add a collider
    AddPhysics(physicsworld);

    if (physics){
        physics->AddBoxCollider(vec3(1.0,1.0,1.0),vec3(0,0.0,0),quat().identity());
        physics->SetStatic(false);
    }

    armobject = assetmanager->GetObjectFromAsset("Arm");
    armobject->SetPosition(vec3(0,1,0));
    AttachChild(armobject);
    armobject->name = "Arm";
    Object* exhaust = assetmanager->GetObjectFromAsset("Exhaust");
    AttachChild(exhaust);
    exhaust->name = "Exhaust";
}

DozerCharacter::~DozerCharacter(){

}

void DozerCharacter::MoveForward(){
    if (physics){
        physics->AddLocalForce(vec3(0,0,10));
    }
}

//Going to play a move forward animation based on whatever animation its in.
void DozerCharacter::MoveBackward(){
    if (physics){
        physics->AddLocalForce(vec3(0,0,-10));
    }
}


void DozerCharacter::TurnRight(){
    float delta = -0.05f;
    quat q = quat(vec3(0,1,0),delta);
    RotateBy(q);
}

void DozerCharacter::TurnLeft(){
    float delta = 0.05f;
    quat q = quat(vec3(0,1,0),delta);
    RotateBy(q);
}

void DozerCharacter::ArmUp(){
    float delta = -0.05f;
    quat q = quat(vec3(1,0,0),delta);
    armobject->RotateBy(q);
}

void DozerCharacter::ArmDown(){
    float delta = 0.05f;
    quat q = quat(vec3(1,0,0),delta);
    armobject->RotateBy(q);
}