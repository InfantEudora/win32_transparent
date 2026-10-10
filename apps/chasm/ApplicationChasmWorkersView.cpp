#include "ApplicationChasm.h"
#include "BuildingMesh.h"
#include "MeshBuild.h"
#include "Palette.h"
#include "AssetManager.h"
#include "Debug.h"

#include <math.h>
#include <algorithm>

static Debugger* debug = new Debugger("ChasmPeople",DEBUG_ALL);

/*
    THE PEOPLE, DRAWN (docs/people_plan.md). Each person is the user's figure from chasm_props.glb - a man
    or a woman, or a boy or a girl until they are young adults (lifeline_plan.md) - with what he carries on
    him while he carries it, by the GOOD: wood
    as a log along the right shoulder, food as a sack on the back, water as a pail in the right hand. (By
    the good, not the job: an idle carrier taking wood to a site carries a log, not a pail.) A woodcutter
    at work, and a builder, bob as they swing.

    View only: one object per person, reused, placed every frame from the published
    EconomyState. The state moves on ticks, so a person steps at the tick rate.

    A RIGGED FIGURE - one whose node in the props file is bound to a skin (the man and the woman, as of
    2026-10-10) - is drawn SKINNED instead: per person a Skeleton of that rig sharing the figure's one skinned mesh,
    so they still draw in one instanced call per pass, walking while his route has a leg left and standing
    otherwise. What he carries is a child of the bone that carries it, so a pail swings with his arm. A
    figure without a rig stays static, as above, until it gets one; nothing here names the rig.

    Measured with the crowd bench (ApplicationChasmCrowdBench.cpp, 2026-10-10): a skinned figure costs about
    2.3 us a tick and 7 us a frame of CPU, a static one next to nothing - so a person standing hidden indoors
    is given no clip at all, which takes the tick's share to nothing.
*/

//ApplicationChasmRoads.cpp: the retired debug walker's figure, origin at the feet, facing +z - the
//stand-in when the props file has no figure.
void BuildWalkerFigure(std::vector<vertex>& out, int tunic_column);

const char* person_asset_names[PERSON_FIGURES] = {"adult_male","adult_female","kid_male","kid_female"};

#define WORKER_LOADS        4       //nothing, a log, a sack, a pail
#define LOAD_NONE           0
#define LOAD_LOG            1
#define LOAD_SACK           2
#define LOAD_PAIL           3
#define PERSON_MODEL_SCALE  0.46f   //the figures are modelled 1.87 tall (a man); drawn the walker's 0.86
#define WORKER_CHOP_RATE    1.6f    //a working woodcutter's swings a second
#define WORKER_CHOP_BOB     0.07f   //and how far each lifts him

//A rigged figure's clips, by their names in the props file.
#define WORKER_CLIP_NONE    0       //none: the rig's bind pose, and nothing to sample each tick
#define WORKER_CLIP_WALK    1
#define WORKER_CLIP_IDLE    2
#define WORKER_WALK_CLIP    "Walking"
#define WORKER_IDLE_CLIP    "Idle"  //stood in when the file has one; the bind pose otherwise
/*
    THE WALK'S PACE is measured from each rig's own feet at Init (MeasureWalkSpeed), so the clip runs at
    a person's speed with his feet planted however the walk is re-animated. The rig of 2026-10-10 walks
    0.81 world units a second at rate 1 (a 0.625 s cycle of two 0.25 steps), so 2 on open ground and 4
    on a road. A rig without feet of these names gets WORKER_WALK_SPEED, that rig's figure.
*/
#define WORKER_FOOT_L       "Foot.L"
#define WORKER_FOOT_R       "Foot.R"
#define WORKER_WALK_SPEED   0.81f

