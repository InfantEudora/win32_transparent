#include "ApplicationGrid.h"
#include "Debug.h"
#include "OBJLoader.h"
#include "Light.h"
#include "CubeMap.h"

#define INPUT_H INPUT_LAST+1


static Debugger *debug = new Debugger("ApplicationGrid", DEBUG_ALL);

ApplicationGrid::ApplicationGrid():Application(){
    debug->Info("Created new application.\n");
};


Scene* ApplicationGrid::CreateTestScene(){
    test_scene = new Scene();
    Scene* scene = test_scene;
    scene->name = "Grid Test Scene";

    //Create a renderer for this scene...
    scene->renderer = new Renderer(main_window->width,main_window->height);
    //scene->renderer->Init();

    //scene->renderer = renderer;
    scene->inputcontroller = main_window->inputcontroller;
    scene->shader = default_shader;

    scene->camera = new Camera();
    scene->camera->name = "TestScene Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Make a sun
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "TestScene Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    std::vector<Material>loaded_materials;
    //Build assets
    Object* temp = new Object();
    temp->SetMesh(OBJLoader::ParseOBJFile("data/tile_001.obj",&loaded_materials));
    assetmanager->AddNewAsset("tile_001",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/tile_002.obj",&loaded_materials));
    assetmanager->AddNewAsset("tile_002",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/border_rock.obj",&loaded_materials));
    assetmanager->AddNewAsset("border_rock",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/editor_camera.obj",&loaded_materials));
    assetmanager->AddNewAsset("editor_camera",temp);
    temp->SetMesh(OBJLoader::ParseOBJFile("data/tile_gate.obj",&loaded_materials));
    assetmanager->AddNewAsset("tile_gate",temp);
    scene->renderer->AddMaterials(loaded_materials);
    delete temp;

    //Test arrows to test all this quaternion madness.
    Object* arrows = new Object();
    loaded_materials.clear();
    arrows->SetMesh(OBJLoader::ParseOBJFile("data/arrows.obj",&loaded_materials));
    scene->renderer->AddMaterials(loaded_materials);
    arrows->PickMaterials(loaded_materials,scene->renderer->materials);

    arrows->name = "Axis Arrows";
    arrows->SetPosition(vec3(-2,0,0));
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
    gate = assetmanager->GetObjectFromAsset("tile_gate");
    gate->name  = "Gate Tile";
    gate->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(gate);

    //Manually create and add materials
    Material mat = {};
    mat.glsl_material.color = vec4(1,1,1,1);
    mat.glsl_material.diffuse_texture = 0;
    mat.name = "Custom Loaded Textured Material";
    Texture* tex = scene->renderer->LoadTexture("data/textures/test_texture_4096.png");
    mat.glsl_material.handle_diffuse = tex->texture_handle;
    mat.diff_texture = tex;
    int matindex = scene->renderer->AddMaterial(mat);

    return test_scene;
}

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
    app->renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");

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

    //Add input to input controller
    scene->inputcontroller->AddKeyMap('H',INPUT_H);

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

    //Currently not testing, but should be working.
    app->test_scene = app->CreateTestScene();

    std::vector<Material>loaded_materials;

    //Load from a GLTF file and build assets.
    app->gltfloader.LoadGLTFFile("data/trees.glb");
    for (std::string& node_name:app->gltfloader.node_names){
        if (node_name.compare("bone_mesh") != 0){
            continue;
        }
        loaded_materials.clear();
        Mesh* gltfmesh = app->gltfloader.GetMeshFromNode(node_name.c_str(),&loaded_materials);
        if (!gltfmesh){
            continue;
        }
        Object* gltf_object = new Object();
        gltf_object->name = "GLTF Object " + node_name;
        gltf_object->SetMesh(gltfmesh);
        scene->renderer->AddMaterials(loaded_materials);
        gltf_object->TakeMaterialNames(loaded_materials);
        gltf_object->PickMaterials(loaded_materials,scene->renderer->materials);
        scene->AddObject(gltf_object);
        app->assetmanager->AddNewAsset(node_name.c_str(),gltf_object);
    }

    //We load a skeleton from the same file
    //Need asset manager to load mesh for bone debugging
    Material bone_mat;
    bone_mat.name = "bone_mat";
    bone_mat.glsl_material.color = vec4(1,1,1,1);
    scene->renderer->AddMaterial(bone_mat);

    Skeleton* skeleton = app->gltfloader.GetSkeleton("character_armature",app->assetmanager);
    //Move the root bone back so we can view the skinned mesh
    //skeleton->GetChild(0)->SetPosition(vec3(0,0,-1));
    loaded_materials.clear();
    SkinnedMesh* skinned_mesh = app->gltfloader.GetSkinnedMeshFromNode("character",&loaded_materials);
    scene->renderer->AddMaterials(loaded_materials);
    skeleton->SetSkinnedMesh(skinned_mesh);
    scene->AddObject(skeleton);

    {
        loaded_materials.clear();
        Mesh* gltfmesh = app->gltfloader.GetMeshFromNode("target_vis",&loaded_materials);
        if (gltfmesh){
            Object* gltf_object = new Object();
            gltf_object->name = "target_vis";
            gltf_object->SetMesh(gltfmesh);
            scene->renderer->AddMaterials(loaded_materials);
            gltf_object->TakeMaterialNames(loaded_materials);
            gltf_object->PickMaterials(loaded_materials,scene->renderer->materials);
            scene->AddObject(gltf_object);
            app->assetmanager->AddNewAsset("target_vis",gltf_object);
        }
    }

    //We now generate a terrain, and load that in.
    app->terrain = new IsoTerrain();
    app->terrain->name = "Iso Terrain";
    app->terrain->assetmanager = app->assetmanager;
    app->terrain->CreateTerrain(5,5,2);
    //scene->AddObject(app->terrain);

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
    app->selection_tile->Hide();

    app->main_scene->UpdatePhysics();

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



    //And update the map for terrain types
    IsoCell::terrain_material_map[CELL_TERRAIN_NONE] = -1;
    IsoCell::terrain_material_map[CELL_TERRAIN_GRASS] = app->renderer->FindMaterialIndex("grass");
    IsoCell::terrain_material_map[CELL_TERRAIN_ROCK] = app->renderer->FindMaterialIndex("stone_surface_001");

    BinaryAsset::DumpBinaryAssets();
    app->assetmanager->ListAssets();

    app->grid_settings.f_place = false;
    app->grid_settings.f_place_prop = false;
    app->grid_settings.f_delete = false;

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
    main_window = Window::CreateNewWindow(1680,960,&Window::wcs.at(0));
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

    CheckObjectSelection();

    //Camera rotation moving
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        f_show_rightclick_menu = false;
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
        projection_plane.pos.y += 0.1;
    }
    if (input->WasKeyReleased(INPUT_TURN_DOWN)){
        grid_settings.grid_level--;
        projection_plane.pos.y -= 0.1;
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

    //Grid stuffies
    int2 px = main_scene->inputcontroller->GetRelativeMousePosition();
    ray r = main_scene->camera->GetPixelRay(px);
    vec3 at = {};
    bool intersect = r.intersects_plane(projection_plane,at);

    IsoCell* hovered_cell = dynamic_cast<IsoCell*>(hovered_object);

    static float mouse_delta_sum = 0;
    if (mouse_delta_sum != 0){
        camera->MoveForwardBy(mouse_delta_sum / 10.0f);
        mouse_delta_sum /= 1.1;
    }

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }

    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);

    if (right_clicked){
        f_show_rightclick_menu = true;
        rightclick_menu_coord = px;
        rightclick_menu_normal = main_scene->inputcontroller->GetHoveredNormal();
        rightclick_menu_object = hovered_object;
    }
    if (left_clicked){
        f_show_rightclick_menu = false;
    }

    //Selection tile on side of hovered cell by normal
    if (hovered_cell && selection_tile){
        //Check if the hovered object is a cell and select a different cell side based on normal.
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

        vec3 p = hovered_cell->GetPosition();
        p += dir;

        if (grid_settings.f_selection){
            selection_tile->SetPosition(p);
            selection_tile->Show();
        }else{
            selection_tile->SetPosition(p);
            selection_tile->Hide();
        }
    }

    if (selection_tile){
        vec3 p = selection_tile->GetPosition();
        if (left_clicked){
            if (grid_settings.f_place){
                IsoCell* terrain_cell = terrain->FindCellByWorldPosition(p);
                if (terrain_cell){
                    terrain_cell->SetVisibility(true);
                }else{
                    debug->Warn("Unable to spawn cell there\n");
                }
            }else if (grid_settings.f_place_prop){
                if (hovered_cell == NULL){
                    debug->Info("Not a terrain cell\n");
                }

                if (hovered_cell && hovered_cell->PlaceWall(DIRECTION_NORTH)){
                    debug->Info("Placed a new wall there\n");
                }else{
                    debug->Warn("Unable to place wall there\n");
                }
            }
        }

        if (grid_settings.f_delete && right_clicked){
            if (hovered_cell){
                debug_physics->Info("That was a Cell we hovered.\n");
                hovered_cell->Hide();
            }
        }
    }

    //Modify active object
    if (selected_object){
        if (input->WasKeyReleased(INPUT_H)){
            selected_object->Hide();
        }
    }

    //Selection tile on a plane
    if (0 && intersect && selection_tile){
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

    if (f_track_cursor){
        const std::string target_name = "character_armature";
        Skeleton* skeleton = FindSkeletonInScene(main_scene,target_name);
        if (!skeleton){
            return;
        }

        Bone* bone_hips = skeleton->FindBone("Hips");
        if (bone_hips){
            quat q = bone_hips->GetRotation();
            bone_hips->SetWorldLookat(at,vec3(0,1,0));
            quat qn = bone_hips->GetRotation();
            quat r = q.slerp(q,qn,0.005,true);
            bone_hips->SetRotation(r);
        }

        Bone* bone_abdomen = skeleton->FindBone("Abdomen");
        if (bone_abdomen){
            quat q = bone_abdomen->GetRotation();
            bone_abdomen->SetWorldLookat(at,vec3(0,1,0));
            quat qn = bone_abdomen->GetRotation();
            quat r = q.slerp(q,qn,0.02,true);
            bone_abdomen->SetRotation(r);
        }

        Bone* bone_torso = skeleton->FindBone("Torso");
        if (bone_torso){
            quat q = bone_torso->GetRotation();
            bone_torso->SetWorldLookat(at,vec3(0,1,0));
            quat qn = bone_torso->GetRotation();
            quat r = q.slerp(q,qn,0.02,true);
            bone_torso->SetRotation(r);
        }

        Bone* bone_head = skeleton->FindBone("Head");
        if (bone_head){
            quat q = bone_head->GetRotation();
            quat wr = bone_head->GetWorldRotation();
            //vec3 head_up = wr * bone_head->ref_up;

            bone_head->SetWorldLookat(at,vec3(0,1,0));
            quat qn = bone_head->GetRotation();
            quat r = q.slerp(q,qn,0.03,true);
            bone_head->SetRotation(r);
        }

        Object* target = main_scene->FindObject("target_vis");
        if (target){
            vec3 p = target->GetPosition();
            p = p.lerp(at,0.05);
            target->SetPosition(p);
        }
    }
}

//TODO: GetWorldUp, using GetWorldRotation

void ApplicationGrid::RenderRightClickMenu_IsoCell(IsoCell* hovered_cell){
    ImVec2 window_pos, window_pos_pivot;
    window_pos_pivot.x = 0.0f;
    window_pos_pivot.y = 0.0f;
    int2 p  = rightclick_menu_coord; //Pixel position of the menu
    window_pos.x = p.x;
    window_pos.y = p.y;
    ImGui::SetNextWindowBgAlpha(0.65f); // Transparent background
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("Interaction Menu", NULL, window_flags)){
        //TODO: All the making of objects should be done through some thread safe message thing.
        if (ImGui::Button("Place North Wall")){
            hovered_cell->PlaceWall(DIRECTION_NORTH);
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Place East Wall")){
            hovered_cell->PlaceWall(DIRECTION_EAST);
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Place South Wall")){
            hovered_cell->PlaceWall(DIRECTION_SOUTH);
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Place West Wall")){
            hovered_cell->PlaceWall(DIRECTION_WEST);
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Place Tree")){
            hovered_cell->PlaceTree();
            f_show_rightclick_menu = false;
        }
    }
    ImGui::End();
}

