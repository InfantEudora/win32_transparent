#include "HingedDoor.h"
#include "type_helpers.h"
#include <cmath>
#include "Debug.h"

static Debugger* debug = new Debugger("HingedDoor",DEBUG_INFO);

//How far the door can swing either way before the hinge's hard limit stops it. rp3d wants
//minAngleLimit in [-2pi,0] and maxAngleLimit in [0,2pi], and measures both from the pose the
//joint was created in - which here is the closed door.
static const float door_min_angle = -110.0f * TYPE_PI / 180.0f;
static const float door_max_angle =  110.0f * TYPE_PI / 180.0f;

//Scales one of the glb's unit primitives to the size we want. GetExtents() is the mesh's FULL
//size along each axis (the "cube" and "cylinder" assets are both 2x2x2), same convention
//CraneCharacter's MakeCraneBox uses.
static void ScaleAssetTo(Object* object, const vec3& size){
    vec3 raw = object->GetMesh() ? object->GetMesh()->GetExtents() : vec3(1,1,1);
    if (raw.x > 0.0001f && raw.y > 0.0001f && raw.z > 0.0001f){
        object->SetScale(vec3(size.x / raw.x,size.y / raw.y,size.z / raw.z));
    }
}

HingedDoor::HingedDoor(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene,
                       const vec3& hinge_position, float yaw, float width):Object(){
    name = "Door Panel";
    if (!assetmanager || !physicsworld || !target_scene){
        debug->Fatal("HingedDoor needs an assetmanager, a physics world and a scene.\n");
        return;
    }
    physics_world = physicsworld;

    const float panel_height = 1.2f;        //tall enough to catch the ship (half-height 0.4) and the asteroids
    const float panel_thickness = 0.16f;
    const float panel_mass = 8.0f;          //the ship is 10kg, so a shoulder-charge visibly swings this
    const float post_radius = 0.18f;
    const float post_height = 1.5f;

    const quat door_rotation = quat(vec3(0,1,0),yaw);

    //--- The post: a static cylinder standing on the hinge axis. Static and not kinematic,
    //unlike the crane's base - nothing ever moves it, so there is no velocity for the joint to
    //have to follow.
    post = assetmanager->GetObjectFromAsset("cylinder");
    if (post){
        post->name = "Door Hinge Post";
        post->material_names[0] = "metal_material";
        ScaleAssetTo(post,vec3(post_radius * 2.0f,post_height,post_radius * 2.0f));
        post->SetPosition(hinge_position);
        post->AddPhysics(physicsworld);
        if (Physics* p = post->GetPhysics()){
            //rp3d has no cylinder shape; a capsule of the same radius whose total height
            //(cylinder part + the two caps) matches the visual is the closest primitive, and the
            //rounded ends sit above/below anything in this scene anyway.
            p->AddCapsuleCollider(post_radius,post_height - post_radius * 2.0f,vec3(0,0,0),quat().identity());
            p->SetStatic(true);
        }
        post->SetCollisionCategoryBits(COLLISION_CATEGORY_DOOR);
        post->SetCollideWithMaskBits(COLLISION_CATEGORY_SHIP|COLLISION_CATEGORY_ASTEROID|COLLISION_CATEGORY_LASER);
        target_scene->AddObject(post);
    }

    //--- The panel: the "cube" asset squashed flat, hung off the post by one edge so the hinge
    //axis runs down that edge rather than through the middle of the leaf.
    assetmanager->GetObjectFromAsset("cube",this);
    material_names[0] = "container_material";
    ScaleAssetTo(this,vec3(width,panel_height,panel_thickness));
    rest_position = hinge_position + door_rotation * vec3(width * 0.5f,0,0);
    rest_rotation = door_rotation;
    //Both before AddPhysics: that is what seeds the rigid body's transform.
    SetPosition(rest_position);
    SetRotation(rest_rotation);
    AddPhysics(physicsworld);
    if (physics){
        //Density rather than SetMass, so the inertia tensor rp3d computes is the one belonging to
        //a body of this mass, not a 1kg/m3 one rescaled afterwards.
        float density = panel_mass / (width * panel_height * panel_thickness);
        physics->AddBoxCollider(vec3(width,panel_height,panel_thickness) * 0.5f,vec3(0,0,0),quat().identity(),density);
        physics->SetStatic(false);
        //Same as the ship and the asteroids: this scene is a top-down plane, and gravity would
        //only load the hinge up with a force it has to cancel again every tick.
        physics->SetGravityEnabled(false);
        if (physics->body && physics->body->rigidbody){
            //AddBoxCollider leaves both dampings at 0.5. The spring below is what should settle
            //the door, so take most of the built-in angular damping back out - the linear
            //damping can stay, it costs nothing on a body the hinge pins in place.
            physics->body->rigidbody->setAngularDamping(0.05f);
            //A door that came to rest would otherwise fall asleep, and a sleeping body ignores
            //the spring torque and the laser knocks alike - it would just stop reacting.
            physics->body->rigidbody->setIsAllowedToSleep(false);
        }
    }
    SetCollisionCategoryBits(COLLISION_CATEGORY_DOOR);
    SetCollideWithMaskBits(COLLISION_CATEGORY_SHIP|COLLISION_CATEGORY_ASTEROID|COLLISION_CATEGORY_LASER);

    //--- The hinge itself, about world up through the post.
    if (post && GetRigidBody() && post->GetRigidBody()){
        rp3d::HingeJointInfo info(post->GetRigidBody(),GetRigidBody(),
                                  (rp3d::Vector3&)hinge_position,(rp3d::Vector3&)hinge_axis);
        //The post stands inside the panel's hinge edge by design, so left at rp3d's default the
        //two would be resolving that overlap every tick on top of the joint constraint - the
        //same trap CraneCharacter's boom hinge documents.
        info.isCollisionEnabled = false;
        info.isLimitEnabled = true;
        info.minAngleLimit = door_min_angle;
        info.maxAngleLimit = door_max_angle;
        hinge = dynamic_cast<rp3d::HingeJoint*>(physicsworld->rp_world->createJoint(info));
        if (!hinge){
            debug->Err("Failed to create the door's hinge joint.\n");
        }
    }
}