namespace {

//The bone each load rides on: the log and the sack on the spine, which the shoulders and back move with;
//the pail in the right hand. A rig without the bone carries the load on its root, still.
const char* load_bone_names[WORKER_LOADS] = {NULL,"Spine","Spine","Hand.R"};

/*
    After Object::AddAnimation: a track this rig BORROWED - bound by name to a bone of a node it was not
    made for (Animation::LinkObjects), another rig's channel - is played as turns only, since its positions
    are that rig's bone offsets and would stretch this one to its proportions. A rig's own channels keep
    theirs, the hips' bob with them.
*/
void KeepOwnPositions(Animation* clip){
    for (ObjectAnimation* track : clip->object_animations){
        Bone* bone = dynamic_cast<Bone*>(track->target);
        if (bone && bone->node_index != track->target_node){
            clip->SetPositionUpdates(track,false);
        }
    }
}

/*
    World units a second `walk` carries this rig at rate 1, posed on a skeleton that is in no scene: each
    foot swings fore and aft about one step, and a cycle is two - so the two feet's ranges, at the drawn
    scale, over the clip's length. 0 when the rig has no feet by those names.
*/
float MeasureWalkSpeed(Skeleton* probe, Animation* walk){
    Bone* feet[2] = {probe->FindBone(WORKER_FOOT_L),probe->FindBone(WORKER_FOOT_R)};
    if (!feet[0] || !feet[1] || walk->duration <= 0.0f){
        return 0.0f;
    }
    probe->SwitchToAnimation(walk);
    float lo[2] = {1e9f,1e9f};
    float hi[2] = {-1e9f,-1e9f};
    const int steps = 48;
    for (int s = 0; s <= steps; s++){
        probe->ApplyAnimation(walk->duration / (float)steps);
        for (int k = 0; k < 2; k++){
            float z = feet[k]->GetHeadWorldPosition().z;
            lo[k] = fminf(lo[k],z);
            hi[k] = fmaxf(hi[k],z);
        }
    }
    return ((hi[0] - lo[0]) + (hi[1] - lo[1])) * PERSON_MODEL_SCALE / walk->duration;
}

//An axis-aligned box from lo to hi, flat-shaded.
void Box(std::vector<vertex>& out, const vec3& lo, const vec3& hi, int column){
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = vec3((i & 1) ? hi.x : lo.x,(i & 2) ? hi.y : lo.y,(i & 4) ? hi.z : lo.z);
    }
    MeshQuad(out,c[0],c[2],c[6],c[4],vec3(-1,0,0),column);
    MeshQuad(out,c[1],c[3],c[7],c[5],vec3(1,0,0),column);
    MeshQuad(out,c[0],c[1],c[5],c[4],vec3(0,-1,0),column);
    MeshQuad(out,c[2],c[3],c[7],c[6],vec3(0,1,0),column);
    MeshQuad(out,c[0],c[1],c[3],c[2],vec3(0,0,-1),column);
    MeshQuad(out,c[4],c[5],c[7],c[6],vec3(0,0,1),column);
}

//The load a good makes. Fitted to the figures at PERSON_MODEL_SCALE: shoulder about 0.67 up, the back
//0.10 behind the middle, the right hand about 0.23 out and 0.37 up.
int LoadOf(int good){
    switch (good){
        case GOOD_WOOD:     return LOAD_LOG;
        case GOOD_WATER:    return LOAD_PAIL;
        case -1:            return LOAD_NONE;
        default:            return LOAD_SACK;     //the crops
    }
}

void AddLoad(std::vector<vertex>& out, int load){
    switch (load){
        case LOAD_LOG:
            Box(out,vec3(0.08f,0.64f,-0.34f),vec3(0.20f,0.75f,0.30f),PAL_BARK);
            break;
        case LOAD_SACK:
            Box(out,vec3(-0.13f,0.32f,-0.27f),vec3(0.13f,0.62f,-0.09f),PAL_STONE_LIGHT);
            break;
        case LOAD_PAIL:{
            Box(out,vec3(0.19f,0.12f,-0.07f),vec3(0.33f,0.30f,0.07f),PAL_TIMBER);
            const float y = 0.302f;
            MeshQuad(out,vec3(0.21f,y,-0.05f),vec3(0.31f,y,-0.05f),vec3(0.31f,y,0.05f),vec3(0.21f,y,0.05f),
                     vec3(0,1,0),PAL_WATER);
            break;
        }
        default:
            break;
    }
}

}

