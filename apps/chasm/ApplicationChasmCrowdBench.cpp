#include "ApplicationChasm.h"

#ifdef USE_MCP
//Only this from the server: see the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

#include "Debug.h"

#include <math.h>

static Debugger* debug = new Debugger("ChasmCrowd",DEBUG_ALL);

/*
    THE CROWD BENCH: what skinned villagers would cost, measured before anyone rigs the other three.

    A measurement, not a feature. The people are drawn as static figures (ApplicationChasmWorkersView.cpp);
    this stands N copies of the user's rigged adult_male in a grid around the camera target, every one
    walking in place, so renderer_timings can be read at 0, 50, 100, 200 and 400 of them.

    It is built the way skinned villagers would be: ONE skinned mesh shared by every figure - so the
    renderer draws them all in one instanced call per pass, bones in the SSBO - and per figure its own
    Skeleton (the bones are Objects, posed on the physics thread's tick) with its own copy of the clip,
    because Object::AddAnimation binds a clip's tracks to the bones of the skeleton it is added to
    (apps/bomber, BuildSkinnedActor). Each starts at its own point in the clip, so the crowd does not
    walk in step.

    Figures past the count asked for are hidden AND frozen (f_animation_override with no ticks), so a
    lower count really costs what it says: Scene::UpdateAnimations walks every object, shown or not.

    `static` stands the same count of PLAIN Objects instead, each with the static figure the people are
    drawn with today (worker_meshes, no load), so the difference between the two runs is what being
    skinned costs - and what is left is what any object per villager costs.
*/

#define CROWD_FIGURE        "adult_male"        //and whatever skin it is bound to (GLTFLoader::GetNodeSkinName)
#define CROWD_CLIP          "Walking"
#define CROWD_SCALE         0.46f       //PERSON_MODEL_SCALE's: modelled 1.87 tall, drawn the walker's 0.86
#define CROWD_SPACING       1.2f        //world units between neighbours in the grid
#define CROWD_MAX           2000

//RENDER THREAD, from LoadProps: the shared skinned mesh. Without it the bench says so and does nothing.
void ApplicationChasm::BuildCrowdBench(){
    std::vector<Material> materials;
    Mesh* mesh = gltfloader.GetMeshFromNode(CROWD_FIGURE,&materials,true);
    if (!mesh || !mesh->IsSkinnedMesh()){
        debug->Warn("No skinned '%s' in chasm_props.glb - the crowd bench has nothing to draw\n",CROWD_FIGURE);
        return;
    }
    //Held for good, as the worker meshes are: the last Object::SetMesh release would delete it.
    mesh->Retain();
    crowd_mesh = mesh;
    debug->Info("Crowd bench: '%s' skinned, %u vertices\n",CROWD_FIGURE,crowd_mesh->num_vertices);
}

