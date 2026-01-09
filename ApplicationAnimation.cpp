#include "ApplicationAnimation.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationAnimation", DEBUG_ALL);

#define INPUT_JUMP    INPUT_LAST+1
#define INPUT_F       INPUT_LAST+2
#define INPUT_G       INPUT_LAST+3

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
    scene->inputcontroller->AddKeyMap('F',INPUT_F);
    scene->inputcontroller->AddKeyMap('G',INPUT_G);
    scene->camera->SetPosition(vec3(3,3,3));
    scene->camera->SetLookAt(vec3(0,1.0,0));

    //Add phyics
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0,-9.81,0));
    scene->physics_world->SetDebugRendering(false);
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

    Object* floor = assetmanager->GetObjectFromAsset("floor");
    main_scene->AddObject(floor);

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

        chain = assetmanager->GetObjectFromAsset("bone");
        Physics* physics = chain->AddPhysics(main_scene->physics_world);
        if (physics){
            physics->SetStatic(true);
        }
        main_scene->AddObject(chain);

    }





    //A handler for dropping files onto the window
    main_window->SetOnFileDropped([this](std::string filename){
        debug->Info("File callback received with file %s\n",filename.c_str());
        //Create a modal window in the UI:
        f_filemodal = true;
        filemodal_filename = filename;
    });

    main_window->Resize(1680,900);
}

