#include "ApplicationTank.h"
#include "Debug.h"
#include "type_helpers.h"
#include "MCPServer.h"
#include <cmath>
#include <cstdlib>

#define INPUT_FIRE INPUT_LAST+1

//Crane-only actuator keys. The boom's elevation and the base's slew reuse the same WASD the
//vehicles drive with (a crane's boom is its "forward/back" and its slew is its "left/right", so
//the mapping reads the same way whichever rig has the keyboard - see RunLogic); these two pairs
//are the axes a vehicle has no equivalent of, so they need keys of their own.
#define INPUT_CRANE_EXTEND      INPUT_LAST+2
#define INPUT_CRANE_RETRACT     INPUT_LAST+3
#define INPUT_CRANE_HOOK_DOWN   INPUT_LAST+4
#define INPUT_CRANE_HOOK_UP     INPUT_LAST+5
//Toggles the magnet - a latching switch, not a held actuator like the four above, so it is read
//on the key's RELEASE edge (same as INPUT_FIRE) rather than while it is down.
#define INPUT_CRANE_MAGNET      INPUT_LAST+12

#define GAMEPAD_LEFT_STICK_X    INPUT_LAST+6
#define GAMEPAD_LEFT_STICK_Y    INPUT_LAST+7
#define GAMEPAD_RIGHT_STICK_X   INPUT_LAST+8
#define GAMEPAD_RIGHT_STICK_Y   INPUT_LAST+9
#define GAMEPAD_L2             INPUT_LAST+10
#define GAMEPAD_R2             INPUT_LAST+11

//Scalar control axes. Any source can drive these - today it is the MCP tools acting as a scripted
//player (see InputController::HoldAxis); the gamepad sticks and the keyboard still go straight to
//the Vehicle below. Drive is signed: positive accelerates, negative reverses.
#define INPUT_AXIS_DRIVE       INPUT_LAST+20
#define INPUT_AXIS_STEER       INPUT_LAST+21
#define INPUT_AXIS_BRAKE       INPUT_LAST+22
//Per-track commands for the direct-track test rig (TankCharacter::direct_track_control), signed
//like drive. These bypass the throttle/steer mix, so a scripted player can command an exact
//differential - the case the mixed axes cannot express cleanly, and the one the open
//"won't yaw on a same-direction differential" question needs.
#define INPUT_AXIS_TRACK_L     INPUT_LAST+23
#define INPUT_AXIS_TRACK_R     INPUT_LAST+24

static Debugger *debug = new Debugger("ApplicationTank", DEBUG_ALL);

ApplicationTank::ApplicationTank():Application(){
    debug->Info("Created new ApplicationTank.\n");
};

//Switches which vehicle the arrow keys/fire key drive (see the "Controlling" selector in
//RenderTankWheelDebugUI). Releases the outgoing vehicle's own pedals/steering/hold-latches
//first - without this, switching away mid-throttle would leave it silently coasting forever on
//whatever gas_pedal it last had (nothing re-asserts or decays it once nothing calls
//ApplyHoldLatches against a fresh key state for it - Accelerate/etc. are only ever called from
//here, on whichever vehicle is currently controlled).
void ApplicationTank::SetControlledVehicle(Vehicle* vehicle){
    if (controlled_vehicle && controlled_vehicle != vehicle){
        controlled_vehicle->ReleaseInputs();
    }
    //Same reasoning one rig over: the crane's actuators are velocity commands that hold until
    //something changes them, so handing the keyboard to a vehicle while the base is mid-slew
    //would leave it turning for good.
    if (f_crane_controlled){
        f_crane_controlled = false;
        ReleaseCraneInputs();
    }
    controlled_vehicle = vehicle;
}

//The other half of the same switch - see SetControlledVehicle above. Passing NULL there rather
//than duplicating the release is deliberate: there is exactly one place that lets go of a
//vehicle's pedals, and this goes through it.
void ApplicationTank::SetControlledCrane(){
    SetControlledVehicle(NULL);
    f_crane_controlled = true;
}

void ApplicationTank::ReleaseCraneInputs(){
    crane_slew_speed = crane_piston_speed = crane_extension_speed = crane_hook_speed = 0.0f;
    crane_hw_slew = crane_hw_boom = crane_hw_extension = crane_hw_hook = 0.0f;
    if (crane){
        crane->SetSlewSpeed(0.0f);
        crane->SetPistonSpeed(0.0f);
        crane->SetExtensionSpeed(0.0f);
        crane->SetHookSpeed(0.0f);
    }
}

Object* ApplicationTank::GetControlledObject(){
    if (f_crane_controlled){
        return crane; //the base - everything else on the crane hangs off it
    }
    return controlled_vehicle;
}

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

    //If we want reproducable random numbers, we can either set a seed and generate one.
    // or we can load a texture with random numbers in it. Both store as a texture.
    rrand = new RRandom();
    rrand->Generate(512,512);

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics(1/50.0f);
    main_scene->inputcontroller->AddKeyMap(VK_SPACE,INPUT_FIRE);
    //Crane-only keys - see the INPUT_CRANE_* defines. Only read while the crane is the rig the
    //"Controlling" selector has handed the keyboard to, so they cost the vehicles nothing.
    main_scene->inputcontroller->AddKeyMap('E',INPUT_CRANE_EXTEND);
    main_scene->inputcontroller->AddKeyMap('Q',INPUT_CRANE_RETRACT);
    main_scene->inputcontroller->AddKeyMap('F',INPUT_CRANE_HOOK_DOWN);
    main_scene->inputcontroller->AddKeyMap('R',INPUT_CRANE_HOOK_UP);
    main_scene->inputcontroller->AddKeyMap('G',INPUT_CRANE_MAGNET);

    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetGravity(vec3(0,-9.81,0));
    main_scene->physics_world->SetDebugRendering(false);

    gamepad_controller = main_window->inputcontroller;
    gamepad_controller->ListDevices();
    gamepad_controller->AddGamePadMap(0,GAMEPAD_LEFT_STICK_X);
    gamepad_controller->AddGamePadMap(1,GAMEPAD_LEFT_STICK_Y);
    gamepad_controller->AddGamePadMap(2,GAMEPAD_RIGHT_STICK_X);
    gamepad_controller->AddGamePadMap(3,GAMEPAD_RIGHT_STICK_Y);
    gamepad_controller->AddGamePadMap(4,GAMEPAD_L2);
    gamepad_controller->AddGamePadMap(5,GAMEPAD_R2);

    //System keycode 0: nothing on a keyboard produces these, they only ever carry a scalar.
    gamepad_controller->AddKeyMap(0,INPUT_AXIS_DRIVE);
    gamepad_controller->AddKeyMap(0,INPUT_AXIS_STEER);
    gamepad_controller->AddKeyMap(0,INPUT_AXIS_BRAKE);
    gamepad_controller->AddKeyMap(0,INPUT_AXIS_TRACK_L);
    gamepad_controller->AddKeyMap(0,INPUT_AXIS_TRACK_R);

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

        //A point light just above the floor, near where both vehicles spawn. The sun alone is
        //directional, so it lights every surface by its angle to one fixed direction and leaves
        //the underside of a vehicle - wheels, suspension, the gap the tracks sit in - flat and
        //unreadable, which is exactly the geometry this project is usually looking at. A local
        //light low to the ground puts a visible falloff and a real highlight on those parts.
        PointLight* floor_lamp = new PointLight();
        floor_lamp->name = "Point Light (Floor)";
        floor_lamp->SetPosition(vec3(0,1.0f,0));
        floor_lamp->color = vec3(1.0f,0.95f,0.85f);
        //4 rather than 8: at 8 the pool directly under it saturates to flat white and the
        //terrain's own relief inside that radius stops being readable at all, which defeats the
        //point of adding it. Tune freely - it is a debug/readability light, not an art choice.
        floor_lamp->brightness = 4.0f;
        main_scene->AddObject(floor_lamp);
    }

    gltfloader.LoadGLTFFile("data/tank.glb");
    GetAllAssetsFromGLTF();


    compass = assetmanager->GetObjectFromAsset("compass");
    main_scene->AddObject(compass);

    controlled_tank = new TankCharacter();
    assetmanager->GetObjectFromAsset("tank_base",controlled_tank);
    Object* tank_top = assetmanager->GetObjectFromAsset("tank_top");
    if (tank_top){
        tank_top->name = "Tank Top";
        controlled_tank->AttachChild(tank_top);
        controlled_tank->turret = tank_top;

    }
    tank_tracks = assetmanager->GetObjectFromAsset("tank_tracks");
    if (tank_tracks){
        tank_tracks->name = "Tank Tracks";
        controlled_tank->AttachChild(tank_tracks);
        //Superseded visually by the per-wheel tank_wheel Objects set up below (one per
        //Wheel, at its actual raycast mount point) - kept attached (for its geometry, still
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

        //Probed from the asset rather than instantiated, because every wheel's geometry below
        //depends on it and the visuals aren't created until further down. Rolls around the
        //hull's local X (left/right) axis (see TankCharacter::UpdatePhysicsState's spin code),
        //so its rolling radius is whichever of Y/Z is larger, not X - that's its width.
        if (Mesh* wheel_mesh = assetmanager->GetMeshFromAsset("tank_wheel")){
            vec3 wheel_extent = wheel_mesh->GetExtents() * 0.5f;
            controlled_tank->wheel_radius = max(wheel_extent.y,wheel_extent.z);
        }

        //rest_length is anchor-to-HUB, and the wheel's own radius hangs below that, so the
        //anchor-to-GROUND distance the tracks mesh actually dictates (track_extent.y) has to be
        //split between the two. Subtracting the radius here is what puts the tread on the
        //terrain instead of the axle: with the two equal, as they were, the hub came to rest at
        //ground level and 84% of each wheel sat below the surface - the buried look.
        //
        //Deliberately derived rather than re-probed off the mesh: leaving the anchor at
        //track_extent.y and taking the radius out of rest_length instead puts full extension at
        //exactly local Y=0 (0.1224 - 0.0513 - 0.0711), and makes the hull's resting height
        //come out at -compression whatever the radius is - algebraically the same expression it
        //was before, so ride height, droop, ray length and the equilibrium compression the
        //spring rate was tuned against are all unchanged. The wheels move; the force balance
        //does not. A missing wheel asset leaves wheel_radius at 0 and this at track_extent.y,
        //which is the old point-contact model exactly.
        //
        //Note the radius really is ~0.0711, not the 0.0754 the old hand-probed constant beside
        //the raised wheels claimed - that number never agreed with what the same mesh extents
        //produced at runtime, and nothing derives from it any more.
        controlled_tank->suspension_rest_length = track_extent.y - controlled_tank->wheel_radius;

        //6 road wheels, spanning the flat band of the tank_tracks mesh rather than its full
        //length - probed directly off the mesh (binned max-Y along Z): the profile is flat at
        //~0.145-0.155 from about z=-0.31 to z=+0.36, then climbs toward two raised humps at the
        //very ends (idler front, drive sprocket rear - see AddRaisedWheel calls below).
        //0.33 keeps all 6 comfortably inside that flat band rather than spilling into the climb.
        const float road_wheel_half_length = 0.38f;
        controlled_tank->SetupWheels(track_offset_x,road_wheel_half_length,track_extent.y,6);

        //The two raised wheels above the road-wheel band, per side - same probe: humps peak at
        //z~-0.57 (front idler, height 0.2447 = the mesh's overall max Y) and z~+0.57 (rear drive
        //sprocket, height 0.2192). Each wheel's TOP wants to sit at its hump's peak, so its hub
        //rests one radius below that - stated directly now that AddRaisedWheel takes a resting
        //hub height and works the anchor out itself. The hand-computed offset that used to be
        //here existed only to cancel a suspension hang these wheels never had; both it and the
        //hardcoded 0.0754 copy of the radius are gone, so swapping the wheel asset for a
        //different size now re-derives all of this on its own.
        //
        //These are contact-capable but undriven (see AddRaisedWheel). On level ground they stay
        //clear, confirmed against a settled hull over MCP: the rear sprocket, the lower of the
        //two, rests its tread 0.066 m above the terrain with its ray still stopping 0.047 m
        //short of it, and all four report grounded=false while all twelve road wheels carry an
        //even 0.0112 m of compression. Nothing about flat-ground behaviour changes. They bite
        //when there's something to bite - a step the idler noses into, a ledge the sprocket
        //comes down off - which is the whole reason for giving them a ray at all.
        //
        //A short arm (0.03 rest / 0.02 travel, against the road wheels' 0.0471 / 0.08) both
        //matches what a tensioner actually has and keeps that flat-ground clearance: the ray is
        //sized rest + travel + radius, so travel is what governs how far below the wheel it
        //still reaches. The axis leans them ~19 degrees toward their own end of the hull, so
        //they extend down-and-outward the way an idler arm swings rather than straight down.
        const float raised_rest_length = 0.03f;
        const float raised_travel = 0.02f;
        float raised_radius = controlled_tank->wheel_radius;
        controlled_tank->AddRaisedWheel(track_offset_x,-0.57f,0.2447f - raised_radius,
                                        raised_rest_length,raised_travel,vec3(0,-1,-0.35f));
        controlled_tank->AddRaisedWheel(track_offset_x,0.57f,0.2192f - raised_radius,
                                        raised_rest_length,raised_travel,vec3(0,-1,0.35f));

        //Visual reference only: one tank_wheel Object per Wheel, parented to the hull and
        //placed at its actual mount point (the same local_offset UpdatePhysicsState raycasts
        //from) - so the wheel positions used by the physics are visible, not just the fixed
        //(now-hidden) tank_tracks band. Followed tick to tick by UpdatePhysicsState (bobs with
        //compression, spins with roll_angle) once wheel_radius below is set.
        for (Wheel& wheel : controlled_tank->wheels){
            Object* wheel_visual = assetmanager->GetObjectFromAsset("tank_wheel");
            if (!wheel_visual){
                break; //asset missing - warned once via AssetManager's own debug->Err already
            }
            wheel_visual->name = "Tank Wheel";
            controlled_tank->AttachChild(wheel_visual);
            //Placed at the hub's resting position rather than at the anchor - the two are no
            //longer the same point. UpdatePhysicsState overwrites this every tick anyway; it
            //only matters for the frame before the first physics step.
            wheel_visual->SetPosition(wheel.local_offset +
                                      wheel.suspension_axis * controlled_tank->WheelRestLength(wheel));
            wheel.visual = wheel_visual;
            wheel.visual_natural_radius = controlled_tank->wheel_radius; //what tank_wheel's own mesh represents at scale 1 - see Wheel's comment
        }

        //All wheels laid out and the hull has its collider (mass): hand them to reactphysics3d.
        //From here on the wheels are simulated inside the physics world by a VehicleConstraint,
        //and TankCharacter::UpdatePhysicsState only decides torques - see core/Vehicle.h.
        controlled_tank->CreateVehicleConstraint();
    }

    target = assetmanager->GetObjectFromAsset("target");
    main_scene->AddObject(target);

    controlled_tank->turret_target = target;
    target->SetPickability(false);
    controlled_tank->SetPosition(vec3(-4,0.05,0));
    tank_start_position = controlled_tank->GetPosition();
    tank_start_rotation = controlled_tank->GetRotation();


    //Buggy: a 4-wheeled, front-steered vehicle sharing the tank's wheel/suspension code (see
    //core/Wheel.h/BuggyCharacter.h). Spawned alongside the tank rather than replacing it - both
    //simulate all the time, and the "Controlling" toggle in RenderTankWheelDebugUI decides which
    //one the arrow keys/fire key drive (see ApplicationTank::SetControlledVehicle).
    controlled_buggy = new BuggyCharacter();
    assetmanager->GetObjectFromAsset("buggy_base",controlled_buggy);
    if (Object* buggy_interior = assetmanager->GetObjectFromAsset("buggy_interior")){
        buggy_interior->name = "Buggy Interior";
        controlled_buggy->AttachChild(buggy_interior); //decoration only, no physics of its own - same role as the tank's tank_top
    }
    controlled_buggy->name = "Buggy";
    main_scene->AddObject(controlled_buggy);

    //Suspension test bed height - the body sits pinned here (see SetStatic(true) below) with
    //nothing under its wheels by default; buggy_test_cubes are what a wheel's raycast actually
    //finds, once dragged up into reach via the debug UI.
    const vec3 buggy_suspended_position(1.0f,1.5f,0.0f);

    controlled_buggy->AddPhysics(main_scene->physics_world);
    if (Physics* physics = controlled_buggy->GetPhysics()){
        //Front/rear wheels are visually different sizes (buggy_wheel_front/buggy_wheel_back) -
        //read directly off each mesh, same probing approach as the tank's single wheel_radius,
        //then applied per wheel below via Wheel::radius (0 = inherit controlled_buggy->wheel_radius).
        float front_radius = 0.0f, rear_radius = 0.0f;
        if (Mesh* front_mesh = assetmanager->GetMeshFromAsset("buggy_wheel_front")){
            vec3 e = front_mesh->GetExtents() * 0.5f;
            front_radius = max(e.y,e.z);
        }
        if (Mesh* rear_mesh = assetmanager->GetMeshFromAsset("buggy_wheel_back")){
            vec3 e = rear_mesh->GetExtents() * 0.5f;
            rear_radius = max(e.y,e.z);
        }
        controlled_buggy->wheel_radius = rear_radius > 0.0f ? rear_radius : front_radius;

        //Same "one box collider for mass/incidental collision only, the wheels do the actual
        //ground support" split as the tank - see its own collider comment above for why the
        //bottom face is trimmed up rather than left at true ground level (a second, independent
        //rigid contact would fight the wheels' own spring force every tick). Using a wheel
        //radius as the clearance stand-in here since there's no separate tracks-style mesh to
        //measure it from, same reasoning the tank uses wheel_radius for in its own rest_length.
        vec3 extent = controlled_buggy->GetMesh() ? controlled_buggy->GetMesh()->GetExtents() * 0.5f : vec3(1,0.4f,2);
        //Scale the collider a bit
        extent *= 0.85f;

        float ground_clearance = max(front_radius,rear_radius);
        vec3 box_extent = vec3(extent.x,max(extent.y - ground_clearance * 0.5f,0.01f),extent.z);
        vec3 box_center = vec3(0,extent.y + ground_clearance * 0.5f,0);

        float target_mass_kg = 60.0f; //first guess, lighter than the tank's own 100kg - expect to retune live, see BuggyCharacter's own tuning comments
        float volume = max((extent.x * 2.0f) * (extent.y * 2.0f) * (extent.z * 2.0f),0.001f);
        float density = target_mass_kg / volume;
        physics->AddBoxCollider(box_extent,box_center,quat().identity(),density);
        physics->AddBoxCollider(box_extent/2,box_center + vec3(0,extent.y,0),quat().identity(),density);
        physics->SetFrictionCoefficient(0.5f);
        physics->SetBounciness(0.0f);
        //Suspension test bed: the body is held STATIC (immune to every force, including its own
        //wheels' spring force and gravity) and hangs in mid-air, so each wheel's raycast/
        //compression/spring math still runs and its visual still bobs/scales, but nothing here
        //moves the chassis. What moves is buggy_test_cubes below - static box colliders you drag
        //up into a wheel's reach via the "Buggy Suspension Test Bed" debug UI panel, to watch one
        //wheel's suspension respond in isolation. Swap SetStatic(false) back on (and stop pinning
        //the body to a fixed height below) once it's time to actually drive the thing.
        physics->SetStatic(false);
        physics->SetGravityEnabled(true);

        //First-pass geometry derived from the body mesh's own extents, exactly like the tank's
        //own bootstrap numbers were before being probed/measured precisely (see its SetupWheels
        //call above) - expect these to be replaced with exact anchor points once you've placed
        //and measured buggy_suspension/buggy_wheel_front/buggy_wheel_back in Blender.
        float track_half_width = extent.x * 0.75f;
        float half_wheelbase = extent.z * 0.6f;
        float mount_height = extent.y * 0.5f;
        controlled_buggy->SetupWheels(track_half_width,half_wheelbase,mount_height);

        //Both axles: measured directly in Blender (2026-08-31) rather than derived from the body
        //mesh like the rest of this block - the real geometry is now the source of truth. Every
        //wheel here comes from the one front-left measurement: the right side of each axle
        //mirrors the left (negate X - same convention Wheel::is_left_side/local_offset.x already
        //use everywhere else), and the rear axle mirrors the front (negate the BLENDER-space Y,
        //i.e. before axis conversion - front and back sit the same distance out and up, just on
        //opposite ends of the wheelbase).
        //
        //Wheel::local_offset is the suspension ANCHOR, not the hub (see Wheel's own comment) -
        //the two were given as separate points, so suspension_axis/rest_length are derived from
        //the vector between them rather than assumed to be a plain vertical strut.
        {
            //The glTF export already re-derives mesh/node geometry into this engine's own axes,
            //but numbers copied BY HAND out of Blender's own transform panel are still in
            //Blender's axes (Z-up) and need converting: X is unchanged, Blender's Z becomes this
            //engine's Y, and Blender's Y becomes this engine's -Z. It's an orientation-preserving
            //change of basis (a plain rotation, not a reflection), so a rotation's quaternion
            //vector part (x,y,z) converts with the exact same remap; w is unaffected.
            auto FromBlenderPos = [](const vec3& b){ return vec3(b.x,b.z,-b.y); };
            auto FromBlenderRot = [](const quat& b){ return quat(b.x,b.z,-b.y,b.w); };

            //Front-left, in Blender's own axes - everything else in this block is mirrored from
            //just these three values.
            const vec3 front_left_hub_bl(-0.27f,0.35f,0.12f);
            const vec3 front_left_anchor_bl(-0.15f,0.33f,0.27f);
            //Component order assumed to be this engine's own (x,y,z,w), same as the position
            //fields above - not yet confirmed against Blender's own quaternion display order
            //(which shows W first), so this is the one part of this block still worth double-
            //checking against the render if the suspension mesh looks twisted.
            const quat front_left_suspension_rotation_bl(0.38f,0.0f,0.92f,0.0f);

            for (Wheel& wheel : controlled_buggy->wheels){
                bool is_left = wheel.is_left_side;
                bool is_front = wheel.is_front_side;

                //Mirror the rear axle from the front BEFORE converting axes - flipping Blender's
                //own Y (front/back) is what "the back wheel sits the same, just further back"
                //means in the space these numbers were measured in.
                vec3 hub_bl = front_left_hub_bl;
                vec3 anchor_bl = front_left_anchor_bl;
                if (!is_front){
                    hub_bl.y = -hub_bl.y;
                    anchor_bl.y = -anchor_bl.y;
                }
                vec3 hub = FromBlenderPos(hub_bl);
                vec3 anchor = FromBlenderPos(anchor_bl);
                quat suspension_rotation = FromBlenderRot(front_left_suspension_rotation_bl);
                if (!is_left){
                    //Mirroring a rotation across the vehicle's centreline (negate X) negates the
                    //other two vector components and keeps X and W - see core/Wheel.cpp's own
                    //note on this same reasoning for the TODO nearby.
                    hub.x = -hub.x;
                    anchor.x = -anchor.x;
                    suspension_rotation = quat(suspension_rotation.x,-suspension_rotation.y,
                                                -suspension_rotation.z,suspension_rotation.w);
                }

                vec3 diff = hub - anchor;
                float length = diff.length();
                wheel.local_offset = anchor;
                if (length > 0.0001f){
                    wheel.suspension_axis = diff * (1.0f / length);
                    wheel.rest_length = length;
                }
                wheel.suspension_visual_rotation = suspension_rotation;
                //The same wheel asset is used on both sides of each axle - flipping the hubcap
                //to face outward on the right needs the wheel's own base orientation mirrored
                //too, composed under the roll spin (and, for the front axle, the steer yaw) by
                //WheelSuspension::UpdateVisual/BuggyCharacter::UpdatePhysicsState. A 180 degree
                //rotation around UP (Y), same axis steering already rotates around, so the two
                //commute and the mirrored wheel's STEERING direction comes out correct (confirmed
                //live: a Z-axis flip here left the right wheel steering opposite the left).
                //
                //That same rotation-based mirror then gets ROLLING direction backwards instead
                //(confirmed live too - a rotation can only stay consistent with ONE other
                //rotation it's composed with, whichever shares its axis), so visual_mirrored
                //tells WheelSuspension::UpdateVisual to negate roll_angle for just this wheel's
                //own rendering - see its own comment for why that's the fix rather than a true
                //reflection (this renderer's fixed backface-culling winding order would need
                //handling too for a mirrored scale to render right-side-out).
                wheel.visual_base_rotation = is_left ? quat(0,0,0,1) : quat(vec3(0,1,0),TYPE_PI);
                wheel.visual_mirrored = !is_left;
            }
        }

        //One buggy_wheel_front/buggy_wheel_back and one buggy_suspension Object per Wheel,
        //parented to the body and placed at its actual mount point - same visual-reference role
        //as the tank's per-wheel tank_wheel Objects. Followed tick to tick by
        //WheelSuspension::UpdateVisual (bob+spin for the wheel, position+orient+scale for the
        //spring) once BuggyCharacter::UpdatePhysicsState starts running.
        for (Wheel& wheel : controlled_buggy->wheels){
            wheel.radius = wheel.is_front_side ? front_radius : rear_radius; //0 falls back to wheel_radius above if a mesh was missing

            Object* wheel_visual = assetmanager->GetObjectFromAsset(wheel.is_front_side ? "buggy_wheel_front" : "buggy_wheel_back");
            if (wheel_visual){
                wheel_visual->name = "Buggy Wheel";
                controlled_buggy->AttachChild(wheel_visual);
                wheel_visual->SetPosition(wheel.local_offset + wheel.suspension_axis * controlled_buggy->WheelRestLength(wheel));
                wheel.visual = wheel_visual;
                wheel.visual_natural_radius = wheel.is_front_side ? front_radius : rear_radius; //what THIS wheel's own mesh represents at scale 1 - see Wheel's comment
            }

            Object* suspension_visual = assetmanager->GetObjectFromAsset("buggy_suspension");
            if (suspension_visual){
                suspension_visual->name = "Buggy Suspension";
                controlled_buggy->AttachChild(suspension_visual);
                suspension_visual->SetPosition(wheel.local_offset); //the anchor - the modeled spring's own origin
                wheel.suspension_visual = suspension_visual;
            }
        }

        //Same as the tank: wheels laid out and measured, colliders on - into the physics world.
        controlled_buggy->CreateVehicleConstraint();
    }

