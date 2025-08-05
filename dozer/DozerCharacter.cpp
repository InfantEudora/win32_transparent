#include "DozerCharacter.h"

#include "Debug.h"
static Debugger* debug = new Debugger("DozerCharacter",DEBUG_INFO);

DozerCharacter::DozerCharacter(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene,RRandom* rrand):Object(){
    //Build the object.
    name = "Dozer";
    body = assetmanager->GetObjectFromAsset("Body");
    body->name = "Body";
    AttachChild(body);
    tracks = assetmanager->GetObjectFromAsset("Tracks");
    tracks->name = "Tracks";
    AttachChild(tracks);
    //Add a collider
    AddPhysics(physicsworld);

    if (physics){
        physics->AddBoxCollider(vec3(0.8,0.4,0.8),vec3(0,0.5,0),quat().identity());

        physics->AddBoxCollider(vec3(0.5,0.5,0.5),vec3(0,1.5,-0.5),quat().identity());
        quat r = quat(vec3(0,0,1),TYPE_PI/2);
        physics->AddCapsuleCollider(0.25,1,vec3(0,0.25,0.8),r);
        physics->AddCapsuleCollider(0.25,1,vec3(0,0.25,-0.8),r);
        physics->SetStatic(false);
        physics->SetGravityEnabled(true);
        physics->body->rigidbody->setLinearDamping(0.5);
        physics->body->rigidbody->setMass(10);
    }

    armobject = assetmanager->GetObjectFromAsset("Arm");
    armobject->SetPosition(vec3(0,1,0));
    armobject->name = "Arm";
    armobject->AddPhysics(physicsworld);
    if (Physics* p = armobject->GetPhysics()){
        p->AddBoxCollider(vec3(1.0,0.7,0.2),vec3(0,-.2,1.5),quat().identity());
        p->SetGravityEnabled(true);
        p->SetStatic(false);
        armobject->SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
        armobject->SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS|COLLISION_CATEGORY_FLOOR);
        armobject->GetPhysics()->body->rigidbody->setMass(0.5);
    }

    rp3d::RigidBody* body1 = GetRigidBody();
    rp3d::RigidBody* body2 = armobject->GetRigidBody();

    //We attach it with a hinge joint
    // Anchor point in world-space
    const rp3d::Vector3 anchorPoint(0.0, 1.0, 0.0);

    // Hinge rotation axis in world-space
    const rp3d::Vector3 axis(1.0, 0.0, 0.0);

    // Create the joint info object
    rp3d::HingeJointInfo jointInfo = rp3d::HingeJointInfo(body1, body2, anchorPoint, axis);

    // Enable the motor of the joint
    jointInfo.isMotorEnabled = false;

    // Motor angular speed
    jointInfo.motorSpeed = 0;

    // Maximum allowed torque
    jointInfo.maxMotorTorque = 100.0;

    jointInfo.isLimitEnabled = true;

    // Minimum limit angle
    jointInfo.minAngleLimit = -TYPE_PI / 4.0;

    // Maximum limit angle
    jointInfo.maxAngleLimit = +0.10;

    joint = dynamic_cast<rp3d::HingeJoint*>(physicsworld->rp_world->createJoint(jointInfo));
    //physicsworld->rp_world->destroyJoint(joint);
    //joint = NULL;

    exhaust = assetmanager->GetObjectFromAsset("Exhaust");
    AttachChild(exhaust);
    exhaust->name = "Exhaust";

    //The exhaust gets a particle emitter that will be able to emit smoke particles
    smoke_emitter = new ParticleEmitter(physicsworld);
    smoke_emitter->name = "Smoke Emitter";
    smoke_emitter->target_scene = target_scene;
    smoke_emitter->SetRandomGenerator(rrand);
    smoke_emitter->SetPosition(vec3(0.3,2.5,-0.75));
    Particle* white_smoke = new Particle(physicsworld);
    assetmanager->GetObjectFromAsset("SmokeWhite",white_smoke);
    vec3 sz = white_smoke->GetMesh()->GetExtents();
    white_smoke->GetPhysics()->AddSphereCollider(sz.length()/2.0,vec3(0,0,0),quat().identity(),0.1f);
    white_smoke->SetCollideWithMaskBits(COLLISION_CATEGORY_FLOOR);
    white_smoke->SetCollisionCategoryBits(COLLISION_CATEGORY_SMOKE);
    white_smoke->GetPhysics()->SetActive(false);
    smoke_emitter->AddParticleType(white_smoke);
    AttachChild(smoke_emitter);

    SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
    SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS|COLLISION_CATEGORY_FLOOR);
}

DozerCharacter::~DozerCharacter(){

}

void DozerCharacter::MoveForward(){
    if ((engine_state == ENGINE_STOPPED) || (engine_state == ENGINE_STALLING)){
        return;
    }
    if (physics){
        vec3 lf = vec3(0,0,100);
        quat q = GetRotation();
        vec3 rf = q * lf;
        physics->AddLocalForce(lf);
        //physics->AddWorldForceAt(rf,GetPosition());


        vec3 f = physics->GetForce();
        //debug->Info("Force applied: %.1f, %.1f, %.1f Newton\n",f.x,f.y,f.z);
        //debug->Info("Result manual: %.1f, %.1f, %.1f Newton\n",rf.x,rf.y,rf.z);

        //vec3 fwd = GetForward();
        //debug->Info("Forward : %.3f, %.3f, %.3f \n",fwd.x,fwd.y,fwd.z);
        //float motorspeed = joint->getMotorSpeed();
        //debug->Info("Motor Speed : %.3f\n",motorspeed);

        vec3 vel = GetVelocity();
        //debug->Info("Force applied       : %.1f, %.1f, %.1f Newton\n",f.x,f.y,f.z);

        SetVelocity(vel + rf*0.0025);
        //debug->Info("Vehicle Velocity    : %.3f (%.1f, %.1f, %.1f)\n",vel.length(),vel.x,vel.y,vel.z);
    }
    throttle = true;
    belt_tension+= 0.1f;
    belt_tension = clamp(belt_tension,0.0,1.0);

    if (soundsystem->FinishedPlaying("engine_revup")){
        soundsystem->Play("engine_revup");
    }

    smoke_emitter->EmitParticles(8);
}

