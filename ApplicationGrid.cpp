#include "ApplicationGrid.h"
#include "Debug.h"
#include "OBJLoader.h"
#include "Light.h"
#include "CubeMap.h"

#define INPUT_H     INPUT_LAST+1
#define INPUT_E     INPUT_LAST+2
#define INPUT_FOCUS INPUT_LAST+3

static Debugger *debug = new Debugger("ApplicationGrid", DEBUG_ALL);

ApplicationGrid::ApplicationGrid():Application(){
    debug->Info("Created new application.\n");
};

Scene* ApplicationGrid::CreateEmptyScene(){
    Scene* scene = CreateNewScene("Empty Test Scene");

    //Setup light and camera
    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Add input to input controller
    scene->inputcontroller->AddKeyMap('H',INPUT_H);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);

    //Make a sun
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    return scene;
}

Scene* ApplicationGrid::CreateHandTestScene(){
    Scene* scene = CreateNewScene("Hand Animation Test Scene");

    //Setup light and camera
    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Add input to input controller
    scene->inputcontroller->AddKeyMap('H',INPUT_H);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);
    scene->inputcontroller->AddKeyMap(VK_DECIMAL,INPUT_FOCUS);

    //Add phyics
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0,-9.81,0));

    //Make a sun
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    //Add a point light
    PointLight* lamp = new PointLight();
    lamp->name = "Point Light Red";
    lamp->SetPosition(vec3(0,1,00));
    lamp->color = vec3(1.0,0.1,0.0);
    lamp->brightness = 2.0;
    scene->AddObject(lamp);

    std::vector<Material>loaded_materials;

    //Load from a GLTF file and build assets.
    gltfloader.LoadGLTFFile("data/hand.glb");

    character = new IsoCharacter();
    Skeleton* skeleton = dynamic_cast<Skeleton*>(character);
    gltfloader.GetSkeleton("HandArmature",assetmanager,skeleton);
    if (skeleton){
        //Change name
        skeleton->name = "character_armature";
        character->root_bone_name = "Palm";

        Object* bones = skeleton->GetChild(0);
        if (bones){
            bones->SetVisibility(false);
        }

        loaded_materials.clear();
        Mesh* skinned_mesh = gltfloader.GetSkinnedMeshFromNode("Hand",&loaded_materials);

        scene->renderer->AddMaterials(loaded_materials);
        skeleton->SetMesh(skinned_mesh);
        skeleton->TakeMaterialNames(loaded_materials);
        skeleton->PickMaterials(loaded_materials,scene->renderer->materials);
        scene->AddObject(character);

        //gltfloader.LoadGLTFFile("data/animations.glb");

        std::vector<std::string>looping_animations;
        looping_animations.push_back("IdleStanding");
        looping_animations.push_back("CatwalkForward");
        looping_animations.push_back("Jumping");
        looping_animations.push_back("Stairs");

        for (std::string& name:looping_animations){
            selected_animation = gltfloader.LoadAnimation(name.c_str());
            if (selected_animation){
                character->AddAnimation(selected_animation);
                selected_animation->looped = true;
            }
        }

        //Add a collider
        character->AddPhysics(scene->physics_world);
        Physics* physics = character->GetPhysics();
        if (physics){
            physics->AddBoxCollider(vec3(0.25,0.25,0.25),vec3(0,0.25,0),quat().identity());
            physics->SetStatic(true);
        }
    }
    character->SetNextAnimation("IdleStanding");
    character->SetPosition(vec3(0,0,0));

    //Load another hand, that uses the same mesh... preferably.
    {
        IsoCharacter* character = new IsoCharacter();
        Skeleton* skeleton = dynamic_cast<Skeleton*>(character);
        gltfloader.GetSkeleton("HandArmature",assetmanager,skeleton);
        if (skeleton){
            //Change name
            skeleton->name = "Second Character Armature";
            character->root_bone_name = "Palm";

            Object* bones = skeleton->GetChild(0);
            if (bones){
                bones->SetVisibility(false);
            }

            loaded_materials.clear();
            Mesh* skinned_mesh = gltfloader.GetSkinnedMeshFromNode("Hand",&loaded_materials);

            scene->renderer->AddMaterials(loaded_materials);
            skeleton->SetMesh(skinned_mesh);
            skeleton->TakeMaterialNames(loaded_materials);
            skeleton->PickMaterials(loaded_materials,scene->renderer->materials);
            scene->AddObject(character);

            //gltfloader.LoadGLTFFile("data/animations.glb");

            std::vector<std::string>looping_animations;
            looping_animations.push_back("IdleStanding");
            looping_animations.push_back("CatwalkForward");

            for (std::string& name:looping_animations){
                selected_animation = gltfloader.LoadAnimation(name.c_str());
                if (selected_animation){
                    character->AddAnimation(selected_animation);
                    selected_animation->looped = true;
                }
            }
        }
        character->SetNextAnimation("IdleStanding");
         character->SetPosition(vec3(0,0,2));
    }


    //The scenery to make it look pretty
    Object* scenery = CreateNewObjectFromGLTF("Scenery",scene);
    scenery->MoveBy(vec3(-3,0,-3));

    //An object with a more simple animation, without skinning.
    Object* cog_object = CreateNewObjectFromGLTF("Cog",scene);
    cog_object->MoveBy(vec3(-3,0,-3));

    selected_animation = gltfloader.LoadAnimation("CogRotation");
    if (selected_animation){
        debug->Ok("Loaded Cog Animation from file.\n");
    }
    if (selected_animation && cog_object){
        cog_object->AddAnimation(selected_animation);
        selected_animation->looped = true;
        cog_object->SetNextAnimation("CogRotation");
    }

    // A character with Morphh targets and animations.
    Object* eyeplant_object = CreateNewObjectFromGLTF("EyePlant",scene);

    debug->SetLevel("GLTFLoader",DEBUG_TRACE);
    selected_animation = gltfloader.LoadAnimation("Blink");

    if (selected_animation){
        debug->Ok("Loaded Blink Animation from file.\n");
        if (eyeplant_object){
            eyeplant_object->AddAnimation(selected_animation);
        }
    }
    GetAllAssetsFromGLTF();

    selection_tile = assetmanager->GetObjectFromAsset("TileSelection");
    selection_tile->SetPickability(false);
    selection_tile->name = "Selection Tile";
    selection_tile->Hide();
    scene->AddObject(selection_tile);

    debug->SetLevel("GLTFLoader",DEBUG_INFO);
    return scene;
}


