#include "ApplicationShip.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationShip", DEBUG_ALL);

#define INPUT_SHOOT   INPUT_LAST+1
#define INPUT_F       INPUT_LAST+2
#define INPUT_G       INPUT_LAST+3
#define INPUT_Q       INPUT_LAST+4
#define INPUT_E       INPUT_LAST+5

ApplicationShip::ApplicationShip():Application(){
    debug->Info("Created new ApplicationShip.\n");
};

Scene* ApplicationShip::CreateEmptyScene(){
    Scene* scene = CreateNewScene("Empty Test Scene");
    //Make a sun
    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 7.0;
    sun->viewport.zoom = 10;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    scene->inputcontroller->AddKeyMap(VK_SPACE,INPUT_SHOOT);
    scene->inputcontroller->AddKeyMap('F',INPUT_F);
    scene->inputcontroller->AddKeyMap('G',INPUT_G);
    scene->inputcontroller->AddKeyMap('Q',INPUT_Q);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);
    scene->camera->SetPosition(vec3(0,25,0));
    scene->camera->SetLookAt(vec3(0,0.0,0));

    //Add phyics
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0,-9.81,0));
    scene->physics_world->SetDebugRendering(false);
    return scene;
}

void ApplicationShip::Init(void){
    //Create a renderer with initial size
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");
    renderer->alpha_clip = 0.5f;
    renderer->f_render_skybox = false;

    SetPhysicsTPS(100.0f);

    //Randomise the randomiser
    rrand = new RRandom();
    debug->Info("Polulating RRandom\n");
    rrand->Generate(512,512);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_scene = CreateEmptyScene();
    main_scene->UpdatePhysics(1.0f / physics_tps * physics_time_factor);

    assetmanager = new AssetManager();
    gltfloader.LoadGLTFFile("data/ships.glb");
    GetAllAssetsFromGLTF();

    //Let's load in the ship
    ship_character = new ShipCharacter(assetmanager,main_scene->physics_world,main_scene,rrand);
    if (ship_character){
        main_scene->AddObject(ship_character);
        //Give the exhaust some particles
        Particle* exhaust_particle = new Particle(main_scene->physics_world);
        assetmanager->GetObjectFromAsset("smoke_particle",exhaust_particle);
        ship_character->exhaust_emitter->emission_properties = {
            .emission_rate         = 10.0f,
            .emission_direction    = vec3(0,0,-1), //Relative to ship
            .emission_spread       = 20.0f,
            .particle_size_min     = 0.05f,
            .particle_size_max     = 0.25f,
            .particle_lifetime_min = 0.2f,
            .particle_lifetime_max = 0.5f,
        };
        ship_character->exhaust_emitter->AddParticleType(exhaust_particle);

        //Setup laser emitter
        Particle* laser_particle = new Particle(main_scene->physics_world);

        assetmanager->GetObjectFromAsset("laser_particle",laser_particle);
        vec3 sz = laser_particle->GetMesh()->GetExtents();
        laser_particle->GetPhysics()->AddBoxCollider(sz/2,vec3(0,0,0),quat().identity(),0.1f);
        laser_particle->SetCollideWithMaskBits(COLLISION_CATEGORY_ASTEROID);
        laser_particle->SetCollisionCategoryBits(COLLISION_CATEGORY_LASER);
        laser_particle->GetPhysics()->SetActive(false); //We don't want it to interfere until fired.
        ship_character->laser_emitter->emission_properties = {
            .emission_rate         = 10.0f,
            .emission_direction    = vec3(0,0,1), //Relative to ship
            .emission_spread       = 20.0f,
            .particle_size_min     = 1.0f,
            .particle_size_max     = 1.0f,
            .particle_lifetime_min = 0.5f,
            .particle_lifetime_max = 0.5f,
            .emission_speed_min    = 10.0f,
            .emission_speed_max    = 12.0f,
        };
        ship_character->laser_emitter->AddParticleType(laser_particle);

        //The ships laser light should be added to the scene for rendering
        main_scene->AddObject(ship_character->laser_light);
    }

    //We add a grid of cells to have a sense of speed and movement
    Object* grid = new Object();
    grid->name = "Grid of Cells";
    for (int x = -10; x <= 10; x++){
        for (int z = -10; z <= 10; z++){
            Object* cell = assetmanager->GetObjectFromAsset("gridcell");
            cell->name = "Grid Cell";
            grid->AttachChild(cell);
            if (cell){
                cell->SetPosition(vec3(x * 2,-1,z * 2));

            }
        }
    }
    main_scene->AddObject(grid);

    //A handler for dropping files onto the window
    main_window->SetOnFileDropped([this](std::string filename){
        debug->Info("File callback received with file %s\n",filename.c_str());
        //Create a modal window in the UI:
        f_filemodal = true;
        filemodal_filename = filename;
    });

    main_window->Resize(1680,900);


}

