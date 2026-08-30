#include "ApplicationTank.h"
#include "Debug.h"
#include "type_helpers.h"
#include "MCPServer.h"
#include <cmath>

static Debugger *debug = new Debugger("ApplicationTank", DEBUG_ALL);

ApplicationTank::ApplicationTank():Application(){
    debug->Info("Created new ApplicationTank.\n");
};

void ApplicationTank::Init(void){
    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    assetmanager = new AssetManager();

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics(1/50.0f);

    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetGravity(vec3(0,-9.81,0));
    main_scene->physics_world->SetDebugRendering(false);

    {
        //Setup sun light
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Directional Light (Sun)";
        sun->SetPosition(vec3(10,7,9));
        sun->color = vec3(1,0.85,0.7);
        sun->brightness = 30.0;
        sun->viewport.zoom = 10;
        sun->SetLookAt(vec3());
        main_scene->AddObject(sun);
    }

    gltfloader.LoadGLTFFile("data/tank.glb");
    GetAllAssetsFromGLTF();

    compass = CreateNewObjectFromGLTF("compass",main_scene);

    controlled_tank = new TankCharacter();
    assetmanager->GetObjectFromAsset("tank_base",controlled_tank);
    Object* tank_top = assetmanager->GetObjectFromAsset("tank_top");
    if (tank_top){
        tank_top->name = "Tank Top";
        controlled_tank->AttachChild(tank_top);
        controlled_tank->turret = tank_top;

    }
    Object* tank_tracks = assetmanager->GetObjectFromAsset("tank_tracks");
    if (tank_tracks){
        tank_tracks->name = "Tank Tracks";
        controlled_tank->AttachChild(tank_tracks);
        //Superseded visually by the per-wheel tank_wheel Objects set up below (one per
        //TankWheel, at its actual raycast mount point) - kept attached (for its geometry, still
        //used to size the suspension below) but hidden rather than removed.
        tank_tracks->SetVisibility(false);
    }
    controlled_tank->name = "Tank";
    main_scene->AddObject(controlled_tank);

    controlled_tank->AddPhysics(main_scene->physics_world);
    if (Physics* physics = controlled_tank->GetPhysics()){
        //A single box collider now provides mass and incidental collision (walls, other objects -
        //none exist yet, but this is what would catch them) only. It is NOT what the tank rests
        //or drives on: ground support and steering both come from several raycast-sampled wheel
        //contacts per track instead (TankCharacter::SetupWheels/UpdatePhysicsState) - a spring+
        //damper suspension force plus that side's own drive force at each point, closer to how a
        //tracked vehicle actually moves and far less sensitive to the terrain heightmap's small-
        //scale noise than one rigid shape resting directly on it.
        //
        //Sized from the tank_tracks mesh itself, not the hull - the hull's extent.y is basically
        //the whole vehicle's half-height. The tracks mesh's own bounds run Y:[0, 0.245],
        //X:[-0.354, 0.346] (bottom sits at local Y=0, same ground-level convention as the hull),
        //so its own half-extent doubles as both the wheels' mount height and their ray's rest
        //length - the hull floats at the same height the old rigid capsules used to sit at.
        vec3 extent = controlled_tank->GetMesh()->GetExtents() * 0.5f;
        vec3 track_extent = tank_tracks ? tank_tracks->GetMesh()->GetExtents() * 0.5f : extent;

        float target_mass_kg = 100.0f;
        float volume = max((extent.x * 2.0f) * (extent.y * 2.0f) * (extent.z * 2.0f),0.001f);
        float density = target_mass_kg / volume; //AddBoxCollider's density param is kg/m^3, not total kg

        //Raised so its bottom face clears the ground by track_extent.y (the same ground
        //clearance the wheels rest at) instead of sitting right at world Y=0 like the full hull
        //extent would put it - a box reaching all the way down to true ground level would be a
        //SECOND, independent rigid contact with the terrain fighting the wheels' spring contact
        //every tick, which is what was behind the hull never fully settling (confirmed: this is
        //exactly that same box, previously positioned at vec3(0,extent.y,0) spanning down to
        //Y=0). Top stays where the visual hull's top actually is - only the bottom is trimmed up.
        vec3 box_extent = vec3(extent.x,extent.y - track_extent.y * 0.5f,extent.z);
        vec3 box_center = vec3(0,extent.y + track_extent.y * 0.5f,0);
        physics->AddBoxCollider(box_extent,box_center,quat().identity(),density);
        physics->SetFrictionCoefficient(0.5f);
        physics->SetBounciness(0.0f);
        physics->SetStatic(false);
        physics->SetGravityEnabled(true);

        float track_offset_x = track_extent.x * 0.8f; //slightly inset from the tracks' outer edge
        controlled_tank->suspension_rest_length = track_extent.y;
        controlled_tank->SetupWheels(track_offset_x,track_extent.z,track_extent.y,5);

        //Visual reference only: one tank_wheel Object per TankWheel, parented to the hull and
        //placed at its actual mount point (the same local_offset UpdatePhysicsState raycasts
        //from) - so the wheel positions used by the physics are visible, not just the fixed
        //(now-hidden) tank_tracks band. Static for now, at the mount point itself rather than
        //wherever the current compression/ground contact has it - not yet following
        //wheel.compression tick to tick.
        for (TankWheel& wheel : controlled_tank->wheels){
            Object* wheel_visual = assetmanager->GetObjectFromAsset("tank_wheel");
            if (!wheel_visual){
                break; //asset missing - warned once via AssetManager's own debug->Err already
            }
            wheel_visual->name = "Tank Wheel";
            controlled_tank->AttachChild(wheel_visual);
            wheel_visual->SetPosition(wheel.local_offset);
            wheel.visual = wheel_visual;
        }
    }

    target = CreateNewObjectFromGLTF("target",main_scene);
    controlled_tank->turret_target = target;
    target->SetPickability(false);
    controlled_tank->SetPosition(vec3(0,0.05,0));

    //terrain = CreateNewObjectFromGLTF("terrain",main_scene);


    //TestHeightmapRoundTrip();
    TestHeightmapMesh();

    RegisterMCPTools();

    main_window->Resize(1200,800);

    //Need an inital step to show everything.
    main_scene->StepPhysics(1);

    main_scene->PausePhysics(true);
}