//This loads it, makes an asset from it... and sets up all the things.
Object* ApplicationGrid::CreateNewObjectFromGLTF(const std::string& nodename, Scene* target_scene){
    std::vector<Material>loaded_materials;
    loaded_materials.clear();
    Mesh* gltfmesh = gltfloader.GetMeshFromNode(nodename.c_str(),&loaded_materials);
    if (gltfmesh){
        Object* gltf_object = new Object();
        gltf_object->SetPosition(gltfloader.GetNodePosition(nodename.c_str()));
        gltf_object->SetRotation(gltfloader.GetNodeRotation(nodename.c_str()));
        gltf_object->name = nodename.c_str();
        gltf_object->SetMesh(gltfmesh);
        target_scene->renderer->AddMaterials(loaded_materials);
        gltf_object->TakeMaterialNames(loaded_materials);
        gltf_object->PickMaterials(loaded_materials,target_scene->renderer->materials);
        target_scene->AddObject(gltf_object);
        Asset* asset = assetmanager->AddNewAsset(nodename.c_str(),gltf_object);
        return gltf_object;
    }
    return NULL;
}

Scene* ApplicationGrid::CreateTestScene(){
    Scene* scene = CreateNewScene("Grid Test Scene");

    //Create a renderer for this scene...
    //scene->renderer = new Renderer(main_window->width,main_window->height);

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

    /* //We attempt to load a cubemap for the skybox.
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
    */

    //And update the map for terrain types
    IsoCell::terrain_material_map[CELL_TERRAIN_NONE] = -1;
    IsoCell::terrain_material_map[CELL_TERRAIN_GRASS] = renderer->FindMaterialIndex("grass");
    IsoCell::terrain_material_map[CELL_TERRAIN_ROCK] = renderer->FindMaterialIndex("stone_surface_001");

    return scene;
}