//Called before update physics
void ApplicationAnimation::RunLogic(){
    //Shortcuts
    Camera* camera = main_scene->camera;
    InputController* input = main_scene->inputcontroller;

    if (character){
        if (f_mode_camera_track){
            Bone* neck = character->FindBone("mixamorig:Neck");
            Bone* head = character->FindBone("mixamorig:Head");
            vec3 head_wp = head->GetWorldPosition(STATE_ACCESS_PHYSICS) - 3 * head->GetWorldForward();
            vec3 p = camera->GetPosition();
            vec3 diff = p.lerp(head_wp,0.04f);
            camera->SetPosition(diff);
            quat r = camera->GetRotation();
            quat t = quat::getquat(neck->GetWorldPosition(STATE_ACCESS_PHYSICS),camera->GetWorldPosition(STATE_ACCESS_PHYSICS),Object::ref_up);
            t.normalize();
            r = quat::slerp(r,t,0.15f);
            camera->SetRotation(r);
        }

        if (f_mode_grab){
            //We use the camera left/right up down to move the character.
            int dx = input->GetDelta(INPUT_MOUSE_X);
            int dy = input->GetDelta(INPUT_MOUSE_Y);

            vec3 vdx = camera->GetLeft() * dx * 0.01;
            vec3 vdy = camera->GetUp() * -dy * 0.01;
            character->MoveBy(vdx + vdy);

            if (input->WasKeyReleased(INPUT_CLICK_LEFT)){
                f_mode_grab = false;
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

        //Let the bone track the character
        if (f_chain_track_head && chain && character){
            Bone* head = character->FindBone("mixamorig:Head");
            vec3 head_wp = head->GetWorldPosition(STATE_ACCESS_PHYSICS) - head->GetWorldForward();
            vec3 p = chain->GetPosition();
            vec3 diff = p.lerp(head_wp,0.04f);
            chain->SetPosition(diff);
        }
    }

    if (selected_skeleton && f_ik_arm){
        Bone* hand_r = selected_skeleton->FindBone("mixamorig:RightHand");
        if (!hand_r){
            hand_r = selected_skeleton->FindBone("Bone.002");
        }

        if (hand_r && chain){
            vec3 target_pos = chain->GetWorldPosition(STATE_ACCESS_PHYSICS);
            hand_r->IKExtend(target_pos,2,0.5f);
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
    if (character && main_window->f_has_focus){
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
        if (input->IsKeyDown(INPUT_JUMP)){
            character->Jump();
        }
        /*
        if (input->WasKeyReleased(INPUT_TURN_UP)){
            character->ToIdle();
        }
        if (input->WasKeyReleased(INPUT_TURN_RIGHT)){
            character->ToIdle();
        }
        if (input->WasKeyReleased(INPUT_TURN_LEFT)){
            character->ToIdle();
        }*/
        if (input->IsKeyDown(INPUT_MOVE_LEFT)){
            character->TurnLookLeft();
        }
        if (input->IsKeyDown(INPUT_MOVE_RIGHT)){
            character->TurnLookRight();
        }
        if (input->IsKeyDown(INPUT_MOVE_UP)){
            character->TurnLookUp();
        }
        if (input->IsKeyDown(INPUT_MOVE_DOWN)){
            character->TurnLookDown();
        }
        if (input->IsKeyDown(INPUT_F)){
            character->f_animation_override = true;
            character->animation_override_ticks++;
        }
        if (input->WasKeyReleased(INPUT_G)){
            f_mode_grab = !f_mode_grab;
        }
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
    RenderSkeletonUI();

    ImGui::Begin("Character Animation");
    if (character){
        ImGui::Text("character_state");
        ImGui::Text("moving_forward : %s",character->character_state.moving_forward ? "Yes" : "No");
        ImGui::Text("moving_left    : %s",character->character_state.moving_left ? "Yes" : "No");
        ImGui::Text("moving_right   : %s",character->character_state.moving_right ? "Yes" : "No");

        ImGui::Separator();
        ImGui::Text("Head Turn Direction L/R : %.2f",character->head_turn_direction_lr);
        ImGui::Text("Head Turn Direction U/D : %.2f",character->head_turn_direction_ud);
        ImGui::Checkbox("Movement in Place",&character->f_movement_animation_inplace);
        ImGui::Checkbox("Enable Manual Animations",&character->f_animation_override);
        ImGui::Checkbox("Rotation Animations",&character->f_rotation_animation);
        ImGui::Checkbox("Grab Mode",&f_mode_grab);
        ImGui::Checkbox("Head Track",&f_chain_track_head);
        ImGui::Checkbox("Camera Track",&f_mode_camera_track);
        ImGui::Separator();
        Bone* hips = character->FindBone("mixamorig:Hips");
        if (hips){
            ImGui::Text("Hip Bone");
            vec3 wp = hips->GetWorldPosition(STATE_ACCESS_RENDERER);
            vec3 pos = hips->GetPosition(STATE_ACCESS_RENDERER);
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Local Position", (float*)&pos, 0.01f, -1.0f, 1.0f);
            ImGui::DragFloat3("World Position", (float*)&wp, 0.01f, -1.0f, 1.0f);
            vec3 wfwd = hips->GetWorldForward(STATE_ACCESS_RENDERER);
            vec3 fwd = hips->GetForward(STATE_ACCESS_RENDERER);
            ImGui::DragFloat3("Local Forward", (float*)&fwd, 0.01f, -1.0f, 1.0f);
            ImGui::DragFloat3("World Forward", (float*)&wfwd, 0.01f, -1.0f, 1.0f);
            ImGui::EndDisabled();
        }
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


void ApplicationAnimation::RenderSkeletonUI(){
    ImGui::Begin("Skeleton UI");

    Skeleton* skeleton = dynamic_cast<Skeleton*>(selected_object);
    if (!skeleton){
        ImGui::Text("Selected object is not a skeleton.\n");
    }else{
        if (ImGui::Button("Remember Sekelton")){
            selected_skeleton = skeleton;
        }
    }

    if (!selected_skeleton){
        ImGui::End();
        return;
    }
    skeleton = selected_skeleton;

    Object* bones = skeleton->GetChild(0);

    bool obj_visible = bones->IsVisible();
    if (ImGui::Checkbox("Show Skeleton Bones",&obj_visible)){
        bones->SetVisibility(obj_visible);
    }


    vec3 at = {};
    plane& p = projection_plane;
    int2 px = main_scene->inputcontroller->GetRelativeMousePosition();
    ray r = main_scene->camera->GetPixelRay(px);
    bool intersect = r.intersects_plane(p,at);

    static bool  f_track_cursor = false;

    if (ImGui::Checkbox("f_track_cursor",&f_track_cursor)){

    }
    if (ImGui::Checkbox("f_ik_arm",&f_ik_arm)){

    }

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

    std::vector<Bone*> bone_list;
    skeleton->GetAllBones(skeleton,bone_list);

    int imgui_id = -1;
    for (Bone* bone:bone_list){
        imgui_id++;
        RenderBoneModifierHeader(bone, imgui_id);
    }
    ImGui::End();
}


void ApplicationAnimation::RenderBoneModifierHeader(Bone* bone, int id){
    if (!bone){
        return;
    }

    if (ImGui::CollapsingHeader(bone->name.c_str())){
        ImGui::PushID(id);
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

        ImGui::DragFloat4("Reference Quaternion", (float*)&bone->reference_rotation, 0.01f, -1.0f, 1.0f);
        ImGui::DragFloat4("Resulting Quaternion", (float*)&q, 0.01f, -1.0f, 1.0f);
        ImGui::EndDisabled();
        if (apply_rotation){
            bone->SetRotation(q);
        }

        static float modify_parent = 0.0f;
        if (ImGui::DragFloat("Parent Depth = 1", (float*)&modify_parent, 0.01f, 0.0f, 1.0f)){

        }
        if (ImGui::DragFloat("Animation Mask", (float*)&bone->animation_mask, 0.01f, 0.0f, 1.0f)){

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

        float up_by = 0;
        if (ImGui::DragFloat("Up By", (float*)&up_by, 0.01f, -1.0f, 1.0f)){
            bone->MoveUpBy(up_by);
        }

        ImGui::PopID();
    }
}