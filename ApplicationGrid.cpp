#include "ApplicationGrid.h"
#include "Debug.h"
#include "OBJLoader.h"
#include "IsoTerrain.h"

static Debugger *debug = new Debugger("ApplicationGrid", DEBUG_ALL);

ApplicationGrid::ApplicationGrid():Application(){
    debug->Info("Created new application.\n");
};


//Function for rendering the frame to a window
DWORD WINAPI ApplicationGrid::GridFrameThreadFunction(LPVOID lpParameter){
    ApplicationGrid* app = static_cast<ApplicationGrid*>(lpParameter);
    if (!app){
        debug->Err("No application was supplied to FrameThread\n");
        return 0;
    }

    app->thread_id_render = GetCurrentThreadId();
    debug->Info("FrameFunction ThreadID: %lu\n",app->thread_id_render);

    //We make the window's context current to this thread
    if (!wglMakeCurrent(app->main_window->hDC, app->main_window->hRC)){
        debug->Err("FrameFunction Thread unable to get context by wglMakeCurrent\n");
        return 0;
    }

    if (!app->main_window->InitImGui()){
        debug->Fatal("Failed to setup ImGui on Window\n");
    }

    //Create a renderer for this window
    app->renderer = new Renderer(app->main_window->width,app->main_window->height);
    app->renderer->Init();

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    app->main_scene = new Scene();
    app->main_scene->renderer = app->renderer;
    app->main_scene->inputcontroller = app->main_window->inputcontroller;
    app->main_scene->shader = app->default_shader;

    //Either we put thigs in the scene, or we make a scene extension class....
    Scene* scene = app->main_scene;
    scene->camera = new Camera();
    scene->camera->SetPosition(vec3(5,5,8));
    scene->camera->SetLookat(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->renderer->objects.push_back(scene->camera);

    //We now generate a terrain, and load that in.
    IsoTerrain* terrain = new IsoTerrain();
    terrain->CreateTerrain(5,5);
    scene->renderer->objects.push_back(terrain);

    //Test arrows to test all this quaternion madness.
    app->arrows = new Object();
    app->arrows->SetMesh(OBJLoader::ParseOBJFile("data/arrows.obj"));
    scene->renderer->objects.push_back(app->arrows);

    app->main_scene->UpdatePhysics();

    for (Object* child:terrain->children){
        vec3 p = child->GetPosition();
        //debug->Info("Terrain Child ID, Pos: %3i,  %.2f %.2f %.2f\n",child->GetID(), p.x,p.y,p.z);
    }

    //Create a material
    material_t m;
    m.color = vec4(1,1,1,1);
    m.texture_unit = 0;
    scene->renderer->materials.push_back(m);

    //A material with a texture.
    Texture* texture = new Texture();
    texture->LoadFromFile("data/textures/test_texture_4096.png");
    glBindTextureUnit(0, texture->texture_id);


    m.color = vec4(1,0.2,0.2,0.9);
    m.texture_unit = 0;
    scene->renderer->materials.push_back(m);

    Asset::DumpAssets();

    //Now that all the setup is done, we create another thread for physics.
    HANDLE hThread = NULL;
    DWORD thread_id;
    // Create a new thread which will get it's own render context
    hThread = CreateThread(
        NULL,    // Thread attributes
        0,       // Stack size (0 = use default)
        PhysicsThreadFunction, // Thread start address
        app,    // Parameter to pass to the thread
        0,       // Creation flags
        &app->thread_id_physics);   // Thread id

    if (hThread == NULL){
        debug->Fatal("Unable to create thread\n");
    }

    while (app->main_window->f_should_quit == false){
        //Should only modify the object, and we should be able to move this to a seperate thread.
        app->main_scene->HandleInput();

        //Tell ImGui to start a new frame
        app->main_window->ImGuiNewFrame();

        //This should render the frame only.
        app->main_scene->DrawFrame();

        app->UpdateUI();

        app->main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        app->main_window->DrawFrame();
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}


void ApplicationGrid::Run(void){
    //Create a main window
    main_window = Window::CreateNewWindow(1024,768,&Window::wcs.at(0));
    if (!main_window){
        debug->Fatal("Unable to create window\n");
    }
    if (!main_window->Init()){
        debug->Fatal("Failed to init window\n");
    }

    main_window->Show(SW_SHOWDEFAULT);

    //Setup renderer
    Renderer::SetVSync(true);

    //We release the window's context from this thread
    wglMakeCurrent(main_window->hDC, NULL);

    //And do all render calls from a seperate thread:
    HANDLE hThread = NULL;

    // Create a new thread which will get this one's render context
    hThread = CreateThread(
        NULL,    // Thread attributes
        0,       // Stack size (0 = use default)
        GridFrameThreadFunction, // Thread start address
        this,    // Parameter to pass to the thread
        0,       // Creation flags
        &thread_id_render);   // Thread id

    if (hThread == NULL){
        debug->Fatal("Unable to FrameFunction thread\n");
    }

    //Catch all input and window related messages in this thread:
    MSG msg = {0};
    while (main_window->f_should_quit == false){
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)){
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }else{
            Sleep(1);
        }
    }
}

