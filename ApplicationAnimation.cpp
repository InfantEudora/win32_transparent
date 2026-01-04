#include "ApplicationAnimation.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationAnimation", DEBUG_ALL);

#define INPUT_JUMP    INPUT_LAST+1

ApplicationAnimation::ApplicationAnimation():Application(){
    debug->Info("Created new ApplicationAnimation.\n");
};

Scene* ApplicationAnimation::CreateEmptyScene(){
    Scene* scene = CreateNewScene("Empty Test Scene");
    //Make a sun
    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-10,10,10));
    sun->color = vec3(1,0.8,0.6);
    sun->brightness = 7.0;
    sun->viewport.zoom = 3;
    sun->SetLookAt(vec3());
    scene->AddObject(sun);

    scene->inputcontroller->AddKeyMap(VK_SPACE,INPUT_JUMP);

    return scene;
}

void ApplicationAnimation::Init(void){
    //Create a renderer with initial size
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    renderer->skinned_shader = new Shader("shaders/default_skinned.vert","shaders/default.frag");
    renderer->alpha_clip = 0.5f;
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_scene = CreateEmptyScene();
    main_scene->UpdatePhysics();

    assetmanager = new AssetManager();
    gltfloader.LoadGLTFFile("data/gwen_anim.glb");
    GetAllAssetsFromGLTF();

    //Load Gwen model with animation data
    character = new PlayerCharacter();
    Skeleton* skeleton = dynamic_cast<Skeleton*>(character);
    gltfloader.GetSkeleton("gwen",assetmanager,skeleton);
    if (skeleton){
        std::vector<Material>loaded_materials;
        Mesh* skinned_mesh = gltfloader.GetSkinnedMeshFromNode("gwen_body",&loaded_materials);
        skeleton->SetMesh(skinned_mesh);
        skeleton->TakeMaterialNames(loaded_materials);
        skeleton->PickMaterials(loaded_materials,main_scene->renderer->materials);
        main_scene->AddObject(character);
        character->root_bone_name = character->GetChild(0) ? character->GetChild(0)->name : "No Root Bone";

        //Add all animations from GLTF
        std::vector<std::string>animation_names = gltfloader.GetAnimationNames();
        for (std::string animation_name:animation_names){
            Animation* animation = gltfloader.LoadAnimation(animation_name.c_str());
            character->AddAnimation(animation);
        }

        //We'll give it to foot trackers.
        character->foot_tracker_l = assetmanager->GetObjectFromAsset("icosphere");
        character->foot_tracker_r = assetmanager->GetObjectFromAsset("icosphere");
        main_scene->AddObject(character->foot_tracker_l);
        main_scene->AddObject(character->foot_tracker_r);

        character->foot_tracker_l->material_slot[0] = renderer->FindMaterialIndex("left_tracker");
        character->foot_tracker_l->material_names[0] = "left_tracker";
        character->foot_tracker_r->material_slot[0] = renderer->FindMaterialIndex("right_tracker");
        character->foot_tracker_r->material_names[0] = "right_tracker";
        character->tracked_foot_l = character->FindChild("mixamorig:LeftToeBase");
        character->tracked_foot_r = character->FindChild("mixamorig:RightToeBase");
    }

    //A handler for dropping files onto the window
    main_window->SetOnFileDropped([this](std::string filename){
        debug->Info("File callback received with file %s\n",filename.c_str());
        //Create a modal window in the UI:
        f_filemodal = true;
        filemodal_filename = filename;
    });

    main_window->Resize(1440,900);
}

//Called before update physics
void ApplicationAnimation::RunLogic(){
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

    //Character input
    if (character){
        if (input->IsKeyDown(INPUT_TURN_UP)){
            character->MoveForward();
        }
        if (input->IsKeyDown(INPUT_TURN_DOWN)){
            character->MoveBackward();
        }
        if (input->IsKeyDown(INPUT_TURN_RIGHT)){
            character->TurnRight();
        }
        if (input->IsKeyDown(INPUT_TURN_LEFT)){
            character->TurnLeft();
        }
        if (input->WasKeyReleased(INPUT_JUMP)){
            character->Jump();
        }
    }

    //Get the sun position to the character.
    if (sun){
        vec3 delta = sun->GetPosition() - character->GetPosition();
        //debug->Info("Sun Delta %.2f,%.2f,%.2f\n",delta.x,delta.y,delta.z);
        //Make sure it's always above our character
        sun->SetPosition(character->GetPosition() + vec3(-5,5,5));
        sun->SetWorldLookat(character->GetPosition(),vec3(0,1,0));
    }

}

void ApplicationAnimation::DrawImGuiUI(){

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

    ImGui::ShowDemoWindow();

    RenderDebugMenuBar();
    RenderApplicationUI();

    ImGui::Begin("Character Animation");
    if (character){
        ImGui::Text("Foot Tracker Left Y-Pos: %.2f",character->foot_tracker_l->GetPosition().y);
        ImGui::Text("Foot Tracker Left Y-Pos: %.2f",character->foot_tracker_r->GetPosition().y);
        ImGui::Checkbox("Movement in Place",&character->f_movement_animation_inplace);
    }
    ImGui::End();

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