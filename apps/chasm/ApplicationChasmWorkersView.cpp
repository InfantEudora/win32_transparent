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
    figure without a rig stays static, as above, until it gets one; nothing here names the rig. An archer
    (range_plan.md "Shooting") carries a bow and a quiver, and on the line plays the Shoot clip by his
    stint's clock; his arrows, in flight and landed, are the range's (ApplicationChasmRange.cpp).

    Measured with the crowd bench (ApplicationChasmCrowdBench.cpp, 2026-10-10): a skinned figure costs about
    2.3 us a tick and 7 us a frame of CPU, a static one next to nothing - so a person standing hidden indoors
    is given no clip at all, which takes the tick's share to nothing.
*/

//ApplicationChasmRoads.cpp: the retired debug walker's figure, origin at the feet, facing +z - the
//stand-in when the props file has no figure.
void BuildWalkerFigure(std::vector<vertex>& out, int tunic_column);

const char* person_asset_names[PERSON_FIGURES] = {"adult_male","adult_female","kid_male","kid_female"};

int PersonFigure(const EconomyWorker& k, uint64_t tick){
    return (EconomyStageAt(k.born,tick) < LIFE_YOUNG ? PERSON_KID_MALE : PERSON_ADULT_MALE) + (k.f_female ? 1 : 0);
}

//WORKER_LOADS (ApplicationChasm.h): the loads below, one mesh each per figure.
#define LOAD_NONE           0
#define LOAD_LOG            1
#define LOAD_SACK           2
#define LOAD_PAIL           3
#define LOAD_TENT           4       //packed, rolled across his back (settlements_plan.md)
#define PERSON_MODEL_SCALE  0.46f   //the figures are modelled 1.87 tall (a man); drawn the walker's 0.86
#define WORKER_CHOP_RATE    1.6f    //a working woodcutter's swings a second
#define WORKER_CHOP_BOB     0.07f   //and how far each lifts him

//A rigged figure's clips, by their names in the props file.
#define WORKER_CLIP_NONE    0       //none: the rig's bind pose, and nothing to sample each tick
#define WORKER_CLIP_WALK    1
#define WORKER_CLIP_IDLE    2
#define WORKER_CLIP_SHOOT   3
#define WORKER_WALK_CLIP    "Walking"
#define WORKER_IDLE_CLIP    "Idle"  //stood in when the file has one; the bind pose otherwise
#define WORKER_SHOOT_CLIP   "Shoot" //a trainee on the line (range_plan.md "Shooting"): one turn is one shot
#define WORKER_CLIP_BLEND   0.25f   //seconds a change of clip crossfades over
#define WORKER_SHOOT_BLEND  0.45f   //...into and out of Shoot: he turns side-on to his target, and back
/*
    AN ARCHER'S KIT (range_plan.md "Shooting"). The bow's grip and the arrow's nock are each a PALM: the wrist
    (a hand bone's head - the hands have no tails) carried on along the forearm by WORKER_PALM_REACH, in the
    figure's units. The bow turns from upright, as carried, to square with the arrow between WORKER_BOW_RAISE and
    SHOOT_NOCK, and back between WORKER_BOW_LOWER and WORKER_BOW_DOWN - the Shoot clip's swing to the bow, and
    its lowering after the release.
*/
#define WORKER_PALM_REACH   0.07f
#define WORKER_BOW_RAISE    0.22f
#define WORKER_BOW_LOWER    0.82f
#define WORKER_BOW_DOWN     0.92f
const char* gear_asset_names[GEAR_ASSETS] = {"bow","quiver"};
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

//The bone each load rides on: the log, the sack and the tent on the spine, which the shoulders and back move
//with; the pail in the right hand. A rig without the bone carries the load on its root, still.
const char* load_bone_names[WORKER_LOADS] = {NULL,"Spine","Spine","Hand.R","Spine"};

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