/*
    RENDER THREAD. Lays the crowd out again whenever a request comes in: builds the skeletons it lacks
    (GetSkeleton and the clip make Objects only, nothing GL), then shows, places and wakes the first
    `want` and hides and freezes the rest - under the tick lock, since the physics thread animates them.
*/
void ApplicationChasm::UploadCrowdBench(){
    int want;
    vec3 centre;
    int request;
    bool f_static;
    {
        std::lock_guard<std::mutex> lock(crowd_mutex);
        want = crowd_want;
        centre = crowd_centre;
        request = crowd_request;
        f_static = f_crowd_static;
    }
    if (request == crowd_applied || !crowd_mesh){
        return;
    }
    crowd_applied = request;

    std::vector<Object*> made;
    std::vector<Object*> made_plain;
    while (f_static && (int)(crowd_plain.size() + made_plain.size()) < want){
        Object* o = new Object();
        o->name = "Crowd plain " + std::to_string(crowd_plain.size() + made_plain.size());
        o->SetMesh(worker_meshes[0]);       //the man, carrying nothing - already at PERSON_MODEL_SCALE
        o->SetMaterialSlot(0,palette_material);
        o->SetPickability(false);
        o->SetVisualOnly(true);
        o->SetVisibility(false);
        made_plain.push_back(o);
    }
    std::string skin = gltfloader.GetNodeSkinName(CROWD_FIGURE);
    while (!f_static && (int)(crowd_objects.size() + made.size()) < want){
        Skeleton* skeleton = skin.empty() ? NULL : gltfloader.GetSkeleton(skin.c_str(),assetmanager);
        Animation* walk = skeleton ? gltfloader.LoadAnimation(CROWD_CLIP) : NULL;
        if (!skeleton || !walk){
            debug->Err("Crowd bench: no skin on '%s' or no clip '%s' in chasm_props.glb\n",CROWD_FIGURE,CROWD_CLIP);
            break;
        }
        skeleton->name = "Crowd " + std::to_string(crowd_objects.size() + made.size());
        skeleton->SetMesh(crowd_mesh);
        skeleton->SetMaterialSlot(0,palette_material);
        skeleton->SetPickability(false);
        skeleton->SetVisualOnly(true);
        skeleton->SetVisibility(false);
        skeleton->SetScale(vec3(CROWD_SCALE,CROWD_SCALE,CROWD_SCALE));
        walk->looped = true;
        skeleton->AddAnimation(walk);
        skeleton->SwitchToAnimation(walk);
        made.push_back(skeleton);
    }

    std::shared_ptr<const ChasmWorld> w = GetWorld();
    int side = (int)ceilf(sqrtf((float)want));
    world_scene->AtTickBoundary([&](){
        for (Object* o : made){
            world_scene->AddObject(o);
            crowd_objects.push_back(o);
        }
        for (Object* o : made_plain){
            world_scene->AddObject(o);
            crowd_plain.push_back(o);
        }
        //The pool not asked for, all hidden (and the skeletons frozen); then the one asked for.
        std::vector<Object*>& off = f_static ? crowd_objects : crowd_plain;
        for (Object* o : off){
            o->SetVisibility(false);
            o->f_animation_override = true;
            o->animation_override_ticks = 0;
        }
        std::vector<Object*>& pool = f_static ? crowd_plain : crowd_objects;
        for (size_t i = 0; i < pool.size(); i++){
            Object* o = pool[i];
            bool f_shown = (int)i < want;
            o->SetVisibility(f_shown);
            o->f_animation_override = !f_shown;
            o->animation_override_ticks = 0;
            if (!f_shown){
                continue;
            }
            float col = (float)((int)i % side) - 0.5f * (float)(side - 1);
            float row = (float)((int)i / side) - 0.5f * (float)(side - 1);
            vec2 p(centre.x + col * CROWD_SPACING,centre.z + row * CROWD_SPACING);
            float y = centre.y;
            if (w){
                GridPick pick = w->picker->Pick(p);
                if (pick.f_hit){
                    y = w->terrain->GroundHeight(p,w->terrain->Height(pick.plot));
                }
            }
            o->SetPosition(vec3(p.x,y,p.y),false);
            //The golden angle for the heading and its fraction for the phase: no two neighbours alike.
            o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),(float)i * 2.3999632f),false);
            if (o->current_animation){
                o->current_animation->time_index = fmodf((float)i * 0.618034f,1.0f) * o->current_animation->duration;
            }
        }
    });
    const std::vector<Object*>& pool = f_static ? crowd_plain : crowd_objects;
    crowd_shown = want < (int)pool.size() ? want : (int)pool.size();
    Skeleton* first = crowd_objects.empty() ? NULL : dynamic_cast<Skeleton*>(crowd_objects[0]);
    debug->Info("Crowd bench: %i %s shown of %i built, %i bones each skinned\n",crowd_shown,
                f_static ? "static" : "skinned",(int)pool.size(),first ? first->num_bones : 0);
}

#ifdef USE_MCP
void ApplicationChasm::RegisterCrowdBenchTool(){
    MCPServer::Get()->RegisterTool("chasm_crowd_bench",
        "A MEASUREMENT, not gameplay (ApplicationChasmCrowdBench.cpp): stands `count` skinned adult_male "
        "figures walking in place in a grid around the camera target (or `x`,`z`), all sharing one mesh, "
        "each its own skeleton and copy of the Walking clip; 0 hides them all. Figures past the count are "
        "hidden and frozen, so they cost nothing. Read renderer_timings a second or two later: physics_us "
        "holds the clips' sampling, scene_draw_us/renderer_us the bones' upload, gpu_passes the drawing. "
        "`static` true stands plain Objects with today's static figure instead, for comparison.",
        json{
            {"type","object"},
            {"properties",{
                {"count",{{"type","integer"},{"description","figures to show, 0 to hide them all"}}},
                {"static",{{"type","boolean"},{"description","plain Objects with the static figure (default false)"}}},
                {"x",{{"type","number"},{"description","centre of the grid (default the camera target)"}}},
                {"z",{{"type","number"}}}
            }},
            {"required",json::array({"count"})}
        },
        [this](const json& args) -> json {
            if (!crowd_mesh){
                return json{{"error","no skinned adult_male in chasm_props.glb"}};
            }
            int count = args.value("count",0);
            count = count < 0 ? 0 : (count > CROWD_MAX ? CROWD_MAX : count);
            //camera_target belongs to the physics thread (ApplicationChasm.h, THREADS).
            vec3 target;
            main_scene->AtTickBoundary([&](){ target = camera_target; });
            if (args.contains("x") && args.contains("z")){
                target = vec3(args["x"].get<float>(),target.y,args["z"].get<float>());
            }
            bool f_static = args.value("static",false);
            {
                std::lock_guard<std::mutex> lock(crowd_mutex);
                crowd_want = count;
                crowd_centre = target;
                f_crowd_static = f_static;
                crowd_request++;
            }
            return json{{"count",count},{"static",f_static},{"centre",{target.x,target.y,target.z}},
                        {"vertices",crowd_mesh->num_vertices},
                        {"note","laid out on the render thread's next frame"}};
        });
}
#endif
