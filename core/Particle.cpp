#include "Particle.h"
#include "Debug.h"

static Debugger* debug = new Debugger("Particle",DEBUG_INFO);

Particle::Particle(PhysicsWorld* world):Object(){
    AddPhysics(world);
    //debug->Info("Create Particle by world\n");
    name = "Particle";
}

Particle::Particle(Particle* particle):Particle(particle->GetPhysics()->world){
    //debug->Info("Create Particle by reference particle\n");
    //How would we use the Object Copy constructor... that would be nice.
    SetMesh(particle->GetMesh());
    Physics* p = particle->GetPhysics();
    if (p){
        //debug->Info("physics->world = %p\n",physics->world);
        rp3d::CollisionShape* shape =  p->body->collider->getCollisionShape();
        reactphysics3d::Transform t = reactphysics3d::Transform::identity();
        physics->body->collider = physics->body->rigidbody->addCollider(shape,t);
        physics->body->rigidbody->updateMassPropertiesFromColliders();
    }
    name = particle->name + "+1";
    SetCollisionCategoryBits(particle->collision_category_bits);
    SetCollideWithMaskBits(particle->collide_with_bits);
}

Particle::~Particle(){

}

void Particle::UpdatePhysicsState(){
    if (lifetime > 0){
        lifetime -= 0.01;
    }else{
        Hide();
    }
    Object::UpdatePhysicsState();
}
