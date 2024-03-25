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

class Scene{
public:
    Scene();

    InputController* inputcontroller = NULL;
    Renderer* renderer = NULL;
    Shader* shader = NULL;

    void SetupExample();
    void SetupShipExample();

    void HandleInput();
    void UpdatePhysics();
    void DrawFrame();

    bool f_paused = false;

    //Our example objects and thigs that definitely need to be in their own class
    Object* cube = NULL;
    Camera* camera = NULL;
    //The textures we want to use as material
    Texture* tex_1 = NULL;
    Texture* tex_2 = NULL;

    uint64_t physics_ticks = 0;

};

#endif
