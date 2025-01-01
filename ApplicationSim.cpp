#include "ApplicationSim.h"
#include "OBJLoader.h"
#include <stdlib.h>
#include <string>

#include "Debug.h"
static Debugger *debug = new Debugger("ApplicationSim", DEBUG_ALL);

ApplicationSim::ApplicationSim():Application(){
    debug->Info("Created new application.\n");
};

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

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    scene->shader = app->default_shader;

    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(0,5,0));
    vec3 up = vec3(0,0,1);
    scene->camera->SetLookAt(vec3(),&up);
    //scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->camera->SetupOrthographic(scene->renderer->width,scene->renderer->height,20,0.1,100);
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

    scene->renderer->AddMaterials(app->assetmanager->loaded_materials);

    //We make two stars
    StellarObject* star = StellarObject::CreateNewStar(app->assetmanager);
    scene->AddObject(star);
    app->stellarobjects.push_back(star);

    star = StellarObject::CreateNewStar(app->assetmanager);
    star->SetPosition(vec3(vec3(4,0,4)));
    app->stellarobjects.push_back(star);
    scene->AddObject(star);

    StellarObject* ship = StellarObject::CreateNewShip(app->assetmanager);
    ship->SetPosition(vec3(vec3(2,0,2)));
    app->stellarobjects.push_back(ship);
    scene->AddObject(ship);

    app->assetmanager->ListAssets();
    //Before starting anything
    scene->UpdatePhysics();

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
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}

void ApplicationSim::Run(void){
    int2 dimensions = GetDisplaySettings();

    //Create a main window
    main_window = Window::CreateNewWindow(1280,720,&Window::wcs.at(0));
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
        camera->viewport.zoom -= mouse_delta_sum / 10.0f;
        mouse_delta_sum /= 1.1;
        camera->viewport.zoom = clamp(camera->viewport.zoom,2,50);
    }

    //Camera rotation moving
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        int dx = input->GetDelta(INPUT_MOUSE_X);
        int dy = input->GetDelta(INPUT_MOUSE_Y);

        //Camera movement in the xz plane.
        vec3 delta = vec3((float)dx / 10.0f,0,(float)dy/10.0f);
        camera->MoveBy(delta);
    }

    popticks++;
    if (popticks > simulation_interval){
        popticks = 0;
        for (StellarObject* stellarobject:stellarobjects){
            if (stellarobject->stellarbody && stellarobject->stellarbody->colony){
                stellarobject->UpdatePosition();
                Colony* colony = stellarobject->stellarbody->colony;
                for (Structure& structure:colony->structures){
                    structure.Progress(colony->resource_slots,colony->resource_slots);
                }
                PopulationProgress(colony->population,colony->resource_slots);
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
            noise_texture->Create2D(texw,texh,GL_RGB8,GL_TEXTURE_2D,1);
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
    if (selected_stellarobject && selected_stellarobject->stellarbody){
        selected_colony = selected_stellarobject->stellarbody->colony;
    }

    Colony* colony = selected_colony;

    if (!colony){
        ImGui::Text("No Colony Selected");
        ImGui::End();
        return;
    }

    ImGui::Text("Colony - %s",colony->name.c_str());

    ImGui::Text(" Credits    : %i",colony->credits);
    ImGui::Text(" Population : %i",colony->population.amount);

    ResourceSlot* foodslot = FindResourceInSlots(colony->resource_slots,RESOURCE_FOOD);
    if (foodslot){
        ImGui::Text(" Food: %i",foodslot->amount);
    }else{
        ImGui::Text(" No food!");
    }


    for (Structure& structure:colony->structures){
        ImGui::Text(" Structure: %s",structure.name.c_str());
        for (ResourceSlot& slot:structure.productionrate_slots){
            ImGui::Text("  Production: %s at rate of %i",ResourceNameByType(slot.resource.type),slot.amount);
        }
    }

    if (ImGui::CollapsingHeader("Offered Contracts",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Spacing();
        if (ImGui::BeginTable("Contracts", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_Borders)){
            int index = 0;
            for (Contract& contract:colony->contracts){
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("Contract %i", index);
                //ImGui::Selectable(label, &selected[i], ImGuiSelectableFlags_SpanAllColumns);
                ImGui::TableNextColumn();

                std::string name = contract.buy ? "Buy ":"Sell ";
                name += ResourceNameByType(contract.resource_slot.resource.type);
                ImGui::Text(name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%i",contract.resource_slot.amount);
                ImGui::TableNextColumn();
                if (!contract.fulfilled){
                    if (ImGui::SmallButton("Fulfill")){
                        contract.fulfilled = true;
                        AddResourceToSlots(colony->resource_slots,contract.resource_slot);
                        int price = contract.resource_slot.amount * contract.markup * ResourceBasePriceByType(contract.resource_slot.resource.type);
                        debug->Info("Fulfilled contract for total price: %i\n",price);
                        colony->credits -= price;
                    }
                }
                index++;
            }
            ImGui::EndTable();
        }
    }

    if (ImGui::Button("Add Food to Colony")){
        foodslot->IncrementResource(20);
    };


    ImGui::End();
}

void ApplicationSim::UpdateUI(){
    RenderRandTestWindow();
    RenderNoiseTestWindow();
    RenderPopulationOverview();
    RenderGenericObjectUI();

    ImGui::ShowDemoWindow();
}