/*
    for (int i=0;i<4;i++){
        //One static "crate" box per wheel, placed in WORLD space (not attached to the
        //body - the body doesn't move while suspended, but these need to be dragged
        //independently of it) below the wheel's own rest hub position, clear of every
        //wheel's raycast reach by default. Dragging one up in the "Buggy Suspension Test
        //Bed" debug UI panel is what a wheel's ray then actually finds.

        Object* test_cube = assetmanager->GetObjectFromAsset("crate");
        if (test_cube){
            test_cube->name = "Buggy Suspension Test Cube";
            main_scene->AddObject(test_cube);
            test_cube->AddPhysics(main_scene->physics_world);
            if (Physics* cube_physics = test_cube->GetPhysics()){
                vec3 cube_extent = test_cube->GetMesh() ? test_cube->GetMesh()->GetExtents() * 0.5f : vec3(0.25f,0.25f,0.25f);
                cube_physics->AddBoxCollider(cube_extent,vec3(0,cube_extent.y,0),quat().identity(),1.0f); //density irrelevant, static
                cube_physics->SetFrictionCoefficient(0.8f);
                cube_physics->SetBounciness(0.0f);
                cube_physics->SetStatic(true);
            }
            vec3 hub_rest_local = wheel.local_offset + wheel.suspension_axis * wheel.rest_length;
            test_cube->SetPosition(buggy_suspended_position + vec3(hub_rest_local.x,hub_rest_local.y - 0.7f,hub_rest_local.z));
            buggy_test_cubes.push_back(test_cube);
        }
    }*/


    controlled_buggy->SetPosition(buggy_suspended_position);
    buggy_start_position = controlled_buggy->GetPosition();
    buggy_start_rotation = controlled_buggy->GetRotation();
    controlled_buggy->power_split_front = 0.5f;

    //The default vehicle
    SetControlledVehicle(controlled_buggy);

    fire_impact_emitter = new ParticleEmitter(main_scene->physics_world);
    fire_impact_emitter->name = "Fire Impact Emitter";
    fire_impact_emitter->target_scene = main_scene;
    fire_impact_emitter->SetRandomGenerator(rrand);
    fire_impact_emitter->emission_properties.emission_direction = vec3(0,1,0);
    fire_impact_emitter->emission_properties.emission_spread = 360.0f; //outward in every direction, not a narrow cone
    fire_impact_emitter->emission_properties.particle_size_min = 0.15f;
    fire_impact_emitter->emission_properties.particle_size_max = 0.35f;
    fire_impact_emitter->emission_properties.particle_lifetime_min = 0.3f;
    fire_impact_emitter->emission_properties.particle_lifetime_max = 0.6f;
    fire_impact_emitter->emission_properties.emission_speed_min = 3.0f;
    fire_impact_emitter->emission_properties.emission_speed_max = 6.0f;
    main_scene->AddObject(fire_impact_emitter);

    //The particle template: a bare copy of the target marker's mesh/material - see
    //Particle::Particle(Particle*), which this mirrors by hand since target is a plain
    //Object, not itself a Particle. No collider, unlike some other apps' particle types -
    //this is a one-off visual burst, nothing needs to collide with it.
    Particle* target_particle = new Particle(main_scene->physics_world);
    target_particle->name = "Fire Impact Particle";
    target_particle->SetMesh(target->GetMesh());
    target_particle->SetMaterialNames(target->GetMaterialNames());
    //Falls back to the ground once emitted, rather than just coasting outward on its burst
    //velocity forever - AddPhysics defaults gravity off, same as every Object, so this has to
    //be requested. Read back and re-applied to each actual clone by Particle's copy
    //constructor (see its comment), since every EmitParticles spawn is a fresh physics body.
    target_particle->GetPhysics()->SetGravityEnabled(true);
    fire_impact_emitter->AddParticleType(target_particle);

    //One dust-kicking particle emitter per buggy wheel, parented to the BUGGY BODY rather than
    //the wheel itself (see buggy_wheel_spin_emitters' own comment for why) - repositioned onto
    //each wheel's own current spot every frame, bursting tiny cube particles while that wheel is
    //actively spinning out (see UpdateBuggyWheelSpinParticles).
    for (Wheel& wheel : controlled_buggy->wheels){
        ParticleEmitter* dust_emitter = new ParticleEmitter(main_scene->physics_world);
        dust_emitter->name = "Buggy Wheelspin Dust";
        dust_emitter->target_scene = main_scene;
        dust_emitter->SetRandomGenerator(rrand);
        dust_emitter->emission_properties.emission_direction = vec3(0,0,-1);
        dust_emitter->emission_properties.emission_spread = 100.0f; //a loose upward cone, not a narrow jet
        dust_emitter->emission_properties.particle_size_min = 0.03f;
        dust_emitter->emission_properties.particle_size_max = 0.07f;
        dust_emitter->emission_properties.particle_lifetime_min = 0.2f;
        dust_emitter->emission_properties.particle_lifetime_max = 0.4f;
        dust_emitter->emission_properties.emission_speed_min = 1.5f;
        dust_emitter->emission_properties.emission_speed_max = 3.5f;
        controlled_buggy->AttachChild(dust_emitter);

        //Template particle: a small cube, no collider (a one-off visual puff, nothing needs to
        //collide with it) - same reasoning as the fire-impact particle above.
        Particle* dust_particle = new Particle(main_scene->physics_world);
        dust_particle->name = "Buggy Wheelspin Dust Particle";
        //Never added to the scene (unlike every other GetObjectFromAsset("cube") call in this
        //file) - just borrowed for its mesh/material, then deleted, same as any other throwaway
        //local would be.
        if (Object* cube_ref = assetmanager->GetObjectFromAsset("cube")){
            dust_particle->SetMesh(cube_ref->GetMesh());
            dust_particle->SetMaterialNames(cube_ref->GetMaterialNames());
            delete cube_ref;
        }
        dust_particle->GetPhysics()->SetGravityEnabled(true);
        dust_emitter->AddParticleType(dust_particle);

        buggy_wheel_spin_emitters.push_back(dust_emitter);
    }

    //Recorded on 2026-08-31 via bridge_telemetry over MCP: drove the tank up to the ravine
    //notch just north-west of its start position, dropped the bridge in over MCP, then nudged
    //its position/yaw (the ~21.2 deg here) by hand over MCP until it spanned the gap cleanly -
    //this is that placement, made permanent. Shifted -20 in Z along with the heightmap terrain
    //below (see TestHeightmapMesh) so both sit well clear of the flat test ground AddTestSceneObjects
    //adds at the origin, keeping vehicle-dynamics testing there reproducible.
    SpawnBridge(vec3(-1.294021f,-0.48f,-26.942404f),21.199468f);

    //terrain = CreateNewObjectFromGLTF("terrain",main_scene);


    //TestHeightmapRoundTrip();
    TestHeightmapMesh();
    AddTestSceneObjects();

    //A joint-based test rig - see CraneCharacter's own header comment for what it's testing.
    //Placed near the origin (same area the tank/buggy/default camera lookat all already sit in -
    //see Application::CreateNewScene's own default camera pose) rather than off in a far corner,
    //so it's actually in view on startup without having to go hunting for it.
    crane = new CraneCharacter(assetmanager,main_scene->physics_world,main_scene,vec3(3.5f,0.0f,2.0f));
    main_scene->AddObject(crane);

    //Only for the crane magnet's field trigger - see onTrigger. Set after the crane exists so
    //there is never a callback into a half-built one.
    main_scene->physics_world->rp_world->setEventListener(this);

    RegisterCommandHandlers();
    RegisterMCPTools();

    main_window->Resize(1600,800);

    //Window stays 1600x800 (so ImGui - composited into the same shared framebuffer as the
    //3D scene, see Renderer.h's comment on viewport_x/viewport_width/viewport_height - keeps
    //its full canvas), but the 3D scene itself is confined to an 800x800 square docked to
    //the right, leaving the left 800px strip free of 3D geometry for ImGui panels to occupy.
    renderer->viewport_x = 800;
    renderer->viewport_width = 800;
    renderer->viewport_height = 800;

    //Need an inital step to show everything.
    main_scene->StepPhysics(1);

    main_scene->PausePhysics(false);
}

