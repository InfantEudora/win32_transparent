#ifndef _OBJECT_COLLIDER_H_
#define _OBJECT_COLLIDER_H_
#include "Object.h"
#include "type_int3.h"
#include "AssetManager.h"
#include <map>
#include <array>

/*
    An extension of object with the sole purpose of modifying another object's collider
    This normally gets called into existance in the editor, so we can modify it as a normal object.
    Only... this hooks to the collider of a target object.
*/
class ObjectCollider : public virtual Object{
public:
    ObjectCollider();
    ~ObjectCollider();

    void UpdatePhysicsState() override;
    void HookTargetCollider(rp3d::Collider* target);

private:
    rp3d::Collider* target_collider = NULL;
};


#endif