//Called before update physics after update animations
void ApplicationShip::RunLogic(){
    //Shortcuts
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    if (selected_object){
        if (f_mode_grab){
            //We use the camera left/right up down to move the character.
            int dx = input->GetDelta(INPUT_MOUSE_X);
            int dy = input->GetDelta(INPUT_MOUSE_Y);

            vec3 vdx = camera->GetLeft() * dx * 0.01;
            vec3 vdy = camera->GetUp() * -dy * 0.01;
            selected_object->MoveBy(vdx + vdy);

            if (input->WasKeyReleased(INPUT_CLICK_LEFT)){
                f_mode_grab = false;
            }
        }
    }

    if (f_mode_camera_track && ship_character){
        //We'll make the camera track the ship
        vec3 ship_pos = ship_character->GetPosition(STATE_ACCESS_PHYSICS);
        vec3 p = camera->GetPosition(STATE_ACCESS_PHYSICS);
        vec3 camera_target = ship_pos + vec3(0,zoom_target,0);
        vec3 diff = p.lerp(camera_target,0.04f);
        camera->SetPosition(diff);

        //This will also rotate the camera to look at the ship
        /*
        quat r = camera->GetRotation();
        quat t = quat::getquat(ship_pos,camera->GetWorldPosition(STATE_ACCESS_PHYSICS),-Object::ref_forward);
        t.normalize();
        r = quat::slerp(r,t,0.15f);
        camera->SetRotation(r);
        */
    }

    if (f_lock_ship_axis && ship_character){
        vec3 angular_vel = ship_character->GetPhysics()->GetAngularVelocity();
        if (angular_vel.length() > 0.01f){
            //Dampen the angular velocity
            ship_character->GetPhysics()->SetAngularVelocity(angular_vel * 0.95f);
        }


        //We attempt to keep the ship upright
        quat ship_rot = ship_character->GetRotation();

        vec3 fwd = ship_character->GetForward(STATE_ACCESS_PHYSICS);
        //We want the forward vector to have no y component
        vec3 corrected_fwd = vec3(fwd.x,0,fwd.z).normalize();

        quat q1 = quat::getquat(corrected_fwd,vec3(),vec3(0,1,0));

        quat q2 = quat::slerp(ship_rot,q1.normalize(),0.05f);

        ship_character->SetRotation(q2);

        //We also attempt to keep the ship at y=0
        vec3 ship_pos = ship_character->GetPosition(STATE_ACCESS_PHYSICS);
        if (ship_pos.y < -0.1f || ship_pos.y > 0.1f){
            vec3 v = ship_character->GetVelocity();
            ship_character->SetVelocity(v - vec3(0,ship_pos.y * 0.1f,0));
        }
    }

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }

    CheckObjectSelection();



    //Camera rotation moving
    /*
    if (main_window->f_has_focus && input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        //f_show_rightclick_menu = false;
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
    }*/

    //Mouse wheel for zoom
    static float mouse_delta_sum = 0;
    if (mouse_delta_sum != 0){
        zoom_target -= mouse_delta_sum * 0.5f;
        if (zoom_target < 5.0f) zoom_target = 5.0f;
        if (zoom_target > 80.0f) zoom_target = 80.0f;
        mouse_delta_sum = 0;
    }
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);

    //Character input
    if (ship_character && main_window->f_has_focus){
        if (input->IsKeyDown(INPUT_TURN_UP)){
            ship_character->MoveForwardBy(100);
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            ship_character->MoveBackwardBy(100);
        }
        if (input->IsKeyDown(INPUT_TURN_RIGHT)){
            ship_character->TurnRightBy(0.05f);
            ship_character->RollBy(-0.05f);
        }
        if (input->IsKeyDown(INPUT_TURN_LEFT)){
            ship_character->TurnLeftBy(0.05f);
            ship_character->RollBy(0.05f);
        }
        if (input->IsKeyDown(INPUT_Q)){
            ship_character->RollBy(0.05f);
        }
        if (input->IsKeyDown(INPUT_E)){
            ship_character->RollBy(-0.05f);
        }
        if (input->IsKeyDown(INPUT_F)){
            ship_character->f_animation_override = true;
            ship_character->animation_override_ticks++;
        }
        if (input->WasKeyReleased(INPUT_G)){
            f_mode_grab = !f_mode_grab;
        }
        if (input->IsKeyDown(INPUT_SHOOT)){
            ship_character->ShootLaser();
        }
    }
}