Scene* ApplicationGrid::CreateBoneTestScene(){
    Scene* scene = CreateNewScene("Bone Test Scene");

    //Setup light and camera
    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Add input to input controller
    scene->inputcontroller->AddKeyMap('H',INPUT_H);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);

    //Make a sun
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 5.0;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    //Create a renderer for this scene...
    //scene->renderer = new Renderer(main_window->width,main_window->height);
    //scene->renderer->Init();

    std::vector<Material>loaded_materials;

    //Load from a GLTF file and build assets.
    gltfloader.LoadGLTFFile("data/trees.glb");
    for (std::string& node_name:gltfloader.node_names){
        if (node_name.compare("bone_mesh") != 0){
            continue;
        }
        loaded_materials.clear();
        Mesh* gltfmesh = gltfloader.GetMeshFromNode(node_name.c_str(),&loaded_materials);
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
        assetmanager->AddNewAsset(node_name.c_str(),gltf_object);
    }

    {
        loaded_materials.clear();
        Mesh* gltfmesh = gltfloader.GetMeshFromNode("target_vis",&loaded_materials);
        if (gltfmesh){
            Object* gltf_object = new Object();
            gltf_object->name = "target_vis";
            gltf_object->SetMesh(gltfmesh);
            scene->renderer->AddMaterials(loaded_materials);
            gltf_object->TakeMaterialNames(loaded_materials);
            gltf_object->PickMaterials(loaded_materials,scene->renderer->materials);
            scene->AddObject(gltf_object);
            assetmanager->AddNewAsset("target_vis",gltf_object);
        }
    }

    {
        loaded_materials.clear();
        Mesh* gltfmesh = gltfloader.GetMeshFromNode("floor",&loaded_materials);
        if (gltfmesh){
            Object* gltf_object = new Object();
            gltf_object->name = "floor";
            gltf_object->SetMesh(gltfmesh);
            scene->renderer->AddMaterials(loaded_materials);
            gltf_object->TakeMaterialNames(loaded_materials);
            gltf_object->PickMaterials(loaded_materials,scene->renderer->materials);
            scene->AddObject(gltf_object);
            assetmanager->AddNewAsset("floor",gltf_object);
        }
    }

    //We load a skeleton from the same file
    //Need asset manager to load mesh for bone debugging
    Material bone_mat;
    bone_mat.name = "bone_mat";
    bone_mat.glsl_material.color = vec4(1,1,1,1);
    scene->renderer->AddMaterial(bone_mat);

    character = new IsoCharacter();
    character->root_bone_name = "Hips";
    Skeleton* skeleton = dynamic_cast<Skeleton*>(character);
    gltfloader.GetSkeleton("character_armature",assetmanager,skeleton);


    if (skeleton){
        Object* bones = skeleton->GetChild(0);
        if (bones){
            bones->SetVisibility(false);
        }

        loaded_materials.clear();
        Mesh* skinned_mesh = gltfloader.GetSkinnedMeshFromNode("character",&loaded_materials);

        scene->renderer->AddMaterials(loaded_materials);
        skeleton->SetMesh(skinned_mesh);
        skeleton->PickMaterials(loaded_materials,scene->renderer->materials);
        scene->AddObject(character);

        gltfloader.LoadGLTFFile("data/animations.glb");

        std::vector<std::string>looping_animations;
        looping_animations.push_back("Idle");
        looping_animations.push_back("IdleBored");
        looping_animations.push_back("Walking");
        looping_animations.push_back("CatwalkForward");
        std::vector<std::string>non_looping_animations;
        non_looping_animations.push_back("Swoop");
        non_looping_animations.push_back("ToHanging");
        non_looping_animations.push_back("TurnLeft");
        non_looping_animations.push_back("TurnRight");

        for (std::string& name:looping_animations){
            selected_animation = gltfloader.LoadAnimation(name.c_str());
            if (selected_animation){
                character->AddAnimation(selected_animation);
                selected_animation->looped = true;
            }
        }
        for (std::string& name:non_looping_animations){
            selected_animation = gltfloader.LoadAnimation(name.c_str());
            if (selected_animation){
                character->AddAnimation(selected_animation);
                selected_animation->looped = false;
            }
        }
    }

    character->SetNextAnimation("IdleBored");



    //Load Elf Mesh
    gltfloader.LoadGLTFFile("data/elf.glb");
    {
        loaded_materials.clear();
        gltfloader.ListNodes();

        Mesh* gltfmesh = gltfloader.GetMeshFromNode("Elf",&loaded_materials);
        if (gltfmesh){
            Object* gltf_object = new Object();
            gltf_object->name = "Elf";
            gltf_object->SetMesh(gltfmesh);
            scene->renderer->AddMaterials(loaded_materials);
            gltf_object->TakeMaterialNames(loaded_materials);
            gltf_object->PickMaterials(loaded_materials,scene->renderer->materials);
            scene->AddObject(gltf_object);
            assetmanager->AddNewAsset("Elf",gltf_object);
        }
    }

    //Load a bunch of palm trees
    gltfloader.LoadGLTFFile("data/palmtree.glb");
    {
        loaded_materials.clear();
        gltfloader.ListNodes();

        for (int index = 1;index<4;index++){
            std::string name = "Palm" + std::to_string(index);
            Mesh* gltfmesh = gltfloader.GetMeshFromNode(name.c_str(),&loaded_materials);
            if (gltfmesh){
                Object* gltf_object = new Object();
                gltf_object->name = name;
                gltf_object->SetMesh(gltfmesh);
                scene->renderer->AddMaterials(loaded_materials);
                gltf_object->TakeMaterialNames(loaded_materials);
                gltf_object->PickMaterials(loaded_materials,scene->renderer->materials);
                scene->AddObject(gltf_object);
                assetmanager->AddNewAsset(name.c_str(),gltf_object);
            }
        }
    }

    //We now generate a terrain, and load that in.
    terrain = new IsoTerrain();
    terrain->name = "Iso Terrain";
    terrain->assetmanager = assetmanager;
    terrain->CreateTerrain(NULL,5,5,2);
    //scene->AddObject(terrain);

    projection_plane.pos = {};
    projection_plane.normal = vec3(0,1,0);

    //Construct a selection tile that we will somehow turn into a transparent grid.
    loaded_materials.clear();
    selection_tile = new Object();
    selection_tile->SetMesh(OBJLoader::ParseOBJFile("data/selection_tile.obj",&loaded_materials));
    selection_tile->name = "Selection Tile";
    selection_tile->SetPosition(vec3(0,3,0));
    selection_tile->SetPickability(false);
    scene->renderer->AddMaterials(loaded_materials);
    selection_tile->PickMaterials(loaded_materials,scene->renderer->materials);
    scene->AddObject(selection_tile);
    selection_tile->Hide();

    return scene;
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
    app->renderer->Init(PIPELINE_DEFERRED);
    app->renderer->SetVSync(true);
    app->renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");

    //Renderer settings
    app->renderer->alpha_clip = 0.5f;
    app->renderer->f_render_skybox = false;

    app->default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    app->assetmanager = new AssetManager();

    //Comment in/out a test scene.
    //app->test_scene = app->CreateEmptyScene();
    //app->test_scene = app->CreateTestScene();
    //app->test_scene = app->CreateBoneTestScene();
    app->test_scene = app->CreateHandTestScene();

    app->main_scene = app->test_scene;

    app->main_scene->UpdatePhysics();

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
        PhysicsThreadFunction, // Thread start address in Application base class
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
        app->main_scene->DrawFrame(); // This renders state, not state_physics

        app->renderer->state_mutex.lock();
        app->UpdateUI(); //This right now modifies state_physics... but
        app->renderer->state_mutex.unlock();
        //RunLogic() at a completely different time interval also modifies state_physics.
        //When done, copies that over to prev_state.

        //In Renderer, that get's called
        app->main_window->ImGuiDrawFrame();

        //Copy to screen and finish
        app->main_window->DrawFrame();
    }

    debug->Info("FrameThreadFunction terminated\n");
    return 1;
}

