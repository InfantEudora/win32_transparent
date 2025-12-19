#include "ApplicationTileset.h"
#include "Debug.h"
#include "Directory.h"

static Debugger *debug = new Debugger("ApplicationTileset", DEBUG_ALL);

ApplicationTileset::ApplicationTileset():Application(){
    debug->Info("Created new application.\n");
};

ApplicationTileset::~ApplicationTileset(){
    if (http_server){
        http_server->Stop();
        delete http_server;
        http_server = nullptr;
    }
};

void ApplicationTileset::Init(void){
    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->SetVSync(true);

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    assetmanager = new AssetManager();

    //Setup sound system
    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    soundsystem->AppendFile("isocity/data/car_horn_1.wav","car_horn_1");

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics();


    gltfloader.LoadGLTFFile("data/cityandroads.glb");
    GetAllAssetsFromGLTF();
    {
        //Setup sun light
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Directional Light (Sun)";
        sun->SetPosition(vec3(-10,7,9));
        sun->color = vec3(1,0.85,0.7);
        sun->brightness = 6.0;
        sun->viewport.zoom = 5;
        sun->SetLookAt(vec3());
        main_scene->AddObject(sun);
    }

    //Setup Random Generator
    rrand = new RRandom();
    rrand->Generate(512,512);

    terrain = new IsoTerrain();
    terrain->name = "Iso Terrain";
    terrain->assetmanager = assetmanager;
    terrain->base_tile = "tile";
    terrain->height_factor = 0.2f;
    terrain->CreateTerrain(NULL, rrand, 11,11,1);
    main_scene->AddObject(terrain);

    Object* compass = CreateNewObjectFromGLTF("compass",main_scene);

    //Add phyics
    main_scene->physics_world = new PhysicsWorld();
    main_scene->physics_world->SetGravity(vec3(0,-9.81,0));
    main_scene->physics_world->SetDebugRendering(false);
    main_scene->physics_world->rp_world->setEventListener(this);

    icon_sprites = new SpriteSheet();
    Texture temp_texture;
    // Load all the icons from the icon folder by extension:
    std::vector<std::string>filenames = Directory::GetFiles("isocity/data/icons","*.png");
    for (std::string& filename: filenames){
        debug->Info("Got filename: %s\n",filename.c_str());
        temp_texture.LoadFromFile(filename.c_str(),GL_TEXTURE_2D,TEXTURE_DONT_UPLOAD);
        icon_sprites->AddSpriteFromTexture(&temp_texture,filename.c_str());
    }

    icon_sprites->Upload();

    //Create an HTTP server to listen for connections
    http_server = new HTTPServer(9090);
    if (!http_server->Start()){
        debug->Fatal("Failed to start HTTP server\n");
    }
    // Set initial values that the page can display
    http_server->SetVariable("playerHealth", "100");
    http_server->SetVariable("score", "0");
    http_server->SetVariable("fps", "0");
    // initial string values (mV, min/max, temp)
    http_server->SetVariable("string1_mv", "3300");
    http_server->SetVariable("string1_min_mv", "3290");
    http_server->SetVariable("string1_max_mv", "3310");
    http_server->SetVariable("string1_temp_c", "25");

    http_server->SetVariable("string2_mv", "3300");
    http_server->SetVariable("string2_min_mv", "3280");
    http_server->SetVariable("string2_max_mv", "3320");
    http_server->SetVariable("string2_temp_c", "24");

    http_server->SetVariable("string3_mv", "3300");
    http_server->SetVariable("string3_min_mv", "3270");
    http_server->SetVariable("string3_max_mv", "3330");
    http_server->SetVariable("string3_temp_c", "23");
    http_server->SetVariable("string1_soc", "100");
    http_server->SetVariable("string2_soc", "80");
    http_server->SetVariable("string3_soc", "60");
    // initial operation mode
    http_server->SetVariable("operationMode", "normal");
    // initial per-mode enabled flags
    http_server->SetVariable("mode_netzero_enabled", "1");
    http_server->SetVariable("mode_charge_enabled", "1");
    http_server->SetVariable("mode_discharge_enabled", "1");
    debug->Info("HTTP Server started on port 9090\n");

    main_window->Resize(1600,800);
}

void ApplicationTileset::StartDrag(IsoCell* cell){

}