//What he is seen carrying: his load, else a tent packed for the road.
int WorkerLoad(const EconomyWorker& k){
    return (k.carry > 0) ? LoadOf(k.carry_good) : (k.tent == ECONOMY_TENT_CARRIED) ? LOAD_TENT : LOAD_NONE;
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
        case LOAD_TENT:
            //The canvas rolled across his shoulders, the tent's colour, its poles along it standing out at the ends.
            Box(out,vec3(-0.21f,0.50f,-0.25f),vec3(0.21f,0.64f,-0.11f),PAL_WALL);
            Box(out,vec3(-0.27f,0.56f,-0.19f),vec3(0.27f,0.59f,-0.16f),PAL_TIMBER);
            break;
        default:
            break;
    }
}

//A box round the line from a to b, `half` out from it each way - a limb of a bow, a string, a quiver.
void Strut(std::vector<vertex>& out, const vec3& a, const vec3& b, float half, int column){
    vec3 u = b - a;
    float len = u.length();
    if (len < 1e-6f){
        return;
    }
    u = u * (1.0f / len);
    vec3 p = u.cross(fabsf(u.x) < 0.9f ? vec3(1.0f,0.0f,0.0f) : vec3(0.0f,0.0f,1.0f));
    p.normalize();
    vec3 q = u.cross(p);
    vec3 c[8];
    for (int i = 0; i < 8; i++){
        c[i] = ((i & 4) ? b : a) + p * ((i & 1) ? half : -half) + q * ((i & 2) ? half : -half);
    }
    MeshQuad(out,c[0],c[1],c[3],c[2],u * -1.0f,column);
    MeshQuad(out,c[4],c[5],c[7],c[6],u,column);
    MeshQuad(out,c[0],c[1],c[5],c[4],q * -1.0f,column);
    MeshQuad(out,c[2],c[3],c[7],c[6],q,column);
    MeshQuad(out,c[0],c[2],c[6],c[4],p * -1.0f,column);
    MeshQuad(out,c[1],c[3],c[7],c[5],p,column);
}

/*
    THE STAND-INS for an archer's kit, until the props file has a `bow` and a `quiver`, in the figures' units (the
    man is 1.87 tall in them) - which is how one modelled beside him in Blender comes out:
      - the BOW with its grip at the origin, its limbs up and down +y and swept back toward the archer (-z) to
        the tips, the string straight between them: facing +z, the way he shoots;
      - the QUIVER with its middle at the origin and its mouth up +y, a few fletchings standing out of it. Where
        it sits on his back is WORKER_QUIVER_AT, not the model's.
*/
void BowStandIn(std::vector<vertex>& out){
    const float half = 0.55f;
    const float sweep = 0.14f;
    auto at = [&](float t){ return vec3(0.0f,t * half,-sweep * t * t); };
    const int segments = 6;
    for (int s = 0; s < segments; s++){
        Strut(out,at(-1.0f + 2.0f * s / segments),at(-1.0f + 2.0f * (s + 1) / segments),0.028f,PAL_TIMBER);
    }
    Strut(out,vec3(0.0f,-0.07f,0.0f),vec3(0.0f,0.07f,0.0f),0.04f,PAL_BARK_DARK);
    Strut(out,at(-1.0f),at(1.0f),0.007f,PAL_WALL);
}

void QuiverStandIn(std::vector<vertex>& out){
    Strut(out,vec3(0.0f,-0.24f,0.0f),vec3(0.0f,0.22f,0.0f),0.065f,PAL_BARK);
    for (int i = 0; i < 3; i++){
        float x = (float)(i - 1) * 0.035f;
        Strut(out,vec3(x,0.22f,0.012f * i),vec3(x,0.34f,0.012f * i),0.016f,PAL_WALL);
    }
}
//Where the quiver sits on the man's back in his bind pose, and the way its mouth points: up past his right shoulder,
//where the Shoot clip's right hand goes for an arrow. Fitted to the other figures as the loads are.
#define WORKER_QUIVER_AT    vec3(0.02f,1.15f,-0.26f)
#define WORKER_QUIVER_UP    vec3(-0.35f,1.0f,-0.06f)

