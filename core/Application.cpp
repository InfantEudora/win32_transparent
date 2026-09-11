#include <winsock2.h>
#include "glad.h"

#include "Application.h"
#include "imgui_internal.h" //DockBuilder* and ImHashStr, for the default dock layout
#include "PrecisionSleeper.h"
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

    //Keyboard and mouse acquisition onto its own thread, before anything starts reading input.
    //THIS thread is about to become the window message pump below, and that pump stops dead for
    //the duration of a title-bar drag or a resize (DefWindowProc runs a nested modal loop), which
    //is exactly why rendering and physics are already elsewhere. Raw input on a message-only
    //window of its own is what keeps input alive through that - see core/RawInput.h.
    if (raw_input.Start(main_window->inputcontroller)){
        main_window->inputcontroller->SetRawInputActive(true);
        debug->Ok("Raw input active: key edges and unaccelerated mouse deltas\n");
    }else{
        //Not fatal. InputController keeps polling GetAsyncKeyState, which still produces edges,
        //just sampled once a tick and without raw deltas.
        debug->Warn("Raw input unavailable, falling back to polled input\n");
    }

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
            //Nothing queued. Block until something arrives rather than polling on a Sleep(1):
            //that Sleep was only ever ~1ms because the physics thread happened to be holding the
            //system timer resolution at 1ms, and now that nothing raises it, the same call would
            //idle for up to ~15.6ms. Waiting on the queue instead wakes the instant a message
            //lands and costs no CPU at all in between.
            //MWMO_INPUTAVAILABLE closes the race where a message arrives after the PeekMessage
            //above returned FALSE but before this wait starts - without it that message would not
            //re-signal the queue and the wait would sit out its full timeout.
            //The timeout exists only so f_should_quit, which other threads set, is still noticed
            //when no messages are coming through at all.
            MsgWaitForMultipleObjectsEx(0,NULL,50,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
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
    main_scene->UpdatePhysics(GetPhysicsTimestep());
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
    //Before RegisterCoreMCPTools: those tools submit commands, so the handlers have to be in
    //place before any of them can be called.
    app->RegisterCoreCommandHandlers();
    app->RegisterCoreMCPTools();

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
    //Any GL work the app needs done before the scene is drawn - see Application::PreRender.
    PreRender();

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
    main_window->ImGuiRenderDrawData();

    //Copy to screen and finish
    main_window->SwapWindowBuffers();
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

    //Paces this loop. Constructed on the thread that uses it so its timer handle - and, on the
    //fallback path, the raised system timer period - lives exactly as long as the thread does.
    PrecisionSleeper sleeper;
    if (!sleeper.IsHighResolution()){
        app->debug_physics->Warn("Physics pacing is on the low-resolution timer path\n");
    }
    sleeper.ResetSchedule();

    while (1){
        if (app->main_scene){
            //How long one tick should take in REAL time. physics_time_factor stretches or
            //compresses this interval only - the timestep handed to the simulation stays
            //GetPhysicsTimestep() no matter what, so slow motion is "fewer ticks per second",
            //never "smaller ticks". Recomputed every loop because the UI slider can change it
            //mid-run. Clamped low so a factor near zero can't produce an infinite interval.
            double us_looptime_desired = app->physics_us_per_tick / max(app->physics_time_factor,0.01f);

            //Time spent on aquiring a lock
            app->renderer->physics_mutex.lock();

            //Input sampling moved INSIDE the lock: it writes KeyState (via
            //InputController::ApplyPendingEvents), and the render thread reads that same KeyState
            //from DrawImGuiUI - which holds this mutex. Sampling outside it was an unsynchronised
            //write against those reads. Polling ~17 keys costs microseconds, so paying for it
            //under the lock is free next to a physics tick.
            app->UpdateInput();

            //One decision for the whole pass: does this pass simulate? Everything below reads that
            //answer instead of re-deriving it, so animation, gameplay and physics cannot end up
            //disagreeing about whether a tick happened. BeginPass also drains the command queue
            //and services the pause key, both of which must happen on every pass. See Scene.h.
            bool f_tick = app->main_scene->BeginPass();

            //Time spent on this pass's work
            app->tmr_physics->Restart();
            if (f_tick){
                app->UpdateAnimations();
                app->RunSimulationTick();
                app->UpdatePhysics();
            }

            //View work runs AFTER the tick, and on the passes that did not tick as well. After,
            //because its whole job is to look at the simulation, so it should see the state this
            //pass just produced rather than the previous one's - the old RunLogic ran before the
            //step, which left every camera one tick stale. On paused passes too, because a paused
            //editor still has to have a working camera.
            app->UpdateView();
            app->tmr_physics->Stop();
            app->renderer->physics_mutex.unlock();

            //Restarted BEFORE the sleep, so what it reports is the whole period including it.
            app->tmr_physics_loop->Stop();
            app->tmr_physics_loop->Restart();

            //This used to compute its own sleep as clamp(last_sleep + (desired - us_loop),...),
            //which reads like an integral controller but is not one: us_loop already contained
            //last_sleep, the two cancelled algebraically, and what was left was the plain
            //feedforward "sleep = desired - work". With no integral term there was nothing to
            //absorb the up-to-999us that Sleep()'s whole-millisecond argument discarded every
            //tick, so it became a permanent bias - the loop measured ~51Hz at a 50Hz setting.
            //SleepUntilNextTick paces against an absolute deadline, where that error has
            //nowhere to accumulate, and does it without touching the global timer resolution.
            app->tmr_physics_sleep->Restart();
            sleeper.SleepUntilNextTick(us_looptime_desired);
            app->tmr_physics_sleep->Stop();

            //Clearing the per-tick edge flags stays HERE, after the sleep, rather than moving
            //inside the tick's critical section above: the render thread consumes mouse deltas
            //(UpdateUICameraControls -> GetDelta) during this window, and clearing them at the end
            //of the tick would leave the UI reading zeroes and break camera mouse-look. It only
            //needs the lock so the write itself is synchronised against those reads.
            app->renderer->physics_mutex.lock();
            app->NextInput();
            app->renderer->physics_mutex.unlock();
        }else{
            sleeper.SleepUs(5000);
            //Nothing was being paced while there was no scene, so the first real tick must not
            //come out of this looking late.
            sleeper.ResetSchedule();
            debug->Warn("No main scene for physics thread to work on!\n");
        }
        //This loop deliberately keeps no tick count of its own: it would count loop iterations
        //(which happen while paused too) rather than simulated ticks. Scene::GetPhysicsTick() is
        //the authoritative clock.
        //debug->Ok("Physics Loop %llu completed\n",app->main_scene ? app->main_scene->GetPhysicsTick() : 0);
    }
    debug->Info("Thread terminated\n");
    return 0;
}

//Both hooks are empty by default - see Application.h for what belongs in which.
void Application::UpdateView(){
    return;
}

void Application::RunSimulationTick(){
    return;
}

void Application::UpdateAnimations(){
    if (!main_scene){
        return;
    }
    main_scene->UpdateAnimations(GetPhysicsTimestep());
}

void Application::UpdatePhysics(){
    if (!main_scene){
        return;
    }
    main_scene->UpdatePhysics(GetPhysicsTimestep());
}

//--- Generic object MCP tools -------------------------------------------------------------

static json Vec3ToJson(const vec3& v){
    return json::array({v.x,v.y,v.z});
}

static json QuatToJson(const quat& q){
    return json::array({q.x,q.y,q.z,q.w});
}

static bool JsonToVec3(const json& arr,vec3& out){
    if (!arr.is_array() || arr.size() != 3 || !arr[0].is_number() || !arr[1].is_number() || !arr[2].is_number()){
        return false;
    }
    out = vec3(arr[0].get<float>(),arr[1].get<float>(),arr[2].get<float>());
    return true;
}

//"id" (number) or "name" (string) - id wins if both are given. Fills error and returns NULL if
//neither resolves.
static Object* ResolveObjectArg(Scene* scene,const json& args,std::string& error){
    if (!scene){
        error = "no scene";
        return NULL;
    }
    if (args.contains("id") && args.at("id").is_number()){
        objectid_t id = (objectid_t)args.at("id").get<uint32_t>();
        Object* object = scene->FindObjectByID(id);
        if (!object){
            error = "no object with id " + std::to_string(id);
        }
        return object;
    }
    if (args.contains("name") && args.at("name").is_string()){
        std::string name = args.at("name").get<std::string>();
        Object* object = scene->FindObject(name);
        if (!object){
            error = "no object named '" + name + "' (names aren't unique - prefer id from object_list)";
        }
        return object;
    }
    error = "supply either id (number) or name (string)";
    return NULL;
}

//Accepts any one of: "rotation" [x,y,z,w] quaternion, "axis_degrees" [x,y,z] (applied in X, Y, Z
//order, exactly like the Generic Object UI's "Axis Degrees" mode), or "yaw_degrees" (rotation
//about world up only). Returns false with error left empty if none is present, false with error
//set if one is present but malformed.
static bool RotationFromArgs(const json& args,quat& out,std::string& error){
    if (args.contains("rotation")){
        const json& arr = args.at("rotation");
        if (!arr.is_array() || arr.size() != 4){
            error = "rotation must be a [x,y,z,w] quaternion";
            return false;
        }
        out.x = arr[0].get<float>(); out.y = arr[1].get<float>(); out.z = arr[2].get<float>(); out.w = arr[3].get<float>();
        out.normalize();
        return true;
    }
    if (args.contains("axis_degrees")){
        vec3 deg;
        if (!JsonToVec3(args.at("axis_degrees"),deg)){
            error = "axis_degrees must be [x,y,z] in degrees";
            return false;
        }
        quat qx(vec3(1,0,0),toradians(deg.x));
        quat qy(vec3(0,1,0),toradians(deg.y));
        quat qz(vec3(0,0,1),toradians(deg.z));
        out = qx * qy * qz;
        return true;
    }
    if (args.contains("yaw_degrees") && args.at("yaw_degrees").is_number()){
        out = quat(vec3(0,1,0),toradians(args.at("yaw_degrees").get<float>()));
        return true;
    }
    return false;
}

//Shared by camera_get and camera_set so the two cannot drift apart. camera_target is the app's
//orbit pivot (see Application::GetCameraTargetPtr) and may be NULL for an app that has none.
static json CameraToJson(Camera* camera,vec3* camera_target){
    //GetPosition, not GetWorldPosition: this is exactly what the middle-mouse orbit reads and
    //writes, so what these tools report is what that code sees. The camera is a root object, so
    //local and world are the same thing anyway. (This used to matter for a second reason -
    //GetWorldPosition read a once-per-frame copy that was stale on this MCP thread straight
    //after a camera_set. There is one ObjectState now, so both are equally fresh.)
    vec3 pos = camera->GetPosition();
    vec3 forward = camera->GetForward();
    json result = {
        {"position", Vec3ToJson(pos)},
        //Where it points, one unit out - the argument camera_set's look_at takes back.
        {"look_at", Vec3ToJson(pos + forward)},
        {"forward", Vec3ToJson(forward)},
        {"up", Vec3ToJson(camera->GetUp())},
        {"left", Vec3ToJson(camera->GetLeft())},
        {"rotation", QuatToJson(camera->GetRotation())},
        {"fov", camera->viewport.fov},
        {"znear", camera->viewport.znear},
        {"zfar", camera->viewport.zfar},
        {"aspect", camera->viewport.aspect},
    };
    if (camera_target){
        result["camera_target"] = Vec3ToJson(*camera_target);
        result["target_distance"] = (pos - *camera_target).length();
    }
    return result;
}

