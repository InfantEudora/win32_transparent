#include "Mechanisms.h"

#include "Debug.h"
#include "type_helpers.h"

#include <math.h>

static Debugger* debug = new Debugger("PinMechanisms",DEBUG_INFO);

//A static body with no collider, for a joint to hang off. Lights are Objects without meshes and
//so is this; it never renders and never collides, it just gives rp3d a body of infinite mass at a
//known place. The joint's own isCollisionEnabled=false makes the missing collider moot anyway.
static Object* MakeAnchor(const char* name, const vec3& position, PhysicsWorld* world, Scene* scene){
    Object* anchor = new Object();
    anchor->name = name;
    anchor->SetPosition(position);
    anchor->SetPickability(false);
    anchor->AddPhysics(world);      //AddPhysics leaves a body STATIC, which is what this wants
    scene->AddObject(anchor);
    return anchor;
}

//The same three lines every dynamic body here needs and rp3d does not default to.
static void PrepareDynamicBody(Physics* p, float mass){
    p->SetStatic(false);
    //On a hinge or a slider gravity only loads the joint with a force it cancels again every
    //tick - and this table's gravity is tilted, which would drag every bat down-table for ever.
    p->SetGravityEnabled(false);
    p->SetMass(mass);
    p->SetLinearDamping(0.0f);
    p->SetAngularDamping(0.0f);
    if (p->body && p->body->rigidbody){
        //A bat at rest would otherwise fall asleep, and a sleeping body ignores its motor.
        p->body->rigidbody->setIsAllowedToSleep(false);
    }
}

//--- Flipper ------------------------------------------------------------------------------------