//reactphysics3d::EventListener - the crane magnet's proximity query. Runs on the physics thread
//from inside PhysicsWorld::Update, so this does no more than hand the crane the other half of
//each pair; the crane records it and decides afterwards, between steps (CraneCharacter's header
//comment says why that split is not optional).
void ApplicationTank::onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData){
    if (!crane || !crane->magnet_collider){
        return;
    }
    for (uint8_t i = 0; i < callbackData.getNbOverlappingPairs(); i++){
        rp3d::OverlapCallback::OverlapPair pair = callbackData.getOverlappingPair(i);
        //OverlapExit is the pair that has just STOPPED touching - reporting it as a candidate
        //would let the magnet grab something on its way out of the field.
        if (pair.getEventType() == rp3d::OverlapCallback::OverlapPair::EventType::OverlapExit){
            continue;
        }
        //Identified by collider pointer, not category bits: the hook carries two colliders (its
        //own box and the magnet sphere) and only one of them is the field.
        if (pair.getCollider1() == crane->magnet_collider){
            crane->OnMagnetFieldOverlap(pair.getCollider2());
        }else if (pair.getCollider2() == crane->magnet_collider){
            crane->OnMagnetFieldOverlap(pair.getCollider1());
        }
    }
}

SimCommand ApplicationTank::MakeCraneMagnetCommand(bool on) const{
    SimCommand cmd;
    cmd.type = TANK_CMD_CRANE_MAGNET;
    cmd.value[0] = on ? 1.0f : 0.0f;
    return cmd;
}

//Shared by all three MCP tools below - same fields tank_telemetry reports on its own,
//reused so tank_drive/tank_steer can hand back the resulting state without a separate call.
json ApplicationTank::GetCraneTelemetry(){
    if (!crane || !crane->boom_hinge || !crane->boom){
        return json{ {"error","no crane"} };
    }
    json result = {
        {"speed_command", crane_piston_speed},
        {"slew_speed_command", crane_slew_speed},
        {"slew_heading_deg", crane->GetSlewAngle() * 180.0f / TYPE_PI},
        {"magnet_enabled", crane->IsMagnetEnabled()},
        {"magnet_radius_m", crane->magnet_radius},
        {"magnet_max_mass_kg", crane->magnet_max_mass},
        {"magnet_grabbed", crane->GetGrabbedObject() ? json(crane->GetGrabbedObject()->name) : json(nullptr)},
        {"magnet_grabbed_mass_kg", crane->GetGrabbedMass()},
        {"magnet_in_field", crane->magnet_in_field},
        {"magnet_grabbable_in_field", crane->magnet_grabbable_in_field},
        {"hinge_angle_deg", crane->boom_hinge->getAngle() * 180.0f / TYPE_PI},
        {"limit_min_deg", crane->boom_min_angle * 180.0f / TYPE_PI},
        {"limit_max_deg", crane->boom_max_angle * 180.0f / TYPE_PI},
        {"motor_target_speed", crane->boom_hinge->getMotorSpeed()},
        {"motor_torque_nm", crane->boom_hinge->getMotorTorque(1.0f / physics_tps)},
        {"motor_max_torque_nm", crane->boom_hinge->getMaxMotorTorque()},
    };
    if (Physics* physics = crane->boom->GetPhysics()){
        vec3 pos = physics->GetBodyWorldPosition();
        vec3 vel = physics->GetVelocity();
        vec3 angvel = physics->GetAngularVelocity();
        rp3d::Vector3 inertia = physics->body->rigidbody->getLocalInertiaTensor();
        result["boom_position"] = json::array({pos.x,pos.y,pos.z});
        result["boom_velocity"] = json::array({vel.x,vel.y,vel.z});
        result["boom_angular_velocity"] = json::array({angvel.x,angvel.y,angvel.z});
        result["boom_mass_kg"] = physics->GetMass();
        result["boom_inertia"] = json::array({inertia.x,inertia.y,inertia.z});
    }
    if (crane->extension_slider && crane->extension){
        result["extension_speed_command"] = crane_extension_speed;
        result["extension_translation_m"] = crane->extension_slider->getTranslation();
        result["extension_limit_min_m"] = crane->extension_min;
        result["extension_limit_max_m"] = crane->extension_max;
        result["extension_motor_target_speed"] = crane->extension_slider->getMotorSpeed();
        result["extension_motor_force_n"] = crane->extension_slider->getMotorForce(1.0f / physics_tps);
        if (Physics* physics = crane->extension->GetPhysics()){
            vec3 pos = physics->GetBodyWorldPosition();
            vec3 vel = physics->GetVelocity();
            result["extension_position"] = json::array({pos.x,pos.y,pos.z});
            result["extension_velocity"] = json::array({vel.x,vel.y,vel.z});
            result["extension_mass_kg"] = physics->GetMass();
        }
    }
    if (crane->hook_slider && crane->hook && crane->swivel){
        result["hook_speed_command"] = crane_hook_speed;
        result["hook_cable_paid_out_m"] = crane->hook_slider->getTranslation();
        result["hook_limit_min_m"] = crane->hook_min;
        result["hook_limit_max_m"] = crane->hook_max;
        result["hook_motor_target_speed"] = crane->hook_slider->getMotorSpeed();
        result["hook_winch_force_n"] = crane->hook_slider->getMotorForce(1.0f / physics_tps);
        if (Physics* physics = crane->hook->GetPhysics()){
            vec3 pos = physics->GetBodyWorldPosition();
            vec3 vel = physics->GetVelocity();
            result["hook_position"] = json::array({pos.x,pos.y,pos.z});
            result["hook_velocity"] = json::array({vel.x,vel.y,vel.z});
        }
        if (Physics* physics = crane->swivel->GetPhysics()){
            vec3 pos = physics->GetBodyWorldPosition();
            result["swivel_position"] = json::array({pos.x,pos.y,pos.z});
        }
    }
    return result;
}

Vehicle* ApplicationTank::ResolveVehicleArg(const json& args){
    std::string name = args.value("vehicle","tank");
    if (name == "buggy"){
        return controlled_buggy;
    }
    return controlled_tank;
}

json ApplicationTank::GetTankTelemetry(){
    return GetVehicleTelemetry(controlled_tank);
}