//Shared by all three MCP tools below - same fields tank_telemetry reports on its own,
//reused so tank_drive/tank_steer can hand back the resulting state without a separate call.
json ApplicationTank::GetTankTelemetry(){
    if (!controlled_tank){
        return json{ {"error","no tank"} };
    }
    vec3 pos = controlled_tank->GetPosition();
    vec3 forward = controlled_tank->GetForward();
    json result = {
        {"position", json::array({pos.x,pos.y,pos.z})},
        {"forward", json::array({forward.x,forward.y,forward.z})},
    };
    if (Physics *physics = controlled_tank->GetPhysics()){
        vec3 vel = physics->GetVelocity();
        vec3 angvel = physics->GetAngularVelocity();
        result["velocity"] = json::array({vel.x,vel.y,vel.z});
        result["speed"] = vel.length();
        result["angular_velocity"] = json::array({angvel.x,angvel.y,angvel.z});
        result["mass_kg"] = physics->GetMass();
        result["is_sleeping"] = physics->IsSleeping();
        //The body's transform origin (reported as "position" above) is NOT its centre of mass -
        //with the hull's box collider centred well above the hull origin the two sit roughly
        //half a metre apart, which is what made every lever arm in TankCharacter's wheel loop
        //wrong. Reported so that offset is visible rather than something to rederive by hand.
        vec3 com_local = physics->GetCenterofMass();
        vec3 com_world = physics->GetBodyWorldPosition() + physics->GetBodyWorldOrientation() * com_local;
        result["center_of_mass_local"] = json::array({com_local.x,com_local.y,com_local.z});
        result["center_of_mass_world"] = json::array({com_world.x,com_world.y,com_world.z});
    }
    if (controlled_tank->turret){
        vec3 turret_forward = controlled_tank->turret->GetWorldForward(STATE_ACCESS_RENDERER);
        result["turret_forward"] = json::array({turret_forward.x,turret_forward.y,turret_forward.z});
    }

    //Per-wheel suspension/force breakdown, straight from the TankWheel diagnostics the physics
    //thread wrote on its last tick (see TankWheel in tank/TankCharacter.h). The hull-level
    //fields above only ever say THAT something is wrong; this says which contact is doing it.
    //Read unsynchronized while the physics thread writes, same as the pedal inputs already are.
    //
    //What to look for: point_speed at or above max_point_speed means that clamp is holding the
    //simulation together rather than merely trimming it, i.e. the tuning underneath is
    //diverging. lateral_force is the one to watch for roll trouble - it carries the largest
    //coefficient in the system, so a left/right pair disagreeing in sign while the hull is
    //level is a contact fighting the suspension instead of helping it.
    json wheels = json::array();
    int wheels_grounded = 0;
    bool point_speed_clamped = false;
    for (const TankWheel& wheel : controlled_tank->wheels){
        if (wheel.grounded){
            wheels_grounded++;
        }
        if (wheel.point_speed > controlled_tank->max_point_speed){
            point_speed_clamped = true;
        }
        wheels.push_back(json{
            {"side", wheel.is_left_side ? "left" : "right"},
            {"local_offset", json::array({wheel.local_offset.x,wheel.local_offset.y,wheel.local_offset.z})},
            {"grounded", wheel.grounded},
            {"compression", wheel.compression},
            {"compression_rate", wheel.compression_rate},
            {"point_speed", wheel.point_speed},
            {"spring_force", wheel.spring_force},
            {"drive_force", wheel.drive_force},
            {"longitudinal_force", wheel.longitudinal_force},
            {"lateral_force", wheel.lateral_force},
        });
    }
    result["wheels"] = wheels;
    result["wheels_grounded"] = wheels_grounded;
    //Surfaced on its own rather than left to be spotted in the per-wheel list: if this is ever
    //true the point_velocity clamp is load-bearing, which invalidates reading any force below
    //it as a real physical value.
    result["point_speed_clamped"] = point_speed_clamped;
    //Echoed so a reader can judge the numbers above against the tuning that produced them
    //without a separate lookup or a rebuild to check what the constants currently are.
    result["tuning"] = json{
        {"suspension_stiffness", controlled_tank->suspension_stiffness},
        {"suspension_damping", controlled_tank->suspension_damping},
        {"suspension_rest_length", controlled_tank->suspension_rest_length},
        {"suspension_travel", controlled_tank->suspension_travel},
        {"lateral_friction", controlled_tank->lateral_friction},
        {"engine_force", controlled_tank->engine_force},
        {"brake_force", controlled_tank->brake_force},
        {"top_speed", controlled_tank->top_speed},
        {"max_wheel_force", controlled_tank->max_wheel_force},
        {"max_point_speed", controlled_tank->max_point_speed},
        {"max_roll_speed", controlled_tank->max_roll_speed},
    };
    return result;
}

