#include "ApplicationDozer.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationDozer", DEBUG_ALL);

#define INPUT_H     INPUT_LAST+1
#define INPUT_E     INPUT_LAST+2
#define INPUT_FOCUS INPUT_LAST+3
#define INPUT_B     INPUT_LAST+4
#define INPUT_T     INPUT_LAST+5

ApplicationDozer::ApplicationDozer():Application(){
    debug->Info("Created new application.\n");
};

//Function for rendering the frame to a window
DWORD WINAPI ApplicationDozer::FrameThreadFunction(LPVOID lpParameter){
    ApplicationDozer* app = static_cast<ApplicationDozer*>(lpParameter);
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
    app->renderer->Init(PIPELINE_DEFERRED);
    app->renderer->SetVSync(true);
    app->renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");

    //Randomise the randomiser
    app->rrand = new RRandom();
    debug->Info("Polulating RRandom\n");
    app->rrand->Generate(512,512);


    //Renderer settings
    app->renderer->alpha_clip = 0.5f;
    app->renderer->f_render_skybox = false;

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    app->assetmanager = new AssetManager();

    app->soundsystem = new SoundSystem();
    app->soundsystem->Initialise();
    app->soundsystem->AppendFile("dozer/data/engine_start_2.wav","engine_start");
    app->soundsystem->AppendFile("dozer/data/arm_up.wav","arm_up");
    app->soundsystem->AppendFile("dozer/data/engine_idle.wav","engine_idle");
    app->soundsystem->AppendFile("dozer/data/engine_revup.wav","engine_revup");
    app->soundsystem->AppendFile("dozer/data/engine_stop.wav","engine_stop");

    app->main_scene = app->CreateMainScene();
    app->main_scene->UpdatePhysics();

    BinaryAsset::DumpBinaryAssets();
    app->assetmanager->ListAssets();





    //Now that all the setup is done, we create another thread for physics.
    HANDLE hThread = NULL;
    DWORD thread_id;
    // Create a new thread which will get it's own render context
    hThread = CreateThread(
        NULL,    // Thread attributes
        0,       // Stack size (0 = use default)
        PhysicsThreadFunction, // Thread start address in Application base class
        app,    // Parameter to pass to the thread
        0,       // Creation flags
        &app->thread_id_physics);   // Thread id

    if (hThread == NULL){
        debug->Fatal("Unable to create thread\n");
    }

    while (app->main_window->f_should_quit == false){
        if (app->main_window->f_resized){
            app->main_window->f_resized = false;
            app->renderer->Resize(app->main_window->width,app->main_window->height);
        }

        //Tell ImGui to start a new frame
        app->main_window->ImGuiNewFrame();

        //This should render the frame only.
        app->main_scene->DrawFrame(); // This renders state, not state_physics

        app->renderer->state_mutex.lock();
        app->UpdateUI(); //This right now modifies state_physics... but
        app->renderer->state_mutex.unlock();
        //When done, copies that over to prev_state.

        //In Renderer, that get's called
        app->main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        app->main_window->DrawFrame();
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}

void ApplicationDozer::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewWindow(1280,800,&Window::wcs.at(0));
    if (!main_window){
        debug->Fatal("Unable to create window\n");
    }
    if (!main_window->Init()){
        debug->Fatal("Failed to init window\n");
    }

    main_window->Show(SW_SHOWDEFAULT);

    //We release the window's context from this thread
    wglMakeCurrent(main_window->hDC, NULL);

    //And do all render calls from a seperate thread:
    HANDLE hThread = NULL;

    // Create a new thread which will get this one's render context
    hThread = CreateThread(
        NULL,    // Thread attributes
        0,       // Stack size (0 = use default)
        FrameThreadFunction, // Thread start address
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

//Called before update physics from the physics Thread
void ApplicationDozer::RunLogic(){
    //Camera pivot around point
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;



    //Only when in focus
    if (!main_window->f_has_focus){
        return;
    }

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }

    CheckObjectSelection();

    if (dozer_camera_tracking){
        camera_target = dozer->GetPosition();
        vec3 up = vec3(0,1,0);
        camera->SetLookAt(camera_target,&up);
    }

    //Camera rotation moving
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){

        int dx = input->GetDelta(INPUT_MOUSE_X);
        int dy = input->GetDelta(INPUT_MOUSE_Y);
        if (input->IsKeyDown(INPUT_SHIFT)){
            //Move the camera
            vec3 d = camera->MoveSidewaysBy(-dx/100.0f);
            d += camera->MoveUpBy(dy/100.0f);
            camera_target += d;
        }else{
            //If we move left/right, we rotate the camera around the camera target.
            vec3 p = camera->GetPosition() - camera_target;
            vec3 axis = camera->GetLeft();

            //Get the axis towards the camera.
            quat q(axis,-dy/50.0f);

            //Rotate the camera position around the camera target
            p = q * p;
            //We update the position
            camera->SetPosition(p+camera_target);

            //Reset the lookat to 0,0,0 with current camera up, allowing a full 360 rotation around left axis.
            vec3 up = camera->GetUp();
            //up = vec3(0,1,0);
            camera->SetLookAt(camera_target,&up);

            //Now we rotate around the Y-axis
            p = camera->GetPosition()-camera_target;
            axis = vec3(0,1,0);
            q.set_rotation(axis,-dx/50.0f);
            p = q * p;
            camera->SetPosition(p+camera_target);
            //The lookat should make the same rotation around the y axis
            camera->RotateBy(q);
        }
    }

    static float mouse_delta_sum = 0;
    if (mouse_delta_sum != 0){
        camera->MoveForwardBy(mouse_delta_sum / 10.0f);
        mouse_delta_sum /= 1.1;
    }
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);

    //Hide selected object
    if (selected_object){
        if (input->WasKeyReleased(INPUT_H)){
            selected_object->Hide();
        }
    }

    //character
    if (dozer){
        if (input->IsKeyDown(INPUT_MOVE_UP)){
            dozer->MoveForward();
        }
        if (input->IsKeyDown(INPUT_MOVE_DOWN)){
            dozer->MoveBackward();
        }
        if (input->IsKeyDown(INPUT_MOVE_RIGHT)){
            dozer->TurnRight();
        }
        if (input->IsKeyDown(INPUT_MOVE_LEFT)){
            dozer->TurnLeft();
        }
        if (input->IsKeyDown(INPUT_TURN_UP)){
            dozer->ArmUp();
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            dozer->ArmDown();
        }
    }

    if (input->WasKeyReleased(INPUT_B)){
        for (int i=0;i<10;i++){
        int r = rand()%3;
        if (r == 0)
            SpawnAssetAt("Box", vec3(0,5,0));
        if (r == 1)
            SpawnAssetAt("Crate", vec3(0,5,0));
        if (r == 2)
            SpawnAssetAt("Barrel", vec3(0,5,0));
        }
    }

    if (input->WasKeyReleased(INPUT_E)){
        dozer->StartEngine();
    }

    if (input->WasKeyReleased(INPUT_T)){
        dozer_camera_tracking = !dozer_camera_tracking;
    }
}

