#include "ApplicationChasm.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "AssetManager.h"
#include "Debug.h"
#include <algorithm>
#include <math.h>

static Debugger* debug = new Debugger("ChasmZombies",DEBUG_ALL);

/*
    ZOMBIES IN THE APP (docs/zombies_plan.md, step 1): the debug tools that drop them - Z under the cursor, the panel,
    the chasm_zombie tool - each a recorded CHASM_CMD_ZOMBIE, and the zombies drawn. Their rules are the economy's
    (Economy.h, ZOMBIES; Zombies.cpp).

    DRAWN: the props file's `zombie` at the people's scale. RIGGED (2026-10-18: `zombie_armature`, a copy of the
    man's), as the people are - a skeleton per zombie sharing the one skinned mesh, walking the man's walk while its
    route has a leg left, at that leg's speed, and standing in his idle otherwise (ApplicationChasmWorkersView.cpp).
    Without a rig, one instance set for all of them, rebuilt every frame there are any, and it glides. Either way not
    drawn under the clouds where they are shown, so one comes out of them as it walks in.
*/

#define ZOMBIE_MODEL_SCALE  0.46f   //the people's (PERSON_MODEL_SCALE): modelled as tall as the man, 1.85

//Its clips, the man's, by their names in the props file - nothing of his bow.
#define ZOMBIE_CLIP_NONE    0       //the rig's bind pose, and nothing to sample each tick
#define ZOMBIE_CLIP_WALK    1
#define ZOMBIE_CLIP_IDLE    2
#define ZOMBIE_WALK_CLIP    "Walking"
#define ZOMBIE_IDLE_CLIP    "Idle"  //stood in when the file has one (f_worker_idle_clip); the bind pose otherwise
#define ZOMBIE_CLIP_BLEND   0.25f   //seconds a change of clip crossfades over, as a person's
#define ZOMBIE_WALK_SPEED   0.81f   //its walk's pace for a rig without feet named as the man's: his

//ApplicationChasmWorkersView.cpp: a borrowed track played as turns only, and a walk's pace from the rig's own feet.
void KeepOwnPositions(Animation* clip);
float MeasureWalkSpeed(Skeleton* probe, Animation* walk);

/*
    RENDER THREAD, from LoadProps: the zombie's rig, if its node has one - the skinned mesh every zombie shares, and the
    pace its walk carries it at, from its own feet as the people's is. Its vertices are in the file's space, where
    zombie_armature stood (x -5); the skin takes that back out, as it does the woman's (GLTFLoader::GetSkeleton).
*/
void ApplicationChasm::BuildZombieSkin(){
    std::string skin = gltfloader.GetNodeSkinName(CHASM_ZOMBIE_ASSET);
    if (skin.empty()){
        debug->Info("'%s' has no rig - zombies are drawn static\n",CHASM_ZOMBIE_ASSET);
        return;
    }
    Mesh* mesh = gltfloader.GetMeshFromNode(CHASM_ZOMBIE_ASSET,NULL,true);
    Skeleton* probe = (mesh && mesh->IsSkinnedMesh()) ? gltfloader.GetSkeleton(skin.c_str(),assetmanager) : NULL;
    if (!probe){
        debug->Warn("'%s' has the skin '%s' but no skinned mesh or skeleton loads - drawn static\n",
                    CHASM_ZOMBIE_ASSET,skin.c_str());
        return;
    }
    mesh->Retain();     //held for good, as the people's are: every zombie's skeleton wears it
    zombie_skin_mesh = mesh;
    zombie_skin_name = skin;
    Animation* walk = gltfloader.LoadAnimation(ZOMBIE_WALK_CLIP);
    float measured = 0.0f;
    if (walk){
        walk->looped = true;
        probe->AddAnimation(walk);
        KeepOwnPositions(walk);
        measured = MeasureWalkSpeed(probe,walk);
    }
    zombie_walk_speed = (measured > 0.01f) ? measured : ZOMBIE_WALK_SPEED;
    debug->Info("'%s' is rigged (%s, %i bones): drawn skinned, walking %.2f units a second at rate 1%s\n",
                CHASM_ZOMBIE_ASSET,skin.c_str(),probe->num_bones,zombie_walk_speed,
                measured > 0.01f ? "" : " (not measured: no feet named as the man's)");
    delete probe;   //never in the scene; its clip is left, a one-off
}