/*
    How high his route is (ramparts_plan.md) at `along` down leg `seg`: each of its points says which layer it is on.
    On the ground, the ground's relief there; on a deck, its height. Between two plots, from the one's height straight
    to where their layers meet - at the edge between them - and on to the other's, so a stair is walked as a ramp and a
    deck to a deck stays level. Up a tower's ladder, whose layers meet nowhere, he steps onto the platform at its wall,
    as he always did.
*/
float RouteHeight(const ChasmWorld& w, const ZoneState* z, const EconomyWorker& k, int seg, float along){
    const Terrain& t = *w.terrain;
    auto plot_at = [&](const vec2& at){
        GridPick pick = w.picker->Pick(at);
        return pick.f_hit ? pick.plot : -1;
    };
    auto ground_at = [&](const vec2& at){
        int v = plot_at(at);
        return (v >= 0) ? t.GroundHeight(at,t.Height(v)) : 0.0f;
    };
    if (k.route.empty()){
        return ground_at(k.pos);
    }
    int s = std::max(0,std::min(seg,(int)k.route.size() - 1));
    //The point itself: on the leg, or at the route's last point once past it.
    vec2 pos = k.route[s];
    float f = 1.0f;
    if (s + 1 < (int)k.route.size()){
        vec2 d = k.route[s + 1] - k.route[s];
        float len = d.length();
        f = (len > 1e-4f) ? std::max(0.0f,std::min(1.0f,along / len)) : 1.0f;
        pos = k.route[s] + d * f;
    }
    if (!z || z->world.get() != &w || k.route_layer.empty()){
        return ground_at(pos);
    }
    auto at_point = [&](const vec2& at, int v, int layer){
        return (layer == WALK_GROUND) ? ground_at(at) : BuildingLayerHeight(w,*z,v,layer);
    };
    int la = EconomyRouteLayer(k,s);
    if (s + 1 >= (int)k.route.size()){
        int v = plot_at(pos);
        return (v >= 0) ? at_point(pos,v,la) : ground_at(pos);
    }
    int lb = EconomyRouteLayer(k,s + 1);
    if (la == WALK_GROUND && lb == WALK_GROUND){
        return ground_at(pos);
    }
    vec2 a = k.route[s];
    vec2 b = k.route[s + 1];
    int pa = plot_at(a);
    int pb = plot_at(b);
    if (pa < 0 || pb < 0){
        return ground_at(pos);
    }
    //On one plot - a step to or from his place on it - he is on its layer.
    if (pa == pb){
        return at_point(pos,pa,(f < 0.5f) ? la : lb);
    }
    int meet = WalkLayersMeet(la,lb);
    if (meet < 0){
        bool f_on_b = f >= 0.5f;
        if (lb != WALK_GROUND && z->KindOf(pb) == ZONE_KIND_TOWER){
            f_on_b = BuildingOnTower(w,pb,pos);
        }else if (la != WALK_GROUND && z->KindOf(pa) == ZONE_KIND_TOWER){
            f_on_b = !BuildingOnTower(w,pa,pos);
        }
        return f_on_b ? at_point(pos,pb,lb) : at_point(pos,pa,la);
    }
    vec2 mid = (a + b) * 0.5f;
    float hm = (meet == WALK_GROUND) ? ground_at(mid)
                                     : (t.ground[pa] + t.ground[pb]) * 0.5f + (float)meet * 0.5f * ZONE_STOREY_HEIGHT;
    if (f < 0.5f){
        float ha = at_point(a,pa,la);
        return ha + (hm - ha) * (f * 2.0f);
    }
    float hb = at_point(b,pb,lb);
    return hm + (hb - hm) * ((f - 0.5f) * 2.0f);
}

