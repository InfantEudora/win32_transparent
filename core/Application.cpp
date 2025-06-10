#include <winsock2.h>
#include "glad.h"

#include "Application.h"
#include "OBJLoader.h"

#include "Window.h"
#include "Renderer.h"

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

    tmr_physics = new PerfTimer("Physics Time"); //Physics loop time
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

void Application::Run(void){
    //Create a main window
    main_window = Window::CreateNewLayeredWindow(512,512,&Window::wcs.at(0));
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

    //Create a renderer for this window
    app->renderer = new Renderer(app->main_window->width,app->main_window->height);
    app->renderer->Init();

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    app->main_scene = new Scene();
    app->main_scene->renderer = app->renderer;
    app->main_scene->inputcontroller = app->main_window->inputcontroller;
    app->main_scene->shader = app->default_shader;

    BinaryAsset::DumpBinaryAssets();

    //Just so the current items show on the first frame...?
    app->main_scene->UpdatePhysics();

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

        app->main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        app->main_window->DrawFrame();
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
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
    app->debug_physics = new Debugger("AppPhysics", DEBUG_ALL);

    uint32_t physics_ticks = 0;
    while (1){
        //debug->Info("Physics Loop %lu\n",physics_ticks);
        timeBeginPeriod(1);
        Sleep(5);
        timeEndPeriod(1);
        if (app->main_scene){
            app->main_scene->HandleInput();
            app->renderer->state_mutex.lock();
            app->RunLogic();
            app->main_scene->UpdatePhysics();
            app->renderer->state_mutex.unlock();
            app->main_scene->inputcontroller->Tick();
        }
        //debug->Ok("Physics Loop %lu completed\n",physics_ticks);
        physics_ticks++;
    }
    debug->Info("Thread terminated\n");
    return 0;
}

//Called before update physics
void Application::RunLogic(){
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;
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

        if (ImGui::Button("Switch Camera")){
            main_scene->camera->Show();
            main_scene->camera = camera;
            main_scene->camera->Hide();
        }
    }
}

void Application::UpdateUISceneObjectTree(){
    if (ImGui::TreeNode("Scene Root")){
        for (Object* object:main_scene->renderer->objects){
            UpdateUISceneObjectTreeNode(object,NULL);
        }
        ImGui::TreePop();
    }
}


void Application::UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked){
    objectid_t id = object->GetID();
    long long p = id; //To suppress warning from 32-bit pointer
    if (ImGui::TreeNodeEx((void*)p,ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf, "Object #%i - %s",id,object->name.c_str())){
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

                if (assetmanager->GetObjectFromAsset("editor_camera",camera)){
                    camera->SetPosition(vec3(1,2,1));
                    camera->material_slot[0] = 3;
                    camera->SetLookAt(vec3());
                    camera->SetupPerspective(main_scene->renderer->width,main_scene->renderer->height,45,0.1,100);
                }
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
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Debug")){
            std::map<std::string, Debugger*>* handles = debug->GetHandles();
            std::map<std::string,Debugger*>::iterator it = handles->begin();
            for (int i=0;i<handles->size();i++){
                 if (ImGui::BeginMenu(it->first.c_str())){
                    static bool enabled = true;
                    ImGui::MenuItem("Enabled", "", &enabled);
                    ImGui::InputInt("Input", &it->second->level, 1);
                    ImGui::EndMenu();
                }
                it++;
            }
            ImGui::EndMenu();
        }
         ImGui::EndMainMenuBar();
    }

}