//The n'th zombie skeleton, wearing the shared mesh, with its own copy of each clip - hidden, in no scene yet.
Object* ApplicationChasm::BuildZombieSkeleton(size_t n){
    Skeleton* s = gltfloader.GetSkeleton(zombie_skin_name.c_str(),assetmanager);
    if (!s){
        return NULL;
    }
    s->name = "zombies " + std::to_string(n);      //"zombies": what the title's diorama leaves out (DioramaOptions)
    s->SetMesh(zombie_skin_mesh);
    s->SetMaterialSlot(0,palette_material);
    s->SetPickability(false);
    s->SetVisualOnly(true);
    s->SetVisibility(false);
    s->SetScale(vec3(ZOMBIE_MODEL_SCALE,ZOMBIE_MODEL_SCALE,ZOMBIE_MODEL_SCALE));
    //Its own channels where the clip has them (the export keys Walking on zombie_armature too), the man's by name else.
    const char* names[2] = {ZOMBIE_WALK_CLIP,f_worker_idle_clip ? ZOMBIE_IDLE_CLIP : NULL};
    for (const char* name : names){
        Animation* clip = name ? gltfloader.LoadAnimation(name) : NULL;
        if (!clip){
            continue;
        }
        clip->looped = true;
        s->AddAnimation(clip);
        KeepOwnPositions(clip);
    }
    s->animation_transition_time_max = ZOMBIE_CLIP_BLEND;
    return s;
}

void ApplicationChasm::SubmitZombies(int plot, int count){
    SimCommand cmd;
    cmd.type = CHASM_CMD_ZOMBIE;
    cmd.flags = SIM_CMD_FLAG_RECORD;
    cmd.subtype = (uint32_t)std::max(0,plot);
    cmd.value[0] = (float)std::max(0,count);
    main_scene->SubmitCommand(cmd);
}

