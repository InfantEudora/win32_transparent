#include "ApplicationDozer.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationDozer", DEBUG_ALL);

#define INPUT_H         INPUT_LAST+1
#define INPUT_E         INPUT_LAST+2
#define INPUT_FOCUS     INPUT_LAST+3
#define INPUT_B         INPUT_LAST+4
#define INPUT_T         INPUT_LAST+5
#define INPUT_DELETE    INPUT_LAST+6

ApplicationDozer::ApplicationDozer():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationDozer::Init(){
    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->SetVSync(true);
    renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");

    //Randomise the randomiser
    rrand = new RRandom();
    debug->Info("Polulating RRandom\n");
    rrand->Generate(512,512);

    //Renderer settings
    renderer->alpha_clip = 0.5f;
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    //We make an assetmanager which we use to load/build all assets from:
    assetmanager = new AssetManager();

    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    soundsystem->AppendFile("dozer/data/engine_start_2.wav","engine_start");
    soundsystem->AppendFile("dozer/data/arm_up.wav","arm_up");
    soundsystem->AppendFile("dozer/data/engine_idle.wav","engine_idle");
    soundsystem->AppendFile("dozer/data/engine_revup.wav","engine_revup");
    soundsystem->AppendFile("dozer/data/engine_revup_long.wav","engine_revup_long");
    soundsystem->AppendFile("dozer/data/engine_stop.wav","engine_stop");
    soundsystem->AppendFile("dozer/data/engine_crank.wav","engine_crank");
    soundsystem->AppendFile("dozer/data/steelbeam.wav","steelbeam");
    soundsystem->AppendFile("dozer/data/door_opening.wav","door_opening");

    main_scene = CreateMainScene();
    main_scene->UpdatePhysics();

    BinaryAsset::DumpBinaryAssets();
    assetmanager->ListAssets();

    main_window->Resize(1600,800);
}