//Going to play a move forward animation based on whatever animation its in.
void DozerCharacter::MoveBackward(){
    if ((engine_state == ENGINE_STOPPED) || (engine_state == ENGINE_STALLING)){
        return;
    }
    if (physics){
        vec3 lf = vec3(0,0,-100);
        vec3 vel = GetVelocity();
        quat q = GetRotation();
        vec3 rf = q * lf;
        SetVelocity(vel + rf*0.00125);
        physics->AddLocalForce(vec3(0,0,-100));
    }
}

void DozerCharacter::UpdatePhysicsState(){
    if (!throttle && belt_tension > 0){
        belt_tension = clamp(belt_tension-0.1f,0.0f,1.0f);
    }
    throttle = false;
    tracks->morph_factors[0] = belt_tension;

    if (engine_state == ENGINE_STARTING){
        float r = (rand()%100) / 100.0f;
        exhaust->morph_factors[0] = r;
        //Figure out if the start engine sound has finished.
        int stopped = soundsystem->FinishedPlaying("engine_start");
        if (stopped){
            debug->Info("Engine start has stopped\n");
            engine_state = ENGINE_IDLE;
            soundsystem->Play("engine_idle",true);
        }
        r -= 0.5f;
        body->SetScale(1.0 + (r/50.0f));
        //smoke_emitter->EmitParticles(1);

    }else if (engine_state == ENGINE_IDLE){
        float r = (rand()%100) / 300.0f;
        exhaust->morph_factors[0] = r;
        r = ((rand()%100) - 50) / 10000.0f;
        body->SetScale(1.0 + r);
    }else if (engine_state == ENGINE_STALLING){
        float r = (rand()%100) / 200.0f;
        exhaust->morph_factors[0] = r;
        int stopped = soundsystem->FinishedPlaying("engine_stop");
        if (stopped){
            debug->Info("Engine stop has stopped\n");
            engine_state = ENGINE_STOPPED;
        }
    }else if (engine_state == ENGINE_STOPPED){
        exhaust->morph_factors[0] = 0;
    }

    if (arm_moving){
        arm_movement = clamp(arm_movement-0.1f,0.0f,1.0f);
        if (arm_movement == 0.0f){
            arm_moving = false;
            soundsystem->Pause("arm_up");
        }
        if (arm_movement < 0.1){
            arm_torque = clamp(arm_torque - 0.5,0,arm_torque_max);
        }
    }



    Object::UpdatePhysicsState();
}


void DozerCharacter::TurnRight(){
    if ((engine_state == ENGINE_STOPPED) || (engine_state == ENGINE_STALLING)){
        return;
    }
    float delta = -0.05f;
    quat q = quat(vec3(0,1,0),delta);
    RotateBy(q);
}

void DozerCharacter::TurnLeft(){
    if ((engine_state == ENGINE_STOPPED) || (engine_state == ENGINE_STALLING)){
        return;
    }
    float delta = 0.05f;
    quat q = quat(vec3(0,1,0),delta);
    RotateBy(q);
}

void DozerCharacter::ArmUp(){
    if ((engine_state == ENGINE_STOPPED) || (engine_state == ENGINE_STALLING)){
        return;
    }

    float joint_angle = 0;
    if (joint){
        joint_angle = joint->getAngle();
    }


    //float delta = -0.05f;
    //quat q = quat(vec3(1,0,0),delta);
    //armobject->RotateBy(q);
    //joint->enableMotor(false);
    //joint->setMotorSpeed(0.01);

    armobject->GetPhysics()->AddLocalTorque(vec3(-arm_torque,0,0));
    if (soundsystem && !arm_moving){
        soundsystem->Rewind("arm_up");
        soundsystem->Play("arm_up");
        arm_moving = true;
        arm_torque = 0;
    }
    arm_movement = 0.5f;
    if (joint_angle > -0.75){
        arm_torque = clamp(arm_torque+2,0,arm_torque_max);
    }




    debug->Info("Joint Angle: %.2f. Arm Torque: %.1f Nm\n",joint_angle,arm_torque);
}

void DozerCharacter::ArmDown(){
    if ((engine_state == ENGINE_STOPPED) || (engine_state == ENGINE_STALLING)){
        return;
    }
    //float delta = 0.05f;
    //quat q = quat(vec3(1,0,0),delta);
    //armobject->RotateBy(q);
    //joint->enableMotor(false);
    armobject->GetPhysics()->AddLocalTorque(vec3(20,0,0));

}

bool DozerCharacter::IsEngineRunning(){
    if (engine_state == ENGINE_IDLE){
        return true;
    }
    if (engine_state == ENGINE_RUNNING){
        return true;
    }
    return false;
}

void DozerCharacter::StartStopEngine(bool start){
    if (!soundsystem){
        return;
    }

    if (start && (engine_state == ENGINE_STOPPED)){
        soundsystem->Play("engine_start");
        engine_state = ENGINE_STARTING;
        smoke_emitter->EmitParticles(5);
    }else if (!start && (engine_state == ENGINE_IDLE)){
        soundsystem->Play("engine_stop");
        soundsystem->Pause("engine_idle");
        engine_state = ENGINE_STALLING;
    }else{
        soundsystem->Play("engine_crank");
    }
    //Some sound for starting while running
}