//RENDER THREAD, from PreRender.
void ApplicationChasm::UploadZombies(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    if (!w || !f_zombie_loaded){
        return;
    }
    if (!zombie_set){
        world_scene->AtTickBoundary([&](){
            zombie_set = assetmanager->GetObjectFromAsset(CHASM_ZOMBIE_ASSET);
            if (zombie_set){
                zombie_set->name = "zombies";
                //The GLB names a material it carries no image for; the palette is ours.
                zombie_set->SetMaterialSlot(0,palette_material);
                zombie_set->SetPickability(false);
                zombie_set->SetVisualOnly(true);
                zombie_set->SetInstances(std::vector<fmat4>());
                zombie_set->SetVisibility(false);
                world_scene->AddObject(zombie_set);
            }
            //Their arrows: the range's, once its props are loaded (BuildRangeScene) - in the air whole, else a stub.
            for (int k = 0; k < 2 && range_mesh[2] && range_mesh[3]; k++){
                Object* o = new Object();
                o->name = (k == 0) ? "Zombie arrows flying" : "Zombie arrows stuck";
                o->SetMesh(range_mesh[(k == 0) ? 3 : 2]);
                o->SetMaterialSlot(0,palette_material);
                o->SetVisualOnly(true);
                o->SetPickability(false);
                o->SetInstances(std::vector<fmat4>());
                o->SetVisibility(false);
                world_scene->AddObject(o);
                zombie_arrow_set[k] = o;
            }
        });
        if (!zombie_set){
            return;
        }
    }
    bool f_ours = e && e->world == w;
    std::shared_ptr<const Exploration> ex = (PlayMode() || f_view_clouds) ? GetExploration() : nullptr;
    auto hidden = [&](const vec2& p){
        return ex && !ex->Empty() && !ex->Explored(p);
    };
    std::vector<ZombieShown> drawn;
    std::vector<std::pair<uint32_t,vec3>> feet;     //...and by id, for the arrows in them
    for (size_t i = 0; f_ours && i < e->zombies.size(); i++){
        const EconomyZombie& m = e->zombies[i];
        vec2 body = EconomyBody(m);     //SPACING: where it stands, off its route
        if (hidden(body)){
            continue;
        }
        GridPick p = w->picker->Pick(body);
        if (!p.f_hit || p.plot < 0){
            continue;
        }
        float ground = w->terrain->GroundHeight(body,w->terrain->Height(p.plot));
        vec2 facing = EconomyZombieFacing(m);
        //Stood by whom it is after - pushed round him by the others (SPACING) - it faces him, not the way it came.
        if (m.target && m.seg >= (int)m.route.size() - 1){
            for (const EconomyWorker& k : e->workers){
                vec2 to = EconomyBody(k) - body;
                if (k.id == m.target && to.length() > 1e-3f){
                    facing = to * (1.0f / to.length());
                }
            }
        }
        drawn.push_back({&m,vec3(body.x,ground,body.y),quat(vec3(0.0f,1.0f,0.0f),atan2f(facing.x,facing.y))});
        feet.emplace_back(m.id,vec3(body.x,ground,body.y));
    }
    if (zombie_skin_mesh){
        UploadZombieSkins(drawn);
    }else if (!drawn.empty() || zombie_set->IsVisible()){
        std::vector<fmat4> zombies;
        for (const ZombieShown& d : drawn){
            zombies.push_back(Object::ComposeTransformScale(d.at,d.turn,
                                                            vec3(ZOMBIE_MODEL_SCALE,ZOMBIE_MODEL_SCALE,ZOMBIE_MODEL_SCALE)));
        }
        zombie_set->SetInstances(std::move(zombies));
        zombie_set->SetVisibility(!drawn.empty());
    }

    /*
        THEIR ARROWS (zombies_plan.md, step 3), from the rules' (EconomyArrow): one in the air along its way from the bow,
        rising a little (RANGE_ARROW_ARC) as the range's do; a hit stuck in its zombie where it went in, following it,
        until it falls; a miss in the ground where it came down. Under the clouds, as the zombies are, none.
    */
    if (!zombie_arrow_set[0] || !zombie_arrow_set[1]){
        return;
    }
    const vec3 up(0.0f,1.0f,0.0f);
    std::vector<fmat4> flying;
    std::vector<fmat4> stuck;
    for (size_t i = 0; f_ours && i < e->arrows.size(); i++){
        const EconomyArrow& a = e->arrows[i];
        vec3 way = a.to - a.from;
        float len = way.length();
        if (len < 1e-3f){
            continue;
        }
        way = way * (1.0f / len);
        if (EconomyArrowFlying(a)){
            float f = (float)a.flown / (float)std::max(1,a.ticks);
            vec3 point = a.from + (a.to - a.from) * f + up * (RANGE_ARROW_ARC * len * 4.0f * f * (1.0f - f));
            vec3 dir = way + up * (RANGE_ARROW_ARC * 4.0f * (1.0f - 2.0f * f));
            dir.normalize();
            if (fabsf(dir.y) < 0.999f && !hidden(vec2(point.x,point.z))){
                flying.push_back(Object::ComposeTransformScale(point - dir * RANGE_ARROW_FULL,TurnZTo(dir,up),
                                                               vec3(1.0f,1.0f,1.0f)));
            }
            continue;
        }
        vec3 point = a.to;
        vec3 dir = way;
        if (a.f_hit){
            //In it where it went in - across and up from its chest, across square to the way it came - as it walks.
            auto it = std::find_if(feet.begin(),feet.end(),[&](const std::pair<uint32_t,vec3>& f){ return f.first == a.zombie; });
            if (it == feet.end()){
                continue;
            }
            vec3 at = it->second;
            vec2 from(at.x - a.from.x,at.z - a.from.z);
            float l = from.length();
            vec2 across = (l > 1e-4f) ? vec2(-from.y,from.x) * (1.0f / l) : vec2(1.0f,0.0f);
            point = vec3(at.x + across.x * a.at.x,at.y + ARROW_CHEST + a.at.y,at.z + across.y * a.at.x);
        }else{
            //In the ground, the way it was coming down.
            dir = way - up * (RANGE_ARROW_ARC * 4.0f);
            dir.normalize();
            if (hidden(vec2(point.x,point.z))){
                continue;
            }
        }
        if (fabsf(dir.y) < 0.999f){
            stuck.push_back(Object::ComposeTransformScale(point - dir * (RANGE_ARROW_HALF * 0.7f),TurnZTo(dir * -1.0f,up),
                                                          vec3(1.0f,1.0f,1.0f)));
        }
    }
    bool f_any = !flying.empty() || !stuck.empty();
    if (f_any || f_zombie_arrows_shown){
        f_zombie_arrows_shown = f_any;
        zombie_arrow_set[0]->SetVisibility(!flying.empty());
        zombie_arrow_set[1]->SetVisibility(!stuck.empty());
        zombie_arrow_set[0]->SetInstances(std::move(flying));
        zombie_arrow_set[1]->SetInstances(std::move(stuck));
    }
}

