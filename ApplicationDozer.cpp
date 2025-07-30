#include "ApplicationDozer.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationDozer", DEBUG_ALL);

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

    //Renderer settings
    app->renderer->alpha_clip = 0.5f;
    app->renderer->f_render_skybox = false;

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    app->assetmanager = new AssetManager();

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
        //RunLogic() at a completely different time interval also modifies state_physics.
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
    main_window = Window::CreateNewWindow(800,600,&Window::wcs.at(0));
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

//Called before update physics
void ApplicationDozer::RunLogic(){

}

void ApplicationDozer::UpdateUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("This application has a ImGUI window.");
    ImGui::End();

    RenderDebugMenuBar();
    RenderGenericObjectUI();
}

Scene* ApplicationDozer::CreateMainScene(){
    Scene* scene = CreateNewScene("Dozer Test Scene");

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

    //Load from a GLTF file and build assets.
    gltfloader.LoadGLTFFile("data/dozer.glb");

    GetAllAssetsFromGLTF();


    return scene;
}