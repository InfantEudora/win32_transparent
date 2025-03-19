#include "ApplicationSim.h"
#include "OBJLoader.h"
#include <stdlib.h>
#include <string>
#include "imgooey.h"
#include "Directory.h"
#include "SpriteSheet.h"

#include "Debug.h"
static Debugger *debug = new Debugger("ApplicationSim", DEBUG_ALL);

#define INPUT_M INPUT_LAST+1
#define INPUT_R INPUT_LAST+2

ApplicationSim::ApplicationSim():Application(){
    debug->Info("Created new ApplicationSim.\n");

};

void ApplicationSim::StoreStellarObject(StellarObject* object){
    if (!object->stellarbody){
        debug->Warn("Stellar Object has no body!\n");
        return;
    }
    stellarobjects.push_back(object);
    stellarbodies.push_back(object->stellarbody);
}

//For UI testing
std::vector<Component>components;
std::vector<Operation>operations; //Should get regenerated upon opening a CPU.
SpriteSheet* icon_sprites;

void InitComponents(){
    Component cpu1;
    cpu1.name = "CPU1";
    cpu1.AddState("Operational",0,ImGooyStatusFlag_Ok);

    Component cpu2;
    cpu2.name = "CPU2";
    cpu2.AddState("Operational",0,ImGooyStatusFlag_Ok);

    Component cpu3;
    cpu3.name = "CPU3";
    cpu3.AddState("Offline",0,ImGooyStatusFlag_Fail);
    cpu3.AddState("Conflict",10,ImGooyStatusFlag_Fail);
    cpu3.AddState("Error",10,ImGooyStatusFlag_Fail);

    components.push_back(cpu1);
    components.push_back(cpu2);
    components.push_back(cpu3);

    // Load all the icons from the icon folder by extension:
    std::vector<std::string>filenames = Directory::GetFiles("data/icons","*.png");
    icon_sprites = new SpriteSheet();
    Texture temp_texture;
    for (std::string& filename: filenames){
        debug->Info("Got filename: %s\n",filename.c_str());
        temp_texture.LoadFromFile(filename.c_str(),GL_TEXTURE_2D,TEXTURE_DONT_UPLOAD);
        icon_sprites->AddSpriteFromTexture(&temp_texture,filename.c_str());
    }
    icon_sprites->Upload();
}

void GenerateComponentOperations(Component* component){
    operations.clear();
    if (component->HasStatus(ImGooyStatusFlag_Ok)){
        Operation operation;
        operation.name = "Offline";
        operations.push_back(operation);
    }else if (component->HasStatus(ImGooyStatusFlag_Fail)){
        Operation operation;
        operation.name = "Operational";
        operations.push_back(operation);
    }
}