/*
    RENDER THREAD, from UploadZombies once the zombie is rigged. Each zombie drawn keeps the skeleton it had, found by its
    id; one without takes a free one, or one is made. The rest - fallen, cleared, or gone under the clouds - are hidden
    and given no clip, which costs a tick nothing. As UploadWorkers does: placed here every frame, while a change of clip
    is set under the tick's lock, since the physics thread's tick is what plays it, and only when it changes.
*/
void ApplicationChasm::UploadZombieSkins(const std::vector<ZombieShown>& shown){
    const size_t none = (size_t)-1;
    std::vector<size_t> slot(shown.size(),none);
    for (size_t s = 0; s < zombie_skins.size(); s++){
        bool f_kept = false;
        for (size_t i = 0; i < shown.size() && !f_kept; i++){
            if (zombie_skins[s].id == shown[i].m->id){
                slot[i] = s;
                f_kept = true;
            }
        }
        if (!f_kept){
            zombie_skins[s].id = 0;
        }
    }
    std::vector<Object*> made;
    size_t free_slot = 0;
    for (size_t i = 0; i < shown.size(); i++){
        if (slot[i] != none){
            continue;
        }
        while (free_slot < zombie_skins.size() && zombie_skins[free_slot].id != 0){
            free_slot++;
        }
        if (free_slot == zombie_skins.size()){
            ZombieSkin z;
            z.figure = BuildZombieSkeleton(zombie_skins.size());
            if (!z.figure){
                break;      //its rig loaded at Init, so this does not happen; the rest go undrawn if it does
            }
            made.push_back(z.figure);
            zombie_skins.push_back(z);
        }
        zombie_skins[free_slot].id = shown[i].m->id;
        slot[i] = free_slot;
    }

    struct ClipChange{
        size_t slot;
        int clip;
        float rate;
        float phase;
    };
    std::vector<ClipChange> changes;
    auto set_clip = [&](size_t s, int clip, float rate, float phase){
        const ZombieSkin& z = zombie_skins[s];
        if (clip != z.clip || fabsf(rate - z.rate) > 0.01f * z.rate){
            changes.push_back({s,clip,rate,phase});
        }
    };
    for (size_t s = 0; s < zombie_skins.size(); s++){
        if (zombie_skins[s].id == 0){
            zombie_skins[s].figure->SetVisibility(false);
            set_clip(s,ZOMBIE_CLIP_NONE,1.0f,0.0f);
        }
    }
    for (size_t i = 0; i < shown.size(); i++){
        if (slot[i] == none){
            continue;
        }
        const EconomyZombie& m = *shown[i].m;
        Object* s = zombie_skins[slot[i]].figure;
        s->SetPosition(shown[i].at,false);
        s->SetRotation(shown[i].turn,false);
        s->SetVisibility(true);
        //Walking while its route has a leg left, at that leg's speed, so its feet are planted; standing otherwise -
        //biting too, which has no clip of its own yet. Each on its own step.
        float phase = fmodf((float)m.id * 0.618034f,1.0f);
        if (m.seg + 1 < (int)m.route.size() && m.seg < (int)m.speed.size()){
            set_clip(slot[i],ZOMBIE_CLIP_WALK,m.speed[m.seg] / zombie_walk_speed,phase);
        }else{
            set_clip(slot[i],f_worker_idle_clip ? ZOMBIE_CLIP_IDLE : ZOMBIE_CLIP_NONE,1.0f,phase);
        }
    }
    if (made.empty() && changes.empty()){
        return;
    }
    world_scene->AtTickBoundary([&](){
        for (Object* s : made){
            world_scene->AddObject(s);
        }
        for (const ClipChange& c : changes){
            ZombieSkin& z = zombie_skins[c.slot];
            Object* s = z.figure;
            if (c.clip != z.clip){
                Animation* clip = (c.clip == ZOMBIE_CLIP_WALK) ? s->FindAnimation(ZOMBIE_WALK_CLIP)
                                : (c.clip == ZOMBIE_CLIP_IDLE) ? s->FindAnimation(ZOMBIE_IDLE_CLIP) : NULL;
                //A crossfade from one clip to another, as a person's; to none or from none, at once.
                if (!clip || z.clip == ZOMBIE_CLIP_NONE || !s->TransitionToAnimation(clip)){
                    s->SwitchToAnimation(clip);
                }
                if (clip){
                    clip->time_index = c.phase * clip->duration;
                }
                z.clip = c.clip;
            }
            s->SetAnimationRate(c.rate);
            z.rate = c.rate;
        }
    });
}