//Called before update physics from the physics Thread
void ApplicationDozer::RunLogic(){
    //Camera pivot around point
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    //Only when in focus
    if (!main_window->f_has_focus){
        return;
    }

    //We reset this here, gets set from callback from app->UpdatePhysics();
    dozer_floor_contact_points = 0;

    //Check that all objects havent fallen to their doom
    bool something_was_destroyed = false;
    for (Object* object:renderer->objects){
        if (object == camera){
            continue;
        }
        vec3 pos = object->GetPosition();
        if (pos.y < -10){
            //This one is lost to the void... for sure.
            if ((object == dozer) || (object == dozer->armobject)){
                ResetDozer();
            }else{

            }
        }
    }

    //Can't just call this... the renderer might be rendering. Now it needs a mutex around everything
    renderer->DeleteDestroyedObjects();

    //All further code requires the cursor not to be above an UI element
    if (ImGui::GetIO().WantCaptureMouse){
        //Clear mouse delta
        input->GetDelta(INPUT_MOUSE_WHEEL);
        return;
    }

    CheckObjectSelection();

    if (dozer_camera_tracking){
        //We attempt to keep distance constant, and height
        vec3 new_camera_target = dozer->GetPosition();
        vec3 camera_target_lerp = camera_target.lerp(new_camera_target,0.05f);

        vec3 camera_pos_target = camera->GetPosition();
        camera_pos_target.y = dozer->GetPosition().y + 7.0;
        camera_pos_target.x = dozer->GetPosition().x;
        vec3 camera_pos_lerp = camera->GetPosition().lerp(camera_pos_target,0.05f);
        camera->SetPosition(camera_pos_lerp);

        vec3 up = vec3(0,1,0);
        camera->SetLookAt(camera_target_lerp,&up);

        camera_target = camera_target_lerp;

        vec3 dist = camera->GetPosition() - new_camera_target;
        if (dist.length() < 20.0f){
            camera->MoveForwardBy(-0.1f);
        }
        if (dist.length() > 25.0f){
            camera->MoveForwardBy(0.1f);
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

    static float mouse_delta_sum = 0;
    if (mouse_delta_sum != 0){
        camera->MoveForwardBy(mouse_delta_sum / 10.0f);
        mouse_delta_sum /= 1.1;
    }
    mouse_delta_sum += input->GetDelta(INPUT_MOUSE_WHEEL);

    //Hide selected object
    if (selected_object){
        if (input->WasKeyReleased(INPUT_H)){
            selected_object->Hide();
        }
        if (input->WasKeyReleased(INPUT_DELETE)){
            selected_object->Destroy();
            Physics* physics = selected_object->GetPhysics();
            if (physics){
                physics->world->WakeUpEveryone();
            }
            selected_object = NULL;
        }
    }

    //character
    if (dozer){
        if (input->IsKeyDown(INPUT_MOVE_UP)){
            dozer->MoveForward();
        }
        if (input->IsKeyDown(INPUT_MOVE_DOWN)){
            dozer->MoveBackward();
        }
        if (input->IsKeyDown(INPUT_MOVE_RIGHT)){
            dozer->TurnRight();
        }
        if (input->IsKeyDown(INPUT_MOVE_LEFT)){
            dozer->TurnLeft();
        }
        if (input->IsKeyDown(INPUT_TURN_UP)){
            dozer->ArmUp();
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            dozer->ArmDown();
        }
    }

    if (input->WasKeyReleased(INPUT_B)){
        for (int i=0;i<50;i++){
        int r = rand()%4;
        if (r == 0)
            SpawnAssetAt("Box", vec3(0,5,0));
        if (r == 1)
            SpawnAssetAt("Crate", vec3(0,5,0));
        if (r == 2)
            SpawnAssetAt("Barrel", vec3(0,5,0));
        if (r == 3)
            SpawnAssetAt("TrafficCone", vec3(0,6,0));
        }
    }

    if (input->WasKeyReleased(INPUT_E)){
        if (dozer->IsEngineRunning()){
            dozer->StartStopEngine(false);
        }else{
            dozer->StartStopEngine(true);
        }
    }

    if (input->WasKeyReleased(INPUT_T)){
        dozer_camera_tracking = !dozer_camera_tracking;
    }

    if (input->WasKeyReleased(INPUT_FOCUS)){
        //We center and track the camera onto the selected object
        if (selected_object){
            camera_target = selected_object->GetWorldPosition();
            vec3 up = up = vec3(0,1,0);
            camera->SetLookAt(camera_target,&up);
        }
    }

}

void ApplicationDozer::ResetDozer(){
    //This joint thing really likes to break
    // Destroy the joint
    if (dozer->joint){
        //dozer->GetPhysics()->world->rp_world->destroyJoint(dozer->joint);
        //dozer->joint = NULL;
    }

    dozer->ResetPhysics();
    dozer->SetPosition(vec3(0,2,0));
    dozer->armobject->ResetPhysics();
    dozer->armobject->SetPosition(vec3(0,1,0));
}

void ApplicationDozer::SpawnAssetAt(const std::string& name, const vec3& wpos){
    Object* asset = assetmanager->GetObjectFromAsset(name.c_str());
    if (!asset){
        return;
    }
    asset->name = name;
    asset->SetPosition(wpos);
    asset->AddPhysics(main_scene->physics_world);
    Physics* physics = asset->GetPhysics();
    if (physics){
        vec3 extents = asset->GetMesh()->GetExtents();

        physics->AddBoxCollider(extents*0.5f,vec3(0,0.0,0),quat().identity());
        physics->SetStatic(false);
        physics->SetGravityEnabled(true);
    }
    asset->SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
    asset->SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS|COLLISION_CATEGORY_FLOOR);

    main_scene->AddObject(asset);
    //debug->Info("Spwaned in %s\n",name.c_str());
}

//Write all scene objects with parameters to a file.
void ApplicationDozer::ExportSceneString(){
    FILE* file;
	size_t sz = 0;
    const char* filename = "SceneExport.cpp";
	file = fopen(filename, "wb");
    if(!file){
		debug->Fatal("Writing to %s failed.\n",filename);
		return;
	}

    //Loop over the objects we are interested in
    for (Object* object:renderer->objects){
        if (object->name.compare("Wall") == 0){
            vec3 p = object->GetPosition();
            quat q = object->GetRotation();
            fprintf(file,"wall = new Object(wall);\n");
            fprintf(file,"wall->SetPosition(vec3(%.5f,%.5f,%.5f));\n",p.x,p.y,p.z);
            fprintf(file,"wall->SetRotation(quat(%.5f,%.5f,%.5f,%.5f));\n",q.x,q.y,q.z,q.w);
            fprintf(file,"scene->AddObject(wall);\n");
        }
        if (object->name.compare("FloorConcrete") == 0){
            fprintf(file,"FloorConcrete:\n");
            fprintf(file," -> ID  : %lu\n",object->GetID());
            vec3 p = object->GetPosition();
            fprintf(file," -> POS : %.5f %.5f %.5f\n",p.x,p.y,p.z);
        }
    }

    debug->Info("Exported to %s\n",filename);
    fclose(file);
}

void ApplicationDozer::RenderDebugMenuBarClass(void){
    if (ImGui::BeginMenu("Dozer Scene")){
        if (ImGui::MenuItem("Export")){
            ExportSceneString();
        }
        ImGui::EndMenu();
    }
}

void ApplicationDozer::DrawImGuiUI(){
    //UI
    ImGui::Begin("Hi there!");

    ImGui::Text("Press 'B' to drop some boxes\n");
    ImGui::Text("Press 'E' to start the engine\n");
    ImGui::Text("Press 'W/A' to move the arm up or down\n");
    std::string str_camera_tracking;
    dozer_camera_tracking ? str_camera_tracking = "ENABLED" : str_camera_tracking = "DISABLED";
    ImGui::Text("Press 'T' to enable camera tracking (Currently %s)\n",str_camera_tracking.c_str());

    ImGui::Text("Dozer Floor Contacts: %i\n",dozer_floor_contact_points);

    ImGui::End();

    RenderDebugMenuBar();
    RenderApplicationUI();
    RenderRandTestWindow();
    //ImGui::ShowDemoWindow();
}

Scene* ApplicationDozer::CreateMainScene(){
    Scene* scene = CreateNewScene("Dozer Test Scene");

    //Add input to input controller
    scene->inputcontroller->AddKeyMap('H',INPUT_H);
    scene->inputcontroller->AddKeyMap('E',INPUT_E);
    scene->inputcontroller->AddKeyMap('B',INPUT_B);
    scene->inputcontroller->AddKeyMap('T',INPUT_T);
    scene->inputcontroller->AddKeyMap(VK_DECIMAL,INPUT_FOCUS);
    scene->inputcontroller->AddKeyMap(VK_DELETE,INPUT_DELETE);

    //Setup light and camera
    DirectionalLight* sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.85,0.7);
    sun->brightness = 8.5;
    sun->viewport.zoom = 15;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    PointLight* lamp = new PointLight();
    lamp->name = "Blue Point Light";
    lamp->SetPosition(vec3(0,5,0));
    lamp->color = vec3(0,0.2,1.0);
    lamp->brightness = 10;
    scene->AddObject(lamp);

    scene->camera = new Camera();
    scene->camera->name = "Main Camera";
    scene->camera->SetPosition(vec3(5,5,5));
    scene->camera->SetLookAt(vec3());
    scene->camera->SetupPerspective(scene->renderer->width,scene->renderer->height,45,0.1,100);
    scene->AddObject(scene->camera);

    //Add phyics
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0,-9.81,0));
    scene->physics_world->SetDebugRendering(false);
    scene->physics_world->rp_world->setEventListener(this);

    //Load from a GLTF file and build assets.
    gltfloader.LoadGLTFFile("dozer/data/dozer.glb");

    {//FLOOR
        Object* floor = CreateNewObjectFromGLTF("FloorConcrete",scene);
        if (!floor){
            debug->Fatal("No Floor was found\n");
        }
        floor->SetPosition(vec3(0,-1,0));
        floor->AddPhysics(scene->physics_world);
        if (Physics* physics = floor->GetPhysics()){
            physics->AddBoxCollider(vec3(4.0,0.4,4.0),vec3(0,0,0),quat().identity());
            physics->SetStatic(true);
            floor->SetCollisionCategoryBits(COLLISION_CATEGORY_FLOOR);
            floor->SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS|COLLISION_CATEGORY_SMOKE);
            physics->body->rigidbody->setUserData((Object*)floor);
        }
        //Create a copy
        floor = new Object(floor);
        floor->SetPosition(vec3(-8,-1,0));
        scene->AddObject(floor);
        floor = new Object(floor);
        floor->SetPosition(vec3(-16,-2.7,0));
        quat q;
        q.set_rotation(vec3(0,1,0),toradians(5));
        q.set_rotation(vec3(0,0,1),toradians(25));
        floor->SetRotation(q);
        scene->AddObject(floor);
        floor = new Object(floor);
        floor->SetPosition(vec3(-24,-4.7,0));
        scene->AddObject(floor);
        floor = new Object(floor);
        floor->SetPosition(vec3(-24,-4.7,8));
        scene->AddObject(floor);

    }

    {//Walls
        Object* wall = CreateNewObjectFromGLTF("Wall",scene);
        if (!wall){
            debug->Fatal("No wall was found\n");
        }
        wall->AddPhysics(scene->physics_world);
        if (Physics* physics = wall->GetPhysics()){
            vec3 extent = wall->GetMesh()->GetExtents()*0.5f;
            physics->AddBoxCollider(extent,vec3(0,extent.y,0),quat().identity());
            physics->SetStatic(false);
            physics->SetGravityEnabled(true);
            physics->SetBounciness(0);
            wall->SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
            wall->SetCollideWithMaskBits(COLLISION_CATEGORY_FLOOR|COLLISION_CATEGORY_OBJECTS);
            wall->SetMass(20);
            wall->SetPosition(vec3(2.5,-0.5,0));
        }
        wall = new Object(wall);
        wall->SetPosition(vec3(-9.81609,-0.60122,3.31158));
        wall->SetRotation(quat(0.00049,0.75604,0.00029,0.65452));
        scene->AddObject(wall);
        wall = new Object(wall);
        wall->SetPosition(vec3(-9.94301,-0.60226,-2.87427));
        wall->SetRotation(quat(0.00024,0.69999,-0.00001,0.71415));
        scene->AddObject(wall);
        wall = new Object(wall);
        wall->SetPosition(vec3(-26.40296,-4.30119,-2.79435));
        wall->SetRotation(quat(-0.00031,-0.36153,-0.00010,0.93236));
        scene->AddObject(wall);
        wall = new Object(wall);
        wall->SetPosition(vec3(-27.08307,-4.30100,4.32575));
        wall->SetRotation(quat(0.00000,-0.00000,-0.00001,1.00000));
        scene->AddObject(wall);
        wall = new Object(wall);
        wall->SetPosition(vec3(-27.08308,-4.30096,0.66575));
        wall->SetRotation(quat(-0.00000,-0.00000,-0.00005,1.00000));
        scene->AddObject(wall);
        wall = new Object(wall);
        wall->SetPosition(vec3(-22.75300,-4.24106,-3.39431));
        wall->SetRotation(quat(0.00000,0.73455,0.00000,0.67856));
        scene->AddObject(wall);
    }

    {
        Object* wall = CreateNewObjectFromGLTF("WallDoor",scene);
        if (!wall){
            debug->Fatal("No WallDoor was found\n");
        }
        wall->AddPhysics(scene->physics_world);
        if (Physics* physics = wall->GetPhysics()){

            physics->AddBoxCollider(vec3(0.4,4,1),vec3(0,0,-3),quat().identity());
            physics->AddBoxCollider(vec3(0.4,4,1),vec3(0,0,3),quat().identity());
            physics->AddBoxCollider(vec3(0.4,1,4),vec3(0,3,0),quat().identity());
            physics->SetStatic(true);
            physics->SetGravityEnabled(false);
            physics->SetBounciness(0);
            wall->SetCollisionCategoryBits(COLLISION_CATEGORY_FLOOR);
            wall->SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS|COLLISION_CATEGORY_SMOKE);
            quat q; q.set_rotation(vec3(0,1,0),toradians(90));
            wall->SetRotation(q);
            wall->SetPosition(vec3(0,3.4,-3.4));
        }
    }

    {
        Object* beam = CreateNewObjectFromGLTF("Beam",scene);
        if (!beam){
            debug->Fatal("No Beam was found\n");
        }
        beam->AddPhysics(scene->physics_world);
        if (Physics* physics = beam->GetPhysics()){
            vec3 extent = beam->GetMesh()->GetExtents()*0.5f;
            physics->AddBoxCollider(extent,vec3(0,extent.y,0),quat().identity());
            physics->SetStatic(false);
            physics->SetGravityEnabled(true);
            physics->SetBounciness(0);
            beam->SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
            beam->SetCollideWithMaskBits(COLLISION_CATEGORY_FLOOR|COLLISION_CATEGORY_OBJECTS);
            quat q; q.set_rotation(vec3(1,0,0),toradians(-20));
            beam->SetRotation(q);
            beam->SetPosition(vec3(2.7,2,0));
            physics->body->rigidbody->setUserData(beam);
        }
        beam = new Object(beam);
        quat q; q.set_rotation(vec3(1,0,0),toradians(-10));
        beam->SetRotation(q);
        beam->SetPosition(vec3(-3.2,0.4,-1));
        scene->AddObject(beam);
    }

    {
        Object* barrier = CreateNewObjectFromGLTF("BarrierDouble",scene);
        if (!barrier){
            debug->Fatal("No BarrierDouble was found\n");
        }
        barrier->AddPhysics(scene->physics_world);
        if (Physics* physics = barrier->GetPhysics()){
            vec3 extent = barrier->GetMesh()->GetExtents()*0.5f;
            physics->AddBoxCollider(extent,vec3(0,0,0),quat().identity());
            physics->SetStatic(false);
            physics->SetGravityEnabled(true);
            physics->SetBounciness(0);
            barrier->SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
            barrier->SetCollideWithMaskBits(COLLISION_CATEGORY_FLOOR|COLLISION_CATEGORY_OBJECTS);
            quat q; q.set_rotation(vec3(0,1,0),toradians(90));
            barrier->SetRotation(q);
            barrier->SetPosition(vec3(-.6,0.0,3.2));
            physics->body->rigidbody->setUserData(barrier);
            barrier->SetMass(50);
        }
    }

    {
        Object* barrier = CreateNewObjectFromGLTF("SlidingDoorFrame",scene);
        if (!barrier){
            debug->Fatal("No SlidingDoorFrame was found\n");
        }
        barrier->AddPhysics(scene->physics_world);
        if (Physics* physics = barrier->GetPhysics()){
            vec3 extent = barrier->GetMesh()->GetExtents()*0.5f;
            physics->AddBoxCollider(vec3(0.8,2,0.8),vec3(-5.0,0,0.2),quat().identity());
            physics->AddBoxCollider(vec3(0.8,2,0.8),vec3(1.8,0,0.2),quat().identity());
            physics->SetStatic(true);
            physics->SetGravityEnabled(false);
            physics->SetBounciness(0);
            barrier->SetCollisionCategoryBits(COLLISION_CATEGORY_FLOOR);
            barrier->SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS);
            quat q; q.set_rotation(vec3(0,1,0),toradians(-90));
            barrier->SetRotation(q);
            barrier->SetPosition(vec3(-7.0,0.6,2.2));
            physics->body->rigidbody->setUserData(barrier);
        }
    }

    Object* door = NULL;
    {
        door = CreateNewObjectFromGLTF("SlidingDoor",scene);
        if (!door){
            debug->Fatal("No SlidingDoorFrame was found\n");
        }
        door->AddPhysics(scene->physics_world);
        if (Physics* physics = door->GetPhysics()){
            vec3 extent = door->GetMesh()->GetExtents()*0.5f;
            physics->AddBoxCollider(extent,vec3(0,extent.y,0),quat().identity());
            physics->SetStatic(true);
            physics->SetGravityEnabled(false);
            physics->SetBounciness(0);
            door->SetCollisionCategoryBits(COLLISION_CATEGORY_FLOOR);
            door->SetCollideWithMaskBits(COLLISION_CATEGORY_OBJECTS);
            quat q; q.set_rotation(vec3(0,1,0),toradians(-90));
            door->SetRotation(q);
            door->SetPosition(vec3(-7.35,-.75,0.2));
            physics->body->rigidbody->setUserData(door);
        }
    }

    DozerButton* button = CreateDozerButton(scene);
    button->SetDoor(door);
    button->SetSoundSystem(soundsystem);

    //Add's all remaining unloaded objects
    GetAllAssetsFromGLTF();
    scene->renderer->AddMaterials(gltfloader.GetAllUniqueLoadedMaterials());
    dozer = new DozerCharacter(assetmanager,scene->physics_world,scene,rrand);
    dozer->soundsystem = soundsystem;
    scene->AddObject(dozer);
    if (dozer->armobject){
        scene->AddObject(dozer->armobject);
    }

    Animation* animation = gltfloader.LoadAnimation("EngineIdle");

    if (animation){
        debug->Ok("Loaded EngineIdle Animation from file.\n");
        dozer->AddAnimation(animation);
    }
    return scene;
}

