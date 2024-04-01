#include "ApplicationGrid.h"
#include "Debug.h"
#include "OBJLoader.h"


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
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,8));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->renderer->objects.push_back(scene->camera);

    //We make an assetmanager which we use to load/build all assets from:
    app->assetmanager = new AssetManager();
    Object* grid_cell = new Object();
    grid_cell->SetMesh(OBJLoader::ParseOBJFile("isoterrain/data/tile_terrain.obj"));
    app->assetmanager->AddNewAsset("grid_cell",grid_cell);
    delete grid_cell;


    //We now generate a terrain, and load that in.
    app->terrain = new IsoTerrain();
    app->terrain->assetmanager = app->assetmanager;
    app->terrain->CreateTerrain(5,5);
    scene->renderer->objects.push_back(app->terrain);

    //Test arrows to test all this quaternion madness.
    Object* arrows = new Object();
    arrows->SetMesh(OBJLoader::ParseOBJFile("data/arrows.obj"));
    arrows->name = "Axis Arrows";
    app->selected_object = arrows;
    scene->renderer->objects.push_back(arrows);

    app->main_scene->UpdatePhysics();

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

    BinaryAsset::DumpBinaryAssets();

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

    tmr_physics->Stop();
    tmr_physics->Restart();

    //Testing. TODO: Make a slider with more accurate intervals than sleep.
    Sleep(5);

    //Check if we selected a tile
    objectid_t objid = input->GetHoveredObjectID();

    //Camera rotation moving
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        int dx = input->GetDelta(INPUT_MOUSE_X);
        int dy = input->GetDelta(INPUT_MOUSE_Y);
        if (input->IsKeyDown(INPUT_SHIFT)){
            //Move the camera
            camera->MoveSidewaysBy(-dx/100.0f);
            camera->MoveUpBy(dy/100.0f);
        }else{
            //If we move left/right, we rotate the camera around the origin.
            vec3 p = camera->GetPosition();
            vec3 axis = camera->GetLeft();

            //Get the axis towards the camera.
            quat q(axis,-dy/50.0f);

            //Rotate the camera position around the origin
            p = q * p;
            //We update the position
            camera->SetPosition(p);

            //Reset the lookat to 0,0,0 with current camera up, allowing a full 360 rotation around left axis.
            vec3 up = camera->GetUp();
            //up = vec3(0,1,0);
            camera->SetLookAt(vec3(),&up);

            //Now we rotate around the Y-axis
            p = camera->GetPosition();
            axis = vec3(0,1,0);
            q.set_rotation(axis,-dx/50.0f);
            p = q * p;
            camera->SetPosition(p);
            //The lookat should make the same rotation around the y axis
            camera->RotateBy(q);
        }
    }

    static float mouse_delta_sum = 0;
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
    if (mouse_delta_sum != 0){
        camera->MoveForwardBy(-mouse_delta_sum / 10.0f);
        mouse_delta_sum /= 1.1;
    }

    //Iterate over all the rendered objects
    for (Object* object:renderer->renderable_objects){
        if (input->WasKeyReleased(INPUT_CLICK_LEFT) && (object->GetID() == objid)){
            vec3 p = object->GetPosition();
            debug->Info("Clicked on ID: %3i Object Pos: %.2f %.2f %.2f\n",objid,p.x,p.y,p.z);
            object->material_slot[1] = 1;
            selected_object = object;
        }else if (object->GetID() == objid){
            object->material_slot[0] = 2;
        }else{
            object->material_slot[0] = object->material_slot[1];
        }
    }
}

void ApplicationGrid::UpdateUI(){
    //UI
    ImGui::Begin("Grid UI");
    ImGui::Text("Behold, a grid of tiles.");

    Object* object = main_scene->camera;
    if (ImGui::CollapsingHeader("Main Camera Controls")){
        float roll = 0;
        if (ImGui::DragFloat("Drag to Roll Camera",&roll,0.01,-1,1)){
            object->RollBy(roll);
        }

        vec3 up = object->GetUp();
        vec3 forward = object->GetForward();
        ImGui::BeginDisabled();
        ImGui::DragFloat3("Forward Vector", (float*)&forward, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Up Vector", (float*)&up, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
    }

    object = selected_object;

    if (!object){
        ImGui::Text("No Object Selected");
    }else{
        ImGui::Text("Selected Object: %s",object->name.c_str());
        if (object->GetMesh() && ImGui::CollapsingHeader("Mesh")){
            Mesh* mesh = object->GetMesh();
            ImGui::Text(" ID             : %lu",mesh->GetID());
            ImGui::Text(" num_vertices   : %lu",mesh->num_vertices);
            ImGui::Text(" num_materials  : %lu",mesh->num_materials);
            ImGui::Text(" num_references : %lu",mesh->num_references);
        }

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
            ImGui::Text("Renderer Materials: %i",renderer->materials.size());
            ImGui::Separator();

            ImGui::DragInt("Material Slot 0",&object->material_slot[0],1,-1,10);
            ImGui::DragInt("Material Slot 1",&object->material_slot[1],1,-1,10);
            ImGui::DragInt("Material Slot 2",&object->material_slot[2],1,-1,10);
        }
    }

    if (ImGui::CollapsingHeader("Performance")){
        ImGui::Text("Frame Rate   : %.2f FPS (%.2f ms)", 1000000.0f / renderer->tmr_frame->avg,renderer->tmr_frame->avg/1000.0f );
        ImGui::Text("Physics Rate : %.2f TPS (%.2f ms)", 1000000.0f / tmr_physics->avg,tmr_physics->avg/1000.0f );
    }

    ImGui::End();
}