json ApplicationTank::GetVehicleTelemetry(Vehicle* vehicle){
    if (!vehicle){
        return json{ {"error","no such vehicle"} };
    }
    vec3 pos = vehicle->GetPosition();
    vec3 forward = vehicle->GetForward();
    vec3 up = vehicle->GetUp();
    json result = {
        {"vehicle", vehicle == controlled_tank ? "tank" : (vehicle == controlled_buggy ? "buggy" : "other")},
        {"position", json::array({pos.x,pos.y,pos.z})},
        {"forward", json::array({forward.x,forward.y,forward.z})},
        {"up", json::array({up.x,up.y,up.z})},
        {"forward_speed", vehicle->forward_speed},
        //Whether the window had focus: hardware control (keyboard/gamepad) is only applied when it
        //does, and a released gamepad trigger writes Brake(0) - which wipes TankCharacter's idle
        //brake. So the same scripted drive coasts differently focused vs not. Reported so a
        //baseline comparison can tell the two apart instead of looking like a physics change.
        {"window_focus", main_window ? main_window->f_has_focus : false},
        //The driver-facing inputs as the vehicle actually received them, before any drivetrain
        //turns them into pedals or per-track commands - so a scripted run can tell "the input
        //never arrived" apart from "the input arrived and the physics ignored it".
        {"throttle_input", vehicle->throttle_input},
        {"steer_input", vehicle->steer_input},
        {"brake_input", vehicle->brake_input},
        {"gas_pedal", vehicle->gas_pedal},
        {"brake_pedal", vehicle->brake_pedal},
        {"steering_position", vehicle->steering_position},
        {"reverse", vehicle->f_reverse},
    };
    //The per-track rig's own state, when this is the tank and the rig is on: what each track was
    //actually commanded, which under direct control is the whole input and is NOT derivable from
    //throttle_input/steer_input (those are bypassed and read 0).
    if (vehicle == controlled_tank && controlled_tank){
        result["direct_track_control"] = controlled_tank->direct_track_control;
        if (controlled_tank->direct_track_control){
            result["left_track_input"] = controlled_tank->left_track_input;
            result["right_track_input"] = controlled_tank->right_track_input;
        }
    }
    if (Physics *physics = vehicle->GetPhysics()){
        vec3 vel = physics->GetVelocity();
        vec3 angvel = physics->GetAngularVelocity();
        result["velocity"] = json::array({vel.x,vel.y,vel.z});
        result["speed"] = vel.length();
        result["angular_velocity"] = json::array({angvel.x,angvel.y,angvel.z});
        //Yaw rate about the body's own up axis - what a pivot turn is measured by.
        result["yaw_rate"] = angvel.dot(up);
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

    //Per-wheel suspension/force breakdown, straight from the Wheel diagnostics the physics
    //thread wrote on its last tick (read back from the rp3d VehicleConstraint, see Wheel in
    //core/Wheel.h). The hull-level fields above only ever say THAT something is wrong; this
    //says which contact is doing it. Read unsynchronized while the physics thread writes, same
    //as the pedal inputs already are.
    //
    //What to look for: friction_saturated on a wheel means the tire is at its grip limit -
    //wheelspin under power, a locked wheel under the brake, or a sideways slide. lateral_force
    //is the one to watch for roll trouble: a left/right pair disagreeing in sign while the hull
    //is level is a contact fighting the suspension instead of helping it.
    json wheels = json::array();
    int wheels_grounded = 0;
    float total_spring_force = 0.0f;
    for (const Wheel& wheel : vehicle->wheels){
        if (wheel.grounded){
            wheels_grounded++;
        }
        total_spring_force += wheel.spring_force;
        wheels.push_back(json{
            {"side", wheel.is_left_side ? "left" : "right"},
            {"front", wheel.is_front_side},
            //Which wheel this is, and what it's currently allowed to do - without these the
            //raised idler/sprocket are indistinguishable from a road wheel that has simply
            //lost contact, and a compression of 0 reads the same either way.
            {"kind", wheel.is_road_wheel ? "road" : "raised"},
            {"driven", wheel.driven},
            {"steerable", wheel.steerable},
            {"can_contact_ground", wheel.can_contact_ground},
            //Included because compression is only interpretable against it: the wheel touches
            //down when its anchor is (rest_length + radius) above the terrain, so a reader
            //that assumes a point contact will misjudge every ride height by one radius.
            {"radius", vehicle->WheelRadius(wheel)},
            {"rest_length", vehicle->WheelRestLength(wheel)},
            {"travel", vehicle->WheelTravel(wheel)},
            {"local_offset", json::array({wheel.local_offset.x,wheel.local_offset.y,wheel.local_offset.z})},
            {"grounded", wheel.grounded},
            {"compression", wheel.compression},
            {"compression_rate", wheel.compression_rate},
            {"spring_force", wheel.spring_force},
            {"drive_force", wheel.drive_force},
            {"longitudinal_force", wheel.longitudinal_force},
            {"lateral_force", wheel.lateral_force},
            {"friction_budget", wheel.friction_budget},
            {"lateral_slip_angle_deg", wheel.lateral_slip_angle * 180.0f / TYPE_PI},
            {"friction_saturated", wheel.friction_saturated},
            {"steer_angle", wheel.steer_angle},
            {"angular_velocity", wheel.angular_velocity},
        });
    }
    result["wheels"] = wheels;
    result["wheels_grounded"] = wheels_grounded;
    //Against mass_kg * 9.81: at rest on level ground these two agree, so a mismatch is either
    //motion (a landing, a bounce) or a suspension that is not carrying the vehicle.
    result["total_spring_force"] = total_spring_force;
    result["has_vehicle_constraint"] = vehicle->rp_vehicle != NULL;

    //Echoed so a reader can judge the numbers above against the tuning that produced them
    //without a separate lookup or a rebuild to check what the constants currently are.
    if (vehicle == controlled_tank && controlled_tank){
        if (controlled_tank->turret){
            vec3 turret_forward = controlled_tank->turret->GetWorldForward();
            result["turret_forward"] = json::array({turret_forward.x,turret_forward.y,turret_forward.z});
        }
        result["tuning"] = json{
            {"suspension_stiffness", controlled_tank->suspension_stiffness},
            {"suspension_damping", controlled_tank->suspension_damping},
            {"suspension_rest_length", controlled_tank->suspension_rest_length},
            {"suspension_travel", controlled_tank->suspension_travel},
            {"lateral_friction", controlled_tank->lateral_friction},
            {"friction_coefficient", controlled_tank->friction_coefficient},
            {"wheel_mass", controlled_tank->wheel_mass},
            {"contact_samples", controlled_tank->contact_samples},
            {"engine_force", controlled_tank->engine_force},
            {"brake_force", controlled_tank->brake_force},
            {"top_speed", controlled_tank->top_speed},
            {"max_roll_speed", controlled_tank->max_roll_speed},
        };
    }else if (vehicle == controlled_buggy && controlled_buggy){
        result["tuning"] = json{
            {"suspension_stiffness", controlled_buggy->suspension_stiffness},
            {"suspension_damping", controlled_buggy->suspension_damping},
            {"suspension_rest_length", controlled_buggy->suspension_rest_length},
            {"suspension_travel", controlled_buggy->suspension_travel},
            {"lateral_friction", controlled_buggy->lateral_friction},
            {"friction_coefficient", controlled_buggy->friction_coefficient},
            {"wheel_mass", controlled_buggy->wheel_mass},
            {"contact_samples", controlled_buggy->contact_samples},
            {"engine_force", controlled_buggy->engine_force},
            {"brake_force", controlled_buggy->brake_force},
            {"top_speed", controlled_buggy->top_speed},
            {"power_split_front", controlled_buggy->power_split_front},
            {"max_steer_angle_degrees", controlled_buggy->max_steer_angle_degrees},
            {"max_roll_speed", controlled_buggy->max_roll_speed},
        };
    }
    return result;
}

//Creates the bridge from its asset, gives it a static box collider sized to its own mesh
bool ApplicationTank::SpawnBridge(const vec3& pos, float yaw_degrees){
    if (bridge || !assetmanager){
        return false;
    }
    bridge = assetmanager->GetObjectFromAsset("bridge");
    if (!bridge){
        return false;
    }
    bridge->name = "Bridge";
    main_scene->AddObject(bridge);

    bridge->AddPhysics(main_scene->physics_world);
    if (Physics* physics = bridge->GetPhysics()){
        vec3 extent = bridge->GetMesh() ? bridge->GetMesh()->GetExtents() * 0.5f : vec3(1,0.1f,1);
        extent.y *= 0.1f; //Thin asphalt.
        vec3 box_center = vec3(0,0.781,0);
        physics->AddBoxCollider(extent,box_center,quat().identity(),1.0f); //density is irrelevant, static
        physics->SetFrictionCoefficient(0.8f); //asphalt-ish grip for the tank's tracks
        physics->SetBounciness(0.0f);
        physics->SetStatic(true);
    }

    bridge->SetPosition(pos);
    bridge_yaw_degrees = yaw_degrees;
    bridge->SetRotation(quat(vec3(0,1,0),bridge_yaw_degrees * TYPE_PI / 180.0f));
    return true;
}

//Reports the bridge's current placement - position, the yaw we last set it to (see
//bridge_yaw_degrees's comment in the header for why that's tracked rather than decomposed
//back out of the quaternion), and the raw rotation quaternion for pasting a placement straight
//into code once it's been dialed in over MCP.
json ApplicationTank::GetBridgeTelemetry(){
    if (!bridge){
        return json{ {"error","no bridge - call bridge_spawn first"} };
    }
    vec3 pos = bridge->GetPosition();
    quat rot = bridge->GetRotation();
    return json{
        {"position", json::array({pos.x,pos.y,pos.z})},
        {"yaw_degrees", bridge_yaw_degrees},
        {"rotation_quat", json::array({rot.x,rot.y,rot.z,rot.w})},
    };
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
SimCommand ApplicationTank::MakeVehicleResetCommand(Vehicle* vehicle) const{
    SimCommand cmd;
    if (!vehicle){
        return cmd; //SIM_CMD_NONE
    }
    cmd.type = TANK_CMD_VEHICLE_RESET;
    cmd.target = vehicle->GetID();
    //The pose travels IN the command rather than being looked up by the handler, so the command
    //says everything about what it does. A replayed reset then reproduces the original pose even
    //if the app's recorded spawn point has since been moved in code.
    cmd.flags = SIM_CMD_FLAG_POSITION | SIM_CMD_FLAG_ROTATION;
    if (vehicle == controlled_buggy){
        cmd.position = buggy_start_position;
        cmd.rotation = buggy_start_rotation;
    }else{
        cmd.position = tank_start_position;
        cmd.rotation = tank_start_rotation;
    }
    return cmd;
}

void ApplicationTank::RegisterCommandHandlers(){
    if (!main_scene){
        debug_frame->Err("No scene to register tank command handlers on\n");
        return;
    }
    //Runs on the physics thread, at the top of the tick, with physics_mutex held - so ResetState
    //(which teleports the body, zeroes its velocities and clears every wheel) is safe to call
    //directly here, and that is the only place it ever is.
    //Runs on the physics thread at the top of the tick, BEFORE the step - so switching the
    //magnet off here (which is what drops the load) can never land mid-solve. The joint itself is
    //made and broken a little later in the same tick, in CraneCharacter::UpdateMagnet.
    main_scene->RegisterCommandHandler(TANK_CMD_CRANE_MAGNET,
        [this](const SimCommand& cmd) -> objectid_t {
            if (crane){
                crane->SetMagnetEnabled(cmd.value[0] != 0.0f);
            }
            return OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(TANK_CMD_VEHICLE_RESET,
        [this](const SimCommand& cmd) -> objectid_t {
            //Matched by ID against the vehicles this app owns rather than downcast from whatever
            //FindObjectByID returns: the command carries an id, and only these two objects are
            //Vehicles that this handler has any business resetting.
            Vehicle* vehicle = NULL;
            if (controlled_tank && cmd.target == controlled_tank->GetID()){
                vehicle = controlled_tank;
            }else if (controlled_buggy && cmd.target == controlled_buggy->GetID()){
                vehicle = controlled_buggy;
            }
            if (!vehicle){
                debug_physics->Err("vehicle_reset: id %u is not a vehicle of this app\n",cmd.target);
                return OBJECTID_INVALID;
            }
            vehicle->ResetState(cmd.position,cmd.rotation);
            return vehicle->GetID();
        });
}

void ApplicationTank::RegisterMCPTools(){
    MCPServer::Get()->RegisterTool("tank_drive",
        "Drive the tank hull forward or reverse, or release the pedals. The input is held for "
        "duration_ms, rounded up to whole physics ticks and re-asserted on each of them, not just for the "
        "instant of this call - a single MCP round-trip can't reliably out-pace the physics tick "
        "rate, so without this a call would produce almost no motion, the same way an unrealistically "
        "brief key tap wouldn't. Default duration is 100ms, about as short as a real key tap. This "
        "call blocks until duration_ms has elapsed and returns the resulting telemetry (same shape "
        "as tank_telemetry) - no need for a separate call to see the outcome. Set include_screenshot "
        "to also get a PNG of the resulting frame, to see what happened rather than just read numbers.",
        json{
            {"type","object"},
            {"properties", {
                {"vehicle", {{"type","string"},{"enum", json::array({"tank","buggy"})},{"description","which vehicle, default tank"}}},
                {"direction", {{"type","string"},{"enum", json::array({"forward","reverse","brake","stop"})}}},
                {"amount", {{"type","number"},{"description","0..1 throttle/brake magnitude, default 1"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the resulting frame, default false"}}}
            }},
            {"required", json::array({"direction"})}
        },
        [this](const json &args) -> json {
            Vehicle* vehicle = ResolveVehicleArg(args);
            if (!vehicle){
                return json{ {"error","no such vehicle"} };
            }
            float amount = args.value("amount",1.0f);
            float duration_ms = clamp(args.value("duration_ms",100.0f),0.0f,15000.0f);
            uint32_t duration_ticks = DurationMsToTicks(duration_ms);
            std::string direction = args.value("direction","stop");
            //MCP is a player: it holds a control on the InputController exactly as a person would,
            //and RunLogic drives whichever vehicle is being controlled. So "which vehicle" means
            //"take control of it" - there is no separate back door into a vehicle any more.
            SetControlledVehicle(vehicle);
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            if (direction == "forward"){
                input->HoldAxis(INPUT_AXIS_DRIVE,amount,duration_ticks);
            }else if (direction == "reverse"){
                input->HoldAxis(INPUT_AXIS_DRIVE,-amount,duration_ticks);
            }else if (direction == "brake"){
                input->HoldAxis(INPUT_AXIS_BRAKE,amount,duration_ticks);
            }else{
                input->ReleaseSynthetic();
                vehicle->ReleaseInputs();
            }
            //While paused (tank_pause) the hold plays out through tank_step instead, so there is
            //nothing to wait for here.
            if (!main_scene || !main_scene->IsPhysicsPaused()){
                Sleep(TicksToRealMs(duration_ticks));
            }
            return MaybeAttachScreenshot(GetVehicleTelemetry(vehicle),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_steer",
        "Steer the tank hull left or right. Held for duration_ms, rounded up to whole physics ticks (re-asserted on each of "
        "physics tick server-side) - default 100ms, about as short as a real key tap. This call "
        "blocks until duration_ms has elapsed and returns the resulting telemetry (same shape as "
        "tank_telemetry) - no need for a separate call to see the outcome. Set include_screenshot "
        "to also get a PNG of the resulting frame, to see what happened rather than just read numbers.",
        json{
            {"type","object"},
            {"properties", {
                {"vehicle", {{"type","string"},{"enum", json::array({"tank","buggy"})},{"description","which vehicle, default tank"}}},
                {"direction", {{"type","string"},{"enum", json::array({"left","right"})}}},
                {"amount", {{"type","number"},{"description","0..1 turn-rate magnitude, default 1"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the resulting frame, default false"}}}
            }},
            {"required", json::array({"direction"})}
        },
        [this](const json &args) -> json {
            Vehicle* vehicle = ResolveVehicleArg(args);
            if (!vehicle){
                return json{ {"error","no such vehicle"} };
            }
            float amount = args.value("amount",1.0f);
            float duration_ms = clamp(args.value("duration_ms",100.0f),0.0f,15000.0f);
            uint32_t duration_ticks = DurationMsToTicks(duration_ms);
            std::string direction = args.value("direction","left");
            float signed_amount = (direction == "right") ? amount : -amount;
            SetControlledVehicle(vehicle);
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            input->HoldAxis(INPUT_AXIS_STEER,signed_amount,duration_ticks);
            if (!main_scene || !main_scene->IsPhysicsPaused()){
                Sleep(TicksToRealMs(duration_ticks));
            }
            return MaybeAttachScreenshot(GetVehicleTelemetry(vehicle),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_track_drive",
        "Drive the tank's two tracks INDEPENDENTLY, bypassing the throttle/steer mix entirely - "
        "the scripted equivalent of the Tank UI's 'Direct track control' rig, and the way to "
        "command an exact differential. left and right are signed -1..1 (positive drives that "
        "track forward). Switches the rig on automatically and takes control of the tank; call "
        "with enable=false to switch it back off and return to normal tank_drive/tank_steer "
        "mixing. Held for duration_ms, rounded up to whole physics ticks and re-asserted on each, "
        "for the same reason tank_drive holds. Blocks until it has played out and returns the "
        "resulting telemetry, which reports left_track_input/right_track_input alongside the yaw "
        "rate in angular_velocity. Note tank_drive/tank_steer do nothing while the rig is on.",
        json{
            {"type","object"},
            {"properties", {
                {"left", {{"type","number"},{"description","-1..1 command for the LEFT track, default 0"}}},
                {"right", {{"type","number"},{"description","-1..1 command for the RIGHT track, default 0"}}},
                {"duration_ms", {{"type","number"},{"description","how long to hold the input and block for, default 100, capped at 15000"}}},
                {"enable", {{"type","boolean"},{"description","turn the direct-track rig on (default true); false switches it off and ignores left/right"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the resulting frame, default false"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!controlled_tank){
                return json{ {"error","no tank"} };
            }
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            //Same "which vehicle means take control of it" rule the other tools follow - the rig
            //is only read for the vehicle RunLogic is actually driving.
            SetControlledVehicle(controlled_tank);
            bool enable = args.value("enable",true);
            if (!enable){
                controlled_tank->direct_track_control = false;
                controlled_tank->ReleaseInputs();
                input->ReleaseSynthetic();
                return MaybeAttachScreenshot(GetVehicleTelemetry(controlled_tank),args.value("include_screenshot",false));
            }
            controlled_tank->direct_track_control = true;
            float left = clamp(args.value("left",0.0f),-1.0f,1.0f);
            float right = clamp(args.value("right",0.0f),-1.0f,1.0f);
            float duration_ms = clamp(args.value("duration_ms",100.0f),0.0f,15000.0f);
            uint32_t duration_ticks = DurationMsToTicks(duration_ms);
            input->HoldAxis(INPUT_AXIS_TRACK_L,left,duration_ticks);
            input->HoldAxis(INPUT_AXIS_TRACK_R,right,duration_ticks);
            if (!main_scene || !main_scene->IsPhysicsPaused()){
                Sleep(TicksToRealMs(duration_ticks));
            }
            return MaybeAttachScreenshot(GetVehicleTelemetry(controlled_tank),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_telemetry",
        "Report the tank hull's current position, facing, and physics state (velocity, "
        "angular velocity, mass, whether the rigidbody is asleep). Set include_screenshot to "
        "also get a PNG of the current frame.",
        json{
            {"type","object"},
            {"properties", {
                {"vehicle", {{"type","string"},{"enum", json::array({"tank","buggy"})},{"description","which vehicle, default tank"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG screenshot of the current frame, default false"}}}
            }}
        },
        [this](const json &args) -> json {
            return MaybeAttachScreenshot(GetVehicleTelemetry(ResolveVehicleArg(args)),args.value("include_screenshot",false));
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
                {"vehicle", {{"type","string"},{"enum", json::array({"tank","buggy"})},{"description","which vehicle, default tank"}}},
                {"paused", {{"type","boolean"},{"description","true to pause, false to resume free-running physics"}}}
            }},
            {"required", json::array({"paused"})}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            main_scene->PausePhysics(args.value("paused",true));
            json result = GetVehicleTelemetry(ResolveVehicleArg(args));
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
                {"vehicle", {{"type","string"},{"enum", json::array({"tank","buggy"})},{"description","which vehicle, default tank"}}},
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
            return MaybeAttachScreenshot(GetVehicleTelemetry(ResolveVehicleArg(args)),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("tank_reset",
        "Teleport a vehicle back to its spawn pose at rest (same as the debug panel's 'Reset To "
        "Start' button): zero velocity, pedals and steering released, wheels reset. Returns the "
        "telemetry right after the reset. Use it to start every scripted scenario from the same "
        "place, e.g. before the vehicle has driven off the test ground plane.",
        json{
            {"type","object"},
            {"properties", {
                {"vehicle", {{"type","string"},{"enum", json::array({"tank","buggy"})},{"description","which vehicle, default tank"}}}
            }}
        },
        [this](const json &args) -> json {
            Vehicle* vehicle = ResolveVehicleArg(args);
            if (!vehicle){
                return json{ {"error","no such vehicle"} };
            }
            //Submitted as a command rather than applied here: this handler runs on the MCP
            //thread, and a teleport racing the running physics step corrupts the vehicle. Waiting
            //for it means the telemetry below describes the state AFTER the reset, which is what
            //a scripted scenario starting from a known pose needs - the old RequestReset returned
            //the pre-reset state and left the caller to guess when it had landed.
            if (SubmitCommandAndWait(MakeVehicleResetCommand(vehicle)) == OBJECTID_INVALID){
                return json{ {"error","the reset command was not applied - see the log"} };
            }
            return GetVehicleTelemetry(vehicle);
        });

    MCPServer::Get()->RegisterTool("buggy_tune",
        "Read or set the buggy's drivetrain and suspension tuning - the same knobs the Buggy "
        "Controls debug window exposes. Only the fields given are changed; every call returns the "
        "full resulting tuning, so calling it with no arguments just reads it. suspension_hz is a "
        "ride frequency (f = sqrt(k/m)/2pi over the mass one corner carries) and is written back "
        "as a per-wheel stiffness override on every wheel, exactly as the UI slider does.",
        json{
            {"type","object"},
            {"properties", {
                {"power_split_front", {{"type","number"},{"description","0 = RWD, 1 = FWD, in between splits engine_force between the axles"}}},
                {"brake_split_front", {{"type","number"},{"description","0 = all braking on the rear axle, 1 = all on the front, 0.5 = even"}}},
                {"engine_force", {{"type","number"},{"description","total drive force at the tires (N) at full throttle"}}},
                {"top_speed", {{"type","number"},{"description","soft speed cap (m/s) beyond which no more drive torque is added"}}},
                {"suspension_hz", {{"type","number"},{"description","ride frequency (Hz) - converted to a spring stiffness against the per-corner sprung mass"}}},
                {"suspension_damping", {{"type","number"},{"description","damping coefficient c, N per (m/s) of compression rate"}}},
                {"friction_coefficient", {{"type","number"},{"description","Coulomb friction along the rolling direction (drive/brake)"}}},
                {"lateral_friction", {{"type","number"},{"description","Coulomb friction sideways (cornering)"}}},
                {"sliding_friction_ratio", {{"type","number"},{"description","fraction of its grip a SLIDING tire keeps; 1.0 disables the falloff (pre-falloff behaviour)"}}},
                {"peak_slip_ratio", {{"type","number"},{"description","longitudinal slip ratio at which grip peaks, ~0.12 for tarmac"}}},
                {"max_steer_angle_degrees", {{"type","number"},{"description","front wheel lock angle in degrees"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!controlled_buggy){
                return json{ {"error","no buggy"} };
            }
            BuggyCharacter* buggy = controlled_buggy;
            if (args.contains("power_split_front")){ buggy->power_split_front = clamp(args.at("power_split_front").get<float>(),0.0f,1.0f); }
            if (args.contains("brake_split_front")){ buggy->brake_split_front = clamp(args.at("brake_split_front").get<float>(),0.0f,1.0f); }
            if (args.contains("engine_force")){ buggy->engine_force = max(args.at("engine_force").get<float>(),0.0f); }
            if (args.contains("top_speed")){ buggy->top_speed = max(args.at("top_speed").get<float>(),0.0f); }
            if (args.contains("suspension_damping")){ buggy->suspension_damping = max(args.at("suspension_damping").get<float>(),0.0f); }
            if (args.contains("friction_coefficient")){ buggy->friction_coefficient = max(args.at("friction_coefficient").get<float>(),0.0f); }
            if (args.contains("lateral_friction")){ buggy->lateral_friction = max(args.at("lateral_friction").get<float>(),0.0f); }
            if (args.contains("sliding_friction_ratio")){ buggy->sliding_friction_ratio = clamp(args.at("sliding_friction_ratio").get<float>(),0.0f,1.0f); }
            if (args.contains("peak_slip_ratio")){ buggy->peak_slip_ratio = max(args.at("peak_slip_ratio").get<float>(),0.0001f); }
            if (args.contains("max_steer_angle_degrees")){ buggy->max_steer_angle_degrees = args.at("max_steer_angle_degrees").get<float>(); }

            //Same conversion the Susp. Freq slider does, and written the same way: onto each
            //wheel's own stiffness override, so it survives ResolveTuning's 0-means-inherit rule
            //rather than being masked by a per-wheel value already set.
            float corner_mass = 0.0f;
            if (Physics* physics = buggy->GetPhysics()){
                if (!buggy->wheels.empty()){
                    corner_mass = physics->GetMass() / (float)buggy->wheels.size();
                }
            }
            if (args.contains("suspension_hz")){
                if (corner_mass <= 0.0f){
                    return json{ {"error","buggy has no mass or no wheels - cannot convert a frequency to a stiffness"} };
                }
                float omega = 2.0f * TYPE_PI * max(args.at("suspension_hz").get<float>(),0.01f);
                float stiffness = corner_mass * omega * omega;
                buggy->suspension_stiffness = stiffness;
                for (Wheel& wheel : buggy->wheels){
                    wheel.stiffness = stiffness;
                }
            }

            float resolved_stiffness = buggy->wheels.empty() ? buggy->suspension_stiffness :
                                       buggy->WheelStiffness(buggy->wheels[0]);
            json result = {
                {"power_split_front", buggy->power_split_front},
                {"brake_split_front", buggy->brake_split_front},
                {"engine_force", buggy->engine_force},
                {"top_speed", buggy->top_speed},
                {"suspension_stiffness", resolved_stiffness},
                {"suspension_damping", buggy->suspension_damping},
                {"friction_coefficient", buggy->friction_coefficient},
                {"lateral_friction", buggy->lateral_friction},
                {"sliding_friction_ratio", buggy->sliding_friction_ratio},
                {"peak_slip_ratio", buggy->peak_slip_ratio},
                {"max_steer_angle_degrees", buggy->max_steer_angle_degrees},
                {"corner_mass_kg", corner_mass},
            };
            if (corner_mass > 0.0f){
                result["suspension_hz"] = sqrtf(max(resolved_stiffness,0.0f) / corner_mass) / (2.0f * TYPE_PI);
            }
            return result;
        });

    MCPServer::Get()->RegisterTool("crane_speed",
        "Set the crane's velocity commands (-1..1 each): `speed` for the boom's hinge motor "
        "(positive raises), `extension_speed` for the telescoping extension's slider motor "
        "(positive extends), `hook_speed` for the hook's winch (positive LOWERS the hook), and "
        "`slew_speed` for the base turning on the spot (positive turns counter-clockwise seen "
        "from above), which carries boom, extension and hook round with it. 0 holds. "
        "Same values the Crane debug panel's sliders set; only the ones given are changed, and they "
        "persist until changed again. Note that while the 'Controlling' selector is on Crane AND "
        "the window has focus, a key press overrides these - drive the crane from here with the "
        "window in the background, or with another rig selected. Returns crane_telemetry.",
        json{
            {"type","object"},
            {"properties", {
                {"speed", {{"type","number"},{"description","-1..1 boom velocity command, positive raises"}}},
                {"extension_speed", {{"type","number"},{"description","-1..1 extension velocity command, positive extends"}}},
                {"hook_speed", {{"type","number"},{"description","-1..1 winch velocity command, positive lowers the hook"}}},
                {"slew_speed", {{"type","number"},{"description","-1..1 base slew velocity command, positive turns counter-clockwise seen from above"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!args.contains("speed") && !args.contains("extension_speed") &&
                !args.contains("hook_speed") && !args.contains("slew_speed")){
                return json{ {"error","give speed, extension_speed, hook_speed and/or slew_speed"} };
            }
            //Only the command members are written here - RunLogic is what hands them to the
            //crane, every tick, from these same members (see its crane block). That's also why
            //the debug panel can't overwrite them back: it edits these too.
            if (args.contains("hook_speed")){
                crane_hook_speed = clamp(args.value("hook_speed",0.0f),-1.0f,1.0f);
            }
            if (args.contains("speed")){
                crane_piston_speed = clamp(args.value("speed",0.0f),-1.0f,1.0f);
            }
            if (args.contains("extension_speed")){
                crane_extension_speed = clamp(args.value("extension_speed",0.0f),-1.0f,1.0f);
            }
            if (args.contains("slew_speed")){
                crane_slew_speed = clamp(args.value("slew_speed",0.0f),-1.0f,1.0f);
            }
            return GetCraneTelemetry();
        });

    MCPServer::Get()->RegisterTool("crane_magnet",
        "Switch the crane's electromagnet on or off. While on, it grabs the nearest DYNAMIC body "
        "that enters the field under the hook (a sphere of magnet_radius_m over the pad) and welds "
        "it there with a fixed joint; switching it off drops whatever is held. Static and kinematic "
        "bodies (the terrain, the crane's own parts) and anything over magnet_max_mass_kg are "
        "ignored. Blocks until the switch has been applied AND the tick that acts on it has run, "
        "so the returned crane_telemetry already reports magnet_grabbed. While the simulation is "
        "paused (tank_pause) that tick has to come from tank_step instead, so there the grab shows "
        "up on the following call. Returns crane_telemetry.",
        json{
            {"type","object"},
            {"properties", {
                {"on", {{"type","boolean"},{"description","true switches the magnet on, false drops the load"}}}
            }},
            {"required", json::array({"on"})}
        },
        [this](const json &args) -> json {
            if (!crane){
                return json{ {"error","no crane"} };
            }
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            //SubmitCommandAndWait, unlike the debug UI's own path: an MCP handler is on the MCP
            //thread and holds no physics lock, so it can afford to wait for the tick that applies
            //it - and a caller that asked to grab something wants the result, not a promise.
            SubmitCommandAndWait(MakeCraneMagnetCommand(args.value("on",false)));
            //SubmitCommandAndWait returns as soon as the HANDLER has run, and the handler only
            //moves the flag - the grab itself happens in CraneCharacter::UpdateMagnet, after the
            //step of that same tick. Waiting a couple more ticks is the difference between this
            //tool reporting what it picked up and always reporting null. Skipped while paused,
            //where no tick is coming without tank_step - same as tank_drive's own wait.
            if (!main_scene->IsPhysicsPaused()){
                Sleep(TicksToRealMs(2));
            }
            return GetCraneTelemetry();
        });

    MCPServer::Get()->RegisterTool("crane_telemetry",
        "Report the crane boom's hinge angle (deg, relative to its spawn pose), the motor's "
        "current target speed and applied torque, the boom body's world position/velocity, the "
        "current speed commands, the telescoping extension's slider translation (m), motor "
        "target speed/force and body state, the hook's cable pay-out (m), winch force and "
        "hook/swivel positions, and the base's slew command and heading (deg from its spawn "
        "heading, +-180, positive counter-clockwise seen from above).",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json & /*args*/) -> json {
            return GetCraneTelemetry();
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
    float cell_size_x = 20.0f / (w - 1);
    float cell_size_z = 20.0f / (h - 1);
    Mesh* mesh = CreateMeshFromHeightmap(heights,w,h,cell_size_x,cell_size_z);
    if (!mesh){
        debug->Err("TestHeightmapMesh: failed to build mesh\n");
        return;
    }

    heightmap_mesh_test = new Object();
    heightmap_mesh_test->SetMesh(mesh);
    heightmap_mesh_test->name = "Heightmap Test Mesh";
    //Shifted -20 in Z (alongside the bridge, see Init()) so it doesn't overlap the flat test
    //ground AddTestSceneObjects adds at the origin - the heightmap's undulations made
    //vehicle-dynamics tests inconsistent run to run.
    heightmap_mesh_test->SetPosition(vec3(0,-0.270,-20));

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

//A flat, perfectly reproducible driving surface at the origin - same 20x20 footprint as the
//heightmap terrain, but with none of its undulations, so vehicle-dynamics tests run there stay
//consistent run to run. Sits right under the tank/buggy's own spawn points (see their SetPosition
//calls above), now that the heightmap terrain itself has been moved out to z=-20.
void ApplicationTank::AddTestSceneObjects(){
    Object* ground_plane = assetmanager->GetObjectFromAsset("cube");
    if (!ground_plane){
        debug->Err("AddTestSceneObjects: no 'cube' asset found\n");
        return;
    }
    ground_plane->name = "Test Ground Plane";
    main_scene->AddObject(ground_plane);

    const vec3 target_scale(20.0f,1.0f,20.0f);
    vec3 raw_extent = ground_plane->GetMesh() ? ground_plane->GetMesh()->GetExtents() : vec3(1,1,1);
    //Scale BEFORE adding the collider: SetScale rescales existing colliders along with the
    //visual, so a collider sized to the final extents must be added once the scale is final.
    ground_plane->SetScale(target_scale);
    ground_plane->AddPhysics(main_scene->physics_world);
    if (Physics* physics = ground_plane->GetPhysics()){
        vec3 extent = vec3(raw_extent.x * target_scale.x,raw_extent.y * target_scale.y,raw_extent.z * target_scale.z) * 0.5f;
        physics->AddBoxCollider(extent,vec3(0,0,0),quat().identity(),1.0f); //density irrelevant, static
        physics->SetFrictionCoefficient(0.8f);
        physics->SetBounciness(0.0f);
        physics->SetStatic(true);
    }
    ground_plane->SetMaterialSlot(0,0);

    //Cube mesh is centred on its own origin - drop it by half its (post-scale) height so its
    //TOP face lands on y=0, matching where the tank/buggy spawn expects ground to be.
    ground_plane->SetPosition(vec3(0,-raw_extent.y * target_scale.y * 0.5f,0));

    //A handful of static obstacles scattered across the ground plane above - fixed seed, so the
    //layout is the same every run (same reproducibility goal as the flat plane itself).
    for (int i = 0; i < 3; i++){
        Object* obstacle = assetmanager->GetObjectFromAsset("cube");
        if (!obstacle){
            debug->Err("AddTestSceneObjects: no 'cube' asset found for obstacle %d\n",i);
            continue;
        }
        obstacle->name = "Test Obstacle";
        main_scene->AddObject(obstacle);
        obstacle->AddPhysics(main_scene->physics_world);
        vec3 obstacle_extent = obstacle->GetMesh() ? obstacle->GetMesh()->GetExtents() * 0.5f : vec3(0.5f,0.5f,0.5f);
        if (Physics* physics = obstacle->GetPhysics()){
            physics->AddBoxCollider(obstacle_extent,vec3(0,0,0),quat().identity(),1.0f); //density irrelevant, static
            physics->SetFrictionCoefficient(0.8f);
            physics->SetBounciness(0.1f);
            physics->SetStatic(false);
            physics->SetMass(10.0f);
            physics->SetGravityEnabled(true);
        }
        float x = rrand->GetFloat(-8.0f,8.0f); //-8..8, safely inside the 20x20 plane
        float z = rrand->GetFloat(-8.0f,8.0f); //-8..8, safely inside the 20x20 plane
        obstacle->SetPosition(vec3(x,obstacle_extent.y,z)); //sits with its bottom on the plane's top face (y=0)
    }

    //A capsule collider, radius=1 height=1, to look at directly - reactphysics3d's own
    //createCapsuleShape(radius,height) convention is that `height` is just the cylindrical
    //section BETWEEN the two hemisphere caps, so this one is capsule_height + 2*capsule_radius
    //(3 units) tall overall, not a sphere. Left at exactly the requested radius/height rather
    //than adjusted to force a spherical look, so what's actually rendered can be checked against
    //that expectation directly.
    Object* capsule_obstacle = assetmanager->GetObjectFromAsset("capsule");
    if (!capsule_obstacle){
        debug->Err("AddTestSceneObjects: no 'capsule' asset found\n");
    }else{
        capsule_obstacle->name = "Test Capsule Obstacle";
        main_scene->AddObject(capsule_obstacle);
        capsule_obstacle->AddPhysics(main_scene->physics_world);
        const float capsule_radius = 1.0f;
        const float capsule_height = 1.0f;
        if (Physics* physics = capsule_obstacle->GetPhysics()){
            physics->AddCapsuleCollider(capsule_radius,capsule_height,vec3(0,0,0),quat().identity(),1.0f);
            physics->SetFrictionCoefficient(0.8f);
            physics->SetBounciness(0.0f);
            physics->SetStatic(true);
        }
        float x = rrand->GetFloat(-8.0f,8.0f);
        float z = rrand->GetFloat(-8.0f,8.0f);
        float half_total_height = capsule_height * 0.5f + capsule_radius; //cylinder half + one hemisphere cap
        capsule_obstacle->SetPosition(vec3(x,half_total_height,z));
    }

    //An elongated capsule tipped onto its side - a speed-bump-style obstacle to drive over.
    //A single capsule mesh can't be stretched along its axis without distorting the hemisphere
    //caps into an egg/lens shape (SetScale scales the whole mesh, caps included) - the collider
    //stays exact regardless (it's built from radius+length directly, not from this mesh), but
    //the two need to actually look alike. So the VISUAL is three pieces instead: a "cylinder"
    //asset for the straight middle (safe to stretch along its own axis - a circular cross-section
    //doesn't distort that way) capped by two capsule instances, each scaled UNIFORMLY (so they
    //stay properly round) and centred exactly on the cylinder's end faces, so only their outer
    //rounded half shows - the same trick a modeller would use, an embedded sphere as a cap.
    //bump itself stays a bare, mesh-less Object: it only carries the physics collider (built
    //directly from bump_radius/bump_length below, unaffected by any of this) and is the parent
    //the three visual pieces attach to, exactly like controlled_tank/controlled_buggy are
    //themselves mesh-less bodies with their visuals all attached as children.
    Object* bump = new Object();
    bump->name = "Test Speed Bump";
    main_scene->AddObject(bump);
    bump->AddPhysics(main_scene->physics_world);
    const float bump_radius = 0.1f;
    const float bump_length = 6.0f; //cylindrical section between the two hemisphere caps
    if (Physics* physics = bump->GetPhysics()){
        physics->AddCapsuleCollider(bump_radius,bump_length,vec3(0,0,0),quat().identity(),1.0f);
        physics->SetFrictionCoefficient(0.8f);
        physics->SetBounciness(0.0f);
        physics->SetStatic(true);
    }

    Object* bump_cylinder = assetmanager->GetObjectFromAsset("cylinder");
    if (!bump_cylinder){
        debug->Err("AddTestSceneObjects: no 'cylinder' asset found for the bump\n");
    }else{
        bump->AttachChild(bump_cylinder);
        //Diameter (2*radius) in X/Z, bump_length in Y, against the raw "cylinder" asset's own
        //unscaled extents - only the length axis gets stretched, so the circular cross-section
        //stays circular the whole way along.
        vec3 raw_extent = bump_cylinder->GetMesh() ? bump_cylinder->GetMesh()->GetExtents() : vec3(2.0f,2.0f,2.0f);
        if (raw_extent.x > 0.0001f && raw_extent.y > 0.0001f && raw_extent.z > 0.0001f){
            float target_diameter = bump_radius * 2.0f;
            bump_cylinder->SetScale(vec3(target_diameter / raw_extent.x,bump_length / raw_extent.y,target_diameter / raw_extent.z));
        }
        bump_cylinder->SetPosition(vec3(0,0,0)); //centred on the parent, same local Y axis as the collider above
    }

    for (int side = -1; side <= 1; side += 2){
        Object* cap = assetmanager->GetObjectFromAsset("capsule");
        if (!cap){
            debug->Err("AddTestSceneObjects: no 'capsule' asset found for the bump's end cap\n");
            continue;
        }
        bump->AttachChild(cap);
        //Uniform scale (not the non-uniform stretch above) - this is exactly the single capsule
        //used for the vertical one earlier, just scaled down to bump_radius, so it stays a
        //properly proportioned little capsule with genuinely round caps.
        vec3 raw_cap_extent = cap->GetMesh() ? cap->GetMesh()->GetExtents() : vec3(2.0f,3.0f,2.0f);
        float raw_cap_radius = raw_cap_extent.x * 0.5f;
        float uniform_scale = raw_cap_radius > 0.0001f ? bump_radius / raw_cap_radius : 1.0f;
        cap->SetScale(vec3(uniform_scale,uniform_scale,uniform_scale));
        //Centred exactly on the cylinder's end face - half of this little capsule pokes out
        //beyond it (the rounded tip that actually shows), the other half is hidden inside the
        //cylinder's own body.
        cap->SetPosition(vec3(0,side * (bump_length * 0.5f),0));
    }

    bump->SetRotation(quat(vec3(0,0,1),TYPE_PI * 0.5f)); //tip the whole assembly onto its side: local Y axis -> world X
    //Rests on the ground plane's top face (y=0, see ground_plane above), a few units ahead of
    //the tank/buggy spawn points so it's easy to find and drive over.
    bump->SetPosition(vec3(0,0,4));

    //Two ramps on the left side of the ground plane (negative X) - a tilted box makes an
    //adequate ramp: the "cube" asset again, scaled into a long slab and rotated about X so its
    //far (local +Z) edge lifts into the air while the near (local -Z) edge stays down at the
    //ground plane's top face, for testing how the vehicle climbs an incline.
    auto AddRamp = [this](vec3 position,float angle_degrees){
        Object* ramp = assetmanager->GetObjectFromAsset("cube");
        if (!ramp){
            debug->Err("AddTestSceneObjects: no 'cube' asset found for a ramp\n");
            return;
        }
        ramp->name = "Test Ramp";
        main_scene->AddObject(ramp);

        const float width = 3.0f;     //across the ramp (X)
        const float thickness = 0.4f; //slab thickness (Y, before rotation)
        const float length = 5.0f;    //up the slope (Z, before rotation)
        const float angle = angle_degrees * TYPE_PI / 180.0f;

        //Scale first, collider after - same reasoning as ground_plane above.
        vec3 raw_extent = ramp->GetMesh() ? ramp->GetMesh()->GetExtents() : vec3(1.0f,1.0f,1.0f);
        if (raw_extent.x > 0.0001f && raw_extent.y > 0.0001f && raw_extent.z > 0.0001f){
            ramp->SetScale(vec3(width / raw_extent.x,thickness / raw_extent.y,length / raw_extent.z));
        }
        ramp->AddPhysics(main_scene->physics_world);
        if (Physics* physics = ramp->GetPhysics()){
            vec3 extent = vec3(width,thickness,length) * 0.5f;
            physics->AddBoxCollider(extent,vec3(0,0,0),quat().identity(),1.0f); //density irrelevant, static
            physics->SetFrictionCoefficient(0.8f);
            physics->SetBounciness(0.0f);
            physics->SetStatic(true);
        }

        //Rotated about X by -angle: with this engine's quat(axis,angle) convention (the same
        //Rodrigues rotation verified against the steering code's own "quat(+Y,t) sends forward
        //(0,0,-1) to (-sin t,0,-cos t)" comment), local +Z then maps to (0, sin(angle),
        //cos(angle)) - tipping upward as angle increases, rather than into the ground.
        ramp->SetRotation(quat(vec3(1,0,0),-angle));
        ramp->SetPosition(position);
    };

    AddRamp(vec3(-5.0f, 1.0f, 11.67f), 30.0f);
    AddRamp(vec3(-8.0f, 1.5f, 11.27f), 45.0f);
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

//Whichever of a key-derived and a stick-derived command for the same crane axis is asking for
//more, so a controller and the keyboard can both be plugged in without one pinning the other to
//0. Sign is kept, not magnitudes summed - these are velocity commands, not accumulators.
static float PickStronger(float a,float b){
    return fabsf(b) > fabsf(a) ? b : a;
}

//Applies one tick of hardware input to one crane command. `hardware` is what the keys/stick are
//asking for now, `previous` the same axis's hardware value from last tick (kept per axis on the
//app), `command` the crane_*_speed this axis owns.
//
//The write is conditional on purpose. Writing every tick would be simpler but would mean that
//merely HAVING the crane selected held all four commands at 0 whenever no key was down - which
//would fight the debug panel's sliders, and silently swallow a crane_speed MCP call. Writing
//only when nonzero is the other obvious wrong answer: letting go of a key would then leave the
//motor running on the last value. So: hardware owns the command while it is nonzero, plus the
//one tick it drops back to zero, and lets go of it entirely after that.
static void ApplyHardwareCraneAxis(float hardware,float& previous,float& command){
    if (hardware != 0.0f || previous != 0.0f){
        command = hardware;
    }
    previous = hardware;
}

//Called before update physics
void ApplicationTank::RunLogic(){
    //Before every early-out below, deliberately. The vehicle keeps moving whether or not the
    //window has focus and whether or not the cursor happens to be over a debug panel, so a
    //follow that sat further down would let the camera fall behind exactly while the panel is
    //being used to watch something - which is most of the time this is on. Doing it first also
    //means this frame's own orbit/zoom pivots around where the vehicle is NOW, not where it was
    //last frame.
    if (f_camera_follow_vehicle){
        SnapCameraToControlledVehicle();
    }
    UpdateBuggyWheelSpinParticles();

    //Focus is no longer a reason to skip this whole function. It used to return here, which was
    //fine while MCP drove the vehicles behind RunLogic's back through Vehicle::HoldDrive - but MCP
    //is a player now, and its input arrives through InputController like anyone else's. Returning
    //early would silence scripted control exactly when the window ISN'T in front, which is every
    //automated run. Hardware input is already suppressed while unfocused inside InputController
    //(keys report up, gamepad axes zero), so nothing stale can leak in; scripted input is not from
    //the OS and deliberately isn't gated. What still needs the check is the cursor-driven work
    //below, which would otherwise track a mouse being used in another application.
    bool has_focus = main_window->f_has_focus;

    //Shortcuts
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    //Gamepad sampling now happens once per tick inside InputController::PollDevices, with the
    //keyboard and mouse - no separate per-app poll.

    //Track the target on the terrain under the mouse cursor. If the cursor isn't over the
    //terrain (eg. over the sky, or over the tank itself), leave the target where it is.
    if ((controlled_vehicle == controlled_tank) && target && heightmap_mesh_test){
        target->SetVisibility(true);
        //Only while focused - otherwise the target chases a cursor being used elsewhere.
        if (has_focus && input->GetHoveredObjectID() != OBJECTID_INVALID){
            target->SetPosition(input->GetHoveredPosition());
        }
    }else{
        target->SetVisibility(false);
    }

    //Cursor-driven work only: picking and selection. Skipped when the window isn't focused, or
    //when the pointer is over a UI element. Vehicle control continues below either way, so a
    //scripted drive isn't cancelled by the operator happening to mouse over a debug panel.
    if (has_focus && !ImGui::GetIO().WantCaptureMouse){
        CheckObjectSelection();
    }
    if (ImGui::GetIO().WantCaptureMouse){
        input->GetDelta(INPUT_MOUSE_WHEEL); //clear the wheel delta so it doesn't apply later
    }

    //Routed to whichever vehicle is currently selected (see the "Controlling" toggle in
    //RenderTankWheelDebugUI) - Accelerate/Reverse/SteerLeft/SteerRight are on Vehicle, shared by
    //both TankCharacter and BuggyCharacter, so the same four keys drive whichever one is active.
    //Firing stays tank-only: BuggyCharacter has no turret.
    if (controlled_vehicle){

        //HARDWARE control - keyboard and gamepad - only while this window is in front. This gate
        //is load-bearing beyond just ignoring stray keys: the gamepad block below writes the pedals
        //UNCONDITIONALLY (Brake(gp_r2), Accelerate(0)), so with a controller plugged in and its
        //triggers at rest it zeroes the idle brake that TankCharacter re-asserts at the end of
        //every tick. Letting that run during an unfocused scripted drive stopped the tank braking
        //when it coasted - it rolled 1.2m instead of 0.16m. Scripted input below is deliberately
        //outside this gate: it isn't from the OS, and an automated run is never in the foreground.
        if (has_focus){
            //Analog inputs
            float gp_lx = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_LEFT_STICK_X);
            float gp_ly = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_LEFT_STICK_Y);
            float gp_rx = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_RIGHT_STICK_X);
            float gp_ry = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_RIGHT_STICK_Y);
            float gp_l2 = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_L2);
            float gp_r2 = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_R2);

            //debug->Info("Gamepad: left stick (%.3f,%.3f) right stick (%.3f,%.3f) triggers %.3f\n",gp_lx,gp_ly,gp_rx,gp_ry,gp_l2r2);
            float steer_input = gp_lx;
            float throttle_input = gp_ry;
            float brake_input = gp_r2;

            //Direct per-track test rig: with it on, each stick's Y drives its own track and the
            //throttle/steer mix is bypassed entirely (see TankCharacter::direct_track_control).
            //Left stick Y -> left track, right stick Y -> right track, stick forward = track
            //forward. Tank only - the buggy is front-steered and has no per-track command.
            //Steering and brake are left on their normal inputs so the tank stays stoppable.
            if (controlled_tank && controlled_vehicle == controlled_tank &&
                controlled_tank->direct_track_control){
                controlled_tank->TrackInput(gp_ly,gp_ry);
                controlled_tank->ThrottleInput(0.0f);
                controlled_tank->SteerInput(0.0f);
                controlled_tank->BrakeInput(brake_input);
            }else{
                controlled_vehicle->ThrottleInput(throttle_input);
                controlled_vehicle->SteerInput(steer_input);
                controlled_vehicle->BrakeInput(brake_input);
            }

            //Overriden by keyboard. In direct-track mode the arrow keys drive the tracks
            //instead: up/down move BOTH together, left/right hold one back, so the rig is
            //still drivable with no controller plugged in.
            if (controlled_tank && controlled_vehicle == controlled_tank &&
                controlled_tank->direct_track_control){
                float kb_left = 0.0f, kb_right = 0.0f;
                if (input->IsKeyDown(INPUT_TURN_UP)){ kb_left += 1.0f; kb_right += 1.0f; }
                if (input->IsKeyDown(INPUT_TURN_DOWN)){ kb_left -= 1.0f; kb_right -= 1.0f; }
                if (input->IsKeyDown(INPUT_TURN_LEFT)){ kb_left -= 1.0f; }
                if (input->IsKeyDown(INPUT_TURN_RIGHT)){ kb_right -= 1.0f; }
                if (kb_left != 0.0f || kb_right != 0.0f){
                    controlled_tank->TrackInput(kb_left,kb_right);
                }
            }else{
                if (input->IsKeyDown(INPUT_TURN_UP)){
                    controlled_vehicle->ThrottleInput(1.0f);
                }
                if (input->IsKeyDown(INPUT_TURN_DOWN)){
                    controlled_vehicle->ThrottleInput(-1.0f);
                }
                if (input->IsKeyDown(INPUT_TURN_LEFT)){
                    controlled_vehicle->SteerInput(-1.0f);
                }
                if (input->IsKeyDown(INPUT_TURN_RIGHT)){
                    controlled_vehicle->SteerInput(1.0f);
                }
            }
        }

        //Scripted input - the MCP tools acting as a player - applied last so it layers over the
        //gamepad/keyboard the way a second controller would, and outside the focus gate above
        //because it doesn't come from the OS. The axes are already in exactly the units the
        //vehicle inputs take (drive and steer signed -1..+1, brake 0..1), which is the point of
        //defining them that way: a scripted player commands the same three things a human does,
        //and each drivetrain decides for itself what they mean.
        //
        //Only applied when non-zero, so releasing a scripted hold hands control straight back to
        //whatever else is driving rather than pinning the input at 0.
        float ax_drive = input->GetAxis(INPUT_AXIS_DRIVE);
        float ax_steer = input->GetAxis(INPUT_AXIS_STEER);
        float ax_brake = input->GetAxis(INPUT_AXIS_BRAKE);
        if (ax_drive != 0.0f){
            controlled_vehicle->ThrottleInput(ax_drive);
        }
        if (ax_steer != 0.0f){
            controlled_vehicle->SteerInput(ax_steer);
        }
        if (ax_brake != 0.0f){
            controlled_vehicle->BrakeInput(ax_brake);
        }

        //Scripted per-track commands, the direct-track rig's equivalent of drive/steer above.
        //Applied only to the tank and only while the rig is on - the mix is what the other axes
        //feed, and running both at once would just have them fight. Unlike drive/steer these are
        //applied even at exactly 0: "hold this track still while the other drives" is a command
        //in its own right here, and it is precisely the case being measured.
        if (controlled_tank && controlled_vehicle == controlled_tank &&
            controlled_tank->direct_track_control){
            float ax_track_l = input->GetAxis(INPUT_AXIS_TRACK_L);
            float ax_track_r = input->GetAxis(INPUT_AXIS_TRACK_R);
            if (ax_track_l != 0.0f || ax_track_r != 0.0f){
                controlled_tank->TrackInput(ax_track_l,ax_track_r);
            }
        }
        if (has_focus && input->WasKeyReleased(INPUT_FIRE) && controlled_vehicle == controlled_tank && controlled_tank){
            controlled_tank->Fire();
            if (fire_impact_emitter && target){
                //Local, not world, position - target is a root object (added straight to
                //main_scene, no parent), so the two are the same, and local is fresh (just set
                //a few lines up in this same function) where GetWorldPosition's default render-
                //state read would still be lagging a frame behind.
                fire_impact_emitter->SetPosition(target->GetPosition());
                fire_impact_emitter->EmitParticles(32);
            }
        }
    }

    //Crane control - the third rig the "Controlling" selector can hand the keyboard to (see
    //SetControlledCrane). Deliberately its own block rather than sharing the throttle/steer/brake
    //plumbing above: a Vehicle has one drivetrain fed by three mixed inputs, whereas a crane is
    //four independent velocity-commanded actuators, and every key here just holds one of them
    //open. Runs whether or not the crane is the selected rig, because the push at the bottom is
    //what applies the debug panel's sliders and the crane_speed MCP tool as well.
    if (crane){
        //Hardware only while focused and only while the crane is the selected rig - same gate,
        //for the same reasons, as the vehicle block above.
        if (f_crane_controlled && has_focus){
            //WASD, matching the vehicles' own layout as closely as the mechanism allows: W/S is
            //the boom (the crane's "forward/back"), A/D is the slew (its "left/right"). A turns
            //the base counter-clockwise seen from above, which is the direction the boom visibly
            //swings to the viewer's left from the default camera.
            float kb_boom = 0.0f, kb_slew = 0.0f, kb_extension = 0.0f, kb_hook = 0.0f;
            if (input->IsKeyDown(INPUT_TURN_UP)){ kb_boom += 1.0f; }
            if (input->IsKeyDown(INPUT_TURN_DOWN)){ kb_boom -= 1.0f; }
            if (input->IsKeyDown(INPUT_TURN_LEFT)){ kb_slew += 1.0f; }
            if (input->IsKeyDown(INPUT_TURN_RIGHT)){ kb_slew -= 1.0f; }
            if (input->IsKeyDown(INPUT_CRANE_EXTEND)){ kb_extension += 1.0f; }
            if (input->IsKeyDown(INPUT_CRANE_RETRACT)){ kb_extension -= 1.0f; }
            if (input->IsKeyDown(INPUT_CRANE_HOOK_DOWN)){ kb_hook += 1.0f; }  //positive pays cable out
            if (input->IsKeyDown(INPUT_CRANE_HOOK_UP)){ kb_hook -= 1.0f; }

            //Gamepad, laid out the same way the vehicles' is: left stick drives the "vehicle"
            //(here, boom and slew), the right stick and triggers take what's left. Stick X is
            //positive to the RIGHT, and right is a NEGATIVE (clockwise) slew, hence the flip.
            float gp_boom = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_LEFT_STICK_Y);
            float gp_slew = -gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_LEFT_STICK_X);
            float gp_hook = -gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_RIGHT_STICK_Y); //stick back lowers
            //Triggers are unsigned 0..1 each, so the extension takes their difference: R2 out,
            //L2 in, both together cancelling out to a hold.
            float gp_extension = gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_R2) -
                                 gamepad_controller->GetNormalizedAnalogValue(GAMEPAD_L2);

            ApplyHardwareCraneAxis(PickStronger(kb_boom,gp_boom),crane_hw_boom,crane_piston_speed);
            ApplyHardwareCraneAxis(PickStronger(kb_slew,gp_slew),crane_hw_slew,crane_slew_speed);
            ApplyHardwareCraneAxis(PickStronger(kb_extension,gp_extension),crane_hw_extension,crane_extension_speed);
            ApplyHardwareCraneAxis(PickStronger(kb_hook,gp_hook),crane_hw_hook,crane_hook_speed);

            //The magnet is a latch, not an actuator, so it goes through the command queue rather
            //than the crane_*_speed machinery above - and on the release edge, so holding G does
            //not toggle it once per tick. Submitted, never waited on: RunLogic is called from the
            //frame thread with physics_mutex held, same as the reset buttons.
            if (input->WasKeyReleased(INPUT_CRANE_MAGNET)){
                main_scene->SubmitCommand(MakeCraneMagnetCommand(!crane->IsMagnetEnabled()));
            }
        }

        //The one place the crane's commands are handed to the crane. Every other writer - the
        //keys above, the debug panel's sliders, the crane_speed MCP tool - only ever moves the
        //crane_*_speed floats, so there is a single, obvious answer to "what is the crane doing
        //and who asked for it", and a collapsed debug panel doesn't stop the crane responding.
        crane->SetPistonSpeed(crane_piston_speed);
        crane->SetExtensionSpeed(crane_extension_speed);
        crane->SetHookSpeed(crane_hook_speed);
        crane->SetSlewSpeed(crane_slew_speed);
    }

    //Camera rotation moving.
    //
    //Read with GetDelta, and read EVERY tick whether or not the drag is active - both halves of
    //that matter, and getting either wrong makes the camera spin out:
    //
    //  GetDelta, not GetValue. For a relative axis (INPUT_EVENT_AXIS_RELATIVE) the KeyMap keeps
    //  two numbers: `delta`, this tick's movement, cleared by InputController::Tick once read,
    //  and `value`, a running total that is NEVER reset. GetValue returns that lifetime total,
    //  so the camera was rotating by every raw mouse count accumulated since the process started,
    //  once per frame, growing without bound - the instant runaway spin.
    //
    //  Read unconditionally. Tick only clears `delta` for maps that were actually read this tick
    //  (f_processed), so leaving these behind the IsKeyDown gate let movement pile up for the
    //  whole time the button was NOT held, and the first frame of a drag then applied all of it
    //  at once. Draining it here keeps a drag starting from rest.
    int dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
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

    //Mouse wheel for zoom. Cursor-driven, so focused only - the same rule as the picking above.
    //InputController drops the wheel delta while unfocused anyway; the check here is what stops
    //the decay below from coasting the zoom on for a few frames after the user alt-tabs away.
    static float mouse_delta_sum = 0;
    if (has_focus){
        if (mouse_delta_sum != 0){
            vec3 diff = camera->GetForward() - camera_target;
            float dist = diff.length() * mouse_delta_sum;

            camera->MoveForwardBy(dist / 50.0f);

            mouse_delta_sum /= 1.1;
        }
        mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
    }
}

void ApplicationTank::SnapCameraToControlledVehicle(){
    //Whatever the "Controlling" selector currently points at - the crane's base included. Both
    //things wanted below are plain Object reads (world position and forward), so nothing here
    //ever needed a Vehicle; for the crane the "heading" chased is the base's own, which means
    //the camera swings around with a slew exactly as it does with a vehicle turning.
    Object* controlled = GetControlledObject();
    if (!controlled || !main_scene || !main_scene->camera){
        return;
    }
    //The camera has to sit on the vehicle as DRAWN, or the vehicle jitters against a camera that
    //has already moved. That used to mean deliberately picking the render copy of the state over
    //the fresher physics one; there is a single ObjectState now, and this runs on the frame
    //thread under physics_mutex, so the position read here is by construction the one this frame
    //draws the vehicle at.
    vec3 vehicle_pos = controlled->GetWorldPosition();
    //Translate the camera by the same delta rather than re-aiming it: the pivot moves, the
    //viewing angle and distance the user set with the mouse are left exactly as they were.
    vec3 delta = vehicle_pos - camera_target;
    main_scene->camera->SetPosition(main_scene->camera->GetPosition() + delta);
    camera_target = vehicle_pos;

    //Chase-cam: gently steer the camera's horizontal orbit angle back around behind the
    //vehicle's own current heading, rather than leaving it wherever the user last set it with
    //the mouse (which is all the translate above preserves) - so as the vehicle turns, the
    //camera swings back around to keep its rear in view instead of drifting to a side-on or
    //head-on angle. Height and distance from the pivot are both left exactly as the user's own
    //zoom/orbit set them; only the horizontal angle is nudged, and only partway each call (see
    //camera_behind_blend_rate) rather than snapped, so a sharp turn doesn't whip the view around
    //in one frame.
    vec3 vehicle_forward = controlled->GetWorldForward(); //render-state, same reasoning as vehicle_pos above
    vehicle_forward.y = 0.0f;
    float vehicle_forward_length = vehicle_forward.length();
    if (vehicle_forward_length < 0.0001f){
        return; //vehicle pointing straight up/down (or degenerate) - no horizontal heading to chase
    }
    vehicle_forward = vehicle_forward * (1.0f / vehicle_forward_length);

    vec3 offset = main_scene->camera->GetPosition() - camera_target;
    float height = offset.y;
    vec3 horizontal_offset = vec3(offset.x,0,offset.z);
    float horizontal_distance = horizontal_offset.length();
    if (horizontal_distance < 0.0001f){
        return; //camera sitting directly above/below the pivot - no horizontal angle to correct
    }
    vec3 current_dir = horizontal_offset * (1.0f / horizontal_distance);
    vec3 behind_dir = vehicle_forward * -1.0f; //directly behind = opposite the vehicle's own forward

    vec3 blended_dir = current_dir + (behind_dir - current_dir) * camera_behind_blend_rate;
    float blended_length = blended_dir.length();
    if (blended_length < 0.0001f){
        return; //current and desired directions cancelled out exactly (180 degrees apart, rare)
    }
    blended_dir = blended_dir * (1.0f / blended_length);

    main_scene->camera->SetPosition(camera_target + blended_dir * horizontal_distance + vec3(0,height,0));
    vec3 up = main_scene->camera->GetUp();
    main_scene->camera->SetLookAt(camera_target);
}

void ApplicationTank::UpdateBuggyWheelSpinParticles(){
    if (!controlled_buggy){
        return;
    }
    for (size_t i = 0; i < controlled_buggy->wheels.size() && i < buggy_wheel_spin_emitters.size(); i++){
        Wheel& wheel = controlled_buggy->wheels[i];
        ParticleEmitter* emitter = buggy_wheel_spin_emitters[i];
        if (!emitter){
            continue;
        }
        if (wheel.visual){
            emitter->SetPosition(wheel.visual->GetPosition());
        }
        //Wheelspin specifically - driven AND friction-saturated, the exact condition
        //BuggyCharacter::UpdatePhysicsState's own free-spin blend uses (see its comment) - not a
        //locked/skidding UNDRIVEN wheel, which this doesn't cover.
        if (wheel.friction_saturated && (wheel.drive_force != 0.0f)){
            emitter->emission_properties.emission_speed_min = wheel.angular_velocity / 10.0f;
            emitter->emission_properties.emission_speed_max = emitter->emission_properties.emission_speed_min + 1.0f;
            emitter->EmitParticles(1);
        }
    }
}

void ApplicationTank::DrawImGuiUI(){
    RenderDebugMenuBar();
    RenderApplicationUI();
    RenderTankWheelDebugUI();
    RenderBuggyControlDebugUI();
}

//Renders the per-wheel table for whichever Vehicle is passed - the table only ever reads/writes
//Wheel fields and Vehicle::WheelRadius/WheelRestLength/WheelTravel, none of which are
//vehicle-specific, so this is shared between the tank and buggy sections of
//RenderTankWheelDebugUI below rather than duplicated per vehicle type.
void ApplicationTank::RenderVehicleWheelTable(Vehicle* vehicle){
    if (!vehicle){
        return;
    }
    ImGui::PushID(vehicle);
    if (ImGui::BeginTable("vehicle_wheels",12,ImGuiTableFlags_Borders|ImGuiTableFlags_RowBg|ImGuiTableFlags_ScrollX)){
        ImGui::TableSetupColumn("#");
        ImGui::TableSetupColumn("Side");
        ImGui::TableSetupColumn("Kind");
        ImGui::TableSetupColumn("Grounded");
        ImGui::TableSetupColumn("Compression (m)");
        ImGui::TableSetupColumn("Anchor Offset (editable)");
        ImGui::TableSetupColumn("Radius");
        ImGui::TableSetupColumn("Rest / Travel");
        ImGui::TableSetupColumn("Susp Axis");
        ImGui::TableSetupColumn("Friction Coef");
        ImGui::TableSetupColumn("Lateral Friction");
        ImGui::TableSetupColumn("Roll Angle");
        ImGui::TableHeadersRow();

        int i = 0;
        for (Wheel& wheel:vehicle->wheels){
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%i",i);

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s",wheel.is_left_side ? "Left" : "Right");

            ImGui::TableSetColumnIndex(2);
            //The checkbox rides in the Kind cell rather than taking a column of its own:
            //unticking it drops this wheel's raycast (and so all of its force) for as long as
            //it's off, which is the quickest way to find out what one contact is contributing.
            ImGui::Checkbox("##can_contact",&wheel.can_contact_ground);
            ImGui::SameLine();
            ImGui::Text("%s%s%s",wheel.is_road_wheel ? "Road" : "Idler/sprocket",
                                wheel.steerable ? " (steer)" : "",
                                wheel.driven ? "" : " (undriven)");

            ImGui::TableSetColumnIndex(3);
            if (wheel.can_contact_ground){
                ImGui::TextColored(wheel.grounded ? ImVec4(0.3f,1.0f,0.3f,1.0f) : ImVec4(1.0f,0.4f,0.4f,1.0f),
                                    wheel.grounded ? "Yes" : "No");
            }else{
                ImGui::TextDisabled("off");
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%.4f",wheel.compression);

            ImGui::TableSetColumnIndex(5);
            //Writes straight into wheel.local_offset - the next raycast (mount_world in each
            //vehicle's own UpdatePhysicsState) and the wheel's visual both read it fresh every
            //tick, so a drag here takes effect immediately, no rebuild needed to try a new mount
            //point. Note this is the suspension ANCHOR, not the hub: the wheel itself hangs
            //rest_length below it along the axis, so the visual won't sit where this says.
            ImGui::SetNextItemWidth(180.0f);
            ImGui::DragFloat3("##local_offset",(float*)&wheel.local_offset,0.005f,-2.0f,2.0f,"%.3f");

            //The next three show what this wheel RESOLVES to (Wheel's own value, or the
            //vehicle-level default when it's left at 0) and only write a per-wheel override
            //once actually dragged - so wheels keep inheriting one shared spring until you
            //deliberately single one out. Dragging a value back to exactly 0 hands it back to
            //the default.
            ImGui::TableSetColumnIndex(6);
            ImGui::SetNextItemWidth(70.0f);
            float radius = vehicle->WheelRadius(wheel);
            if (ImGui::DragFloat("##radius",&radius,0.001f,0.0f,0.5f,"%.4f")){
                wheel.radius = radius;
            }

            ImGui::TableSetColumnIndex(7);
            ImGui::SetNextItemWidth(120.0f);
            float rest_travel[2] = {vehicle->WheelRestLength(wheel),vehicle->WheelTravel(wheel)};
            if (ImGui::DragFloat2("##rest_travel",rest_travel,0.002f,0.0f,0.5f,"%.3f")){
                wheel.rest_length = rest_travel[0];
                wheel.travel = rest_travel[1];
            }

            ImGui::TableSetColumnIndex(8);
            //Renormalized by UpdatePhysicsState every tick, so dragging one component here is
            //safe - it just tilts the strut rather than lengthening it.
            ImGui::SetNextItemWidth(150.0f);
            ImGui::DragFloat3("##susp_axis",(float*)&wheel.suspension_axis,0.01f,-1.0f,1.0f,"%.2f");

            //Same "shows the resolved value, only writes a per-wheel override once dragged"
            //pattern as radius/rest_travel above.
            ImGui::TableSetColumnIndex(9);
            ImGui::SetNextItemWidth(70.0f);
            float friction_coefficient = vehicle->WheelFrictionCoefficient(wheel);
            if (ImGui::DragFloat("##friction_coefficient",&friction_coefficient,0.01f,0.0f,3.0f,"%.2f")){
                wheel.friction_coefficient = friction_coefficient;
            }

            ImGui::TableSetColumnIndex(10);
            ImGui::SetNextItemWidth(80.0f);
            //A Coulomb coefficient now (max sideways force = this * normal load), same units as
            //Friction Coef - see Wheel::lateral_friction.
            float lateral_friction = vehicle->WheelLateralFriction(wheel);
            if (ImGui::DragFloat("##lateral_friction",&lateral_friction,0.01f,0.0f,3.0f,"%.2f")){
                wheel.lateral_friction = lateral_friction;
            }

            ImGui::TableSetColumnIndex(11);
            ImGui::Text("%.2f",wheel.roll_angle);

            ImGui::PopID();
            i++;
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

void ApplicationTank::RenderTankWheelDebugUI(){
    ImGui::Begin("Vehicle Debug");

    ImGui::Text("Controlling:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Tank",controlled_vehicle == controlled_tank) && controlled_tank){
        SetControlledVehicle(controlled_tank);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(controlled_buggy == NULL);
    if (ImGui::RadioButton("Buggy",controlled_vehicle == controlled_buggy) && controlled_buggy){
        SetControlledVehicle(controlled_buggy);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    //The crane is not a Vehicle, so its "selected" state is f_crane_controlled rather than a
    //comparison against controlled_vehicle - see the flag's own comment for why the two are
    //separate and how they're kept mutually exclusive.
    ImGui::BeginDisabled(crane == NULL);
    if (ImGui::RadioButton("Crane",f_crane_controlled) && crane){
        SetControlledCrane();
    }
    ImGui::EndDisabled();

    //Camera pivot. Both act on camera_target, the point the middle-mouse orbit and the wheel
    //zoom already work relative to - so following leaves every existing camera control working
    //exactly as before, just around a moving point instead of a fixed one.
    ImGui::Text("Camera:");
    ImGui::SameLine();
    ImGui::BeginDisabled(GetControlledObject() == NULL);
    if (ImGui::Button("Snap To Vehicle")){
        SnapCameraToControlledVehicle();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Follow",&f_camera_follow_vehicle);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(chases behind heading - distance/zoom still work)");
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderFloat("Behind Turn Rate",&camera_behind_blend_rate,0.0f,1.0f,"%.2f");
    ImGui::Separator();

    if (controlled_tank){
        if (ImGui::CollapsingHeader("Tank",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::PushID("tank_section");
            ImGui::Text("Gas Pedal      : %.2f",controlled_tank->gas_pedal);
            ImGui::Text("Brake Pedal    : %.2f",controlled_tank->brake_pedal);
            ImGui::Text("Steering       : %.2f",controlled_tank->steering_position);
            ImGui::Text("Reverse        : %s",controlled_tank->f_reverse ? "true" : "false");

            //Direct per-track test rig - see TankCharacter::direct_track_control. Toggling it
            //releases the inputs so neither control path is left latched from the other.
            if (ImGui::Checkbox("Direct track control (L stick Y = left, R stick Y = right)",
                                &controlled_tank->direct_track_control)){
                controlled_tank->ReleaseInputs();
            }
            if (controlled_tank->direct_track_control){
                ImGui::Text("  Left Track   : %+.2f",controlled_tank->left_track_input);
                ImGui::Text("  Right Track  : %+.2f",controlled_tank->right_track_input);
                ImGui::TextDisabled("  Steering mix bypassed. Arrows also work: up/down both, left/right hold one back.");
            }

            if (ImGui::Button("Reset Tank To Start")){
                //Submitted and NOT waited on: DrawImGuiUI already holds physics_mutex, so
                //blocking here for the physics thread to apply it would deadlock. The reset
                //shows up next frame - see Application::SubmitCommandAndWait.
                main_scene->SubmitCommand(MakeVehicleResetCommand(controlled_tank));
            }

            if (ImGui::Checkbox("Show pink wheel debug visuals (vs. tracks mesh)",&f_show_wheel_debug_visuals)){
                for (Wheel& wheel:controlled_tank->wheels){
                    if (wheel.visual){
                        wheel.visual->SetVisibility(f_show_wheel_debug_visuals);
                    }
                }
                if (tank_tracks){
                    tank_tracks->SetVisibility(!f_show_wheel_debug_visuals);
                }
            }
            ImGui::Separator();
            RenderVehicleWheelTable(controlled_tank);
            ImGui::PopID();
        }
    }

    if (controlled_buggy){
        if (ImGui::CollapsingHeader("Buggy",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::PushID("buggy_section");
            ImGui::Text("Gas Pedal      : %.2f",controlled_buggy->gas_pedal);
            ImGui::Text("Brake Pedal    : %.2f",controlled_buggy->brake_pedal);
            ImGui::Text("Steering       : %.2f",controlled_buggy->steering_position);
            ImGui::Text("Reverse        : %s",controlled_buggy->f_reverse ? "true" : "false");
            ImGui::DragFloat("Power Split (0=RWD, 1=FWD)",&controlled_buggy->power_split_front,0.01f,0.0f,1.0f,"%.2f");

            if (ImGui::Button("Reset Buggy To Start")){
                main_scene->SubmitCommand(MakeVehicleResetCommand(controlled_buggy)); //see the tank's button above
            }
            ImGui::Separator();
            RenderVehicleWheelTable(controlled_buggy);

            //Suspension test bed: one drag control per buggy_test_cubes entry (same order as
            //controlled_buggy->wheels - see Init()). The body is pinned static/suspended while
            //this is in use, so dragging a cube up into a wheel's reach is what compresses it -
            //watch that wheel's row above (Compression/Grounded) and the visual bob/spring scale
            //respond live.
            if (!buggy_test_cubes.empty()){
                ImGui::Separator();
                ImGui::Text("Buggy Suspension Test Bed");
                for (size_t i = 0; i < buggy_test_cubes.size() && i < controlled_buggy->wheels.size(); i++){
                    Object* cube = buggy_test_cubes[i];
                    if (!cube){ continue; }
                    ImGui::PushID((int)i);
                    const Wheel& wheel = controlled_buggy->wheels[i];
                    ImGui::Text("%s %s",wheel.is_front_side ? "Front" : "Rear",wheel.is_left_side ? "Left" : "Right");
                    ImGui::SameLine();
                    vec3 pos = cube->GetPosition();
                    ImGui::SetNextItemWidth(220.0f);
                    if (ImGui::DragFloat3("##cube_pos",(float*)&pos,0.01f,-3.0f,3.0f,"%.3f")){
                        cube->SetPosition(pos);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }
    }

    RenderCraneDebugUI();

    ImGui::End();
}

//The crane's section of the Vehicle Debug window. Unlike the tank/buggy sections above, none of
//the sliders here push their value into the crane: RunLogic does that for all four commands
//every tick (see its crane block), so this only has to move the crane_*_speed floats - which
//also means the panel and the keyboard and the crane_speed MCP tool are all editing the same
//numbers rather than each having their own path into the mechanism.
void ApplicationTank::RenderCraneDebugUI(){
    if (!crane){
        return;
    }
    if (!ImGui::CollapsingHeader("Crane",ImGuiTreeNodeFlags_DefaultOpen)){
        return;
    }
    ImGui::PushID("crane_section");
    if (f_crane_controlled){
        ImGui::TextDisabled("Keys: W/S boom, A/D slew, E/Q extend, F/R hook, G magnet. Sliders hold whatever a key last set.");
    }else{
        ImGui::TextDisabled("Select 'Crane' above to drive it from the keyboard.");
    }

    //Read back from the crane, not from a UI-side copy: the flag is only ever changed by the
    //command handler on the physics thread, so this checkbox shows what the simulation actually
    //has rather than what was last clicked (they differ for the one frame in between).
    bool magnet_on = crane->IsMagnetEnabled();
    if (ImGui::Checkbox("Magnet",&magnet_on)){
        main_scene->SubmitCommand(MakeCraneMagnetCommand(magnet_on)); //not waited on - see the reset buttons
    }
    ImGui::SameLine();
    if (Object* held = crane->GetGrabbedObject()){
        ImGui::Text("holding %s (%.1f kg)",held->name.c_str(),crane->GetGrabbedMass());
    }else if (crane->IsMagnetEnabled()){
        ImGui::TextDisabled("on - nothing in range");
    }else{
        ImGui::TextDisabled("off");
    }
    ImGui::SetNextItemWidth(120.0f);
    ImGui::DragFloat("Max Grab Mass (kg)",&crane->magnet_max_mass,1.0f,0.0f,500.0f,"%.0f");
    ImGui::Separator();

    //Velocity commands - 0 holds that actuator where it is (its motor runs at speed 0 rather
    //than switching off, so it keeps holding station against gravity).
    ImGui::SliderFloat("Slew Speed (+-1, + turns left)",&crane_slew_speed,-1.0f,1.0f,"%.2f");
    ImGui::Text("Base Heading : %.1f deg",crane->GetSlewAngle() * 180.0f / TYPE_PI);
    ImGui::SliderFloat("Boom Speed (+-1)",&crane_piston_speed,-1.0f,1.0f,"%.2f");
    if (crane->boom_hinge){
        ImGui::Text("Boom Angle : %.1f deg",crane->boom_hinge->getAngle() * 180.0f / TYPE_PI);
        ImGui::Text("Motor Torque : %.0f N.m",crane->boom_hinge->getMotorTorque(1.0f/physics_tps));
    }
    ImGui::SliderFloat("Extension Speed (+-1)",&crane_extension_speed,-1.0f,1.0f,"%.2f");
    if (crane->extension_slider){
        ImGui::Text("Extension : %.2f m",crane->extension_slider->getTranslation());
        ImGui::Text("Motor Force : %.0f N",crane->extension_slider->getMotorForce(1.0f/physics_tps));
    }
    ImGui::SliderFloat("Hook Speed (+-1, + lowers)",&crane_hook_speed,-1.0f,1.0f,"%.2f");
    if (crane->hook_slider){
        ImGui::Text("Cable Paid Out : %.2f m",crane->hook_slider->getTranslation());
        ImGui::Text("Winch Force : %.0f N",crane->hook_slider->getMotorForce(1.0f/physics_tps));
    }
    ImGui::PopID();
}

//A focused control panel for the buggy - unlike RenderTankWheelDebugUI's own "Buggy" section
//(visible any time controlled_buggy exists), this one only shows up while the buggy is actually
//the vehicle receiving input, so it doesn't clutter the screen while driving the tank instead.
void ApplicationTank::RenderBuggyControlDebugUI(){
    if (!controlled_buggy || controlled_vehicle != controlled_buggy){
        return;
    }

    ImGui::Begin("Buggy Controls");

    ImGui::SliderFloat("Power Split (0=RWD, 1=FWD)",&controlled_buggy->power_split_front,0.0f,1.0f,"%.2f");
    ImGui::SliderFloat("Engine Power (N)",&controlled_buggy->engine_force,0.0f,8000.0f,"%.0f");
    ImGui::SliderFloat("Top Speed (m/s)",&controlled_buggy->top_speed,0.0f,20.0f,"%.1f");
    //Brake bias, the braking counterpart of Power Split above - see BuggyCharacter::brake_split_front.
    ImGui::SliderFloat("Brake Bias (0=rear, 1=front)",&controlled_buggy->brake_split_front,0.0f,1.0f,"%.2f");
    //Tire grip falloff once sliding - see BuggyCharacter::sliding_friction_ratio. Vehicle-wide
    //rather than per-wheel (the wheel blocks below already carry the per-wheel friction override):
    //these describe the tire compound, which is the same on all four corners.
    //
    //Set the ratio to 1.00 to switch the falloff off entirely and get the single-coefficient
    //behaviour from before it existed - the quickest A/B for whether a handling change came from
    //this model or from something else.
    ImGui::SliderFloat("Sliding Grip Ratio (1=no falloff)",&controlled_buggy->sliding_friction_ratio,0.1f,1.0f,"%.2f");
    ImGui::SliderFloat("Peak Slip Ratio",&controlled_buggy->peak_slip_ratio,0.02f,0.5f,"%.3f");
    ImGui::Text("Inputs   Gas %.2f   Brake %.2f   Steer %+.2f",
                controlled_buggy->gas_pedal,controlled_buggy->brake_pedal,controlled_buggy->steering_position);


    if (Physics* physics = controlled_buggy->GetPhysics()){
        //Physics::SetMass overrides whatever AddBoxCollider's own density param set at spawn -
        //doesn't touch the inertia tensor computed back then, so heavier/lighter here changes how
        //hard the chassis is to accelerate/brake without also rebalancing how it tumbles.
        float mass = physics->GetMass();
        if (ImGui::SliderFloat("Mass (kg)",&mass,10.0f,300.0f,"%.1f")){
            physics->SetMass(mass);
        }
    }
    ImGui::Separator();
    ImGui::Text("Speed: %.2f m/s",controlled_buggy->forward_speed);
    ImGui::Separator();

    //4 blocks laid out to match the buggy's actual physical layout (front pair on top, rear pair
    //below) - found by is_front_side/is_left_side rather than assumed storage order, since
    //SetupWheels' order isn't this function's concern to know.
    auto FindWheel = [this](bool front,bool left)->Wheel*{
        for (Wheel& wheel:controlled_buggy->wheels){
            if ((wheel.is_front_side == front) && (wheel.is_left_side == left)){
                return &wheel;
            }
        }
        return NULL;
    };
    //Sprung mass carried by one corner - what turns this wheel's spring rate into a ride
    //frequency below. The body's mass over the wheel count is the same "N wheels carry the body"
    //reading BuggyCharacter's own suspension comments use.
    float corner_mass = 0.0f;
    if (Physics* physics = controlled_buggy->GetPhysics()){
        if (!controlled_buggy->wheels.empty()){
            corner_mass = physics->GetMass() / (float)controlled_buggy->wheels.size();
        }
    }
    //AutoResizeY rather than a fixed height: the block is exactly as tall as its contents, so it
    //cannot scroll whatever the font size is and adding another readout later can't reintroduce
    //the scrollbar. Width stays fixed so the two columns line up, and the sliders get an explicit
    //item width so their labels aren't clipped against the child's edge.
    auto WheelBlock = [this,corner_mass](const char* label,Wheel* wheel){
        ImGui::BeginChild(label,ImVec2(350,0),ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
        ImGui::Text("%s",label);
        int id = 0;
        if (wheel){
            ImGui::Text("Grounded    : %s",wheel->grounded ? "true" : "false");
            ImGui::Text("Steerable   : %s",wheel->steerable ? "true" : "false");
            ImGui::Text("Driven      : %s",wheel->driven ? "true" : "false");
            ImGui::Text("Steer       : %6.1f deg",wheel->steer_angle * 180.0f / TYPE_PI);
            ImGui::Text("AngVel      : %6.2f rad/s",wheel->angular_velocity);
            ImGui::Text("Drive Force : %6.2f N",wheel->drive_force);
            //The force actually applied to the body at this wheel's contact point - see
            //physics->AddWorldForceAt(wheel_forward * longitudinal_force + wheel_left *
            //lateral_force, ...) in BuggyCharacter/TankCharacter::UpdatePhysicsState. Longitudinal
            //is drive OR passive grip, whichever this tick used (Drive Force above is only the
            //engine's own share, 0 on an undriven axle even while it's still exerting grip); a
            //steered front wheel with 0 Drive Force can still show real Lateral/Total here from
            //cornering grip alone.
            float total_force = sqrtf(wheel->longitudinal_force * wheel->longitudinal_force + wheel->lateral_force * wheel->lateral_force);
            ImGui::Text("Longitudinal: %6.2f N",wheel->longitudinal_force);
            ImGui::Text("Lateral     : %6.2f N",wheel->lateral_force);
            ImGui::Text("Total       : %6.2f N",total_force);
            ImGui::Text("Grip Budget : %6.2f N",wheel->friction_budget);
            if (wheel->friction_saturated){
                ImGui::TextColored(ImVec4(1.0f,0.35f,0.35f,1.0f),"SATURATED");
            }else{
                ImGui::TextDisabled("(grip ok)");
            }
            ImGui::PushID(id++);
            //wheel->friction_coefficient is the raw per-wheel OVERRIDE (0 = inherit the
            //vehicle's own default - same pattern as every other tuning field, see
            //BuggyCharacter::ResolveTuning), not the value physics actually uses - read that
            //through WheelFrictionCoefficient() instead, same as RenderVehicleWheelTable does,
            //or this shows 0 until dragged even though the resolved value (1.0 by default) is
            //what's really being applied.
            float friction_coefficient = controlled_buggy->WheelFrictionCoefficient(*wheel);
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::DragFloat("Friction Coef.",&friction_coefficient,0.01f,0.0f,3.0f,"%.2f")){
                wheel->friction_coefficient = friction_coefficient;
            }
            ImGui::PopID();

            //Suspension expressed as a RIDE FREQUENCY rather than a raw spring rate, because
            //that is the number that describes how the car feels and the only one that stays
            //meaningful when the mass changes: f = sqrt(k/m)/2pi for the mass this corner
            //carries. ~1 Hz is a soft road car, ~2.5 Hz a stiff racing one. Dragging it converts
            //straight back to the stiffness the tuning actually stores (k = m*(2*pi*f)^2), so
            //nothing downstream has to know about frequency.
            //
            //The range runs to 8 Hz because the buggy's own default is already 4.24 Hz (8000 N/m
            //over a 11.25 kg corner - the same arithmetic BuggyCharacter's suspension comment
            //does), i.e. very stiff indeed. A tighter, more car-like range would have clamped the
            //starting value and made the slider jump the moment it was touched.
            //
            //Reads the RESOLVED stiffness through WheelStiffness(), not wheel->stiffness - the
            //latter is the 0-means-inherit override and would show 0 Hz until first dragged, the
            //same trap the friction slider above documents.
            ImGui::PushID(id++);
            if (corner_mass > 0.0f){
                float stiffness = controlled_buggy->WheelStiffness(*wheel);
                float frequency = sqrtf(max(stiffness,0.0f) / corner_mass) / (2.0f * TYPE_PI);
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::DragFloat("Susp. Freq (Hz)",&frequency,0.02f,0.5f,8.0f,"%.2f")){
                    float omega = 2.0f * TYPE_PI * max(frequency,0.01f);
                    wheel->stiffness = corner_mass * omega * omega;
                }
                ImGui::TextDisabled("  %.0f N/m",controlled_buggy->WheelStiffness(*wheel));
            }else{
                ImGui::TextDisabled("Susp. Freq: (no mass)");
            }
            ImGui::PopID();
        }else{
            ImGui::TextDisabled("(missing)");
        }
        ImGui::EndChild();
    };

    WheelBlock("Front Left",FindWheel(true,true));
    ImGui::SameLine();
    WheelBlock("Front Right",FindWheel(true,false));
    WheelBlock("Rear Left",FindWheel(false,true));
    ImGui::SameLine();
    WheelBlock("Rear Right",FindWheel(false,false));

    ImGui::End();
}
