/*
    The debug UI - the menu bar, the Scene tree, the Inspector and its five tabs, the Engine
    panel, the shader list and the RRandom test window. Everything an ImGui panel in this engine
    draws that is not an app's own.

    THIS FILE IS ONE OF A SWAPPABLE PAIR. core/ApplicationDebugUI_none.cpp defines the same
    entry points as empty functions, and engine.mk compiles exactly one of them: this one with
    USE_IMGUI=1, the other with USE_IMGUI=0.

    WHY A PAIR RATHER THAN AN #ifdef, for the third time in this codebase (see
    core/ApplicationMCP.cpp and BinaryAssetMemoryEmpty.cpp): the objects under build/core are
    compiled ONCE and shared by every app, and make compares them by timestamp, never by the
    flags they were built with. A flag that CHANGES a core translation unit would let whichever
    app built first decide what every other app links, with nothing rebuilt and nothing warned.
    A flag that swaps one whole unit for another cannot. engine.mk's CORE_CFLAGS block is the
    long version.

    An APP's own panels are guarded differently - #ifdef USE_IMGUI at their definitions - because
    app objects are compiled with CFLAGS and a define reaches them safely. Same flag, two
    mechanisms, and the line between them is the one CORE_CFLAGS draws.

    Moved out of Application.cpp on 2026-09-14, which was 2381 lines and is now 1174.
*/
#include <winsock2.h>
#include "glad.h"

#include "Application.h"
//Explicit now that core/Window.h no longer provides them - see the note at the top of it.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "imgui_internal.h" //DockBuilder* and ImHashStr, for the default dock layout
#include "Window.h"
#include "Renderer.h"
#include "Scene.h"
#include "Camera.h"
#include "AssetManager.h"
#include "Object.h"
#include "RRandom.h"
#include "Debug.h"

static Debugger* debug = new Debugger("ApplicationDebugUI",DEBUG_ALL);

//Builds the shell of a command for one object, so the call sites below stay one or two lines.
static SimCommand ObjectCommand(uint16_t type, objectid_t target){
    SimCommand cmd;
    cmd.type = type;
    cmd.target = target;
    return cmd;
}

//A boolean property is a PAIR of bits - the flag says "I am setting this", the bit at the same
//position in bool_values says what to. See the SIM_CMD_FLAG_* comment in SimCommand.h.
static void SetCommandBool(SimCommand& cmd, uint32_t flag, bool value){
    cmd.flags |= flag;
    if (value){
        cmd.bool_values |= flag;
    }else{
        cmd.bool_values &= ~flag;
    }
}

void Application::UpdateUICameraControls(Camera* camera,int id){
    if (!camera){
        return;
    }

    std::string title = camera->name + "##" + std::to_string(id) +   " Camera Controls";

    if (ImGui::CollapsingHeader(title.c_str())){
        //From THIS camera, not main_scene's - the panel also renders for a selected camera
        //object, and seeding the widget from the main camera used to write the main camera's
        //znear onto whichever camera was being inspected.
        float znear = camera->viewport.znear;
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
        if (camera->type == CAMERA_TYPE_PERSPECTIVE){
            if (ImGui::DragFloat("FOV", (float*)&camera->viewport.fov, 0.1f, 0.0f, 180.0f)){
                camera->CalculateLookatMatrix();
            }
            if (ImGui::Button("Swith to Orthographic")){
                camera->SetType(CAMERA_TYPE_ORTHOGRAPHIC);
            }
        }else{
            if (ImGui::DragFloat("Zoom", (float*)&camera->viewport.zoom, 0.1f, 0.0f, 100.0f)){
                camera->CalculateLookatMatrix();
            }
            if (ImGui::Button("Swith to Perspective")){
                camera->SetType(CAMERA_TYPE_PERSPECTIVE);
            }
        }
    }
}

//Renders all things related to world physics.
//Whole function, because its parameter names a type that does not exist without USE_PHYSICS.
//Note the tick-rate widgets at the bottom are NOT physics-specific - they belong to the sim
//clock, which every app has - but they live inside this panel, so a no-physics build loses
//them too. Moving them is a separate job; see docs/engine_backlog.md item 73.
#ifdef USE_PHYSICS
void Application::UpdateUIWorldPhysics(PhysicsWorld* physics_world){
    if (!physics_world){
        ImGui::BeginDisabled();
        ImGui::CollapsingHeader("No World Physics");
        ImGui::EndDisabled();
        return;
    }

    if (ImGui::CollapsingHeader("World Physics")){
        bool ph_debug_render = physics_world->IsDebugRenderingEnabled();
        if (ImGui::Checkbox("Render Colliders [Debug]",&ph_debug_render)){
            physics_world->SetDebugRendering(ph_debug_render);
        }
        bool ph_paused = main_scene->IsPhysicsPaused();
        if (ImGui::Checkbox("Pause Physics (Active Scene) [Debug]",&ph_paused)){
            main_scene->PausePhysics(ph_paused);
        }
        static int step_count = 1;
        ImGui::BeginDisabled(!ph_paused);
        ImGui::SetNextItemWidth(80);
        ImGui::InputInt("##PhysicsStepCount",&step_count);
        if (step_count < 1) step_count = 1;
        ImGui::SameLine();
        if (ImGui::Button("Step Physics")){
            main_scene->StepPhysics(step_count);
        }
        ImGui::EndDisabled();
        int pending_steps = main_scene->GetPendingPhysicsSteps();
        if (pending_steps > 0){
            ImGui::SameLine();
            ImGui::Text("(%i pending)",pending_steps);
        }
        //Gravity is simulation state, so it goes through the queue like everything else that
        //is. Pause/step/time factor/TPS below do NOT: they change how often a tick runs in real
        //time, never what a tick computes, so they are not part of what a replay reproduces.
        vec3 gravity = physics_world->GetGravity();
        if (ImGui::DragFloat3("Gravity (m/s^2)",(float*)&gravity,0.1f,-20,20)){
            SimCommand cmd;
            cmd.type = SIM_CMD_WORLD_SET_GRAVITY;
            cmd.velocity = gravity;
            SubmitUICommand(cmd);
        }
        if (ImGui::DragFloat("Time Factor (tick rate)",&physics_time_factor,0.01f,0.1f,2.0f)){
            //Nothing to do here, it's applied in the physics update loop - and it changes how
            //OFTEN a tick runs, not how long a tick is. The timestep stays GetPhysicsTimestep().
        }
        ImGui::SetItemTooltip("Runs ticks more/less often in real time. The simulation timestep itself never changes.");
        float tps = physics_tps;
        if (ImGui::DragFloat("Target Physics TPS",&tps,1.0f,1.0f,250.0f)){
            SetPhysicsTPS(tps);
        }
    }

}
#endif

