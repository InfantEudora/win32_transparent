#include <winsock2.h>
#include "glad.h"

#include "Application.h"
#include "OBJLoader.h"
#include "MCPServer.h"

#include "Window.h"
#include "Renderer.h"

#include "tinygltf/json.hpp"
using json = nlohmann::json;

static Debugger *debug = new Debugger("Application", DEBUG_ALL);

Application::Application(){
    //Init OpenGL.
    //Show some kind of loading screen and load stuff from disk.
    //Maybe we need some kind of way to get each app to get they own makefile.
    SetupConsole();

    //Get the current thread ID this application was called in:
    thread_id_main = GetCurrentThreadId();
    debug->Info("WinMain Thread ID: %lu\n",thread_id_main);

    //TODO: This only needs to be done once.
    Window::RegisterWindowClasses();

    tmr_physics = new PerfTimer("Physics Time");
    tmr_physics_loop = new PerfTimer("Physics Loop Time");
    tmr_physics_sleep = new PerfTimer("Physics Sleep Time");
    tmr_render_loop = new PerfTimer("Render Loop Time");
};

int2 Application::GetDisplaySettings(){
    DWORD       iMode = 0;
    BOOL	    res = true;
    DEVMODEA    devmode;

    //This would list all the supported setting for whatever the current display is.
    while(0 && res){
        res = EnumDisplaySettings(NULL, iMode++, &devmode);
        if (res){
            debug->Info("%d x %d, %d bits %d Hz\n", devmode.dmPelsWidth,devmode.dmPelsHeight, devmode.dmBitsPerPel, devmode.dmDisplayFrequency);
        }
    }

    res = EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devmode);
    if (res){
        debug->Info("Current Display Settings: %d x %d, %d bits %d Hz\n", devmode.dmPelsWidth,devmode.dmPelsHeight, devmode.dmBitsPerPel, devmode.dmDisplayFrequency);
    }
    int2 dimensions = {(int)devmode.dmPelsWidth,(int)devmode.dmPelsHeight};
    return dimensions;
}