void ApplicationShip::DrawImGuiUI(){
    //We're asked to import the f_filemodal file.
    if (f_import_file){
        debug->Info("Starting import of file %s\n",filemodal_filename.c_str());
        if (!assetmanager){
            debug->Info("No AssetManager. Creating...\n");
            assetmanager = new AssetManager();
        }
        Debugger* handle = debug->FindHandle("GLTFLoader");
        handle->SetLevel(DEBUG_INFO);
        gltfloader.LoadGLTFFile(filemodal_filename.c_str());
        f_import_file = false;
        GetAllAssetsFromGLTF();
    }

    //ImGui::ShowDemoWindow();
    RenderDebugMenuBar();
    RenderApplicationUI();
    //RenderShaderUI(default_shader);

    ImGui::Begin("Ship Settings");
    if (ship_character){
        ImGui::Checkbox("Enable Manual Animations",&ship_character->f_animation_override);
        ImGui::Checkbox("Grab Mode",&f_mode_grab);
        ImGui::Checkbox("Camera Track",&f_mode_camera_track);
        ImGui::Checkbox("Lock Ship Axis",&f_lock_ship_axis);
        ImGui::Text("Ship Up: (%.2f, %.2f, %.2f)",ship_character->GetUp(STATE_ACCESS_RENDERER).x,ship_character->GetUp(STATE_ACCESS_RENDERER).y,ship_character->GetUp(STATE_ACCESS_RENDERER).z);
        ImGui::Text("Ship Y-Pos: %.2f",ship_character->GetPosition(STATE_ACCESS_RENDERER).y);
        if (ImGui::Button("Reset Ship Position")){
            ship_character->SetPosition(vec3(0,0,0));
            ship_character->SetRotation(quat().identity());
        }
        if (ImGui::Button("Add Asteroid")){
            Asteroid* asteroid = new Asteroid(assetmanager,main_scene->physics_world,main_scene,rrand);
            if (asteroid){
                vec3 ship_pos = ship_character->GetPosition(STATE_ACCESS_RENDERER);
                vec3 offset = vec3(rrand->GetFloat(-10,10),rrand->GetFloat(-0.2,0.2),rrand->GetFloat(-10,10));
                asteroid->SetPosition(ship_pos + offset);
                asteroid->SetScale(vec3(rrand->GetFloat(0.8f,1.5f)));
                asteroid->UpdatePhysicsState();
                main_scene->AddObject(asteroid);
            }
        }
        ImGui::End();
    }


    if (f_filemodal){
        ImGui::OpenPopup("Attempt to import assets?");
    }

    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Attempt to import assets?", NULL, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Attempt to import [%s] with assetmanager?",filemodal_filename.c_str());
        ImGui::Separator();
        if (ImGui::Button("Yes, go ahead.", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            f_filemodal = false;
            f_import_file = true;
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No, never mind", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
            f_filemodal = false;
            f_import_file = false;
        }
        ImGui::EndPopup();
    }
}

//Called from within physics update.
void ApplicationShip::onContact(const rp3d::CollisionCallback::CallbackData& callbackData){
    //debug->Info("Contact: num pairs %hhu\n",callbackData.getNbContactPairs());
    for (uint32_t i = 0; i < callbackData.getNbContactPairs(); i++) {
    }
}