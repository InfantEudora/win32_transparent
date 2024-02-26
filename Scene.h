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

    void HandleInput();
    void UpdatePhysics();
    void DrawFrame();

    //Our example objects and thigs that definitely need to be in their own class
    Object* cube = NULL;
    Camera* camera = NULL;

};

#endif