void ApplicationGrid::Run(void){
    //Create a main window
    main_window = Window::CreateNewWindow(1920,960,&Window::wcs.at(0));
    if (!main_window){
        debug->Fatal("Unable to create window\n");
    }
    if (!main_window->Init()){
        debug->Fatal("Failed to init window\n");
    }

    main_window->Show(SW_SHOWDEFAULT);

    //Setup renderer
    //Renderer::SetVSync(true);

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

    //Only when in focus
    if (!main_window->f_has_focus){
        return;
    }

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

    if (grid_settings.f_camera_control){
        if (input->IsKeyDown(INPUT_MOVE_UP)){
            vec3 d = camera->MoveForwardBy(0.1f);
            camera_target += d;
        }
        if (input->IsKeyDown(INPUT_MOVE_DOWN)){
            vec3 d = camera->MoveForwardBy(-0.1f);
            camera_target += d;
        }
    }else{
        if (character){
            if (input->IsKeyDown(INPUT_MOVE_UP)){
                character->MoveForward();
            }
            if (input->IsKeyDown(INPUT_MOVE_DOWN)){
                character->MoveBackward();
            }
            if (input->IsKeyDown(INPUT_MOVE_RIGHT)){
                character->TurnRight();
            }
            if (input->IsKeyDown(INPUT_MOVE_LEFT)){
                character->TurnLeft();
            }
        }
    }


    if (input->WasKeyReleased(INPUT_TURN_UP)){
        grid_settings.grid_level++;
        projection_plane.pos.y += 0.1;
    }
    if (input->WasKeyReleased(INPUT_TURN_DOWN)){
        grid_settings.grid_level--;
        projection_plane.pos.y -= 0.1;
    }

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
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
        float height_factor = hovered_cell->terrain->height_factor;
        //Get the closest value of xyz
        if (hov_normal.x >.8){
            dir = vec3(1,0,0);
        }
        if (hov_normal.x <-0.8){
            dir = vec3(-1,0,0);
        }
        if (hov_normal.y >.8){
            dir = vec3(0,1*height_factor,0);
        }
        if (hov_normal.y <-0.8){
            dir = vec3(0,-1*height_factor,0);
        }
        if (hov_normal.z >.8){
            dir = vec3(0,0,1);
        }
        if (hov_normal.z <-0.8){
            dir = vec3(0,0,-1);
        }

        vec3 p = hovered_cell->GetPosition();
        p += dir ;

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
                    terrain_cell->Show();
                }else{
                    debug->Warn("Unable to spawn cell there\n");
                }
            }else if (grid_settings.f_place_prop){
                if (hovered_cell == NULL){
                    debug->Info("Not a terrain cell\n");
                }

                if (hovered_cell && hovered_cell->PlaceWall("Pallisade",DIRECTION_NORTH)){
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

    //Hide selected object
    if (selected_object){
        if (input->WasKeyReleased(INPUT_H)){
            selected_object->Hide();
        }
    }

    //Track leg to point
    if (input->WasKeyReleased(INPUT_E)){
        //A for loop so we can break
        for (;;){
            debug->Info("IKing\n");
            const std::string target_name = "character_armature";
            Skeleton* skeleton = FindSkeletonInScene(main_scene,target_name);
            if (!skeleton){
                break;
            }

            //Get the target Vis
            Object* target = main_scene->FindObject("target_vis");
            if (!target){
                break;
            }
            //We target the left foot
            Bone* foot_right = skeleton->FindBone("Foot.R");
            if (!foot_right){
                break;
            }

            foot_right->IKExtend(target->GetPosition(),2,1.0f);

            break;
        }
    }

    if (input->WasKeyReleased(INPUT_FOCUS)){
        //We center and track the camera onto the selected object
        if (selected_object){
            camera_target = selected_object->GetWorldPosition();
            vec3 up = up = vec3(0,1,0);
            camera->SetLookAt(camera_target,&up);
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


IsoRoom* ApplicationGrid::FindRoomByCell(IsoCell* cell){
    for (IsoRoom* room:rooms){
        if (room->IsCellInRoom(cell)){
            return room;
        }
    }
    return NULL;
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
        IsoRoom* room = FindRoomByCell(hovered_cell);
        if (room){
            ImGui::Text("Cell is in room : %s",room->name.c_str());
        }else{
            if (ImGui::Button("Create New Room")){
                CreateRoom(hovered_cell,1,1);
                f_show_rightclick_menu = false;
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Raise Terrain")){
            hovered_cell->RaiseTerrain();
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Water Terrain")){
            hovered_cell->SetTerrainType(CELL_TERRAIN_WATER);
            f_show_rightclick_menu = false;
        }

        if (ImGui::BeginMenu("Grass Corner")){
            int direction = MenuQueryDirection();
            if (direction != DIRECTION_NONE){
                hovered_cell->SetTerrainType(CELL_TERRAIN_GRASS);
                assetmanager->GetObjectFromAsset("Tile.1111.Grass.Corner",hovered_cell);
                f_show_rightclick_menu = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Place Stone Wall")){
            int direction = MenuQueryDirection();
            if (direction != DIRECTION_NONE){
                hovered_cell->PlaceWall("StoneWall",direction);
                f_show_rightclick_menu = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Place Pallisade Wall")){
            int direction = MenuQueryDirection();
            if (direction != DIRECTION_NONE){
                hovered_cell->PlaceWall("Pallisade",direction);
                f_show_rightclick_menu = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Place Stone Pillar")){
            int direction = MenuQueryDirection();
            if (direction != DIRECTION_NONE){
                hovered_cell->PlacePillar("PillarStone",direction);
                f_show_rightclick_menu = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::Button("Place Floor Flagstones")){
            hovered_cell->PlaceFloor("Flagstones");
            f_show_rightclick_menu = false;
        }

        if (ImGui::Button("Place Tree.001")){
            hovered_cell->PlaceTree("Tree.001");
            f_show_rightclick_menu = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Place Tree.002")){
            hovered_cell->PlaceTree("Tree.002");
            f_show_rightclick_menu = false;
        }

        if (ImGui::BeginMenu("Place Stairs")){
            int direction = MenuQueryDirection();
            if (direction != DIRECTION_NONE){
                hovered_cell->PlaceStairs("Stairs",direction);
                f_show_rightclick_menu = false;
            }
            ImGui::EndMenu();
        }
    }
    ImGui::End();
}

// Renders a menu with the 4 directions
int ApplicationGrid::MenuQueryDirection(){
    if (ImGui::MenuItem("North")){
        return DIRECTION_NORTH;
    }
    if (ImGui::MenuItem("East")){
        return DIRECTION_EAST;
    }
    if (ImGui::MenuItem("South")){
        return DIRECTION_SOUTH;
    }
    if (ImGui::MenuItem("West")){
        return DIRECTION_WEST;
    }
    return DIRECTION_NONE;
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
        if (ImGui::Button("Hide")){
            hovered_wall->Hide();
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Place Stairs")){
            int normal_dir = IsoDirection::NormalToDirection(rightclick_menu_normal);
            debug->Info("Would place stairs in direcion %i from normal %.2f,%.2f,%.2f\n",normal_dir,rightclick_menu_normal.x,rightclick_menu_normal.y,rightclick_menu_normal.z);
            //hovered_wall->PlaceStairs(normal_dir);
            f_show_rightclick_menu = false;
        }
        if (ImGui::Button("Door Me!")){
            hovered_wall->ChangeToDoor(assetmanager, "WallArch");
            f_show_rightclick_menu = false;
        }
    }
    ImGui::End();
}

void ApplicationGrid::RenderRightClickMenu(){
    IsoWall* hovered_wall = dynamic_cast<IsoWall*>(rightclick_menu_object);
    if (hovered_wall){
        return RenderRightClickMenu_IsoWall(hovered_wall);
    }

    IsoCell* clicked_cell = dynamic_cast<IsoCell*>(rightclick_menu_object);
    IsoStairs* clicked_stairs = dynamic_cast<IsoStairs*>(rightclick_menu_object);
    if (clicked_stairs){
        clicked_cell = dynamic_cast<IsoCell*>(clicked_stairs->parent);
    }
    //Still nothing?
    if (!clicked_cell && rightclick_menu_object && rightclick_menu_object->parent){
        clicked_cell = dynamic_cast<IsoCell*>(rightclick_menu_object->parent);
    }
    if (clicked_cell){
        return RenderRightClickMenu_IsoCell(clicked_cell);
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

        std::string parent_name = "NULL";
        std::string child_name = "NULL";
        if (bone->parent_bone){
            parent_name = bone->parent_bone->name;
        }
        if (bone->child_bone){
            child_name = bone->child_bone->name;
        }
        ImGui::Text("Parent Bone     : %s\n",parent_name.c_str());
        ImGui::Text("Child Bone [0]  : %s\n",child_name.c_str());


        axis_degrees.x = todegrees(q.get_pitch());
        axis_degrees.z = todegrees(q.get_roll());
        axis_degrees.y = todegrees(q.get_yaw());

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

        static float modify_parent = 0.0f;
        if (ImGui::DragFloat("Parent Depth = 1", (float*)&modify_parent, 0.01f, 0.0f, 1.0f)){

        }

        float roll_by = 0;
        if (ImGui::DragFloat("Roll By", (float*)&roll_by, 0.01f, -1.0f, 1.0f)){
            bone->RollBy(roll_by);
            if (bone->parent_bone && (modify_parent > 0.0f)){
                bone->parent_bone->RollBy(roll_by * modify_parent);
            }
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
        ImGui::End();
        return;
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

    std::vector<std::string>bone_list;
    bone_list.push_back("Head");
    bone_list.push_back("Neck");
    bone_list.push_back("Shoulders");

    bone_list.push_back("Torso");
    bone_list.push_back("Abdomen");
    bone_list.push_back("Hips");

    bone_list.push_back("Waist.R");
    bone_list.push_back("UpperLeg.R");
    bone_list.push_back("LowerLeg.R");
    bone_list.push_back("Foot.R");

    bone_list.push_back("Shoulders");
    bone_list.push_back("UpperArm.R");
    bone_list.push_back("LowerArm.R");
    bone_list.push_back("Hand.R");


    int index = -1;
    for (std::string& name:bone_list){
        index++;
        Bone* bone = skeleton->FindBone(name);
        RenderBoneModifierHeader(bone, 0);
    }
    ImGui::End();
}

void ApplicationGrid::UpdateUI(){
    RenderGridUI();
    RenderDebugMenuBar();
    RenderGenericObjectUI();

    if (f_show_rightclick_menu){
        RenderRightClickMenu();
    }

    //RenderSkeletonUI();
    //RenderAnimationUI(); //TODO: Move/Moved to base Object class + Base Application
}

void ApplicationGrid::RenderGridUI(){
    Object* object = main_scene->camera;

    IsoCell* selected_cell = dynamic_cast<IsoCell*>(selected_object);
    //UI for GridCells
    ImGui::Begin("Grid UI");

    if (!terrain){
        ImGui::Text("No terrain.");
        if (ImGui::Button("Create New Terrain")){
            terrain = new IsoTerrain();
            terrain->name = "Iso Terrain";
            terrain->assetmanager = assetmanager;
            //Now, tell it what the base tile asset should look like?
            terrain->base_tile = "Tile.1111";
            terrain->wall_tile = "Pallisade";

            terrain->CreateTerrain(main_scene->physics_world, 9,9,2);
            main_scene->AddObject(terrain);

            //Only show the first layer
            for (IsoCell* cell:terrain->cells){
                if (cell->coordinate.z > 0){
                    cell->Hide();
                }else{
                    cell->Show();
                }
            }
        }
    }


    if (ImGui::CollapsingHeader("Grid Settings")){
        ImGui::Checkbox("Place New Tiles (Left Click)",&grid_settings.f_place);
        ImGui::DragInt("Tile Number",&grid_settings.tile_number,1,1,5);
        ImGui::Checkbox("Delete Tiles (Right Click)",&grid_settings.f_delete);
        ImGui::Checkbox("Show Selection Tile",&grid_settings.f_selection);
        ImGui::DragInt("Grid Level",&grid_settings.grid_level,1,0,5);
        ImGui::Checkbox("Arrows control camera",&grid_settings.f_camera_control);
    }

    IsoCell* hovered_cell = dynamic_cast<IsoCell*>(hovered_object);
    IsoStairs* hovered_stairs = dynamic_cast<IsoStairs*>(hovered_object);
    if (hovered_stairs){
        ImGui::Text("Hovered Stairs");
        hovered_cell = dynamic_cast<IsoCell*>(hovered_stairs->parent);
    }
    //Still nothing?
    if (!hovered_cell && hovered_object && hovered_object->parent){
        hovered_cell = dynamic_cast<IsoCell*>(hovered_object->parent);
    }

    if (!hovered_cell){
        ImGui::Text("No Hovered Cell");
    }else{
        ImGui::Text("Hovered Cell Coordinate   : %i x %i Height: %i",hovered_cell->coordinate.x,hovered_cell->coordinate.y,hovered_cell->coordinate.z);
    }

    if (!selected_cell){
        ImGui::Text("No Object of type Cell is selected.");
    }else{
        ImGui::Text("Selected Cell");
        ImGui::Separator();
        ImGui::Text("Cell Coordinate   : %i x %i Height: %i",selected_cell->coordinate.x,selected_cell->coordinate.y,selected_cell->coordinate.z);
        IsoRoom* room = FindRoomByCell(selected_cell);
        if (room){
            ImGui::Text("Cell is in room : %s",room->name.c_str());
        }

        if (!selected_cell->object_floor){
            ImGui::Text("No Floor Decoration");
        }else{
            ImGui::Text("Floor Decoration : %s",selected_cell->object_floor->name.c_str());
            if (ImGui::Button("Hide")){
                selected_cell->object_floor->Hide();
            }
        }
        if (ImGui::Button("Default Terrain")){
            selected_cell->SetTerrainType(CELL_TERRAIN_NONE);
        }
        if (ImGui::Button("Water")){
            selected_cell->SetTerrainType(CELL_TERRAIN_WATER);
        }
        if (ImGui::Button("Grass Terrain")){
            selected_cell->SetTerrainType(CELL_TERRAIN_GRASS);
        }
        ImGui::SameLine();
    }

    if (terrain){
        if (ImGui::Button("Show Layer 1")){
            for (IsoCell* cell:terrain->cells){
                if (cell->coordinate.z > 0){
                    cell->Hide();
                }else{
                    cell->Show();
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Show Layer 2")){
            for (IsoCell* cell:terrain->cells){
                if (cell->coordinate.z > 1){
                    cell->Hide();
                }else{
                    cell->Show();
                }
            }
        }
        ImGui::Separator();

        static int2 room_size = {1,1};
        static int3 room_center  {1,1,0};
        ImGui::SliderInt2("Room Size",(int*)&room_size,1,5);
        ImGui::SliderInt3("Room Center",(int*)&room_center,0,8);

        if (ImGui::Button("Reset Character")){
            character->SetPosition(vec3(0,0,0));
            character->SetRotation(quat().identity());
            character->ResetPhysics();
        }
        if (ImGui::Button("Generate Room")){
            debug->Info("Generating Room %i x %i\n",room_size.x,room_size.y);
            IsoCell* center_cell = terrain->GetCellByCoordinate(room_center);
            if (!center_cell){
                debug->Fatal("Unable to find center cell ???\n");
            }
            CreateRoom(center_cell,room_size.x,room_size.y);
        }
        if (ImGui::Button("Drop Ball")){
            debug->Info("Dropping le Ball\n");
            Object* ball = assetmanager->GetObjectFromAsset("Icosphere");
            Physics* physics = ball->AddPhysics(main_scene->physics_world);
            if (physics){
                physics->AddSphereCollider(0.1,vec3(),quat().identity());
                physics->SetGravityEnabled(true);
                physics->SetStatic(false);
            }

            ball->SetPosition(vec3(0,1,0));
            ball->SetScale(vec3(0.2));
            main_scene->AddObject(ball);

        }
    }
    ImGui::End();
}

void ApplicationGrid::CreateRoom(IsoCell* center_cell, int size_x, int size_y){
    //Make sure the center is not already in a room
    IsoRoom* room = FindRoomByCell(center_cell);
    if (room){
        debug->Err("Center cell is already in a room\n");
        return;
    }

    int3 room_center = center_cell->coordinate;

    room = new IsoRoom();
    room->name = "Generated Room " + std::to_string(rooms.size());
    rooms.push_back(room);


    //Now we add all cells that are around it, in a square that arent in a room to the room.
    int cx = room_center.x;
    int cy = room_center.y;


    int xmin = cx - floor(size_x / 2.0f) + (size_x+1) % 2;
    int xmax = cx + floor(size_x / 2.0f);
    int ymin = cy - floor(size_y / 2.0f) + (size_y+1) % 2;
    int ymax = cy + floor(size_y / 2.0f);

    debug->Ok("Room Generated\n");
    int3 coord = room_center;

    for (int y=ymin;y<=ymax;y++){
        for (int x=xmin;x<=xmax;x++){
            coord.x = x;
            coord.y = y;
            IsoCell* cell = terrain->GetCellByCoordinate(coord);
            if (cell){
                //Check that this is not in a room already
                if (FindRoomByCell(cell)){
                    debug->Info("Skipping cell that's already in a room\n");
                }else{
                    room->AddCell(cell);
                    cell->PlaceFloor("Flagstones");
                }
            }
        }
    }

    //Now that all the floor tiles are added, we can place walls.
    for (IsoCell* cell:room->cells){
        //Figure out if this cell has a neighbour in the same room for each edge:
        for (int direction = DIRECTION_NORTH;direction<=DIRECTION_WEST;direction++){
            IsoCell* n = cell->GetNeighbour(direction);
            if (n && room->IsCellInRoom(n)){
                //We don't place a wall.
            }else{
                IsoWall* wall = cell->PlaceWall("StoneWall",direction);
                if (wall){
                    cell->PlacePillar("PillarStone",direction);
                    cell->PlacePillar("PillarStone",(direction+1) % 4);
                }
            }
        }
    }
}