void ApplicationDozer::SpawnAssetAt(const std::string& name, const vec3& wpos){
    Object* asset = assetmanager->GetObjectFromAsset(name.c_str());
    if (!asset){
        return;
    }
    asset->SetPosition(wpos);
    asset->AddPhysics(main_scene->physics_world);
    Physics* physics = asset->GetPhysics();
    if (physics){
        vec3 extents = asset->GetMesh()->GetExtents();

        physics->AddBoxCollider(extents*0.5f,vec3(0,0.0,0),quat().identity());
        physics->SetStatic(false);
        physics->SetGravityEnabled(true);
    }

    main_scene->AddObject(asset);
    debug->Info("Spwaned in %s\n",name.c_str());

    //Give it some defaults
}

void ApplicationDozer::UpdateUI(){
    //UI
    ImGui::Begin("Hi there!");

    ImGui::Text("Press 'B' to drop some boxes\n");
    ImGui::Text("Press 'E' to start the engine\n");
    ImGui::Text("Press 'W/A' to move the arm up or down\n");
    std::string str_camera_tracking;
    dozer_camera_tracking ? str_camera_tracking = "ENABLED" : str_camera_tracking = "DISABLED";
    ImGui::Text("Press 'T' to enable camera tracking (Currently %s)\n",str_camera_tracking.c_str());


    ImGui::End();

    RenderDebugMenuBar();
    RenderGenericObjectUI();
    RenderRandTestWindow();
}

Scene* ApplicationDozer::CreateMainScene(){
    Scene* scene = CreateNewScene("Dozer Test Scene");

    //Add input to input controller
    scene->inputcontroller->AddKeyMap('H',INPUT_H);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);
    scene->inputcontroller->AddKeyMap('B',INPUT_B);
    scene->inputcontroller->AddKeyMap('T',INPUT_T);
    scene->inputcontroller->AddKeyMap(VK_DECIMAL,INPUT_FOCUS);

    //Setup light and camera
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Add phyics
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0,-9.81,0));

    //Load from a GLTF file and build assets.
    gltfloader.LoadGLTFFile("dozer/data/dozer.glb");

    Object* floor = CreateNewObjectFromGLTF("Floor",scene);
    floor->AddPhysics(scene->physics_world);
    if (Physics* physics = floor->GetPhysics()){
        physics->AddBoxCollider(vec3(4.0,0.4,4.0),vec3(0,0,0),quat().identity());
        physics->SetStatic(true);
    }
    //Create a copy
    floor = new Object(floor);
    floor->SetPosition(vec3(2,0,0)) ;

    scene->AddObject(floor);


    //Add's all remaining unloaded objects
    GetAllAssetsFromGLTF();

    dozer = new DozerCharacter(assetmanager,scene->physics_world,scene,rrand);
    dozer->soundsystem = soundsystem;
    scene->AddObject(dozer);
    if (dozer->armobject){
        scene->AddObject(dozer->armobject);
    }

    Animation* animation = gltfloader.LoadAnimation("EngineIdle");

    if (animation){
        debug->Ok("Loaded EngineIdle Animation from file.\n");
        dozer->AddAnimation(animation);
    }


    return scene;
}