void ApplicationGrid::RenderRightClickMenu_IsoWall(IsoWall* hovered_wall){
    ImVec2 window_pos, window_pos_pivot;
    window_pos_pivot.x = 0.0f;
    window_pos_pivot.y = 0.0f;
    int2 p  = rightclick_menu_coord; //Pixel position of the menu
    window_pos.x = p.x;
    window_pos.y = p.y;
    ImGui::SetNextWindowBgAlpha(0.65f); // Transparent background
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    if (ImGui::Begin("Interaction Menu", NULL, window_flags)){
        ImGui::Text("Imma Wall Biatch!");
        if (ImGui::Button("Lower")){
            hovered_wall->Lower();
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Place Stairs")){
            int normal_dir = IsoDirection::NormalToDirection(rightclick_menu_normal);
            debug->Info("Would place stairs in direcion %i from normal %.2f,%.2f,%.2f\n",normal_dir,rightclick_menu_normal.x,rightclick_menu_normal.y,rightclick_menu_normal.z);
            hovered_wall->PlaceStairs(normal_dir);
            f_show_rightclick_menu = false;
        }
    }
    ImGui::End();
}

void ApplicationGrid::RenderRightClickMenu(){
    IsoCell* hovered_cell = dynamic_cast<IsoCell*>(rightclick_menu_object);
    if (hovered_cell){
        return RenderRightClickMenu_IsoCell(hovered_cell);
    }

    IsoWall* hovered_wall = dynamic_cast<IsoWall*>(rightclick_menu_object);
    if (hovered_wall){
        return RenderRightClickMenu_IsoWall(hovered_wall);
    }
}

