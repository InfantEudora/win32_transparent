#ifndef _SCENE_H_
#define _SCENE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

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
    void UpdatePhysics();
    void DrawFrame();

    void AddObject(Object* object);

    Object* FindObject(const std::string& name);

    bool IsPhysicsPaused(){return f_paused;}
    void PausePhysics(bool paused){f_paused = paused;}

    //Do we always need a handle to a single camera?
    Camera* camera = NULL;
private:
    uint64_t physics_ticks = 0;
    bool f_paused = false;
};

#endif