//If requested, blocks (Renderer::RequestScreenshot) until the render thread has captured
//and PNG-encoded the current frame, and attaches it to result as an MCP image content
//block. A no-op passthrough otherwise, so every MCP tool below can opt into a screenshot
//with the same one line.
json ApplicationTank::MaybeAttachScreenshot(json result, bool include_screenshot){
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

//Exposes the tank's existing input methods (the same ones RunLogic already calls for
//keyboard input) and its live physics state over MCP. Handlers run on MCPServer's own
//stdin-reading thread, writing the same gas_pedal/brake_pedal/steering_position floats
//RunLogic writes from the main thread and UpdatePhysicsState reads/resets on the physics
//thread - unsynchronized, but no more so than that existing main/physics-thread relationship
//already is, and a stale/torn single frame here is harmless for a control input.
//
//tank_drive/tank_steer block for duration_ms (Sleep on this call's own TCPServer receive
//thread - the HTTP transport is already one-request-at-a-time synchronous, so this doesn't
//stall anything else) and return the telemetry that resulted, instead of firing the hold
//and returning immediately. A caller otherwise has no way to know when the hold has actually
//played out without a separate tank_telemetry round-trip guessing at a wait in between -
//this collapses "apply input, wait it out, read the result" into one call.
void ApplicationTank::RegisterMCPTools(){
    MCPServer::Get()->RegisterTool("tank_drive",
        "Drive the tank hull forward or reverse, or release the pedals. The input is held for "
        "duration_ms of real time (re-asserted every physics tick server-side), not just for the "
        "instant of this call - a single MCP round-trip can't reliably out-pace the physics tick "
        "rate, so without this a call would produce almost no motion, the same way an unrealistically "
        "brief key tap wouldn't. Default duration is 100ms, about as short as a real key tap. This "
        "call blocks until duration_ms has elapsed and returns the resulting telemetry (same shape "
        "as tank_telemetry) - no need for a separate call to see the outcome. Set include_screenshot "
        "to also get a PNG of the resulting frame, to see what happened rather than just read numbers.",
        json{
            {"type","object"},
            {"properties", {
                {"direction", {{"type","string"},{"enum", json::array({"forward","reverse","brake","stop"})}}},
                {"amount", {{"type","number"},{"description","0..1 throttle/brake magnitude, default 1"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the resulting frame, default false"}}}
            }},
            {"required", json::array({"direction"})}
        },
        [this](const json &args) -> json {
            if (!controlled_tank){
                return json{ {"error","no tank"} };
            }
            float amount = args.value("amount",1.0f);
            float duration_ms = clamp(args.value("duration_ms",100.0f),0.0f,15000.0f);
            std::string direction = args.value("direction","stop");
            if (direction == "forward"){
                controlled_tank->HoldDrive(false,amount,duration_ms);
            }else if (direction == "reverse"){
                controlled_tank->HoldDrive(true,amount,duration_ms);
            }else if (direction == "brake"){
                controlled_tank->HoldBrake(amount,duration_ms);
            }else{
                controlled_tank->ReleaseInputs();
            }
            Sleep((DWORD)duration_ms);
            return MaybeAttachScreenshot(GetTankTelemetry(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_steer",
        "Steer the tank hull left or right. Held for duration_ms of real time (re-asserted every "
        "physics tick server-side) - default 100ms, about as short as a real key tap. This call "
        "blocks until duration_ms has elapsed and returns the resulting telemetry (same shape as "
        "tank_telemetry) - no need for a separate call to see the outcome. Set include_screenshot "
        "to also get a PNG of the resulting frame, to see what happened rather than just read numbers.",
        json{
            {"type","object"},
            {"properties", {
                {"direction", {{"type","string"},{"enum", json::array({"left","right"})}}},
                {"amount", {{"type","number"},{"description","0..1 turn-rate magnitude, default 1"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the resulting frame, default false"}}}
            }},
            {"required", json::array({"direction"})}
        },
        [this](const json &args) -> json {
            if (!controlled_tank){
                return json{ {"error","no tank"} };
            }
            float amount = args.value("amount",1.0f);
            float duration_ms = clamp(args.value("duration_ms",100.0f),0.0f,15000.0f);
            std::string direction = args.value("direction","left");
            float signed_amount = (direction == "right") ? amount : -amount;
            controlled_tank->HoldSteer(signed_amount,duration_ms);
            Sleep((DWORD)duration_ms);
            return MaybeAttachScreenshot(GetTankTelemetry(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_telemetry",
        "Report the tank hull's current position, facing, and physics state (velocity, "
        "angular velocity, mass, whether the rigidbody is asleep). Set include_screenshot to "
        "also get a PNG of the current frame.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the current frame, default false"}}}
            }}
        },
        [this](const json &args) -> json {
            return MaybeAttachScreenshot(GetTankTelemetry(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_screenshot",
        "Capture a PNG screenshot of the currently rendered frame, along with the same "
        "telemetry tank_telemetry reports. Blocks briefly for the render thread to finish "
        "the frame already in progress and encode the image - use this to actually look at "
        "the tank/terrain rather than infer what happened from numbers alone.",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json & /*args*/) -> json {
            return MaybeAttachScreenshot(GetTankTelemetry(),true);
        });

    MCPServer::Get()->RegisterTool("tank_pause",
        "Pause or resume the physics simulation. While paused, the render loop keeps running "
        "(the window stays responsive) but nothing physical moves until either tank_step "
        "advances it manually or this is called again with paused=false. Useful for inspecting "
        "exactly what a single physics tick does instead of guessing how long to sleep.",
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
            json result = GetTankTelemetry();
            result["paused"] = main_scene->IsPhysicsPaused();
            return result;
        });

    MCPServer::Get()->RegisterTool("tank_step",
        "Advance the physics simulation by exactly num_steps ticks (each the same fixed "
        "timestep a normally-running frame would use) while paused, then return the resulting "
        "telemetry - lets you single-step the simulation deterministically rather than driving "
        "for some guessed duration and polling. Requires physics to already be paused via "
        "tank_pause; returns an error otherwise. Blocks until the physics thread has actually "
        "consumed the requested steps.",
        json{
            {"type","object"},
            {"properties", {
                {"num_steps", {{"type","number"},{"description","how many physics ticks to advance, default 1"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the resulting frame, default false"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!main_scene->IsPhysicsPaused()){
                return json{ {"error","physics is not paused - call tank_pause with paused=true first"} };
            }
            int num_steps = max((int)args.value("num_steps",1.0f),0);
            main_scene->StepPhysics(num_steps);

            //Physics ticks run on their own thread at its own pace - poll briefly for it to
            //actually consume what was just queued rather than guessing a fixed sleep.
            int timeout_ms = max(2000,num_steps * 30);
            for (int waited_ms = 0; waited_ms < timeout_ms && main_scene->GetPendingPhysicsSteps() > 0; waited_ms += 5){
                Sleep(5);
            }
            return MaybeAttachScreenshot(GetTankTelemetry(),args.value("include_screenshot",false));
        });
}

//Debug helper: write a small hand-picked grid of known heights to a PNG, read it back,
//and compare - to verify the SaveHeightmapPNG/LoadHeightmapPNG round-trip is correct
//before building the real mesh-to-heightfield sampler on top of it.
void ApplicationTank::TestHeightmapRoundTrip(){
    const int w = 4;
    const int h = 4;
    const float min_height = -1.0f;
    const float max_height = 1.0f;

    float original[w*h] = {
        -1.00f, -0.50f,  0.00f,  0.50f,
         1.00f,  0.25f, -0.25f,  0.75f,
        -0.75f,  0.10f, -0.10f,  0.33f,
         0.00f,  1.00f, -1.00f, -0.66f,
    };

    const char* filename = "tank/heightmap_roundtrip_test.png";
    if (!SaveHeightmapPNG(filename,original,w,h,min_height,max_height)){
        debug->Err("TestHeightmapRoundTrip: save failed\n");
        return;
    }

    std::vector<float> loaded;
    int loaded_w = 0, loaded_h = 0;
    if (!LoadHeightmapPNG(filename,loaded,loaded_w,loaded_h,min_height,max_height)){
        debug->Err("TestHeightmapRoundTrip: load failed\n");
        return;
    }

    if (loaded_w != w || loaded_h != h){
        debug->Err("TestHeightmapRoundTrip: size mismatch, expected %ix%i got %ix%i\n",w,h,loaded_w,loaded_h);
        return;
    }

    //8-bit quantization over a range of 2.0 gives a max error of about 2.0/255 ~= 0.0078 per step.
    const float tolerance = (max_height - min_height) / 255.0f;
    float max_error = 0.0f;
    for (int i = 0; i < w*h; i++){
        float error = fabs(loaded[i] - original[i]);
        max_error = max(max_error,error);
        debug->Info("  [%2i] original=%6.3f loaded=%6.3f error=%6.4f\n",i,original[i],loaded[i],error);
    }
    if (max_error <= tolerance){
        debug->Ok("TestHeightmapRoundTrip: PASSED (max error %.4f, tolerance %.4f)\n",max_error,tolerance);
    }else{
        debug->Err("TestHeightmapRoundTrip: FAILED (max error %.4f, tolerance %.4f)\n",max_error,tolerance);
    }
    Debugger::Flush(); //Make sure the verdict above is visible immediately, not stuck in the console buffer.
}

//Debug helper: build a renderable mesh straight from the same test heightmap PNG
//(no raycast sampling involved - Blender can already produce heightmaps directly).
void ApplicationTank::TestHeightmapMesh(){
    std::vector<float> heights;
    int w = 0, h = 0;
    Texture tex;
    if (!LoadHeightmapPNG("tank/export_terrain_photoshop.png",heights,w,h,-1.0f,1.0f,&tex)){
        debug->Err("TestHeightmapMesh: failed to load heightmap\n");
        return;
    }

    //Bake the target 10x10 world footprint into the mesh itself rather than via
    //Object::SetScale afterward - normals are computed from these final positions,
    //so a post-hoc non-uniform scale wouldn't be reflected in them.
    float cell_size_x = 10.0f / (w - 1);
    float cell_size_z = 10.0f / (h - 1);
    Mesh* mesh = CreateMeshFromHeightmap(heights,w,h,cell_size_x,cell_size_z);
    if (!mesh){
        debug->Err("TestHeightmapMesh: failed to build mesh\n");
        return;
    }

    heightmap_mesh_test = new Object();
    heightmap_mesh_test->SetMesh(mesh);
    heightmap_mesh_test->name = "Heightmap Test Mesh";
    heightmap_mesh_test->SetPosition(vec3(0,0,0)); //Off to the side so it doesn't overlap the tank.

    heightmap_mesh_test->AddPhysics(main_scene->physics_world);
    if (Physics* physics = heightmap_mesh_test->GetPhysics()){
        //Same cell_size_x/cell_size_z as the render mesh above, so collision matches what's drawn.
        physics->AddHeightFieldCollider(heights,w,h,cell_size_x,cell_size_z,vec3(0,0,0),quat().identity());
        physics->SetFrictionCoefficient(0.01f);
        physics->SetBounciness(0.00f);
        physics->SetStatic(true);
    }

    main_scene->AddObject(heightmap_mesh_test);
}

//Debug helper: dump the raw vertex data of the terrain mesh, to see what order/layout
//Blender's glTF export gives us before we try to turn it into a heightfield.
void ApplicationTank::DumpTerrainVertices(){
    if (!terrain || !terrain->GetMesh()){
        debug->Warn("DumpTerrainVertices: no terrain mesh loaded.\n");
        return;
    }
    const std::vector<vertex>& verts = terrain->GetMesh()->GetVertices();
    debug->Info("Terrain mesh: %zu vertices (%zu triangles)\n",verts.size(),verts.size()/3);
    for (size_t i = 0; i < verts.size(); i++){
        const vertex& v = verts[i];
        debug->Info("  [%4zu] pos=(%8.3f, %8.3f, %8.3f)\n",i,v.pos.x,v.pos.y,v.pos.z);
    }
}

//Called before update physics
void ApplicationTank::RunLogic(){
    //Only when in focus
    if (!main_window->f_has_focus){
        return;
    }

    //Shortcuts
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    //Track the target on the Y=0 plane under the mouse cursor.
    if (target){
        int2 px = input->GetRelativeMousePosition();
        ray r = camera->GetPixelRay(px);
        vec3 at = {};
        projection_plane.normal = vec3(0,1,0);
        projection_plane.pos = vec3(0,0,0);
        if (r.intersects_plane(projection_plane,at)){
            target->SetPosition(at);
        }
    }

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }

    CheckObjectSelection();

    if (controlled_tank){
        if (input->IsKeyDown(INPUT_TURN_UP)){
            controlled_tank->Accelerate(1.0f);
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            controlled_tank->Reverse(1.0f);
        }
        if (input->IsKeyDown(INPUT_TURN_LEFT)){
            controlled_tank->SteerLeft(1.0f);
        }
        if (input->IsKeyDown(INPUT_TURN_RIGHT)){
            controlled_tank->SteerRight(1.0f);
        }
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

    //Mouse wheel for zoom
    static float mouse_delta_sum = 0;
    if (mouse_delta_sum != 0){
        vec3 diff = camera->GetForward() - camera_target;
        float dist = diff.length() * mouse_delta_sum;

        camera->MoveForwardBy(dist / 50.0f);

        mouse_delta_sum /= 1.1;
    }
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
}

void ApplicationTank::DrawImGuiUI(){
    RenderDebugMenuBar();
    RenderApplicationUI();
    RenderTankWheelDebugUI();
}

void ApplicationTank::RenderTankWheelDebugUI(){
    ImGui::Begin("Tank Wheels [Debug]");
    if (!controlled_tank){
        ImGui::Text("No controlled_tank");
        ImGui::End();
        return;
    }

    ImGui::Text("Gas Pedal      : %.2f",controlled_tank->gas_pedal);
    ImGui::Text("Brake Pedal    : %.2f",controlled_tank->brake_pedal);
    ImGui::Text("Steering       : %.2f",controlled_tank->steering_position);
    ImGui::Text("Reverse        : %s",controlled_tank->f_reverse ? "true" : "false");
    ImGui::Separator();

    if (ImGui::BeginTable("tank_wheels",6,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg)){
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Grounded");
        ImGui::TableSetupColumn("Compression (m)");
        ImGui::TableSetupColumn("Local Offset");
        ImGui::TableSetupColumn("Roll Angle");
        ImGui::TableHeadersRow();

        int i = 0;
        for (TankWheel& wheel:controlled_tank->wheels){
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%i",i);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s",wheel.is_left_side ? "Left" : "Right");

            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(wheel.grounded ? ImVec4(0.3f,1.0f,0.3f,1.0f) : ImVec4(1.0f,0.4f,0.4f,1.0f),
                                wheel.grounded ? "Yes" : "No");

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.4f",wheel.compression);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.2f, %.2f, %.2f",wheel.local_offset.x,wheel.local_offset.y,wheel.local_offset.z);

            ImGui::TableSetColumnIndex(5);
            ImGui::Text("%.2f",wheel.roll_angle);

            ImGui::PopID();
            i++;
        }
        ImGui::EndTable();
    }
    ImGui::End();
}
