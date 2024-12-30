#include "ApplicationGrid.h"
#include "Debug.h"
#include "OBJLoader.h"
#include "Light.h"
#include "CubeMap.h"

static Debugger *debug = new Debugger("ApplicationGrid", DEBUG_ALL);

ApplicationGrid::ApplicationGrid():Application(){
    debug->Info("Created new application.\n");
};

//Function for rendering the frame to a window
DWORD WINAPI ApplicationGrid::GridFrameThreadFunction(LPVOID lpParameter){
    ApplicationGrid* app = static_cast<ApplicationGrid*>(lpParameter);
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

    //Create a renderer for this window
    app->renderer = new Renderer(app->main_window->width,app->main_window->height);
    app->renderer->Init();

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    app->main_scene = new Scene();
    app->main_scene->name = "Grid Main Scene";
    app->main_scene->renderer = app->renderer;
    app->main_scene->inputcontroller = app->main_window->inputcontroller;
    app->main_scene->shader = app->default_shader;

    //Either we put things in the scene, or we make a scene extension class....
    Scene* scene = app->main_scene;
    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Make a sun
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    //We make an assetmanager which we use to load/build all assets from:
    app->assetmanager = new AssetManager();
    std::vector<Material>loaded_materials;

    Object* temp = new Object();
    temp->SetMesh(OBJLoader::ParseOBJFile("data/tile_001.obj",&loaded_materials));
    app->assetmanager->AddNewAsset("tile_001",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/tile_002.obj",&loaded_materials));
    app->assetmanager->AddNewAsset("tile_002",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/border_rock.obj",&loaded_materials));
    app->assetmanager->AddNewAsset("border_rock",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/editor_camera.obj",&loaded_materials));
    app->assetmanager->AddNewAsset("editor_camera",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/tile_gate.obj",&loaded_materials));
    app->assetmanager->AddNewAsset("tile_gate",temp);
    scene->renderer->AddMaterials(loaded_materials);
    delete temp;

    //We now generate a terrain, and load that in.
    app->terrain = new IsoTerrain();
    app->terrain->name = "Iso Terrain";
    app->terrain->assetmanager = app->assetmanager;
    app->terrain->CreateTerrain(63,63,3);
    scene->AddObject(app->terrain);

    //Test arrows to test all this quaternion madness.
    Object* arrows = new Object();
    loaded_materials.clear();
    arrows->SetMesh(OBJLoader::ParseOBJFile("data/arrows.obj",&loaded_materials));
    scene->renderer->AddMaterials(loaded_materials);
    arrows->PickMaterials(loaded_materials,scene->renderer->materials);

    arrows->name = "Axis Arrows";
    arrows->SetPosition(vec3(-2,0,0));
    app->selected_object = arrows;
    scene->AddObject(arrows);

    //A test thing with 4 new textures that should auto load and display:
    loaded_materials.clear();
    Object* testcube  = new Object();
    testcube->SetMesh(OBJLoader::ParseOBJFile("data/test_cube.obj",&loaded_materials));
    testcube->name = "Test Cube";
    testcube->SetPosition(vec3(0,0.5,0));
    scene->renderer->AddMaterials(loaded_materials);
    testcube->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(testcube);

    loaded_materials.clear();
    Object* tree  = new Object();
    tree->SetMesh(OBJLoader::ParseOBJFile("data/tree_001.obj",&loaded_materials));
    tree->name = "Tree 001";
    tree->SetPosition(vec3(0.5,0,0));
    scene->renderer->AddMaterials(loaded_materials);
    tree->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(tree);

    loaded_materials.clear();
    Object* wall  = new Object();
    wall->SetMesh(OBJLoader::ParseOBJFile("data/wall_segment.obj",&loaded_materials));
    wall->name = "Wall 001";
    wall->SetPosition(vec3(0.5,0,0));
    scene->renderer->AddMaterials(loaded_materials);
    wall->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(wall);

    Object* gate = new Object();
    gate = app->assetmanager->GetObjectFromAsset("tile_gate");
    gate->name  = "Gate Tile";
    gate->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(gate);

    app->projection_plane.pos = {};
    app->projection_plane.normal = vec3(0,1,0);

    //Construct a selection tile that we will somehow turn into a transparent grid.
    loaded_materials.clear();
    app->selection_tile = new Object();
    app->selection_tile->SetMesh(OBJLoader::ParseOBJFile("data/selection_tile.obj",&loaded_materials));
    app->selection_tile->name = "Selection Tile";
    app->selection_tile->SetPosition(vec3(0,3,0));
    app->selection_tile->SetPickability(false);
    scene->renderer->AddMaterials(loaded_materials);
    app->selection_tile->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(app->selection_tile);

    app->main_scene->UpdatePhysics();

    //Manually createa and add materials
    Material mat = {};
    mat.glsl_material.color = vec4(1,1,1,1);
    mat.glsl_material.diffuse_texture = 0;
    mat.name = "Custom Loaded Textured Material";
    Texture* tex = scene->renderer->LoadTexture("data/textures/test_texture_4096.png");
    mat.glsl_material.handle_diffuse = tex->texture_handle;
    mat.diff_texture = tex;
    int matindex = scene->renderer->AddMaterial(mat);

    //We attempt to load a cubemap for the skybox.
    CubeMap* skybox = new CubeMap();
    //skybox->LoadFromFile("data/textures/skybox/right.jpg",0);
    //skybox->LoadFromFile("data/textures/skybox/left.jpg",1);
    //skybox->LoadFromFile("data/textures/skybox/top.jpg",2);
    //skybox->LoadFromFile("data/textures/skybox/bottom.jpg",3);
    //skybox->LoadFromFile("data/textures/skybox/front.jpg",4);
    //skybox->LoadFromFile("data/textures/skybox/back.jpg",5);
    skybox->LoadFromFile("data/textures/skybox/px.jpg",0);
    skybox->LoadFromFile("data/textures/skybox/nx.jpg",1);
    skybox->LoadFromFile("data/textures/skybox/py.jpg",2);
    skybox->LoadFromFile("data/textures/skybox/ny.jpg",3);
    skybox->LoadFromFile("data/textures/skybox/pz.jpg",4);
    skybox->LoadFromFile("data/textures/skybox/nz.jpg",5);
    scene->renderer->UploadCubeMap(skybox);

    scene->renderer->skybox = skybox;
    scene->renderer->skybox_shader = new Shader("shaders/skybox.vert","shaders/skybox.frag");
    scene->renderer->skybox_mesh = OBJLoader::ParseOBJFile("data/unit_cube.obj");

    //Now we can assign materials to all the tiles.
    for (IsoCell* cell:app->terrain->cells){
        //cell->material_slot[0] = matindex;
    }

    //And update the map for terrain types
    IsoCell::terrain_material_map[CELL_TERRAIN_NONE] = -1;
    IsoCell::terrain_material_map[CELL_TERRAIN_GRASS] = app->renderer->FindMaterialIndex("grass");
    IsoCell::terrain_material_map[CELL_TERRAIN_ROCK] = app->renderer->FindMaterialIndex("stone_surface_001");

    BinaryAsset::DumpBinaryAssets();
    app->assetmanager->ListAssets();

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
        if (app->main_window->f_resized){
            app->main_window->f_resized = false;
            app->renderer->Resize(app->main_window->width,app->main_window->height);
        }

        //Tell ImGui to start a new frame
        app->main_window->ImGuiNewFrame();

        //This should render the frame only.
        app->main_scene->DrawFrame();

        app->UpdateUI();

        app->main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        app->main_window->DrawFrame();
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}

void ApplicationGrid::Run(void){
    //Create a main window
    main_window = Window::CreateNewWindow(1440,720,&Window::wcs.at(0));
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
        GridFrameThreadFunction, // Thread start address
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
            Sleep(1);
        }
    }
}

//Called before update physics
void ApplicationGrid::RunLogic(){
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    tmr_physics->Stop();
    tmr_physics->Restart();

    //Testing. TODO: Make a slider with more accurate intervals than sleep.
    Sleep(5);

    //Check if we selected a tile
    objectid_t hovered_objid = OBJECTID_INVALID;
    if (!ImGui::GetIO().WantCaptureMouse){
        hovered_objid = input->GetHoveredObjectID();
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

    if (input->IsKeyDown(INPUT_MOVE_UP)){
        vec3 d = camera->MoveForwardBy(0.1f);
        camera_target += d;
    }

    if (input->WasKeyReleased(INPUT_TURN_UP)){
        grid_settings.grid_level++;
        projection_plane.pos.y = grid_settings.grid_level;
    }
    if (input->WasKeyReleased(INPUT_TURN_DOWN)){
        grid_settings.grid_level--;
        projection_plane.pos.y = grid_settings.grid_level;
    }

    static float mouse_delta_sum = 0;
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);
    if (mouse_delta_sum != 0){
        camera->MoveForwardBy(mouse_delta_sum / 10.0f);
        mouse_delta_sum /= 1.1;
    }

    //Iterate over all the rendered objects
    bool clicked_empty = false;
    bool left_clicked = false;
    bool right_clicked = false;
    if (input->WasKeyReleased(INPUT_CLICK_LEFT)){
        clicked_empty = true;
        left_clicked = true;
    }
    if (input->WasKeyReleased(INPUT_CLICK_RIGHT)){
        right_clicked = true;
    }

    //When a terrain cell gets destroyed, we should remove it from renderer and terrain.
    //Either we use a shared pointer, or we keep track of the number of references.
    hovered_object = NULL;

    for (Object* object:renderer->renderable_objects){
        if (object->IsDestroyed()){
            //TODO: Remove it... here?
        }

        if (input->WasKeyReleased(INPUT_CLICK_LEFT) && (object->GetID() == hovered_objid)){
            vec3 p = object->GetPosition();
            debug->Info("Clicked on ID: %3i Object Pos: %.2f %.2f %.2f\n",hovered_objid,p.x,p.y,p.z);
            //object->material_slot[1] = 1;
            selected_object = object;
            clicked_empty = false;
        }else if (object->GetID() == hovered_objid){
            hovered_object = object;
        }
    }

    //Grid stuffies
    int2 px = main_scene->inputcontroller->GetRelativeMousePosition();
    ray r = main_scene->camera->GetPixelRay(px);

    vec3 at = {};
    bool intersect = r.intersects_plane(projection_plane,at);

    //Selection tile on side of hovered cell by normal
    if (!ImGui::GetIO().WantCaptureMouse && hovered_object && selection_tile){
        //Check if the hovered object is a cell and select a side based on normal.
        IsoCell* cell = dynamic_cast<IsoCell*>(hovered_object);
        if (cell){
            vec3 hov_normal = main_scene->inputcontroller->GetHoveredNormal();
            vec3 dir;
            //Get the closest value of xyz
            if (hov_normal.x >.8){
                dir = vec3(1,0,0);
            }
            if (hov_normal.x <-0.8){
                dir = vec3(-1,0,0);
            }
            if (hov_normal.y >.8){
                dir = vec3(0,1,0);
            }
            if (hov_normal.y <-0.8){
                dir = vec3(0,-1,0);
            }
            if (hov_normal.z >.8){
                dir = vec3(0,0,1);
            }
            if (hov_normal.z <-0.8){
                dir = vec3(0,0,-1);
            }

            vec3 p = cell->GetPosition();
            p += dir;
            selection_tile->SetPosition(p);
        }
    }

    if (!ImGui::GetIO().WantCaptureMouse && selection_tile){
        vec3 p = selection_tile->GetPosition();
        IsoCell* terraincell = terrain->FindCellByWorldPosition(p);

        if (grid_settings.f_place && left_clicked){
            if (terraincell){
                terraincell->SetVisibility(true);
            }else{
                debug->Warn("Unable to spawn cell there\n");
            }

        }
    }

    //Selection tile on a plane
    if (0 && !ImGui::GetIO().WantCaptureMouse && intersect && selection_tile){
        //We snap the selection tile to a grid.
        at.round();
        selection_tile->SetPosition(at);

        //Cell is the object the mouse is over, which can be different from our mouse grid coordinate
        IsoCell* cell = dynamic_cast<IsoCell*>(selected_object);
        IsoCell* terraincell = terrain->FindCellByWorldPosition(at);
        if (grid_settings.f_place && (clicked_empty || (cell && left_clicked))){
            //Request the cell at the current coordinate from the grid:

            if (terraincell){
                debug_physics->Info("Already IsoCell at %.1f x %.1f : terraincell %ix%i\n",at.x,at.z,terraincell->coordinate.x,terraincell->coordinate.y);
                terraincell->Show();
            }else{
                if (cell && (cell->GetPosition().x == at.x) && (cell->GetPosition().z == at.z)){
                    debug_physics->Info("Already IsoCell at %.1f x %.1f : cell %ix%i\n",at.x,at.z,cell->coordinate.x,cell->coordinate.y);
                    cell->Show();
                }else{
                    IsoCell* c = new IsoCell();
                    std::string name = "tile_00" + std::to_string(grid_settings.tile_number);
                    if (assetmanager->GetObjectFromAsset(name.c_str(),c)){
                        c->SetPosition(at);
                        c->name = "Floating IsoCell";
                        main_scene->AddObject(c);
                        debug_physics->Info("Spawned a new IsoCell at %.1f x %.1f\n",at.x,at.z);
                    }else{
                        debug_physics->Err("Unable to instantiate object %sat %.1f x %.1f\n",name.c_str(),at.x,at.z);
                    }
                }
            }
        }else if (grid_settings.f_delete && right_clicked){
            debug_physics->Info("Clickerdy clackerdy.\n");
            if (!hovered_object){
                selected_object = NULL;
            }else{
                IsoCell* hovered_cell = dynamic_cast<IsoCell*>(hovered_object);
                if (hovered_cell){
                    debug_physics->Info("That was a Cell we hovered.\n");
                    hovered_cell->Hide();
                }
            }
        }

    }
}

void ApplicationGrid::UpdateUISceneObjectTree(){
    if (ImGui::TreeNode("Scene Root")){
        for (Object* object:main_scene->renderer->objects){
            UpdateUISceneObjectTreeNode(object,NULL);
        }
        ImGui::TreePop();
    }
}


void ApplicationGrid::UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked){
    objectid_t id = object->GetID();
    if (ImGui::TreeNodeEx((void*)id,ImGuiTreeNodeFlags_Bullet | ImGuiTreeNodeFlags_Leaf, "Object #%i - %s",id,object->name.c_str())){
        ImGui::TreePop();
        if (ImGui::IsItemClicked()){
            selected_object = object;
            debug->Info("Selected %s\n",object->name.c_str());
        }
    }
}

void ApplicationGrid::UpdateUICameraControls(Camera* camera,int id){
    if (!camera){
        return;
    }

    std::string title = camera->name + "##" + std::to_string(id) +   " Camera Controls";

    if (ImGui::CollapsingHeader(title.c_str())){
        float znear = main_scene->camera->viewport.znear;
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
        if (ImGui::Button("Switch Camera")){
            main_scene->camera->Show();
            main_scene->camera = camera;
            main_scene->camera->Hide();
        }
    }
}

void ApplicationGrid::UpdateUI(){
    Object* object = main_scene->camera;

    IsoCell* cell = dynamic_cast<IsoCell*>(selected_object);
    //UI for GridCells
    ImGui::Begin("Grid UI");
    if (ImGui::CollapsingHeader("Grid Settings")){
        ImGui::Checkbox("Place New Tiles (Left Click)",&grid_settings.f_place);
        ImGui::DragInt("Tile Number",&grid_settings.tile_number,1,1,5);
        ImGui::Checkbox("Delete Tiles (Right Click)",&grid_settings.f_delete);
        ImGui::DragInt("Grid Level",&grid_settings.grid_level,1,0,5);
    }
    vec3 hov_normal = main_scene->inputcontroller->GetHoveredNormal();
    ImGui::Text("Normal at mouse   : %.3f, %.3f, %.3f",hov_normal.x,hov_normal.y,hov_normal.z);

    IsoCell* hovered_cell = dynamic_cast<IsoCell*>(hovered_object);
    if (!hovered_cell){
        ImGui::Text("No Hovered Cell");
    }else{
        ImGui::Text("Hovered Cell Coordinate   : %i x %i",hovered_cell->coordinate.x,hovered_cell->coordinate.y);
    }

    if (!cell){
        ImGui::Text("No Object of type Cell is selected.");
    }else{
        ImGui::Text("Cell Coordinate   : %i x %i",cell->coordinate.x,cell->coordinate.y);
        ImGui::Text("Terrain Type : %i",cell->terrain_type);
        if (ImGui::Button("Set None")){
            cell->SetTerrainType(CELL_TERRAIN_NONE);
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Empty")){
            cell->SetTerrainType(CELL_TERRAIN_EMPTY);
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Grass")){
            cell->SetTerrainType(CELL_TERRAIN_GRASS);
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Rock")){
            cell->SetTerrainType(CELL_TERRAIN_ROCK);
        }
    }
    ImGui::End();

    //For generic Objects and parameters
    ImGui::Begin("Generic Object UI");
    if (ImGui::CollapsingHeader("Scene")){
        Scene* scene = main_scene;
        ImGui::Text("Main Scene             : %s",scene->name.c_str());
        UpdateUISceneObjectTree();
        if (ImGui::Button("Add Camera")){
            Camera* camera = new Camera();
            camera->name = "New Camera";
            if (assetmanager->GetObjectFromAsset("editor_camera",camera)){
                camera->SetPosition(vec3(1,2,1));
                camera->material_slot[0] = 3;
                camera->SetLookAt(vec3());
                camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
                scene->AddObject(camera);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Add Directional Light")){
            DirectionalLight* l = new DirectionalLight();
            l->name = "Directional Light";
            scene->AddObject(l);
        }
        if (ImGui::Button("Add Point Light")){
            PointLight* l = new PointLight();
            l->name = "Point Light";
            scene->AddObject(l);
        }
    }

    //So the same camera panel has a different ImGUI ID.
    int ui_camid = 0;
    UpdateUICameraControls(main_scene->camera ,ui_camid);

    object = selected_object;
    if (!object){
        ImGui::Text("No Object Selected");
    }else{
        ImGui::Text("Selected Object: %s",object->name.c_str());
        bool obj_visible = object->IsVisible();
        if (ImGui::Checkbox("Visible",&obj_visible)){
            object->SetVisibility(obj_visible);
        }
    }
    if (object){
        Camera* cam = dynamic_cast<Camera*>(object);
        if (cam){
            ui_camid++;
            UpdateUICameraControls(cam,ui_camid);
        }

        Light* light = dynamic_cast<Light*>(object);
        if (light && ImGui::CollapsingHeader("Light Properties")){
            vec3 pos = object->GetPosition();
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Position", (float*)&pos, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
            ImGui::DragFloat3("Color", (float*)&light->color, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Brightness", (float*)&light->brightness, 0.01f, 0.0f, 10.0f);
        }


        if (object->GetMesh() && ImGui::CollapsingHeader("Mesh")){
            Mesh* mesh = object->GetMesh();
            ImGui::Text(" ID             : %lu",mesh->GetID());
            ImGui::Text(" num_vertices   : %lu",mesh->num_vertices);
            ImGui::Text(" num_materials  : %lu",mesh->num_materials);
            ImGui::Text(" num_references : %lu",mesh->num_references);
        }

        if (ImGui::CollapsingHeader("Position")){
            vec3 delta = {0,0,0};
            ImGui::DragFloat3("Move Position", (float*)&delta, 0.01f, -1.0f, 1.0f);
            object->MoveBy(delta);
            ImGui::BeginDisabled();
            vec3 pos = object->GetPosition();
            ImGui::DragFloat3("Position", (float*)&pos, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
        }
        if (ImGui::CollapsingHeader("Rotation")){
            static int option = 0;
            ImGui::Text("Input By:");
            ImGui::RadioButton("None", &option, 0); ImGui::SameLine();
            ImGui::RadioButton("Vector + Rotation", &option, 1); ImGui::SameLine();
            ImGui::RadioButton("Target, Position, Up", &option, 2);
            ImGui::RadioButton("Axis Degrees", &option, 3);
            ImGui::Separator();

            ImGui::BeginDisabled();
            quat q = object->GetRotation();
            ImGui::DragFloat4("Current Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();

            bool apply_rotation = false;


            if (option == 1){
                static vec3 quatinp = {0,0,0};
                ImGui::DragFloat3("Quat Input Vector", (float*)&quatinp, 0.01f, -1.0f, 1.0f);
                static float quatroll = 0.0f;
                ImGui::DragFloat("Quat Roll", (float*)&quatroll, 0.01f, -TYPE_PI, TYPE_PI);
                ImGui::BeginDisabled();
                vec3 quatn = quatinp;
                quatn.normalize();
                ImGui::DragFloat3("Quat Normalized Vector", (float*)&quatn, 0.01f, -1.0f, 1.0f);
                q = quat(quatn,quatroll);
                ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();

            }else if (option == 2){
                static vec3 target = {0,0,-1};
                if (ImGui::DragFloat3("Target Vector", (float*)&target, 0.01f, -5.0f, 5.0f)){
                    apply_rotation = true;
                }
                static vec3 position = {0,0,0};
                if (ImGui::DragFloat3("Position", (float*)&position, 0.01f, -5.0f, 5.0f)){
                    apply_rotation = true;
                }
                static vec3 worldup = {0,1,0};
                if (ImGui::DragFloat3("World Up", (float*)&worldup, 0.01f, -1.0f, 1.0f)){
                    apply_rotation = true;
                }
                ImGui::BeginDisabled();
                q = quat::getquat(target,position,worldup);
                ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
            }else if (option == 3){
                static vec3 axis_degrees = {0,0,0};
                if (ImGui::DragFloat3("Axis Degrees", (float*)&axis_degrees, 1.0f, -180.0f, 180.0f)){
                    apply_rotation = true;
                }
                ImGui::BeginDisabled();
                //Let's do them in order?
                quat q1; q1.set_rotation(vec3(1,0,0),toradians(axis_degrees.x));
                quat q2; q2.set_rotation(vec3(0,1,0),toradians(axis_degrees.y));
                quat q3; q3.set_rotation(vec3(0,0,1),toradians(axis_degrees.z));

                q = q1 * q2 * q3;
                ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
                ImGui::EndDisabled();
            }

            if (ImGui::Button("Apply Rotation")){
                apply_rotation = true;
            }
            if (apply_rotation){
                object->SetRotation(q);
            }
        }
        if (ImGui::CollapsingHeader("Scale")){
            vec3 scale = object->GetScale();
            if (ImGui::DragFloat3("Scale Vector", (float*)&scale, 0.01f, 0.01f, 10.0f)){
                object->SetScale(scale);
            }
        }
        if (ImGui::CollapsingHeader("Material")){
            ImGui::Text("Renderer Materials: %i",renderer->materials.size());
            ImGui::Separator();

            ImGui::DragInt("Material Slot 0",&object->material_slot[0],1,-1,10);
            ImGui::DragInt("Material Slot 1",&object->material_slot[1],1,-1,10);
            ImGui::DragInt("Material Slot 2",&object->material_slot[2],1,-1,10);
            ImGui::DragInt("Material Slot 3",&object->material_slot[3],1,-1,10);
        }


    }

    if (ImGui::CollapsingHeader("Performance")){
        ImGui::Text("Frame Rate   : %.2f FPS (%.2f ms)", 1000000.0f / renderer->tmr_frame->avg,renderer->tmr_frame->avg/1000.0f );
        ImGui::Text("Physics Rate : %.2f TPS (%.2f ms)", 1000000.0f / tmr_physics->avg,tmr_physics->avg/1000.0f );
    }

    if (ImGui::CollapsingHeader("Renderer")){
        ImGui::Text(    "Num Materials  : %i", renderer->GetNumMaterials());
        ImGui::Text(    "Normal Mapping :");ImGui::SameLine();
        ImGui::Checkbox("##1", &renderer->f_normal_mapping);
    }

    if (ImGui::CollapsingHeader("Window")){
        ImGui::Text(    "Current Size   : %i x %i", main_window->width,main_window->height);
    }

    if (ImGui::CollapsingHeader("Assets")){
        for (Asset* asset: assetmanager->assets){
            ImGui::Text("Asset  : %s", asset->name.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Materials")){
        for (Material& material: renderer->materials){
            ImGui::Text("Material  : %s", material.name.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Ray - Plane Intersection")){
        plane& p = projection_plane;

        int2 px = main_scene->inputcontroller->GetRelativeMousePosition();

        ray r = main_scene->camera->GetPixelRay(px);
        ImGui::BeginDisabled();
        ImGui::DragInt2("Mouse Position", (int*)&px, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Ray Origin", (float*)&r.origin, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Ray Direction", (float*)&r.direction, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
        ImGui::Separator();


        ImGui::DragFloat3("Plane Origin", (float*)&p.pos, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Plane Normal", (float*)&p.normal, 0.01f, -1.0f, 1.0f);

        vec3 at = {};
        bool intersect = r.intersects_plane(p,at);

        if (intersect){
            ImGui::DragFloat3("Intersection at", (float*)&at, 0.01f, -1.0f, 1.0f);
            //Move the object there?
            if (selected_object){
                selected_object->SetPosition(at);
            }
        }else{
            ImGui::Text("No intersection");
        }
        //
    }

    ImGui::End();
}