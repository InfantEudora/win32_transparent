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
    }
    controlled_tank->name = "Tank";
    main_scene->AddObject(controlled_tank);

    controlled_tank->AddPhysics(main_scene->physics_world);
    if (Physics* physics = controlled_tank->GetPhysics()){
        //Two capsule colliders spanning the tracks (left/right) instead of one box. A single box
        //only contacts the terrain along one face/edge, so on a slope it can hinge/tip around that
        //single contact line - two colliders separated sideways give two contact lines instead,
        //closer to how actual tracks would resist tipping.
        //Sized from the tank_tracks mesh itself, not the hull - the hull's extent.y is basically
        //the whole vehicle's half-height, which made the capsules almost as fat as the tank (visible
        //as the oversized blob). The tracks mesh's own bounds run Y:[0, 0.245], X:[-0.354, 0.346]
        //(bottom sits at local Y=0, same ground-level convention as the hull), so its own half-extent
        //doubles as both the correct radius and the correct center height.
        vec3 extent = controlled_tank->GetMesh()->GetExtents() * 0.5f;
        vec3 track_extent = tank_tracks ? tank_tracks->GetMesh()->GetExtents() * 0.5f : extent;
        float capsule_radius = track_extent.y;
        float capsule_length = max(track_extent.z * 2.0f - capsule_radius * 2.0f,0.01f);
        float track_offset_x = track_extent.x - capsule_radius;
        //Capsules stand along local Y by default; rotate onto Z to run along the hull's length.
        quat capsule_orientation(vec3(1,0,0),TYPE_PI * 0.5f);

        //AddCapsuleCollider's density param is kg/m^3, not total kg - derive it from the desired
        //~100kg total mass and the capsules' own combined volume (cylinder + 2 hemisphere caps).
        float capsule_volume = TYPE_PI * capsule_radius * capsule_radius * (capsule_length + (4.0f/3.0f) * capsule_radius);
        float target_mass_kg = 100.0f;
        float density = target_mass_kg / (capsule_volume * 2.0f);

        //SetFrictionCoefficient/SetBounciness only touch body->collider, which points at whichever
        //collider was added last - so each needs to be set right after its own AddCapsuleCollider,
        //not once at the end (that would leave the first capsule at rp3d's default material).
        physics->AddCapsuleCollider(capsule_radius,capsule_length,vec3(-track_offset_x,capsule_radius,0),capsule_orientation,density);
        physics->SetFrictionCoefficient(0.5f);
        physics->SetBounciness(0.00f);

        physics->AddCapsuleCollider(capsule_radius,capsule_length,vec3( track_offset_x,capsule_radius,0),capsule_orientation,density);
        physics->SetFrictionCoefficient(0.5f);
        physics->SetBounciness(0.00f);

        physics->SetStatic(false);
        physics->SetGravityEnabled(true);

    }

    target = CreateNewObjectFromGLTF("target",main_scene);
    controlled_tank->turret_target = target;
    target->SetPickability(false);

    //terrain = CreateNewObjectFromGLTF("terrain",main_scene);


    //TestHeightmapRoundTrip();
    TestHeightmapMesh();

    RegisterMCPTools();

    main_window->Resize(1600,800);
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
    }
    if (controlled_tank->turret){
        vec3 turret_forward = controlled_tank->turret->GetWorldForward(STATE_ACCESS_RENDERER);
        result["turret_forward"] = json::array({turret_forward.x,turret_forward.y,turret_forward.z});
    }
    return result;
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
        "as tank_telemetry) - no need for a separate call to see the outcome.",
        json{
            {"type","object"},
            {"properties", {
                {"direction", {{"type","string"},{"enum", json::array({"forward","reverse","brake","stop"})}}},
                {"amount", {{"type","number"},{"description","0..1 throttle/brake magnitude, default 1"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}}
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
            return GetTankTelemetry();
        });

    MCPServer::Get()->RegisterTool("tank_steer",
        "Steer the tank hull left or right. Held for duration_ms of real time (re-asserted every "
        "physics tick server-side) - default 100ms, about as short as a real key tap. This call "
        "blocks until duration_ms has elapsed and returns the resulting telemetry (same shape as "
        "tank_telemetry) - no need for a separate call to see the outcome.",
        json{
            {"type","object"},
            {"properties", {
                {"direction", {{"type","string"},{"enum", json::array({"left","right"})}}},
                {"amount", {{"type","number"},{"description","0..1 turn-rate magnitude, default 1"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}}
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
            return GetTankTelemetry();
        });

    MCPServer::Get()->RegisterTool("tank_telemetry",
        "Report the tank hull's current position, facing, and physics state (velocity, "
        "angular velocity, mass, whether the rigidbody is asleep).",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json & /*args*/) -> json {
            return GetTankTelemetry();
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
}
