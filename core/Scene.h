#ifndef _SCENE_H_
#define _SCENE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <atomic>

#include "type_fmat3.h"
#include "type_fmat4.h"

#include "InputController.h"
#include "Renderer.h"
#include "Shader.h"

#include "PhysicsWorld.h"

#include "imgui.h"

class Scene{
public:
    Scene();
    std::string name;
    InputController* inputcontroller = NULL;
    Renderer* renderer = NULL;
    Shader* shader = NULL;

    PhysicsWorld* physics_world = NULL;


    void UpdateInput();
    void UpdateAnimations();
    void UpdatePhysics(float delta_time);
    void DrawFrame();

    void AddObject(Object* object);

    Object* FindObject(const std::string& name);

    bool IsPhysicsPaused(){return f_paused;}
    void PausePhysics(bool paused){f_paused = paused;}

    //While paused, queues up num_steps physics ticks (each a full physics_world->Update() +
    //every object's UpdatePhysicsState(), same as a normal unpaused tick) for the physics
    //thread to run one at a time - lets a caller single-step the simulation deterministically
    //instead of guessing how long to sleep and hoping nothing else advanced in the meantime.
    //A no-op while not paused, since there's nothing to "step" - it's already running freely.
    void StepPhysics(int num_steps){ pending_physics_steps += max(num_steps,0); }
    int GetPendingPhysicsSteps(){ return pending_physics_steps; }

    //Do we always need a handle to a single camera?
    Camera* camera = NULL;
private:
    uint64_t physics_ticks = 0;
    bool f_paused = false;
    std::atomic<int> pending_physics_steps{0}; //written from any thread, consumed by UpdatePhysics on the physics thread
};

#endif