HingedDoor::~HingedDoor(){
    //The joint holds raw body pointers, so it has to go before either body does.
    if (hinge && physics_world && physics_world->rp_world){
        physics_world->rp_world->destroyJoint(hinge);
        hinge = NULL;
    }
    if (post){
        post->Destroy(); //picked up by Renderer::DeleteDestroyedObjects, like any other scene object
        post = NULL;
    }
}

//Measured off the panel's own rotation rather than read from hinge->getAngle(): the panel and the
//post are both pure yaw rotations, so the relative rotation is a yaw too and this is exact - and
//it stays independent of which way round rp3d happens to sign its own hinge angle.
float HingedDoor::GetAngle(){
    quat inv_rest = rest_rotation;
    inv_rest.inverse();
    quat relative = inv_rest * GetRotation();
    return 2.0f * atan2f(relative.y,relative.w);
}

void HingedDoor::QueueKnock(const vec3& world_point, const vec3& world_direction){
    queued_knocks.push_back({world_point,world_direction});
    last_knock_point = world_point;
    last_knock_direction = world_direction;
    knock_count++;
}

void HingedDoor::UpdatePhysicsState(){
    if (physics){
        //Forces are cleared by rp3d at the end of every step, so everything applied here lasts
        //exactly the one tick that follows - which is the point: a knock is meant to be an
        //impulse, and the spring wants re-evaluating against the new angle anyway.
        for (const Knock& knock:queued_knocks){
            vec3 direction = knock.direction;
            if (direction.length() > 0.0001f){
                physics->AddWorldForceAt(direction.normalize() * knock_force,knock.point);
            }
        }
        queued_knocks.clear();

        if (spring_stiffness != 0.0f || spring_damping != 0.0f){
            float angle = GetAngle();
            //Only the component of the spin about the hinge axis matters; the joint eats the rest.
            float rate = physics->GetAngularVelocity().dot(hinge_axis);
            float torque = -(spring_stiffness * angle) - (spring_damping * rate);
            physics->AddWorldTorque(hinge_axis * torque);
        }
    }
    Object::UpdatePhysicsState();
}

void HingedDoor::Reset(){
    if (physics){
        physics->SetVelocity(vec3());
        physics->SetAngularVelocity(vec3());
    }
    queued_knocks.clear();
    SetPosition(rest_position);
    SetRotation(rest_rotation);
}