void Application::Start(void){
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

void Application::UpdateInput(){
    if (!main_scene){
        return;
    }
    main_scene->UpdateInput();
}

int Application::Exit(void){
    return 1;
}

bool Application::SetupConsole(){
    //Used to do things from console, like CTRL+C
    if (SetConsoleCtrlHandler((PHANDLER_ROUTINE)Application::ConsoleHandler,TRUE)==FALSE){
        debug->Err("Unable to install a console handler!\n");
        return false;
    }
    return true;
}

bool WINAPI Application::ConsoleHandler(DWORD console_event){
    switch(console_event){
        case CTRL_C_EVENT:
            debug->Ok("Shutting down by CTRL+C\n");
            ExitProcess(1);
        break;
    }
    return true;
}

void Application::Init(){
     //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    renderer->Init();
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_scene = new Scene();
    main_scene->renderer = renderer;
    main_scene->inputcontroller = main_window->inputcontroller;
    main_scene->shader = default_shader;

    BinaryAsset::DumpBinaryAssets();

    //Just so the current items show on the first frame...?
    main_scene->UpdatePhysics(1.0f / physics_tps * physics_time_factor);
}

void Application::DrawImGuiUI(){
    return;
}

//Function for rendering the frame to a window
DWORD WINAPI Application::FrameThreadFunction(LPVOID lpParameter){
    Application* app = static_cast<Application*>(lpParameter);
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

    app->Init();

    //Only now, after the concrete app's Init() has fully returned (and so
    //registered every MCPServer::Get()->RegisterTool() call it makes - see
    //ApplicationTank::RegisterMCPTools), start accepting MCP requests.
    //Init() is where per-app tools get registered, not the constructor, and
    //it runs here on the render thread, potentially taking several seconds
    //(asset/shader loading) - starting the MCP server any earlier races an
    //MCP client's initial tools/list against that registration, resulting in
    //only the built-in "status" tool ever being returned.
    //
    //Both transports are started unconditionally for every app - stdio for
    //clients that want to spawn+own the process, HTTP for a client that just
    //wants to attach to (and detach from) an already-running, user-visible
    //instance without touching its lifetime. If the HTTP port is already
    //taken (e.g. another instance of this app is already running), that
    //transport just logs an error and stays off; stdio still works.
    MCPServer::Get()->Start();
    MCPServer::Get()->StartHttp(8765);

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
        app->tmr_render_loop->Stop();
        app->tmr_render_loop->Restart();
        if (app->main_window->f_resized){
            app->main_window->f_resized = false;
            app->renderer->Resize(app->main_window->width,app->main_window->height);
        }
        app->DrawFrame();
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}

void Application::DrawFrame(){
    //Tell ImGui to start a new frame
    main_window->ImGuiNewFrame();

    //This should render the objects and whatever it wants
    if (main_scene){
        main_scene->DrawFrame();
    }

    //Overlay ImGui
    //This will access and modify physics, globally... all over the place.
    renderer->physics_mutex.lock();
    DrawImGuiUI();
    renderer->physics_mutex.unlock();

    //Finish ImGui
    main_window->ImGuiDrawFrame();

    //Copy to screen and finish
    main_window->DrawFrame();
}

DWORD WINAPI Application::PhysicsThreadFunction(LPVOID lpParameter){
    DWORD thread_id = GetCurrentThreadId();
    debug->Info("Output from PhysicsThread Thread ID: %lu\n",thread_id);

    Application* app = static_cast<Application*>(lpParameter);
    if (!app){
        debug->Err("No application was supplied to FrameThread\n");
        return 0;
    }

    //Setup debugging to run from this thread:
    app->debug_physics = new Debugger("App.Physics", DEBUG_ALL);

    uint32_t physics_ticks = 0;
    double us_looptime_desired = app->physics_us_per_tick;
    double last_sleep = 0;
    while (1){
        if (app->main_scene){
            app->UpdateInput();

            //Time spent on aquiring a lock
            app->renderer->physics_mutex.lock();

            //Time spent on logic + physics
            app->tmr_physics->Restart();
            app->UpdateAnimations();
            app->RunLogic();
            app->UpdatePhysics();
            app->tmr_physics->Stop();
            app->renderer->physics_mutex.unlock();

            double us_loop = app->tmr_physics_loop->Stop();
            app->tmr_physics_loop->Restart();
            //debug->Info("Physics Looptime was %f us including %f sleeping\n",us_loop,last_sleep);
            double us_sleep = us_looptime_desired - us_loop;

            double newsleep = clamp(last_sleep + us_sleep,0,us_looptime_desired);
            //debug->Info("Sleeping for additional %f us totalling %f\n",us_sleep,newsleep);

            app->tmr_physics_sleep->Restart();
            timeBeginPeriod(1);
            Sleep(newsleep / 1000.0f);
            timeEndPeriod(1);
            last_sleep = app->tmr_physics_sleep->Stop();

            app->NextInput();
        }else{
            timeBeginPeriod(1);
            Sleep(5);
            timeEndPeriod(1);
            debug->Warn("No main scene for physics thread to work on!\n");
        }
        //debug->Ok("Physics Loop %lu completed\n",physics_ticks);
        physics_ticks++;
    }
    debug->Info("Thread terminated\n");
    return 0;
}

//Called after input update before update physics to run something...?
void Application::RunLogic(){
    return;
}

void Application::UpdateAnimations(){
    if (!main_scene){
        return;
    }
    main_scene->UpdateAnimations();
}

void Application::UpdatePhysics(){
    if (!main_scene){
        return;
    }
    main_scene->UpdatePhysics(1.0f / physics_tps * physics_time_factor);
}

void Application::NextInput(){
    if (!main_scene){
        return;
    }
    if (!main_scene->inputcontroller){
        return;
    }
    main_scene->inputcontroller->Tick();
}

void Application::UpdateUICameraControls(Camera* camera,int id){
    if (!camera){
        return;
    }

    std::string title = camera->name + "##" + std::to_string(id) +   " Camera Controls";

    if (ImGui::CollapsingHeader(title.c_str())){
        float znear = main_scene->camera->viewport.znear;
        if (ImGui::DragFloat("Camera ZNear",&znear,0.01,0.0,10.0)){
            camera->viewport.znear = znear;
            camera->CalculateLookatMatrix();
        }

        float roll = 0;
        if (ImGui::DragFloat("Drag to Roll Camera",&roll,0.01,-1,1)){
            camera->RollBy(roll);
        }

        vec3 up = camera->GetUp();
        vec3 forward = camera->GetForward();
        vec3 left = camera->GetLeft();
        vec3 camera_position = camera->GetPosition();
        if (ImGui::DragFloat3("Cam Position", (float*)&camera_position, 0.01f, -10.0f, 10.0f)){
            camera->SetPosition(camera_position);
            camera->CalculateLookatMatrix();
        }

        static vec3 target;
        if (ImGui::DragFloat3("Target", (float*)&target, 0.01f, -10.0f, 10.0f)){
            camera->SetLookAt(target);
        }
        ImGui::BeginDisabled();
        ImGui::DragFloat3("Forward Vector", (float*)&forward, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Up Vector", (float*)&up, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Left Vector", (float*)&left, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
        if (camera->type == CAMERA_TYPE_PERSPECTIVE){
            if (ImGui::DragFloat("FOV", (float*)&camera->viewport.fov, 0.1f, 0.0f, 180.0f)){
                camera->CalculateLookatMatrix();
            }
            if (ImGui::Button("Swith to Orthographic")){
                camera->SetType(CAMERA_TYPE_ORTHOGRAPHIC);
            }
        }else{
            if (ImGui::DragFloat("Zoom", (float*)&camera->viewport.zoom, 0.1f, 0.0f, 100.0f)){
                camera->CalculateLookatMatrix();
            }
            if (ImGui::Button("Swith to Perspective")){
                camera->SetType(CAMERA_TYPE_PERSPECTIVE);
            }
        }
    }
}

void Application::UpdateUISceneObjectTree(Scene* scene){
    if (ImGui::TreeNode("Scene Root")){
        for (Object* object:scene->renderer->objects){
            UpdateUISceneObjectTreeNode(object,NULL);
        }
        ImGui::TreePop();
    }
}

//Renders all things related to world physics
void Application::UpdateUIWorldPhysics(PhysicsWorld* physics_world){
    if (!physics_world){
        ImGui::BeginDisabled();
        ImGui::CollapsingHeader("No World Physics");
        ImGui::EndDisabled();
        return;
    }

    if (ImGui::CollapsingHeader("World Physics")){
        bool ph_debug_render = physics_world->IsDebugRenderingEnabled();
        if (ImGui::Checkbox("Render Colliders [Debug]",&ph_debug_render)){
            physics_world->SetDebugRendering(ph_debug_render);
        }
        bool ph_paused = main_scene->IsPhysicsPaused();
        if (ImGui::Checkbox("Pause Physics (Active Scene) [Debug]",&ph_paused)){
            main_scene->PausePhysics(ph_paused);
        }
        static int step_count = 1;
        ImGui::BeginDisabled(!ph_paused);
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##PhysicsStepCount",&step_count);
        if (step_count < 1) step_count = 1;
        ImGui::SameLine();
        if (ImGui::Button("Step Physics")){
            main_scene->StepPhysics(step_count);
        }
        ImGui::EndDisabled();
        int pending_steps = main_scene->GetPendingPhysicsSteps();
        if (pending_steps > 0){
            ImGui::SameLine();
            ImGui::Text("(%i pending)",pending_steps);
        }
        vec3 gravity = physics_world->GetGravity();
        if (ImGui::DragFloat3("Gravity (m/s^2)",(float*)&gravity,0.1f,-20,20)){
            physics_world->SetGravity(gravity);
        }
        if (ImGui::DragFloat("Global Time Factor",&physics_time_factor,0.01f,0.1f,2.0f)){
            //Nothing to do here, it's applied in the physics update loop.
        }
        float tps = physics_tps;
        if (ImGui::DragFloat("Target Physics TPS",&tps,1.0f,1.0f,200.0f)){
            SetPhysicsTPS(tps);
        }
    }

}
//Renders all things related to world physics
void Application::UpdateUIPhysics(Physics* physics){

}

void Application::UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked){
    objectid_t id = object->GetID();
    long long p = id; //To suppress warning from 32-bit pointer
    Object* child = object->GetChild(0);
    if (child){
        if (ImGui::TreeNodeEx((void*)p,0 , "Object #%i - %s",id,object->name.c_str())){
            if (ImGui::IsItemClicked() && (lastclicked == NULL)){
                selected_object = object;
                lastclicked = object;
                debug->Info("Tree -> Selected %s\n",object->name.c_str());
            }
            for (Object* child:object->children){
                UpdateUISceneObjectTreeNode(child,lastclicked);
            }
            ImGui::TreePop();
        }
    }else{
        if (ImGui::TreeNodeEx((void*)p,ImGuiTreeNodeFlags_Bullet , "Object #%i - %s",id,object->name.c_str())){
            if (ImGui::IsItemClicked() && (lastclicked == NULL)){
                selected_object = object;
                lastclicked = object;
                debug->Info("Tree -> Selected %s\n",object->name.c_str());
            }
            ImGui::TreePop();
        }
    }
}

void Application::RenderDebugMenuBarClass(){
    return;
}

void Application::RenderDebugMenuBar(){
    if (ImGui::BeginMainMenuBar()){
        if (main_scene && ImGui::BeginMenu("Add Object")){
            if (ImGui::MenuItem("Empty")){
                Object* empty = new Object();
                main_scene->AddObject(empty);
            }
            if (ImGui::MenuItem("Camera")){
                Camera* camera = new Camera();
                camera->name = "New Camera";
                main_scene->AddObject(camera);
                if (assetmanager && assetmanager->GetObjectFromAsset("editor_camera",camera)){
                    camera->SetPosition(vec3(1,2,1));
                    camera->material_slot[0] = 3;

                }
                camera->SetLookAt(vec3());
                camera->SetupPerspective(main_scene->renderer->width,main_scene->renderer->height,45,0.1,100);
            }
            if (ImGui::MenuItem("DirectionalLight")){
                DirectionalLight* l = new DirectionalLight();
                l->name = "Directional Light";
                main_scene->AddObject(l);
            }
            if (ImGui::MenuItem("PointLight")){
                PointLight* l = new PointLight();
                l->name = "Point Light";
                main_scene->AddObject(l);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Objects From Assets")){
                if (!assetmanager){
                    ImGui::MenuItem("-- NO ASSET MANAGER --");
                }else{
                    for (Asset* asset:assetmanager->assets){
                        if (ImGui::MenuItem(asset->name.c_str())){
                            Object* object = assetmanager->GetObjectFromAsset(asset->name.c_str());
                            object->name = asset->name;
                            main_scene->AddObject(object);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("PlayerCharacter(Skeleton) From Loaded GLTF")){
                //Use of assetmanager is optional, can be NULL. It's only used to load debug bones.
                //Skeletons need to be loaded as skin in GLTF. List skins currently open in GLTF loader.
                std::vector<std::string>skeleton_names = gltfloader.GetSkeletonNames();
                if (skeleton_names.size() == 0){
                    ImGui::MenuItem("-- NO SKELETON IN GLTF --");
                }else{
                    for (std::string name:skeleton_names){
                        if (ImGui::BeginMenu(name.c_str())){
                            //We list all the skinned meshes from the current file.
                            std::vector<std::string>skinned_meshes = gltfloader.GetSkinnedMeshNames();
                            for (std::string skinned_mesh_name:skinned_meshes){
                                if (ImGui::MenuItem(skinned_mesh_name.c_str())){
                                    PlayerCharacter* character = new PlayerCharacter();
                                    Skeleton* skeleton = dynamic_cast<Skeleton*>(character);
                                    gltfloader.GetSkeleton(name.c_str(),assetmanager,skeleton);
                                    if (skeleton){
                                        std::vector<Material>loaded_materials;
                                        Mesh* skinned_mesh = gltfloader.GetMeshFromNode(skinned_mesh_name.c_str(),&loaded_materials,true);
                                        skeleton->SetMesh(skinned_mesh);
                                        skeleton->TakeMaterialNames(loaded_materials);
                                        skeleton->PickMaterials(loaded_materials,main_scene->renderer->materials);
                                        main_scene->AddObject(character);
                                        //In order to apply animations to anyting, there needs to be a
                                        //root bone name set.
                                        character->root_bone_name = "Mixamorig:Hips";
                                    }
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                }
                ImGui::EndMenu();
            }


            ImGui::EndMenu();
        }
        if (main_scene && ImGui::BeginMenu("Load Animation")){
            if (ImGui::BeginMenu("Skeleton Animations From Loaded GLTF")){
                std::vector<std::string>animation_names = gltfloader.GetAnimationNames();
                if (animation_names.size() == 0){
                    ImGui::MenuItem("-- NO ANIMATIONS IN GLTF --");
                }else{
                    for (std::string name:animation_names){
                        if (ImGui::MenuItem(name.c_str())){
                            Animation* animation = gltfloader.LoadAnimation(name.c_str());
                            if (selected_object){
                                selected_object->AddAnimation(animation);
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")){
            if (ImGui::MenuItem("Set Always on Top")){
                SetWindowPos(main_window->hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            if (ImGui::MenuItem("Set Normal")){
                SetWindowPos(main_window->hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")){
            std::map<std::string, Debugger*>* handles = debug->GetHandles();
            std::map<std::string,Debugger*>::iterator it = handles->begin();
            const char* items[] = { "TRACE", "INFO", "WARN", "ERROR"};
            static std::vector<int>current_items;
            if (current_items.size() != handles->size()){
                current_items.resize(handles->size());
            }
            for (int i=0;i<handles->size();i++){
                 if (ImGui::BeginMenu(it->first.c_str())){
                    static bool enabled = true;
                    ImGui::MenuItem("Enabled", "", &enabled);
                    ImGui::InputInt("Input", &it->second->level, 1);
                    if (it->second->level >= DEBUG_TRACE){
                        current_items.at(i) = 0;
                    }
                    if (it->second->level >= DEBUG_INFO){
                        current_items.at(i) = 1;
                    }

                    if (ImGui::Combo("Levels", &current_items.at(i), items, IM_ARRAYSIZE(items))){
                        if (current_items.at(i) == 0){
                            it->second->SetLevel(DEBUG_TRACE);
                        }else if (current_items.at(i) == 1){
                            it->second->SetLevel(DEBUG_INFO);
                        }else if (current_items.at(i) == 2){
                            it->second->SetLevel(DEBUG_WARN);
                        }else if (current_items.at(i) == 3){
                            it->second->SetLevel(DEBUG_ERROR);
                        }
                    }
                    ImGui::Text("The lower the level, the more info get's printed");

                    ImGui::EndMenu();
                }
                it++;
            }
            ImGui::EndMenu();
        }

        if (main_scene && ImGui::BeginMenu("Export Scene")){
            if (ImGui::MenuItem("Scene Objects to export.json")){
                FILE* f = fopen("export.json", "w");
                if (f){
                    fprintf(f, "{\n  \"objects\": [\n");
                    bool first = true;
                    for (Object* object : main_scene->renderer->objects){
                        vec3 pos = object->GetPosition();
                        quat rot = object->GetRotation();
                        if (!first) fprintf(f, ",\n");
                        fprintf(f, "    { \"name\": \"%s\", \"position\": [%.4f, %.4f, %.4f], \"rotation\": [%.4f, %.4f, %.4f, %.4f] }",
                                object->name.c_str(),
                                pos.x, pos.y, pos.z,
                                rot.x, rot.y, rot.z, rot.w);
                        first = false;
                    }
                    fprintf(f, "\n  ]\n}\n");
                    fclose(f);
                    debug->Info("Exported %zu objects to export.json\n", main_scene->renderer->objects.size());
                }else{
                    debug->Err("Failed to open export.json for writing\n");
                }
            }
            ImGui::EndMenu();
        }

        //Render class spcific menu bar things
        RenderDebugMenuBarClass();

        ImGui::EndMainMenuBar();
    }
}

void Application::RenderShaderUI(Shader* shader){
    if (!shader){
        return;
    }
    ImGui::Begin("Shader UI");

        GLint shaderprog_id = shader->progid;
        GLint count = 0;
        GLint size; // size of the variable
		GLenum type; // type of the variable (float, vec3 or mat4, etc)

		GLchar name[128] = {}; // variable name in GLSL
		GLsizei length; // name length

		glGetProgramiv(shaderprog_id, GL_ACTIVE_UNIFORMS, &count);
        ImGui::Text("vert file       : %s\n", shader->vname.c_str());
        ImGui::Text("frag file       : %s\n", shader->fname.c_str());

		ImGui::Text("Shader ID       : %i\n", shaderprog_id);
		ImGui::Text("Active Uniforms : %i\n", count);

        for (int i = 0; i < (int)count; i++){

            glGetActiveUniform(shaderprog_id, (GLuint)i, 128, &length, &size, &type, name);
            if (type == GL_INT){
				int v = 0;
				//glGetnUniformiv(progid,i,1*sizeof(GLint),&v);
				if(ImGui::DragInt(name,&v, 1,-10,10)){
					//glUseProgram(progid);
					//Setint(name,&v);
				}
            }else if (type == GL_FLOAT){
				float v = 0;
				//glGetnUniformfv(progid,i,1*sizeof(GLfloat),&v);
				if(ImGui::DragFloat(name,&v, 0.01f,-10,100,"%.6f")){
					//glUseProgram(progid);
					//SetFloat(name,&v);
				}
            }else{
				ImGui::Text("Uniform #%i Type: %X Name: %s\n", i, type, name);
			}
        }

    ImGui::End();
}

void Application::RenderApplicationUI(){
    //For generic Objects and parameters
    ImGui::Begin("Generic Object UI");
    if (ImGui::CollapsingHeader("Application")){
        if (main_scene){
            ImGui::Text("Main Scene             : %s",main_scene->name.c_str());
        }else{
            ImGui::Text("Main Scene             : NULL");
        }
        ImGui::Separator();
        ImGui::Text("Scenes");
        for (Scene* scene:scenes){
            ImGui::Text("Scene             : %s",scene->name.c_str());
            UpdateUISceneObjectTree(scene);
        }
    }

    //So the same camera panel has a different ImGUI ID.
    int ui_camid = 0;
    if (main_scene){
        UpdateUICameraControls(main_scene->camera ,ui_camid);
        UpdateUIWorldPhysics(main_scene->physics_world);
    }


    if (ImGui::CollapsingHeader("Input")){
        ImGui::Text("Window In Focus          : %s",main_window->f_has_focus ? "True" : "False");
        ImGui::Text("Mouser Over Window       : %s",main_window->inputcontroller->IsMouseOverWindow() ? "True" : "False");
        ImGui::Text("ImGui.WantCaptureMouse   : %s",ImGui::GetIO().WantCaptureMouse ? "True" : "False");

        vec3 hov_normal = main_scene->inputcontroller->GetHoveredNormal();
        ImGui::Text("Normal at mouse   : %.3f, %.3f, %.3f",hov_normal.x,hov_normal.y,hov_normal.z);

        if (!hovered_object){
            ImGui::Text("No Object Hovered");
        }else{
            ImGui::Text("Hovered Object Name: %s",hovered_object->name.c_str());
        }
    }


    if (ImGui::CollapsingHeader("Performance")){
        bool sync = renderer->GetVSync();
        if (ImGui::Checkbox("V-Sync", &sync)){
            renderer->SetVSync(sync);
        }
        ImGui::Text("Renderer Time : %8.1f us  (%5.2f ms)", renderer->tmr_frame->avg,renderer->tmr_frame->avg/1000.0f);
        ImGui::Text("Frame Time    : %8.2f FPS (%5.2f ms)", 1000000.0f/tmr_render_loop->avg,tmr_render_loop->avg/1000.0f );

        ImGui::Text("Physics Loop  : %8.2f TPS (%5.2f ms)", 1000000.0f/tmr_physics_loop->avg,tmr_physics_loop->avg/1000.0f );
        ImGui::Text("Physics Sleep : %8.1f us  (%5.2f ms)", tmr_physics_sleep->avg,tmr_physics_sleep->avg/1000.0f );
        ImGui::Text("Physics Time  : %8.1f us  (%5.2f ms)", tmr_physics->avg,tmr_physics->avg/1000.0f );

        ImGui::Text("Scene - Renderable Objects  : %i", main_scene->renderer->renderable_objects.size());
        ImGui::Text("Scene - Unique Meshes       : %i", main_scene->renderer->unique_meshes.size());
        ImGui::Text("Scene - Batches             : %i", main_scene->renderer->unique_mesh_batches.size());
    }

    if (ImGui::CollapsingHeader("Renderer")){
        ImGui::Text(    "Normal Mapping     :");ImGui::SameLine();
        ImGui::Checkbox("##1", &renderer->f_normal_mapping);
        ImGui::Text(    "SSAO               :");ImGui::SameLine();
        ImGui::Checkbox("##2", &renderer->f_ssao);
        ImGui::Text(    "Render Skybox      :");ImGui::SameLine();
        ImGui::Checkbox("##3", &renderer->f_render_skybox);
        ImGui::Text(    "Render Reflections :");ImGui::SameLine();
        ImGui::Checkbox("##4", &renderer->f_use_reflections);

        int view_buffer = renderer->view_buffer;
        if (ImGui::SliderInt("View Buffer      : ",&view_buffer,0,8)){
            renderer->SelectViewBuffer(view_buffer);
        }

        int num_samples = renderer->aa_samples;
        if (ImGui::SliderInt("MSAA Num Samples : ",&num_samples,1,16)){
            renderer->SetNumAASamples(num_samples);
        }

        if (ImGui::SliderFloat("Alpha Clip     : ",&renderer->alpha_clip,0.0f,1.0f)){

        }
    }

    if (ImGui::CollapsingHeader("Window")){
        ImGui::Text(    "Current Size   : %i x %i", main_window->width,main_window->height);
    }

    if (ImGui::CollapsingHeader("Assets")){
        for (Asset* asset: assetmanager->assets){
            ImGui::Text("Asset  : %s", asset->name.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Materials")){
        ImGui::Text(    "Num Materials  : %i", renderer->GetNumMaterials());
        int n =0;
        for (Material& material: renderer->materials){
            ImGui::Text("Material  : %s", material.name.c_str());
            ImGui::Text("GLSL Material Properties");
            ImGui::Text(" diffuse_texture  : %i", material.glsl_material.diffuse_texture);

            ImGui::PushID(n++);
            ImGui::DragFloat(" Metallic", (float*)&material.glsl_material.metallic,0.01f,0,1);
            ImGui::DragFloat(" Roughness", (float*)&material.glsl_material.roughness,0.01f,0,1);
            ImGui::DragFloat(" Brightness", (float*)&material.glsl_material.brightness,0.01f,0,10);
            ImGui::ColorEdit4(" GLSL Color", (float*)&material.glsl_material.color, ImGuiColorEditFlags_DisplayRGB);
            ImGui::PopID();

        }
    }

    if (ImGui::CollapsingHeader("[TEST] Ray - Plane Intersection")){
        plane& p = projection_plane;

        int2 px = main_scene->inputcontroller->GetRelativeMousePosition();

        ray r = main_scene->camera->GetPixelRay(px);
        ImGui::BeginDisabled();
        ImGui::DragInt2("Mouse Position", (int*)&px, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Ray Origin", (float*)&r.origin, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Ray Direction", (float*)&r.direction, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
        ImGui::Separator();


        ImGui::DragFloat3("Plane Origin", (float*)&p.pos, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Plane Normal", (float*)&p.normal, 0.01f, -1.0f, 1.0f);

        vec3 at = {};
        bool intersect = r.intersects_plane(p,at);

        if (intersect){
            ImGui::DragFloat3("Intersection at", (float*)&at, 0.01f, -1.0f, 1.0f);
            //Move the object there?
            if (selected_object){
                selected_object->SetPosition(at);
            }
        }else{
            ImGui::Text("No intersection");
        }
    }

    RenderSelectedObjectUI(selected_object, ui_camid);


    ImGui::End();
}

//Renders a set of collapsing headers for supplied object
void Application::RenderSelectedObjectUI(Object* object, int ui_camera_id){

    ImGui::Separator();
    if (object == NULL){
        ImGui::Text("No Object is Selected");
        return;
    }
    ImGui::Text("Selected Object");
    ImGui::Separator();
    ImGui::Text("Object Name     : %s",object->name.c_str());
    ImGui::Text("Object ID       : %lu",object->GetID());

    if (object->GetParent()){
        ImGui::Text("Parent          : ID: %lu Name: %s",object->GetParent()->GetID(),object->GetParent()->name.c_str());
        if (ImGui::Button("Select Root Node")){
            Object* o = object->GetParent();
            while(o->GetParent()){
                o = o->GetParent();
            }
            selected_object = o;
        }
    }else{
        ImGui::Text("Parent          : Has No Parent");
    }
    ImGui::Text("Children        : %i",object->GetNumChildren());

    bool obj_visible = object->IsVisible();
    if (ImGui::Checkbox("Visible",&obj_visible)){
        object->SetVisibility(obj_visible);
    }

    if (ImGui::Button("Duplicate(Linked)")){
        Object* duplicated = new Object(object);
        main_scene->AddObject(duplicated);
        Physics* physics = duplicated->GetPhysics();
        if (physics){
            physics->SetActive(false); //Helps with placement
        }
        selected_object = duplicated;
    }
    ImGui::SameLine();
    if (ImGui::Button("Destroy")){
        object->Destroy();
        Physics* physics = object->GetPhysics();
        if (physics){
            physics->world->WakeUpEveryone();
        }
        selected_object = NULL;
    }

    Camera* cam = dynamic_cast<Camera*>(object);
    if (cam){
        UpdateUICameraControls(cam,++ui_camera_id);
    }

        Light* light = dynamic_cast<Light*>(object);
        if (light && ImGui::CollapsingHeader("Light Properties")){
            vec3 pos = object->GetPosition();
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Position", (float*)&pos, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
            ImGui::DragFloat3("Color", (float*)&light->color, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Brightness", (float*)&light->brightness, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Shadow Bias", (float*)&light->shadow_bias, 0.0001f, 0.0f, 1.0f);
        }

        if (object->GetMesh()){
            if (ImGui::CollapsingHeader("Mesh")){
                Mesh* mesh = object->GetMesh();
                ImGui::Text(" ID                : %lu",mesh->GetID());
                mesh->IsSkinnedMesh() ? ImGui::Text(" IsSkinned         : Yes") : ImGui::Text(" IsSkinned         : No");
                ImGui::Text(" num_vertices      : %lu",mesh->num_vertices);
                ImGui::Text(" num_materials     : %lu",mesh->num_materials);
                ImGui::Text(" num_references    : %lu",mesh->num_references);
                ImGui::Text(" num_morph_targets : %lu",mesh->num_morph_targets);
            }
        }else{
            ImGui::BeginDisabled();
            ImGui::CollapsingHeader("No Mesh");
            ImGui::EndDisabled();
        }

        Bone* bone = dynamic_cast<Bone*>(object);
        if (bone){
            if (ImGui::CollapsingHeader("Bone")){
                ImGui::Text("bone_index          : %i",bone->bone_index);
                ImGui::Text("bone_unpacked_index : %i",bone->bone_unpacked_index);
                ImGui::Text("node_index          : %i",bone->node_index);
                ImGui::Text("initial_length      : %.2f",bone->initial_length);
                ImGui::Text("inverse_bind_matrix : ");
                ImGui::BeginDisabled();
                ImGui::DragFloat4("V1", (float*)&bone->inverse_bind_matrix.vertex[0], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V2", (float*)&bone->inverse_bind_matrix.vertex[1], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V3", (float*)&bone->inverse_bind_matrix.vertex[2], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V4", (float*)&bone->inverse_bind_matrix.vertex[3], 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
                fmat4 m = bone->GetWorldTransformScaleMatrix();
                ImGui::Text("world_transform_scale_matrix : ");
                ImGui::BeginDisabled();
                ImGui::DragFloat4("V1", (float*)&m.vertex[0], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V2", (float*)&m.vertex[1], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V3", (float*)&m.vertex[2], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V4", (float*)&m.vertex[3], 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
                m.inverse_transform();
                ImGui::Text("world_transform_scale_matrix.inverse_transform() : ");
                ImGui::BeginDisabled();
                ImGui::DragFloat4("V1", (float*)&m.vertex[0], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V2", (float*)&m.vertex[1], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V3", (float*)&m.vertex[2], 0.01f, -1.0f, 1.0f);
                ImGui::DragFloat4("V4", (float*)&m.vertex[3], 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
            }
        }

        if (ImGui::CollapsingHeader("Position")){
            vec3 delta = {0,0,0};
            if (ImGui::DragFloat3("Move Position", (float*)&delta, 0.01f, -1.0f, 1.0f)){
                object->MoveBy(delta);
            }

            vec3 pos = object->GetPosition();
            if (ImGui::DragFloat3("Position", (float*)&pos, 0.01f, -1.0f, 1.0f)){
                object->SetPosition(pos);
            }


            float forward = 0.0f;
            if (ImGui::DragFloat("Move Forward/Backward", (float*)&forward, 0.01f, -1.0f, 1.0f)){
                object->MoveForwardBy(forward);
            }
            float left = 0.0f;
            if (ImGui::DragFloat("Move Left/Right", (float*)&left, 0.01f, -1.0f, 1.0f)){
                object->MoveSidewaysBy(left);
            }
            float up = 0.0f;
            if (ImGui::DragFloat("Move Up/Down", (float*)&up, 0.01f, -1.0f, 1.0f)){
                object->MoveUpBy(up);
            }
        }
        if (ImGui::CollapsingHeader("Rotation")){
            static int option = 0;
            ImGui::Text("Input By:");
            ImGui::RadioButton("None", &option, 0); ImGui::SameLine();
            ImGui::RadioButton("Vector + Rotation", &option, 1); ImGui::SameLine();
            ImGui::RadioButton("Target, Position, Up", &option, 2);
            ImGui::RadioButton("Axis Degrees", &option, 3);
            ImGui::Separator();

            ImGui::BeginDisabled();
            quat q = object->GetRotation();
            ImGui::DragFloat4("Current Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();

            bool set_rotation = false;

            float roll_by = 0;
            if (ImGui::DragFloat("Roll By", (float*)&roll_by, 0.01f, -1.0f, 1.0f)){
                object->RollBy(roll_by);
            }
            float pitch_by = 0;
            if (ImGui::DragFloat("Pitch By", (float*)&pitch_by, 0.01f, -1.0f, 1.0f)){
                object->PitchBy(pitch_by);
            }
            float yaw_by = 0;
            if (ImGui::DragFloat("Yaw By", (float*)&yaw_by, 0.01f, -1.0f, 1.0f)){
                object->YawBy(yaw_by);
            }


            if (option == 1){
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

            }else if (option == 2){
                static vec3 target = {0,0,-1};
                if (ImGui::DragFloat3("Target Vector", (float*)&target, 0.01f, -5.0f, 5.0f)){
                    set_rotation = true;
                }
                static vec3 position = {0,0,0};
                if (ImGui::DragFloat3("Position", (float*)&position, 0.01f, -5.0f, 5.0f)){
                    set_rotation = true;
                }
                static vec3 worldup = {0,1,0};
                if (ImGui::DragFloat3("World Up", (float*)&worldup, 0.01f, -1.0f, 1.0f)){
                    set_rotation = true;
                }
                ImGui::BeginDisabled();
                q = quat::getquat(target,position,worldup);
                ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
            }else if (option == 3){
                static vec3 axis_degrees = {0,0,0};
                if (ImGui::DragFloat3("Axis Degrees", (float*)&axis_degrees, 1.0f, -180.0f, 180.0f)){
                    set_rotation = true;
                }
                ImGui::BeginDisabled();
                //Let's do them in order?
                quat q1; q1.set_rotation(vec3(1,0,0),toradians(axis_degrees.x));
                quat q2; q2.set_rotation(vec3(0,1,0),toradians(axis_degrees.y));
                quat q3; q3.set_rotation(vec3(0,0,1),toradians(axis_degrees.z));

                q = q1 * q2 * q3;
                ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
            }

            if (ImGui::Button("Set Rotation")){
                set_rotation = true;
            }
            if (ImGui::Button("Rotate By")){
                object->RotateBy(q);
            }
            if (set_rotation){
                object->SetRotation(q);
            }
        }
        if (ImGui::CollapsingHeader("Scale")){
            vec3 scale = object->GetScale();
            float scale_all = 1.0f;
            if (ImGui::DragFloat("Scale All", (float*)&scale_all, 0.01f, 0.01f, 10.0f)){
                object->SetScale(scale * scale_all);
            }

            if (ImGui::DragFloat3("Scale Vector", (float*)&scale, 0.01f, 0.01f, 10.0f)){
                object->SetScale(scale);
            }
        }

        if (object->GetPhysics()){
            if (ImGui::CollapsingHeader("Physics",ImGuiTreeNodeFlags_DefaultOpen)){
                Physics* physics = object->GetPhysics();
                bool f_static = physics->IsStatic();
                if (ImGui::Checkbox("Static", &f_static)){
                    physics->SetStatic(f_static);
                }
                bool f_gravity = physics->IsGravityEnabled();
                if (ImGui::Checkbox("Reacts to Grativy", &f_gravity)){
                    physics->SetGravityEnabled(f_gravity);
                }
                bool f_active = physics->IsActive();
                if (ImGui::Checkbox("Active", &f_active)){
                    physics->SetActive(f_active);
                }
                bool f_sleeping = physics->IsSleeping();
                if (ImGui::Checkbox("Sleeping", &f_sleeping)){
                    physics->WakeUp();
                }
                int num_colliders = physics->GetNumColliders();
                float mass = physics->GetMass();
                ImGui::Text("Number of colliders : %i",num_colliders);
                for (uint32_t i=0;i<physics->body->rigidbody->getNbColliders();i++){
                    if (physics->body->rigidbody->getCollider(i)->getCollisionShape()->getName() == rp3d::CollisionShapeName::BOX){
                        //Testing. Spawn an object that 'attaches' to the collider of this object so we can modify it.
                        char caption[32];
                        sprintf(caption, "Modify Box Collider %lu",i);
                        if (ImGui::Button(caption)){
                            ObjectCollider* oc = new ObjectCollider();
                            oc->HookTargetCollider(physics->body->rigidbody->getCollider(i));
                            main_scene->AddObject(oc);
                            selected_object = oc;
                        }
                    }
                }
                ImGui::Text("Mass                : %.3f kg",mass);
                ImGui::Text("Collision Cat Bits  : %08X",object->collision_category_bits);
                ImGui::Separator();
                bool bits[8];
                bool cat_wasmodified = false;
                for (int i=0;i<8;i++){
                    bits[i] = !!(object->collision_category_bits & (1<<i));
                    char boxid[32];
                    sprintf(boxid,"##CatBit%i",i);
                    if (ImGui::Checkbox(boxid,&bits[i])){
                        cat_wasmodified = true;
                    }
                    if (i < 7)
                        ImGui::SameLine();
                }
                if (cat_wasmodified){
                    uint32_t mask = 0;
                    for (int i=0;i<8;i++){
                        mask |= (bits[i]<<i);
                    }
                    object->SetCollisionCategoryBits(mask);
                }

                ImGui::Text("Collide Wtih  Bits  : %08X",object->collide_with_bits);

                bool col_wasmodified = false;
                for (int i=0;i<8;i++){
                    bits[i] = !!(object->collide_with_bits & (1<<i));
                    char boxid[32];
                    sprintf(boxid,"##ColBit%i",i);
                    if (ImGui::Checkbox(boxid,&bits[i])){
                        col_wasmodified = true;
                    }
                    if (i < 7)
                        ImGui::SameLine();
                }
                if (col_wasmodified){
                    uint32_t mask = 0;
                    for (int i=0;i<8;i++){
                        mask |= (bits[i]<<i);
                    }
                    object->SetCollideWithMaskBits(mask);
                }

                vec3 v = physics->GetVelocity();
                vec3 a = physics->GetAngularVelocity();
                //ImGui::BeginDisabled();
                if (ImGui::DragFloat3("Velocity", (float*)&v, 0.01f, -1.0f, 1.0f)){
                    physics->SetVelocity(v);
                }
                if (ImGui::DragFloat3("Angular Velocity", (float*)&a, 0.01f, -1.0f, 1.0f)){
                    physics->SetAngularVelocity(a);
                }
                //ImGui::EndDisabled();

                float collider_friction = physics->GetFrictionCoefficient();
                if (ImGui::DragFloat("Friction Coefficient", (float*)&collider_friction, 0.01f, 0.0f, 2.0f)){
                    physics->SetFrictionCoefficient(collider_friction);
                    main_scene->physics_world->WakeUpEveryone();
                }

                float collider_bounciness = physics->GetBounciness();
                if (ImGui::DragFloat("Bounciness", (float*)&collider_bounciness, 0.01f, 0.0f, 1.0f)){
                    physics->SetBounciness(collider_bounciness);
                }

                rp3d::Collider* collider = NULL;


            }
        }else{
            ImGui::BeginDisabled();
            ImGui::CollapsingHeader("No Physics");
            ImGui::EndDisabled();
        }


        if (ImGui::CollapsingHeader("Material")){
            ImGui::Text("Renderer Materials: %i",renderer->materials.size());
            ImGui::Separator();

            ImGui::DragInt("Material Slot 0",&object->material_slot[0],1,-1,20);
            ImGui::DragInt("Material Slot 1",&object->material_slot[1],1,-1,20);
            ImGui::DragInt("Material Slot 2",&object->material_slot[2],1,-1,20);
            ImGui::DragInt("Material Slot 3",&object->material_slot[3],1,-1,20);

            ImGui::Text("Material Name 0 : %s",object->material_names[0].c_str());
            ImGui::Text("Material Name 1 : %s",object->material_names[1].c_str());
            ImGui::Text("Material Name 2 : %s",object->material_names[2].c_str());
            ImGui::Text("Material Name 3 : %s",object->material_names[3].c_str());
        }
        Skeleton* skeleton = dynamic_cast<Skeleton*>(object);
        if (!skeleton){
            ImGui::BeginDisabled();
            ImGui::CollapsingHeader("No Skeleton");
            ImGui::EndDisabled();
        }else{
            if (ImGui::CollapsingHeader("Skeleton")){
                ImGui::Text("Root Bone Name: %s",skeleton->root_bone_name.c_str());
                static char bone_name[64] = {};
                ImGui::InputText("Set Root Bone Name",bone_name, 64);
                if (ImGui::Button("Apply Name")){
                    skeleton->root_bone_name = bone_name;
                }
                std::vector<Bone*> bones;
                skeleton->GetAllBones(skeleton,bones);
                ImGui::Text("Num Bones: %i",bones.size());
                for (Bone* bone:bones){
                    ImGui::Text("Bone Name: %s",bone->name.c_str());
                }

            }
        }

        if (ImGui::CollapsingHeader("Animations")){
            ImGui::Text("Morph Factors");
            ImGui::DragFloat("Target 1",&object->morph_factors[0],0.01,0,1);
            ImGui::DragFloat("Target 2",&object->morph_factors[1],0.01,0,1);
            ImGui::DragFloat("Target 3",&object->morph_factors[2],0.01,0,1);
            ImGui::DragFloat("Target 4",&object->morph_factors[3],0.01,0,1);

            ImGui::DragFloat("Animation Transistion Time Max",&object->animation_transition_time_max,0.01,0,3);

            if (object->animations.size() == 0){
                ImGui::Text("Object has no animations");
            }else{
                ImGui::Text("Object Animations");

                if (ImGui::Button("NULL")){
                    object->SwitchToAnimation(NULL);
                }
                ImGui::SameLine();


                int button_id = 1;
                for (Animation* animation:object->animations){
                    if (ImGui::Button(animation->name.c_str())){
                        AnimationTransition* transition = object->animation_graph ? object->animation_graph->FindTransition(object->current_animation, animation) : NULL;
                        if (transition){
                            //debug->Info("Transition found from %s to %s. Transitioning!\n",transition->from ? transition->from->name.c_str() : "NULL",transition->to ? transition->to->name.c_str() : "NULL");
                            object->TransitionToAnimation(animation,transition);
                        }else{
                            //debug->Info("No transition found from %s to %s. Switching directly.\n",object->current_animation ? object->current_animation->name.c_str() : "NULL",animation->name.c_str());
                            object->SwitchToAnimation(animation);
                        }
                    }
                    button_id++;
                    if (button_id % 4 != 0)
                        ImGui::SameLine();
                }

                if (object->current_animation){
                    ImGui::Text("Current Animation : %s @ %.2f / %.2f",object->current_animation->name.c_str(),object->current_animation->time_index,object->current_animation->duration);
                    bool looping = object->current_animation->looped;
                    if (ImGui::Checkbox("  - Looping",&looping)){
                        object->current_animation->looped = looping;
                    }
                    ImGui::Checkbox("  - Modifies Root Object",&object->current_animation->modifies_root_object);
                }else{
                    ImGui::Text("Current Animation : NULL");
                }
                if (object->current_transition){
                    if (object->current_transition->from){
                        ImGui::Text("Transition->From  : %s @ %.2f / %.2f",object->current_transition->from->name.c_str(),object->current_transition->from->time_index,object->current_transition->from->duration);
                    }else{
                        ImGui::Text("Transition->From  : NULL (Reference Pose)");
                    }
                    if (object->current_transition->to){
                        ImGui::Text("Transition->To    : %s @ %.2f / %.2f",object->current_transition->to->name.c_str(),object->current_transition->to->time_index,object->current_transition->to->duration);
                    }else{
                        ImGui::Text("Transition->To    : NULL (Reference Pose)");
                    }
                }else{
                    ImGui::Text("Transition->From  : NULL");
                    ImGui::Text("Transition->To    : NULL");
                }
                ImGui::Text("Current Animation State : %i\n",object->animation_state);
                ImGui::Text("Desired Animation       : %s\n",object->dbg_desired_animation_name.c_str());
                ImGui::Text("Anim Transition Factor  : %.3f\n",object->animation_transition_factor);

            }

            if (object->animation_graph && object->animation_graph->transitions.size() != 0){
                ImGui::Text("Animation Transitions");
                int id = 0;
                for (AnimationTransition* transition:object->animation_graph->transitions){
                    ImGui::PushID(id++);
                    std::string button_text = transition->from ? transition->from->name : "NULL";
                    ImGui::Button(button_text.c_str());
                    ImGui::SameLine();
                    ImGui::Button(" --> ");
                    ImGui::SameLine();
                    button_text = transition->to ? transition->to->name : "NULL";
                    ImGui::Button(button_text.c_str());
                    ImGui::PopID();
                }
            }
        }

}

//For showing how RRandom would work.
void Application::RenderRandTestWindow(){
    static float histogram_arr[256];
    static int histogram_count = 0;

    ImGui::Begin("Random Test Suite");
    ImGui::Text("This is for testing our own random functions. Neat?");
    if (rrand == NULL){
        ImGui::Text("rrand has not been initialised\n");
        if (ImGui::Button("Generate 512x512")){
            rrand = new RRandom();
            rrand->Generate(512,512);
        }
        ImGui::End();
        return;
    }



    uint8_t r = rrand->Get_uint8();
    float f = 0;
    int s = 1;
    for (int i = 0;i<s;i++){
        f += rrand->Get_uint8();
    }
    f/= s;

    histogram_arr[histogram_count++] = f;//rand() % 256;

    histogram_count = histogram_count % 256;
    //UI


    ImGui::PlotHistogram("Histogram", histogram_arr, IM_ARRAYSIZE(histogram_arr), 0, NULL, 0.0f, 255.0f, ImVec2(0, 80.0f));

    static int rand_int = 0;
    if (ImGui::Button("Get Random Int")){
        rand_int = rrand->GetInt();
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_int);

    static int rand_limit = 0;
    static int minmax[2] = {0,1};
    ImGui::SliderInt2("Int Min / Max",minmax,-100,100);
    if (ImGui::Button("Get Random Int Between")){
        rand_limit = rrand->GetInt(minmax[0],minmax[1]);
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_limit);

    static float rand_float = 0;
    static float fminmax[2] = {0,1};
    ImGui::SliderFloat2("Float Min / Max",fminmax,-100,100);
    if (ImGui::Button("Get Random Float Between")){
        rand_float = rrand->GetFloat(fminmax[0],fminmax[1]);
    }
    ImGui::SameLine();
    ImGui::Text("Random Float: %.3f",rand_float);

    //Normal distribution
    int num_bins = 50;
    static float bins[50] = {};
    int bin_start = -25;
    int bin_size = 1;

    //We sample from our normal distribution and see if they fall in a bin
    int num_samples = 10000;
    float smax = 0;
    static float bmax = 50;
    if (ImGui::Button("Sample Distribution")){
        memset(bins,0,sizeof(float)*num_bins);
        bmax = 50;
        for (int s =0;s<num_samples;s++){
            //float sample = rrand->GetFloat(-10,10);
            float sample = rrand->GetNormalFloat(0,4);
            if (sample > smax){
                smax = sample;
            }
            for (int i=0;i<num_bins;i++){
                //Check if sample is in this bin
                if ((sample > (bin_start + i)) && (sample < (bin_start + i + bin_size))){
                    bins[i]+=1;
                    if (bins[i] > bmax){
                        bmax = bins[i];
                    }
                    break;
                }
            }
        }
        bmax *= 1.1f;
    }

    ImGui::PlotHistogram("Sampled Floats", bins, IM_ARRAYSIZE(bins), 0, NULL, 0.0f, bmax, ImVec2(0, 80.0f));
    ImGui::End();
}


/*
void ApplicationGrid::RenderAnimationUI(){
    ImGui::Begin("Character Animation Sequence UI");

    if (!character){
        ImGui::Text("No character");
        ImGui::End();
        return;
    }

    static float time_index = 0.0f;
    static float lerp = 0.0f;
    static bool f_ondrag = false;
    static bool f_update_hip_pos = false;

    static Animation* animation_lerp_start = NULL;
    static Animation* animation_lerp_end = NULL;
    static float interval_lerp_start = 0.0f;
    static float interval_lerp_end = 0.0f;

    static float manual_time = 0.1;

    if (ImGui::CollapsingHeader("Auto Animation")){


        if (selected_animation){
            ImGui::Text("Selected Animation   : %s",selected_animation->name.c_str());
            if (ImGui::Button("Set as Next")){
                character->SetNextAnimation(selected_animation);
            }
            if (ImGui::Button("Proceed to Next")){
                character->ProceedToNextAnimation();
            }
        }



        if (ImGui::DragFloat("Idle Time Max", (float*)&character->idle_time_max, 0.01f, 0.0f, 10.0f)){

        }
        if (ImGui::DragFloat("Transition Time Max", (float*)&character->transition_time_max, 0.01f, 0.0f, 10.0f)){

        }
        if (ImGui::DragFloat("Animation Time Delta", (float*)&character->animation_time_delta, 0.005f, -1.0f, 1.0f)){

        }
    }

    if (selected_animation){
        if (ImGui::CollapsingHeader("Selected Animation")){
            float animation_duration = selected_animation->duration;
            ImGui::Text("Duration: %.2f",selected_animation->duration);

            ImGui::Checkbox("Modify on Drag",&f_ondrag);

            if (ImGui::DragFloat("Time Index", (float*)&time_index, 0.005f, 0.0f, selected_animation->duration)){
                if (f_ondrag){
                    selected_animation->ApplyInterval(time_index);
                }
            }

            if (ImGui::Button("Apply Interval on All")){
                selected_animation->ApplyInterval(time_index);
            }

            ObjectAnimation* hips_animation = selected_animation->FindObjectAnimation("Hips");
            if (hips_animation){
                if (ImGui::Button("Apply Interval on Hips")){
                    selected_animation->ApplyIntervalOnto(hips_animation, hips_animation->target,time_index);
                }
                if (ImGui::Checkbox("Toggle Position Update on Hips",&f_update_hip_pos)){
                    selected_animation->SetPositionUpdates(hips_animation,f_update_hip_pos);
                }
            }

            if (ImGui::Button("Set as start Lerp animation")){
                animation_lerp_start = selected_animation;
                interval_lerp_start = time_index;
            }
            if (ImGui::Button("Set as end Lerp animation")){
                animation_lerp_end = selected_animation;
                interval_lerp_end = time_index;
            }

            if (animation_lerp_start && animation_lerp_end){
                ImGui::Text("Lerp between animation %s at interval %.2f to animation %s at interval %.2f",animation_lerp_start->name.c_str(),interval_lerp_start,animation_lerp_end->name.c_str(),interval_lerp_end);
                if (ImGui::DragFloat("Lerp", (float*)&lerp, 0.005f, 0.0f, 1.0f)){
                    animation_lerp_start->Lerp(animation_lerp_end,interval_lerp_start,interval_lerp_end,lerp);
                }
            }
        }

    }else{
        ImGui::Text("No animation selected\n");
    }


    ImGui::End();
}

*/

void Application::CheckObjectSelection(){
    hovered_object = NULL;
    InputController* input = main_scene->inputcontroller;

    if (input->IsMouseOverWindow() == false){
        return;
    }

    if (!ImGui::GetIO().WantCaptureMouse){ //Mouse is not over an ImGUI Window
        hovered_objid = input->GetHoveredObjectID();
        if ((hovered_objid == OBJECTID_INVALID) && input->WasKeyReleased(INPUT_CLICK_LEFT)){
            selected_object = NULL;
            return;
        }
    }

    for (Object* object:renderer->renderable_objects){
        if (object->GetID() == hovered_objid){
            hovered_object = object;
        }
        if (input->IsKeyDown(INPUT_CLICK_LEFT) && (object->GetID() == hovered_objid)){
            dragged_objid = hovered_objid;
            //debug->Info("dragged_objid on ID: %3i \n",hovered_objid);
        }else if (input->WasKeyReleased(INPUT_CLICK_LEFT) && (object->GetID() == dragged_objid)){
            vec3 p = object->GetPosition();
            debug->Info("Clicked on ID: %3i Object Pos: %.2f %.2f %.2f\n",hovered_objid,p.x,p.y,p.z);
            selected_object = object;
            dragged_objid = OBJECTID_INVALID;
            //clicked_empty = false;
        }
    }
}

//Creates a new scene with default camera and settings
Scene* Application::CreateNewScene(const std::string& name){
    Scene* scene = new Scene();
    scene->name = name;
    scene->renderer = renderer;
    scene->inputcontroller = main_window->inputcontroller;
    scene->shader = default_shader;

    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    scenes.push_back(scene);
    return scene;
}

//Attempt to load all assets from the assetmanager.
void Application::BuildSceneFromJSON(){
    debug->Info("Building scene from JSON export\n");
    size_t file_data_sz = 0;
    uint8_t* file_data = NULL;  // Data loaded from disk
    file_data = LoadFile("export.json",&file_data_sz);

    auto j1 = json::parse(file_data);
    //Iterate the objects:
    for (json& j_object:j1["objects"]){
        std::string name = j_object["name"].get<std::string>();
        Object* object = assetmanager->GetObjectFromAsset(name.c_str());
        if (object == NULL){
            debug->Err("Could not find asset with name %s\n",name.c_str());
            continue;
        }
        object->name = name;
        auto pos_array = j_object["position"].get<std::vector<float>>();
        if (pos_array.size() == 3){
            object->SetPosition(vec3(pos_array[0],pos_array[1],pos_array[2]));
        }
        auto rot_array = j_object["rotation"].get<std::vector<float>>();
        if (rot_array.size() == 4){
            object->SetRotation(quat(rot_array[0],rot_array[1],rot_array[2],rot_array[3]));
        }
        debug->Info("Loaded object with name %s at position (%.2f, %.2f, %.2f)\n", name.c_str(), object->GetPosition().x, object->GetPosition().y, object->GetPosition().z);
        main_scene->AddObject(object);
    }
}


//Get's the currently loaded GLTF file, and imports only the requested node names that aren't already loaded.
//This has to be called from a thread that owns the OpenGL context.
void Application::GetAssetsFromGLTF(const std::vector<std::string>& names){
    DWORD called_thread_id = -1;
    called_thread_id = GetCurrentThreadId();
    debug->Info("GetAssetsFromGLTF called from ThreadID: %lu\n", called_thread_id);
    if (called_thread_id != thread_id_render){
        debug->Fatal("Should be called from render thread\n");
    }

    if (!assetmanager){
        debug->Err("No assetmanager to load assets into.\n");
    }

    //Materials loaded belonging to a single node
    std::vector<Material>loaded_materials;

    for (const std::string& nodename:names){
        debug->Info("GetAssetsFromGLTF: Node %s\n",nodename.c_str());
        loaded_materials.clear();

        bool already_loaded = false;

        for (Asset* asset:assetmanager->assets){
            if (asset->name.compare(nodename) == 0){
                debug->Err(" -> Already loaded\n");
                already_loaded = true;
                break;
            }
        }

        if (already_loaded){
            continue;
        }

        Mesh* gltfmesh = gltfloader.GetMeshFromNode(nodename.c_str(),&loaded_materials);
        if (gltfmesh){
            Object* gltf_object = new Object();
            gltf_object->name = nodename.c_str();
            gltf_object->SetMesh(gltfmesh);
            gltf_object->TakeMaterialNames(loaded_materials);
            assetmanager->AddNewAsset(nodename.c_str(),gltf_object);
            //Just add all...
            renderer->AddMaterials(loaded_materials);
        } else {
            debug->Err("GetAssetsFromGLTF: Could not find node %s in currently loaded GLTF file\n",nodename.c_str());
        }
    }
}

//Get's the currently loaded GLTF file, and imports everyting that wasn't imported.
//This has to be called from a thread that owns the OpenGL context.
void Application::GetAllAssetsFromGLTF(){
    DWORD called_thread_id = -1;
    called_thread_id = GetCurrentThreadId();
    debug->Info("GetAllAssetsFromGLTF called from ThreadID: %lu\n", called_thread_id);
    if (called_thread_id != thread_id_render){
        debug->Fatal("Should be called from render thread\n");
    }

    if (!assetmanager){
        debug->Err("No assetmanager to load assets into.\n");
    }

    //Materials loaded belonging to a single node
    std::vector<Material>loaded_materials;

    debug->Info("GetAllAssetsFromGLTF: Loading %i nodes\n",gltfloader.node_names.size());
    //Iterate through all nodes that have a mesh, check if we have no asset with that name and load.
    for (std::string& nodename:gltfloader.node_names){
        debug->Info("GetAllAssetsFromGLTF: Node %s\n",nodename.c_str());
        loaded_materials.clear();

        bool already_loaded = false;

        for (Asset* asset:assetmanager->assets){
            if (asset->name.compare(nodename) == 0){
                debug->Err(" -> Already loaded\n");
                already_loaded = true;
                break;
            }
        }

        if (already_loaded){
            continue;
        }

        Mesh* gltfmesh = gltfloader.GetMeshFromNode(nodename.c_str(),&loaded_materials,true);
        if (gltfmesh){
            Object* gltf_object = new Object();
            gltf_object->name = nodename.c_str();
            gltf_object->SetMesh(gltfmesh);
            gltf_object->TakeMaterialNames(loaded_materials);
            assetmanager->AddNewAsset(nodename.c_str(),gltf_object);
            //Just add all...
            renderer->AddMaterials(loaded_materials);
            if (loaded_materials.size() == 0){
                //Mesh with no materials? We set the material to -1
                gltf_object->material_slot[0] = -1;
            }
        }
    }
    debug->Info("Loaded %i different materials from GLTF file\n",loaded_materials.size());
    //TODO: We also need to make sure all materials are loaded and stored somewhere usefull
}

//This loads it, makes an asset from it... and sets up all the things.
Object* Application::CreateNewObjectFromGLTF(const std::string& nodename, Scene* target_scene){
    std::vector<Material>loaded_materials;
    loaded_materials.clear();
    Mesh* gltfmesh = gltfloader.GetMeshFromNode(nodename.c_str(),&loaded_materials);
    if (gltfmesh){
        Object* gltf_object = new Object();
        gltf_object->SetPosition(gltfloader.GetNodePosition(nodename.c_str()));
        gltf_object->SetRotation(gltfloader.GetNodeRotation(nodename.c_str()));
        gltf_object->name = nodename.c_str();
        gltf_object->SetMesh(gltfmesh);
        target_scene->renderer->AddMaterials(loaded_materials);
        gltf_object->TakeMaterialNames(loaded_materials);
        gltf_object->PickMaterials(loaded_materials,target_scene->renderer->materials);
        target_scene->AddObject(gltf_object);
        Asset* asset = assetmanager->AddNewAsset(nodename.c_str(),gltf_object);
        return gltf_object;
    }
    return NULL;
}