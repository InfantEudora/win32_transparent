#include "Pickup.h"
#include "Debug.h"
static Debugger* debug = new Debugger("Pickup",DEBUG_INFO);

Pickup::Pickup(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene,
               PickupKind kind_in, float amount_in):Object(){
    name = "Pickup";
    kind = kind_in;
    amount = amount_in;

    if (!assetmanager){
        debug->Fatal("No assetmanager given!\n");
        return;
    }
    assetmanager->GetObjectFromAsset("capsule",this);

    AddPhysics(physicsworld);
    if (physics){
        //Sized to the mesh rather than to hardcoded numbers, so rescaling the asset in the .glb
        //does not silently leave the trigger volume the wrong size. GetExtents is the FULL size
        //along each axis; a capsule takes a radius and the height of its cylindrical part, and
        //the two hemisphere caps add radius at each end - hence the -2*radius.
        vec3 extents = GetMesh() ? GetMesh()->GetExtents() : vec3(1,1,1);
        float radius = max(extents.x,extents.z) * 0.5f;
        float height = max(extents.y - radius * 2.0f,0.01f);
        physics->AddCapsuleCollider(radius,height,vec3(0,0,0),quat().identity());
        //A trigger reports overlaps and generates no contact response - see the class comment.
        //Physics::SetTrigger only touches the collider that was added last, so this has to come
        //straight after AddCapsuleCollider.
        physics->SetTrigger(true);
        //DYNAMIC purely so it can hold a spin for looks; with a trigger collider nothing can push
        //it, and gravity is off like everything else in this top-down scene, so it stays put.
        physics->SetStatic(false);
        physics->SetGravityEnabled(false);
        if (physics->body && physics->body->rigidbody){
            //AddCapsuleCollider leaves angular damping at 0.5, which would bleed the spin away
            //within a second or two. A pickup should keep turning until it is taken.
            physics->body->rigidbody->setAngularDamping(0.0f);
            physics->body->rigidbody->setIsAllowedToSleep(false);
        }
    }

    //Only the ship. A pickup is deliberately invisible to asteroids and lasers: rp3d filters
    //trigger overlaps through the same category/mask pair as ordinary collisions, so leaving
    //those out means the broadphase never even reports them. Note ShipCharacter's own
    //collide-with mask has to name PICKUP too, or the pair is filtered from the other side.
    SetCollisionCategoryBits(COLLISION_CATEGORY_PICKUP);
    SetCollideWithMaskBits(COLLISION_CATEGORY_SHIP);
}

Pickup::~Pickup(){

}

const char* Pickup::KindName() const{
    switch (kind){
        case PICKUP_KIND_ENERGY: return "energy";
        case PICKUP_KIND_AMMO:   return "ammo";
        case PICKUP_KIND_HEALTH: return "health";
        default:                 return "none";
    }
}