Flipper::Flipper(const char* flipper_name, AssetManager* assets, const char* part, Mesh* fallback,
                 int material, PhysicsWorld* world, Scene* scene,
                 const vec3& pivot_position, float bat_length, float rest_deg, float up_deg,
                 bool mirrored):Object(){
    name          = flipper_name;
    physics_world = world;
    pivot         = pivot_position;
    length        = bat_length;
    rest_degrees  = rest_deg;
    up_degrees    = up_deg;
    f_mirrored    = mirrored;
    direction     = mirrored ? -1.0f : 1.0f;

    //The mesh: the modelled bat if the file has one, the slab if not. GetObjectFromAsset with a
    //target fills THIS object rather than making a new one, which is how HingedDoor does it too.
    if (assets && part && assets->GetAsset(part)){
        assets->GetObjectFromAsset(part,this);
    }else if (fallback){
        SetMesh(fallback);
    }
    SetMaterialSlot(0,material);

    /*
        A positive angle about +Y takes +X toward -Z (up-table). The left bat at rest is turned by
        rest_deg; the mirrored bat is the SAME bat turned through 180 - rest_deg, so its x flips
        and its z does not, and both tips point down-table at rest. Set before AddPhysics, which
        seeds the body from this transform - a rotation set afterwards is thrown away on the first
        sync (see Object::AddPhysics).
    */
    const float rest_yaw = mirrored ? (180.0f - rest_deg) : rest_deg;
    rest_rotation = quat(vec3(0,1,0),toradians(rest_yaw));
    SetPosition(vec3(pivot.x,0.06f,pivot.z));
    SetRotation(rest_rotation);
    SetPickability(false);

    if (!world || !scene){
        debug->Err("Flipper %s: no world or scene\n",flipper_name);
        return;
    }
    AddPhysics(world);
    Physics* p = GetPhysics();
    if (!p){
        return;
    }
    /*
        THE COLLIDER: a box for the body of the bat and a capsule at each end to round it, all in
        the bat's own frame (pivot at the origin, +X along the bat, y from 0 to 0.20).

        The capsules stand VERTICAL with their cylinder part spanning the bat's height, so the
        ball - whose centre is 0.135 above the deck, 0.075 above the bat's underside - only ever
        meets the straight middle; the rounded caps are below the deck and above the ball. A
        flat-ended box alone would give the tip a corner, and a corner throws a ball off the tip
        in a direction that depends on which micro-facet it found.
    */
    const float half_h = PIN_FLIPPER_THICKNESS * 0.5f;
    const float root_r = PIN_FLIPPER_WIDTH * 0.5f;
    const float tip_r  = PIN_FLIPPER_TIP_RADIUS;
    const float body_l = length;
    const quat  upright = quat().identity();
    p->AddBoxCollider(vec3(body_l * 0.5f,half_h,(root_r + tip_r) * 0.5f),
                      vec3(body_l * 0.5f,half_h,0.0f),upright,1.0f);
    p->SetBounciness(PIN_FLIPPER_BOUNCINESS);
    p->SetFrictionCoefficient(PIN_FLIPPER_FRICTION);
    p->AddCapsuleCollider(root_r,PIN_FLIPPER_THICKNESS,vec3(0.0f,half_h,0.0f),upright,1.0f);
    p->SetBounciness(PIN_FLIPPER_BOUNCINESS);
    p->SetFrictionCoefficient(PIN_FLIPPER_FRICTION);
    p->AddCapsuleCollider(tip_r,PIN_FLIPPER_THICKNESS,vec3(body_l,half_h,0.0f),upright,1.0f);
    p->SetBounciness(PIN_FLIPPER_BOUNCINESS);
    p->SetFrictionCoefficient(PIN_FLIPPER_FRICTION);
    PrepareDynamicBody(p,PIN_FLIPPER_MASS);
    SetCollisionCategoryBits(PIN_CAT_FLIPPER);
    SetCollideWithMaskBits(PIN_CAT_BALL);
    scene->AddObject(this);

    char anchor_name[64];
    snprintf(anchor_name,sizeof(anchor_name),"%s_anchor",flipper_name);
    anchor = MakeAnchor(anchor_name,pivot,world,scene);

    /*
        The hinge, about +Y through the pivot, with the bat as body 2. Its angle is zero in the
        pose it is created in - rest - and the limit is one-sided: from rest to rest + sweep in
        the direction this hand swings. rp3d wants the lower limit in [-2pi, 0] and the upper in
        [0, 2pi], which is why the mirrored hand's sweep goes on the negative side rather than the
        limits being mirrored by swapping them.
    */
    if (GetRigidBody() && anchor->GetRigidBody()){
        //Into a named variable first, then converted. This used to read toradians(up - rest),
        //and core's toradians did not parenthesise its argument, so it came out as
        //up - (rest/180)*pi: a 101-radian sweep, sixteen turns, and a flipper that spun like a
        //propeller. The macro is fixed now; the variable stays because it costs nothing and the
        //next macro might not be.
        const float sweep_degrees = up_degrees - rest_degrees;
        const float sweep = toradians(sweep_degrees);
        rp3d::Vector3 world_pivot(pivot.x,pivot.y,pivot.z);
        rp3d::Vector3 axis(0.0f,1.0f,0.0f);
        rp3d::HingeJointInfo info(anchor->GetRigidBody(),GetRigidBody(),world_pivot,axis);
        info.isCollisionEnabled = false;
        info.isLimitEnabled     = true;
        info.minAngleLimit      = (direction > 0.0f) ? 0.0f : -sweep;
        info.maxAngleLimit      = (direction > 0.0f) ? sweep : 0.0f;
        info.isMotorEnabled     = true;
        info.motorSpeed         = -direction * return_speed;
        info.maxMotorTorque     = return_torque;
        hinge = dynamic_cast<rp3d::HingeJoint*>(world->rp_world->createJoint(info));
        if (!hinge){
            debug->Err("Flipper %s: failed to create the hinge joint\n",flipper_name);
        }
    }
}

Flipper::~Flipper(){
    //The joint holds raw body pointers, so it has to go before either body does.
    if (hinge && physics_world && physics_world->rp_world){
        physics_world->rp_world->destroyJoint(hinge);
        hinge = NULL;
    }
    if (anchor){
        anchor->Destroy();
        anchor = NULL;
    }
}

void Flipper::SetFlip(bool f_flip){
    if (!hinge){
        return;
    }
    //Set every tick, not only on change: the tuning values behind these can move between ticks
    //and a motor is cheap to re-target.
    f_flipping = f_flip;
    hinge->enableMotor(true);
    hinge->setMotorSpeed(direction * (f_flip ? motor_speed : -return_speed));
    hinge->setMaxMotorTorque(f_flip ? motor_torque : return_torque);
}

float Flipper::GetAngleDegrees(){
    //The relative rotation from rest is a pure yaw, so this is exact - HingedDoor's method.
    quat inv_rest = rest_rotation;
    inv_rest.inverse();
    quat relative = inv_rest * GetRotation();
    float radians = 2.0f * atan2f(relative.y,relative.w);
    return direction * todegrees(radians);
}

//--- Plunger ------------------------------------------------------------------------------------