//Called before update physics
void ApplicationGrid::RunLogic(){
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    //Check if we selected a tile
    objectid_t objid = input->GetHoveredObjectID();

    //Camera rotation moving
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        //If we move left/right, we rotate the camera around the origin.
        int dx = input->GetDelta(INPUT_MOUSE_X);
        int dy = input->GetDelta(INPUT_MOUSE_Y);

        float dist = camera->GetPosition().length();
        vec3 p = camera->GetPosition();
        vec3 ax = p.normalize().cross(camera->GetUp());
        quat q;

        //Get the axis towards the camera.
        q.set_rotation(ax,-dy/50.0f);
        p = q * p * dist;

        //debug->Info("Mouse dX = %i\n",dx);
        vec3 d = vec3(dx / 20.0f,dy / 20.0f,0);

        //We update the position, without changing the lookat.
        //This should update the UP vector.
        camera->SetPosition(p);
    }

    //Iterate over all the rendered objects
    for (Object* object:renderer->renderable_objects){
        if (input->WasKeyReleased(INPUT_CLICK_LEFT) && (object->GetID() == objid)){
            vec3 p = object->GetPosition();
            debug->Info("Clicked on ID: %3i Object Pos: %.2f %.2f %.2f\n",objid,p.x,p.y,p.z);
            object->material_slot[1] = 1;
        }else if (object->GetID() == objid){
            object->material_slot[0] = 2;
        }else{
            object->material_slot[0] = object->material_slot[1];
        }
    }

    for (Object* object:renderer->objects){
        if (object == camera){
            object->UpdatePhysicsState();
            continue;
        }
    }
}



void ApplicationGrid::UpdateUI(){
    //UI
    ImGui::Begin("Grid UI");
    ImGui::Text("Behold, a grid of tiles.");

    Object* object = main_scene->camera;
    static float roll = 0;
    if (ImGui::DragFloat("Camera Roll",&roll,0.01,-1,1)){
        object->RollBy(roll);
        roll = 0;
    }

    object = arrows;

    //Material slots
    if (ImGui::CollapsingHeader("Rotation")){
        static int option = 0;
        ImGui::Text("Input By:");
        ImGui::RadioButton("Vector + Rotation", &option, 0); ImGui::SameLine();
        ImGui::RadioButton("Target, Position, Up", &option, 1);
        ImGui::Separator();
        quat q;
        if (option == 0){
            static vec3 quatinp = {0,0,0};
            ImGui::DragFloat3("Quat Input Vector", (float*)&quatinp, 0.01f, -1.0f, 1.0f);
            static float quatroll = 0.0f;
            ImGui::DragFloat("Quat Roll", (float*)&quatroll, 0.01f, -TYPE_PI, TYPE_PI);
            ImGui::BeginDisabled();
            vec3 quatn = quatinp;
            quatn.normalize();
            ImGui::DragFloat3("Quat Normalized Vector", (float*)&quatn, 0.01f, -1.0f, 1.0f);
            q = quat(quatn,quatroll);
            ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
        }else{
            static vec3 target = {0,0,-1};
            ImGui::DragFloat3("Target Vector", (float*)&target, 0.01f, -5.0f, 5.0f);
            static vec3 position = {0,0,0};
            ImGui::DragFloat3("Position", (float*)&position, 0.01f, -5.0f, 5.0f);
            static vec3 worldup = {0,1,0};
            ImGui::DragFloat3("World Up", (float*)&worldup, 0.01f, -1.0f, 1.0f);
            ImGui::BeginDisabled();
            q = quat::getquat(target,position,worldup);
            ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
        }
        object->SetRotation(q);
    }
    if (ImGui::CollapsingHeader("Material")){
        ImGui::DragInt("Material Slot 0",&object->material_slot[0],1,-1,10);
        ImGui::DragInt("Material Slot 1",&object->material_slot[1],1,-1,10);
    }
    ImGui::End();
}