DozerButton* ApplicationDozer::CreateDozerButton(Scene* scene){
    std::vector<Material>loaded_materials;
    DozerButton* button = new DozerButton();
    Mesh* gltfmesh = gltfloader.GetMeshFromNode("Button",&loaded_materials);
    if (!gltfmesh){
        debug->Fatal("No Button Mesh was found\n");
    }
    button->name = "Button";
    button->SetMesh(gltfmesh);
    Asset* asset = assetmanager->AddNewAsset("Button",button);
    button->AddPhysics(scene->physics_world);
    if (Physics* physics = button->GetPhysics()){
        vec3 extent = button->GetMesh()->GetExtents()*0.5f;
        physics->AddBoxCollider(extent,vec3(0,-0.2,0),quat().identity());
        physics->SetStatic(false);
        physics->SetGravityEnabled(true);
        physics->SetBounciness(0);
        button->SetCollisionCategoryBits(COLLISION_CATEGORY_OBJECTS);
        button->SetCollideWithMaskBits(COLLISION_CATEGORY_FLOOR|COLLISION_CATEGORY_OBJECTS);
        physics->body->rigidbody->setUserData((Object*)button);
        button->SetPosition(vec3(-6,0.6,3.0));
        button->TakeMaterialNames(loaded_materials);
    }

    scene->AddObject(button);
    return button;

}