static json ObjectToJson(Object* object,bool verbose){
    json result = {
        {"id", object->GetID()},
        {"name", object->name},
        {"parent_id", object->GetParent() ? json(object->GetParent()->GetID()) : json(nullptr)},
        {"position", Vec3ToJson(object->GetPosition())},
        {"has_physics", object->GetPhysics() != NULL},
    };
    if (Physics* physics = object->GetPhysics()){
        result["static"] = physics->IsStatic();
    }
    if (!verbose){
        return result;
    }
    result["world_position"] = Vec3ToJson(object->GetWorldPosition());
    result["rotation"] = QuatToJson(object->GetRotation());
    //`forward`/`up` come from the object's OWN rotation, so for a child they are relative to the
    //parent - next to a `world_position` that is absolute, which is a genuinely confusing pair.
    //The world versions are reported alongside rather than replacing them, because "the axis in
    //my parent's frame" is what a caller manipulating a child wants. For a root object the two
    //are identical.
    result["forward"] = Vec3ToJson(object->GetForward());
    result["world_forward"] = Vec3ToJson(object->GetWorldForward());
    result["up"] = Vec3ToJson(object->GetUp());
    result["world_up"] = Vec3ToJson(object->GetWorldUp());
    result["scale"] = Vec3ToJson(object->GetScale());
    result["visible"] = object->IsVisible();
    json children = json::array();
    for (Object* child:object->children){
        children.push_back({{"id",child->GetID()},{"name",child->name}});
    }
    result["children"] = children;
    if (Physics* physics = object->GetPhysics()){
        result["physics"] = {
            {"static", physics->IsStatic()},
            {"gravity", physics->IsGravityEnabled()},
            {"sleeping", physics->IsSleeping()},
            {"mass_kg", physics->GetMass()},
            {"velocity", Vec3ToJson(physics->GetVelocity())},
            {"angular_velocity", Vec3ToJson(physics->GetAngularVelocity())},
        };
    }
    return result;
}

//--- Simulation commands ------------------------------------------------------------------

void Application::RegisterCoreCommandHandlers(){
    if (!main_scene){
        debug->Err("No scene to register core command handlers on\n");
        return;
    }

    //Teleport. Only the fields the flags mark as present are written - a command that just wants
    //to rotate something must not also stamp a zeroed position over it.
    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_SET_TRANSFORM,
        [this](const SimCommand& cmd) -> objectid_t {
            Object* object = main_scene->FindObjectByID(cmd.target);
            if (!object){
                debug->Err("SimCommand set_transform: no object with id %u\n",cmd.target);
                return OBJECTID_INVALID;
            }
            if (cmd.flags & SIM_CMD_FLAG_POSITION){
                object->SetPosition(cmd.position);
            }
            if (cmd.flags & SIM_CMD_FLAG_ROTATION){
                object->SetRotation(cmd.rotation);
            }
            if (cmd.flags & SIM_CMD_FLAG_SCALE){
                object->SetScale(cmd.scale);
            }
            return object->GetID();
        });

    //Spawn. Same three lines the debug UI's Add > Asset menu runs (build from the asset, take
    //the asset's name, add to the scene) - only now on the physics thread, on a defined tick, so
    //the id handed out is reproducible.
    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_SPAWN_ASSET,
        [this](const SimCommand& cmd) -> objectid_t {
            if (!assetmanager){
                debug->Err("SimCommand spawn_asset: no asset manager\n");
                return OBJECTID_INVALID;
            }
            Asset* asset = assetmanager->GetAssetByID(cmd.asset); //logs its own error if missing
            if (!asset){
                return OBJECTID_INVALID;
            }
            Object* object = assetmanager->GetObjectFromAssetID(cmd.asset);
            if (!object){
                return OBJECTID_INVALID;
            }
            object->name = asset->name;
            if (cmd.flags & SIM_CMD_FLAG_POSITION){
                object->SetPosition(cmd.position);
            }
            if (cmd.flags & SIM_CMD_FLAG_ROTATION){
                object->SetRotation(cmd.rotation);
            }
            if (cmd.flags & SIM_CMD_FLAG_SCALE){
                object->SetScale(cmd.scale);
            }
            main_scene->AddObject(object);
            return object->GetID();
        });

    //Every physics property the Inspector can change, in one handler. Same shape as
    //SET_TRANSFORM: one `if` per flag, so a command carrying one property leaves the other eight
    //alone. The boolean values live at the same bit positions in bool_values as their own flags,
    //which is what keeps this to one line each - see SimCommand.h.
    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_SET_PHYSICS,
        [this](const SimCommand& cmd) -> objectid_t {
            Object* object = main_scene->FindObjectByID(cmd.target);
            if (!object){
                debug->Err("SimCommand set_physics: no object with id %u\n",cmd.target);
                return OBJECTID_INVALID;
            }
            //The collision masks live on the Object, not the Physics, so they are reachable even
            //on an object that has no body yet.
            if (cmd.flags & SIM_CMD_FLAG_CATEGORY_BITS){
                object->SetCollisionCategoryBits(cmd.collision_category_bits);
            }
            if (cmd.flags & SIM_CMD_FLAG_COLLIDE_BITS){
                object->SetCollideWithMaskBits(cmd.collide_with_bits);
            }
            Physics* physics = object->GetPhysics();
            if (!physics){
                //Not an error worth logging: the masks above may well have been the whole point.
                return object->GetID();
            }
            if (cmd.flags & SIM_CMD_FLAG_STATIC){
                physics->SetStatic(!!(cmd.bool_values & SIM_CMD_FLAG_STATIC));
            }
            if (cmd.flags & SIM_CMD_FLAG_GRAVITY){
                physics->SetGravityEnabled(!!(cmd.bool_values & SIM_CMD_FLAG_GRAVITY));
            }
            if (cmd.flags & SIM_CMD_FLAG_ACTIVE){
                physics->SetActive(!!(cmd.bool_values & SIM_CMD_FLAG_ACTIVE));
            }
            if (cmd.flags & SIM_CMD_FLAG_WAKE_UP){
                physics->WakeUp();
            }
            if (cmd.flags & SIM_CMD_FLAG_VELOCITY){
                physics->SetVelocity(cmd.velocity);
            }
            if (cmd.flags & SIM_CMD_FLAG_ANGULAR_VELOCITY){
                physics->SetAngularVelocity(cmd.angular_velocity);
            }
            if (cmd.flags & SIM_CMD_FLAG_FRICTION){
                physics->SetFrictionCoefficient(cmd.value[0]);
                //Friction only takes effect on contacts that get re-evaluated, and a pile that has
                //gone to sleep never re-evaluates - so without this the new value appears to do
                //nothing until something else disturbs the stack.
                if (main_scene->physics_world){
                    main_scene->physics_world->WakeUpEveryone();
                }
            }
            if (cmd.flags & SIM_CMD_FLAG_BOUNCINESS){
                physics->SetBounciness(cmd.value[1]);
            }
            return object->GetID();
        });

    //The engine's own object types, for the things that come from no asset (Add > Empty/Camera/
    //Light). Only the scene INSERTION is a command; anything that needs the GL context or the
    //GLTF loader (importing a skinned mesh, say) stays on the render thread and hands the
    //finished asset to SPAWN_ASSET afterwards.
    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_SPAWN_PRIMITIVE,
        [this](const SimCommand& cmd) -> objectid_t {
            Object* object = NULL;
            switch (cmd.subtype){
                case SIM_PRIMITIVE_EMPTY:{
                    object = new Object();
                    object->name = "Empty";
                    break;
                }
                case SIM_PRIMITIVE_CAMERA:{
                    Camera* camera = new Camera();
                    camera->name = "New Camera";
                    //A mesh so the thing can be seen and picked in the viewport. Copying an
                    //asset's mesh pointer touches no GL state, so it is safe here.
                    if (assetmanager && assetmanager->GetObjectFromAsset("editor_camera",camera)){
                        camera->SetMaterialSlot(0,3);
                    }
                    camera->SetPosition(vec3(1,2,1));
                    camera->SetLookAt(vec3());
                    camera->SetupPerspective(renderer->width,renderer->height,45,0.1,100);
                    object = camera;
                    break;
                }
                case SIM_PRIMITIVE_DIRECTIONAL_LIGHT:{
                    DirectionalLight* light = new DirectionalLight();
                    light->name = "Directional Light";
                    //Shading follows the light's forward, so give a freshly spawned one a
                    //sensible downward angle instead of the default horizontal -Z.
                    light->SetPosition(vec3(-10,10,10));
                    light->SetLookAt(vec3());
                    object = light;
                    break;
                }
                case SIM_PRIMITIVE_POINT_LIGHT:{
                    PointLight* light = new PointLight();
                    light->name = "Point Light";
                    object = light;
                    break;
                }
                default:{
                    debug->Err("SimCommand spawn_primitive: unknown subtype %u\n",cmd.subtype);
                    return OBJECTID_INVALID;
                }
            }
            //After construction, so an explicit placement wins over the type's own default.
            if (cmd.flags & SIM_CMD_FLAG_POSITION){
                object->SetPosition(cmd.position);
            }
            if (cmd.flags & SIM_CMD_FLAG_ROTATION){
                object->SetRotation(cmd.rotation);
            }
            if (cmd.flags & SIM_CMD_FLAG_SCALE){
                object->SetScale(cmd.scale);
            }
            main_scene->AddObject(object);
            return object->GetID();
        });

    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_DUPLICATE,
        [this](const SimCommand& cmd) -> objectid_t {
            Object* source = main_scene->FindObjectByID(cmd.target);
            if (!source){
                debug->Err("SimCommand duplicate: no object with id %u\n",cmd.target);
                return OBJECTID_INVALID;
            }
            Object* duplicated = new Object(source);
            main_scene->AddObject(duplicated);
            //Deliberately AFTER AddObject, and only when the submitter asked for it: a copy that
            //starts inactive can be dragged into place before it begins falling, which is why the
            //Inspector's Duplicate button clears the flag.
            Physics* physics = duplicated->GetPhysics();
            if (physics && (cmd.flags & SIM_CMD_FLAG_ACTIVE)){
                physics->SetActive(!!(cmd.bool_values & SIM_CMD_FLAG_ACTIVE));
            }
            return duplicated->GetID();
        });

    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_DESTROY,
        [this](const SimCommand& cmd) -> objectid_t {
            Object* object = main_scene->FindObjectByID(cmd.target);
            if (!object){
                debug->Err("SimCommand destroy: no object with id %u\n",cmd.target);
                return OBJECTID_INVALID;
            }
            objectid_t id = object->GetID();
            //MARKS it - see the note on SIM_CMD_OBJECT_DESTROY. Waking the world first, while the
            //body still exists, is what lets anything resting on it start falling.
            Physics* physics = object->GetPhysics();
            if (physics && physics->world){
                physics->world->WakeUpEveryone();
            }
            object->Destroy();
            return id;
        });

    main_scene->RegisterCommandHandler(SIM_CMD_WORLD_SET_GRAVITY,
        [this](const SimCommand& cmd) -> objectid_t {
            if (!main_scene->physics_world){
                debug->Err("SimCommand set_gravity: scene has no physics world\n");
                return OBJECTID_INVALID;
            }
            main_scene->physics_world->SetGravity(cmd.velocity);
            return OBJECTID_INVALID; //no object involved - the sequence alone says it landed
        });

    //The collider gizmo resolves its rp3d::Collider* HERE, on the physics thread, from the index
    //the command carried - which is the point: the pointer never travels, so it cannot be stale
    //and cannot end up in a recording.
    main_scene->RegisterCommandHandler(SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO,
        [this](const SimCommand& cmd) -> objectid_t {
            Object* object = main_scene->FindObjectByID(cmd.target);
            Physics* physics = object ? object->GetPhysics() : NULL;
            if (!physics || !physics->body || !physics->body->rigidbody){
                debug->Err("SimCommand collider_gizmo: object %u has no rigidbody\n",cmd.target);
                return OBJECTID_INVALID;
            }
            if (cmd.subtype >= physics->body->rigidbody->getNbColliders()){
                debug->Err("SimCommand collider_gizmo: object %u has no collider %u\n",
                           cmd.target,cmd.subtype);
                return OBJECTID_INVALID;
            }
            ObjectCollider* gizmo = new ObjectCollider();
            gizmo->HookTargetCollider(physics->body->rigidbody->getCollider(cmd.subtype));
            main_scene->AddObject(gizmo);
            return gizmo->GetID();
        });
}