#ifdef USE_IMGUI
//The panel (debug builds): how many there are and whom each is after; a number of them dropped on the selected plot, or
//every one taken off the map.
void ApplicationChasm::RenderZombiePanel(){
#ifdef DEBUG
    if (!ImGui::CollapsingHeader("Zombies")){
        return;
    }
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    bool f_ours = e && w && e->world == w;
    int count = f_ours ? (int)e->zombies.size() : 0;
    ImGui::Text("%i about",count);
    ImGui::TextDisabled("Z drops one on the plot under the cursor, in debug and in play mode");
    GridPick selected = GetSelectedPick();
    ImGui::SetNextItemWidth(90.0f);
    ImGui::InputInt("##zombie_count",&zombie_edit_count);
    zombie_edit_count = std::max(1,std::min((int)ZOMBIE_SPAWN_MOST,zombie_edit_count));
    ImGui::SameLine();
    bool f_plot = selected.f_hit && selected.plot >= 0;
    if (!f_plot){
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton("Drop on the selected plot")){
        SubmitZombies(selected.plot,zombie_edit_count);
    }
    if (!f_plot){
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (count == 0){
        ImGui::BeginDisabled();
    }
    if (ImGui::SmallButton("Clear all##zombies")){
        SubmitZombies(0,0);
    }
    if (count == 0){
        ImGui::EndDisabled();
    }
    std::string refusal;
    {
        std::lock_guard<std::mutex> lock(grid_mutex);
        refusal = zombie_last_refusal;
    }
    if (!refusal.empty()){
        ImGui::TextColored(ImVec4(1.0f,0.45f,0.35f,1.0f),"last refused: %s",refusal.c_str());
    }
    if (!f_ours){
        return;
    }
    for (const EconomyZombie& m : e->zombies){
        const EconomyWorker* target = NULL;
        for (const EconomyWorker& k : e->workers){
            if (k.id == m.target){
                target = &k;
            }
        }
        if (target){
            ImGui::Text("zombie %u (%i of %i): after %s, %.1f away%s",m.id,m.health,(int)ZOMBIE_HEALTH,
                        EconomyPersonName(*target).c_str(),(double)(EconomyBody(*target) - EconomyBody(m)).length(),
                        m.bite > 0 ? ", biting" : "");
        }else{
            ImGui::TextDisabled("zombie %u (%i of %i): %s",m.id,m.health,(int)ZOMBIE_HEALTH,
                                m.f_lost ? "nobody near it can get to" : "nobody out");
        }
    }
#endif
}
#endif

#ifdef USE_MCP
void ApplicationChasm::RegisterZombieTools(){
    MCPServer::Get()->RegisterTool("chasm_zombie",
        "ZOMBIES (apps/chasm/docs/zombies_plan.md, steps 1-3): dropped by debug tools, each goes for the nearest person out "
        "of doors and on the ground it can walk to, walks after him and, within reach, bites him - and a grown-up hits back "
        "(a worker's `fighting` in chasm_economy). Archers and guards who stand shoot the nearest they see (a worker's "
        "`shooting`); `arrows` lists every arrow in the air or come down. op spawn: `count` "
        "(default 1, at most 40) dropped on the plot at x,z (or `plot`), spread out from its point; refused on the water, the "
        "mountain, a building or a wall (`refusal`). op clear: every zombie taken off the map. Both a RECORDED debug edit "
        "(CHASM_CMD_ZOMBIE), applied before this returns - as the Z key over the map and the panel's Zombies section do. op "
        "status (default): every zombie - id, x, z, plot, its body's `off` from its route, `health` (hit points), "
        "`next_bite_ticks` (0 none in reach), its target (person id and name, 0 none) and how far he is - route to route "
        "and `body_distance` - whether it is walking, and `lost` (it could get to none of the nearest at its last look, so "
        "looks less often).",
        json{
            {"type","object"},
            {"properties",{
                {"op",{{"type","string"},{"description","spawn, clear or status"}}},
                {"x",{{"type","number"}}},
                {"z",{{"type","number"}}},
                {"plot",{{"type","integer"}}},
                {"count",{{"type","integer"},{"description","spawn: how many, 1 to 40"}}}
            }}
        },
        [this](const json& args) -> json {
            std::shared_ptr<const ChasmWorld> w = GetWorld();
            if (!w){
                return json{{"error","no world"}};
            }
            std::string op = args.value("op",std::string("status"));
            json result = json::object();
            if (op == "spawn" || op == "clear"){
                int plot = args.value("plot",-1);
                int count = 0;
                if (op == "spawn"){
                    if (plot < 0 && args.contains("x") && args.contains("z")){
                        GridPick p = w->picker->Pick(vec2(args["x"].get<float>(),args["z"].get<float>()));
                        plot = p.f_hit ? p.plot : -1;
                    }
                    if (plot < 0){
                        return json{{"error","spawn: no plot at x,z (or give `plot`)"}};
                    }
                    count = std::max(1,std::min((int)ZOMBIE_SPAWN_MOST,args.value("count",1)));
                }
                SimCommand cmd;
                cmd.type = CHASM_CMD_ZOMBIE;
                cmd.flags = SIM_CMD_FLAG_RECORD;
                cmd.subtype = (uint32_t)std::max(0,plot);
                cmd.value[0] = (float)count;
                uint32_t refused = zombie_refusals.load();
                uint32_t before = zombie_commands_done.load();
                SubmitCommandAndWait(cmd);
                result["ran"] = zombie_commands_done.load() != before;
                std::string refusal;
                if (zombie_refusals.load() != refused){
                    std::lock_guard<std::mutex> lock(grid_mutex);
                    refusal = zombie_last_refusal;
                }
                result["refusal"] = refusal;
                if (op == "spawn"){
                    result["plot"] = plot;
                }
            }else if (op != "status"){
                return json{{"error","op is spawn, clear or status"}};
            }
            std::shared_ptr<const EconomyState> e = GetEconomy();
            if (!e || e->world != w){
                result["error"] = "no economy for this world yet";
                return result;
            }
            auto round2 = [](float v){ return std::round(v * 100.0f) / 100.0f; };
            json list = json::array();
            for (const EconomyZombie& m : e->zombies){
                GridPick p = w->picker->Pick(m.pos);
                json j{{"id",m.id},{"x",round2(m.pos.x)},{"z",round2(m.pos.y)},{"plot",p.f_hit ? p.plot : -1},
                       {"off",json::array({round2(m.off.x),round2(m.off.y)})},{"health",m.health},{"next_bite_ticks",m.bite},
                       {"target",m.target},{"walking",m.seg < (int)m.route.size() - 1},{"lost",m.f_lost}};
                for (const EconomyWorker& k : e->workers){
                    if (k.id == m.target){
                        j["target_name"] = EconomyPersonName(k);
                        j["target_distance"] = round2((k.pos - m.pos).length());
                        j["body_distance"] = round2((EconomyBody(k) - EconomyBody(m)).length());
                    }
                }
                list.push_back(j);
            }
            result["zombies"] = list;
            //ARROWS (step 3): who at which, a hit or not, how far through its flight, and where it is going.
            json arrows = json::array();
            for (const EconomyArrow& a : e->arrows){
                json j{{"by",a.by},{"zombie",a.zombie},{"hit",a.f_hit},{"flown",a.flown},{"ticks",a.ticks},
                       {"distance",round2((a.to - a.from).length())},{"to",json::array({round2(a.to.x),round2(a.to.y),round2(a.to.z)})}};
                if (!EconomyArrowFlying(a)){
                    j["landed"] = a.f_hit ? "stuck" : "ground";
                    if (!a.f_hit){
                        j["lie_ticks"] = a.lie;
                    }
                }
                arrows.push_back(j);
            }
            result["arrows"] = arrows;
            result["count"] = (int)e->zombies.size();
            result["next_id"] = e->next_zombie;
            return result;
        });
}
#endif
