#include <winsock2.h>
#include "glad.h"

#include "Application.h"
#include "PrecisionSleeper.h"

//For ThreadIdText below - the only way to get text out of a std::thread::id.
#include <sstream>

#include "Window.h"
#include "Renderer.h"
#ifdef USE_PHYSICS
//The collider-editing gizmo, spawned by SIMCMD_ADD_COLLIDER_GIZMO below. Here rather than in
//Application.h because this is the only place in the engine that names the type - see the note
//where that include used to be.
#include "ObjectCollider.h"
#endif

#include "tinygltf/json.hpp"
using json = nlohmann::json;

static Debugger *debug = new Debugger("Application", DEBUG_ALL);

/*
    A thread id as printable text. std::thread::id has no numeric value to reach - the standard
    gives it operator<< and nothing else - so the stream lives here once instead of at every log
    line that wants to name a thread.

    WHAT COMES OUT IS NOT THE OS THREAD ID. libstdc++ prints its own counter, so the three threads
    here log as 1, 2 and 3 while RawInput.cpp, Window.cpp and a debugger all show the win32 ids
    (11396 and friends) for the same threads. Handy for reading a log, useless for matching one
    against a debugger - if that is ever needed, log GetCurrentThreadId() alongside rather than
    assuming these agree.
*/
static std::string ThreadIdText(std::thread::id id){
    std::ostringstream ss;
    ss << id;
    return ss.str();
}

Application::Application(){
    //Init OpenGL.
    //Show some kind of loading screen and load stuff from disk.
    //Maybe we need some kind of way to get each app to get they own makefile.
    SetupConsole();

    //Get the current thread ID this application was called in. This is the thread that goes on to
    //pump window messages in Start(), never the one that renders or simulates.
    thread_id_main = std::this_thread::get_id();
    debug->Info("Main thread id: %s\n",ThreadIdText(thread_id_main).c_str());

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
    //The app's own name in the title bar - see Application::app_name, which a subclass sets in
    //its constructor and so has already set by the time Start() runs.
    main_window = Window::CreateNewWindow(1280,800,0,app_name.c_str());
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

    //And do all render calls from a seperate thread. It picks up the context released just above.
    //
    //Kept rather than detached, because this function joins it at the bottom - see there. Failure
    //to start does not come back as a status: std::thread reports it by throwing, and this build
    //is -fno-exceptions, so it aborts on the spot. Either way the old Fatal() on a NULL handle has
    //nothing left to guard.
    frame_thread = std::thread(FrameThreadFunction,this);

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

    /*
        The pump has ended, so the window is on its way out. Wait for the render thread - and
        through it the physics thread, which it joins itself - to finish the frame and the tick
        they are in, before Start() returns and main() lets the process go.

        RAISED HERE RATHER THAN ASSUMED, because the loop above has two exits and only one of them
        sets it: WM_QUIT breaks out with f_should_quit still false, and that is the same flag the
        render loop is watching. Joining without this would be joining a loop with no reason to
        end. Everything downstream hangs off this one line.
    */
    main_window->f_should_quit = true;
    if (frame_thread.joinable()){
        frame_thread.join();
    }
    debug->Info("Both threads joined, Start() returning\n");
}

void Application::UpdateInput(){
    if (!main_scene){
        return;
    }
    main_scene->UpdateInput();
}