Plunger::Plunger(const char* plunger_name, AssetManager* assets, const char* part, Mesh* fallback,
                 int material, PhysicsWorld* world, Scene* scene,
                 const vec3& rest, float travel_distance):Object(){
    name          = plunger_name;
    physics_world = world;
    rest_position = rest;
    travel        = travel_distance;

    if (assets && part && assets->GetAsset(part)){
        assets->GetObjectFromAsset(part,this);
    }else if (fallback){
        SetMesh(fallback);
    }
    SetMaterialSlot(0,material);
    //The tip is a cylinder along its own +Y (MakeCylinder's axis, and the part is modelled to
    //match); the plunger's axis is +Z. +90 about X takes +Y to +Z.
    const quat onto_z = quat(vec3(1,0,0),toradians(90.0f));
    SetPosition(rest_position);
    SetRotation(onto_z);
    SetPickability(false);

    if (!world || !scene){
        debug->Err("Plunger %s: no world or scene\n",plunger_name);
        return;
    }
    AddPhysics(world);
    Physics* p = GetPhysics();
    if (!p){
        return;
    }
    //A flat-faced box in the body's own frame, where local +Y is world +Z: half a tip length
    //along local y, the tip's radius the other two ways. A capsule would give the ball a domed
    //face to glance off; a plunger tip is flat.
    const float r = 0.11f;
    p->AddBoxCollider(vec3(r,PIN_PLUNGER_TIP_LENGTH * 0.5f,r),vec3(),quat().identity(),1.0f);
    p->SetBounciness(PIN_PLUNGER_TIP_BOUNCINESS);
    p->SetFrictionCoefficient(0.20f);
    PrepareDynamicBody(p,PIN_PLUNGER_MASS);
    //The slider constrains everything but z; the locks make sure of it, so a glancing ball
    //cannot twist the tip in its bore.
    p->SetLinearLockAxis(vec3(0.0f,0.0f,1.0f));
    p->SetAngularLockAxis(vec3(0.0f,0.0f,0.0f));
    SetCollisionCategoryBits(PIN_CAT_PLUNGER);
    SetCollideWithMaskBits(PIN_CAT_BALL);
    scene->AddObject(this);

    char anchor_name[64];
    snprintf(anchor_name,sizeof(anchor_name),"%s_anchor",plunger_name);
    anchor = MakeAnchor(anchor_name,rest_position,world,scene);

    //The slider along +Z from rest, with a hard stop at rest (0) and at full pull (travel).
    if (GetRigidBody() && anchor->GetRigidBody()){
        rp3d::Vector3 world_anchor(rest_position.x,rest_position.y,rest_position.z);
        rp3d::Vector3 axis(0.0f,0.0f,1.0f);
        rp3d::SliderJointInfo info(anchor->GetRigidBody(),GetRigidBody(),world_anchor,axis,
                                   0.0f,travel);
        info.isCollisionEnabled = false;
        info.isMotorEnabled     = false;
        info.motorSpeed         = 0.0f;
        info.maxMotorForce      = pull_force;
        slider = dynamic_cast<rp3d::SliderJoint*>(world->rp_world->createJoint(info));
        if (!slider){
            debug->Err("Plunger %s: failed to create the slider joint\n",plunger_name);
        }
    }
}

Plunger::~Plunger(){
    if (slider && physics_world && physics_world->rp_world){
        physics_world->rp_world->destroyJoint(slider);
        slider = NULL;
    }
    if (anchor){
        anchor->Destroy();
        anchor = NULL;
    }
}

void Plunger::SetPull(bool f_pull){
    if (!slider){
        return;
    }
    f_pulling = f_pull;
    //The hand: a motor toward +Z while held. Off on release, so the spring alone brings it home
    //and the motor is not fighting it on the way.
    slider->enableMotor(f_pull);
    slider->setMotorSpeed(f_pull ? pull_speed : 0.0f);
    slider->setMaxMotorForce(pull_force);
}

float Plunger::GetTravel(){
    Physics* p = GetPhysics();
    if (!p){
        return 0.0f;
    }
    return max(0.0f,p->GetBodyWorldPosition().z - rest_position.z);
}

void Plunger::UpdatePhysicsState(){
    Physics* p = GetPhysics();
    if (p && !f_pulling){
        //Only while the hand is off it: while pulling, the motor is the only thing that should be
        //deciding where the tip goes, or pull_force has to beat the spring at full stretch too.
        const float x = GetTravel();
        const float v = p->GetVelocity().z;
        const float force = -(spring * x) - (damping * v);
        p->AddWorldForceAt(vec3(0.0f,0.0f,force),p->GetBodyWorldPosition());
    }
    Object::UpdatePhysicsState();
}