/*
    Into the space of `bone` at its bind pose, from the figure's as it is DRAWN - `verts` scaled by `scale` first.

    By the bone's own bind matrix, read off the probe (a skeleton at the origin in its reference pose), and NOT by the
    skin's inverse bind matrix. That one starts from the FILE's space - wherever the armature stood, at whatever scale,
    when it was exported - and GLTFLoader::GetSkeleton leaves the armature's node out, so the figure is drawn as if it
    stood at the origin while a load put through the inverse bind matrix still hung where the armature had been: a
    log a world unit beside the woman, once her armature was moved off the origin in Blender. NULL keeps the figure's.
*/
void IntoBone(std::vector<vertex>& verts, Bone* bone, float scale){
    fmat4 to_bone = bone ? bone->GetWorldTransformScaleMatrix() : fmat4().identity();
    to_bone.inverse_transform();
    const vec3 origin = to_bone * vec3(0.0f,0.0f,0.0f);
    for (vertex& v : verts){
        v.pos = to_bone * (v.pos * scale);
        v.normal = to_bone * v.normal - origin;
        v.normal.normalize();
    }
}

Mesh* RetainedMesh(std::vector<vertex>& verts){
    Mesh* m = new Mesh();
    m->SetMeshData(verts.data(),(int)verts.size());
    m->Retain();    //held for good, as the figures are: SetMesh releases what it replaces
    return m;
}

//A palm, in the space the bones are posed in: the wrist carried on along the forearm by `reach`.
vec3 Palm(Bone* forearm, Bone* hand, float reach){
    vec3 wrist = hand->GetWorldPosition();
    vec3 along = wrist - forearm->GetWorldPosition();
    float len = along.length();
    return len > 1e-6f ? wrist + along * (reach / len) : wrist;
}

//How far the bow has turned from upright, as carried, to square with the arrow: 0 to 1 by the Shoot clip's `phase`.
float BowAim(float phase){
    float in = (phase - WORKER_BOW_RAISE) / (SHOOT_NOCK - WORKER_BOW_RAISE);
    float out = (WORKER_BOW_DOWN - phase) / (WORKER_BOW_DOWN - WORKER_BOW_LOWER);
    return std::max(0.0f,std::min(1.0f,std::min(in,out)));
}

}