void Application::RenderDebugMenuBar(){
    if (ImGui::BeginMainMenuBar()){
        //Creating an object is a change to the simulation, so every item here submits a command
        //and the object appears on the next tick - see the Debug UI section comment. The one
        //exception is the GLTF import below, which cannot be a command.
        if (main_scene && ImGui::BeginMenu("Add Object")){
            if (ImGui::MenuItem("Empty")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_EMPTY;
                SubmitUICommand(cmd);
            }
            if (ImGui::MenuItem("Camera")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_CAMERA;
                SubmitUICommand(cmd);
            }
            if (ImGui::MenuItem("DirectionalLight")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_DIRECTIONAL_LIGHT;
                SubmitUICommand(cmd);
            }
            if (ImGui::MenuItem("PointLight")){
                SimCommand cmd;
                cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                cmd.subtype = SIM_PRIMITIVE_POINT_LIGHT;
                SubmitUICommand(cmd);
            }
            ImGui::Separator();
            if (ImGui::BeginMenu("Objects From Assets")){
                if (!assetmanager){
                    ImGui::MenuItem("-- NO ASSET MANAGER --");
                }else{
                    for (Asset* asset:assetmanager->assets){
                        if (ImGui::MenuItem(asset->name.c_str())){
                            //By id, not by name: the id IS the name, hashed, and it is what fits
                            //in a fixed payload - see AssetIDFromName in AssetManager.h.
                            SimCommand cmd;
                            cmd.type = SIM_CMD_OBJECT_SPAWN_ASSET;
                            cmd.asset = asset->id;
                            SubmitUICommand(cmd);
                        }
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("PlayerCharacter(Skeleton) From Loaded GLTF")){
                //Use of assetmanager is optional, can be NULL. It's only used to load debug bones.
                //Skeletons need to be loaded as skin in GLTF. List skins currently open in GLTF loader.
                std::vector<std::string>skeleton_names = gltfloader.GetSkeletonNames();
                if (skeleton_names.size() == 0){
                    ImGui::MenuItem("-- NO SKELETON IN GLTF --");
                }else{
                    for (std::string name:skeleton_names){
                        if (ImGui::BeginMenu(name.c_str())){
                            //We list all the skinned meshes from the current file.
                            std::vector<std::string>skinned_meshes = gltfloader.GetSkinnedMeshNames();
                            for (std::string skinned_mesh_name:skinned_meshes){
                                if (ImGui::MenuItem(skinned_mesh_name.c_str())){
                                    //DELIBERATELY NOT a command. Importing from the GLTF loader
                                    //builds meshes, which is render-thread work (see
                                    //GetAssetsFromGLTF, which Fatal()s off it), and a pointer to
                                    //the result cannot travel in a SimCommand. Asset import is
                                    //authoring, not simulation - it happens outside the tick and
                                    //is not part of what a replay reproduces. Once imported, the
                                    //asset spawns through SIM_CMD_OBJECT_SPAWN_ASSET like any
                                    //other.
                                    PlayerCharacter* character = new PlayerCharacter();
                                    Skeleton* skeleton = dynamic_cast<Skeleton*>(character);
                                    gltfloader.GetSkeleton(name.c_str(),assetmanager,skeleton);
                                    if (skeleton){
                                        std::vector<Material>loaded_materials;
                                        Mesh* skinned_mesh = gltfloader.GetMeshFromNode(skinned_mesh_name.c_str(),&loaded_materials,true);
                                        skeleton->SetMesh(skinned_mesh);
                                        skeleton->TakeMaterialNames(loaded_materials);
                                        skeleton->PickMaterials(loaded_materials,main_scene->renderer->materials);
                                        main_scene->AddObject(character);
                                        //In order to apply animations to anyting, there needs to be a
                                        //root bone name set.
                                        character->root_bone_name = "Mixamorig:Hips";
                                    }
                                }
                            }
                            ImGui::EndMenu();
                        }
                    }
                }
                ImGui::EndMenu();
            }


            ImGui::EndMenu();
        }
        if (main_scene && ImGui::BeginMenu("Load Animation")){
            if (ImGui::BeginMenu("Skeleton Animations From Loaded GLTF")){
                std::vector<std::string>animation_names = gltfloader.GetAnimationNames();
                if (animation_names.size() == 0){
                    ImGui::MenuItem("-- NO ANIMATIONS IN GLTF --");
                }else{
                    for (std::string name:animation_names){
                        if (ImGui::MenuItem(name.c_str())){
                            Animation* animation = gltfloader.LoadAnimation(name.c_str());
                            if (selected_object){
                                selected_object->AddAnimation(animation);
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")){
            ImGui::MenuItem("Scene","",&f_show_scene_window);
            ImGui::MenuItem("Inspector","",&f_show_inspector_window);
            ImGui::MenuItem("Engine","",&f_show_engine_window);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window")){
            if (ImGui::MenuItem("Set Always on Top")){
                SetWindowPos(main_window->hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            if (ImGui::MenuItem("Set Normal")){
                SetWindowPos(main_window->hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Debug")){
            std::map<std::string, Debugger*>* handles = debug->GetHandles();
            std::map<std::string,Debugger*>::iterator it = handles->begin();
            const char* items[] = { "TRACE", "INFO", "WARN", "ERROR"};
            static std::vector<int>current_items;
            if (current_items.size() != handles->size()){
                current_items.resize(handles->size());
            }
            for (int i=0;i<handles->size();i++){
                 if (ImGui::BeginMenu(it->first.c_str())){
                    static bool enabled = true;
                    ImGui::MenuItem("Enabled", "", &enabled);
                    ImGui::InputInt("Input", &it->second->level, 1);
                    if (it->second->level >= DEBUG_TRACE){
                        current_items.at(i) = 0;
                    }
                    if (it->second->level >= DEBUG_INFO){
                        current_items.at(i) = 1;
                    }

                    if (ImGui::Combo("Levels", &current_items.at(i), items, IM_ARRAYSIZE(items))){
                        if (current_items.at(i) == 0){
                            it->second->SetLevel(DEBUG_TRACE);
                        }else if (current_items.at(i) == 1){
                            it->second->SetLevel(DEBUG_INFO);
                        }else if (current_items.at(i) == 2){
                            it->second->SetLevel(DEBUG_WARN);
                        }else if (current_items.at(i) == 3){
                            it->second->SetLevel(DEBUG_ERROR);
                        }
                    }
                    ImGui::Text("The lower the level, the more info get's printed");

                    ImGui::EndMenu();
                }
                it++;
            }
            ImGui::EndMenu();
        }

        if (main_scene && ImGui::BeginMenu("Export Scene")){
            if (ImGui::MenuItem("Scene Objects to export.json")){
                FILE* f = fopen("export.json", "w");
                if (f){
                    fprintf(f, "{\n  \"objects\": [\n");
                    bool first = true;
                    for (Object* object : main_scene->objects){
                        vec3 pos = object->GetPosition();
                        quat rot = object->GetRotation();
                        if (!first) fprintf(f, ",\n");
                        fprintf(f, "    { \"name\": \"%s\", \"position\": [%.4f, %.4f, %.4f], \"rotation\": [%.4f, %.4f, %.4f, %.4f] }",
                                object->name.c_str(),
                                pos.x, pos.y, pos.z,
                                rot.x, rot.y, rot.z, rot.w);
                        first = false;
                    }
                    fprintf(f, "\n  ]\n}\n");
                    fclose(f);
                    debug->Info("Exported %zu objects to export.json\n", main_scene->objects.size());
                }else{
                    debug->Err("Failed to open export.json for writing\n");
                }
            }
            ImGui::EndMenu();
        }

        //Render class spcific menu bar things
        RenderDebugMenuBarClass();

        ImGui::EndMainMenuBar();
    }
}

void Application::RenderShaderUI(Shader* shader){
    if (!shader){
        return;
    }
    ImGui::Begin("Shader UI");

        GLint shaderprog_id = shader->progid;
        GLint count = 0;
        GLint size; // size of the variable
		GLenum type; // type of the variable (float, vec3 or mat4, etc)

		GLchar name[128] = {}; // variable name in GLSL
		GLsizei length; // name length

		glGetProgramiv(shaderprog_id, GL_ACTIVE_UNIFORMS, &count);
        ImGui::Text("vert file       : %s\n", shader->vname.c_str());
        ImGui::Text("frag file       : %s\n", shader->fname.c_str());

		ImGui::Text("Shader ID       : %i\n", shaderprog_id);
		ImGui::Text("Active Uniforms : %i\n", count);

        for (int i = 0; i < (int)count; i++){

            glGetActiveUniform(shaderprog_id, (GLuint)i, 128, &length, &size, &type, name);
            if (type == GL_INT){
				int v = 0;
				//glGetnUniformiv(progid,i,1*sizeof(GLint),&v);
				if(ImGui::DragInt(name,&v, 1,-10,10)){
					//glUseProgram(progid);
					//Setint(name,&v);
				}
            }else if (type == GL_FLOAT){
				float v = 0;
				//glGetnUniformfv(progid,i,1*sizeof(GLfloat),&v);
				if(ImGui::DragFloat(name,&v, 0.01f,-10,100,"%.6f")){
					//glUseProgram(progid);
					//SetFloat(name,&v);
				}
            }else{
				ImGui::Text("Uniform #%i Type: %X Name: %s\n", i, type, name);
			}
        }

    ImGui::End();
}

void Application::RenderApplicationUI(){
    //One dockspace covering the viewport, with a transparent central node so the 3D scene shows
    //through it and the mouse still reaches the world for picking (see CheckObjectSelection,
    //which gates on ImGui::GetIO().WantCaptureMouse). Without PassthruCentralNode the host window
    //would paint over the whole viewport and swallow every click.
    const ImGuiID dockspace_id = ImHashStr("WindMainDockSpace");

    //Default layout, once, and only if imgui.ini restored nothing - otherwise a layout the user
    //arranged by hand would be thrown away on every start. Everything docks LEFT: Scene and
    //Engine share the upper node as tabs, the Inspector takes the lower one, which is the
    //arrangement that keeps the tree visible while properties are being edited.
    static bool dock_layout_checked = false;
    if (!dock_layout_checked){
        dock_layout_checked = true;
        if (ImGui::DockBuilderGetNode(dockspace_id) == NULL){
            ImGui::DockBuilderAddNode(dockspace_id,ImGuiDockNodeFlags_DockSpace);
            /*
                NOT ZERO, which is what the viewport is on a start with --minimized: the window
                has no client area yet, and DockBuilderSetNodeSize asserts on it - so a fresh
                checkout (no imgui.ini) started minimised died here on its first frame.

                Substituted rather than skipped. Skipping is not "try again later":
                DockSpaceOverViewport below creates the node this frame regardless, the check
                above then finds it, and the default layout is never built - an empty dockspace,
                which imgui.ini then saves. Any positive size gives the same result, because the
                splits are fractions and the dockspace is resized to the real viewport every frame.
            */
            ImVec2 dock_size = ImGui::GetMainViewport()->WorkSize;
            if (dock_size.x <= 0.0f || dock_size.y <= 0.0f){
                dock_size = ImVec2(1280.0f,800.0f);
            }
            ImGui::DockBuilderSetNodeSize(dockspace_id,dock_size);
            ImGuiID left_id = 0;
            ImGuiID centre_id = 0;
            ImGui::DockBuilderSplitNode(dockspace_id,ImGuiDir_Left,0.24f,&left_id,&centre_id);
            ImGuiID left_top_id = 0;
            ImGuiID left_bottom_id = 0;
            ImGui::DockBuilderSplitNode(left_id,ImGuiDir_Up,0.40f,&left_top_id,&left_bottom_id);
            ImGui::DockBuilderDockWindow("Scene",left_top_id);
            ImGui::DockBuilderDockWindow("Engine",left_top_id);
            ImGui::DockBuilderDockWindow("Inspector",left_bottom_id);
            ImGui::DockBuilderFinish(dockspace_id);
        }
    }
    ImGui::DockSpaceOverViewport(dockspace_id,NULL,ImGuiDockNodeFlags_PassthruCentralNode);

    if (f_show_scene_window){
        RenderSceneWindow();
    }
    if (f_show_inspector_window){
        RenderInspectorWindow();
    }
    if (f_show_engine_window){
        RenderEngineWindow();
    }
}

void Application::RenderSceneWindow(){
    ImGui::Begin("Scene",&f_show_scene_window);
    if (scenes.empty()){
        ImGui::TextDisabled("No scenes");
        ImGui::End();
        return;
    }

    //A name filter, because the tree is the primary way to find things and a real scene runs to
    //dozens of objects (the tank scene is 59). While it has text the tree is replaced by a FLAT
    //list of every match at any depth - filtering a tree in place would either hide the matches
    //inside collapsed parents or force every parent open.
    static char name_filter[64] = {};
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##ObjectFilter","filter by name",name_filter,sizeof(name_filter));
    bool filtering = (name_filter[0] != 0);

    for (Scene* scene:scenes){
        ImGui::PushID(scene);
        int num_objects = (int)scene->objects.size();
        const char* active = (scene == main_scene) ? " (active)" : "";
        ImGui::SeparatorText((scene->name + active).c_str());

        if (filtering){
            int matches = 0;
            scene->ForEachObject([&](Object* object){
                if (object->name.find(name_filter) == std::string::npos){
                    return;
                }
                matches++;
                bool selected = (object == selected_object);
                //snprintf, not sprintf: unlike every other label here this one interpolates an
                //object NAME, whose length comes from an asset or a GLTF file.
                char label[160];
                snprintf(label,sizeof(label),"#%lu %s",(unsigned long)object->GetID(),object->name.c_str());
                if (ImGui::Selectable(label,selected)){
                    selected_object = object;
                }
            });
            ImGui::TextDisabled("%i of %i objects match",matches,num_objects);
        }else{
            ImGui::TextDisabled("%i root objects",num_objects);
            for (Object* object:scene->objects){
                UpdateUISceneObjectTreeNode(object,NULL);
            }
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void Application::UpdateUISceneObjectTreeNode(Object* object, Object* lastclicked){
    objectid_t id = object->GetID();
    long long p = id; //To suppress warning from 32-bit pointer

    //Highlighting the selected row is what makes the tree usable as a selection widget rather
    //than just a listing - before this, nothing in it showed what was selected.
    ImGuiTreeNodeFlags flags = 0;
    if (object == selected_object){
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (object->GetChild(0) == NULL){
        flags |= ImGuiTreeNodeFlags_Bullet;
    }

    if (ImGui::TreeNodeEx((void*)p,flags,"Object #%i - %s",id,object->name.c_str())){
        if (ImGui::IsItemClicked() && (lastclicked == NULL)){
            selected_object = object;
            lastclicked = object;
            debug->Info("Tree -> Selected %s\n",object->name.c_str());
        }
        for (Object* child:object->children){
            UpdateUISceneObjectTreeNode(child,lastclicked);
        }
        ImGui::TreePop();
    }
}

void Application::RenderInspectorWindow(){
    ImGui::Begin("Inspector",&f_show_inspector_window);
    Object* object = selected_object;
    if (object == NULL){
        ImGui::TextDisabled("Nothing selected.");
        ImGui::TextDisabled("Click an object in the viewport or in the Scene tree.");
        ImGui::End();
        return;
    }

    //--- Identity, and the two things that apply to any object at all ---
    ImGui::Text("%s",object->name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("#%lu",(unsigned long)object->GetID());
    if (Object* parent = object->GetParent()){
        ImGui::TextDisabled("child of #%lu %s",(unsigned long)parent->GetID(),parent->name.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Select root")){
            Object* root = parent;
            while (root->GetParent()){
                root = root->GetParent();
            }
            selected_object = root;
        }
    }else if (object->GetNumChildren() > 0){
        ImGui::TextDisabled("root, %i children",object->GetNumChildren());
    }else{
        ImGui::TextDisabled("root, no children");
    }

    //Visibility is a RENDER flag (Object::SetVisibility only touches f_visible), so it stays a
    //direct call - it changes what is drawn, not what is simulated.
    bool obj_visible = object->IsVisible();
    if (ImGui::Checkbox("Visible",&obj_visible)){
        object->SetVisibility(obj_visible);
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_DUPLICATE,object->GetID());
        //Inactive, so the copy can be dragged into place before it starts falling.
        SetCommandBool(cmd,SIM_CMD_FLAG_ACTIVE,false);
        SubmitUICommand(cmd);
        //Deliberately NOT selecting the copy: its id is only handed out when the physics thread
        //applies the command, so it does not exist yet. Scene::GetCommandResult can report it,
        //but reading that means waiting for the tick, and UI code must never wait (see
        //SubmitUICommand). The copy appears in the tree next frame.
    }
    ImGui::SameLine();
    if (ImGui::Button("Destroy")){
        SubmitUICommand(ObjectCommand(SIM_CMD_OBJECT_DESTROY,object->GetID()));
        selected_object = NULL;
    }

    //--- The tabs. A tab is only submitted when the object actually has that aspect. ---
    if (ImGui::BeginTabBar("InspectorTabs")){
        if (ImGui::BeginTabItem("Transform")){
            RenderInspectorTransformTab(object);
            ImGui::EndTabItem();
        }
#ifdef USE_PHYSICS
        if (object->GetPhysics() && ImGui::BeginTabItem("Physics")){
            RenderInspectorPhysicsTab(object);
            ImGui::EndTabItem();
        }
#endif
        if (ImGui::BeginTabItem("Render")){
            RenderInspectorRenderTab(object);
            ImGui::EndTabItem();
        }
        if (!object->animations.empty() && ImGui::BeginTabItem("Animation")){
            RenderInspectorAnimationTab(object);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Debug")){
            RenderInspectorDebugTab(object);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void Application::RenderInspectorTransformTab(Object* object){
    objectid_t id = object->GetID();

    //Absolute position. The widget reads the object every frame and only submits on an actual
    //edit, so it tracks the simulation while idle and does not fight it while being dragged.
    vec3 position = object->GetPosition();
    if (ImGui::DragFloat3("Position",(float*)&position,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_POSITION;
        cmd.position = position;
        SubmitUICommand(cmd);
    }

    //Relative nudges, resolved to an ABSOLUTE pose here rather than sent as a delta. Two reasons:
    //a command carrying "move 0.1 forward" would apply against whatever pose the object has when
    //the physics thread gets to it, and "forward" is the object's own axis, which only the caller
    //knows. Reading the pose here is safe - DrawImGuiUI holds physics_mutex.
    vec3 nudge = {};
    if (ImGui::DragFloat3("Move by",(float*)&nudge,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_POSITION;
        cmd.position = object->GetPosition() + nudge;
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Drag to nudge along the world axes. Snaps back to 0 - it is a delta.");

    float along[3] = {0,0,0};
    if (ImGui::DragFloat3("Fwd / Left / Up",along,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_POSITION;
        cmd.position = object->GetPosition()
                     + object->GetForward() * along[0]
                     + object->GetLeft() * along[1]
                     + object->GetUp() * along[2];
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Same, but along the object's OWN axes.");

    ImGui::SeparatorText("Rotation");
    //Shown as a quaternion, and edited by deltas or by absolute axis-degrees - deliberately NOT
    //as euler angles. quat::get_pitch/get_yaw/get_roll do not invert the q1*q2*q3 composition
    //used below (measured: 30 deg about X reads back as 27.7, and [10,80,10] as [63,68,63]), so a
    //euler box would drift and jump the moment it round-tripped. Fixing that means settling the
    //quaternion conventions in type_quat.h, which is its own job.
    ImGui::BeginDisabled();
    quat current = object->GetRotation();
    ImGui::DragFloat4("Quaternion",(float*)&current,0.01f);
    ImGui::EndDisabled();

    //One row of three deltas instead of the three separate "Roll By"/"Pitch By"/"Yaw By" drags.
    float rotate_by[3] = {0,0,0};
    if (ImGui::DragFloat3("Pitch/Yaw/Roll by",rotate_by,0.01f)){
        //Composed onto the CURRENT rotation here, so the command still carries an absolute pose.
        quat q = object->GetRotation();
        if (rotate_by[0] != 0.0f){
            quat d; d.set_rotation(object->GetLeft(),rotate_by[0]); q = d * q;
        }
        if (rotate_by[1] != 0.0f){
            quat d; d.set_rotation(object->GetUp(),rotate_by[1]); q = d * q;
        }
        if (rotate_by[2] != 0.0f){
            quat d; d.set_rotation(object->GetForward(),rotate_by[2]); q = d * q;
        }
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_ROTATION;
        cmd.rotation = q.normalize();
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Rotates about the object's own left/up/forward. Radians, and a delta.");

    //Absolute, and the same composition order the object_set_transform MCP tool uses for its
    //axis_degrees argument, so the two agree.
    static vec3 axis_degrees = {};
    ImGui::DragFloat3("Axis degrees",(float*)&axis_degrees,1.0f,-180.0f,180.0f);
    ImGui::SameLine();
    if (ImGui::Button("Set")){
        quat qx; qx.set_rotation(vec3(1,0,0),toradians(axis_degrees.x));
        quat qy; qy.set_rotation(vec3(0,1,0),toradians(axis_degrees.y));
        quat qz; qz.set_rotation(vec3(0,0,1),toradians(axis_degrees.z));
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_ROTATION;
        cmd.rotation = qx * qy * qz;
        SubmitUICommand(cmd);
    }

    ImGui::SeparatorText("Scale");
    vec3 scale = object->GetScale();
    if (ImGui::DragFloat3("Scale",(float*)&scale,0.01f,0.01f,100.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_SCALE;
        cmd.scale = scale;
        SubmitUICommand(cmd);
    }
    float scale_all = 1.0f;
    if (ImGui::DragFloat("Scale all by",&scale_all,0.01f,0.01f,10.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_TRANSFORM,id);
        cmd.flags |= SIM_CMD_FLAG_SCALE;
        cmd.scale = object->GetScale() * scale_all;
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("Colliders and their offsets follow; mass is kept and inertia recomputed.");
}

//The whole tab: it reads Physics and walks the body's rp3d colliders, and nothing without a
//physics build can reach it - the tab is only submitted for an object that has a body.
#ifdef USE_PHYSICS
void Application::RenderInspectorPhysicsTab(Object* object){
    Physics* physics = object->GetPhysics();
    if (!physics){
        return; //the tab is not submitted in this case, so this is belt and braces
    }
    objectid_t id = object->GetID();

    bool f_static = physics->IsStatic();
    if (ImGui::Checkbox("Static",&f_static)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        SetCommandBool(cmd,SIM_CMD_FLAG_STATIC,f_static);
        SubmitUICommand(cmd);
    }
    bool f_gravity = physics->IsGravityEnabled();
    if (ImGui::Checkbox("Reacts to gravity",&f_gravity)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        SetCommandBool(cmd,SIM_CMD_FLAG_GRAVITY,f_gravity);
        SubmitUICommand(cmd);
    }
    bool f_active = physics->IsActive();
    if (ImGui::Checkbox("Active",&f_active)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        SetCommandBool(cmd,SIM_CMD_FLAG_ACTIVE,f_active);
        SubmitUICommand(cmd);
    }
    ImGui::Text("Sleeping : %s",physics->IsSleeping() ? "yes" : "no");
    ImGui::SameLine();
    if (ImGui::SmallButton("Wake up")){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_WAKE_UP; //a trigger, so no bool_values bit
        SubmitUICommand(cmd);
    }
    ImGui::Text("Mass     : %.3f kg",physics->GetMass());

    ImGui::SeparatorText("Velocity");
    vec3 velocity = physics->GetVelocity();
    if (ImGui::DragFloat3("Linear",(float*)&velocity,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_VELOCITY;
        cmd.velocity = velocity;
        SubmitUICommand(cmd);
    }
    vec3 angular = physics->GetAngularVelocity();
    if (ImGui::DragFloat3("Angular",(float*)&angular,0.01f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_ANGULAR_VELOCITY;
        cmd.angular_velocity = angular;
        SubmitUICommand(cmd);
    }

    ImGui::SeparatorText("Surface");
    float friction = physics->GetFrictionCoefficient();
    if (ImGui::DragFloat("Friction",&friction,0.01f,0.0f,2.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_FRICTION;
        cmd.value[0] = friction;
        SubmitUICommand(cmd);
    }
    ImGui::SetItemTooltip("The handler wakes the whole world after this - a sleeping pile would "
                          "otherwise keep its old friction until something disturbed it.");
    float bounciness = physics->GetBounciness();
    if (ImGui::DragFloat("Bounciness",&bounciness,0.01f,0.0f,1.0f)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_BOUNCINESS;
        cmd.value[1] = bounciness;
        SubmitUICommand(cmd);
    }

    ImGui::SeparatorText("Collision masks");
    uint32_t category_bits = object->collision_category_bits;
    if (RenderBitmaskCheckboxes("Cat",category_bits)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_CATEGORY_BITS;
        cmd.collision_category_bits = category_bits;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    ImGui::Text("category %08X",object->collision_category_bits);

    uint32_t collide_bits = object->collide_with_bits;
    if (RenderBitmaskCheckboxes("Col",collide_bits)){
        SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SET_PHYSICS,id);
        cmd.flags |= SIM_CMD_FLAG_COLLIDE_BITS;
        cmd.collide_with_bits = collide_bits;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    ImGui::Text("collides with %08X",object->collide_with_bits);

    ImGui::SeparatorText("Colliders");
    int num_colliders = physics->GetNumColliders();
    ImGui::Text("%i collider(s)",num_colliders);
    if (physics->body && physics->body->rigidbody){
        for (uint32_t i=0;i<physics->body->rigidbody->getNbColliders();i++){
            rp3d::Collider* collider = physics->body->rigidbody->getCollider(i);
            if (collider->getCollisionShape()->getName() != rp3d::CollisionShapeName::BOX){
                continue;
            }
            char caption[48];
            sprintf(caption,"Edit box collider %lu",(unsigned long)i);
            if (ImGui::Button(caption)){
                //Spawns an ObjectCollider gizmo hooked onto this collider. The command carries
                //the collider's INDEX, not its pointer - an rp3d::Collider* is neither
                //recordable nor safe to hand across a tick boundary.
                SimCommand cmd = ObjectCommand(SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO,id);
                cmd.subtype = i;
                SubmitUICommand(cmd);
            }
        }
    }
}

#endif

//Eight checkboxes for one 8-bit mask, returning true when any of them changed. Two of these used
//to be written out twice in full, inline, at 20 lines each.
bool Application::RenderBitmaskCheckboxes(const char* id, uint32_t& mask){
    bool modified = false;
    uint32_t result = 0;
    ImGui::PushID(id);
    for (int i=0;i<8;i++){
        bool bit = !!(mask & (1<<i));
        char boxid[16];
        sprintf(boxid,"##bit%i",i);
        if (ImGui::Checkbox(boxid,&bit)){
            modified = true;
        }
        result |= ((uint32_t)bit) << i;
        if (i < 7){
            ImGui::SameLine();
        }
    }
    ImGui::PopID();
    if (modified){
        mask = result;
    }
    return modified;
}

void Application::RenderInspectorRenderTab(Object* object){
    //Everything in this tab is about what gets DRAWN, so it all stays direct - none of it is
    //simulation state and none of it belongs in a recording.
    if (Camera* camera = dynamic_cast<Camera*>(object)){
        int ui_camera_id = 1;
        UpdateUICameraControls(camera,ui_camera_id);
    }

    if (Light* light = dynamic_cast<Light*>(object)){
        ImGui::SeparatorText("Light");
        ImGui::DragFloat3("Colour",(float*)&light->color,0.01f,0.0f,1.0f);
        ImGui::DragFloat("Brightness",(float*)&light->brightness,0.01f,0.0f,10.0f);
        ImGui::DragFloat("Shadow bias",(float*)&light->shadow_bias,0.0001f,0.0f,1.0f);
    }

    ImGui::SeparatorText("Materials");
    for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
        char label[32];
        sprintf(label,"Slot %i",i);
        //Through the setter, not straight into the array: dragging a slot has to settle it, or
        //the name lookup would put its own answer back on the next frame and the widget would
        //appear to do nothing on any object loaded from an asset.
        int slot_value = object->GetMaterialSlot(i);
        if (ImGui::DragInt(label,&slot_value,1,-1,20)){
            object->SetMaterialSlot(i,slot_value);
        }
        if (!object->GetMaterialName(i).empty()){
            ImGui::SameLine();
            ImGui::TextDisabled("%s",object->GetMaterialName(i).c_str());
        }
    }

    if (Mesh* mesh = object->GetMesh()){
        ImGui::SeparatorText("Mesh");
        ImGui::Text("id %lu, %lu vertices, %lu materials",mesh->GetID(),mesh->num_vertices,mesh->num_materials);
        ImGui::TextDisabled("%s, %i references, %i morph targets",
                            mesh->IsSkinnedMesh() ? "skinned" : "static",
                            mesh->GetNumReferences(),mesh->num_morph_targets);
    }else{
        ImGui::SeparatorText("Mesh");
        ImGui::TextDisabled("No mesh - this object is not drawn.");
    }
}

void Application::RenderInspectorAnimationTab(Object* object){
    //NOT on the command queue yet, and this is the one remaining hole in step 6 of the
    //determinism plan: since the root-motion rewrite an animation's extracted deltas DRIVE the
    //object's motion, which makes clip selection simulation state. Putting it on the queue needs
    //a way to name a clip inside a fixed payload (a hash of the clip name, the same trick
    //assetid_t uses), which is a deliberate later step.
    ImGui::TextDisabled("Direct calls - not on the command queue yet.");

    ImGui::SeparatorText("Morph factors");
    for (int i=0;i<4;i++){
        char label[24];
        sprintf(label,"Target %i",i+1);
        ImGui::DragFloat(label,&object->morph_factors[i],0.01f,0,1);
    }

    ImGui::SeparatorText("Clips");
    ImGui::DragFloat("Transition time max",&object->animation_transition_time_max,0.01f,0,3);
    if (ImGui::Button("NULL (reference pose)")){
        object->SwitchToAnimation(NULL);
    }
    int button_id = 1;
    for (Animation* animation:object->animations){
        if (ImGui::Button(animation->name.c_str())){
            object->TransitionToAnimation(animation);
        }
        button_id++;
        if (button_id % 4 != 0){
            ImGui::SameLine();
        }
    }
    ImGui::NewLine();

    if (object->current_animation){
        Animation* current = object->current_animation;
        ImGui::Text("Playing : %s @ %.2f / %.2f",current->name.c_str(),current->time_index,current->duration);
        ImGui::Checkbox("Looping",&current->looped);
        ImGui::Checkbox("Extract horizontal root motion",&current->extract_horizontal_root_motion);
        ImGui::Checkbox("Extract vertical root motion",&current->extract_vertical_root_motion);
    }else{
        ImGui::TextDisabled("Playing : nothing");
    }
    //"Playing" above is already the clip being blended INTO - current_animation is the destination
    //from the moment a transition starts - so what is worth adding here is where it came from.
    if (object->previous_animation){
        ImGui::Text("Blending from %s (%.0f%% of the way across)",
                    object->previous_animation->name.c_str(),
                    object->animation_transition_factor * 100.0f);
    }
    ImGui::TextDisabled("state %i, wanted '%s'",object->animation_state,
                        object->dbg_desired_animation_name.c_str());

    if (!object->animation_blend_overrides.empty()){
        ImGui::SeparatorText("Blend time overrides");
        for (Object::AnimationBlendOverride& o:object->animation_blend_overrides){
            ImGui::Text("%s --> %s : %.2fs",o.from.empty() ? "*" : o.from.c_str(),o.to.c_str(),o.blend_time);
        }
    }
}

void Application::RenderInspectorDebugTab(Object* object){
    //Read-only internals. They live behind their own tab so they stop competing for screen space
    //with the controls - which is most of why the old single-column panel ran off the bottom of
    //the screen the moment anything was selected.
    ImGui::Text("Address : %p",(void*)object);
    ImGui::Text("Visible (renderer) : %s",object->IsVisible() ? "yes" : "no");
    ImGui::Text("Destroyed          : %s",object->IsDestroyed() ? "yes" : "no");

    if (Bone* bone = dynamic_cast<Bone*>(object)){
        ImGui::SeparatorText("Bone");
        ImGui::Text("bone_index %i, unpacked %i, node %i, initial length %.2f",
                    bone->bone_index,bone->bone_unpacked_index,bone->node_index,bone->initial_length);
        if (ImGui::TreeNode("inverse_bind_matrix")){
            ImGui::BeginDisabled();
            for (int i=0;i<4;i++){
                char label[8];
                sprintf(label,"V%i",i+1);
                ImGui::DragFloat4(label,(float*)&bone->inverse_bind_matrix.vertex[i],0.01f);
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("world_transform_scale_matrix")){
            fmat4 m = bone->GetWorldTransformScaleMatrix();
            ImGui::BeginDisabled();
            for (int i=0;i<4;i++){
                char label[8];
                sprintf(label,"V%i",i+1);
                ImGui::DragFloat4(label,(float*)&m.vertex[i],0.01f);
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
    }

    if (Skeleton* skeleton = dynamic_cast<Skeleton*>(object)){
        ImGui::SeparatorText("Skeleton");
        ImGui::Text("Root bone : %s",skeleton->root_bone_name.c_str());
        static char bone_name[64] = {};
        ImGui::InputText("Set root bone",bone_name,sizeof(bone_name));
        ImGui::SameLine();
        if (ImGui::Button("Apply")){
            skeleton->root_bone_name = bone_name;
        }
        std::vector<Bone*> bones;
        skeleton->GetAllBones(skeleton,bones);
        if (ImGui::TreeNode((void*)"bones","%i bones",(int)bones.size())){
            for (Bone* b:bones){
                ImGui::TextDisabled("%s",b->name.c_str());
            }
            ImGui::TreePop();
        }
    }
}

void Application::RenderEngineWindow(){
    ImGui::Begin("Engine",&f_show_engine_window);

    /*
        Every scene the app has, and switching between them - the panel twin of the scene_list and
        scene_set tools.

        FIRST IN THE PANEL, because every header below it (World Physics, Main Camera, and the
        Scene window's object tree) is about whichever scene is active, and which one that is
        should be read before any of them.

        A click only REQUESTS the switch. This runs on the render thread with physics_mutex held,
        and main_scene belongs to the physics thread - RequestActiveScene is the one safe way to
        ask, and it lands at the top of the next pass together with the app's
        OnActiveSceneChanged, exactly as a tool-driven switch does. It never waits, which matters
        here: waiting for the physics thread while holding the mutex it needs is a deadlock. So
        for the frame or two in between, the requested scene is marked as pending.

        Open by default only when there is more than one scene, which is when it has something
        to say.
    */
    if (!scenes.empty()){
        ImGuiTreeNodeFlags flags = (scenes.size() > 1) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        if (ImGui::CollapsingHeader("Scenes",flags)){
            Scene* pending = pending_scene.load();
            for (size_t i = 0; i < scenes.size(); i++){
                Scene* scene = scenes[i];
                if (!scene){
                    continue;
                }
                bool f_active = (scene == main_scene);
                //One label rather than a Selectable and a SameLine: a Selectable spans the whole
                //row, so anything placed after it lands past the right edge of the panel. The
                //"###" suffix keeps the id stable while the tick in the visible text changes.
                char label[160];
                if (scene == pending && !f_active){
                    snprintf(label,sizeof(label),"%s  (switching)###scene%zu",scene->name.c_str(),i);
                }else{
                    snprintf(label,sizeof(label),"%s  - %zu objects, tick %llu%s###scene%zu",
                             scene->name.c_str(),scene->objects.size(),
                             (unsigned long long)scene->GetPhysicsTick(),
                             scene->IsPhysicsPaused() ? ", paused" : "",i);
                }
                if (ImGui::Selectable(label,f_active) && !f_active){
                    //Pause carried across, as scene_set does by default - pause is per Scene, and
                    //switching out of a paused one into a running one is a surprise either way.
                    if (main_scene){
                        scene->PausePhysics(main_scene->IsPhysicsPaused());
                    }
                    RequestActiveScene(scene);
                }
            }
        }
    }

    if (main_scene){
#ifdef USE_PHYSICS
        UpdateUIWorldPhysics(main_scene->physics_world);
#endif
        int ui_camera_id = 0;
        UpdateUICameraControls(main_scene->camera,ui_camera_id);
    }

    if (ImGui::CollapsingHeader("Performance")){
        bool sync = renderer->GetVSync();
        if (ImGui::Checkbox("V-Sync",&sync)){
            renderer->SetVSync(sync);
        }
        ImGui::Text("Renderer Time : %8.1f us  (%5.2f ms)",renderer->tmr_frame->avg,renderer->tmr_frame->avg/1000.0f);
        ImGui::Text("Frame Time    : %8.2f FPS (%5.2f ms)",1000000.0f/tmr_render_loop->avg,tmr_render_loop->avg/1000.0f);
        ImGui::Text("Physics Loop  : %8.2f TPS (%5.2f ms)",1000000.0f/tmr_physics_loop->avg,tmr_physics_loop->avg/1000.0f);
        ImGui::Text("Physics Sleep : %8.1f us  (%5.2f ms)",tmr_physics_sleep->avg,tmr_physics_sleep->avg/1000.0f);
        ImGui::Text("Physics Time  : %8.1f us  (%5.2f ms)",tmr_physics->avg,tmr_physics->avg/1000.0f);
        //A CPU number on purpose - glReadPixels is a sync, so this is the frame stalling, not the
        //GPU working. See Renderer::tmr_pick_readback.
        if (renderer->tmr_pick_readback){
            ImGui::Text("Pick Readback : %8.1f us  (%5.2f ms)  CPU stall",
                        renderer->tmr_pick_readback->avg,renderer->tmr_pick_readback->avg/1000.0f);
        }

        /*
            Per-pass GPU cost, from GL_TIME_ELAPSED queries - the only numbers on this panel that
            are not the CPU. Everything above measures how long submitting the work took; these
            measure how long the GPU spent on it, which is a different quantity and usually the
            one being asked about.

            The total does NOT match "Renderer Time" above and is not meant to. Untimed GL work
            falls outside every scope, and the two clocks are measuring different machines. What
            the total is good for is A/B: change something, watch the pass it belongs to.
        */
        ImGui::SeparatorText("GPU passes (GL_TIME_ELAPSED)");
        if (!renderer->GPUTimersSupported()){
            ImGui::TextDisabled("Timer queries unavailable - see Renderer::InitGPUPassTimers");
        }else if (ImGui::BeginTable("gpu_passes",3,ImGuiTableFlags_RowBg|ImGuiTableFlags_SizingStretchProp)){
            ImGui::TableSetupColumn("Pass");
            ImGui::TableSetupColumn("avg ms");
            ImGui::TableSetupColumn("peak ms");
            ImGui::TableHeadersRow();
            double total_us = 0;
            for (int i=0;i<Renderer::GPU_PASS_COUNT;i++){
                const Renderer::GPUPassTimer* pass = renderer->GetGPUPassTimer(i);
                if (!pass || !pass->timer){
                    continue;
                }
                total_us += pass->timer->avg;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                //A pass that is switched off averages down to zero rather than freezing at what
                //it used to cost (Renderer::EndGPUFrame files the zeroes). Dimming it says which
                //of the two a 0.000 is without needing a fourth column to explain it.
                bool f_idle = (pass->timer->avg <= 0.0);
                if (f_idle){
                    ImGui::TextDisabled("%s",Renderer::GetGPUPassName(i));
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("    -");
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("    -");
                    continue;
                }
                ImGui::Text("%s",Renderer::GetGPUPassName(i));
                ImGui::TableNextColumn();
                ImGui::Text("%7.3f",pass->timer->avg/1000.0);
                ImGui::TableNextColumn();
                ImGui::Text("%7.3f",pass->timer->max/1000.0);
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Total (timed)");
            ImGui::TableNextColumn();
            ImGui::Text("%7.3f",total_us/1000.0);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("");
            ImGui::EndTable();
        }
        if (main_scene && main_scene->renderer){
            ImGui::Text("Renderable Objects : %i",(int)main_scene->renderer->renderable_objects.size());
            ImGui::Text("Unique Meshes      : %i",(int)main_scene->renderer->unique_meshes.size());
            ImGui::Text("Batches            : %i",(int)main_scene->renderer->unique_mesh_batches.size());
        }
        if (main_scene){
            ImGui::Text("Physics Tick       : %llu",(unsigned long long)main_scene->GetPhysicsTick());
            ImGui::Text("Pending Commands   : %i",main_scene->GetPendingCommands());
            ImGui::Text("Applied Commands   : %u",main_scene->GetAppliedCommandSequence());
        }
    }

    if (ImGui::CollapsingHeader("Renderer")){
        ImGui::Checkbox("Normal mapping",&renderer->f_normal_mapping);
        ImGui::Checkbox("SSAO",&renderer->f_ssao);
        ImGui::Checkbox("Render skybox",&renderer->f_render_skybox);
        ImGui::Checkbox("Render reflections",&renderer->f_use_reflections);

        int view_buffer = renderer->view_buffer;
        if (ImGui::SliderInt("View buffer",&view_buffer,0,8)){
            renderer->SelectViewBuffer(view_buffer);
        }
        int num_samples = renderer->aa_samples;
        if (ImGui::SliderInt("MSAA samples",&num_samples,1,16)){
            renderer->SetNumAASamples(num_samples);
        }
        ImGui::SliderFloat("Alpha clip",&renderer->alpha_clip,0.0f,1.0f);

        //Only an app that called EnableFieldShadows has anything to show here, which is one of
        //them - the controls would otherwise be four dead widgets in every other app's panel.
        if (renderer->field_camera){
            ImGui::SeparatorText("Point light shadows (occluder field)");
            ImGui::Checkbox("Field shadows",&renderer->f_field_shadows);
            //Watch "Renderer Time" above while toggling that: it is the honest A/B for what the
            //extra geometry pass and the jump flood actually cost in this scene.
            ImGui::SliderInt("Max march steps",&renderer->field_shadow_steps,0,128);
            //How big the lamp is, in world units. It sets how fast a shadow edge softens with
            //distance from what casts it; 0 is a point source and a hard edge. This is the
            //scene's default - a light that has set its own Light::radius ignores it and will
            //not move with this slider.
            ImGui::SliderFloat("Default light radius",&renderer->field_light_radius,0.0f,2.0f);
            ImGui::SliderFloat("Normal bias",&renderer->field_normal_bias,0.0f,0.5f);
            ImGui::Text("Field map: %i x %i",renderer->field_texture_size,renderer->field_texture_size);
        }
    }

    if (ImGui::CollapsingHeader("Input")){
        ImGui::Text("Window in focus        : %s",main_window->f_has_focus ? "yes" : "no");
        ImGui::Text("Mouse over window      : %s",main_window->inputcontroller->IsMouseOverWindow() ? "yes" : "no");
        ImGui::Text("ImGui.WantCaptureMouse : %s",ImGui::GetIO().WantCaptureMouse ? "yes" : "no");
        if (main_scene && main_scene->inputcontroller){
            vec3 hov_normal = main_scene->inputcontroller->GetHoveredNormal();
            vec3 hov_pos = main_scene->inputcontroller->GetHoveredPosition();
            ImGui::Text("Normal at mouse   : %.3f, %.3f, %.3f",hov_normal.x,hov_normal.y,hov_normal.z);
            ImGui::Text("Position at mouse : %.3f, %.3f, %.3f",hov_pos.x,hov_pos.y,hov_pos.z);
        }
        ImGui::Text("Hovered object    : %s",hovered_object ? hovered_object->name.c_str() : "none");
    }

    if (ImGui::CollapsingHeader("Window")){
        ImGui::Text("Current size : %i x %i",main_window->width,main_window->height);
    }

    if (assetmanager && ImGui::CollapsingHeader("Assets")){
        //With ids, because that is how object_spawn and any SimCommand refers to an asset - see
        //AssetIDFromName in AssetManager.h for why it is a hash of the name.
        for (Asset* asset:assetmanager->assets){
            ImGui::Text("%-28s %10u",asset->name.c_str(),asset->id);
        }
    }

    if (ImGui::CollapsingHeader("Materials")){
        ImGui::Text("%i materials",(int)renderer->GetNumMaterials());
        int n = 0;
        for (Material& material:renderer->materials){
            ImGui::PushID(n++);
            if (ImGui::TreeNode((void*)"mat","%s",material.name.c_str())){
                ImGui::TextDisabled("diffuse_texture %i",material.glsl_material.diffuse_texture);
                ImGui::DragFloat("Metallic",(float*)&material.glsl_material.metallic,0.01f,0,1);
                ImGui::DragFloat("Roughness",(float*)&material.glsl_material.roughness,0.01f,0,1);
                ImGui::DragFloat("Brightness",(float*)&material.glsl_material.brightness,0.01f,0,10);
                ImGui::ColorEdit4("Colour",(float*)&material.glsl_material.color,ImGuiColorEditFlags_DisplayRGB);
                //Emission: the colour comes in from glTF's emissiveFactor and is clamped 0..1, so
                //the strength next to it is what makes something actually glow. Both are edited
                //in place on renderer->materials, which UploadMaterials rebuilds the SSBO from
                //every frame, so edits show up immediately.
                ImGui::ColorEdit3("Emissive",(float*)&material.glsl_material.emissive,ImGuiColorEditFlags_DisplayRGB);
                ImGui::DragFloat("Emissive Strength",(float*)&material.glsl_material.emissive.w,0.05f,0,20);
                //Unlit: the surface IS its colour, with every light, shadow and reflection above
                //skipped. Here as well as in material_t because "why is this black" is asked at
                //this panel, and the answer - a metallic surface in a scene with nothing to
                //reflect - is one checkbox away from being ruled out.
                bool f_unlit = (material.glsl_material.f_unlit != 0);
                if (ImGui::Checkbox("Unlit",&f_unlit)){
                    material.glsl_material.f_unlit = f_unlit ? 1 : 0;
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Textures")){

    }

    ImGui::End();
}

//For showing how RRandom would work.
void Application::RenderRandTestWindow(){
    static float histogram_arr[256];
    static int histogram_count = 0;

    ImGui::Begin("Random Test Suite");
    ImGui::Text("This is for testing our own random functions. Neat?");
    if (rrand == NULL){
        ImGui::Text("rrand has not been initialised\n");
        if (ImGui::Button("Generate 512x512")){
            rrand = new RRandom();
            rrand->Generate(512,512);
        }
        ImGui::End();
        return;
    }



    uint8_t r = rrand->Get_uint8();
    float f = 0;
    int s = 1;
    for (int i = 0;i<s;i++){
        f += rrand->Get_uint8();
    }
    f/= s;

    histogram_arr[histogram_count++] = f;//rand() % 256;

    histogram_count = histogram_count % 256;
    //UI


    ImGui::PlotHistogram("Histogram", histogram_arr, IM_ARRAYSIZE(histogram_arr), 0, NULL, 0.0f, 255.0f, ImVec2(0, 80.0f));

    static int rand_int = 0;
    if (ImGui::Button("Get Random Int")){
        rand_int = rrand->GetInt();
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_int);

    static int rand_limit = 0;
    static int minmax[2] = {0,1};
    ImGui::SliderInt2("Int Min / Max",minmax,-100,100);
    if (ImGui::Button("Get Random Int Between")){
        rand_limit = rrand->GetInt(minmax[0],minmax[1]);
    }
    ImGui::SameLine();
    ImGui::Text("Random Int: %i",rand_limit);

    static float rand_float = 0;
    static float fminmax[2] = {0,1};
    ImGui::SliderFloat2("Float Min / Max",fminmax,-100,100);
    if (ImGui::Button("Get Random Float Between")){
        rand_float = rrand->GetFloat(fminmax[0],fminmax[1]);
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
            //float sample = rrand->GetFloat(-10,10);
            float sample = rrand->GetNormalFloat(0,4);
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

void Application::CheckObjectSelection(){
    hovered_object = NULL;
    InputController* input = main_scene->inputcontroller;

    if (input->IsMouseOverWindow() == false){
        return;
    }

    //Cursor over an ImGui window: nothing in the scene is hovered, and no click here belongs to
    //the world. Returning is the point - this used to be an `if (!WantCaptureMouse)` around the
    //refresh below ONLY, and then fell through to the loop regardless, which went on matching
    //the STALE hovered_objid left over from the last frame the cursor was over the scene. So
    //clicking a button on a panel selected - and logged a "Clicked on ID" for - whatever object
    //happened to be under the cursor before it moved onto the panel.
    if (ImGui::GetIO().WantCaptureMouse){
        hovered_objid = OBJECTID_INVALID;
        //A press that began on an object and is released over a panel is abandoned, rather than
        //completing as a click on that object once the button comes up.
        dragged_objid = OBJECTID_INVALID;
        return;
    }

    hovered_objid = input->GetHoveredObjectID();
    if ((hovered_objid == OBJECTID_INVALID) && input->WasKeyReleased(INPUT_CLICK_LEFT)){
        selected_object = NULL;
        return;
    }

    for (Object* object:renderer->renderable_objects){
        if (object->GetID() == hovered_objid){
            hovered_object = object;
        }
        if (input->IsKeyDown(INPUT_CLICK_LEFT) && (object->GetID() == hovered_objid)){
            dragged_objid = hovered_objid;
            //debug->Info("dragged_objid on ID: %3i \n",hovered_objid);
        }else if (input->WasKeyReleased(INPUT_CLICK_LEFT) && (object->GetID() == dragged_objid)){
            vec3 p = object->GetPosition();
            debug->Info("Clicked on ID: %3i Object Pos: %.2f %.2f %.2f\n",hovered_objid,p.x,p.y,p.z);
            selected_object = object;
            dragged_objid = OBJECTID_INVALID;
            //clicked_empty = false;
        }
    }
}

/*
    Does the debug UI want the mouse (or the keyboard)?

    ImGui::GetIO().WantCaptureMouse behind a name the rest of the engine can call unconditionally -
    game logic asks before acting on a click, so that dragging a slider does not also swing the
    camera. The twin in ApplicationDebugUI_none.cpp answers false: with no panels on screen the
    click belongs to the game.

    This is the only piece of ImGui that app GAME code (as opposed to app panel code) was reaching
    for directly, in apps/ship and apps/tank. Giving it a name is what let those call sites stay
    unguarded and keep working either way.
*/
bool Application::UIWantsMouse(){
    return ImGui::GetIO().WantCaptureMouse;
}

bool Application::UIWantsKeyboard(){
    return ImGui::GetIO().WantCaptureKeyboard;
}
