#include "Asteroid.h"
#include "Debug.h"
static Debugger* debug = new Debugger("Asteroid",DEBUG_INFO);

Asteroid::Asteroid(AssetManager* assetmanager, PhysicsWorld* physicsworld, Scene* target_scene, RRandom* rrand):Object(){
    name = "Asteroid";

    if (!assetmanager){
        debug->Fatal("No assetmanager given!\n");
        return;
    }

    AddPhysics(physicsworld);

    //Select an asteroid model at random
    std::vector<std::string>asteroid_models = {
        "asteroid.001",
        "asteroid.002",
        "asteroid.003"
    };
    int idx = rrand->GetInt(0,asteroid_models.size()-1);
    assetmanager->GetObjectFromAsset(asteroid_models[idx].c_str(),this);

    if (physics){
        physics->AddSphereCollider(1.0f,vec3(0,0,0),quat().identity());
        physics->SetStatic(false);
        physics->SetGravityEnabled(false);
        physics->body->rigidbody->setLinearDamping(0.1);
        physics->body->rigidbody->setUserData(this);
        physics->body->rigidbody->setIsAllowedToSleep(false);
        physics->body->rigidbody->updateMassPropertiesFromColliders();
        physics->body->rigidbody->setMass(5);
    }

    SetCollisionCategoryBits(COLLISION_CATEGORY_ASTEROID);
    SetCollideWithMaskBits(COLLISION_CATEGORY_SHIP|COLLISION_CATEGORY_LASER);
}

Asteroid::~Asteroid(){

}