static unsigned long long g_lastTick = GetTickCount64();
static int g_frames = 0;
static int g_lastFPS = 0;

//Called before update physics
void ApplicationTileset::RunLogic(){
    // Count frames and update FPS once per second
    g_frames++;
    unsigned long long now = GetTickCount64();
    if (now - g_lastTick >= 1000) {
        g_lastFPS = g_frames;
        g_frames = 0;
        g_lastTick = now;
        if (http_server) {
            http_server->SetVariable("fps", std::to_string(g_lastFPS));
        }
    }

    //Only when in focus
    if (!main_window->f_has_focus){
        return;
    }

    //Shortcuts
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }

    CheckObjectSelection();

    if (input->IsKeyDown(INPUT_CLICK_RIGHT)){
        current_tool = ISO_TOOL_NONE;
    }

    terrain->ClearUpdateCounts();



    if (controlled_car){
        if (input->IsKeyDown(INPUT_TURN_UP)){
            controlled_car->Accelerate(1.0f);
            controlled_car->f_has_target = false;
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            controlled_car->Reverse(1.0f);
            controlled_car->f_has_target = false;
        }
        if (input->IsKeyDown(INPUT_TURN_LEFT)){
            controlled_car->SteerLeft(1.0f);
            controlled_car->f_has_target = false;
        }
        if (input->IsKeyDown(INPUT_TURN_RIGHT)){
            controlled_car->SteerRight(1.0f);
            controlled_car->f_has_target = false;
        }
    }

    IsoCell* selected_cell = dynamic_cast<IsoCell*>(selected_object);
    if (selected_cell){
        if (input->WasKeyReleased(INPUT_CLICK_LEFT)){
            if (current_tool == ISO_TOOL_ROAD){
                selected_cell->update_count = 0;
                selected_cell->PlaceRoad("road_straight");
            }else if (current_tool == ISO_TOOL_CAR){
                PlaceCar(selected_cell);
            }else if (current_tool == ISO_TOOL_TREE){
                selected_cell->PlaceTree("pine_tree_1");
            }else if (current_tool == ISO_TOOL_HOUSE){
                PlaceHouse(selected_cell);

            }else if (current_tool == ISO_TOOL_NONE){
                for (IsoCar* car:cars){
                    car->SetTargetCell(selected_cell);
                    debug->Info("Car target set to cell %i,%i\n",selected_cell->coordinate.x,selected_cell->coordinate.y);
                }
            }
        }
        StartDrag(selected_cell);
    }

    //Camera rotation moving
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
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
    }

    //Mouse wheel for zoom
    static float mouse_delta_sum = 0;
    if (mouse_delta_sum != 0){
        vec3 diff = camera->GetForward() - camera_target;
        float dist = diff.length() * mouse_delta_sum;
        float delta = dist / 50.0f;

        camera->MoveForwardBy(dist / 50.0f);

        mouse_delta_sum /= 1.1;
    }
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
}

