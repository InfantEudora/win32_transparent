#ifndef _PICKUP_H_
#define _PICKUP_H_

#include "AssetManager.h"
#include "Physics.h"
#include "Scene.h"
#include "ShipCollisionMasks.h"

//What a pickup gives the ship when it is collected. NOTHING READS THIS YET - the ship has no
//energy, ammo or health to add it to, so a collected pickup currently just vanishes. The kind and
//the amount are carried anyway so that the thing which does eventually consume them has a shape to
//aim at, and so a SHIP_CMD_SPAWN_PICKUP recorded today still means the same thing later.
//Never renumber: these travel in a SimCommand's `subtype`, and a recording refers to them by value.
enum PickupKind : uint32_t{
    PICKUP_KIND_NONE = 0,
    PICKUP_KIND_ENERGY,
    PICKUP_KIND_AMMO,
    PICKUP_KIND_HEALTH
};

//A floating capsule the ship flies through to collect. Built from the "capsule" asset in
//data/ships.glb, which is where the "Add Capsule" debug button used to get it.
//
//Its collider is a TRIGGER, which is the whole difference between this and every other object in
//the scene: rp3d reports the overlap and applies no collision response at all, so the ship flies
//straight through rather than bouncing off, and nothing else in the world can shove a pickup
//around. Collection is handled by ApplicationShip::onTrigger - see there for why the pickup is not
//destroyed on the spot.
class Pickup : public Object{
public:
    Pickup(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene,
           PickupKind kind = PICKUP_KIND_ENERGY, float amount = 25.0f);
    ~Pickup();

    PickupKind kind = PICKUP_KIND_NONE;
    float amount = 0.0f;

    //Set once, by whatever collects this, so a pickup cannot be banked twice if the ship is still
    //inside the trigger volume on the tick after it was claimed. Object::Destroy only MARKS the
    //object (Renderer::DeleteDestroyedObjects does the deleting, a tick later at the earliest), so
    //IsDestroyed alone is not a reliable "already taken".
    bool f_collected = false;

    const char* KindName() const;
};

#endif // _PICKUP_H_