float WorkerHeight(const ChasmWorld& w, const ZoneState* z, const EconomyWorker& k){
    return RouteHeight(w,z,k,k.seg,k.along);
}

/*
    RENDER THREAD, at Init once the props are loaded (LoadProps): a mesh per figure and load - the figure's
    vertices copied and scaled, the load added, fitted to the figure's height (a load sits on a man's
    shoulder, so on a child's it is brought down with him). Without a figure in the props file, the walker's.
*/
void ApplicationChasm::BuildWorkerScene(){
    auto height = [](Mesh* m){
        float top = 0.0f;
        if (m){
            for (const vertex& v : m->GetVertices()){
                top = fmaxf(top,v.pos.y);
            }
        }
        return top;
    };
    float man = height(assetmanager->GetMeshFromAsset(person_asset_names[PERSON_ADULT_MALE]));
    std::vector<vertex> verts;
    std::vector<vertex> load_verts;
    for (int f = 0; f < PERSON_FIGURES; f++){
        Mesh* figure = assetmanager->GetMeshFromAsset(person_asset_names[f]);
        bool f_found = figure && !figure->GetVertices().empty();
        if (!f_found){
            debug->Warn("Person figure '%s' not in chasm_props.glb - the walker's figure stands in\n",person_asset_names[f]);
        }
        float fit = (f_found && man > 0.0f) ? height(figure) / man : 1.0f;
        worker_figure_fit[f] = fit;
        for (int load = 0; load < WORKER_LOADS; load++){
            if (f_found){
                verts = figure->GetVertices();
                for (vertex& v : verts){
                    v.pos = v.pos * PERSON_MODEL_SCALE;
                }
            }else{
                BuildWalkerFigure(verts,PAL_WALL);
            }
            load_verts.clear();
            AddLoad(load_verts,load);
            for (vertex& v : load_verts){
                v.pos = v.pos * fit;
            }
            verts.insert(verts.end(),load_verts.begin(),load_verts.end());
            Mesh* mesh = new Mesh();
            mesh->SetMeshData(verts.data(),(int)verts.size());
            /*
                Held for good. Object::SetMesh releases the mesh an object had, and the last release
                deletes it - so without this, the moment every worker carrying nothing had picked
                something up, the empty-handed figure was freed, and the next one to put his load
                down was drawn from a deleted mesh (GL errors, then a crash).
            */
            mesh->Retain();
            worker_meshes[f * WORKER_LOADS + load] = mesh;
        }
    }

    /*
        The rigged figures: the skinned mesh every person of that figure shares, and each load moved into
        the space of the bone it rides on, so that hung on that bone at no offset it sits where it sits on
        the static figure (the bone's inverse bind matrix takes the model's space to the bone's, as the
        vertices bound to it are taken). A throwaway skeleton is where the bind matrices are read from.
    */
    std::vector<std::string> clips = gltfloader.GetAnimationNames();
    f_worker_idle_clip = std::find(clips.begin(),clips.end(),std::string(WORKER_IDLE_CLIP)) != clips.end();
    for (int f = 0; f < PERSON_FIGURES; f++){
        std::string skin = gltfloader.GetNodeSkinName(person_asset_names[f]);
        if (skin.empty()){
            continue;
        }
        std::vector<Material> materials;
        Mesh* mesh = gltfloader.GetMeshFromNode(person_asset_names[f],&materials,true);
        Skeleton* probe = mesh && mesh->IsSkinnedMesh() ? gltfloader.GetSkeleton(skin.c_str(),assetmanager) : NULL;
        if (!probe){
            debug->Warn("Person figure '%s' has the skin '%s' but no skinned mesh or skeleton loads - drawn static\n",
                        person_asset_names[f],skin.c_str());
            continue;
        }
        mesh->Retain();
        worker_skin_mesh[f] = mesh;
        worker_skin_name[f] = skin;
        for (int load = 1; load < WORKER_LOADS; load++){
            std::vector<vertex> lv;
            AddLoad(lv,load);
            Bone* bone = probe->FindBone(load_bone_names[load]);
            const vec3 origin = bone ? bone->inverse_bind_matrix * vec3(0.0f,0.0f,0.0f) : vec3(0.0f,0.0f,0.0f);
            for (vertex& v : lv){
                //Fitted like the static figure's, then out of world units into the model's.
                v.pos = v.pos * (worker_figure_fit[f] / PERSON_MODEL_SCALE);
                if (bone){
                    v.pos = bone->inverse_bind_matrix * v.pos;
                    v.normal = bone->inverse_bind_matrix * v.normal - origin;
                    v.normal.normalize();
                }
            }
            Mesh* m = new Mesh();
            m->SetMeshData(lv.data(),(int)lv.size());
            m->Retain();    //held for good, as the static figures are
            worker_skin_load[f * WORKER_LOADS + load] = m;
        }
        //The walk's pace, from this rig's own feet with the clip as a person of it will play it.
        Animation* walk = gltfloader.LoadAnimation(WORKER_WALK_CLIP);
        float measured = 0.0f;
        if (walk){
            walk->looped = true;
            probe->AddAnimation(walk);
            KeepOwnPositions(walk);
            measured = MeasureWalkSpeed(probe,walk);
        }
        worker_walk_speed[f] = measured > 0.01f ? measured : WORKER_WALK_SPEED;
        debug->Info("Person figure '%s' is rigged (%s, %i bones): drawn skinned, walking %.2f units a second at rate 1%s%s\n",
                    person_asset_names[f],skin.c_str(),probe->num_bones,worker_walk_speed[f],
                    measured > 0.01f ? "" : " (not measured: no feet named " WORKER_FOOT_L "/" WORKER_FOOT_R ")",
                    f_worker_idle_clip ? ", standing in its idle clip" : "");
        delete probe;   //bones carry no mesh, and the skeleton was never in the scene (its one clip is left, a one-off)
    }
}