void Application::UpdateTickInput(){
    if (!main_scene){
        return;
    }
    main_scene->UpdateTickInput();
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

//Virtual function. Should get overriden in each application that extends it.
void Application::Init(){
     //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    renderer->Init();
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //The return value is the point: CreateNewScene registers the scene and hands it back, it does
    //not decide which one is active. Without this assignment main_scene stays NULL and the line
    //below dereferences it - which no app hits today only because every one of them overrides
    //Init() wholesale.
    main_scene = CreateNewScene("Default Application Scene");

    //Just so the current items show on the first frame...?
    main_scene->UpdatePhysics(GetPhysicsTimestep());
}

void Application::DrawImGuiUI(){
    return;
}

/*
    The on-screen buttons, drawn from InputController's rect list.

    This is where the three pieces meet, and the shape is the point: InputController owns the
    rectangles and emits keycodes, UIOverlay owns the pixels, and neither knows about the other.
    Nothing flows back from here into input - SubmitPointer hit-tests the same list independently,
    so a button works exactly as well when nothing draws it (item 67).

    Run on the render thread with the overlay already Begun. Reading `f_down` here is a benign
    race with the window thread that writes it: the worst outcome is a button drawn lit one frame
    after it released, which is a frame of paint and not a missed input.
*/
void Application::DrawTouchButtons(){
    if (!overlay || !overlay->IsReady() || !main_window || !main_window->inputcontroller){
        return;
    }
    const std::vector<InputController::TouchButton>& buttons =
        main_window->inputcontroller->GetTouchButtons();
    if (buttons.empty()){
        return;
    }

    for (const InputController::TouchButton& b: buttons){
        vec2 min = vec2(b.rect.x,b.rect.y);
        vec2 max = vec2(b.rect.x + b.rect.w,b.rect.y + b.rect.h);
        //A corner radius proportional to the button rather than a constant, so the same layout in
        //millimetres looks the same on a dense screen as on a coarse one once item 70 lands.
        float radius = ((b.rect.w < b.rect.h) ? b.rect.w : b.rect.h) * 0.22f;

        uint32_t fill    = b.f_down ? UIColor(90,190,255,150) : UIColor(230,238,255,40);
        uint32_t outline = b.f_down ? UIColor(200,235,255,255) : UIColor(200,215,240,120);

        overlay->AddRect(min,max,radius,fill);
        overlay->AddRectOutline(min,max,radius,2.0f,outline);

        if (b.label[0]){
            //Sized from the BUTTON, not from a fixed point size - dpi-correct by construction,
            //because the button already is whatever the layout made it.
            float size = b.rect.h * 0.30f;
            //y is a baseline, so the text is nudged down from the centre by roughly a third of its
            //height to sit optically centred rather than hanging above the middle.
            vec2 at = vec2(b.rect.x + b.rect.w * 0.5f,b.rect.y + b.rect.h * 0.5f + size * 0.34f);
            overlay->AddText(b.label,at,size,UIColor(255,255,255,b.f_down ? 255 : 190),
                             UI_ALIGN_CENTER);
        }
    }
}

//Function for rendering the frame to a window
void Application::FrameThreadFunction(Application* app){
    if (!app){
        debug->Err("No application was supplied to FrameThread\n");
        return;
    }

    app->thread_id_render = std::this_thread::get_id();
    debug->Info("FrameFunction thread id: %s\n",ThreadIdText(app->thread_id_render).c_str());

    //We make the window's context current to this thread
    if (!wglMakeCurrent(app->main_window->hDC, app->main_window->hRC)){
        debug->Err("FrameFunction Thread unable to get context by wglMakeCurrent\n");
        return;
    }

    if (!app->main_window->InitImGui()){
        debug->Fatal("Failed to setup ImGui on Window\n");
    }

    app->Init();

    /*
        The 2D overlay, HERE rather than in Application::Init, because Init is virtual and an app
        may replace it wholesale rather than calling the base - ApplicationTetris does exactly
        that, to build a deferred renderer instead of the default one. Anything core needs for
        every app therefore cannot live in Init. This spot is core-owned, on the render thread
        with the context current, and after the app has declared its asset roots.

        A missing font or shader is not fatal: Init logs, returns false, and the overlay stays
        un-ready, so every Add* is a no-op and the app runs without a HUD rather than not at all.
    */
    app->overlay = new UIOverlay();
    app->overlay->Init();

    //Before RegisterCoreMCPTools: those tools submit commands, so the handlers have to be in
    //place before any of them can be called.
    app->RegisterCoreCommandHandlers();
    app->RegisterCoreMCPTools();

    //Only now, after the concrete app's Init() has fully returned (and so
    //registered every tool it means to register - see ApplicationTank::RegisterMCPTools),
    //start accepting MCP requests. Init() is where per-app tools get registered, not the
    //constructor, and it runs here on the render thread, potentially taking several seconds
    //(asset/shader loading) - starting the server any earlier races a client's initial
    //tools/list against that registration, resulting in only the built-in "status" tool
    //ever being returned.
    //
    //Unconditional, and nothing here names MCPServer: with USE_MCP=0 this and the
    //RegisterCoreMCPTools above are the no-op twins in core/ApplicationMCP_none.cpp. See
    //the note at the top of core/ApplicationMCP.cpp for why that is a swapped file rather
    //than an #ifdef.
    app->StartMCPServer();

    //Now that all the setup is done, we create another thread for physics.
    app->StartPhysicsThread();

    while (app->main_window->f_should_quit == false){
        app->tmr_render_loop->Stop();
        app->tmr_render_loop->Restart();
        if (app->main_window->f_resized){
            app->main_window->f_resized = false;
            app->renderer->Resize(app->main_window->width,app->main_window->height);
        }
        app->DrawFrame();
    }

    //The window is going away, and the physics thread is this thread's to stop: it started it, and
    //that loop reads the renderer and the scene this thread has just stopped drawing. Stopping it
    //HERE rather than leaving it to process exit is the difference between a tick that finishes
    //and a tick cut in half somewhere inside the solver.
    app->StopPhysicsThread();

    debug->Info("FrameThreadFunction terminated\n");
}

void Application::DrawFrame(){
    //Before PreRender, not after: an app's PreRender may dispatch a compute shader or fill a
    //texture with one of the programs about to be rebuilt, and it should be using the new one on
    //the frame it arrives rather than the frame after.
    ServiceShaderReload();

    //Any GL work the app needs done before the scene is drawn - see Application::PreRender.
    PreRender();

    //Tell ImGui to start a new frame
    main_window->ImGuiNewFrame();

    //This should render the objects and whatever it wants
    if (main_scene){
        main_scene->DrawFrame();
    }

    /*
        The 2D overlay, between the scene and the panels.

        Renderer::DrawFrame has just left resolve_fbo_id bound holding the resolved scene, and
        nothing below rebinds - so this composites over the scene into the same buffer ImGui is
        about to draw into and SwapWindowBuffers is about to present. The batch is rebuilt from
        scratch every frame, which is why there is no dirty flag anywhere in UIOverlay.

        Note this lands AFTER Renderer's own scene-only screenshot capture and BEFORE the
        UI-inclusive one, so `screenshot include_ui:false` gives the clean 3D scene without the
        HUD, and the default includes it. That split already existed for ImGui and the overlay
        simply joins the UI side of it.
    */
    if (overlay){
        //Re-lay the on-screen buttons whenever the surface changes, and once before the first
        //frame. Here rather than at Init because the size is not final there - see
        //Application::LayoutTouchButtons for the case that proved it.
#if USE_TOUCH_UI
        if ((main_window->width != touch_layout_w) || (main_window->height != touch_layout_h)){
            touch_layout_w = main_window->width;
            touch_layout_h = main_window->height;
            LayoutTouchButtons(touch_layout_w,touch_layout_h);
        }
#endif

        overlay->Begin(main_window->width,main_window->height);
        //NOT gated: this is the app's own 2D HUD, which a desktop build wants as much as a phone
        //does. Only the touch BUTTONS are Android-only - see USE_TOUCH_UI in Application.h. An
        //app whose overlay draws labels FOR those buttons reads its own button indices, which
        //are -1 when they were never bound, so it goes quiet on its own.
        DrawOverlay();
#if USE_TOUCH_UI
        if (f_draw_touch_buttons){
            DrawTouchButtons();
        }
#endif
        renderer->BeginGPUPass(Renderer::GPU_PASS_OVERLAY);
        overlay->Draw();
        renderer->EndGPUPass(Renderer::GPU_PASS_OVERLAY);
    }

    //Overlay ImGui
    //This will access and modify physics, globally... all over the place.
    renderer->physics_mutex.lock();
    DrawImGuiUI();
    renderer->physics_mutex.unlock();

    //Finish ImGui
    renderer->BeginGPUPass(Renderer::GPU_PASS_IMGUI);
    main_window->ImGuiRenderDrawData();
    renderer->EndGPUPass(Renderer::GPU_PASS_IMGUI);

    //The UI-inclusive capture point. ImGui renders into whatever framebuffer is bound and nothing
    //above rebinds, so resolve_fbo_id now holds the scene with the panels composited on top -
    //which is exactly what SwapWindowBuffers is about to present. It has to happen HERE: before
    //ImGui there are no panels to capture, and after the swap the buffer's contents are no longer
    //guaranteed. A request that asked for the scene alone was already served inside
    //Renderer::DrawFrame and this is a no-op for it.
    if (renderer){
        renderer->CaptureScreenshotIfRequested(true);
    }

    //The frame boundary for the GPU timers: every pass has now had its chance to run, so the
    //ones that did not can be told they cost nothing. Here rather than at the end of
    //Renderer::DrawFrame because the overlay and ImGui passes above are outside it.
    if (renderer){
        renderer->EndGPUFrame();
    }

    //Copy to screen and finish
    main_window->SwapWindowBuffers();
}

/*
    Start and stop the simulation loop. See the block on these in Application.h for the shutdown
    order they are half of.

    They are a pair because the flag and the thread have to agree, and each half alone is a bug
    with a long fuse: creating the thread without raising f_physics_running first gives a loop that
    tests the flag as its first act and returns having simulated nothing, and clearing the flag
    without joining lets the loop run on into teardown, which is exactly the thing being fixed.
*/
void Application::StartPhysicsThread(){
    if (f_physics_running){
        debug->Warn("StartPhysicsThread: already running, ignoring\n");
        return;
    }
    f_physics_running = true;
    physics_thread = std::thread(PhysicsThreadFunction,this);
}

void Application::StopPhysicsThread(){
    //Not an error to stop something that never started: FrameThreadFunction can return early
    //(no context, no ImGui) without ever reaching StartPhysicsThread, and the shutdown path
    //below still runs.
    if (!f_physics_running){
        return;
    }
    f_physics_running = false;

    //The wait is bounded by one pass, which is one tick interval plus the work in it - except
    //under heavy slow motion, where physics_time_factor stretches that interval by up to 100x and
    //quitting can therefore take a noticeable moment. Paused is NOT one of those cases: a paused
    //pass still loops at the normal rate, it just does not tick.
    if (physics_thread.joinable()){
        physics_thread.join();
    }
    debug->Info("Physics thread stopped after %llu ticks\n",
                main_scene ? (unsigned long long)main_scene->GetPhysicsTick() : 0ULL);
}

void Application::PhysicsThreadFunction(Application* app){
    if (!app){
        debug->Err("No application was supplied to PhysicsThread\n");
        return;
    }

    //Recorded here rather than by whoever started us, so it is set by the thread it describes -
    //the same way the render thread records its own.
    app->thread_id_physics = std::this_thread::get_id();
    debug->Info("Physics thread id: %s\n",ThreadIdText(app->thread_id_physics).c_str());

    //Setup debugging to run from this thread:
    app->debug_physics = new Debugger("App.Physics", DEBUG_ALL);

    //Paces this loop. Constructed on the thread that uses it so its timer handle - and, on the
    //fallback path, the raised system timer period - lives exactly as long as the thread does.
    PrecisionSleeper sleeper;
    if (!sleeper.IsHighResolution()){
        app->debug_physics->Warn("Physics pacing is on the low-resolution timer path\n");
    }
    sleeper.ResetSchedule();

    //Tested once per pass rather than once per tick, so a scene that is paused - or not there at
    //all - still shuts down promptly. StopPhysicsThread is what clears it.
    while (app->f_physics_running){
        if (app->main_scene){
            //How long one tick should take in REAL time. physics_time_factor stretches or
            //compresses this interval only - the timestep handed to the simulation stays
            //GetPhysicsTimestep() no matter what, so slow motion is "fewer ticks per second",
            //never "smaller ticks". Recomputed every loop because the UI slider can change it
            //mid-run. Clamped low so a factor near zero can't produce an infinite interval.
            double us_looptime_desired = app->physics_us_per_tick / max(app->physics_time_factor,0.01f);

            //Time spent on aquiring a lock
            app->renderer->physics_mutex.lock();

            //Before anything reads main_scene this pass. A switch requested from another thread
            //lands HERE and nowhere else, so BeginPass, the tick and UpdateView below all see the
            //same scene - see Application::ApplyPendingSceneSwitch.
            app->ApplyPendingSceneSwitch();

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
                //Scripted input advances HERE rather than in UpdateInput above, because only now
                //is it known that this pass runs a tick. An edge raised on a pass that does not
                //tick is cleared by NextInput() below without any gameplay seeing it - which is
                //what made every edge-triggered action undeliverable under sim_step. Backlog
                //item 84; the long version is on InputController::ApplyTickInput.
                app->UpdateTickInput();
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
            //
            //f_tick is handed on rather than assumed: an edge raised on a pass that did NOT tick
            //is kept if nothing has read it, so the next tick can still see it. That is the other
            //half of item 84 - see InputController::Tick and backlog item 88.
            app->renderer->physics_mutex.lock();
            app->NextInput(f_tick);
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








//--- Simulation commands ------------------------------------------------------------------

/*
    Registers the core handlers on EVERY scene the app built, not just the active one.

    Handlers live on a Scene, and a command is drained by the scene it was submitted to - so a
    second scene with no handlers accepts object_spawn, object_set_transform and the rest and then
    silently does nothing with them. That is the trap an app hits the moment it has more than one
    scene, and it costs nothing to close here: this runs once, after Init(), by which time every
    scene an app builds in Init exists.

    Each handler is bound to the scene it is registered on rather than reading main_scene, so it
    keeps acting on its own scene whatever is active when the command is drained.
*/
void Application::RegisterCoreCommandHandlers(){
    if (scenes.empty()){
        debug->Err("No scene to register core command handlers on\n");
        return;
    }
    for (Scene* scene:scenes){
        RegisterCoreCommandHandlers(scene);
    }
}

void Application::RegisterCoreCommandHandlers(Scene* scene){
    if (!scene){
        return;
    }

    //Teleport. Only the fields the flags mark as present are written - a command that just wants
    //to rotate something must not also stamp a zeroed position over it.
    scene->RegisterCommandHandler(SIM_CMD_OBJECT_SET_TRANSFORM,
        [this,scene](const SimCommand& cmd) -> objectid_t {
            Object* object = scene->FindObjectByID(cmd.target);
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
    scene->RegisterCommandHandler(SIM_CMD_OBJECT_SPAWN_ASSET,
        [this,scene](const SimCommand& cmd) -> objectid_t {
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
            scene->AddObject(object);
            return object->GetID();
        });

    //Every physics property the Inspector can change, in one handler. Same shape as
    //SET_TRANSFORM: one `if` per flag, so a command carrying one property leaves the other eight
    //alone. The boolean values live at the same bit positions in bool_values as their own flags,
    //which is what keeps this to one line each - see SimCommand.h.
    //Not registered at all without physics: the command exists in the enum either way (SimCommand.h
    //is shared), and an unregistered id already reports itself as unhandled - which is a truer
    //answer for a no-physics build than a handler that accepted the command and did nothing.
#ifdef USE_PHYSICS
    scene->RegisterCommandHandler(SIM_CMD_OBJECT_SET_PHYSICS,
        [this,scene](const SimCommand& cmd) -> objectid_t {
            Object* object = scene->FindObjectByID(cmd.target);
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
                if (scene->physics_world){
                    scene->physics_world->WakeUpEveryone();
                }
            }
            if (cmd.flags & SIM_CMD_FLAG_BOUNCINESS){
                physics->SetBounciness(cmd.value[1]);
            }
            return object->GetID();
        });
#endif

    //The engine's own object types, for the things that come from no asset (Add > Empty/Camera/
    //Light). Only the scene INSERTION is a command; anything that needs the GL context or the
    //GLTF loader (importing a skinned mesh, say) stays on the render thread and hands the
    //finished asset to SPAWN_ASSET afterwards.
    scene->RegisterCommandHandler(SIM_CMD_OBJECT_SPAWN_PRIMITIVE,
        [this,scene](const SimCommand& cmd) -> objectid_t {
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
            scene->AddObject(object);
            return object->GetID();
        });

    scene->RegisterCommandHandler(SIM_CMD_OBJECT_DUPLICATE,
        [this,scene](const SimCommand& cmd) -> objectid_t {
            Object* source = scene->FindObjectByID(cmd.target);
            if (!source){
                debug->Err("SimCommand duplicate: no object with id %u\n",cmd.target);
                return OBJECTID_INVALID;
            }
            Object* duplicated = new Object(source);
            scene->AddObject(duplicated);
            //Deliberately AFTER AddObject, and only when the submitter asked for it: a copy that
            //starts inactive can be dragged into place before it begins falling, which is why the
            //Inspector's Duplicate button clears the flag.
#ifdef USE_PHYSICS
            Physics* physics = duplicated->GetPhysics();
            if (physics && (cmd.flags & SIM_CMD_FLAG_ACTIVE)){
                physics->SetActive(!!(cmd.bool_values & SIM_CMD_FLAG_ACTIVE));
            }
#endif
            return duplicated->GetID();
        });

    scene->RegisterCommandHandler(SIM_CMD_OBJECT_DESTROY,
        [this,scene](const SimCommand& cmd) -> objectid_t {
            Object* object = scene->FindObjectByID(cmd.target);
            if (!object){
                debug->Err("SimCommand destroy: no object with id %u\n",cmd.target);
                return OBJECTID_INVALID;
            }
            objectid_t id = object->GetID();
            //MARKS it - see the note on SIM_CMD_OBJECT_DESTROY. Waking the world first, while the
            //body still exists, is what lets anything resting on it start falling.
#ifdef USE_PHYSICS
            Physics* physics = object->GetPhysics();
            if (physics && physics->world){
                physics->world->WakeUpEveryone();
            }
#endif
            object->Destroy();
            return id;
        });

#ifdef USE_PHYSICS
    scene->RegisterCommandHandler(SIM_CMD_WORLD_SET_GRAVITY,
        [this,scene](const SimCommand& cmd) -> objectid_t {
            if (!scene->physics_world){
                debug->Err("SimCommand set_gravity: scene has no physics world\n");
                return OBJECTID_INVALID;
            }
            scene->physics_world->SetGravity(cmd.velocity);
            return OBJECTID_INVALID; //no object involved - the sequence alone says it landed
        });

    //The collider gizmo resolves its rp3d::Collider* HERE, on the physics thread, from the index
    //the command carried - which is the point: the pointer never travels, so it cannot be stale
    //and cannot end up in a recording.
    scene->RegisterCommandHandler(SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO,
        [this,scene](const SimCommand& cmd) -> objectid_t {
            Object* object = scene->FindObjectByID(cmd.target);
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
            scene->AddObject(gizmo);
            return gizmo->GetID();
        });
#endif
}

//Every UI path that changes the simulation goes through here. The UI must NEVER wait for a
//command: DrawImGuiUI runs with renderer->physics_mutex held, and the physics thread needs that
//same mutex to reach Scene::DrainCommands, so blocking on it deadlocks on the spot. So this is
//deliberately fire-and-forget, and the panel simply shows the new value on a later frame - which
//is exactly how an ImGui widget behaves anyway, since it re-reads the object every frame.
/*
    Asks for a different scene to become the active one. Safe to call from ANY thread.

    It only records the request; ApplyPendingSceneSwitch does the actual swap on the physics
    thread. main_scene is read by the physics thread on every pass and by the render thread on
    every frame, so writing it from a third thread - an MCP handler, an ImGui button - is a plain
    data race, and a swap halfway through a pass would simulate one scene and draw another.
*/
void Application::RequestActiveScene(Scene* scene){
    if (!scene){
        return;
    }
    pending_scene.store(scene);
}

/*
    PHYSICS THREAD ONLY, and only with physics_mutex held - it is called from one place, at the top
    of the pass in PhysicsThreadFunction, so that a whole pass sees one scene from BeginPass to
    UpdateView.

    THE RENDER THREAD IS NOT SYNCHRONISED WITH THIS, deliberately. It reads main_scene once per
    frame outside this lock, so it can draw the OUTGOING scene for at most one more frame after a
    switch. That is harmless for two reasons and it is worth knowing both: a frame of the previous
    screen at a transition is not perceptible, and - the one that actually matters - NOTHING HERE
    DELETES A SCENE. Both stay alive in `scenes` for the life of the app. Freeing the outgoing
    scene on switch is exactly what would turn that benign stale frame into a use-after-free.
*/
void Application::ApplyPendingSceneSwitch(){
    Scene* requested = pending_scene.exchange(NULL);
    if (!requested || (requested == main_scene)){
        return;
    }
    debug->Info("Active scene: '%s' -> '%s'\n",
                main_scene ? main_scene->name.c_str() : "(none)",requested->name.c_str());
    main_scene = requested;
}

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

/*
    The render-thread half of shader_reload. Called from the top of DrawFrame.

    Building the ANSWER here rather than in the tool handler is deliberate: `progid` and
    `compile_log` are only meaningful immediately after the rebuild, and reading them from the
    waiting thread afterwards would be a race with the next frame's reload.
*/
void Application::ServiceShaderReload(){
    std::string filter;
    {
        std::lock_guard<std::mutex> lock(shader_reload_mutex);
        if (!f_shader_reload_pending){
            return;
        }
        filter = shader_reload_filter;
    }

    json reloaded = json::array();
    json available = json::array();
    //ForEachShader holds the registry lock for the duration, which is what makes walking it from
    //here safe against an app building a shader on another thread. Reloading inside it is fine -
    //Reload creates no Shader objects, so there is nothing to recurse into the lock.
    Shader::ForEachShader([&](Shader* shader){
        if (!shader){
            return;
        }
        //A compute program has no vertex stage, so fname is the only name it has.
        std::string label = shader->fname.empty() ? shader->vname : shader->fname;
        available.push_back(label);
        if (filter.empty()){
            return;
        }
        bool f_match = (!shader->fname.empty() && shader->fname.find(filter) != std::string::npos) ||
                       (!shader->vname.empty() && shader->vname.find(filter) != std::string::npos);
        if (!f_match){
            return;
        }
        bool f_ok = shader->Reload();
        reloaded.push_back(json{
            {"shader",label},
            {"vertex",shader->vname},
            {"ok",f_ok},
            {"program",shader->progid},
            {"log",shader->compile_log},
            {"source_files",shader->source_files}
        });
    });

    json result;
    result["reloaded"] = reloaded;
    result["available"] = available;
    if (filter.empty()){
        result["note"] = "no `name` given, so nothing was reloaded - `available` lists what there is";
    }else if (reloaded.empty()){
        result["error"] = "no shader's vertex or fragment file name contains \"" + filter +
                          "\" - see `available`";
    }

    {
        std::lock_guard<std::mutex> lock(shader_reload_mutex);
        shader_reload_result = result;
        shader_reload_filter.clear();
        f_shader_reload_pending = false;
    }
}



void Application::NextInput(bool f_ticked){
    if (!main_scene){
        return;
    }
    if (!main_scene->inputcontroller){
        return;
    }
    main_scene->inputcontroller->Tick(f_ticked);
}


void Application::RenderDebugMenuBarClass(){
    return;
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
    std::thread::id called_thread_id = std::this_thread::get_id();
    debug->Info("GetAssetsFromGLTF called from thread id: %s\n", ThreadIdText(called_thread_id).c_str());
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
    std::thread::id called_thread_id = std::this_thread::get_id();
    debug->Info("GetAllAssetsFromGLTF called from thread id: %s\n", ThreadIdText(called_thread_id).c_str());
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


//--- The device's media volume ------------------------------------------------------------------
/*
    See the declarations in Application.h for what these are for and why a caller must respect
    -1. There is no win32 implementation yet, so this is the whole of it here.

    The __ANDROID__ half is deliberately kept as a visible empty branch rather than left out: the
    Android port (C:/code/android) has a working JNI version of exactly these three functions
    against AudioManager, and naming the branch here is what stops the next person concluding
    the feature does not exist rather than that this platform does not implement it.
*/
#ifdef __ANDROID__
    //Implemented in the Android port against AudioManager/STREAM_MUSIC. Note it must also
    //ADJUST_UNMUTE on any non-zero value: setStreamVolume alone leaves an explicitly muted
    //stream silent, which makes the control look broken in the exact case it exists for.
#else

/*
    -1 is the documented "no control available" answer, and a caller that respects it hides its
    volume UI rather than drawing one that does nothing.

    A win32 version would go through IAudioEndpointVolume (mmdeviceapi/endpointvolume), but that
    adjusts the WHOLE ENDPOINT rather than this process's stream - a game quietly turning the
    speakers down for every other app on the machine is a ruder thing than not having the button,
    so this stays unimplemented until it is per-session (ISimpleAudioVolume) rather than global.
*/
int  Application::GetSystemVolume() const    { return -1; }
int  Application::GetMaxSystemVolume() const { return -1; }
void Application::SetSystemVolume(int volume){ (void)volume; }

#endif //__ANDROID__