void ApplicationTileset::RenderHTTPTestUI(){
    //UI
    ImGui::Begin("Hi there!");
    ImGui::Text("This application only renders a window.");

    // Player health slider
    if (ImGui::SliderInt("Player Health", &ui_playerHealth, 0, 100)){
        if (http_server) http_server->SetVariable("playerHealth", std::to_string(ui_playerHealth));
    }

    // String 1 (cell mV, min, max, temp)
    if (ImGui::SliderInt("String 1 - Cell mV", &ui_string0_mv, 2800, 4200)){
        if (http_server) http_server->SetVariable("string1_mv", std::to_string(ui_string0_mv));
    }
    if (ImGui::SliderInt("String 1 - SOC %", &ui_string0_soc, 0, 100)){
        if (http_server) http_server->SetVariable("string1_soc", std::to_string(ui_string0_soc));
    }
    if (ImGui::SliderInt("String 1 - Min mV", &ui_string0_min_mv, 0, ui_string0_mv)){
        if (http_server) http_server->SetVariable("string1_min_mv", std::to_string(ui_string0_min_mv));
    }
    if (ImGui::SliderInt("String 1 - Max mV", &ui_string0_max_mv, ui_string0_mv, 5000)){
        if (http_server) http_server->SetVariable("string1_max_mv", std::to_string(ui_string0_max_mv));
    }
    if (ImGui::SliderInt("String 1 - Temp (C)", &ui_string0_temp_c, -40, 120)){
        if (http_server) http_server->SetVariable("string1_temp_c", std::to_string(ui_string0_temp_c));
    }

    // String 2
    if (ImGui::SliderInt("String 2 - Cell mV", &ui_string1_mv, 2800, 4200)){
        if (http_server) http_server->SetVariable("string2_mv", std::to_string(ui_string1_mv));
    }
    if (ImGui::SliderInt("String 2 - SOC %", &ui_string1_soc, 0, 100)){
        if (http_server) http_server->SetVariable("string2_soc", std::to_string(ui_string1_soc));
    }
    if (ImGui::SliderInt("String 2 - Min mV", &ui_string1_min_mv, 0, ui_string1_mv)){
        if (http_server) http_server->SetVariable("string2_min_mv", std::to_string(ui_string1_min_mv));
    }
    if (ImGui::SliderInt("String 2 - Max mV", &ui_string1_max_mv, ui_string1_mv, 5000)){
        if (http_server) http_server->SetVariable("string2_max_mv", std::to_string(ui_string1_max_mv));
    }
    if (ImGui::SliderInt("String 2 - Temp (C)", &ui_string1_temp_c, -40, 120)){
        if (http_server) http_server->SetVariable("string2_temp_c", std::to_string(ui_string1_temp_c));
    }

    // String 3
    if (ImGui::SliderInt("String 3 - Cell mV", &ui_string2_mv, 2800, 4200)){
        if (http_server) http_server->SetVariable("string3_mv", std::to_string(ui_string2_mv));
    }
    if (ImGui::SliderInt("String 3 - SOC %", &ui_string2_soc, 0, 100)){
        if (http_server) http_server->SetVariable("string3_soc", std::to_string(ui_string2_soc));
    }
    if (ImGui::SliderInt("String 3 - Min mV", &ui_string2_min_mv, 0, ui_string2_mv)){
        if (http_server) http_server->SetVariable("string3_min_mv", std::to_string(ui_string2_min_mv));
    }
    if (ImGui::SliderInt("String 3 - Max mV", &ui_string2_max_mv, ui_string2_mv, 5000)){
        if (http_server) http_server->SetVariable("string3_max_mv", std::to_string(ui_string2_max_mv));
    }
    if (ImGui::SliderInt("String 3 - Temp (C)", &ui_string2_temp_c, -40, 120)){
        if (http_server) http_server->SetVariable("string3_temp_c", std::to_string(ui_string2_temp_c));
    }

    // Score slider
    if (ImGui::SliderInt("Score", &ui_score, 0, 100000)){
        if (http_server) http_server->SetVariable("score", std::to_string(ui_score));
    }

    // Show last measured FPS
    ImGui::Text("FPS: %d", g_lastFPS);

    // Operation Mode: show and allow selection
    const char* modes[] = { "normal", "performance", "conservative" };
    static int opModeIdx = 0;
    if (ImGui::Combo("Operation Mode", &opModeIdx, (const char* const*)modes, IM_ARRAYSIZE(modes))){
        if (http_server) http_server->SetVariable("operationMode", std::string(modes[opModeIdx]));
    }

    // Mode availability toggles (enabled/disabled)
    if (ImGui::Checkbox("Enable NetZero", &ui_mode_netzero_enabled)){
        if (http_server) http_server->SetVariable("mode_netzero_enabled", ui_mode_netzero_enabled ? "1" : "0");
    }
    if (ImGui::Checkbox("Enable Charge", &ui_mode_charge_enabled)){
        if (http_server) http_server->SetVariable("mode_charge_enabled", ui_mode_charge_enabled ? "1" : "0");
    }
    if (ImGui::Checkbox("Enable Discharge", &ui_mode_discharge_enabled)){
        if (http_server) http_server->SetVariable("mode_discharge_enabled", ui_mode_discharge_enabled ? "1" : "0");
    }

    ImGui::End();
}

void ApplicationTileset::DrawImGuiUI(){
    //RenderHTTPTestUI();
    RenderDebugMenuBar();
    RenderApplicationUI();
    //RenderRandTestWindow();
    //RenderOCPPClientsUI();
    RenderToolsUI();
    RenderTerrainUI();
    RenderSelectedCarUI();
    RenderSelectedRoadUI();
}