//Function for rendering the frame to a window
DWORD WINAPI ApplicationSim::FrameThreadFunction(LPVOID lpParameter){
    ApplicationSim* app = static_cast<ApplicationSim*>(lpParameter);
    if (!app){
        debug->Fatal("No application was supplied to FrameThread\n");
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

    //Create a renderer and attach to this window
    app->renderer = new Renderer(app->main_window->width,app->main_window->height);
    app->renderer->Init();

    //Create and setup new scene
    Scene* scene = new Scene();
    app->main_scene = scene;
    scene->name = "Star Scene";
    scene->renderer = app->renderer;
    scene->inputcontroller = app->main_window->inputcontroller;

    scene->inputcontroller->AddKeyMap('M',INPUT_M);
    scene->inputcontroller->AddKeyMap('R',INPUT_R);

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    scene->shader = app->default_shader;

    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(0,30,0));
    vec3 up = vec3(0,0,1);
    scene->camera->SetLookAt(vec3(),&up);
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    //scene->camera->SetupOrthographic(scene->renderer->width,scene->renderer->height,20,0.1,100);

    scene->AddObject(scene->camera);

    //Make a light that behaves as a sun
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    //We make an assetmanager which we use to load/build all assets from:
    app->assetmanager = new AssetManager();

    //Load stuff here. At some point, this should be in a loading screen... far in the future.
    app->assetmanager->AddNewAsset("sphere","galaxy/data/meshes/sphere.obj");
    app->assetmanager->AddNewAsset("sunhighlight","galaxy/data/meshes/sunhighlight.obj");
    app->assetmanager->AddNewAsset("ship","galaxy/data/meshes/ship.obj");
    app->assetmanager->AddNewAsset("plane","galaxy/data/meshes/plane.obj");

    scene->renderer->AddMaterials(app->assetmanager->loaded_materials);

    //We make two stars
    StellarObject* stara = StellarObject::CreateNewStar(app->assetmanager);
    stara->SetPosition(vec3(-3,0,3));
    stara->UpdatePosition();
    stara->name = "Star A";
    scene->AddObject(stara);
    app->StoreStellarObject(stara);

    StellarObject* starb = StellarObject::CreateNewStar(app->assetmanager);
    starb->SetPosition(vec3(7,0,7));
    starb->UpdatePosition();
    starb->name = "Star B";
    starb->stellarbody->colony->structures[0].productionrate_slots[0].amount = 5;
    starb->stellarbody->colony->population.base_growth = 1.001;
    scene->AddObject(starb);
    app->StoreStellarObject(starb);

    StellarObject* ship1 = StellarObject::CreateNewShip(app->assetmanager);
    ship1->SetPosition(vec3(2,0,2));
    ship1->UpdatePosition();
    ship1->name = "Ship 1";
    scene->AddObject(ship1);
    app->StoreStellarObject(ship1);

    StellarObject* ship2 = StellarObject::CreateNewShip(app->assetmanager);
    ship2->SetPosition(vec3(-8,0,9));
    ship2->UpdatePosition();
    ship2->name = "Ship 2";
    scene->AddObject(ship2);
    app->StoreStellarObject(ship2);

    RouteObject* route = new RouteObject();
    route->SetupNewRoute(stara,starb,app->assetmanager);
    app->routeobjects.push_back(route);
    scene->AddObject(route);

    ship1->PlaceOnRoute(route);
    ship2->PlaceOnRoute(route);

    app->assetmanager->ListAssets();
    //Before starting anything
    scene->UpdatePhysics();

    InitComponents();

    //Catch all input and window related messages in this thread:
    MSG msg = {0};
    while (app->main_window->f_should_quit == false){
        if (app->main_window->f_resized){
            app->main_window->f_resized = false;

            //Allows for texture packing GL_PACK_ALIGNMENT 4
            app->main_window->width -= app->main_window->width % 4;
            app->main_window->height -= app->main_window->height % 4;

            app->renderer->Resize(app->main_window->width,app->main_window->height);
        }

        //Physics. TODO: Move to a seperate thread PhysicsThreadFunction in Application::
        if (app->main_scene){
            app->main_scene->HandleInput();
            app->RunLogic();
            app->main_scene->UpdatePhysics();
            app->main_scene->inputcontroller->Tick();
        }
        //Tell ImGui to start a new frame
        app->main_window->ImGuiNewFrame();

        app->main_scene->DrawFrame();

        app->UpdateUI();

        app->main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        app->main_window->DrawFrame();

        Sleep(1);
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}

void ApplicationSim::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewWindow(1600,900,&Window::wcs.at(0));
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

    //Sounddd
    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    soundsystem->AppendFile("data/sound/floop.wav","floop");

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

    //Catch all input and window related messages in this thread.
    //The thread that creates the window automagically gets messages sent to it.
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

static int popticks = -1;

//Called before update physics
void ApplicationSim::RunLogic(){
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    tmr_physics->Stop();
    tmr_physics->Restart();

    CheckObjectSelection();

    //Mouse zoom
    static float mouse_delta_sum = 0;
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
    if (mouse_delta_sum != 0){
        if (camera->type == CAMERA_TYPE_ORTHOGRAPHIC){
            camera->viewport.zoom -= mouse_delta_sum / 10.0f;
            mouse_delta_sum /= 1.1;
            camera->viewport.zoom = clamp(camera->viewport.zoom,2,50);
        }else{
            camera->MoveForwardBy(mouse_delta_sum / 10.0f);
            mouse_delta_sum /= 1.1;
        }
    }

    //Camera rotation moving
    int dx = input->GetDelta(INPUT_MOUSE_X);
    int dy = input->GetDelta(INPUT_MOUSE_Y);
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        //Camera movement in the xz plane.
        vec3 delta = vec3((float)dx / 10.0f,0,(float)dy/10.0f);
        camera->MoveBy(delta);
    }

    StellarObject* selected_stellarobject = dynamic_cast<StellarObject*>(selected_object);
    if (input->IsKeyDown(INPUT_M) && selected_stellarobject){
        vec3 delta = vec3((float)-dx / 20.0f,0,(float)-dy/20.0f);
        selected_stellarobject->MoveBy(delta);
        selected_stellarobject->UpdatePosition();
    }
    if (input->WasKeyReleased(INPUT_R) && selected_stellarobject && target_beacon){
        RouteObject* route = new RouteObject();
        route->SetupNewRoute(selected_stellarobject,target_beacon,assetmanager);
        routeobjects.push_back(route);
        main_scene->AddObject(route);
    }

    if ((!ImGui::GetIO().WantCaptureMouse) && (input->WasKeyReleased(INPUT_CLICK_LEFT))){
        //Store the clicked position on the plane.
        plane p; p.normal = vec3(0,1,0);
        int2 px = main_scene->inputcontroller->GetRelativeMousePosition();
        ray r = main_scene->camera->GetPixelRay(px);
        vec3 at = vec3();
        bool intersect = r.intersects_plane(p,at);
        if (intersect){
            target_position = at;
            if (!selected_stellarobject){
                if (!target_beacon){
                    target_beacon = StellarObject::CreateNewBeacon(assetmanager);
                    target_beacon->name = "Target Beacon";
                    main_scene->AddObject(target_beacon);
                }
                if (target_beacon){
                    target_beacon->SetPosition(target_position);
                    target_beacon->UpdatePosition();
                }
            }
        }
    }

    if (controlling_ship && (!ImGui::GetIO().WantCaptureMouse)){
        StellarObject* ship = controlling_ship;

        //Todo: Prevent auto-route following.
        if (input->IsKeyDown(INPUT_TURN_UP)){
            ship->stellarbody->MoveForward(0.025f);
        }
        if (input->IsKeyDown(INPUT_TURN_LEFT)){
            ship->stellarbody->Turn(-0.025f);
        }
        if (input->IsKeyDown(INPUT_TURN_RIGHT)){
            ship->stellarbody->Turn(0.025f);
        }
    }

    //Update each tick
    for (StellarObject* stellarobject:stellarobjects){
        if (stellarobject->stellarbody && stellarobject->stellarbody->colony){
            stellarobject->UpdatePosition();
            stellarobject->stellarbody->UpdateRouteInfo();
            stellarobject->stellarbody->FollowRoute();
        }
    }

    //Again, with updated positions
    for (RouteObject* routeobject:routeobjects){
        routeobject->UpdateRoute();
    }

    //popticks++;
    //TODO: Popticks to pause
    if (popticks > simulation_interval){
        popticks = 0;
        for (StellarObject* stellarobject:stellarobjects){
            if (stellarobject->stellarbody && stellarobject->stellarbody->colony){
                Colony* colony = stellarobject->stellarbody->colony;
                for (Structure& structure:colony->structures){
                    structure.Progress(colony->resource_slots,colony->resource_slots);
                }
                //PopulationProgress(colony->population,colony->resource_slots);
                colony->Progress();
            }
        }
    }
}