Skeleton* FindSkeletonInScene(Scene* scene, const std::string& name){
    if (!scene && !scene->renderer){
        return NULL;
    }
    Skeleton* skeleton = NULL;
    for (Object* object:scene->renderer->objects){
        skeleton = dynamic_cast<Skeleton*>(object);
        if (skeleton && (skeleton->name.compare(name) == 0)){
            return skeleton;
        }
    }
    return NULL;
}

void ApplicationGrid::RenderBoneModifierHeader(Bone* bone, int id){
    if (!bone){
        return;
    }

    if (ImGui::CollapsingHeader(bone->name.c_str())){
        bool apply_rotation = false;
        quat q = bone->GetRotation();
        static vec3 axis_degrees = {0,0,0};

        std::string title;
        //std::string title = "Get Current Angles ##" + std::to_string(id);
        //if (ImGui::Button(title.c_str())){
            axis_degrees.x = todegrees(q.get_pitch());
            axis_degrees.z = todegrees(q.get_roll());
            axis_degrees.y = todegrees(q.get_yaw());
        //}

        title = "Axis Degrees ##" + std::to_string(id);
        if (ImGui::DragFloat3(title.c_str(), (float*)&axis_degrees, 1.0f, -180.0f, 180.0f)){
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
        if (apply_rotation){
            bone->SetRotation(q);
        }

        float roll_by = 0;
        if (ImGui::DragFloat("Roll By", (float*)&roll_by, 0.01f, -1.0f, 1.0f)){
            bone->RollBy(roll_by);
        }
        float pitch_by = 0;
        if (ImGui::DragFloat("Pitch By", (float*)&pitch_by, 0.01f, -1.0f, 1.0f)){
            bone->PitchBy(pitch_by);
        }
        float yaw_by = 0;
        if (ImGui::DragFloat("Yaw By", (float*)&yaw_by, 0.01f, -1.0f, 1.0f)){
            bone->YawBy(yaw_by);
        }

        float forward_by = 0;
        if (ImGui::DragFloat("Forward By", (float*)&forward_by, 0.01f, -1.0f, 1.0f)){
            bone->MoveUpBy(forward_by);
        }
    }
}

void ApplicationGrid::RenderSkeletonUI(){
    ImGui::Begin("Skeleton UI");
    const std::string target_name = "character_armature";

    Skeleton* skeleton = FindSkeletonInScene(main_scene,target_name);
    if (!skeleton){
        ImGui::Text("Unable to find skeleton %s\n",target_name.c_str());
    }

    Object* bones = skeleton->GetChild(0);

    bool obj_visible = bones->IsVisible();
    if (ImGui::Checkbox("Show Skeleton Bones",&obj_visible)){
        bones->SetVisibility(obj_visible);
    }


    if (ImGui::Checkbox("Track Cursor",&f_track_cursor)){

    }

    vec3 at = {};
    plane& p = projection_plane;
    int2 px = main_scene->inputcontroller->GetRelativeMousePosition();
    ray r = main_scene->camera->GetPixelRay(px);
    bool intersect = r.intersects_plane(p,at);

    if (f_track_cursor){
        ImGui::BeginDisabled();
        ImGui::DragInt2("Mouse Position", (int*)&px, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Ray Origin", (float*)&r.origin, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Ray Direction", (float*)&r.direction, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
        ImGui::Separator();


        ImGui::DragFloat3("Plane Origin", (float*)&p.pos, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat3("Plane Normal", (float*)&p.normal, 0.01f, -1.0f, 1.0f);

        if (intersect){
            ImGui::DragFloat3("Intersection at", (float*)&at, 0.01f, -1.0f, 1.0f);
        }else{
            ImGui::Text("No intersection");
        }
    }

    Bone* bone_head = skeleton->FindBone("Head");
    RenderBoneModifierHeader(bone_head, 0);

    Bone* bone_neck = skeleton->FindBone("Neck");
    RenderBoneModifierHeader(bone_neck, 1);

    Bone* bone_foot_r = skeleton->FindBone("Foot.R");
    RenderBoneModifierHeader(bone_foot_r, 1);

    ImGui::End();



}

void ApplicationGrid::UpdateUI(){
    Object* object = main_scene->camera;

    IsoCell* selected_cell = dynamic_cast<IsoCell*>(selected_object);
    //UI for GridCells
    ImGui::Begin("Grid UI");
    ImGui::Text("ImGui.WantCaptureMouse   : %s",ImGui::GetIO().WantCaptureMouse ? "True" : "False");

    if (ImGui::CollapsingHeader("Grid Settings")){
        ImGui::Checkbox("Place New Tiles (Left Click)",&grid_settings.f_place);
        ImGui::DragInt("Tile Number",&grid_settings.tile_number,1,1,5);
        ImGui::Checkbox("Delete Tiles (Right Click)",&grid_settings.f_delete);
        ImGui::Checkbox("Show Selection Tile",&grid_settings.f_selection);
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

    if (!selected_cell){
        ImGui::Text("No Object of type Cell is selected.");
    }else{
        ImGui::Text("Cell Coordinate   : %i x %i",selected_cell->coordinate.x,selected_cell->coordinate.y);
        ImGui::Text("Terrain Type : %i",selected_cell->terrain_type);
        if (ImGui::Button("Set None")){
            selected_cell->SetTerrainType(CELL_TERRAIN_NONE);
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Empty")){
            selected_cell->SetTerrainType(CELL_TERRAIN_EMPTY);
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Grass")){
            selected_cell->SetTerrainType(CELL_TERRAIN_GRASS);
        }
        ImGui::SameLine();
        if (ImGui::Button("Set Rock")){
            selected_cell->SetTerrainType(CELL_TERRAIN_ROCK);
        }
    }
    ImGui::End();

    RenderGenericObjectUI();

    if (f_show_rightclick_menu){
        RenderRightClickMenu();
    }

    RenderSkeletonUI();
}