void ApplicationTileset::RenderOCPPClientsUI(){
    if (!http_server) return;

    ImGui::Begin("OCPP Clients");

    ImGui::Text("Connected OCPP Clients: %d", (int)http_server->m_ocppClients.size());
    int client_idx = 0;
    for (SOCKET client_socket : http_server->m_ocppClients){
        ImGui::PushID(client_idx);
        ImGui::Text("Client %d - Socket %llu", client_idx, (unsigned long long)client_socket);
        client_idx++;
        ImGui::PopID();
    }

    ImGui::End();
}

void ApplicationTileset::RenderToolsUI(){
    ImGui::Begin("Tools");
    ImGui::Text("Current Tool: %d", (int)current_tool);



    for (int i = 0; i < 5; i++) {
        std::string id = "Button" + std::to_string(i);
        ImVec2 size = ImVec2(64.0f, 64.0f);
        ImVec2 uv0 = ImVec2(0.0f, 0.0f);
        ImVec2 uv1 = ImVec2(1.0f, 1.0f);

        int sprite_index = i % icon_sprites->Count();
        Sprite* sprite = icon_sprites->GetSprite(sprite_index);
        if (!sprite){
            debug->Fatal("Unable to get sprite index %i from SpriteSheet.\n",sprite_index);
        }
        uv0.x = sprite->uv0.x;
        uv0.y = sprite->uv0.y;
        uv1.x = sprite->uv1.x;
        uv1.y = sprite->uv1.y;

        if (ImGui::ImageButton(id.c_str(), (ImTextureID)(intptr_t)icon_sprites->texture->texture_id, size, uv0, uv1 )){
            switch(i){
                case 0:
                current_tool = ISO_TOOL_CAR;
                if (controlled_car){
                    controlled_car->HonkHorn();
                }
                break;
                case 1:
                current_tool = ISO_TOOL_HOUSE;
                break;
                case 2:
                current_tool = ISO_TOOL_NONE;
                break;
                case 3:
                current_tool = ISO_TOOL_ROAD;
                break;
                case 4:
                current_tool = ISO_TOOL_TREE;
                break;
            }

        }
        ImGui::SameLine();
    }

    ImGui::End();
}

void ApplicationTileset::RenderSelectedRoadUI(){
    ImGui::Begin("Selected Road");

    IsoRoad* road = dynamic_cast<IsoRoad*>(selected_object);
    if (road && road != selected_road){
        selected_road = road;
    }
    road = selected_road;
    if (!road){
        ImGui::Text("No road selected");
        ImGui::End();
        return;
    }
    ImGui::Text("Road Type: (%i) %s",road->road_type, RoadTypeToString(road->road_type).c_str());
    if (ImGui::Button("Show Road Markers")){

    }
    ImGui::End();
}

void ApplicationTileset::RenderSelectedCarUI(){
    ImGui::Begin("Selected Car");
    IsoCar* car = dynamic_cast<IsoCar*>(selected_object);
    if (car && car != selected_car){
        selected_car = car;
    }
    car = selected_car;

    if (selected_object){
        if (ImGui::Button("Set as route start")){
            if (!route_object){
                route_object = new RouteObject();
                main_scene->AddObject(route_object);
            }
            route_object->SetupNewRoute(selected_object,route_object->GetEndObject(),assetmanager);
            route_object->MoveUpBy(0.1f);
        }
        if (ImGui::Button("Set as route end")){
            if (!route_object){
                route_object = new RouteObject();
                main_scene->AddObject(route_object);
            }
            route_object->SetupNewRoute(route_object->GetStartObject(),selected_object,assetmanager);
            route_object->MoveUpBy(0.1f);
        }
    }

    if (!car){
        ImGui::Text("No car selected");
        ImGui::End();
        return;
    }

    if (controlled_car == car){
        if (ImGui::Button("Release contol of Car")){
            controlled_car->f_has_target = !!controlled_car->next_cell;
            controlled_car = NULL;

        }
    }else{
        if (ImGui::Button("Take contol of Car")){
            controlled_car = car;
        }
    }
    if (ImGui::Button("Clear Destination")){
        car->f_has_target = false;
    }
    if (ImGui::Button("Pick New Destination")){
        car->FindNewDestination(5);
    }
    if (ImGui::Button("Honk Horn")){
        car->HonkHorn();
    }

    if (car->current_cell){
        ImGui::Text("Current Cell: %s",car->current_cell->name.c_str());
        IsoRoad* current_road = car->current_cell->object_road;
        if (current_road){
            ImGui::Text("Current Road Type: %s",RoadTypeToString(current_road->road_type).c_str());
        }else{
            ImGui::Text("No Road");
        }
    }else{
        ImGui::TextColored(ImVec4(1,0,0,1), "Car has no current Cell!");
    }
    if (car->next_cell){
        ImGui::Text("Next    Cell: %s",car->next_cell->name.c_str());
    }else{
        ImGui::TextColored(ImVec4(1,1,0,1), "Car has no next Cell");
    }

    ImGui::Text("Car Has Target: %s", car->f_has_target?"Yes":"No");


    ImGui::End();
}