//Event listener for on contact method
//Called from within physics update.
void ApplicationDozer::onContact(const CollisionCallback::CallbackData& callbackData){
    //debug->Info("Contact: num pairs %hhu\n",callbackData.getNbContactPairs());
    for (uint32_t i = 0; i < callbackData.getNbContactPairs(); i++) {
        CollisionCallback::ContactPair contactPair = callbackData.getContactPair(i);

        Object* d1 = (Object*)contactPair.getBody1()->getUserData();
        Object* d2 = (Object*)contactPair.getBody2()->getUserData();

        //Things we are interested in
        Object* floor = NULL;
        Object* dozer = NULL;
        Object* beam = NULL;
        Object* button = NULL;
        if (d1 && (d1->name.compare("FloorConcrete") == 0)){
            floor = d1;
        }
        if (d2 && (d2->name.compare("FloorConcrete") == 0)){
            floor = d2;
        }
        if (d1 && (d1->name.compare("Dozer") == 0)){
            dozer = d1;
        }
        if (d2 && (d2->name.compare("Dozer") == 0)){
            dozer = d2;
        }
        if (d1 && (d1->name.compare("Beam") == 0)){
            beam = d1;
        }
        if (d2 && (d2->name.compare("Beam") == 0)){
            beam = d2;
        }
        if (d1 && (d1->name.compare("Button") == 0)){
            button = d1;
        }
        if (d2 && (d2->name.compare("Button") == 0)){
            button = d2;
        }
        if (dozer && floor){
            dozer_floor_contact_points++;
        }
        if (dozer && button){

            DozerButton* dbutton = dynamic_cast<DozerButton*>(button);
            if (dbutton){
                dbutton->SetWasPressed(true);
            }
        }
        if (beam && floor){
            float beam_velocity = beam->GetVelocity().length();
            if (contactPair.getEventType() == CollisionCallback::ContactPair::EventType::ContactStart){
                //debug->Info("Beam hit floor at velocity %.3f\n",beam_velocity);
                if (soundsystem->FinishedPlaying("steelbeam")){
                    float gain;
                    if (beam_velocity > 1.5){
                        gain = 1;
                    }else{
                        gain = clamp(beam_velocity,0,1);
                    }
                    soundsystem->Play("steelbeam",false,gain);
                    //debug->Info(" -> Doink!\n");
                }
            }
        }
    }
    if (dozer_floor_contact_points > 0){
        //debug->Info("Dozer contacts a floor by %i contactpoints\n",dozer_floor_contact_points);
    }
}

void ApplicationDozer::onTrigger(const reactphysics3d::OverlapCallback::CallbackData& callbackData){
    //debug->Info("Trigger: num overlap pairs %hhu\n",callbackData.getNbOverlappingPairs());
}