/*
    RENDER THREAD. One object per worker, made when there are more workers than objects and hidden
    when there are fewer. An object's mesh follows its worker's job and load; a change of mesh joins
    the scene's walk like a new object does, under its lock, and is rare (a load picked up or put
    down), so the frames between pay nothing for it.

    A rigged figure's person is his Skeleton instead, the static object hidden: built the first time he is
    that figure - a boy who grows up gets the man's - and kept. Its clip is set only when it changes
    (setting off, stopping, a leg at another speed), under the same lock, since the physics thread's tick
    is what plays it; the rest is placed here every frame, as the static figures are.
*/
void ApplicationChasm::UploadWorkers(){
    std::shared_ptr<const ChasmWorld> w = GetWorld();
    std::shared_ptr<const EconomyState> e = GetEconomy();
    std::shared_ptr<const ZoneState> z = GetZones();
    size_t n = (w && e && e->world == w) ? e->workers.size() : 0;
    if (worker_objects.size() < n){
        main_scene->AtTickBoundary([&](){
            while (worker_objects.size() < n){
                Object* o = new Object();
                o->name = "Worker " + std::to_string(worker_objects.size());
                o->SetMesh(worker_meshes[0]);
                o->SetMaterialSlot(0,palette_material);
                o->SetPickability(false);
                o->SetVisualOnly(true);
                o->SetVisibility(false);
                main_scene->AddObject(o);
                worker_objects.push_back(o);
                worker_object_mesh.push_back(0);
            }
        });
    }
    if (worker_skins.size() < n){
        worker_skins.resize(n);
    }
    std::vector<int> want(n,0);
    std::vector<int> figure_of(n,0);
    bool f_swap = false;
    for (size_t i = 0; i < n; i++){
        const EconomyWorker& k = e->workers[i];
        int figure = (EconomyStageAt(k.born,e->tick) < LIFE_YOUNG ? PERSON_KID_MALE : PERSON_ADULT_MALE) + (k.f_female ? 1 : 0);
        figure_of[i] = figure;
        want[i] = figure * WORKER_LOADS + (k.carry > 0 ? LoadOf(k.carry_good) : LOAD_NONE);
        //A rigged figure's static object stays hidden, so its mesh is left as it is.
        f_swap = f_swap || (!worker_skin_mesh[figure] && want[i] != worker_object_mesh[i]);
    }
    if (f_swap){
        main_scene->AtTickBoundary([&](){
            for (size_t i = 0; i < n; i++){
                if (!worker_skin_mesh[figure_of[i]] && want[i] != worker_object_mesh[i]){
                    worker_objects[i]->SetMesh(worker_meshes[want[i]]);
                    worker_object_mesh[i] = want[i];
                }
            }
        });
    }

    //A person's skeleton of a rigged figure, with his own copy of each clip and a child per load on its bone.
    auto build_skin = [&](WorkerSkin& ws, size_t i, int f) -> Object*{
        Skeleton* s = gltfloader.GetSkeleton(worker_skin_name[f].c_str(),assetmanager);
        if (!s){
            return NULL;
        }
        s->name = "Worker " + std::to_string(i) + " " + person_asset_names[f];
        s->SetMesh(worker_skin_mesh[f]);
        s->SetMaterialSlot(0,palette_material);
        s->SetPickability(false);
        s->SetVisualOnly(true);
        s->SetVisibility(false);
        s->SetScale(vec3(PERSON_MODEL_SCALE,PERSON_MODEL_SCALE,PERSON_MODEL_SCALE));
        //His own copies: Object::AddAnimation binds a clip's tracks to the bones of the skeleton it is given.
        const char* names[2] = {WORKER_WALK_CLIP,f_worker_idle_clip ? WORKER_IDLE_CLIP : NULL};
        for (const char* name : names){
            Animation* clip = name ? gltfloader.LoadAnimation(name) : NULL;
            if (!clip){
                continue;
            }
            clip->looped = true;
            //Each rig binds its own channels where the clip has them, and borrows another's by name.
            s->AddAnimation(clip);
            KeepOwnPositions(clip);
        }
        for (int l = 1; l < WORKER_LOADS; l++){
            Mesh* m = worker_skin_load[f * WORKER_LOADS + l];
            if (!m){
                continue;
            }
            Object* p = new Object();
            p->name = s->name + " load " + std::to_string(l);
            p->SetMesh(m);
            p->SetMaterialSlot(0,palette_material);
            p->SetPickability(false);
            p->SetVisualOnly(true);
            p->SetVisibility(false);
            Bone* bone = s->FindBone(load_bone_names[l]);
            if (bone){
                bone->AttachChild(p);
            }else{
                s->AttachChild(p);
            }
            ws.load[l] = p;
        }
        return s;
    };
    //The skeletons people need and have not got: made here (Objects only, nothing GL), added below.
    std::vector<Object*> made;
    for (size_t i = 0; i < n; i++){
        int f = figure_of[i];
        WorkerSkin& ws = worker_skins[i][f];
        if (worker_skin_mesh[f] && !ws.figure){
            ws.figure = build_skin(ws,i,f);
            if (ws.figure){
                made.push_back(ws.figure);
            }
        }
    }

    //Clips to change, all set together under the lock at the end.
    struct ClipChange{
        WorkerSkin* ws;
        int clip;
        float rate;
        float phase;
    };
    std::vector<ClipChange> changes;
    auto set_clip = [&](WorkerSkin& ws, int clip, float rate, float phase){
        if (ws.figure && (clip != ws.clip || fabsf(rate - ws.rate) > 0.01f * ws.rate)){
            changes.push_back({&ws,clip,rate,phase});
        }
    };
    //Person i's skeletons but figure `keep`'s hidden, and given no clip - which costs a tick nothing.
    auto park = [&](size_t i, int keep){
        if (i >= worker_skins.size()){
            return;
        }
        for (int f = 0; f < PERSON_FIGURES; f++){
            WorkerSkin& ws = worker_skins[i][f];
            if (f != keep && ws.figure){
                ws.figure->SetVisibility(false);
                set_clip(ws,WORKER_CLIP_NONE,1.0f,0.0f);
            }
        }
    };

    float seconds = (float)SimSeconds();
    for (size_t i = 0; i < worker_objects.size(); i++){
        Object* o = worker_objects[i];
        if (i >= n){
            o->SetVisibility(false);
            park(i,-1);
            continue;
        }
        const EconomyWorker& k = e->workers[i];
        int f = figure_of[i];
        WorkerSkin* ws = worker_skins[i][f].figure ? &worker_skins[i][f] : NULL;
        Object* shown = ws ? ws->figure : o;
        if (ws){
            o->SetVisibility(false);
        }
        park(i,ws ? f : -1);
        //At home, he is indoors (P4): not drawn.
        if (EconomyIndoors(k)){
            shown->SetVisibility(false);
            if (ws){
                set_clip(*ws,WORKER_CLIP_NONE,1.0f,0.0f);
            }
            continue;
        }
        //On the ground of the plot he stands on, or on the deck his route says - a guard tower's platform.
        float y = WorkerHeight(*w,z.get(),k);
        //Felling, building, and on a road (line_works_plan.md) felling its trees or laying it.
        if ((k.job == WORKER_JOB_WOODCUTTER && k.state == WORKER_WORKING) || k.state == WORKER_BUILDING
            || k.state == WORKER_CLEARING || k.state == WORKER_LAYING || k.state == WORKER_SOWING){
            //Each worker on his own beat, so two at one tree (or one site) do not swing as one.
            float phase = seconds * WORKER_CHOP_RATE + (float)i * 0.37f;
            y += WORKER_CHOP_BOB * fabsf(sinf(phase * 3.1415927f));
        }
        vec2 facing = EconomyWorkerFacing(k);
        if (facing.dot(facing) < 1e-8f){
            facing = vec2(0.0f,1.0f);
        }
        shown->SetPosition(vec3(k.pos.x,y,k.pos.y),false);
        shown->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(facing.x,facing.y)),false);
        shown->SetVisibility(true);
        if (!ws){
            continue;
        }
        int load = k.carry > 0 ? LoadOf(k.carry_good) : LOAD_NONE;
        for (int l = 1; l < WORKER_LOADS; l++){
            if (ws->load[l]){
                ws->load[l]->SetVisibility(l == load);
            }
        }
        //Walking while his route has a leg left, at that leg's speed; standing otherwise. Each on his own step.
        float phase = fmodf((float)k.id * 0.618034f,1.0f);
        if (k.seg + 1 < (int)k.route.size() && k.seg < (int)k.speed.size()){
            set_clip(*ws,WORKER_CLIP_WALK,k.speed[k.seg] / worker_walk_speed[f],phase);
        }else{
            set_clip(*ws,f_worker_idle_clip ? WORKER_CLIP_IDLE : WORKER_CLIP_NONE,1.0f,phase);
        }
    }

    if (!made.empty() || !changes.empty()){
        main_scene->AtTickBoundary([&](){
            for (Object* s : made){
                main_scene->AddObject(s);
            }
            for (const ClipChange& c : changes){
                Object* s = c.ws->figure;
                if (c.clip != c.ws->clip){
                    const char* name = (c.clip == WORKER_CLIP_WALK) ? WORKER_WALK_CLIP
                                     : (c.clip == WORKER_CLIP_IDLE) ? WORKER_IDLE_CLIP : NULL;
                    Animation* clip = name ? s->FindAnimation(name) : NULL;
                    //No clip is the bind pose, and from then on nothing for ApplyAnimation to sample.
                    s->SwitchToAnimation(clip);
                    if (clip){
                        clip->time_index = c.phase * clip->duration;
                    }
                    c.ws->clip = c.clip;
                }
                s->SetAnimationRate(c.rate);
                c.ws->rate = c.rate;
            }
        });
    }
}