void ApplicationTileset::RenderTerrainUI(){
    if (!terrain) return;

    ImGui::Begin("Terrain");

    ImGui::Text("Iso Terrain Settings");
    IsoCell* hovered_cell = dynamic_cast<IsoCell*>(hovered_object);
    if (hovered_cell){
        ImGui::Text("Hovered Cell: %i,%i", hovered_cell->coordinate.x, hovered_cell->coordinate.y);
    }else{
        ImGui::Text("No cell hovered");
    }

    plane p;
    p.pos = vec3(0,0,0);
    p.normal = vec3(0,1,0);
    int2 px = main_scene->inputcontroller->GetRelativeMousePosition();

    ray r = main_scene->camera->GetPixelRay(px);

    vec3 at = {};
    bool intersect = r.intersects_plane(p,at);

    if (intersect){
        ImGui::DragFloat3("Intersection at", (float*)&at, 0.01f, -1.0f, 1.0f);

    }else{
        ImGui::Text("No intersection");
    }
    /*IsoCell* cell = terrain->FindCellByWorldPosition(at);
    if (cell){
        ImGui::Text("Cell at intersection: %i,%i", cell->coordinate.x, cell->coordinate.y);
    }*/

    ImGui::Separator();
    ImGui::Text("Controlled Car: %s",controlled_car?controlled_car->name.c_str():"(none)");

    for (IsoCar* car:cars){
        vec3 car_pos = car->GetPosition();
        ImGui::Text("Car [%s] at %5.2f,%5.2f,%5.2f", car->name.c_str(), car_pos.x, car_pos.y, car_pos.z);
        int car_dir = car->direction;
        ImGui::Text(" Car Direction: %d (%s)", car_dir, IsoDirection::ToString(car_dir).c_str());
        ImGui::Text(" Car Has Target: %s", car->f_has_target?"Yes":"No");
        ImGui::Text(" Car Speed: %5.2f m/s (%5.2f Top)", car->speed, car->top_speed);
        ImGui::Text(" Car Reverse: %s", car->f_reverse?"Yes":"No");
        ImGui::Text(" Car Gas Pedal: %5.2f", car->gas_pedal);
        ImGui::Text(" Car Brake Pedal: %5.2f", car->brake_pedal);
        ImGui::Text(" Car Close Car: %s", car->close_car?car->close_car->name.c_str():"(none)");
        ImGui::Text(" Car Time Waiting: %5.2f", car->time_waiting_for_car_ahead);
        ImGui::Text(" Car Time Threshold: %5.2f", car->time_waiting_threshold);

        ImGui::Text(" Car Path:");
        for (IsoCell* path_cell : car->path.cells){
            ImGui::Text("  Cell %i,%i", path_cell->coordinate.x, path_cell->coordinate.y);
        }

    }
    ImGui::End();
}