//Every UI path that changes the simulation goes through here. The UI must NEVER wait for a
//command: DrawImGuiUI runs with renderer->physics_mutex held, and the physics thread needs that
//same mutex to reach Scene::DrainCommands, so blocking on it deadlocks on the spot. So this is
//deliberately fire-and-forget, and the panel simply shows the new value on a later frame - which
//is exactly how an ImGui widget behaves anyway, since it re-reads the object every frame.
void Application::SubmitUICommand(const SimCommand& cmd){
    if (main_scene){
        main_scene->SubmitCommand(cmd);
    }
}

objectid_t Application::SubmitCommandAndWait(const SimCommand& cmd, int timeout_ms){
    if (!main_scene){
        return OBJECTID_INVALID;
    }
    uint32_t sequence = main_scene->SubmitCommand(cmd);
    //Polled rather than signalled: the physics thread drains commands at the top of its tick and
    //a condition variable there would mean it has to know whether anyone is waiting. A command
    //lands within one tick (~20ms) and the callers are debug tooling, so a 5ms poll is free.
    //Commands drain before the pause check, so this also completes against a PAUSED scene.
    for (int waited_ms = 0; waited_ms < timeout_ms; waited_ms += 5){
        if (main_scene->GetAppliedCommandSequence() >= sequence){
            return main_scene->GetCommandResult(sequence);
        }
        Sleep(5);
    }
    debug->Err("SimCommand type %u (sequence %u) was not applied within %d ms\n",
               cmd.type,sequence,timeout_ms);
    return OBJECTID_INVALID;
}

//Moved here from ApplicationTank, which had one of these, while ApplicationShip had a
//second, near-identical copy wrapped in a tool of its own. Nothing about capturing a frame is
//app-specific, and every app has a renderer.
json Application::MaybeAttachScreenshot(json result, bool include_screenshot){
    if (!include_screenshot){
        return result;
    }
    if (!renderer){
        result["screenshot_error"] = "no renderer";
        return result;
    }
    std::vector<uint8_t> png = renderer->RequestScreenshot();
    if (png.empty()){
        result["screenshot_error"] = "timed out waiting for the render thread to capture a frame";
        return result;
    }
    return MCPServer::AttachImagePNG(result,png);
}

bool Application::ResolveObjectIdArg(const json& args, objectid_t& id_out, std::string& error){
    if (!main_scene){
        error = "no scene";
        return false;
    }
    bool f_found = false;
    main_scene->AtTickBoundary([&]{
        Object* object = ResolveObjectArg(main_scene,args,error);
        if (object){
            id_out = object->GetID();
            f_found = true;
        }
    });
    if (!f_found && error.empty()){
        error = "no scene";
    }
    return f_found;
}

json Application::ObjectJsonAtTickBoundary(objectid_t id, bool full){
    json result;
    bool f_found = false;
    if (main_scene){
        main_scene->AtTickBoundary([&]{
            Object* object = main_scene->FindObjectByID(id);
            if (object){
                result = ObjectToJson(object,full);
                f_found = true;
            }
        });
    }
    if (!f_found){
        return json{ {"error","object " + std::to_string(id) + " no longer exists"} };
    }
    return result;
}

json Application::SimClockJson(){
    if (!main_scene){
        return json{ {"error","no scene"} };
    }
    return json{
        {"tick", main_scene->GetPhysicsTick()},
        {"paused", main_scene->IsPhysicsPaused()},
        {"pending_steps", main_scene->GetPendingPhysicsSteps()},
        {"timestep", main_scene->GetPhysicsTimestep()}
    };
}

uint64_t Application::StepPhysicsAndWait(int num_ticks){
    if (!main_scene){
        return 0;
    }
    uint64_t tick_before = main_scene->GetPhysicsTick();
    main_scene->StepPhysics(num_ticks);

    //The physics thread runs at its own pace, so poll for it to actually consume what was just
    //queued rather than guessing a fixed sleep. The budget scales with the request so a long step
    //isn't cut short, with a floor for the case where the thread is briefly busy elsewhere.
    //
    //GetPendingPhysicsSteps() hitting zero is the right thing to wait on rather than the tick
    //counter: Scene::UpdatePhysics decrements it at the END of the tick it ran, precisely so that
    //a caller seeing zero knows the work is finished and the state is safe to read.
    int timeout_ms = max(2000,num_ticks * 30);
    for (int waited_ms = 0; waited_ms < timeout_ms && main_scene->GetPendingPhysicsSteps() > 0; waited_ms += 5){
        Sleep(5);
    }
    return main_scene->GetPhysicsTick() - tick_before;
}