void Application::RenderGenericObjectUI(){
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
        }

    }

    if (ImGui::CollapsingHeader("Scene")){
        Scene* scene = main_scene;
        ImGui::Text("Main Scene             : %s",scene->name.c_str());
        UpdateUISceneObjectTree();

    }

    //So the same camera panel has a different ImGUI ID.
    int ui_camid = 0;
    UpdateUICameraControls(main_scene->camera ,ui_camid);

    ImGui::Text("ImGui.WantCaptureMouse   : %s",ImGui::GetIO().WantCaptureMouse ? "True" : "False");

    vec3 hov_normal = main_scene->inputcontroller->GetHoveredNormal();
    ImGui::Text("Normal at mouse   : %.3f, %.3f, %.3f",hov_normal.x,hov_normal.y,hov_normal.z);

    Object* object = hovered_object;
    if (!object){
        ImGui::Text("No Object Hovered");
    }else{
        ImGui::Text("Hovered Object: %s",object->name.c_str());
    }

    object = selected_object;
    if (!object){
        ImGui::Text("No Object Selected");
    }else{
        ImGui::Text("Selected Object: %s",object->name.c_str());
        bool obj_visible = object->IsVisible();
        if (ImGui::Checkbox("Visible",&obj_visible)){
            object->SetVisibility(obj_visible);
        }
        if (ImGui::Button("Duplicate(Linked)")){
            Object* duplicated = new Object(object);
            main_scene->AddObject(duplicated);
        }
    }
    if (object){
        Camera* cam = dynamic_cast<Camera*>(object);
        if (cam){
            ui_camid++;
            UpdateUICameraControls(cam,ui_camid);
        }

        if (ImGui::CollapsingHeader("Node Hierarchy")){
            if (object->parent){
                ImGui::Text("Parent             : %s",object->parent->name.c_str());
                if (ImGui::Button("Select Root Node")){
                    Object* o = object->parent;
                    while(o->parent){
                        o = o->parent;
                    }
                    selected_object = o;
                }
            }else{
                ImGui::Text("Parent             : Has No Parent");
            }
                ImGui::Text("Children           : %i",object->children.size());
        }

        Light* light = dynamic_cast<Light*>(object);
        if (light && ImGui::CollapsingHeader("Light Properties")){
            vec3 pos = object->GetPosition();
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Position", (float*)&pos, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
            ImGui::DragFloat3("Color", (float*)&light->color, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Brightness", (float*)&light->brightness, 0.01f, 0.0f, 10.0f);
        }

        if (object->GetMesh()){
            if (ImGui::CollapsingHeader("Mesh")){
                Mesh* mesh = object->GetMesh();
                ImGui::Text(" ID             : %lu",mesh->GetID());
                ImGui::Text(" num_vertices   : %lu",mesh->num_vertices);
                ImGui::Text(" num_materials  : %lu",mesh->num_materials);
                ImGui::Text(" num_references : %lu",mesh->num_references);
            }
        }else{
            ImGui::BeginDisabled();
            ImGui::CollapsingHeader("No Mesh");
            ImGui::EndDisabled();
        }

        if (object->GetSkinnedMesh()){
            if (ImGui::CollapsingHeader("Skinned Mesh")){
                SkinnedMesh* skinned_mesh = object->GetSkinnedMesh();
                ImGui::Text(" ID             : %lu",skinned_mesh->GetID());
                ImGui::Text(" num_vertices   : %lu",skinned_mesh->num_vertices);
                ImGui::Text(" num_materials  : %lu",skinned_mesh->num_materials);
                ImGui::Text(" num_references : %lu",skinned_mesh->num_references);
            }
        }else{
            ImGui::BeginDisabled();
            ImGui::CollapsingHeader("No Skinned Mesh");
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
            ImGui::BeginDisabled();
            vec3 pos = object->GetPosition();
            ImGui::DragFloat3("Position", (float*)&pos, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();

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

            bool apply_rotation = false;

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
                    apply_rotation = true;
                }
                static vec3 position = {0,0,0};
                if (ImGui::DragFloat3("Position", (float*)&position, 0.01f, -5.0f, 5.0f)){
                    apply_rotation = true;
                }
                static vec3 worldup = {0,1,0};
                if (ImGui::DragFloat3("World Up", (float*)&worldup, 0.01f, -1.0f, 1.0f)){
                    apply_rotation = true;
                }
                ImGui::BeginDisabled();
                q = quat::getquat(target,position,worldup);
                ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
            }else if (option == 3){
                static vec3 axis_degrees = {0,0,0};
                if (ImGui::DragFloat3("Axis Degrees", (float*)&axis_degrees, 1.0f, -180.0f, 180.0f)){
                    apply_rotation = true;
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

            if (ImGui::Button("Apply Rotation")){
                apply_rotation = true;
            }
            if (apply_rotation){
                object->SetRotation(q);
            }
        }
        if (ImGui::CollapsingHeader("Scale")){
            vec3 scale = object->GetScale();
            if (ImGui::DragFloat3("Scale Vector", (float*)&scale, 0.01f, 0.01f, 10.0f)){
                object->SetScale(scale);
            }
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

        if (ImGui::CollapsingHeader("Animations")){
            if (object->animations.size() == 0){
                ImGui::Text("Object has no animations");
            }else{
                for (Animation* animation:object->animations){
                    if (ImGui::Button(animation->name.c_str())){

                    }
                }

                if (object->current_animation){
                    ImGui::Text("Current Animation : %s @ %.2f / %.2f",object->current_animation->name.c_str(),object->current_animation->time_index,object->current_animation->duration);
                }else{
                    ImGui::Text("Current Animation : NULL");
                }
                if (object->next_animation){
                    ImGui::Text("Next Animation    : %s @ %.2f / %.2f",object->next_animation->name.c_str(),object->next_animation->time_index,object->next_animation->duration);
                }else{
                    ImGui::Text("Next Animation    : NULL");
                }
                ImGui::Text("Current Animation State : %i\n",object->animation_state);
            }
        }
    }

    if (ImGui::CollapsingHeader("Performance")){
        ImGui::Text("Frame Rate   : %.2f FPS (%.2f ms)", 1000000.0f / renderer->tmr_frame->avg,renderer->tmr_frame->avg/1000.0f );
        ImGui::Text("Physics Rate : %.2f TPS (%.2f ms)", 1000000.0f / tmr_physics->avg,tmr_physics->avg/1000.0f );
    }

    if (ImGui::CollapsingHeader("Renderer")){
        ImGui::Text(    "Normal Mapping :");ImGui::SameLine();
        ImGui::Checkbox("##1", &renderer->f_normal_mapping);
        ImGui::Text(    "Render Skybox  :");ImGui::SameLine();
        ImGui::Checkbox("##2", &renderer->f_render_skybox);

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
            ImGui::ColorEdit4(" GLSL Color", (float*)&material.glsl_material.color, ImGuiColorEditFlags_DisplayRGB);
            ImGui::PopID();

        }
    }

    if (ImGui::CollapsingHeader("Ray - Plane Intersection")){
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
        //
    }

    ImGui::End();
}

void Application::CheckObjectSelection(){
    hovered_object = NULL;
    InputController* input = main_scene->inputcontroller;

    //Check if we selected a tile
    objectid_t hovered_objid = OBJECTID_INVALID;


    if (!ImGui::GetIO().WantCaptureMouse){
        hovered_objid = input->GetHoveredObjectID();
        if ((hovered_objid == OBJECTID_INVALID) && input->WasKeyReleased(INPUT_CLICK_LEFT)){
            selected_object = NULL;
            return;
        }
    }

    for (Object* object:renderer->renderable_objects){
        if (object->IsDestroyed()){
            //TODO: Remove it... here?
        }

        if (object->GetID() == hovered_objid){
            hovered_object = object;
        }
        if (input->WasKeyReleased(INPUT_CLICK_LEFT) && (object->GetID() == hovered_objid)){
            vec3 p = object->GetPosition();
            debug->Info("Clicked on ID: %3i Object Pos: %.2f %.2f %.2f\n",hovered_objid,p.x,p.y,p.z);
            selected_object = object;
            //clicked_empty = false;
        }
    }
}

Scene* Application::CreateNewScene(const std::string& name){
    Scene* scene = new Scene();
    scene->name = name;
    scene->renderer = renderer;
    scene->inputcontroller = main_window->inputcontroller;
    scene->shader = default_shader;

    scenes.push_back(scene);
    return scene;
}