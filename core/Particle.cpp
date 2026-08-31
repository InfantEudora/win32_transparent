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
        //AddPhysics (called by the delegated constructor above, for this new instance's own
        //body) always defaults gravity to false, same as every other Object - copy the
        //reference particle's own setting instead of silently dropping it, so a particle type
        //configured to fall (see ApplicationTank::Init's fire-impact particle) actually does,
        //on every instance EmitParticles spawns from it, not just the one template Object.
        physics->SetGravityEnabled(p->IsGravityEnabled());
    }
    if (p && p->body && p->body->collider){
        //debug->Info("physics->world = %p\n",physics->world);
        rp3d::CollisionShape* shape =  p->body->collider->getCollisionShape();
        reactphysics3d::Transform t = reactphysics3d::Transform::identity();
        physics->body->collider = physics->body->rigidbody->addCollider(shape,t);
        physics->body->rigidbody->updateMassPropertiesFromColliders();
        physics->body->rigidbody->setUserData(this);

        SetCollisionCategoryBits(particle->collision_category_bits);
        SetCollideWithMaskBits(particle->collide_with_bits);

    }
    name = particle->name;
    //Copy the material names and slots
    material_names[0] = particle->material_names[0];
    material_slot[0] = particle->material_slot[0];
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
