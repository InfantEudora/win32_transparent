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
        physics->AddBoxCollider(vec3(1.0,1.0,1.0),vec3(0,1,0),quat().identity());
        physics->SetStatic(false);
    }

    armobject = assetmanager->GetObjectFromAsset("Arm");
    armobject->SetPosition(vec3(0,1,0));
    //AttachChild(armobject);
    armobject->name = "Arm";
    //armobject->AddPhysics(physicsworld);
    //if (Physics* p = armobject->GetPhysics()){
    //    p->AddBoxCollider(vec3(1.0,1.0,1.0),vec3(0,1,0),quat().identity());
    //    p->SetStatic(false);
    //}
    Object* exhaust = assetmanager->GetObjectFromAsset("Exhaust");
    AttachChild(exhaust);
    exhaust->name = "Exhaust";
}

DozerCharacter::~DozerCharacter(){

}

void DozerCharacter::MoveForward(){
    if (physics){
        vec3 lf = vec3(0,0,100);
        physics->AddLocalForce(lf);
        vec3 f = physics->GetForce();
        debug->Info("Force applied: %.1f, %.1f, %.1f Newton\n",f.x,f.y,f.z);

        quat q = GetRotation();
        vec3 rf = q * lf;
        debug->Info("Result manual: %.1f, %.1f, %.1f Newton\n",rf.x,rf.y,rf.z);

        vec3 fwd = GetForward();
        debug->Info("Forward : %.3f, %.3f, %.3f \n",fwd.x,fwd.y,fwd.z);
    }
}

//Going to play a move forward animation based on whatever animation its in.
void DozerCharacter::MoveBackward(){
    if (physics){
        physics->AddLocalForce(vec3(0,0,-100));

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