quat TurnZTo(const vec3& d, const vec3& up){
    return quat::getquat(vec3(0.0f,0.0f,0.0f) - d,vec3(0.0f,0.0f,0.0f),up);
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
    //His body off his route (SPACING, zombies_plan.md), which only happens on the ground: the ground where it stands.
    if ((k.off.x != 0.0f || k.off.y != 0.0f) && EconomyWorkerLayer(k) == WALK_GROUND){
        vec2 at = EconomyBody(k);
        GridPick pick = w.picker->Pick(at);
        if (pick.f_hit && pick.plot >= 0){
            return w.terrain->GroundHeight(at,w.terrain->Height(pick.plot));
        }
    }
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
            if (load == LOAD_NONE){
                float top = 0.0f;
                for (const vertex& v : verts){
                    top = fmaxf(top,v.pos.y);
                }
                worker_figure_height[f] = top;
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
        the figure as drawn (IntoBone, and why not by the inverse bind matrix). A throwaway skeleton - the
        PROBE, at the origin in its reference pose - is where the bind matrices are read from, and where the
        clips are measured.
    */
    std::vector<std::string> clips = gltfloader.GetAnimationNames();
    auto has_clip = [&](const char* name){ return std::find(clips.begin(),clips.end(),std::string(name)) != clips.end(); };
    f_worker_idle_clip = has_clip(WORKER_IDLE_CLIP);
    f_worker_shoot_clip = has_clip(WORKER_SHOOT_CLIP);
    //The archer's kit: the file's bow, or the stand-in, shared; the quiver is fitted to each figure below.
    std::vector<vertex> kit;
    Mesh* bow = assetmanager->GetMeshFromAsset(gear_asset_names[0]);
    if (bow){
        bow->Retain();
        worker_bow_mesh = bow;
    }else{
        BowStandIn(kit);
        worker_bow_mesh = RetainedMesh(kit);
    }
    std::vector<vertex> quiver;
    if (Mesh* q = assetmanager->GetMeshFromAsset(gear_asset_names[1])){
        quiver = q->GetVertices();
    }else{
        QuiverStandIn(quiver);
    }
    float man_drawn = man;      //the man as drawn, the figures' fits are over: his static figure's, or his skin's below
    for (int f = 0; f < PERSON_FIGURES; f++){
        worker_release_at[f] = vec3(0.0f,0.6f,0.15f);   //a rough bow hand, for a figure without the Shoot clip measured
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
        /*
            HOW TALL IT IS DRAWN, which is not how tall its vertices are. They are in the file's space, the armature's
            place and scale in them; the skin draws them by a bone's bind matrix after its inverse bind matrix, which
            leaves the armature out (GLTFLoader::GetSkeleton) - the same change for every bone at the bind pose, so
            any one will do. The woman's armature is scaled 0.83 in the file: her vertices are 1.73 tall, and she is
            drawn 2.09. Her loads and her kit are fitted to what is drawn.
        */
        std::vector<Bone*> bones;
        probe->GetAllBones(probe,bones);
        Mesh* figure = assetmanager->GetMeshFromAsset(person_asset_names[f]);
        float drawn = 0.0f;
        if (!bones.empty() && figure){
            fmat4 bind = bones[0]->GetWorldTransformScaleMatrix();
            for (const vertex& v : figure->GetVertices()){
                drawn = fmaxf(drawn,(bind * (bones[0]->inverse_bind_matrix * v.pos)).y);
            }
        }
        if (f == PERSON_ADULT_MALE && drawn > 0.0f){
            man_drawn = drawn;
        }
        if (drawn > 0.0f && man_drawn > 0.0f){
            worker_figure_fit[f] = drawn / man_drawn;
            worker_figure_height[f] = drawn * PERSON_MODEL_SCALE;
        }
        float fit = worker_figure_fit[f];
        for (int load = 1; load < WORKER_LOADS; load++){
            std::vector<vertex> lv;
            AddLoad(lv,load);
            //Fitted like the static figure's, and out of world units into the figure's.
            IntoBone(lv,probe->FindBone(load_bone_names[load]),fit / PERSON_MODEL_SCALE);
            worker_skin_load[f * WORKER_LOADS + load] = RetainedMesh(lv);
        }
        //The quiver on his back, fitted to him as the loads are.
        kit = quiver;
        vec3 mouth = WORKER_QUIVER_UP;
        mouth.normalize();
        quat tilt = quat::getquat(vec3(0.0f,1.0f,0.0f),mouth);
        for (vertex& v : kit){
            v.pos = tilt * v.pos + WORKER_QUIVER_AT;
            v.normal = tilt * v.normal;
        }
        IntoBone(kit,probe->FindBone("Spine"),fit);
        worker_skin_quiver[f] = RetainedMesh(kit);
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
        //Where his arrow leaves the bow: his left palm at the release, posed as a person of this rig plays the clip.
        Animation* shoot = f_worker_shoot_clip ? gltfloader.LoadAnimation(WORKER_SHOOT_CLIP) : NULL;
        Bone* forearm = probe->FindBone("Arm.Lower.L");
        Bone* hand = probe->FindBone("Hand.L");
        if (shoot && forearm && hand){
            worker_shoot_seconds = shoot->duration;
            probe->AddAnimation(shoot);
            KeepOwnPositions(shoot);
            probe->SwitchToAnimation(shoot);
            probe->ApplyAnimation(SHOOT_RELEASE * shoot->duration);
            worker_release_at[f] = Palm(forearm,hand,WORKER_PALM_REACH * fit) * PERSON_MODEL_SCALE;
        }
        debug->Info("Person figure '%s' is rigged (%s, %i bones): drawn skinned, %.2f tall, walking %.2f units a second at rate 1%s%s%s\n",
                    person_asset_names[f],skin.c_str(),probe->num_bones,worker_figure_height[f],worker_walk_speed[f],
                    measured > 0.01f ? "" : " (not measured: no feet named " WORKER_FOOT_L "/" WORKER_FOOT_R ")",
                    f_worker_idle_clip ? ", standing in its idle clip" : "",
                    shoot ? ", shooting at the range" : "");
        if (shoot){
            debug->Info("  its arrows leave the bow at %.2f %.2f %.2f from its feet, a shot every %.2f s of the clip\n",
                        worker_release_at[f].x,worker_release_at[f].y,worker_release_at[f].z,worker_shoot_seconds);
        }
        delete probe;   //bones carry no mesh, and the skeleton was never in the scene (its clips are left, a one-off)
    }
}

/*
    A person's skeleton of a rigged figure `f`, with his own copy of each clip and a child per load on its bone, his
    kit made and hidden - in no scene yet: the caller adds it, and his bow, which is an object of its own. `name` begins
    every object's name. UploadWorkers' people, and the title's diorama's (ApplicationChasmTitle.cpp).
*/
Object* ApplicationChasm::BuildPersonSkin(WorkerSkin& ws, const std::string& name, int f){
    Skeleton* s = gltfloader.GetSkeleton(worker_skin_name[f].c_str(),assetmanager);
    if (!s){
        return NULL;
    }
    s->name = name + " " + person_asset_names[f];
    s->SetMesh(worker_skin_mesh[f]);
    s->SetMaterialSlot(0,palette_material);
    s->SetPickability(true);
    s->SetVisualOnly(true);
    s->SetVisibility(false);
    s->SetScale(vec3(PERSON_MODEL_SCALE,PERSON_MODEL_SCALE,PERSON_MODEL_SCALE));
    //His own copies: Object::AddAnimation binds a clip's tracks to the bones of the skeleton it is given.
    const char* names[3] = {WORKER_WALK_CLIP,f_worker_idle_clip ? WORKER_IDLE_CLIP : NULL,
                            f_worker_shoot_clip ? WORKER_SHOOT_CLIP : NULL};
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
    s->animation_transition_time_max = WORKER_CLIP_BLEND;
    s->SetBlendTime("",WORKER_SHOOT_CLIP,WORKER_SHOOT_BLEND);
    s->SetBlendTime(WORKER_SHOOT_CLIP,WORKER_WALK_CLIP,WORKER_SHOOT_BLEND);
    s->SetBlendTime(WORKER_SHOOT_CLIP,WORKER_IDLE_CLIP,WORKER_SHOOT_BLEND);
    //His kit, shown while he is an archer: the quiver on his spine, the bow an object of its own that UploadWorkers
    //puts in his hand every frame.
    const char* arm_names[4] = {"Arm.Lower.L","Hand.L","Arm.Lower.R","Hand.R"};
    for (int a = 0; a < 4; a++){
        ws.arm[a] = s->FindBone(arm_names[a]);
    }
    auto kit_object = [&](const char* what, Mesh* m){
        Object* o = new Object();
        o->name = s->name + " " + what;
        o->SetMesh(m);
        o->SetMaterialSlot(0,palette_material);
        o->SetPickability(true);    //a click on his bow picks him
        o->SetVisualOnly(true);
        o->SetVisibility(false);
        return o;
    };
    if (worker_skin_quiver[f]){
        ws.quiver = kit_object("quiver",worker_skin_quiver[f]);
        Bone* spine = s->FindBone("Spine");
        if (spine){
            spine->AttachChild(ws.quiver);
        }else{
            s->AttachChild(ws.quiver);
        }
    }
    if (worker_bow_mesh){
        ws.bow = kit_object("bow",worker_bow_mesh);
        float scale = PERSON_MODEL_SCALE * worker_figure_fit[f];
        ws.bow->SetScale(vec3(scale,scale,scale));
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
        p->SetPickability(true);    //a click on his log picks him
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
}

/*
    An archer's bow in his left hand, and while he shoots the arrow in his right (into `hand_arrows`, if given) -
    from his hands as they are posed NOW, so read between ticks as the renderer reads them. See UploadWorkers.
*/
void ApplicationChasm::PlaceBow(WorkerSkin& ws, int figure, const vec3& forward, std::vector<fmat4>* hand_arrows){
    if (!ws.arm[0] || !ws.arm[1] || !ws.arm[2] || !ws.arm[3]){
        return;
    }
    const vec3 up(0.0f,1.0f,0.0f);
    float fit = worker_figure_fit[figure];
    float reach = WORKER_PALM_REACH * PERSON_MODEL_SCALE * fit;
    vec3 grip = Palm(ws.arm[0],ws.arm[1],reach);
    vec3 nock = Palm(ws.arm[2],ws.arm[3],reach);
    quat bow = TurnZTo(forward,up);
    Animation* clip = ws.figure->current_animation;
    if (clip && clip->name == WORKER_SHOOT_CLIP && clip->duration > 0.0f){
        float phase = clip->time_index / clip->duration;
        vec3 line = grip - nock;
        float len = line.length();
        if (len > 1e-4f && fabsf(line.y) < 0.95f * len){
            line = line * (1.0f / len);
            float aim = BowAim(phase);
            if (aim > 0.0f){
                bow = quat::slerp(bow,TurnZTo(line,up),aim);
            }
            if (hand_arrows && phase >= SHOOT_TAKE && phase < SHOOT_RELEASE){
                vec3 d = (phase < SHOOT_NOCK) ? ws.arm[3]->GetWorldPosition() - ws.arm[2]->GetWorldPosition() : line;
                float dl = d.length();
                if (dl > 1e-6f && fabsf(d.y) < 0.999f * dl){
                    hand_arrows->push_back(Object::ComposeTransformScale(nock,TurnZTo(d * (1.0f / dl),up),
                                                                        vec3(fit,fit,fit)));
                }
            }
        }
    }
    ws.bow->SetPosition(grip,false);
    ws.bow->SetRotation(bow,false);
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
        world_scene->AtTickBoundary([&](){
            while (worker_objects.size() < n){
                Object* o = new Object();
                o->name = "Worker " + std::to_string(worker_objects.size());
                o->SetMesh(worker_meshes[0]);
                o->SetMaterialSlot(0,palette_material);
                //Pickable: the engine reads back who is under the cursor (person_objects, PlayPickUnder).
                o->SetPickability(true);
                o->SetVisualOnly(true);
                o->SetVisibility(false);
                world_scene->AddObject(o);
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
        int figure = PersonFigure(k,e->tick);
        figure_of[i] = figure;
        want[i] = figure * WORKER_LOADS + WorkerLoad(k);
        //A rigged figure's static object stays hidden, so its mesh is left as it is.
        f_swap = f_swap || (!worker_skin_mesh[figure] && want[i] != worker_object_mesh[i]);
    }
    if (f_swap){
        world_scene->AtTickBoundary([&](){
            for (size_t i = 0; i < n; i++){
                if (!worker_skin_mesh[figure_of[i]] && want[i] != worker_object_mesh[i]){
                    worker_objects[i]->SetMesh(worker_meshes[want[i]]);
                    worker_object_mesh[i] = want[i];
                }
            }
        });
    }

    //The skeletons people need and have not got: made here (Objects only, nothing GL), added below.
    std::vector<Object*> made;
    for (size_t i = 0; i < n; i++){
        int f = figure_of[i];
        WorkerSkin& ws = worker_skins[i][f];
        if (worker_skin_mesh[f] && !ws.figure){
            ws.figure = BuildPersonSkin(ws,"Worker " + std::to_string(i),f);
            if (ws.figure){
                made.push_back(ws.figure);
            }
            if (ws.bow){
                made.push_back(ws.bow);
            }
        }
    }

    //Clips to change, all set together under the lock at the end.
    struct ClipChange{
        WorkerSkin* ws;
        int clip;
        float rate;
        float phase;
        uint64_t clock;
    };
    std::vector<ClipChange> changes;
    auto set_clip = [&](WorkerSkin& ws, int clip, float rate, float phase, uint64_t clock = 0){
        if (ws.figure && (clip != ws.clip || fabsf(rate - ws.rate) > 0.01f * ws.rate || clock != ws.clock)){
            changes.push_back({&ws,clip,rate,phase,clock});
        }
    };
    //His bow and quiver shown, or not.
    auto show_kit = [&](WorkerSkin& ws, bool f_show){
        for (Object* kit : {ws.bow,ws.quiver}){
            if (kit){
                kit->SetVisibility(f_show);
            }
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
                show_kit(ws,false);
                set_clip(ws,WORKER_CLIP_NONE,1.0f,0.0f);
            }
        }
    };
    //The archers drawn this frame, for their kit (place_kit, below).
    struct Archer{
        WorkerSkin* ws;
        int figure;
        vec3 forward;       //the way he faces
    };
    std::vector<Archer> archers;

    float seconds = (float)SimSeconds();
    std::vector<std::pair<objectid_t,uint32_t>> figures;    //who each figure drawn is: person_objects
    figures.reserve(n * 2);
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
                show_kit(*ws,false);
            }
            continue;
        }
        //On the ground of the plot he stands on, or on the deck his route says - a guard tower's platform.
        float y = WorkerHeight(*w,z.get(),k);
        //Felling, breaking a rock or digging at a mine (stone_plan.md), building, and on a road (line_works_plan.md) felling
        //its trees or laying it.
        if (((k.job == WORKER_JOB_WOODCUTTER || k.job == WORKER_JOB_STONECUTTER || k.job == WORKER_JOB_MINER)
             && k.state == WORKER_WORKING)
            || k.state == WORKER_BUILDING
            || k.state == WORKER_CLEARING || k.state == WORKER_LAYING || k.state == WORKER_SOWING){
            //Each worker on his own beat, so two at one tree (or one site) do not swing as one.
            float phase = seconds * WORKER_CHOP_RATE + (float)i * 0.37f;
            y += WORKER_CHOP_BOB * fabsf(sinf(phase * 3.1415927f));
        }
        vec2 facing = EconomyWorkerFacing(k);
        if (facing.dot(facing) < 1e-8f){
            facing = vec2(0.0f,1.0f);
        }
        //Shooting at a zombie (zombies_plan.md, step 3): he faces it, and the Shoot clip turns him side-on by itself.
        if (k.aim){
            for (const EconomyZombie& m : e->zombies){
                vec2 to = EconomyBody(m) - EconomyBody(k);
                if (m.id == k.aim && to.length() > 1e-3f){
                    facing = to * (1.0f / to.length());
                }
            }
        }
        vec2 body = EconomyBody(k);     //SPACING: where he stands, off his route
        shown->SetPosition(vec3(body.x,y,body.y),false);
        shown->SetRotation(quat(vec3(0.0f,1.0f,0.0f),atan2f(facing.x,facing.y)),false);
        shown->SetVisibility(true);
        figures.emplace_back(shown->GetID(),k.id);
        if (!ws){
            continue;
        }
        for (int l = 1; l < WORKER_LOADS; l++){
            if (ws->load[l]){
                figures.emplace_back(ws->load[l]->GetID(),k.id);
            }
        }
        int load = WorkerLoad(k);
        for (int l = 1; l < WORKER_LOADS; l++){
            if (ws->load[l]){
                ws->load[l]->SetVisibility(l == load);
            }
        }
        //An archer carries his bow and quiver wherever he goes (range_plan.md "Shooting"), and a guard is the
        //tower's archer.
        bool f_archer = k.job == WORKER_JOB_ARCHER || k.job == WORKER_JOB_GUARD;
        show_kit(*ws,f_archer);
        for (Object* kit : {ws->bow,ws->quiver}){
            if (kit){
                figures.emplace_back(kit->GetID(),k.id);
            }
        }
        if (f_archer && ws->bow){
            archers.push_back({ws,f,vec3(facing.x,0.0f,facing.y)});
        }
        //Walking while his route has a leg left, at that leg's speed; on the line, shooting by his stint's clock;
        //standing otherwise. Each on his own step.
        float phase = fmodf((float)k.id * 0.618034f,1.0f);
        ShotClock shots;
        if (k.seg + 1 < (int)k.route.size() && k.seg < (int)k.speed.size()){
            set_clip(*ws,WORKER_CLIP_WALK,k.speed[k.seg] / worker_walk_speed[f],phase);
        }else if (f_worker_shoot_clip && worker_shoot_seconds > 0.0f && k.draw > 0){
            //A turn of his bow at a zombie (step 3), by the rules' count: his arrow leaves at its release, as they loose it.
            float turn = (float)CALENDAR_TICKS_PER_SECOND * ARROW_SHOT_SECONDS;
            set_clip(*ws,WORKER_CLIP_SHOOT,worker_shoot_seconds / ARROW_SHOT_SECONDS,fmodf((float)k.draw / turn,1.0f),~0ull);
        }else if (f_worker_shoot_clip && worker_shoot_seconds > 0.0f && ShotClockOf(k,e->tick,shots)){
            set_clip(*ws,WORKER_CLIP_SHOOT,worker_shoot_seconds / shots.cycle,fmodf(shots.elapsed / shots.cycle,1.0f),
                     shots.start);
        }else{
            set_clip(*ws,f_worker_idle_clip ? WORKER_CLIP_IDLE : WORKER_CLIP_NONE,1.0f,phase);
        }
    }
    {
        std::lock_guard<std::mutex> lock(pick_mutex);
        person_objects.swap(figures);
    }

    /*
        An archer's bow in his left hand, and while he shoots the arrow in his right - from his hands as they are
        posed NOW, so read between ticks as the renderer reads them. Carried, the bow stands upright, square to the
        way he faces; through the shot it turns square to the arrow, from the nock (the swing to it, BowAim) to the
        lowering. The arrow is in his hand from the quiver to the release: along his forearm as he brings it over,
        then from his right palm through the bow's grip. Its beats are the clip's own time, not his stint's clock, so
        it never parts from the hands drawn.
    */
    worker_hand_arrows.clear();

    if (!made.empty() || !changes.empty() || !archers.empty()){
        world_scene->AtTickBoundary([&](){
            for (Object* s : made){
                world_scene->AddObject(s);
            }
            for (const ClipChange& c : changes){
                Object* s = c.ws->figure;
                if (c.clip != c.ws->clip){
                    const char* name = (c.clip == WORKER_CLIP_WALK) ? WORKER_WALK_CLIP
                                     : (c.clip == WORKER_CLIP_IDLE) ? WORKER_IDLE_CLIP
                                     : (c.clip == WORKER_CLIP_SHOOT) ? WORKER_SHOOT_CLIP : NULL;
                    Animation* clip = name ? s->FindAnimation(name) : NULL;
                    /*
                        From one clip to another, a crossfade (Object::TransitionToAnimation), so he stops, sets off and
                        turns side-on to his target rather than snapping to it. To none - hidden, or the bind pose, which
                        leaves ApplyAnimation nothing to sample - and from none, at once.
                    */
                    if (!clip || c.ws->clip == WORKER_CLIP_NONE || !s->TransitionToAnimation(clip)){
                        s->SwitchToAnimation(clip);
                    }
                    if (clip){
                        clip->time_index = c.phase * clip->duration;
                    }
                    c.ws->clip = c.clip;
                }else if (c.clock != c.ws->clock && s->current_animation){
                    //The same clip on another clock - a trainee turning from the straw to a zombie: put back in step.
                    s->current_animation->time_index = c.phase * s->current_animation->duration;
                }
                c.ws->clock = c.clock;
                s->SetAnimationRate(c.rate);
                c.ws->rate = c.rate;
            }
            for (const Archer& a : archers){
                PlaceBow(*a.ws,a.figure,a.forward,&worker_hand_arrows);
            }
        });
    }
}