void ApplicationTileset::onTrigger(const reactphysics3d::OverlapCallback::CallbackData& callbackData){
    //debug->Trace("Trigger: num overlap pairs %hhu\n",callbackData.getNbOverlappingPairs());
    for (uint8_t i = 0; i < callbackData.getNbOverlappingPairs(); i++){
        reactphysics3d::OverlapCallback::OverlapPair overlapPair = callbackData.getOverlappingPair(i);

        rp3d::Collider* collider1 = overlapPair.getCollider1();
        rp3d::Collider* collider2 = overlapPair.getCollider2();
        uint32_t bits1 = collider1->getCollisionCategoryBits();
        uint32_t bits2 = collider2->getCollisionCategoryBits();

        if (bits1 == COLLISION_CATEGORY_CAR_PROXIMITY && bits2 == COLLISION_CATEGORY_CAR){
            Object* d1 = (Object*)overlapPair.getBody1()->getUserData();
            Object* d2 = (Object*)overlapPair.getBody2()->getUserData();

            IsoCar* proximity_car = dynamic_cast<IsoCar*>(d1);
            IsoCar* other_car = dynamic_cast<IsoCar*>(d2);
            if (proximity_car && other_car){
                //debug->Info("Car-Proximity Trigger Event: %s detected %s nearby\n",proximity_car->name.c_str(),other_car->name.c_str());
                proximity_car->close_car = other_car;
            }
        }
        else if (bits1 == COLLISION_CATEGORY_CAR && bits2 == COLLISION_CATEGORY_CAR_PROXIMITY){
            Object* d1 = (Object*)overlapPair.getBody1()->getUserData();
            Object* d2 = (Object*)overlapPair.getBody2()->getUserData();

            IsoCar* other_car = dynamic_cast<IsoCar*>(d1);
            IsoCar* proximity_car = dynamic_cast<IsoCar*>(d2);
            if (proximity_car && other_car){
                //debug->Info("Car-Proximity Trigger Event: %s detected %s nearby\n",proximity_car->name.c_str(),other_car->name.c_str());
                proximity_car->close_car = other_car;
            }
        }
    }
}

void ApplicationTileset::PlaceCar(IsoCell* target_cell){

    IsoCar* car = new IsoCar();
    assetmanager->GetObjectFromAsset("car_sedan", car);
    if (car){
        car->SetPosition(target_cell->GetWorldPosition(STATE_ACCESS_PHYSICS));
        car->AddPhysics(main_scene->physics_world);
        if (Physics* physics = car->GetPhysics()){
            vec3 extent = car->GetMesh()->GetExtents()*0.5f;
            //The main collider
            physics->AddBoxCollider(extent,vec3(0,extent.y,0),quat().identity());
            //physics->AddSphereCollider(0.15f,vec3(0,extent.y,0),quat().identity());
            physics->SetTrigger(true);
            physics->body->collider->setCollisionCategoryBits(COLLISION_CATEGORY_CAR);
            //Add a box collider in front of the car to act as a trigger for proximity detection
            //Box extends from car position forward to where the sphere would have reached
            vec3 box_extent = vec3(extent.x, extent.y, 1.5f * extent.z);
            vec3 box_offset = vec3(0, extent.y, -3.0f * extent.z);
            physics->AddBoxCollider(box_extent, box_offset, quat().identity());
            physics->body->collider->setCollisionCategoryBits(COLLISION_CATEGORY_CAR_PROXIMITY);
            physics->SetTrigger(true);
            physics->body->rigidbody->setIsAllowedToSleep(false);

            physics->SetGravityEnabled(false);
            physics->SetStatic(false);
            physics->body->rigidbody->setUserData((Object*)car);

        }
        car->top_speed = rrand->GetFloat(0.5,1.0);
        car->soundsystem = soundsystem;
        car->randgen = rrand;
        car->name = "Car " + std::to_string(cars.size());
        car->current_cell = target_cell;
        main_scene->AddObject(car);
        cars.push_back(car);
    }
}

void ApplicationTileset::PlaceHouse(IsoCell* target_cell){
    if (target_cell->object_road){
        debug->Warn("Cannot place house on a road.\n");
        return;
    }
    IsoHouse* house = new IsoHouse();
    assetmanager->GetObjectFromAsset("house_1", house);
    if (house){
        vec3 pos = target_cell->GetWorldPosition(STATE_ACCESS_PHYSICS);
        house->SetPosition(pos);
        house->AddPhysics(main_scene->physics_world);
        if (Physics* physics = house->GetPhysics()){
            vec3 extent = house->GetMesh()->GetExtents()*0.5f;
            physics->AddBoxCollider(extent, vec3(0, extent.y, 0), quat().identity());
            physics->SetStatic(true);
            physics->body->collider->setCollisionCategoryBits(COLLISION_CATEGORY_SCENERY);
        }
        house->name = "House " + std::to_string(houses.size());
        main_scene->AddObject(house);
        houses.push_back(house);
        debug->Info("Placed house at cell %i,%i\n", target_cell->coordinate.x, target_cell->coordinate.y);
    }
}