void ApplicationSim::RenderRandTestWindow(){
    static float arr[256];
    static int count = 0;


    uint8_t r = rrand.Get_uint8();
    float f = 0;
    int s = 1;
    for (int i = 0;i<s;i++){
        f += rrand.Get_uint8();
    }
    f/= s;

    arr[count++] = f;//rand() % 256;

    count = count % 256;
    //UI
    ImGui::Begin("Random Test Suite");
    ImGui::Text("This is for testing our own random functions. Neat?");
    ImGui::PlotHistogram("Histogram", arr, IM_ARRAYSIZE(arr), 0, NULL, 0.0f, 255.0f, ImVec2(0, 80.0f));

    static int rand_int = 0;
    if (ImGui::Button("Get Random Int")){
        rand_int = rrand.GetInt();
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_int);

    static int rand_limit = 0;
    static int minmax[2] = {0,1};
    ImGui::SliderInt2("Int Min / Max",minmax,-100,100);
    if (ImGui::Button("Get Random Int Between")){
        rand_limit = rrand.GetInt(minmax[0],minmax[1]);
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_limit);

    static float rand_float = 0;
    static float fminmax[2] = {0,1};
    ImGui::SliderFloat2("Float Min / Max",fminmax,-100,100);
    if (ImGui::Button("Get Random Float Between")){
        rand_float = rrand.GetFloat(fminmax[0],fminmax[1]);
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
            //float sample = rrand.GetFloat(-10,10);
            float sample = rrand.GetNormalFloat(0,4);
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

void ApplicationSim::RenderNoiseTestWindow(){
    ImGui::Begin("Perlin (and Others) Noise Test Suite");
    static bool regenerate = true;

    int texw = 512;
    int texh = 512;

    if (ImGui::CollapsingHeader("Perlin Noise")){
        if (ImGui::DragInt("Noise Seed",&pnoise.seed,1,0,3200))regenerate = true;
        if (ImGui::DragFloat("Noise Frequency",&pnoise.frequency,0.001f,0.0,10.0))regenerate = true;
        if (ImGui::DragFloat2("Noise Center Coord",(float*)&pnoise.coord,0.1f,-100.0,100.0))regenerate = true;
        if (ImGui::DragFloat("Noise Persistence",&pnoise.persistence,0.01f,0.0,10.0))regenerate = true;
        if (ImGui::DragFloat("Noise Lacunarity",&pnoise.lacunarity,0.01f,0.0,10.0))regenerate = true;
        if (ImGui::DragInt("Noise Num Octaves",&pnoise.num_octaves,1,1,10))regenerate = true;
        if (ImGui::DragFloat("Noise Offset",&pnoise.offset,1,0,256))regenerate = true;
        if (ImGui::DragFloat("Noise Scale",&pnoise.scale,0.01f,0,10.0))regenerate = true;

        if (ImGui::Button("Generate Noise"))regenerate = true;

        if (regenerate && noise_texture){
            //debug->Info("Generated a noise texture\n");
            int index = 0;
            for (int y=0;y<texh;y++){
                for (int x=0;x<texw;x++){
                    float f = pnoise.GetValue2D(x,y);
                    noise_texture->img_data[index + 0] = f;
                    noise_texture->img_data[index + 1] = f;
                    noise_texture->img_data[index + 2] = f;
                    index+=3;
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Worley Noise")){
        if (ImGui::DragInt("Grid Sizes",&wnoise.grid_size,1,1,64))regenerate = true;

        if (ImGui::DragFloat("Noise Scale",&wnoise.scale,0.1f,0,255.0))regenerate = true;
        if (ImGui::DragFloat("Max Dist",&wnoise.max_dist,0.1f,0,16.0f))regenerate = true;
        if (ImGui::DragFloat("Min Dist",&wnoise.min_dist,0.01f,0,1.0f))regenerate = true;

        if (ImGui::Button("Generate Noise"))regenerate = true;

        if (regenerate && noise_texture){
            //debug->Info("Generated a noise texture\n");
            int index = 0;
            for (int y=0;y<texh;y++){
                for (int x=0;x<texw;x++){
                    float f = wnoise.GetValue2D(x,y);
                    noise_texture->img_data[index + 0] = f;
                    noise_texture->img_data[index + 1] = f;
                    noise_texture->img_data[index + 2] = f;
                    index+=3;
                }
            }
        }
    }

    if (!noise_texture){
        ImGui::Text("Image not loaded yet");
    }else{
        ImGui::Image((ImTextureID)(intptr_t)noise_texture->texture_id, ImVec2(noise_texture->width,noise_texture->height));
    }

    static int max_stars = 500;
    static int max_rerolls = 500;
    ImGui::DragInt("Max Stars",&max_stars,1,1,1000);
    ImGui::DragInt("Max Re-Rolls",&max_rerolls,1,1,10000);

    if (ImGui::Button("Generate Starfield")){
        if (noise_texture){

            //The map that we sample from a texture is the likelyhood of a star spawning at that location.
            //Dark areas are super unlikely.
            //Bright ones are super likely.

            //Let's just
            int reroll_limit = 10000;
            int rerolls = 0;
            stellarbodies.clear();
            for (int s=0;s<max_stars;s++){
                //Pick a random point, sample that point.

                vec2 p = vec2(rrand.GetFloat(0,1),rrand.GetFloat(0,1));
                float chance = noise_texture->GetValueAt(p.x,p.y).x / 255.0f;

                //debug->Info("Sampling at %.2f,%.2f -> Likelyhood: %.0f %%\n",p.x,p.y,chance * 100.0);
                if (rrand.Roll(chance)){
                    //debug->Ok(" Spawned!\n");
                    StellarBody* star = new StellarBody();
                    star->type = BODY_STAR;
                    star->coordinate = p;
                    star->likelyhood = chance;
                    stellarbodies.push_back(star);
                }else{
                    //debug->Warn(" Didn't Spawn\n");
                    s--;
                    rerolls++;
                }

                if ((rerolls > max_rerolls) || (rerolls > reroll_limit)){
                    break;
                }
            }
        }
    }

    ImGui::Text("Number of Stars Generated: %i",stellarbodies.size());

    ImGui::End();

    if (regenerate){
        regenerate = false;
        //Build the noise texture.
        if(!noise_texture){
            noise_texture = new Texture();
            //noise_texture->Create2D(texw,texh,GL_RGB8,GL_TEXTURE_2D,1);
            //TODO: Fix this thing in its entirity.
            //Allocate data for it
            noise_texture->img_data_sz = texw*texh*3;
            noise_texture->img_data = (uint8_t*)malloc(noise_texture->img_data_sz);
            noise_texture->UploadTexture();
        }else{
            noise_texture->UploadTexture();
        }
    }
}

void ApplicationSim::RenderPopulationOverview(){
    ImGui::Begin("Population and Stuff");
    ImGui::DragInt("Simulation Interval",&simulation_interval,1,1,360);

    //Figure out which colony is selected
    StellarObject* selected_stellarobject = dynamic_cast<StellarObject*>(selected_object);
    Colony* selected_colony = NULL;
    StellarBody* body = NULL;
    if (selected_stellarobject && selected_stellarobject->stellarbody){
        selected_colony = selected_stellarobject->stellarbody->colony;
        body = selected_stellarobject->stellarbody;
    }

    Colony* colony = selected_colony;


    if (!colony){
        ImGui::Text("No Colony Selected");
    }else{
        ImGui::Text("Colony - %s",colony->name.c_str());
        ImGui::Text(" Coordinate  : %.2f, %.2f",body->coordinate.x,body->coordinate.y);
        ImGui::Text(" Credits     : %i",colony->credits);
        ImGui::Text(" Population  : %i",colony->population.amount);
        ImGui::Text(" Growth Rate : %.1f%%",(colony->population.base_growth - 1.0f)*100.0f);

        ResourceSlot* foodslot = FindResourceInSlots(colony->resource_slots,RESOURCE_FOOD);
        if (foodslot){
            ImGui::Text(" Food Stock  : %i",foodslot->amount);
        }else{
            ImGui::Text(" Food Stock  : No food!");
        }
        ImGui::Text(" Food Reserves : %i",colony->food_reserves);
        if (colony->population.food_shortage){
            ImGui::SameLine();
            ImGui::Text(" Shortage!");
        }

        for (Structure& structure:colony->structures){
            ImGui::Text(" Structure: %s",structure.name.c_str());
            for (ResourceSlot& slot:structure.productionrate_slots){
                ImGui::Text("  Production: %s at rate of %i",ResourceNameByType(slot.resource.type),slot.amount);
            }
        }

        /*if (ImGui::CollapsingHeader("Offered Contracts",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::Spacing();
            if (ImGui::BeginTable("Contracts", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)){
                int index = 0;
                for (Contract& contract:colony->contracts){
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("Contract %i", index);
                    //ImGui::Selectable(label, &selected[i], ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::TableNextColumn();

                    std::string name = contract.contract_type == BUY_CONTRACT ? "BUY ":"SELL ";
                    name += ResourceNameByType(contract.offer.resource.type);
                    ImGui::Text(name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%i",contract.offer.amount);
                    ImGui::TableNextColumn();
                    if (!contract.fulfilled){
                        if (ImGui::SmallButton("Fulfill")){
                            contract.fulfilled = true;
                            AddResourceToSlots(colony->resource_slots,contract.offer);
                            int price = contract.offer.amount * contract.markup * ResourceBasePriceByType(contract.offer.resource.type);
                            debug->Info("Fulfilled contract for total price: %i\n",price);
                            colony->credits -= price;
                        }
                    }
                    index++;
                }
                ImGui::EndTable();
            }
        }*/

        Market* market = colony->market;
        if (market){
            //We make a table of the items for sale, the amount and the price.
            ImGui::Text("Market Buys:");
            if (ImGui::BeginTable("Market Buys", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)){
                int index = 0;
                for (ResourceSlot& slot:market->buy_slots){
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", ResourceNameByType(slot.resource.type));
                    //ImGui::Selectable(label, &selected[i], ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::TableNextColumn();
                    ImGui::Text("%i",slot.amount);
                    ImGui::TableNextColumn();
                    ImGui::Text("Some Price");
                    index++;
                }
                ImGui::EndTable();
            }

            ImGui::Text("Market Sells:");
            if (ImGui::BeginTable("Market Sells", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)){
                int index = 0;
                for (ResourceSlot& slot:market->sell_slots){
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", ResourceNameByType(slot.resource.type));
                    //ImGui::Selectable(label, &selected[i], ImGuiSelectableFlags_SpanAllColumns);
                    ImGui::TableNextColumn();
                    ImGui::Text("%i",slot.amount);
                    ImGui::TableNextColumn();
                    ImGui::Text("Some Price");
                    index++;
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::Button("Add Food to Colony")){
            foodslot->IncrementResource(20);
        };

        if (body->type == BODY_SHIP){
            if (ImGui::Button("Re-route to Nearest Star")){
                //We cancel the current route.
                body->route = new Route();
                StellarBody* closest = body->FindClosest(stellarbodies,BODY_STAR);
                if (closest){
                    body->route->Setup(NULL,closest);
                    debug->Info("Routing to nearest Star at %.2f,%.2f\n",closest->coordinate.x,closest->coordinate.y);
                }
            };
            ImGui::SameLine();
            if (ImGui::Button("Take Control")){
                SetControllingShip(body);
            }
        }
    }

    vec2 at = vec2(target_position.x,target_position.z);

    ImGui::Text("Target Location = %.2f, %.2f",at.x,at.y);
    if (ImGui::Button("Create Star")){
        StellarObject* star = StellarObject::CreateNewStar(assetmanager);
        main_scene->AddObject(star);
        star->SetPosition(target_position);
        star->UpdatePosition();
        star->name = "New Star " + std::to_string(star->GetID());
        StoreStellarObject(star);
    };
    ImGui::SameLine();
    if (ImGui::Button("Create Ship")){
        StellarObject* ship = StellarObject::CreateNewShip(assetmanager);
        ship->stellarbody->colony->population.amount = rrand.GetInt(10,15);
        ship->stellarbody->colony->population.base_growth = 1.0;
        ship->stellarbody->colony->credits = rrand.GetInt(500,1000);

        main_scene->AddObject(ship);
        ship->SetPosition(target_position);
        ship->UpdatePosition();
        ship->name = "New Ship " + std::to_string(ship->GetID());
        StoreStellarObject(ship);
    };

    ImGui::Text("Hold M to move selected object");
    ImGui::Text("Press R to create a route from current object to target");
    ImGui::End();
}


/*
    The Idea being that making a UI takes forever, and ImGui is soo good...
    That maybe it's best to adapt it...
*/
void ApplicationSim::RenderSuperCustomUI(){

    //First stype we want is a button, with a half corner at left missing.
    ImGuiWindowFlags window_flags =  ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
    ImGooey::Begin("Popup UI Test Thing",NULL,window_flags);
    ImGui::Text("IM_DRAWLIST_ARCFAST_TABLE_SIZE: %lu",IM_DRAWLIST_ARCFAST_TABLE_SIZE);

    ImGui::Button("YES");
    ImGui::SameLine();
    ImGooey::CustomButton("NO ABORT",ImVec2(0,0),ImGooyItemFlag_Corner);

    ImGui::Spacing();

    ImVec2 btnsz = ImVec2(100,0);


    ImGooey::CustomButton("SENSORS",btnsz,ImGooyItemFlag_Greyed);
    ImGooey::CustomButton("SHIELDS",btnsz,ImGooyItemFlag_Greyed);
    ImGooey::CustomButton("ENGINES",btnsz,ImGooyItemFlag_Greyed);

    ImGooey::CustomButton("AUXILIARY",btnsz);
    ImGooey::StatusLabel("DEPLETED",btnsz,ImGooyStatusFlag_Ok);

    ImGooey::CustomButton("ANYTHING",btnsz,ImGooyItemFlag_Greyed);




    ImGooey::CustomButton("CPU1",btnsz,ImGooyItemFlag_Greyed);
    ImGooey::CustomButton("CPU2",btnsz,ImGooyItemFlag_Greyed|ImGooyItemFlag_Fail);
    ImGooey::CustomButton("CPU2",btnsz,ImGooyItemFlag_Fail);

    btnsz.y = 40;
    ImGooey::StatusLabel("OFFLINE",btnsz,ImGooyStatusFlag_Fail);
    ImGui::End();


    ImGooey::Begin("CPU PRIMARY DECISION TREE",NULL,window_flags);
    btnsz = ImVec2(100,0);

    static int selected_index = -1;
    //Get current selected item if any
    Component* prev_component = NULL;
    if ((selected_index >= 0) && (selected_index < components.size())){
        prev_component = &components.at(selected_index);
    }


    if (ImGui::IsKeyReleased(ImGuiKey_UpArrow)){
        selected_index--;
        if (prev_component){
            prev_component->expanded = false;

        }
    }
    if (ImGui::IsKeyReleased(ImGuiKey_DownArrow)){
        selected_index++;
        if (prev_component){
            prev_component->expanded = false;

        }
    }
    //Limit to out of bounds 1
    if (selected_index < -1){
        selected_index = -1;
    }
    if (selected_index > (int)components.size()){
        selected_index = components.size();
    }

    Component* current_component = NULL;
    if ((selected_index >= 0) && (selected_index < components.size())){
        current_component = &components.at(selected_index);
    }

    if (current_component && (current_component != prev_component)){
        debug->Info("Selected new current_component\n");
        current_component->animation = 10;
        GenerateComponentOperations(current_component);
        soundsystem->Play("click");
    }

    if (ImGui::IsKeyReleased(ImGuiKey_RightArrow)){
        if (current_component){
            current_component->expanded = true;
            soundsystem->Play("floop");
        }
        if (prev_component && prev_component != current_component){
            prev_component->expanded = false;
        }
    }
    if (ImGui::IsKeyReleased(ImGuiKey_LeftArrow)){
        if (current_component){
            current_component->expanded = false;
        }
    }

    //ImGui::Text("Selected index %i",selected_index);

    ImVec2 arrowcoord;

    ImGui::BeginGroup();
    {
        int index = 0;
        for (Component& component:components){
            ImGooyItemFlags flags = ImGooyItemFlag_None;

            ComponentState* s = component.GetMainState();
            if (s && (s->status == ImGooyStatusFlag_Fail)){
                flags |= ImGooyItemFlag_Fail;
            }
            if (index != selected_index){
                flags |= ImGooyItemFlag_Greyed;
            }
            ImGooey::CustomButton(component.name.c_str(),btnsz,flags);

            //Show the main state in small under the item?
            if (s && (index == selected_index)){
                if (component.expanded){
                    ImGui::SameLine();

                    ImGooey::CustomButton(" > ",ImVec2(0,0),ImGooyItemFlag_Greyed);
                    arrowcoord = ImGui::GetCursorPos();
                }
                float offset = 0;
                if (component.animation > 0){
                    component.animation -= 1;
                    offset = component.animation;
                }

                ImGooey::StatusLabel(s->name.c_str(),btnsz,s->status,offset);
            }
            index++;
        }
        ImGui::EndGroup();
    }
    // Capture the group size and create widgets using the same size
    ImVec2 size = ImGui::GetItemRectSize();


    if (current_component && current_component->expanded){
        ImGui::SameLine();

        ImGui::BeginGroup();
        {
            if (arrowcoord.y > 62)
            ImGui::Dummy(ImVec2(100,arrowcoord.y - 62));
            ImGooey::StatusLabel("STATE",ImVec2(100,40),ImGooyStatusFlag_None);
            std::vector<ComponentState>& current_states = current_component->states;
            for (ComponentState& s:current_states){
                ImGooyItemFlags flags = ImGooyItemFlag_None;
                if (s.status == ImGooyStatusFlag_Fail){
                    flags |= ImGooyItemFlag_Fail;
                }
                ImGooey::CustomButton(s.name.c_str(),btnsz,flags);
            }
            ImGui::EndGroup();
        }
        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            if (arrowcoord.y > 62)
                ImGui::Dummy(ImVec2(24,arrowcoord.y - 62));
            ImGooey::CustomButton(" > ",ImVec2(0,0),ImGooyItemFlag_Greyed);
            ImGui::EndGroup();
        }

        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            if (arrowcoord.y > 62)
            ImGui::Dummy(ImVec2(100,arrowcoord.y - 62));
            for (Operation& operation:operations){
                ImGooey::CustomButton(operation.name.c_str(),ImVec2(0,0),ImGooyItemFlag_Greyed);
            }

            ImGui::EndGroup();
        }
    }
    //ImGui::Button("REACTION", ImVec2((size.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f, size.y));
    ImGuiContext& g = *GImGui;
    ImGuiWindow* window = g.CurrentWindow;
    ImVec2 cursorpos = ImGui::GetCursorPos();

    float dy = window->SizeFull.y - ImGui::GetFrameHeight() - cursorpos.y;
    ImGui::Dummy(ImVec2(200,dy));

    ImGui::Text("ID 0-5604832.1 REF 0xA");
    ImGui::Text("window->SizeFull(x,y): %.0f, %.0f",window->SizeFull.x,window->SizeFull.y);
    ImGui::Text("window->DC.CursorPos: %.0f, %.0f",cursorpos.x,cursorpos.y);
    ImGui::Text("arrowcoord: %.0f, %.0f",arrowcoord.x,arrowcoord.y);
    ImGui::End();

    //Drag and drop interface
    ImGooey::Begin("CARGO INTERFACE",NULL,window_flags);
        float my_tex_w = 96;
        float my_tex_h = 96;
        //glBindTextureUnit(2,icon->texture_id);
        //glBindTexture(GL_TEXTURE_2D, icon->texture_id);

        for (int i = 0; i < 8; i++) {
            // UV coordinates are often (0.0f, 0.0f) and (1.0f, 1.0f) to display an entire textures.
            // Here are trying to display only a 32x32 pixels area of the texture, hence the UV computation.
            // Read about UV coordinates here: https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
            ImGui::PushID(i);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1,1));
            ImVec2 size = ImVec2(96.0f, 96.0f);                         // Size of the image we want to make visible
            ImVec2 uv0 = ImVec2(0.0f, 0.0f);                            // UV coordinates for lower-left
            ImVec2 uv1 = ImVec2(96.0f / my_tex_w, 96.0f / my_tex_h);    // UV coordinates for (32,32) in our texture
            ImVec4 bg_col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);             // Black background
            ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // No tint
            //ImGui::BeginDragDropSource();
            //ImGui::SetDragDropPayload("BONANZA", NULL, 0);

            int sprite_index = i % icon_sprites->Count();
            Sprite* sprite = icon_sprites->GetSprite(sprite_index);
            if (!sprite){
                debug->Fatal("Unable to get sprite index %i from SpriteSheet.\n",sprite_index);
            }

            uv0.x = sprite->uv0.x;
            uv0.y = sprite->uv0.y;
            uv1.x = sprite->uv1.x;
            uv1.y = sprite->uv1.y;

            if (ImGooey::StorageButton(1, (ImTextureID)(intptr_t)icon_sprites->texture->texture_id, size, uv0, uv1, bg_col, tint_col)){

            }

             // Our buttons are both drag sources and drag targets here!
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                // Set payload to carry the index of our item (could be anything)
                ImGui::SetDragDropPayload("STACKS", &i, sizeof(int));

                // Display preview (could be anything, e.g. when dragging an image we could decide to display
                // the filename and a small preview of the image, etc.)
                ImGui::Text("Move Stacks");
                ImGui::PushID(i);
                if (ImGui::ImageButton("", (ImTextureID)(intptr_t)icon_sprites->texture->texture_id, size, uv0, uv1, bg_col, tint_col)){

                }
                ImGui::PopID();

                ImGui::EndDragDropSource();
            }


            ImGui::PopStyleVar();
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();


        for (int i = 8; i < 16; i++) {
            // UV coordinates are often (0.0f, 0.0f) and (1.0f, 1.0f) to display an entire textures.
            // Here are trying to display only a 32x32 pixels area of the texture, hence the UV computation.
            // Read about UV coordinates here: https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples
            ImGui::PushID(i);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1,1));

            ImVec2 size = ImVec2(96.0f, 96.0f);                         // Size of the image we want to make visible
            ImVec2 uv0 = ImVec2(0.0f, 0.0f);                            // UV coordinates for lower-left
            ImVec2 uv1 = ImVec2(96.0f / my_tex_w, 96.0f / my_tex_h);    // UV coordinates for (32,32) in our texture
            ImVec4 bg_col = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);             // Black background
            ImVec4 tint_col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);           // No tint

            if (ImGui::Button("", size)){

            }

            if (ImGui::BeginDragDropTarget()){
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("STACKS",ImGuiDragDropFlags_SourceAllowNullID)){
                    debug->Info("Dropped some fat stacks on ID %i\n",i);
                    int* data = (int*)payload->Data;
                    debug->Info(" payload->DataSize: %i\n",payload->DataSize);
                    debug->Info(" payload->data: %i\n",*data);
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopStyleVar();
            ImGui::PopID();
            ImGui::SameLine();
        }

        ImGui::NewLine();

        if (ImGui::ImageButton("Le Debug Button", (ImTextureID)(intptr_t)icon_sprites->texture->texture_id, ImVec2(icon_sprites->texture->width, icon_sprites->texture->height), ImVec2(0,0), ImVec2(1,1),  ImVec4(0.0f, 0.0f, 0.0f, 1.0f), ImVec4(1.0f, 1.0f, 1.0f, 1.0f))){

        }

    ImGui::End();
}

void ApplicationSim::UpdateUI(){
    //RenderRandTestWindow();
    //RenderNoiseTestWindow();
    RenderPopulationOverview();
    RenderGenericObjectUI();
    ImGui::ShowDemoWindow();

    RenderSuperCustomUI();
}

void ApplicationSim::SetControllingShip(StellarBody* body){
    //First, we find the StellarObject that has this
    StellarObject* shipobject = NULL;
    for (StellarObject* object:stellarobjects){
        if (object->stellarbody == body){
            shipobject = object;
            break;
        }
    }
    controlling_ship = shipobject;
    if (shipobject){
        shipobject->stellarbody->route = NULL;
    }
}