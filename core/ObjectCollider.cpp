#include "ObjectCollider.h"
#include "Debug.h"
static Debugger* debug = new Debugger("ObjectCollider", DEBUG_INFO);

ObjectCollider::ObjectCollider():Object(){

}

ObjectCollider::~ObjectCollider(){

}

void ObjectCollider::HookTargetCollider(rp3d::Collider* target){
    target_collider = target;
    name = "Collider Hook";
    rp3d::Transform t = target_collider->getLocalToBodyTransform();
    SetPosition((vec3&)t.getPosition());
}

//Called by Physics
void ObjectCollider::UpdatePhysicsState(){
    if (!target_collider){
        Object::UpdatePhysicsState();
        return;
    }

    rp3d::Transform t = target_collider->getLocalToBodyTransform();
    vec3 p = GetPosition();
    t.setPosition((rp3d::Vector3&)p);
    target_collider->setLocalToBodyTransform(t);

    Object::UpdatePhysicsState();
}