void Application::RegisterCoreMCPTools(){
    const json object_selector_properties = {
        {"id", {{"type","number"},{"description","object id, as reported by object_list (preferred - unique)"}}},
        {"name", {{"type","string"},{"description","object name, first match wins - names are not unique"}}}
    };

    MCPServer::Get()->RegisterTool("object_list",
        "List the objects in the active scene (children included, depth-first) with id, name, "
        "parent id, position and whether they carry a physics body. Use name_filter (case-sensitive "
        "substring) to narrow it down; the list is capped at `limit` entries (default 200).",
        json{
            {"type","object"},
            {"properties", {
                {"name_filter", {{"type","string"},{"description","only objects whose name contains this substring"}}},
                {"limit", {{"type","number"},{"description","max entries returned, default 200"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            std::string filter = args.value("name_filter","");
            int limit = max((int)args.value("limit",200.0f),1);
            json objects = json::array();
            int total = 0;
            //The whole walk inside one tick boundary, not one object at a time: the physics thread
            //adds and destroys objects, so a walk that released the lock between entries could
            //follow a pointer that had already been reaped, and would report a list that never
            //existed at any single moment.
            main_scene->AtTickBoundary([&]{
                main_scene->ForEachObject([&](Object* object){
                    if (!filter.empty() && object->name.find(filter) == std::string::npos){
                        return;
                    }
                    total++;
                    if ((int)objects.size() < limit){
                        objects.push_back(ObjectToJson(object,false));
                    }
                });
            });
            return json{ {"objects",objects}, {"matched",total}, {"returned",(int)objects.size()} };
        });

    MCPServer::Get()->RegisterTool("object_get",
        "Full state of one object: local and world position, rotation (quaternion [x,y,z,w]), "
        "forward/up vectors, scale, children, and its physics body's static/gravity/sleeping flags, "
        "mass and velocities if it has one.",
        json{ {"type","object"}, {"properties", object_selector_properties} },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            //Resolved and serialised in the SAME tick boundary, so the object cannot move or be
            //destroyed between being found and being read.
            std::string error;
            json result;
            bool f_found = false;
            main_scene->AtTickBoundary([&]{
                Object* object = ResolveObjectArg(main_scene,args,error);
                if (object){
                    result = ObjectToJson(object,true);
                    f_found = true;
                }
            });
            if (!f_found){
                return json{ {"error",error.empty() ? "no scene" : error} };
            }
            return result;
        });

    MCPServer::Get()->RegisterTool("object_set_transform",
        "Instantly set an object's position and/or rotation and/or scale - the same as typing a "
        "value into the Generic Object UI. Only the fields given are changed. Rotation can be given "
        "as `rotation` [x,y,z,w], `axis_degrees` [x,y,z] (applied X then Y then Z, like the UI's "
        "'Axis Degrees' mode) or `yaw_degrees`. A physics body is teleported along with it: use "
        "object_move instead if it should push things out of the way on its way there. Applied on "
        "the physics thread at the start of a tick (a SimCommand), so it cannot land in the middle "
        "of a physics step - it takes effect within a tick, or on the next step if paused.",
        json{
            {"type","object"},
            {"properties", {
                {"id", object_selector_properties.at("id")},
                {"name", object_selector_properties.at("name")},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] local position (world, for a root object)"}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"},{"description","rotation about world up, degrees"}}},
                {"scale", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] scale - box/sphere/capsule colliders and their offsets follow; mass is kept, inertia recomputed"}}}
            }}
        },
        [this](const json &args) -> json {
            std::string error;
            //Resolved here only to turn a name into an id and to report a bad selector straight
            //back to the caller. The command itself carries the ID, never a pointer - see
            //SimCommand::target - which is also why only the id survives the lock.
            objectid_t target_id = OBJECTID_INVALID;
            if (!ResolveObjectIdArg(args,target_id,error)){
                return json{ {"error",error} };
            }
            SimCommand cmd;
            cmd.type = SIM_CMD_OBJECT_SET_TRANSFORM;
            cmd.target = target_id;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),cmd.position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_POSITION;
            }
            if (RotationFromArgs(args,cmd.rotation,error)){
                cmd.flags |= SIM_CMD_FLAG_ROTATION;
            }else if (!error.empty()){
                return json{ {"error",error} };
            }
            if (args.contains("scale")){
                if (!JsonToVec3(args.at("scale"),cmd.scale)){
                    return json{ {"error","scale must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_SCALE;
            }
            if (cmd.flags == 0){
                return json{ {"error","give a position, rotation and/or scale"} };
            }
            //This handler runs on an MCP thread, which holds no locks, so it can wait for the
            //physics thread to apply the command and then report the real resulting state.
            if (SubmitCommandAndWait(cmd) == OBJECTID_INVALID){
                return json{ {"error","the transform command was not applied - see the log"} };
            }
            //Read back AFTER the wait and at its own tick boundary. Waiting inside one would
            //deadlock: the physics thread applies the command, and it cannot run while this
            //handler holds the lock it needs.
            return ObjectJsonAtTickBoundary(target_id,true);
        });

    MCPServer::Get()->RegisterTool("object_move",
        "Move and/or rotate an object to a target over `ticks` physics ticks (default 50, i.e. one "
        "second at 50 tps), interpolating its transform one step per tick - what dragging the "
        "Generic Object UI's position slider does frame by frame. The body's transform is set "
        "directly each tick, so a collider on it pushes dynamic bodies out of the way instead of "
        "teleporting through them. Rotation is given like object_set_transform. Blocks until the "
        "motion has finished (or times out) and returns the object's resulting state; if physics "
        "is paused it returns immediately and the motion plays out as ticks are stepped.",
        json{
            {"type","object"},
            {"properties", {
                {"id", object_selector_properties.at("id")},
                {"name", object_selector_properties.at("name")},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] target local position"}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] target quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] target rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"},{"description","target rotation about world up, degrees"}}},
                {"ticks", {{"type","number"},{"description","physics ticks to spread the motion over, default 50, min 1"}}}
            }}
        },
        [this](const json &args) -> json {
            std::string error;
            objectid_t target_id = OBJECTID_INVALID;
            if (!ResolveObjectIdArg(args,target_id,error)){
                return json{ {"error",error} };
            }
            vec3 target_position;
            bool f_position = false;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),target_position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                f_position = true;
            }
            quat target_rotation;
            bool f_rotation = RotationFromArgs(args,target_rotation,error);
            if (!f_rotation && !error.empty()){
                return json{ {"error",error} };
            }
            if (!f_position && !f_rotation){
                return json{ {"error","give a target position and/or rotation"} };
            }
            int ticks = max((int)args.value("ticks",50.0f),1);
            //A direct WRITE from this thread, and the reason it needs a tick boundary rather than
            //just a careful read: MoveObjectOverTicks edits the object_motions vector that
            //AdvanceObjectMotions is iterating on the physics thread. It has no SimCommand form,
            //so the lock is the whole mechanism. Re-resolved inside, because the id was looked up
            //under a different lock and the object could have been destroyed since.
            bool f_moved = false;
            main_scene->AtTickBoundary([&]{
                Object* object = main_scene->FindObjectByID(target_id);
                if (object){
                    main_scene->MoveObjectOverTicks(object,f_position ? &target_position : NULL,f_rotation ? &target_rotation : NULL,ticks);
                    f_moved = true;
                }
            });
            if (!f_moved){
                return json{ {"error","object " + std::to_string(target_id) + " no longer exists"} };
            }

            json result;
            if (main_scene->IsPhysicsPaused()){
                result["note"] = "physics is paused - the motion runs as ticks are stepped";
            }else{
                //Physics ticks run on their own thread - poll for the queue to drain rather than
                //guessing a sleep. Generous timeout: the tick rate may be lower than nominal.
                int timeout_ms = (int)(ticks * (1000.0f / physics_tps) * 3.0f) + 2000;
                int waited_ms = 0;
                for (; waited_ms < timeout_ms && main_scene->GetPendingObjectMotions() > 0; waited_ms += 5){
                    Sleep(5);
                }
                if (main_scene->GetPendingObjectMotions() > 0){
                    result["note"] = "timed out waiting for the motion to finish - it is still in progress";
                }
                result["waited_ms"] = waited_ms;
            }
            //Outside the wait above, which polls the physics thread and so must not hold the lock.
            result["object"] = ObjectJsonAtTickBoundary(target_id,true);
            return result;
        });

    /*
        Pause and single-step, for every app rather than for the two that happened to need it.

        These are the read-side counterpart to sim_command: a tool handler holds no lock, so
        anything it reads from a free-running simulation is a race it is going to lose eventually.
        Pausing first turns "read the scene and hope" into "read the scene", and stepping turns
        "sleep 200ms and guess how far it got" into an exact number of ticks. Every duration in this
        engine is denominated in ticks (see Scene::GetPhysicsTick), so stepping is the unit the rest
        of the simulation is already written in.

        ApplicationTank and ApplicationTetris keep their own tank_pause/tank_step and
        tetris_pause/tetris_step: those return app telemetry with the step, which is genuinely more
        useful there than a bare clock. They now share the waiting logic below rather than each
        carrying a copy of it.
    */
    MCPServer::Get()->RegisterTool("sim_pause",
        "Pause or resume the simulation. While paused the render loop keeps running - the window "
        "stays responsive, the camera still works and screenshots still work - but no physics tick "
        "runs, no animation advances and no gameplay logic runs until sim_step advances it or this "
        "is called again with paused=false. Pause before reading scene state you care about: an MCP "
        "handler holds no lock, so reading a free-running simulation races the physics thread.",
        json{
            {"type","object"},
            {"properties", {
                {"paused", {{"type","boolean"},{"description","true to pause, false to resume free-running physics"}}}
            }},
            {"required", json::array({"paused"})}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            main_scene->PausePhysics(args.value("paused",true));
            return SimClockJson();
        });

    MCPServer::Get()->RegisterTool("sim_step",
        "Advance a paused simulation by exactly num_ticks ticks and return the resulting clock. "
        "Each tick is the same fixed timestep a free-running one uses, and a tick here is a whole "
        "tick - input, animation, the app's own per-tick logic and the physics step - so stepping "
        "advances the entire simulation, not just the physics. Requires sim_pause first. Blocks "
        "until the physics thread has consumed the steps, and reports ticks_advanced, which is the "
        "value to trust: if it is short of num_ticks the call timed out and the rest is still queued.",
        json{
            {"type","object"},
            {"properties", {
                {"num_ticks", {{"type","number"},{"description","how many ticks to advance, default 1"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the resulting frame, default false"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!main_scene->IsPhysicsPaused()){
                return json{ {"error","simulation is not paused - call sim_pause with paused=true first"} };
            }
            int num_ticks = max((int)args.value("num_ticks",1.0f),0);
            uint64_t advanced = StepPhysicsAndWait(num_ticks);
            json result = SimClockJson();
            result["ticks_advanced"] = advanced;
            result["requested_ticks"] = num_ticks;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });

    //The raw queue, exposed. Every OTHER command-shaped tool here (object_set_transform,
    //object_spawn, tank_reset) is a friendly wrapper that builds one specific SimCommand; this is
    //the generic one, and it exists for two reasons. It is how the command handlers get TESTED -
    //most of them are otherwise only reachable by clicking a button in the Inspector, so they
    //would ship unexercised - and it is what a future record/replay harness needs, since a
    //recording is a stream of exactly these.
    MCPServer::Get()->RegisterTool("sim_command",
        "Submit a raw SimCommand to the simulation's command queue and wait for the physics "
        "thread to apply it. `type` is a command name (object_set_transform, object_set_physics, "
        "object_spawn_asset, object_spawn_primitive, object_duplicate, object_destroy, "
        "world_set_gravity, object_spawn_collider_gizmo) or a raw number for an app's own type. "
        "Any other field given is carried in the command and its presence flag set; fields left "
        "out are not touched by the command. Returns the applied sequence number and the id of "
        "the object the handler created or acted on.",
        json{
            {"type","object"},
            {"properties", {
                {"type", {{"description","command name, or a raw type number for an app-specific command"}}},
                {"subtype", {{"type","number"},{"description","variant within the type: primitive kind for object_spawn_primitive (0 empty, 1 camera, 2 directional light, 3 point light), collider index for object_spawn_collider_gizmo"}}},
                {"target", {{"type","number"},{"description","object id the command is about"}}},
                {"asset", {{"type","string"},{"description","asset name - hashed to its id here, see asset_list"}}},
                {"asset_id", {{"type","number"},{"description","asset id, wins over `asset`"}}},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"}}},
                {"scale", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}}},
                {"velocity", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","linear velocity, and the gravity vector for world_set_gravity"}}},
                {"angular_velocity", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}}},
                {"static", {{"type","boolean"}}},
                {"gravity", {{"type","boolean"},{"description","whether the body reacts to gravity"}}},
                {"active", {{"type","boolean"}}},
                {"wake_up", {{"type","boolean"},{"description","true to wake the body - a trigger, not a state"}}},
                {"category_bits", {{"type","number"},{"description","collision category mask"}}},
                {"collide_with_bits", {{"type","number"},{"description","collide-with mask"}}},
                {"friction", {{"type","number"}}},
                {"bounciness", {{"type","number"}}}
            }},
            {"required", json::array({"type"})}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;

            //--- type ---
            if (!args.contains("type")){
                return json{ {"error","give a command type"} };
            }
            const json& type_arg = args.at("type");
            if (type_arg.is_number()){
                cmd.type = (uint16_t)type_arg.get<uint32_t>();
            }else if (type_arg.is_string()){
                //Names rather than numbers at the boundary, for the same reason durations are
                //milliseconds here and ticks inside: a caller should not have to know the enum.
                const std::string name = type_arg.get<std::string>();
                if (name == "object_set_transform")            cmd.type = SIM_CMD_OBJECT_SET_TRANSFORM;
                else if (name == "object_set_physics")          cmd.type = SIM_CMD_OBJECT_SET_PHYSICS;
                else if (name == "object_spawn_asset")          cmd.type = SIM_CMD_OBJECT_SPAWN_ASSET;
                else if (name == "object_spawn_primitive")      cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                else if (name == "object_duplicate")            cmd.type = SIM_CMD_OBJECT_DUPLICATE;
                else if (name == "object_destroy")              cmd.type = SIM_CMD_OBJECT_DESTROY;
                else if (name == "world_set_gravity")           cmd.type = SIM_CMD_WORLD_SET_GRAVITY;
                else if (name == "object_spawn_collider_gizmo") cmd.type = SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO;
                else return json{ {"error","unknown command type '" + name + "'"} };
            }else{
                return json{ {"error","type must be a command name or a number"} };
            }

            //--- payload. Presence in the JSON is what sets the flag, which is exactly the
            //contract the struct itself has: a field nobody mentioned is not written. ---
            std::string error;
            vec3 v;
            if (args.contains("subtype")){
                cmd.subtype = (uint32_t)args.at("subtype").get<uint32_t>();
            }
            if (args.contains("target")){
                cmd.target = (objectid_t)args.at("target").get<uint32_t>();
            }
            if (args.contains("asset_id")){
                cmd.asset = (assetid_t)args.at("asset_id").get<uint32_t>();
            }else if (args.contains("asset")){
                cmd.asset = AssetIDFromName(args.at("asset").get<std::string>().c_str());
            }
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),cmd.position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_POSITION;
            }
            if (RotationFromArgs(args,cmd.rotation,error)){
                cmd.flags |= SIM_CMD_FLAG_ROTATION;
            }else if (!error.empty()){
                return json{ {"error",error} };
            }
            if (args.contains("scale")){
                if (!JsonToVec3(args.at("scale"),cmd.scale)){
                    return json{ {"error","scale must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_SCALE;
            }
            if (args.contains("velocity")){
                if (!JsonToVec3(args.at("velocity"),cmd.velocity)){
                    return json{ {"error","velocity must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_VELOCITY;
            }
            if (args.contains("angular_velocity")){
                if (!JsonToVec3(args.at("angular_velocity"),cmd.angular_velocity)){
                    return json{ {"error","angular_velocity must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_ANGULAR_VELOCITY;
            }
            //The booleans, whose value lives at the same bit position as their own flag.
            struct BoolArg{ const char* name; uint32_t flag; };
            const BoolArg bool_args[] = {
                {"static",  SIM_CMD_FLAG_STATIC},
                {"gravity", SIM_CMD_FLAG_GRAVITY},
                {"active",  SIM_CMD_FLAG_ACTIVE},
            };
            for (const BoolArg& b:bool_args){
                if (args.contains(b.name)){
                    cmd.flags |= b.flag;
                    if (args.at(b.name).get<bool>()){
                        cmd.bool_values |= b.flag;
                    }
                }
            }
            if (args.value("wake_up",false)){
                cmd.flags |= SIM_CMD_FLAG_WAKE_UP; //a trigger, so nothing in bool_values
            }
            if (args.contains("category_bits")){
                cmd.collision_category_bits = (uint32_t)args.at("category_bits").get<uint32_t>();
                cmd.flags |= SIM_CMD_FLAG_CATEGORY_BITS;
            }
            if (args.contains("collide_with_bits")){
                cmd.collide_with_bits = (uint32_t)args.at("collide_with_bits").get<uint32_t>();
                cmd.flags |= SIM_CMD_FLAG_COLLIDE_BITS;
            }
            if (args.contains("friction")){
                cmd.value[0] = args.at("friction").get<float>();
                cmd.flags |= SIM_CMD_FLAG_FRICTION;
            }
            if (args.contains("bounciness")){
                cmd.value[1] = args.at("bounciness").get<float>();
                cmd.flags |= SIM_CMD_FLAG_BOUNCINESS;
            }

            //Submitted and waited on, so the reply can describe what actually happened. Safe
            //here: an MCP handler holds no locks - see SubmitCommandAndWait.
            uint32_t sequence = main_scene->SubmitCommand(cmd);
            objectid_t result = OBJECTID_INVALID;
            bool applied = false;
            for (int waited_ms = 0; waited_ms < 2000; waited_ms += 5){
                if (main_scene->GetAppliedCommandSequence() >= sequence){
                    result = main_scene->GetCommandResult(sequence);
                    applied = true;
                    break;
                }
                Sleep(5);
            }
            json out;
            out["sequence"] = sequence;
            out["applied"] = applied;
            out["version"] = cmd.version;
            out["flags"] = cmd.flags;
            if (!applied){
                out["error"] = "the command was not applied within 2000 ms";
                return out;
            }
            out["object_id"] = result;
            if (result != OBJECTID_INVALID){
                json object_json = ObjectJsonAtTickBoundary(result,true);
                if (!object_json.contains("error")){
                    out["object"] = object_json;
                }else{
                    //A destroy reports the id it acted on, and that object is gone by now.
                    out["note"] = "the object is no longer in the scene";
                }
            }else if (cmd.type != SIM_CMD_WORLD_SET_GRAVITY){
                //"applied" only means the queue drained it. A handler that found no such object,
                //or no handler at all, still counts as applied - so say so, or a caller reads a
                //declined command as a successful one. world_set_gravity is the legitimate case
                //of a command that concerns no object.
                out["note"] = "the command was drained but its handler acted on nothing - "
                              "bad target, unknown subtype, or no handler for this type. "
                              "See the application log.";
            }
            return out;
        });

    MCPServer::Get()->RegisterTool("asset_list",
        "List the loaded assets - the things object_spawn can build an object from - with the "
        "stable id each one is addressed by. The id is a hash of the asset's name (not a load "
        "order index), so it stays the same across runs and across changes to what else is "
        "loaded.",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json &args) -> json {
            if (!assetmanager){
                return json{ {"error","no asset manager"} };
            }
            json assets = json::array();
            for (Asset* asset:assetmanager->assets){
                assets.push_back(json{
                    {"name",asset->name},
                    {"id",asset->id},
                    {"has_mesh",asset->mesh != NULL}
                });
            }
            return json{ {"assets",assets}, {"count",(int)assets.size()} };
        });

    MCPServer::Get()->RegisterTool("object_spawn",
        "Create an object from a loaded asset and add it to the scene - the same as the debug UI's "
        "Add > Asset menu. Give `asset` (the name) or `asset_id` (from asset_list), and optionally "
        "a position/rotation/scale; rotation is given as in object_set_transform. The object is "
        "created on the physics thread at the start of a tick, so the id it gets is reproducible; "
        "that id is returned along with the object's full state. No physics body is added - use "
        "an app-specific tool if the thing needs a collider.",
        json{
            {"type","object"},
            {"properties", {
                {"asset", {{"type","string"},{"description","asset name, as listed by asset_list"}}},
                {"asset_id", {{"type","number"},{"description","asset id, as listed by asset_list - wins over `asset`"}}},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world position"}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"},{"description","rotation about world up, degrees"}}},
                {"scale", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] scale"}}}
            }}
        },
        [this](const json &args) -> json {
            SimCommand cmd;
            cmd.type = SIM_CMD_OBJECT_SPAWN_ASSET;
            //A name is hashed HERE, at the boundary, so the command itself only ever carries the
            //id - the same rule the MCP tools follow for durations (milliseconds in, ticks
            //onwards). Nothing deeper in the simulation sees the string.
            if (args.contains("asset_id") && args.at("asset_id").is_number()){
                cmd.asset = (assetid_t)args.at("asset_id").get<uint32_t>();
            }else if (args.contains("asset") && args.at("asset").is_string()){
                cmd.asset = AssetIDFromName(args.at("asset").get<std::string>().c_str());
            }else{
                return json{ {"error","give an asset name or asset_id - see asset_list"} };
            }
            std::string error;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),cmd.position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_POSITION;
            }
            if (RotationFromArgs(args,cmd.rotation,error)){
                cmd.flags |= SIM_CMD_FLAG_ROTATION;
            }else if (!error.empty()){
                return json{ {"error",error} };
            }
            if (args.contains("scale")){
                if (!JsonToVec3(args.at("scale"),cmd.scale)){
                    return json{ {"error","scale must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_SCALE;
            }
            //The whole reason a command reports a result: the caller cannot know the new object's
            //id in advance, because ids are handed out on the physics thread in tick order, which
            //is what makes them reproducible in the first place.
            objectid_t created = SubmitCommandAndWait(cmd);
            if (created == OBJECTID_INVALID){
                return json{ {"error","nothing was spawned - no such asset, or the command was not applied (see the log)"} };
            }
            return json{ {"id",created}, {"object",ObjectJsonAtTickBoundary(created,true)} };
        });

    //--- Camera -------------------------------------------------------------------------------
    //The camera is an Object, so object_get/object_set_transform can already reach it by name -
    //but only in terms of position and a quaternion. What the orbit controls actually work in is
    //a position, a point being looked AT, and the pivot they turn around, so these report and
    //accept exactly that. Written for debugging the middle-mouse orbit: camera_get before and
    //after a drag says whether a runaway came from the input delta or from the orbit maths.
    MCPServer::Get()->RegisterTool("camera_get",
        "Report the active scene camera: world position, the point it is looking at, its "
        "forward/up/left vectors, its rotation quaternion, the orbit pivot (camera_target) the "
        "middle-mouse orbit and zoom turn around, the distance from the camera to that pivot, and "
        "the perspective viewport settings.",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json &args) -> json {
            if (!main_scene || !main_scene->camera){
                return json{ {"error","no camera"} };
            }
            //A camera is written every pass now - UpdateView is where the orbit, the chase and the
            //overhead easing all live - so reading one off-tick returns a pose from part-way
            //through whatever it was doing.
            json result;
            main_scene->AtTickBoundary([&]{
                result = CameraToJson(main_scene->camera,GetCameraTargetPtr());
            });
            return result;
        });

    MCPServer::Get()->RegisterTool("camera_set",
        "Place the active scene camera. Only the fields given are changed. `position` moves it, "
        "`look_at` aims it at a world point (applied after position, so giving both aims from the "
        "new place), `up` is an optional up hint for that aim, and `camera_target` moves the orbit "
        "pivot the middle-mouse orbit and zoom turn around. Setting position and camera_target to "
        "a known pair is how you put the orbit into a repeatable state before testing it. Returns "
        "the resulting camera, same shape as camera_get.",
        json{
            {"type","object"},
            {"properties", {
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world position"}}},
                {"look_at", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world point to aim at"}}},
                {"up", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] up hint used with look_at"}}},
                {"camera_target", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] orbit/zoom pivot"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene || !main_scene->camera){
                return json{ {"error","no camera"} };
            }
            /*
                Arguments are parsed BEFORE the tick boundary and applied inside it.

                Parsing needs nothing from the scene, so doing it first means a malformed request
                is rejected without ever stopping the simulation. It also makes the write
                all-or-nothing: the old order set the position, then discovered look_at was
                malformed and returned an error, leaving the camera half-moved by a call that
                reported failure.
            */
            vec3 position, look_at, up, camera_target;
            bool f_position = false, f_look_at = false, f_up = false, f_camera_target = false;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                f_position = true;
            }
            if (args.contains("look_at")){
                if (!JsonToVec3(args.at("look_at"),look_at)){
                    return json{ {"error","look_at must be [x,y,z]"} };
                }
                f_look_at = true;
            }
            if (args.contains("up")){
                if (!JsonToVec3(args.at("up"),up)){
                    return json{ {"error","up must be [x,y,z]"} };
                }
                f_up = true;
            }
            if (args.contains("camera_target")){
                if (!GetCameraTargetPtr()){
                    return json{ {"error","this application has no camera_target"} };
                }
                if (!JsonToVec3(args.at("camera_target"),camera_target)){
                    return json{ {"error","camera_target must be [x,y,z]"} };
                }
                f_camera_target = true;
            }

            //A camera is read by the render thread every frame and written by UpdateView every
            //pass, so these writes land at a tick boundary rather than whenever this thread
            //happens to be scheduled. The resulting state is read back inside the same boundary,
            //so what comes back is what was actually set.
            json result;
            main_scene->AtTickBoundary([&]{
                Camera* camera = main_scene->camera;
                if (f_position){
                    camera->SetPosition(position);
                }
                if (f_look_at){
                    camera->SetLookAt(look_at,f_up ? &up : NULL);
                }
                if (f_camera_target){
                    *GetCameraTargetPtr() = camera_target;
                }
                camera->CalculateLookatMatrix();
                result = CameraToJson(camera,GetCameraTargetPtr());
            });
            return result;
        });

    MCPServer::Get()->RegisterTool("screenshot",
        "Capture the app's current frame as a PNG. Blocks until the render thread has drawn and "
        "encoded a frame, so what comes back is the window as it is now - the only way to check "
        "anything about how an app LOOKS rather than what its numbers say. Available in every app; "
        "an app's own tools may also take an include_screenshot argument to return one alongside "
        "their telemetry, which saves a second round trip.",
        json{
            {"type","object"},
            {"properties", json::object()}
        },
        [this](const json &args) -> json {
            //MaybeAttachScreenshot reports a failure in a field, which is right when there is a
            //telemetry payload to keep - but here the picture IS the result, so an empty one is
            //an error and worth saying so plainly.
            json result = MaybeAttachScreenshot(json::object(),true);
            if (result.contains("screenshot_error")){
                return json{ {"error",result["screenshot_error"]} };
            }
            return result;
        });
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
        //From THIS camera, not main_scene's - the panel also renders for a selected camera
        //object, and seeding the widget from the main camera used to write the main camera's
        //znear onto whichever camera was being inspected.
        float znear = camera->viewport.znear;
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
        //Gravity is simulation state, so it goes through the queue like everything else that
        //is. Pause/step/time factor/TPS below do NOT: they change how often a tick runs in real
        //time, never what a tick computes, so they are not part of what a replay reproduces.
        vec3 gravity = physics_world->GetGravity();
        if (ImGui::DragFloat3("Gravity (m/s^2)",(float*)&gravity,0.1f,-20,20)){
            SimCommand cmd;
            cmd.type = SIM_CMD_WORLD_SET_GRAVITY;
            cmd.velocity = gravity;
            SubmitUICommand(cmd);
        }
        if (ImGui::DragFloat("Time Factor (tick rate)",&physics_time_factor,0.01f,0.1f,2.0f)){
            //Nothing to do here, it's applied in the physics update loop - and it changes how
            //OFTEN a tick runs, not how long a tick is. The timestep stays GetPhysicsTimestep().
        }
        ImGui::SetItemTooltip("Runs ticks more/less often in real time. The simulation timestep itself never changes.");
        float tps = physics_tps;
        if (ImGui::DragFloat("Target Physics TPS",&tps,1.0f,1.0f,200.0f)){
            SetPhysicsTPS(tps);
        }
    }

}
void Application::RenderDebugMenuBarClass(){
    return;
}

void Application::RenderDebugMenuBar(){
    if (ImGui::BeginMainMenuBar()){
        //Creating an object is a change to the simulation, so every item here submits a command
        //and the object appears on the next tick - see the Debug UI section comment. The one
        //exception is the GLTF import below, which cannot be a command.
        if (main_scene && ImGui::BeginMenu("Add Object")){
            if (ImGui::MenuItem("Empty")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_EMPTY;
                SubmitUICommand(cmd);
            }
            if (ImGui::MenuItem("Camera")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_CAMERA;
                SubmitUICommand(cmd);
            }
            if (ImGui::MenuItem("DirectionalLight")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_DIRECTIONAL_LIGHT;
                SubmitUICommand(cmd);
            }
            if (ImGui::MenuItem("PointLight")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_POINT_LIGHT;
                SubmitUICommand(cmd);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Objects From Assets")){
                if (!assetmanager){
                    ImGui::MenuItem("-- NO ASSET MANAGER --");
                }else{
                    for (Asset* asset:assetmanager->assets){
                        if (ImGui::MenuItem(asset->name.c_str())){
                            //By id, not by name: the id IS the name, hashed, and it is what fits
                            //in a fixed payload - see AssetIDFromName in AssetManager.h.
                            SimCommand cmd;
                            cmd.type = SIM_CMD_OBJECT_SPAWN_ASSET;
                            cmd.asset = asset->id;
                            SubmitUICommand(cmd);
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
                                    //DELIBERATELY NOT a command. Importing from the GLTF loader
                                    //builds meshes, which is render-thread work (see
                                    //GetAssetsFromGLTF, which Fatal()s off it), and a pointer to
                                    //the result cannot travel in a SimCommand. Asset import is
                                    //authoring, not simulation - it happens outside the tick and
                                    //is not part of what a replay reproduces. Once imported, the
                                    //asset spawns through SIM_CMD_OBJECT_SPAWN_ASSET like any
                                    //other.
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

        if (ImGui::BeginMenu("View")){
            ImGui::MenuItem("Scene","",&f_show_scene_window);
            ImGui::MenuItem("Inspector","",&f_show_inspector_window);
            ImGui::MenuItem("Engine","",&f_show_engine_window);
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

//--- Debug UI -----------------------------------------------------------------------------------
//
//Three windows, not one: a Scene tree, an Inspector for whatever is selected, and an Engine panel
//for everything that is application- or scene-wide. They used to be one "Generic Object UI"
//window, which had grown to the point where half of it was renderer/performance/material settings
//and selecting an object unrolled fifteen collapsing headers of property names down the screen.
//
//The Inspector uses a TAB BAR rather than a stack of collapsing headers, which is the whole fix
//for that: one line of tabs, one section's worth of controls on screen, and a tab that does not
//apply (no mesh, no physics, no skeleton) simply is not there - as opposed to the three disabled
//"No Mesh"/"No Physics"/"No Skeleton" headers that used to spend three lines each saying what an
//object ISN'T.
//
//EVERY control here that changes the simulation submits a SimCommand (see core/SimCommand.h)
//instead of touching the object. Note this was never a RACE - DrawImGuiUI runs with
//renderer->physics_mutex held, so it is already mutually exclusive with the whole physics tick.
//The reason is determinism: a mutation applied from here lands at a defined point in a defined
//tick and can be recorded, which is what makes a run replayable. Controls that only affect what
//is DRAWN (renderer flags, materials, material slots, light colour, the camera) stay direct -
//they are not simulation state and putting them in the stream would perturb a replay.

//Builds the shell of a command for one object, so the call sites below stay one or two lines.
static SimCommand ObjectCommand(uint16_t type, objectid_t target){
    SimCommand cmd;
    cmd.type = type;
    cmd.target = target;
    return cmd;
}

//A boolean property is a PAIR of bits - the flag says "I am setting this", the bit at the same
//position in bool_values says what to. See the SIM_CMD_FLAG_* comment in SimCommand.h.
static void SetCommandBool(SimCommand& cmd, uint32_t flag, bool value){
    cmd.flags |= flag;
    if (value){
        cmd.bool_values |= flag;
    }else{
        cmd.bool_values &= ~flag;
    }
}

void Application::RenderApplicationUI(){
    //One dockspace covering the viewport, with a transparent central node so the 3D scene shows
    //through it and the mouse still reaches the world for picking (see CheckObjectSelection,
    //which gates on ImGui::GetIO().WantCaptureMouse). Without PassthruCentralNode the host window
    //would paint over the whole viewport and swallow every click.
    const ImGuiID dockspace_id = ImHashStr("WindMainDockSpace");

    //Default layout, once, and only if imgui.ini restored nothing - otherwise a layout the user
    //arranged by hand would be thrown away on every start. Everything docks LEFT: Scene and
    //Engine share the upper node as tabs, the Inspector takes the lower one, which is the
    //arrangement that keeps the tree visible while properties are being edited.
    static bool dock_layout_checked = false;
    if (!dock_layout_checked){
        dock_layout_checked = true;
        if (ImGui::DockBuilderGetNode(dockspace_id) == NULL){
            ImGui::DockBuilderAddNode(dockspace_id,ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id,ImGui::GetMainViewport()->WorkSize);
            ImGuiID left_id = 0;
            ImGuiID centre_id = 0;
            ImGui::DockBuilderSplitNode(dockspace_id,ImGuiDir_Left,0.24f,&left_id,&centre_id);
            ImGuiID left_top_id = 0;
            ImGuiID left_bottom_id = 0;
            ImGui::DockBuilderSplitNode(left_id,ImGuiDir_Up,0.40f,&left_top_id,&left_bottom_id);
            ImGui::DockBuilderDockWindow("Scene",left_top_id);
            ImGui::DockBuilderDockWindow("Engine",left_top_id);
            ImGui::DockBuilderDockWindow("Inspector",left_bottom_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }
    ImGui::DockSpaceOverViewport(dockspace_id,NULL,ImGuiDockNodeFlags_PassthruCentralNode);

    if (f_show_scene_window){
        RenderSceneWindow();
    }
    if (f_show_inspector_window){
        RenderInspectorWindow();
    }
    if (f_show_engine_window){
        RenderEngineWindow();
    }
}

//--- Scene window -------------------------------------------------------------------------------

void Application::RenderSceneWindow(){
    ImGui::Begin("Scene",&f_show_scene_window);
    if (scenes.empty()){
        ImGui::TextDisabled("No scenes");
        ImGui::End();
        return;
    }

    //A name filter, because the tree is the primary way to find things and a real scene runs to
    //dozens of objects (the tank scene is 59). While it has text the tree is replaced by a FLAT
    //list of every match at any depth - filtering a tree in place would either hide the matches
    //inside collapsed parents or force every parent open.
    static char name_filter[64] = {};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ObjectFilter","filter by name",name_filter,sizeof(name_filter));
    bool filtering = (name_filter[0] != 0);

    for (Scene* scene:scenes){
        ImGui::PushID(scene);
        int num_objects = scene->renderer ? (int)scene->renderer->objects.size() : 0;
        const char* active = (scene == main_scene) ? " (active)" : "";
        ImGui::SeparatorText((scene->name + active).c_str());

        if (filtering){
            int matches = 0;
            scene->ForEachObject([&](Object* object){
                if (object->name.find(name_filter) == std::string::npos){
                    return;
                }
                matches++;
                bool selected = (object == selected_object);
                //snprintf, not sprintf: unlike every other label here this one interpolates an
                //object NAME, whose length comes from an asset or a GLTF file.
                char label[160];
                snprintf(label,sizeof(label),"#%lu %s",(unsigned long)object->GetID(),object->name.c_str());
                if (ImGui::Selectable(label,selected)){
                    selected_object = object;
                }
            });
            ImGui::TextDisabled("%i of %i objects match",matches,num_objects);
        }else{
            ImGui::TextDisabled("%i root objects",num_objects);
            for (Object* object:scene->renderer->objects){
                UpdateUISceneObjectTreeNode(object,NULL);
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void Application::UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked){
    objectid_t id = object->GetID();
    long long p = id; //To suppress warning from 32-bit pointer

    //Highlighting the selected row is what makes the tree usable as a selection widget rather
    //than just a listing - before this, nothing in it showed what was selected.
    ImGuiTreeNodeFlags flags = 0;
    if (object == selected_object){
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (object->GetChild(0) == NULL){
        flags |= ImGuiTreeNodeFlags_Bullet;
    }

    if (ImGui::TreeNodeEx((void*)p,flags,"Object #%i - %s",id,object->name.c_str())){
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

//--- Inspector ----------------------------------------------------------------------------------

void Application::RenderInspectorWindow(){
    ImGui::Begin("Inspector",&f_show_inspector_window);
    Object* object = selected_object;
    if (object == NULL){
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextDisabled("Click an object in the viewport or in the Scene tree.");
        ImGui::End();
        return;
    }

    //--- Identity, and the two things that apply to any object at all ---
    ImGui::Text("%s",object->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%lu",(unsigned long)object->GetID());
    if (Object* parent = object->GetParent()){
        ImGui::TextDisabled("child of #%lu %s",(unsigned long)parent->GetID(),parent->name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select root")){
            Object* root = parent;
            while (root->GetParent()){
                root = root->GetParent();
            }
            selected_object = root;
        }
    }else if (object->GetNumChildren() > 0){
        ImGui::TextDisabled("root, %i children",object->GetNumChildren());
    }else{
        ImGui::TextDisabled("root, no children");
    }

    //Visibility is a RENDER flag (Object::SetVisibility only touches f_visible), so it stays a
    //direct call - it changes what is drawn, not what is simulated.
    bool obj_visible = object->IsVisible();
    if (ImGui::Checkbox("Visible",&obj_visible)){
        object->SetVisibility(obj_visible);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_DUPLICATE,object->GetID());
        //Inactive, so the copy can be dragged into place before it starts falling.
        SetCommandBool(cmd,SIM_CMD_FLAG_ACTIVE,false);
        SubmitUICommand(cmd);
        //Deliberately NOT selecting the copy: its id is only handed out when the physics thread
        //applies the command, so it does not exist yet. Scene::GetCommandResult can report it,
        //but reading that means waiting for the tick, and UI code must never wait (see
        //SubmitUICommand). The copy appears in the tree next frame.
    }
    ImGui::SameLine();
    if (ImGui::Button("Destroy")){
        SubmitUICommand(ObjectCommand(SIM_CMD_OBJECT_DESTROY,object->GetID()));
        selected_object = NULL;
    }

    //--- The tabs. A tab is only submitted when the object actually has that aspect. ---
    if (ImGui::BeginTabBar("InspectorTabs")){
        if (ImGui::BeginTabItem("Transform")){
            RenderInspectorTransformTab(object);
            ImGui::EndTabItem();
        }
        if (object->GetPhysics() && ImGui::BeginTabItem("Physics")){
            RenderInspectorPhysicsTab(object);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Render")){
            RenderInspectorRenderTab(object);
            ImGui::EndTabItem();
        }
        if (!object->animations.empty() && ImGui::BeginTabItem("Animation")){
            RenderInspectorAnimationTab(object);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")){
            RenderInspectorDebugTab(object);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Application::RenderInspectorTransformTab(Object* object){
    objectid_t id = object->GetID();

    //Absolute position. The widget reads the object every frame and only submits on an actual
    //edit, so it tracks the simulation while idle and does not fight it while being dragged.
    vec3 position = object->GetPosition();
    if (ImGui::DragFloat3("Position",(float*)&position,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_POSITION;
        cmd.position = position;
        SubmitUICommand(cmd);
    }

    //Relative nudges, resolved to an ABSOLUTE pose here rather than sent as a delta. Two reasons:
    //a command carrying "move 0.1 forward" would apply against whatever pose the object has when
    //the physics thread gets to it, and "forward" is the object's own axis, which only the caller
    //knows. Reading the pose here is safe - DrawImGuiUI holds physics_mutex.
    vec3 nudge = {};
    if (ImGui::DragFloat3("Move by",(float*)&nudge,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_POSITION;
        cmd.position = object->GetPosition() + nudge;
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Drag to nudge along the world axes. Snaps back to 0 - it is a delta.");

    float along[3] = {0,0,0};
    if (ImGui::DragFloat3("Fwd / Left / Up",along,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_POSITION;
        cmd.position = object->GetPosition()
                     + object->GetForward() * along[0]
                     + object->GetLeft() * along[1]
                     + object->GetUp() * along[2];
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Same, but along the object's OWN axes.");

    ImGui::SeparatorText("Rotation");
    //Shown as a quaternion, and edited by deltas or by absolute axis-degrees - deliberately NOT
    //as euler angles. quat::get_pitch/get_yaw/get_roll do not invert the q1*q2*q3 composition
    //used below (measured: 30 deg about X reads back as 27.7, and [10,80,10] as [63,68,63]), so a
    //euler box would drift and jump the moment it round-tripped. Fixing that means settling the
    //quaternion conventions in type_quat.h, which is its own job.
    ImGui::BeginDisabled();
    quat current = object->GetRotation();
    ImGui::DragFloat4("Quaternion",(float*)&current,0.01f);
    ImGui::EndDisabled();

    //One row of three deltas instead of the three separate "Roll By"/"Pitch By"/"Yaw By" drags.
    float rotate_by[3] = {0,0,0};
    if (ImGui::DragFloat3("Pitch/Yaw/Roll by",rotate_by,0.01f)){
        //Composed onto the CURRENT rotation here, so the command still carries an absolute pose.
        quat q = object->GetRotation();
        if (rotate_by[0] != 0.0f){
            quat d; d.set_rotation(object->GetLeft(),rotate_by[0]); q = d * q;
        }
        if (rotate_by[1] != 0.0f){
            quat d; d.set_rotation(object->GetUp(),rotate_by[1]); q = d * q;
        }
        if (rotate_by[2] != 0.0f){
            quat d; d.set_rotation(object->GetForward(),rotate_by[2]); q = d * q;
        }
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_ROTATION;
        cmd.rotation = q.normalize();
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Rotates about the object's own left/up/forward. Radians, and a delta.");

    //Absolute, and the same composition order the object_set_transform MCP tool uses for its
    //axis_degrees argument, so the two agree.
    static vec3 axis_degrees = {};
    ImGui::DragFloat3("Axis degrees",(float*)&axis_degrees,1.0f,-180.0f,180.0f);
    ImGui::SameLine();
    if (ImGui::Button("Set")){
        quat qx; qx.set_rotation(vec3(1,0,0),toradians(axis_degrees.x));
        quat qy; qy.set_rotation(vec3(0,1,0),toradians(axis_degrees.y));
        quat qz; qz.set_rotation(vec3(0,0,1),toradians(axis_degrees.z));
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_ROTATION;
        cmd.rotation = qx * qy * qz;
        SubmitUICommand(cmd);
    }

    ImGui::SeparatorText("Scale");
    vec3 scale = object->GetScale();
    if (ImGui::DragFloat3("Scale",(float*)&scale,0.01f,0.01f,100.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_SCALE;
        cmd.scale = scale;
        SubmitUICommand(cmd);
    }
    float scale_all = 1.0f;
    if (ImGui::DragFloat("Scale all by",&scale_all,0.01f,0.01f,10.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_SCALE;
        cmd.scale = object->GetScale() * scale_all;
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Colliders and their offsets follow; mass is kept and inertia recomputed.");
}

void Application::RenderInspectorPhysicsTab(Object* object){
    Physics* physics = object->GetPhysics();
    if (!physics){
        return; //the tab is not submitted in this case, so this is belt and braces
    }
    objectid_t id = object->GetID();

    bool f_static = physics->IsStatic();
    if (ImGui::Checkbox("Static",&f_static)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        SetCommandBool(cmd,SIM_CMD_FLAG_STATIC,f_static);
        SubmitUICommand(cmd);
    }
    bool f_gravity = physics->IsGravityEnabled();
    if (ImGui::Checkbox("Reacts to gravity",&f_gravity)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        SetCommandBool(cmd,SIM_CMD_FLAG_GRAVITY,f_gravity);
        SubmitUICommand(cmd);
    }
    bool f_active = physics->IsActive();
    if (ImGui::Checkbox("Active",&f_active)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        SetCommandBool(cmd,SIM_CMD_FLAG_ACTIVE,f_active);
        SubmitUICommand(cmd);
    }
    ImGui::Text("Sleeping : %s",physics->IsSleeping() ? "yes" : "no");
    ImGui::SameLine();
    if (ImGui::SmallButton("Wake up")){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_WAKE_UP; //a trigger, so no bool_values bit
        SubmitUICommand(cmd);
    }
    ImGui::Text("Mass     : %.3f kg",physics->GetMass());

    ImGui::SeparatorText("Velocity");
    vec3 velocity = physics->GetVelocity();
    if (ImGui::DragFloat3("Linear",(float*)&velocity,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_VELOCITY;
        cmd.velocity = velocity;
        SubmitUICommand(cmd);
    }
    vec3 angular = physics->GetAngularVelocity();
    if (ImGui::DragFloat3("Angular",(float*)&angular,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_ANGULAR_VELOCITY;
        cmd.angular_velocity = angular;
        SubmitUICommand(cmd);
    }

    ImGui::SeparatorText("Surface");
    float friction = physics->GetFrictionCoefficient();
    if (ImGui::DragFloat("Friction",&friction,0.01f,0.0f,2.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_FRICTION;
        cmd.value[0] = friction;
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("The handler wakes the whole world after this - a sleeping pile would "
                          "otherwise keep its old friction until something disturbed it.");
    float bounciness = physics->GetBounciness();
    if (ImGui::DragFloat("Bounciness",&bounciness,0.01f,0.0f,1.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_BOUNCINESS;
        cmd.value[1] = bounciness;
        SubmitUICommand(cmd);
    }

    ImGui::SeparatorText("Collision masks");
    uint32_t category_bits = object->collision_category_bits;
    if (RenderBitmaskCheckboxes("Cat",category_bits)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_CATEGORY_BITS;
        cmd.collision_category_bits = category_bits;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    ImGui::Text("category %08X",object->collision_category_bits);

    uint32_t collide_bits = object->collide_with_bits;
    if (RenderBitmaskCheckboxes("Col",collide_bits)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_COLLIDE_BITS;
        cmd.collide_with_bits = collide_bits;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    ImGui::Text("collides with %08X",object->collide_with_bits);

    ImGui::SeparatorText("Colliders");
    int num_colliders = physics->GetNumColliders();
    ImGui::Text("%i collider(s)",num_colliders);
    if (physics->body && physics->body->rigidbody){
        for (uint32_t i=0;i<physics->body->rigidbody->getNbColliders();i++){
            rp3d::Collider* collider = physics->body->rigidbody->getCollider(i);
            if (collider->getCollisionShape()->getName() != rp3d::CollisionShapeName::BOX){
                continue;
            }
            char caption[48];
            sprintf(caption,"Edit box collider %lu",(unsigned long)i);
            if (ImGui::Button(caption)){
                //Spawns an ObjectCollider gizmo hooked onto this collider. The command carries
                //the collider's INDEX, not its pointer - an rp3d::Collider* is neither
                //recordable nor safe to hand across a tick boundary.
                SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO,id);
                cmd.subtype = i;
                SubmitUICommand(cmd);
            }
        }
    }
}

//Eight checkboxes for one 8-bit mask, returning true when any of them changed. Two of these used
//to be written out twice in full, inline, at 20 lines each.
bool Application::RenderBitmaskCheckboxes(const char* id, uint32_t& mask){
    bool modified = false;
    uint32_t result = 0;
    ImGui::PushID(id);
    for (int i=0;i<8;i++){
        bool bit = !!(mask & (1<<i));
        char boxid[16];
        sprintf(boxid,"##bit%i",i);
        if (ImGui::Checkbox(boxid,&bit)){
            modified = true;
        }
        result |= ((uint32_t)bit) << i;
        if (i < 7){
            ImGui::SameLine();
        }
    }
    ImGui::PopID();
    if (modified){
        mask = result;
    }
    return modified;
}

void Application::RenderInspectorRenderTab(Object* object){
    //Everything in this tab is about what gets DRAWN, so it all stays direct - none of it is
    //simulation state and none of it belongs in a recording.
    if (Camera* camera = dynamic_cast<Camera*>(object)){
        int ui_camera_id = 1;
        UpdateUICameraControls(camera,ui_camera_id);
    }

    if (Light* light = dynamic_cast<Light*>(object)){
        ImGui::SeparatorText("Light");
        ImGui::DragFloat3("Colour",(float*)&light->color,0.01f,0.0f,1.0f);
        ImGui::DragFloat("Brightness",(float*)&light->brightness,0.01f,0.0f,10.0f);
        ImGui::DragFloat("Shadow bias",(float*)&light->shadow_bias,0.0001f,0.0f,1.0f);
    }

    ImGui::SeparatorText("Materials");
    for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
        char label[32];
        sprintf(label,"Slot %i",i);
        //Through the setter, not straight into the array: dragging a slot has to settle it, or
        //the name lookup would put its own answer back on the next frame and the widget would
        //appear to do nothing on any object loaded from an asset.
        int slot_value = object->GetMaterialSlot(i);
        if (ImGui::DragInt(label,&slot_value,1,-1,20)){
            object->SetMaterialSlot(i,slot_value);
        }
        if (!object->GetMaterialName(i).empty()){
            ImGui::SameLine();
            ImGui::TextDisabled("%s",object->GetMaterialName(i).c_str());
        }
    }

    if (Mesh* mesh = object->GetMesh()){
        ImGui::SeparatorText("Mesh");
        ImGui::Text("id %lu, %lu vertices, %lu materials",mesh->GetID(),mesh->num_vertices,mesh->num_materials);
        ImGui::TextDisabled("%s, %lu references, %lu morph targets",
                            mesh->IsSkinnedMesh() ? "skinned" : "static",
                            mesh->num_references,mesh->num_morph_targets);
    }else{
        ImGui::SeparatorText("Mesh");
        ImGui::TextDisabled("No mesh - this object is not drawn.");
    }
}

void Application::RenderInspectorAnimationTab(Object* object){
    //NOT on the command queue yet, and this is the one remaining hole in step 6 of the
    //determinism plan: since the root-motion rewrite an animation's extracted deltas DRIVE the
    //object's motion, which makes clip selection simulation state. Putting it on the queue needs
    //a way to name a clip inside a fixed payload (a hash of the clip name, the same trick
    //assetid_t uses), which is a deliberate later step.
    ImGui::TextDisabled("Direct calls - not on the command queue yet.");

    ImGui::SeparatorText("Morph factors");
    for (int i=0;i<4;i++){
        char label[24];
        sprintf(label,"Target %i",i+1);
        ImGui::DragFloat(label,&object->morph_factors[i],0.01f,0,1);
    }

    ImGui::SeparatorText("Clips");
    ImGui::DragFloat("Transition time max",&object->animation_transition_time_max,0.01f,0,3);
    if (ImGui::Button("NULL (reference pose)")){
        object->SwitchToAnimation(NULL);
    }
    int button_id = 1;
    for (Animation* animation:object->animations){
        if (ImGui::Button(animation->name.c_str())){
            object->TransitionToAnimation(animation);
        }
        button_id++;
        if (button_id % 4 != 0){
            ImGui::SameLine();
        }
    }
    ImGui::NewLine();

    if (object->current_animation){
        Animation* current = object->current_animation;
        ImGui::Text("Playing : %s @ %.2f / %.2f",current->name.c_str(),current->time_index,current->duration);
        ImGui::Checkbox("Looping",&current->looped);
        ImGui::Checkbox("Extract horizontal root motion",&current->extract_horizontal_root_motion);
        ImGui::Checkbox("Extract vertical root motion",&current->extract_vertical_root_motion);
    }else{
        ImGui::TextDisabled("Playing : nothing");
    }
    if (object->transition_to){
        ImGui::Text("Blending to %s (factor %.3f)",object->transition_to->name.c_str(),
                    object->animation_transition_factor);
    }
    ImGui::TextDisabled("state %i, wanted '%s'",object->animation_state,
                        object->dbg_desired_animation_name.c_str());

    if (!object->animation_blend_overrides.empty()){
        ImGui::SeparatorText("Blend time overrides");
        for (Object::AnimationBlendOverride& o:object->animation_blend_overrides){
            ImGui::Text("%s --> %s : %.2fs",o.from.empty() ? "*" : o.from.c_str(),o.to.c_str(),o.blend_time);
        }
    }
}

void Application::RenderInspectorDebugTab(Object* object){
    //Read-only internals. They live behind their own tab so they stop competing for screen space
    //with the controls - which is most of why the old single-column panel ran off the bottom of
    //the screen the moment anything was selected.
    ImGui::Text("Address : %p",(void*)object);
    ImGui::Text("Visible (renderer) : %s",object->IsVisible() ? "yes" : "no");
    ImGui::Text("Destroyed          : %s",object->IsDestroyed() ? "yes" : "no");

    if (Bone* bone = dynamic_cast<Bone*>(object)){
        ImGui::SeparatorText("Bone");
        ImGui::Text("bone_index %i, unpacked %i, node %i, initial length %.2f",
                    bone->bone_index,bone->bone_unpacked_index,bone->node_index,bone->initial_length);
        if (ImGui::TreeNode("inverse_bind_matrix")){
            ImGui::BeginDisabled();
            for (int i=0;i<4;i++){
                char label[8];
                sprintf(label,"V%i",i+1);
                ImGui::DragFloat4(label,(float*)&bone->inverse_bind_matrix.vertex[i],0.01f);
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("world_transform_scale_matrix")){
            fmat4 m = bone->GetWorldTransformScaleMatrix();
            ImGui::BeginDisabled();
            for (int i=0;i<4;i++){
                char label[8];
                sprintf(label,"V%i",i+1);
                ImGui::DragFloat4(label,(float*)&m.vertex[i],0.01f);
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }

    if (Skeleton* skeleton = dynamic_cast<Skeleton*>(object)){
        ImGui::SeparatorText("Skeleton");
        ImGui::Text("Root bone : %s",skeleton->root_bone_name.c_str());
        static char bone_name[64] = {};
        ImGui::InputText("Set root bone",bone_name,sizeof(bone_name));
        ImGui::SameLine();
        if (ImGui::Button("Apply")){
            skeleton->root_bone_name = bone_name;
        }
        std::vector<Bone*> bones;
        skeleton->GetAllBones(skeleton,bones);
        if (ImGui::TreeNode((void*)"bones","%i bones",(int)bones.size())){
            for (Bone* b:bones){
                ImGui::TextDisabled("%s",b->name.c_str());
            }
            ImGui::TreePop();
        }
    }
}

//--- Engine window ------------------------------------------------------------------------------

void Application::RenderEngineWindow(){
    ImGui::Begin("Engine",&f_show_engine_window);

    if (main_scene){
        UpdateUIWorldPhysics(main_scene->physics_world);
        int ui_camera_id = 0;
        UpdateUICameraControls(main_scene->camera,ui_camera_id);
    }

    if (ImGui::CollapsingHeader("Performance")){
        bool sync = renderer->GetVSync();
        if (ImGui::Checkbox("V-Sync",&sync)){
            renderer->SetVSync(sync);
        }
        ImGui::Text("Renderer Time : %8.1f us  (%5.2f ms)",renderer->tmr_frame->avg,renderer->tmr_frame->avg/1000.0f);
        ImGui::Text("Frame Time    : %8.2f FPS (%5.2f ms)",1000000.0f/tmr_render_loop->avg,tmr_render_loop->avg/1000.0f);
        ImGui::Text("Physics Loop  : %8.2f TPS (%5.2f ms)",1000000.0f/tmr_physics_loop->avg,tmr_physics_loop->avg/1000.0f);
        ImGui::Text("Physics Sleep : %8.1f us  (%5.2f ms)",tmr_physics_sleep->avg,tmr_physics_sleep->avg/1000.0f);
        ImGui::Text("Physics Time  : %8.1f us  (%5.2f ms)",tmr_physics->avg,tmr_physics->avg/1000.0f);
        if (main_scene && main_scene->renderer){
            ImGui::Text("Renderable Objects : %i",(int)main_scene->renderer->renderable_objects.size());
            ImGui::Text("Unique Meshes      : %i",(int)main_scene->renderer->unique_meshes.size());
            ImGui::Text("Batches            : %i",(int)main_scene->renderer->unique_mesh_batches.size());
        }
        if (main_scene){
            ImGui::Text("Physics Tick       : %llu",(unsigned long long)main_scene->GetPhysicsTick());
            ImGui::Text("Pending Commands   : %i",main_scene->GetPendingCommands());
            ImGui::Text("Applied Commands   : %u",main_scene->GetAppliedCommandSequence());
        }
    }

    if (ImGui::CollapsingHeader("Renderer")){
        ImGui::Checkbox("Normal mapping",&renderer->f_normal_mapping);
        ImGui::Checkbox("SSAO",&renderer->f_ssao);
        ImGui::Checkbox("Render skybox",&renderer->f_render_skybox);
        ImGui::Checkbox("Render reflections",&renderer->f_use_reflections);

        int view_buffer = renderer->view_buffer;
        if (ImGui::SliderInt("View buffer",&view_buffer,0,8)){
            renderer->SelectViewBuffer(view_buffer);
        }
        int num_samples = renderer->aa_samples;
        if (ImGui::SliderInt("MSAA samples",&num_samples,1,16)){
            renderer->SetNumAASamples(num_samples);
        }
        ImGui::SliderFloat("Alpha clip",&renderer->alpha_clip,0.0f,1.0f);
    }

    if (ImGui::CollapsingHeader("Input")){
        ImGui::Text("Window in focus        : %s",main_window->f_has_focus ? "yes" : "no");
        ImGui::Text("Mouse over window      : %s",main_window->inputcontroller->IsMouseOverWindow() ? "yes" : "no");
        ImGui::Text("ImGui.WantCaptureMouse : %s",ImGui::GetIO().WantCaptureMouse ? "yes" : "no");
        if (main_scene && main_scene->inputcontroller){
            vec3 hov_normal = main_scene->inputcontroller->GetHoveredNormal();
            vec3 hov_pos = main_scene->inputcontroller->GetHoveredPosition();
            ImGui::Text("Normal at mouse   : %.3f, %.3f, %.3f",hov_normal.x,hov_normal.y,hov_normal.z);
            ImGui::Text("Position at mouse : %.3f, %.3f, %.3f",hov_pos.x,hov_pos.y,hov_pos.z);
        }
        ImGui::Text("Hovered object    : %s",hovered_object ? hovered_object->name.c_str() : "none");
    }

    if (ImGui::CollapsingHeader("Window")){
        ImGui::Text("Current size : %i x %i",main_window->width,main_window->height);
    }

    if (assetmanager && ImGui::CollapsingHeader("Assets")){
        //With ids, because that is how object_spawn and any SimCommand refers to an asset - see
        //AssetIDFromName in AssetManager.h for why it is a hash of the name.
        for (Asset* asset:assetmanager->assets){
            ImGui::Text("%-28s %10u",asset->name.c_str(),asset->id);
        }
    }

    if (ImGui::CollapsingHeader("Materials")){
        ImGui::Text("%i materials",(int)renderer->GetNumMaterials());
        int n = 0;
        for (Material& material:renderer->materials){
            ImGui::PushID(n++);
            if (ImGui::TreeNode((void*)"mat","%s",material.name.c_str())){
                ImGui::TextDisabled("diffuse_texture %i",material.glsl_material.diffuse_texture);
                ImGui::DragFloat("Metallic",(float*)&material.glsl_material.metallic,0.01f,0,1);
                ImGui::DragFloat("Roughness",(float*)&material.glsl_material.roughness,0.01f,0,1);
                ImGui::DragFloat("Brightness",(float*)&material.glsl_material.brightness,0.01f,0,10);
                ImGui::ColorEdit4("Colour",(float*)&material.glsl_material.color,ImGuiColorEditFlags_DisplayRGB);
                //Emission: the colour comes in from glTF's emissiveFactor and is clamped 0..1, so
                //the strength next to it is what makes something actually glow. Both are edited
                //in place on renderer->materials, which UploadMaterials rebuilds the SSBO from
                //every frame, so edits show up immediately.
                ImGui::ColorEdit3("Emissive",(float*)&material.glsl_material.emissive,ImGuiColorEditFlags_DisplayRGB);
                ImGui::DragFloat("Emissive Strength",(float*)&material.glsl_material.emissive.w,0.05f,0,20);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
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

void Application::CheckObjectSelection(){
    hovered_object = NULL;
    InputController* input = main_scene->inputcontroller;

    if (input->IsMouseOverWindow() == false){
        return;
    }

    //Cursor over an ImGui window: nothing in the scene is hovered, and no click here belongs to
    //the world. Returning is the point - this used to be an `if (!WantCaptureMouse)` around the
    //refresh below ONLY, and then fell through to the loop regardless, which went on matching
    //the STALE hovered_objid left over from the last frame the cursor was over the scene. So
    //clicking a button on a panel selected - and logged a "Clicked on ID" for - whatever object
    //happened to be under the cursor before it moved onto the panel.
    if (ImGui::GetIO().WantCaptureMouse){
        hovered_objid = OBJECTID_INVALID;
        //A press that began on an object and is released over a panel is abandoned, rather than
        //completing as a click on that object once the button comes up.
        dragged_objid = OBJECTID_INVALID;
        return;
    }

    hovered_objid = input->GetHoveredObjectID();
    if ((hovered_objid == OBJECTID_INVALID) && input->WasKeyReleased(INPUT_CLICK_LEFT)){
        selected_object = NULL;
        return;
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
                gltf_object->SetMaterialSlot(0,-1);
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