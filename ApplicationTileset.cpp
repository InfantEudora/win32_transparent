#include "ApplicationTileset.h"
#include "Debug.h"

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
        sun->viewport.zoom = 3;
        sun->SetLookAt(vec3());
        main_scene->AddObject(sun);
    }


    terrain = new IsoTerrain();
    terrain->name = "Iso Terrain";
    terrain->assetmanager = assetmanager;
    terrain->base_tile = "tile";
    terrain->height_factor = 0.2f;
    terrain->CreateTerrain(NULL,7,7,1);
    main_scene->AddObject(terrain);


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

    IsoCell* selected_cell = dynamic_cast<IsoCell*>(selected_object);
    if (selected_cell){
        if ((input->WasKeyReleased(INPUT_CLICK_LEFT) && (current_tool == ISO_TOOL_ROAD))){
            selected_cell->update_count = 0;
            selected_cell->PlaceRoad("road_straight");
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
        camera->MoveForwardBy(mouse_delta_sum / 10.0f);
        mouse_delta_sum /= 1.1;
    }
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
}

void ApplicationTileset::DrawImGuiUI(){
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

    RenderDebugMenuBar();
    RenderApplicationUI();
    RenderRandTestWindow();
    RenderOCPPClientsUI();
    RenderToolsUI();
    RenderTerrainUI();
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

    ImGui::Text("This is the Tools UI.");
    ImGui::Text("Current Tool: %d", (int)current_tool);

    if (ImGui::Button("No Tool")){
        current_tool = ISO_TOOL_NONE;
    }
    if (ImGui::Button("Road Tool")){
        current_tool = ISO_TOOL_ROAD;
    }
    if (ImGui::Button("Tree Tool")){
        current_tool = ISO_TOOL_TREE;
    }
    if (ImGui::Button("Terrain Tool")){
        current_tool = ISO_TOOL_TERRAIN;
    }

    ImGui::End();
}

void ApplicationTileset::RenderTerrainUI(){
    if (!terrain) return;

    ImGui::Begin("Terrain");

    ImGui::Text("Iso Terrain Settings");

    ImGui::End();
}