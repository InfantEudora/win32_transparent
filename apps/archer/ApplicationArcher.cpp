#include "ApplicationArcher.h"
#ifdef USE_IMGUI
//core/Window.h no longer pulls ImGui into every translation unit - see the note at the top of it.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#endif

#include "Debug.h"
#include "Primitives.h"
#include "MarchingCubes.h"
#include "type_helpers.h"
#ifdef USE_MCP
#include "MCPServer.h"
#endif
#include "PlaceHash.h"
#ifdef USE_SOUND
//Only here, and only with sound: it is the one file that names both the cues and SoundSystem.
#include "CueSoundOutput.h"
#endif
#include <sys/stat.h>

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <chrono>
#include <thread>
#include <algorithm>

static Debugger* debug = new Debugger("ApplicationArcher",DEBUG_ALL);

/*
    How deep the world is in z.

    The play plane is z = 0 and Stage's coordinates ARE world coordinates, so nothing is ever
    converted. Depth exists only so that 3D assets read as 3D: a block with thickness catches the
    light on its near face and throws a shadow with a visible edge, where a flat quad reads as a
    sprite. The camera looks straight down -Z, so none of this depth is ever in the way.
*/
//The default a block is drawn at; a block that sets StageBlock::depth is drawn at its own.
#define BLOCK_DEPTH                 (2.0f * STAGE_BLOCK_HALF_DEPTH)
#define PROP_DEPTH                  0.80f
#define ARCHER_DEPTH                0.70f
#define ARROW_DEPTH                 0.06f

//A target counts as knocked over once it has tipped this far off vertical.
#define TARGET_KNOCKED_DEG          40.0f

//The solver's gravity, for the props only - see the note where Init sets it. Named because both
//scenes' physics worlds use it, and a target that falls differently on the range would make the
//range useless for the one thing it is for.
#define ARCHER_PROP_GRAVITY         (-18.0f)

//Stage.cpp keeps its own copy of this rather than share one, because sharing it would mean a
//header both files include, and Stage.h deliberately includes nothing.
static const float ARCHER_DEG2RAD = 3.14159265358979f / 180.0f;

/*
    The half of a clip's root motion this character wants: the TURN, and not the travel.

    A clip that turns her - Running_TurnAround, Twirl - carries that turn as yaw on the root bone,
    and Animation::SampleRootMotion strips it off the bone whether or not anything asked for it,
    because stripping it is the same operation as posing the bone correctly. It then hands the yaw
    to this method. The BASE does nothing with it, so on a plain Skeleton the rotation in every
    clip is removed from the pose and then discarded, and the character stands there resolutely
    not turning - which looks for all the world like a clip exported without rotation in it.

    The TRAVEL is deliberately dropped instead. Stage owns where the archer is, down to the
    collision resolution; a clip allowed to move her would walk her through walls and off ledges,
    and there would be two things integrating one character again. Every clip loads with both
    extract flags off, so `delta.position` is zero anyway - this is the second lock on that door.
*/
void ArcherModel::ApplyRootMotion(const RootMotionDelta& delta){
    clip_yaw += delta.yaw;
}

bool ArcherModel::BuildAimChain(){
    //Parents before children: each bone's parent-space axis is worked out from its parent's
    //world rotation, which has to already include the turn given to the bones above it.
    const char* names[ARCHER_AIM_BONES] = {
        "mixamorig:Spine","mixamorig:Spine1","mixamorig:Spine2",
        "mixamorig:LeftShoulder","mixamorig:RightShoulder","mixamorig:Neck"
    };
    const float shares[ARCHER_AIM_BONES] = {
        ARCHER_AIM_SPINE_SHARE,ARCHER_AIM_SPINE_SHARE,ARCHER_AIM_SPINE_SHARE,
        ARCHER_AIM_SHOULDER_SHARE,ARCHER_AIM_SHOULDER_SHARE,ARCHER_AIM_NECK_SHARE
    };
    for (int i = 0; i < ARCHER_AIM_BONES; i++){
        aim_bones[i] = FindBone(names[i]);
        aim_shares[i] = shares[i];
        if (!aim_bones[i] || !aim_bones[i]->GetParent()){
            debug->Err("Aim override is OFF: no bone '%s' (or it has no parent)\n",names[i]);
            for (int j = 0; j < ARCHER_AIM_BONES; j++){ aim_bones[j] = NULL; }
            return false;
        }
    }
    //The aim bones are layered bones too - their base pose is saved and restored with the rest.
    for (int i = 0; i < ARCHER_AIM_BONES; i++){
        if (std::find(layered_bones.begin(),layered_bones.end(),aim_bones[i]) == layered_bones.end()){
            layered_bones.push_back(aim_bones[i]);
        }
    }
    layered_rot.resize(layered_bones.size());
    layered_pos.resize(layered_bones.size());
    return true;
}

bool ArcherModel::BuildLegChains(){
    const char* names[2][ARCHER_LEG_BONES] = {
        { "mixamorig:LeftUpLeg","mixamorig:LeftLeg","mixamorig:LeftFoot","mixamorig:LeftToeBase" },
        { "mixamorig:RightUpLeg","mixamorig:RightLeg","mixamorig:RightFoot","mixamorig:RightToeBase" }
    };
    int found = 0;
    for (int leg = 0; leg < 2; leg++){
        bool f_all = true;
        for (int j = 0; j < ARCHER_LEG_BONES; j++){
            leg_bones[leg][j] = FindBone(names[leg][j]);
            if (!leg_bones[leg][j] || !leg_bones[leg][j]->GetParent()){
                debug->Err("Loose legs: no bone '%s' - that leg stays as the clip has it\n",names[leg][j]);
                f_all = false;
            }
        }
        if (!f_all){
            for (int j = 0; j < ARCHER_LEG_BONES; j++){ leg_bones[leg][j] = NULL; }
            continue;
        }
        found++;
        //Only the three that turn are layered; the toe is where the foot points, and only read.
        for (int j = 0; j < ARCHER_LEG_BONES - 1; j++){
            if (std::find(layered_bones.begin(),layered_bones.end(),leg_bones[leg][j]) == layered_bones.end()){
                layered_bones.push_back(leg_bones[leg][j]);
            }
        }
    }
    layered_rot.resize(layered_bones.size());
    layered_pos.resize(layered_bones.size());
    leg_params.stiffness = ARCHER_LEG_STIFFNESS;
    leg_params.damping = ARCHER_LEG_DAMPING;
    leg_params.follow = ARCHER_LEG_FOLLOW;
    return found == 2;
}

void ArcherModel::TurnInWorld(Bone* bone, const vec3& axis_world, float angle){
    quat parent_inverse = bone->GetParent()->GetWorldRotation();
    parent_inverse.inverse();
    vec3 axis_local = parent_inverse * axis_world;
    quat local = quat(axis_local,angle) * bone->GetRotation();
    local.normalize();
    bone->SetRotation(local);
}

/*
    Each leg's chain, one tick. Stepped EVERY tick, whatever the weight, so that when the legs come
    loose they come loose from a chain that has been following her all along rather than from
    wherever it was last left - a stale chain would whip her legs across on the first frame.

    The pose the chain springs toward is the clip's, turned about the hip by the pump's lead; the
    turn put on the bones is measured against the clip's own, so the lead is what shows. In a
    plane every turn is about the one axis, so each bone takes its segment's swing less its
    parent's - turning the parent has already carried it that far.
*/
void ArcherModel::ApplyLegChains(float time_delta){
    /*
        THE PLANE IS HERS, not the screen's: the one her legs swing fore and aft in, whose normal is
        her own left (the rig's -X, as drawn - yawed, and tilted with the rope). Side-on that IS
        the play plane, +Z facing right and -Z facing left. Fixed to the screen instead, every turn
        she makes on the rope - the rules flip her facing each time the swing reverses - swept her
        legs through the plane and read as a swing of 40 degrees in a few ticks.
        About her left, + swings a leg FORWARD, and the knee folds the foot back: always -.
    */
    vec3 axis = GetWorldRotation() * vec3(-1.0f,0.0f,0.0f);
    axis.normalize();
    const int knee_sign = -1;
    //The lead is the Puppet's, in the world: + toward +X. About her left that is + facing right and
    //- facing left, fading through zero as she turns - her normal's own z.
    float lead = leg_lead_deg * ARCHER_DEG2RAD * axis.z;
    leg_params.plane_normal = axis;
    /*
        HER TURNS ARE CARRIED, not simulated. She yaws round on the rope every time the swing
        reverses; left to the particles, the legs were left behind by her own turn and flung 40-60
        degrees about as she came round. The turn is about her own up - the rope's tilt of it - through
        the model's origin, which is what SetRotation turns her about.
    */
    if (f_leg_yaw_seen && leg_drawn_yaw != leg_last_yaw){
        quat tilt_inverse = leg_drawn_tilt;
        tilt_inverse.inverse();
        quat turn = leg_drawn_tilt * quat(vec3(0.0f,1.0f,0.0f),leg_drawn_yaw - leg_last_yaw) * tilt_inverse;
        turn.normalize();
        for (int leg = 0; leg < 2; leg++){
            leg_chain[leg].Carry(turn,GetWorldPosition());
        }
    }
    leg_last_yaw = leg_drawn_yaw;
    f_leg_yaw_seen = true;
    std::vector<DynamicChainLimit> limits(ARCHER_LEG_BONES - 1);
    limits[0].lo = -ARCHER_LEG_HIP_LIMIT;   limits[0].hi = ARCHER_LEG_HIP_LIMIT;
    limits[1].lo = -ARCHER_LEG_KNEE_LIMIT;  limits[1].hi = ARCHER_LEG_KNEE_LIMIT;
    limits[1].bend_sign = knee_sign;
    limits[2].lo = -ARCHER_LEG_ANKLE_LIMIT; limits[2].hi = ARCHER_LEG_ANKLE_LIMIT;
    leg_params.dt = time_delta;
    for (int leg = 0; leg < 2; leg++){
        leg_swing_deg[leg] = 0.0f;
        leg_knee_deg[leg] = 0.0f;
        if (!leg_bones[leg][0]){
            continue;
        }
        std::vector<vec3> pose(ARCHER_LEG_BONES);
        for (int j = 0; j < ARCHER_LEG_BONES; j++){
            pose[j] = leg_bones[leg][j]->GetWorldPosition();
        }
        std::vector<vec3> target = pose;
        if (lead != 0.0f){
            quat q(axis,lead);
            for (int j = 1; j < ARCHER_LEG_BONES; j++){
                target[j] = pose[0] + q * (pose[j] - pose[0]);
            }
        }
        //In HER frame, so the clip's own leg motion arrives unlagged (leg_params.follow) and only
        //the body's swing is left to the chain.
        leg_chain[leg].Step(target,leg_params,limits,GetWorldRotation());
        if (leg_weight > 0.0f){
            const std::vector<vec3>& sim = leg_chain[leg].Points();
            float carried = 0.0f;
            for (int k = 0; k < ARCHER_LEG_BONES - 1; k++){
                float swing = DynamicChain::SignedAngle(pose[k + 1] - pose[k],sim[k + 1] - sim[k],axis) * leg_weight;
                TurnInWorld(leg_bones[leg][k],axis,swing - carried);
                carried = swing;
                if (k == 0){
                    leg_swing_deg[leg] = swing / ARCHER_DEG2RAD;
                }
            }
        }
        vec3 thigh = leg_bones[leg][1]->GetWorldPosition() - leg_bones[leg][0]->GetWorldPosition();
        vec3 shin = leg_bones[leg][2]->GetWorldPosition() - leg_bones[leg][1]->GetWorldPosition();
        leg_knee_deg[leg] = DynamicChain::SignedAngle(thigh,shin,axis) * (float)knee_sign / ARCHER_DEG2RAD;
    }
}

/*
    Every BONE under the hips, the hips included, parents before children - the whole-body
    overlay's list. The props on the hands and back are not bones and are left out, as in
    BuildUpperMask.
*/
bool ArcherModel::BuildOverlay(){
    Bone* root = FindBone(ARCHER_MODEL_ROOT_BONE);
    if (!root){
        debug->Err("Whole-body overlay is OFF: no bone '%s'\n",ARCHER_MODEL_ROOT_BONE);
        return false;
    }
    overlay_bones.clear();
    overlay_bones.push_back(root);
    std::vector<Object*> subtree;
    root->GetAllSubObjects(subtree);
    for (Object* o : subtree){
        Bone* bone = dynamic_cast<Bone*>(o);
        if (!bone){
            continue;
        }
        overlay_bones.push_back(bone);
    }
    for (Bone* bone : overlay_bones){
        if (std::find(layered_bones.begin(),layered_bones.end(),bone) == layered_bones.end()){
            layered_bones.push_back(bone);
        }
    }
    layered_rot.resize(layered_bones.size());
    layered_pos.resize(layered_bones.size());
    return true;
}

/*
    The overlay: each bone's LOCAL rotation slerped toward the overlay clip's at overlay_time.
    Local is right here where the upper layer needed model space - that was a torso squared up over
    hips that faced another way; this is the whole body, hips included, so the chain is the clip's
    own. Positions are left alone: on this rig they are bone lengths, and the hips' height in the
    air is the app's (air_hip_ref). Reads the keyframes directly, never the clip's playhead, so the
    base may be playing the same clip.
*/
void ArcherModel::ApplyOverlay(){
    for (ObjectAnimation* track : overlay_clip->object_animations){
        Bone* bone = dynamic_cast<Bone*>(track->target);
        if (!bone){
            continue;
        }
        ObjectAnimationKeyFrame key;
        if (!track->Sample(overlay_time,key) || !key.f_rotation){
            continue;
        }
        quat r = quat::slerp(bone->GetRotation(),key.rotation,overlay_weight);
        r.normalize();
        bone->SetRotation(r);
    }
}

bool ArcherModel::BuildUpperMask(){
    Bone* root = FindBone(ARCHER_UPPER_ROOT);
    if (!root){
        debug->Err("Upper-body layer is OFF: no bone '%s'\n",ARCHER_UPPER_ROOT);
        return false;
    }
    /*
        Every BONE from the spine up. GetAllSubObjects also returns the props hanging off the
        hands and the back (the bow, the arrows, the quiver) - those are filtered out, since no clip
        animates them and their transforms belong to Bow (the nocked arrow's position is written
        every tick by SyncBow, and restoring it here would fight that).
    */
    std::vector<Object*> subtree;
    root->GetAllSubObjects(subtree);
    for (Object* o : subtree){
        Bone* bone = dynamic_cast<Bone*>(o);
        if (!bone){
            continue;
        }
        float share = 1.0f;
        if (bone->name == "mixamorig:Spine"){ share = ARCHER_UPPER_SPINE_SHARE; }
        if (bone->name == "mixamorig:Spine1"){ share = ARCHER_UPPER_SPINE1_SHARE; }
        upper_share[bone] = share;
        upper_order.push_back(bone);    //depth-first from the spine: parents before children
        if (std::find(layered_bones.begin(),layered_bones.end(),bone) == layered_bones.end()){
            layered_bones.push_back(bone);
        }
    }
    layered_rot.resize(layered_bones.size());
    layered_pos.resize(layered_bones.size());
    debug->Info("Upper-body layer: %d bones from %s up\n",(int)upper_share.size(),ARCHER_UPPER_ROOT);
    return !upper_share.empty();
}

void ArcherModel::SaveBasePose(){
    for (size_t i = 0; i < layered_bones.size(); i++){
        layered_rot[i] = layered_bones[i]->GetRotation();
        layered_pos[i] = layered_bones[i]->GetPosition();
    }
    f_layered = true;
}

void ArcherModel::RestoreBasePose(){
    if (!f_layered){
        return;
    }
    for (size_t i = 0; i < layered_bones.size(); i++){
        layered_bones[i]->SetRotation(layered_rot[i]);
        layered_bones[i]->SetPosition(layered_pos[i]);
    }
    f_layered = false;
}

/*
    The upper clip, sampled at upper_time and blended over the base on the masked bones only.

    Reads the clip's keyframes directly and never touches its playhead (Animation::time_index), so
    the base can be playing the very same clip - the standing draw is exactly that - without the
    two interfering. Sampled with ObjectAnimation::Sample, the same blend the base uses.

    --- BLENDED IN MODEL SPACE, NOT BONE-LOCAL (Unreal's "mesh space rotation blend") ------------
    An archer stands SIDE-ON: in Standing_DrawArrow her hips face sideways, and every upper-body
    rotation in the clip is authored relative to those hips. Copied as LOCAL rotations onto a
    walk's hips, which face forward, the whole upper body swung round with them - measured, the
    bow ended up pointing into the screen (155 degrees off in the play plane), and the aim override,
    which only turns about the camera axis, folded her over backwards trying to fix it.

    So each bone takes the orientation it has RELATIVE TO THE CHARACTER in the layer clip - its
    model-space rotation, chained up from the clip's own hips - blended against the base's
    model-space rotation by its share, and is then converted back to a local rotation under
    whatever its parent became. Whatever the legs' hips do, the torso squares up exactly as it
    does in the clip, and the graded Spine/Spine1 shares spread the twist over the waist.
*/
/*
    One layer clip's pose at `time`, as MODEL-space rotations of the masked bones - chained from the
    clip's own hips - plus the local positions it keys. Pass 1 of ApplyUpperLayer, for each clip
    the layer is showing (two during its crossfade).
*/
void ArcherModel::LayerClipModel(Animation* clip, float time, std::unordered_map<Object*,quat>& out_model,
                                 std::unordered_map<Object*,vec3>& out_pos){
    //The clip's local rotation for every bone it animates, the root (hips) track included - the
    //chain has to start from the clip's OWN hips.
    std::unordered_map<Object*,quat> clip_local;
    for (ObjectAnimation* track : clip->object_animations){
        if (!track->target){
            continue;
        }
        ObjectAnimationKeyFrame key;
        if (track->Sample(time,key)){
            if (key.f_rotation){
                clip_local[track->target] = key.rotation;
            }
            if (key.f_position){
                out_pos[track->target] = key.position;
            }
        }
    }
    //Parent-before-child order: upper_order is a depth-first walk from the spine.
    for (Bone* bone : upper_order){
        Object* parent = bone->GetParent();
        quat parent_clip;
        std::unordered_map<Object*,quat>::iterator pc = out_model.find(parent);
        if (pc != out_model.end()){
            parent_clip = pc->second;
        }else{
            //The layer's root: its parent is the hips, which the layer does not own - so the
            //clip's hips chain is built from the clip's hips track, up to the skeleton.
            parent_clip = quat().identity();
            std::vector<Object*> chain;
            for (Object* p = parent; p && p != this; p = p->GetParent()){
                chain.push_back(p);
            }
            for (int i = (int)chain.size() - 1; i >= 0; i--){
                std::unordered_map<Object*,quat>::iterator cl = clip_local.find(chain[i]);
                parent_clip = parent_clip * ((cl != clip_local.end()) ? cl->second : chain[i]->GetRotation());
            }
        }
        std::unordered_map<Object*,quat>::iterator cl = clip_local.find(bone);
        out_model[bone] = parent_clip * ((cl != clip_local.end()) ? cl->second : bone->GetRotation());
    }
}

void ArcherModel::ApplyUpperLayer(){
    quat skel_inverse = GetWorldRotation();
    skel_inverse.inverse();

    //Pass 1, before anything is written: every masked bone's BASE model-space rotation (read
    //now - once a parent is rewritten its children's world rotations move with it), the layer's
    //pose, and during its own crossfade the pose it is leaving, mixed toward the new by upper_mix.
    std::unordered_map<Object*,quat> base_model;
    for (Bone* bone : upper_order){
        base_model[bone] = skel_inverse * bone->GetWorldRotation();
    }
    std::unordered_map<Object*,quat> clip_model;
    std::unordered_map<Object*,vec3> clip_pos;
    LayerClipModel(upper_clip,upper_time,clip_model,clip_pos);
    bool f_xfade = (upper_from_clip && upper_mix < 1.0f);
    std::unordered_map<Object*,quat> from_model;
    std::unordered_map<Object*,vec3> from_pos;
    if (f_xfade){
        LayerClipModel(upper_from_clip,upper_from_time,from_model,from_pos);
    }

    //Pass 2: blend against the base in model space, and write each back as a local rotation under
    //its parent's NEW model-space rotation (a masked parent has just been written; the hips have
    //not).
    std::unordered_map<Object*,quat> new_model;
    for (Bone* bone : upper_order){
        float w = upper_weight * upper_share[bone];
        quat target = clip_model[bone];
        if (f_xfade){
            target = quat::slerp(from_model[bone],target,upper_mix);
            target.normalize();
        }
        quat desired = quat::slerp(base_model[bone],target,w);
        desired.normalize();
        new_model[bone] = desired;
        Object* parent = bone->GetParent();
        quat parent_model;
        std::unordered_map<Object*,quat>::iterator pm = new_model.find(parent);
        if (pm != new_model.end()){
            parent_model = pm->second;
        }else{
            parent_model = skel_inverse * parent->GetWorldRotation();
        }
        quat parent_inverse = parent_model;
        parent_inverse.inverse();
        quat local = parent_inverse * desired;
        local.normalize();
        bone->SetRotation(local);

        //Positions stay local: on this rig they are bone lengths and hardly move.
        std::unordered_map<Object*,vec3>::iterator cp = clip_pos.find(bone);
        if (cp != clip_pos.end()){
            vec3 p = cp->second;
            if (f_xfade){
                std::unordered_map<Object*,vec3>::iterator fp = from_pos.find(bone);
                vec3 from = (fp != from_pos.end()) ? fp->second : bone->GetPosition();
                p = from.lerp(p,upper_mix);
            }
            bone->SetPosition(bone->GetPosition().lerp(p,w));
        }
    }
}

float ArcherModel::BowAimDeg(){
    if (!aim_probe){
        return 0.0f;
    }
    vec3 front = aim_probe->GetWorldRotation() * vec3(0.0f,0.0f,1.0f);
    return atan2f(front.y,front.x * aim_facing) / ARCHER_DEG2RAD;
}

void ArcherModel::ApplyAnimation(float time_delta){
    RestoreBasePose();

    Skeleton::ApplyAnimation(time_delta);

    bool f_overlay = (overlay_clip && overlay_weight > 0.0f && !overlay_bones.empty());
    bool f_upper = (upper_clip && upper_weight > 0.0f && !upper_share.empty());
    bool f_aim = (aim_bones[0] && aim_weight > 0.0f);
    bool f_legs = (leg_weight > 0.0f);
    if (f_overlay || f_upper || f_aim || f_legs){
        SaveBasePose();
    }
    //Base, overlay, upper layer, legs, aim: the overlay is part of the whole-body pose the rest
    //are laid over.
    if (f_overlay){
        ApplyOverlay();
    }
    if (f_upper){
        ApplyUpperLayer();
    }
    //Every tick, loose or not - see ApplyLegChains.
    ApplyLegChains(time_delta);

    //The live neutral: where the layered pose points the bow, before the aim turns her.
    aim_pose_deg = BowAimDeg();

    if (f_aim){
        const vec3 axis_world(0.0f,0.0f,1.0f);
        //+ is up in both directions: facing +X, a positive turn about +Z lifts +X toward +Y;
        //facing -X the same lift is a negative turn. Same mirroring as Stage::AimDirection.
        float angle = (aim_target_deg - aim_pose_deg) * aim_weight * aim_facing * ARCHER_DEG2RAD;
        for (int i = 0; i < ARCHER_AIM_BONES; i++){
            TurnInWorld(aim_bones[i],axis_world,angle * aim_shares[i]);
        }
    }

    //What came out: the bow's front in the play plane, relative to facing. See aim_drawn_deg.
    aim_drawn_deg = BowAimDeg();
}

ApplicationArcher::ApplicationArcher():Application(){
    //The title bar, and what input recordings are named and stamped with - a recording refuses to
    //replay in an app with a different name.
    app_name = "Archer";
    debug->Info("ApplicationArcher constructed\n");
}

ApplicationArcher::~ApplicationArcher(){
}

//--- Setup --------------------------------------------------------------------------------------

void ApplicationArcher::Init(void){
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init("shaders/default.vert","shaders/deferred.frag",PIPELINE_DEFERRED)){
        debug->Fatal("Failed to initialise rendering pipeline\n");
    }
    renderer->SetVSync(true);
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");
    /*
        THE ARCHER IS A SKINNED MESH, AND A SKINNED MESH HAS NOWHERE TO BE DRAWN WITHOUT THIS.

        Renderer::skinned_shader is NULL by default and every app that draws one assigns it
        itself. Forgetting it produces NO warning and NO error: the model loads, the skeleton
        binds, the clips play, the object reports itself visible and in the scene - and nothing
        appears. It reads exactly like a failed asset load, which is how an hour went into
        checking the mesh, the bind pose, the weights and the material before the shader.

        Only the FRAGMENT half is shared with default_shader; the vertex half is the one that
        knows about bone matrices. The deferred twin (deferred_shader_skinned) is built by
        Renderer::Init on its own and is not this.
    */
    renderer->skinned_shader = new Shader(shader_skinned_vert_name,shader_lit_frag_name);

    //The engine reaches for it unguarded in places (the Scene panel's asset list, the object_spawn
    //command handler), and it holds the primitive meshes below.
    assetmanager = new AssetManager();

    /*
        THE LOADING SCREEN GOES UP FIRST, before anything slow.

        The window is sized now rather than at the end of Init, so the first loading frame is
        already at the size the game runs at instead of the size the window was created at. The
        overlay is made here rather than by core after Init returns, because the progress text is
        drawn DURING Init - see EnsureOverlay. Everything from here to the end of Init is timed and
        drawn step by step through LoadingStep.
    */
    main_window->Resize(1440,900);      //16:9; a side-scroller wants width far more than height
    CreateTitleScene();
    EnsureOverlay();
    f_loading = true;
    loading_start = std::chrono::steady_clock::now();
    loading_step_start = loading_start;

    //The archer, the blocks and every prop are all scaled unit boxes, so one mesh serves them
    //all - and the archer's collider then cannot disagree with its own mesh, because both come
    //from the size passed to MakePlanarBody.
    unit_mesh   = MakeBox(vec3(1,1,1));
    arrow_mesh  = MakeBox(vec3(ARROW_HALF_LEN * 2.0f,ARROW_DEPTH,ARROW_DEPTH));
    dot_mesh    = MakeSphere(0.075f,10,6);
    if (!unit_mesh || !arrow_mesh || !dot_mesh){
        debug->Fatal("Failed to build the primitive meshes\n");
    }
    //Registered as assets, which is what keeps the app's own pointers valid: the asset holds a
    //reference, so the last Object letting go of one (a debris chunk reaped, every block thrown
    //away by NewGame) cannot free a mesh that is about to be handed to the next. It also puts them
    //in asset_list by name - the handle step 2 of the asset work hangs colliders off.
    assetmanager->AddNewAsset("ar_unit_box",unit_mesh);
    assetmanager->AddNewAsset("ar_arrow_mesh",arrow_mesh);
    assetmanager->AddNewAsset("ar_aim_dot",dot_mesh);

    main_scene = CreateNewScene("Archer");
    main_scene->physics_world = new PhysicsWorld();
    /*
        Gravity for the PROPS ONLY.

        The archer does not use this and neither does an arrow - both are integrated in Stage,
        against ARCHER_GRAVITY and ARROW_GRAVITY, which are four and two times earth's because a
        platformer jump under 9.81 hangs in the air and reads as floaty. The crates and the
        knocked-over targets are the only things this number touches, and they want to look
        physical rather than snappy, so it is nearer the real one - deliberately NOT matched to
        ARCHER_GRAVITY. Two different jobs, two different numbers, and neither is wrong.
    */
    main_scene->physics_world->SetGravity(vec3(0.0f,ARCHER_PROP_GRAVITY,0.0f));
    main_scene->physics_world->SetDebugRendering(false);

    /*
        The loading steps. Numbered by a counter rather than by hand, and checked against
        LOADING_STEPS at the end, so adding a step and forgetting the total is a warning in the
        log rather than a bar that stops short or runs past the end.
    */
    const int LOADING_STEPS = 14;
    int step = 0;
    LoadingStep(step++,LOADING_STEPS,"materials");
    BuildMaterials();
    /*
        The character's file holds the props as well as her, and the first of them - the crate - is
        wanted by BuildProps below, well before BuildArcherModel. So it is read once, here, rather
        than by BuildArcherModel; everything after this finds it already loaded.
    */
    LoadingStep(step++,LOADING_STEPS,"reading the archer");
    gltfloader.LoadGLTFFile(ARCHER_MODEL_ASSET);
    LoadingStep(step++,LOADING_STEPS,"props");
    BuildCrateMesh();
    BuildStandMesh();
    BuildStrawManMesh();
    #ifdef USE_IMGUI
    RegisterPlaceableProps();
    #endif
    LoadingStep(step++,LOADING_STEPS,"the level");
    BuildBlocks();
    //After BuildBlocks, which it hides the melted half of - see the note on the declaration.
    LoadingStep(step++,LOADING_STEPS,"terrain");
    BuildTerrain();
    LoadingStep(step++,LOADING_STEPS,"the archer");
    BuildArcher();
    BuildArcherModel();
    //AFTER the model, for model_scale: an archery stand is drawn at the character's scale, and at
    //the 1.0 it has before BuildArcherModel measures her it came out a doll's-house stand.
    LoadingStep(step++,LOADING_STEPS,"props and bow");
    BuildProps();
    //After it: the grip is derived from a posed clip, and both the bones and the clips arrive with
    //the model. See the note on the declaration.
    BuildBow();
    //After BuildArcherModel too, for the loader and the character's scale - see the declaration.
    LoadingStep(step++,LOADING_STEPS,"foliage");
    BuildFoliage();
    //After BuildArcherModel too - the leaves are sized by model_scale.
    BuildWindLeaves();
    BuildBoulders();
    //The back wall's pines were stood up by BuildTerrain, which runs BEFORE BuildArcherModel - at
    //a model_scale of 1, half size. Again now that the scale is known; placing is cheap, the wall's
    //mesh is not remade.
    PlaceAllBackdropPines();
    LoadingStep(step++,LOADING_STEPS,"vines and rope");
    BuildVines();
    //After BuildProps (the chain it is laid over) and BuildArcherModel (her scale, and the loader).
    BuildRopeSkin();
    LoadingStep(step++,LOADING_STEPS,"arrows and scenery");
    BuildArrowViews();
    BuildAimArc();
    BuildRopeAttachMarkers();
    //Before BuildRange, which shares the popup pool with the range scene along with the arrows.
    BuildHitPopups();
    //After it, for the glyphs. The main level has none yet; this is where they would come from.
    BuildSigns();
    BuildScenery();
    LoadingStep(step++,LOADING_STEPS,"lights and sound");
    //Only the line object here; the field itself is built by the "wind" step below.
    wind_view.Init(main_scene);
    BuildWindStreaks();
    BuildFireflies();
    BuildBackground();
    SetupLights();
    SetupCamera();
    SetupInput();
    SetupSound();
    RegisterCommandHandlers();
    //LAST, because they share the character the lines above built - see the note on ArcherLevel.
    world_scene = main_scene;
    parked_levels.reserve(2);           //BuildExtraLevel holds a reference into it while it builds
    LoadingStep(step++,LOADING_STEPS,"the range");
    range_scene = BuildExtraLevel(STAGE_LEVEL_RANGE,"Range");
    LoadingStep(step++,LOADING_STEPS,"the rope course");
    rope_scene = BuildExtraLevel(STAGE_LEVEL_ROPE,"Rope");
    /*
        The wind field, which used to be solved on the first frame PreRender drew - a fifth of a
        second with the level already on screen and nothing moving. Here it is one more line on
        the loading screen instead. After the extra levels, which leave the main level live, so the
        field is solved for the world's blocks and baked for the world's camera; Wind::Build then
        finds nothing changed on the first real frame and costs nothing.
    */
    LoadingStep(step++,LOADING_STEPS,"wind");
    UpdateWind();
#ifdef USE_MCP
    RegisterMCPTools();
#endif

    /*
        The level entry: she starts lying down and gets up, with the controls locked until she has
        (GETTING UP in Stage.h). After BuildRange, which leaves the main level live. The range gets
        none on its first visit - walking into it is not the level starting - but a restart there
        plays it, because NewGame does. Only when f_level_entry_getup is on, which it is not by
        default - see the note on it.
    */
    if (f_level_entry_getup){
        stage.StartGetUp();
    }

    //The rules denominate everything in ticks and were written against this rate - see ARCHER_TPS
    //in Stage.h, which is the one number they cannot look up for themselves.
    SetPhysicsTPS(ARCHER_TPS);

    //One tick so the first frame is not an empty level.
    main_scene->StepPhysics(1);

    //The last step's time, and the whole load's.
    LoadingStep(step,LOADING_STEPS,NULL);
    if (step != LOADING_STEPS){
        debug->Warn("Loading ran %d steps against LOADING_STEPS %d - the bar was wrong\n",
                    step,LOADING_STEPS);
    }
    f_loading = false;
    debug->Ok("Loaded in %.0f ms\n",
              std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - loading_start).count());

    /*
        AND THE TITLE BECOMES THE LIVE SCENE, now that everything is built: the loading text turns
        into "click to continue", and UpdateTitle hands over to world_scene on the click.

        A direct write is correct HERE and nowhere else, for the reason apps/bomber gives: Init runs
        before Application::Start creates the physics thread, so nothing else reads main_scene yet.
        After this, RequestActiveScene is the only safe way to change it - and since this write
        does not go through OnActiveSceneChanged, the title is entered by hand.

        The panels go down with it (EnterTitle). The loading frames never drew them, so leaving
        them on would have them pop in over the art the moment loading finished - and they cover
        the archer.
    */
    main_scene = title_scene;
    title_return_scene = world_scene;
    EnterTitle();
}

void ApplicationArcher::BuildMaterials(){
    /*
        A flat, readable palette rather than an attempt at a look.

        THE RULE IS THAT COLOUR MEANS A RULE. Ground you stand on, a ledge you will be able to
        grab, a platform you can drop through and a wall that will break are four different
        behaviours, and a prototype's whole job is to let someone see which is which before they
        touch it. When the real assets land this palette is what they have to keep legible.

        Metallic stays low everywhere on purpose: this app turns the skybox off, so a metallic
        surface gives up its diffuse term and gets no environment back in return - see the long
        note on `metallic` in core/Material.h, and breakout's tough bricks, which rendered black.
    */
    struct Simple{
        const char* name;
        vec4 colour;
        float emissive;
        int* out;
    };
    Simple table[] = {
        //Ground: neutral and dark, so everything standing on it reads first.
        { "ar_ground",      vec4(0.26f,0.28f,0.33f,1.0f), 0.04f, &material_ground },
        //Ledge: warm, because it is the one surface with a verb attached to it.
        { "ar_ledge",       vec4(0.78f,0.55f,0.24f,1.0f), 0.12f, &material_ledge },
        //One-way platform: translucent-looking pale blue. It behaves differently from below, and
        //looking lighter than everything solid is the cheapest way to say so.
        { "ar_platform",    vec4(0.55f,0.76f,0.92f,1.0f), 0.22f, &material_platform },
        //Breakable: cracked-brick red, the colour it will burst into.
        { "ar_breakable",   vec4(0.62f,0.28f,0.24f,1.0f), 0.10f, &material_breakable },
        //A tree's trunk: bark brown, and dim - it is behind her and collides with nothing, so it
        //must read as backdrop next to the arms (one-way blue) that are the rule.
        { "ar_trunk",       vec4(0.36f,0.25f,0.16f,1.0f), 0.03f, &material_trunk },
        //The spring plants: a toadstool-red cap and a leaf green, loud on purpose - each moves
        //under her, and reading "this one is springy" before landing on it is the whole design.
        //Not red: red is the timing cue's "now" (material_spring_cue), and the cap at rest must not
        //say it. A pale lilac toadstool.
        { "ar_spring_pad",  vec4(0.66f,0.50f,0.80f,1.0f), 0.25f, &material_spring_pad },
        { "ar_leaf",        vec4(0.38f,0.70f,0.22f,1.0f), 0.20f, &material_leaf },
        //A ramp: slate blue-grey. Its own colour because it is its own rule - past its slip angle
        //the feet do not hold - and it must not read as the plain ground beside it.
        { "ar_ramp",        vec4(0.42f,0.50f,0.62f,1.0f), 0.08f, &material_ramp },
        { "ar_archer",      vec4(0.30f,0.72f,0.42f,1.0f), 0.18f, &material_archer },
        //The character herself. A plain warm off-white, because the model arrives with no textures
        //at all - see the note where it is assigned in BuildArcherModel.
        { "ar_archer_skin", vec4(0.82f,0.74f,0.68f,1.0f), 0.10f, &material_archer_skin },
        //Hanging and climbing. Distinct enough to read at a glance in a screenshot, close enough
        //in hue that it still reads as the same character rather than a different object.
        { "ar_archer_hang", vec4(0.95f,0.80f,0.25f,1.0f), 0.30f, &material_archer_hang },
        //Sliding: an icy blue, standing in for the slide clip that does not exist yet - the colour
        //IS the state in a screenshot, the way the hang's yellow was before its clips.
        { "ar_archer_slide",vec4(0.45f,0.75f,1.00f,1.0f), 0.35f, &material_archer_slide },
        { "ar_crate",       vec4(0.68f,0.52f,0.30f,1.0f), 0.06f, &material_crate },
        { "ar_target",      vec4(0.90f,0.90f,0.88f,1.0f), 0.10f, &material_target },
        //A struck target goes green, so a hit is legible in a screenshot with no HUD at all -
        //which is exactly how this app gets checked over MCP.
        { "ar_target_hit",  vec4(0.30f,0.85f,0.40f,1.0f), 0.45f, &material_target_hit },
        { "ar_arrow",       vec4(0.95f,0.88f,0.55f,1.0f), 0.30f, &material_arrow },
        //Rubble: the breakable red, knocked back and darkened so a pile of chunks reads as debris
        //rather than as a wall that has fallen over intact.
        { "ar_debris",      vec4(0.46f,0.22f,0.19f,1.0f), 0.05f, &material_debris },
        //The placeholder vine: an olive stem and a leaf green a step brighter than the grass, so a
        //vine lying on grass still reads as a separate thing.
        { "ar_vine",        vec4(0.36f,0.42f,0.20f,1.0f), 0.04f, &material_vine },
        { "ar_vine_leaf",   vec4(0.30f,0.55f,0.24f,1.0f), 0.06f, &material_vine_leaf }
    };
    for (size_t i = 0; i < sizeof(table)/sizeof(table[0]); i++){
        Material m;
        m.name = table[i].name;
        m.glsl_material.color = table[i].colour;
        m.glsl_material.metallic = 0.12f;
        m.glsl_material.roughness = 0.62f;
        //emissive.w is what actually makes something glow; emissive.rgb alone is clamped to 1.
        //A little of it keeps a surface in shadow reading as its own colour rather than as a grey
        //lump, which matters in a level lit by one sun.
        m.glsl_material.emissive = vec4(table[i].colour.x,table[i].colour.y,table[i].colour.z,
                                        table[i].emissive);
        renderer->AddMaterial(m);
        *table[i].out = renderer->FindMaterialIndex(m.name);
    }

    /*
        The timing cue's ramp: green, yellow, red, glowing brighter toward red so the best moment
        is also the loudest. A ramp of materials rather than a tint, because an object's colour is
        its material's; SyncSpringPlants swaps the slot every tick.
    */
    for (int i = 0; i < SPRING_CUE_STEPS; i++){
        float f = (float)i / (float)(SPRING_CUE_STEPS - 1);
        vec4 green(0.22f,0.70f,0.25f,1.0f);
        vec4 yellow(0.98f,0.86f,0.18f,1.0f);
        vec4 red(0.98f,0.14f,0.10f,1.0f);
        float h = (f < 0.5f) ? f * 2.0f : (f - 0.5f) * 2.0f;
        vec4 a = (f < 0.5f) ? green : yellow;
        vec4 b = (f < 0.5f) ? yellow : red;
        vec4 colour(a.x + (b.x - a.x) * h,a.y + (b.y - a.y) * h,a.z + (b.z - a.z) * h,1.0f);
        Material m;
        char name[32];
        snprintf(name,sizeof(name),"ar_spring_cue_%i",i);
        m.name = name;
        m.glsl_material.color = colour;
        m.glsl_material.metallic = 0.12f;
        m.glsl_material.roughness = 0.62f;
        m.glsl_material.emissive = vec4(colour.x,colour.y,colour.z,0.25f + 0.45f * f);
        renderer->AddMaterial(m);
        material_spring_cue[i] = renderer->FindMaterialIndex(m.name);
    }

    /*
        The terrain's three, in the order Terrain.cpp writes matid: grass, soil, rock.

        MATCHED TO THE AUTHORED TERRAIN TILES (archer.glb's terrain_tile_big and _round, material
        terrain_texture), so the marching-cubes ground and the tiles read as one set. They used to
        be held dark and desaturated so that ground with no verb could never out-read a ledge;
        the terrain is meant to look like the art now, and the gameplay colours above are the
        blockout's, which the art will replace anyway.

        MEASURED, NOT PICKED: every tile triangle bucketed by its face normal (rock below ny 0.25,
        grass above 0.70, soil between), the texture averaged over it and weighted by area. The
        engine does no sRGB conversion, so that average IS the flat colour that matches. It found
        olive grass on the tops and on the slopes - the tiles' grass lip hangs over their edges -
        and a warm orange-brown earth, grey stones averaged in, on the sides and undersides.

        Terrain.cpp now paints grass by what its cap owns rather than by slope (see "THE SHAPE,
        THROUGH THE SLAB" in Terrain.h), so the lip is the grass and SOIL IS EARTH AGAIN: the
        same earth as a face, lifted a little, because it only ever faces partly up - the top of
        a fillet or a belly's shoulder - and catches more sun than a cliff does.

        AND THE TILES' LIGHTING TERMS, not the table's: metallic 0.4, roughness 0.8, no emissive.
        The same colour under a different metallic renders a different brightness in this app (no
        environment - see the note on the table), so matching one without the other matches
        nothing.
    */
    struct TerrainColour{
        const char* name;
        vec4 colour;
        int* out;
        float fill = 0.0f;      //self-lit share of its own colour - see BACKDROP_FILL
    };
    auto Haze = [](const vec4& c) -> vec4 {
        const vec4 haze(0.22f,0.34f,0.35f,1.0f);     //teal-grey, the rock in the backdrop painting
        const float t = BACKDROP_HAZE;
        return vec4(c.x + (haze.x - c.x) * t,c.y + (haze.y - c.y) * t,c.z + (haze.z - c.z) * t,1.0f);
    };
    TerrainColour terrain[] = {
        { "ar_grass",   vec4(0.38f,0.50f,0.11f,1.0f), &material_grass },
        { "ar_soil",    vec4(0.52f,0.40f,0.19f,1.0f), &material_soil },
        { "ar_rock",    vec4(0.47f,0.35f,0.16f,1.0f), &material_rock },
        /*
            The bank behind the terrain (Backdrop.h): the same three, BACKDROP_HAZE of the way
            toward the painted backdrop's dark teal, so it recedes into the picture behind it
            rather than reading as a second walkway. There is no fog to do this for it. And a FILL,
            because most of it stands in the slab's shadow and came out black.
        */
        { "ar_grass_back", Haze(vec4(0.38f,0.50f,0.11f,1.0f)), &material_grass_back, BACKDROP_FILL },
        { "ar_soil_back",  Haze(vec4(0.52f,0.40f,0.19f,1.0f)), &material_soil_back,  BACKDROP_FILL },
        { "ar_rock_back",  Haze(vec4(0.47f,0.35f,0.16f,1.0f)), &material_rock_back,  BACKDROP_FILL }
    };
    for (size_t i = 0; i < sizeof(terrain)/sizeof(terrain[0]); i++){
        Material m;
        m.name = terrain[i].name;
        m.glsl_material.color = terrain[i].colour;
        m.glsl_material.metallic = 0.4f;
        m.glsl_material.roughness = 0.8f;
        const vec4& c = terrain[i].colour;
        m.glsl_material.emissive = vec4(c.x,c.y,c.z,terrain[i].fill);
        renderer->AddMaterial(m);
        *terrain[i].out = renderer->FindMaterialIndex(m.name);
    }

    {   //The aim arc's beads. UNLIT, which is the right tool and not a hack: these are a HUD
        //element that happens to live in the world, and a readout that dims when it passes into
        //shadow is a readout that lies. See `f_unlit` in core/Material.h.
        Material m;
        m.name = "ar_dot";
        m.glsl_material.color = vec4(1.0f,0.92f,0.45f,1.0f);
        m.glsl_material.f_unlit = 1;
        renderer->AddMaterial(m);
        material_dot = renderer->FindMaterialIndex(m.name);
    }
    {   //The last bead - where the arrow actually ends up. A different colour, because "where it
        //lands" is the one thing the player is really reading off the arc.
        Material m;
        m.name = "ar_dot_hot";
        m.glsl_material.color = vec4(1.0f,0.35f,0.25f,1.0f);
        m.glsl_material.f_unlit = 1;
        renderer->AddMaterial(m);
        material_dot_hot = renderer->FindMaterialIndex(m.name);
    }
}

/*
    One dynamic (or static) box body, pinned to the play plane.

    EVERY prop goes through here, which is the point: the pinning is not something a caller can
    forget. A flat game built on a 3D solver needs this on its first day - anything given a nudge
    out of plane by a spawn impulse or a glancing contact drifts along the axis nobody is watching,
    and in breakout a power-up drifted a unit out of plane and passed clean through the paddle,
    generating no contact at all. See the axis-lock note in core/physics/Physics.h; it is the same
    trap, and this function is this app's answer to it.
*/
Object* ApplicationArcher::MakePlanarBody(Mesh* mesh, const char* name, const vec3& position,
                                          const vec3& size, int material, uint32_t category,
                                          uint32_t collide_mask, float mass, bool f_static,
                                          Object* parent){
    Object* object = new Object();
    object->SetMesh(mesh);
    object->name = name;
    object->SetPosition(position);
    object->SetScale(vec3(size.x,size.y,size.z));
    //A generated mesh carries no material names, so there is nothing for
    //Renderer::UpdateObjectMaterials to resolve over this slot on the next frame.
    object->SetMaterialSlot(0,material);
    //Attached BEFORE AddPhysics, and only ever to an identity parent - see the note on the
    //declaration. `position` is then world as well as local, which is what the body is seeded with.
    if (parent){
        parent->AttachChild(object);
    }else{
        main_scene->AddObject(object);
    }

    Physics* p = object->AddPhysics(main_scene->physics_world);
    if (!p){
        return object;
    }
    /*
        Category and mask before the collider, which is now safe in either order - Physics
        remembers them and every Add*Collider re-applies them. It did not used to be.

        THE MASK IS A PARAMETER RATHER THAN 0xFFFF, and that is not tidiness. "Collides with
        everything" is wrong for this scene in one specific and fatal way: it puts the archer's
        kinematic body in the solver's argument with the static level, which Stage has ALREADY
        resolved by hand. A kinematic body has infinite mass and wins that argument by definition,
        so the symptom is not the archer stopping - it is the archer grinding through the ground
        while every crate and target standing on it gets shoved by the correction. Measured, not
        predicted: with 0xFFFF here, three target boards were flung to x -26, -116 and +78 by an
        archer who had not fired a single arrow.
    */
    p->SetCollisionCategoryBits(category);
    p->SetCollideWithMaskBits(collide_mask);
    //AddBoxCollider takes HALF extents (it goes straight to rp3d's createBoxShape).
    p->AddBoxCollider(vec3(size.x * 0.5f,size.y * 0.5f,size.z * 0.5f),vec3(),quat().identity(),1.0f);
    /*
        SURFACE, WHICH rp3d's DEFAULTS GET WRONG FOR THIS GAME.

        Add*Collider leaves friction at 0.3 and bounciness at 0.5 - see the note above
        AddSphereCollider in core/physics/Physics.h. Half a unit of bounciness is a rubber ball,
        and it shows: a kicked crate hit its neighbour and came straight back past the archer who
        kicked it, ending up LEFT of where it started. 0.3 friction is ice, and a knocked-over
        target board slid for a second and a half after it landed.

        These act on `last_collider`, which is the one added on the line above - every body here
        has exactly one, so this is the right place and the only place.
    */
    p->SetBounciness(0.05f);
    p->SetFrictionCoefficient(0.65f);

    if (f_static){
        //A body from AddPhysics already starts STATIC; saying so is documentation as much as code.
        p->SetStatic(true);
        return object;
    }
    p->SetStatic(false);
    /*
        GRAVITY HAS TO BE TURNED ON. A body from AddPhysics starts STATIC with gravity OFF, which
        is exactly right for a wall and silently wrong for anything that is supposed to fall -
        SetStatic(false) makes it dynamic and does not touch the gravity flag.

        This one cost an afternoon and it is worth knowing what it looks like, because it does not
        look like missing gravity. Nothing floats gently upward; everything LOOKS fine until it is
        touched, and then it never stops. With no gravity there is no weight on the floor, so there
        is no normal force, so there is no friction - a crate given a shove slides forever, and a
        prop given any upward component at all leaves the level and keeps going. It reads exactly
        like an explosion in the solver, and three separate "the archer is flinging things across
        the map" theories were chased before anyone read `gravity: false` off object_get.
    */
    p->SetGravityEnabled(true);
    p->SetMass(mass);
    //The play plane, and rotation only about the axis facing the camera. Without the angular lock
    //a knocked-over target spins out of the plane it was cut from and shows its own edge.
    p->SetLinearLockAxis(vec3(1.0f,1.0f,0.0f));
    p->SetAngularLockAxis(vec3(0.0f,0.0f,1.0f));
    return object;
}

void ApplicationArcher::BuildBlocks(){
    block_objects.clear();
    /*
        Every box goes under one "blockout" object, purely so the scene tree shows the level as one
        collapsible group instead of dozens of loose roots.

        IT MUST STAY AT THE ORIGIN, UNROTATED AND UNSCALED. These are rigid bodies, and a body
        writes its WORLD pose into its object's LOCAL one every tick - which is only right because
        this parent adds nothing. Move it in the Inspector and every box is drawn one offset away
        from where it collides. See the hierarchy note in core/Object.h.

        Made once per level and kept across restarts: NewGame destroys the children, not the group.
    */
    if (!blockout_group){
        blockout_group = new Object();
        blockout_group->name = "blockout";
        main_scene->AddObject(blockout_group);
    }
    for (size_t i = 0; i < stage.blocks.size(); i++){
        const StageBlock& b = stage.blocks[i];
        int material = material_ground;
        switch (b.kind){
            case BLOCK_LEDGE:     material = material_ledge;     break;
            case BLOCK_PLATFORM:  material = material_platform;  break;
            case BLOCK_BREAKABLE: material = material_breakable; break;
            default: break;
        }

        char name[48];
        snprintf(name,sizeof(name),"block_%i",(int)i);
        //Its own depth and z, so the blockout view shows the terrain's pieces standing where the
        //terrain puts them - see StageBlock::z.
        vec3 size(b.hw * 2.0f,b.hh * 2.0f,b.HalfDepth() * 2.0f);

        /*
            A one-way platform gets NO rigid body, on purpose.

            rp3d has no one-way collider, and the rule is Stage's anyway - it already lets the
            archer through from below and holds them up from above. Giving it a real collider would
            make it solid to the crates and the debris too, which is the one thing a one-way
            platform must not be: a crate kicked off the ledge above would land on top of it and
            sit there, in mid-air, on a platform the player walks straight through.
        */
        bool f_collides = (b.kind != BLOCK_PLATFORM);
        Object* object = NULL;
        if (f_collides){
            object = MakePlanarBody(unit_mesh,name,vec3(b.x,b.y,b.z),size,material,
                                    ARCHER_CAT_LEVEL,ARCHER_MASK_LEVEL,0.0f,true,blockout_group);
        }else{
            object = new Object();
            object->SetMesh(unit_mesh);
            object->name = name;
            object->SetPosition(vec3(b.x,b.y,b.z));
            object->SetScale(size);
            object->SetMaterialSlot(0,material);
            blockout_group->AttachChild(object);
        }
        //Still a body - a kicked crate lands on a tile too - but drawn only in the blockout view.
        if (object && b.f_invisible){
            object->SetVisibility(f_show_blockout);
        }
        block_objects.push_back(object);
    }
    /*
        Each tree's TRUNK - Stage::trees. Not a block: it collides with nothing, rules or rp3d,
        because she climbs past it (the arms above are the blocks). So a plain box, set back at
        STAGE_TREE_Z so her body passes in front of it, standing until the tree has a mesh.
    */
    plant_objects.clear();
    spring_plant_objects.clear();
    auto plant_box = [&](const char* name, vec3 at, vec3 size, int material){
        Object* o = new Object();
        o->SetMesh(unit_mesh);
        o->name = name;
        o->SetPosition(at);
        o->SetScale(size);
        o->SetMaterialSlot(0,material);
        blockout_group->AttachChild(o);
        plant_objects.push_back(o);
        return o;
    };
    for (size_t i = 0; i < stage.trees.size(); i++){
        const StageTree& t = stage.trees[i];
        char name[48];
        snprintf(name,sizeof(name),"tree_%i_trunk",(int)i);
        plant_box(name,vec3(t.x,t.base + t.height * 0.5f,STAGE_TREE_Z),
                  vec3(t.radius * 2.0f,t.height,STAGE_TREE_HALF_DEPTH * 2.0f),material_trunk);
    }
    /*
        The SPRING PLANTS - Stage::spring_plants - as boxes until they have meshes: a pad is a cap
        on a stalk, a leaf a thin slab out of its stem. Only the cap and the leaf move, and they are
        placed by SyncSpringPlants every tick from the spring; the sizes here are theirs for good.
        The stalk stands behind her walk line, like the trunk: she stands on the cap, not on it.
    */
    for (size_t i = 0; i < stage.spring_plants.size(); i++){
        const StageSpringPlant& p = stage.spring_plants[i];
        char name[48];
        Object* moving = NULL;
        if (p.kind == SPRING_PAD){
            float stalk = p.root.y - p.base;
            snprintf(name,sizeof(name),"spring_%i_stalk",(int)i);
            plant_box(name,vec3(p.root.x,p.base + stalk * 0.5f,STAGE_TREE_Z * 0.5f),
                      vec3(0.45f,stalk,0.45f),material_trunk);
            snprintf(name,sizeof(name),"spring_%i_cap",(int)i);
            moving = plant_box(name,vec3(p.root.x,p.root.y - 0.2f,0.0f),vec3(p.length,0.4f,1.6f),
                               material_spring_pad);
        }else{
            snprintf(name,sizeof(name),"spring_%i_leaf",(int)i);
            moving = plant_box(name,vec3(p.root.x,p.root.y,0.0f),vec3(p.length,0.12f,1.4f),material_leaf);
        }
        spring_plant_objects.push_back(moving);
    }
    /*
        THE BRANCHES, as thin bark-brown slabs from end to end, turned to their slope. Only her feet
        are ever on one, so it is no deeper than it needs to be to read.
    */
    for (size_t i = 0; i < stage.branches.size(); i++){
        const StageBranch& br = stage.branches[i];
        char name[48];
        snprintf(name,sizeof(name),"branch_%i",(int)i);
        float dx = br.b.x - br.a.x;
        float dy = br.b.y - br.a.y;
        float len = sqrtf(dx * dx + dy * dy);
        float angle = atan2f(dy,dx);
        const float thick = 0.16f;
        //Its top face on the line she walks: the centre half a thickness below the midpoint, along
        //the branch's own down.
        vec3 mid((br.a.x + br.b.x) * 0.5f,(br.a.y + br.b.y) * 0.5f,0.0f);
        vec3 down(sinf(angle),-cosf(angle),0.0f);
        Object* o = plant_box(name,mid + down * (thick * 0.5f),vec3(len,thick,0.35f),material_trunk);
        o->SetRotation(quat(vec3(0.0f,0.0f,1.0f),angle));
    }
    /*
        THE RAMPS - Stage::ramps - the branch's slab again, but ground: a block's full depth and
        thick enough to read as a slope rather than a plank. The wedge under it is left open; the
        rules seal it with the blocks at its ends, and a blockout does not need it filled.
    */
    for (size_t i = 0; i < stage.ramps.size(); i++){
        const StageRamp& r = stage.ramps[i];
        char name[48];
        snprintf(name,sizeof(name),"ramp_%i",(int)i);
        float dx = r.b.x - r.a.x;
        float dy = r.b.y - r.a.y;
        float len = sqrtf(dx * dx + dy * dy);
        float angle = atan2f(dy,dx);
        const float thick = 0.35f;
        vec3 mid((r.a.x + r.b.x) * 0.5f,(r.a.y + r.b.y) * 0.5f,0.0f);
        vec3 down(sinf(angle),-cosf(angle),0.0f);
        Object* o = plant_box(name,mid + down * (thick * 0.5f),vec3(len,thick,STAGE_BLOCK_HALF_DEPTH * 2.0f),
                              material_ramp);
        o->SetRotation(quat(vec3(0.0f,0.0f,1.0f),angle));
    }
    /*
        THE BALANCE GAUGE, while she is on a branch: a dark upright bar beside her head and a marker
        on it at her lean - up the bar is leaning away from the camera, the way Up pushes her. The
        bar's ends are BALANCE_FALL_DEG, and the marker goes green to red as she nears one. It
        stands in for the balance poses until they exist; SyncBalanceGauge places both every tick.
    */
    balance_bar = NULL;
    balance_marker = NULL;
    if (!stage.branches.empty()){
        balance_bar = plant_box("balance_bar",vec3(0.0f,0.0f,0.3f),vec3(0.07f,1.0f,0.07f),material_ground);
        balance_marker = plant_box("balance_marker",vec3(0.0f,0.0f,0.3f),vec3(0.22f,0.09f,0.12f),
                                   material_spring_cue[0]);
        balance_bar->SetVisibility(false);
        balance_marker->SetVisibility(false);
    }
    SyncSpringPlants();
    debug->Info("Built %i level blocks, %i trees, %i spring plants, %i branches and %i ramps\n",
                (int)block_objects.size(),(int)stage.trees.size(),(int)stage.spring_plants.size(),
                (int)stage.branches.size(),(int)stage.ramps.size());
}

void ApplicationArcher::SyncBalanceGauge(){
    if (!balance_bar || !balance_marker){
        return;
    }
    bool f_show = stage.f_on_ground && stage.branch_on >= 0;
    balance_bar->SetVisibility(f_show);
    balance_marker->SetVisibility(f_show);
    if (!f_show){
        return;
    }
    //Behind her, level with her head, so it never covers her face or the way ahead.
    vec3 at(stage.pos.x - stage.facing * 0.75f,stage.pos.y + 0.75f,0.3f);
    balance_bar->SetPosition(at);
    float share = stage.lean / (BALANCE_FALL_DEG * ARCHER_DEG2RAD);
    share = share < -1.0f ? -1.0f : (share > 1.0f ? 1.0f : share);
    balance_marker->SetPosition(at + vec3(0.0f,share * 0.5f,0.02f));
    int step = (int)(stage.BalanceDanger() * (float)(SPRING_CUE_STEPS - 1) + 0.5f);
    step = step < 0 ? 0 : (step >= SPRING_CUE_STEPS ? SPRING_CUE_STEPS - 1 : step);
    balance_marker->SetMaterialSlot(0,material_spring_cue[step]);
}

/*
    The cap sits with its top where the rules put it; the leaf turns about its stem to the rules'
    angle, its top face through the stem so the line she stands on is the leaf's surface. Every
    tick, and on a level switch - these move, unlike every other box in the blockout.
*/
void ApplicationArcher::SyncSpringPlants(){
    for (size_t i = 0; i < stage.spring_plants.size() && i < spring_plant_objects.size(); i++){
        const StageSpringPlant& p = stage.spring_plants[i];
        Object* o = spring_plant_objects[i];
        if (!o){
            continue;
        }
        vec3 size = o->GetScale();
        int material = (p.kind == SPRING_PAD) ? material_spring_pad : material_leaf;
        if ((int)i == spring_cue_plant){
            int step = (int)(spring_cue * (float)(SPRING_CUE_STEPS - 1) + 0.5f);
            material = material_spring_cue[step < 0 ? 0 : (step >= SPRING_CUE_STEPS ? SPRING_CUE_STEPS - 1 : step)];
        }
        o->SetMaterialSlot(0,material);
        if (p.kind == SPRING_PAD){
            o->SetPosition(vec3(p.root.x,p.SurfaceY(p.root.x) - size.y * 0.5f,0.0f));
            continue;
        }
        //Along the leaf by half its length from the stem, then down by half its thickness.
        float c = cosf(p.q);
        float s = sinf(p.q);
        vec3 along(p.side * c,s,0.0f);
        vec3 down(p.side * s,-c,0.0f);
        vec3 at = vec3(p.root.x,p.root.y,0.0f) + along * (p.length * 0.5f) + down * (size.y * 0.5f);
        o->SetPosition(at);
        o->SetRotation(quat(vec3(0.0f,0.0f,1.0f),p.side * p.q));
    }
}

#if ARCHER_TEST_BAY
//Which blocks bay `bay` is built from: 0 below ARCHER_TEST_BAY_SPLIT_Y, 1 above. Shared by the
//mesher and by ApplyBlockoutVisibility, so what is hidden is exactly what was melted.
static TerrainRegion TerrainBayRegion(int bay){
    TerrainRegion r;
    r.x_min = ARCHER_TEST_BAY_X_MIN;
    r.x_max = ARCHER_TEST_BAY_X_MAX;
    r.y_min = (bay == 0) ? -1e30f : ARCHER_TEST_BAY_SPLIT_Y;
    r.y_max = (bay == 0) ? ARCHER_TEST_BAY_SPLIT_Y : 1e30f;
    return r;
}

//True if any bay melts this block.
static bool IsInTerrainBay(const StageBlock& b){
    for (int i = 0;i < ARCHER_TEST_BAY_COUNT;i++){
        if (TerrainBayRegion(i).Contains(b)){
            return true;
        }
    }
    return false;
}
#endif

void ApplicationArcher::BuildTerrain(){
    terrain_objects.clear();
    terrain_back_objects.clear();
    backdrop_reach.clear();
    melted_blocks.clear();
#if ARCHER_TEST_BAY
    /*
        Checked once, here, rather than trusted. The 256-entry triangulation table in
        core/MarchingCubes.cpp is copied data, and a single wrong digit in it produces a hole a few
        cells wide somewhere on a silhouette - which is not reliably visible in a screenshot and is
        miserable to find by looking at geometry. Two milliseconds at startup.
    */
    MarchingCubesSelfTest();

    //One object per bay, made whether or not the bay has anything in it yet: an edit can move
    //boxes into an empty bay, and RemeshTerrainBay then only has to fill the object in.
    for (int i = 0;i < ARCHER_TEST_BAY_COUNT;i++){
        char name[48];
        snprintf(name,sizeof(name),"terrain_bay_%i",i);
        Object* object = new Object();
        object->name = name;
        /*
            IDENTITY TRANSFORM, because the mesh is already in world coordinates.

            Terrain.cpp bakes the bay's world position into the vertices rather than building
            something centred and placing it, for the same reason CreateMeshFromHeightmap does -
            the normals are computed from the final positions, and a non-uniform Object scale
            applied afterwards does not retroactively fix them. See the note in apps/tank/Heightmap.h.
        */
        object->SetPosition(vec3(0.0f,0.0f,0.0f));
        object->SetScale(vec3(1.0f,1.0f,1.0f));
        //Slot per matid, in the order Terrain.cpp writes them.
        object->SetMaterialSlot(0,material_grass);
        object->SetMaterialSlot(1,material_soil);
        object->SetMaterialSlot(2,material_rock);
        main_scene->AddObject(object);
        terrain_objects.push_back(object);

        //Its bank, the same way: world-space vertices, identity transform, one slot per matid.
        snprintf(name,sizeof(name),"terrain_back_%i",i);
        Object* back = new Object();
        back->name = name;
        back->SetPosition(vec3(0.0f,0.0f,0.0f));
        back->SetScale(vec3(1.0f,1.0f,1.0f));
        back->SetMaterialSlot(0,material_grass_back);
        back->SetMaterialSlot(1,material_soil_back);
        back->SetMaterialSlot(2,material_rock_back);
        main_scene->AddObject(back);
        terrain_back_objects.push_back(back);
        backdrop_reach.push_back(0.0f);

        RemeshTerrainBay(i);
    }

    ApplyBlockoutVisibility();
    debug->Info("Terrain: %i bays built, %i blockout boxes hidden\n",
                (int)terrain_objects.size(),(int)melted_blocks.size());
#endif
}

/*
    Meshes one bay from the blocks as they stand in `stage` right now. RENDER THREAD ONLY - it ends
    in Mesh::SetMeshData.

    The bay keeps its Object and its Mesh; only the vertices are replaced. So regenerating never
    adds or removes anything from the scene, which is what makes it safe to do in the middle of a
    running game - nothing the render thread is walking changes shape underneath it. An empty bay
    is hidden rather than given an empty mesh, which SetMeshData cannot take.
*/
void ApplicationArcher::RemeshTerrainBay(int bay){
#if ARCHER_TEST_BAY
    if (bay < 0 || bay >= (int)terrain_objects.size() || !terrain_objects[bay]){
        return;
    }
    Object* object = terrain_objects[bay];
    //The bank behind it first - it follows the same blocks, so the two regenerate together.
    RemeshBackdrop(bay);
    //Every bay on the defaults. The four-way comparison those were chosen from is in the history
    //and in terrain_plan.md; Terrain.h's TerrainParams is where to argue with them.
    TerrainParams params;
    TerrainStats stats;
    std::vector<vertex> verts;
    if (!BuildTerrainVerts(stage.blocks,TerrainBayRegion(bay),params,verts,&stats) || verts.empty()){
        object->SetVisibility(false);
        debug->Info("Terrain bay %i has no solid blocks in it - hidden\n",bay);
        return;
    }
    Mesh* mesh = object->GetMesh();
    if (!mesh){
        mesh = new Mesh();
        object->SetMesh(mesh);      //takes the reference; Destroy drops it and frees the mesh
    }
    mesh->SetMeshData(verts.data(),(int)verts.size());
    object->SetVisibility(true);

    debug->Info("Terrain bay %i: %i blocks, %i tris, %zu samples, "
                "dip %.4f rise %.4f over %i probes (k=%.2f r=%.2f n=%.2f)\n",
                bay,stats.num_blocks,stats.num_triangles,stats.num_samples,
                stats.worst_dip,stats.worst_rise,stats.num_probes,
                params.smooth_k,params.round_r,params.noise_amp);
    /*
        THE ONE ASSERTION THAT MATTERS, and it is logged rather than asserted so that a bad
        layout still renders and can be looked at.

        A dip is the surface sitting BELOW a collider's exposed top face, which is the archer
        standing in mid-air. It is a fifth of a unit at worst, invisible in a screenshot and
        unmistakable under the feet, so it is measured instead of eyeballed.
    */
    if (stats.worst_dip > 0.02f){
        debug->Err("Terrain bay %i DIPS %.4f below a top face - the archer will float there. "
                   "See the top-pinning note in Terrain.h.\n",bay,stats.worst_dip);
    }
#else
    (void)bay;
#endif
}

/*
    The mesher's settings for the bank behind the terrain. Rounder and softer than the slab's - a
    bank far off wants humps, not boxes with rounded edges - with more of both noises, and coarser
    cells, because it is further away and every sample of it is spent on something seen smaller.
*/
static TerrainParams BackdropTerrainParams(){
    TerrainParams p;
    p.cell_xy     = 0.35f;
    p.cell_z      = 0.35f;
    p.round_r     = 0.60f;
    p.smooth_k    = 1.00f;
    p.noise_amp   = 0.25f;
    p.coarse_amp  = 0.60f;
    p.coarse_freq = 0.22f;
    return p;
}

/*
    The pines on one bay's back wall: children of its terrain_back object, reused in order and the
    spares hidden - the foliage pool's arrangement, with the pool being the object's own children.
    The object is identity, so a child's local transform is its world one. RENDER THREAD (the mesh
    load), from RemeshBackdrop.
*/
void ApplicationArcher::PlaceBackdropPines(Object* wall, const std::vector<BackdropTree>& trees){
    if (!wall){
        return;
    }
    if (!pine_mesh){
        pine_mesh = gltfloader.GetMeshFromNode("pine_tree",&pine_materials,false);
        if (!pine_mesh){
            debug->Err("No 'pine_tree' in %s - the back wall grows no pines\n",ARCHER_MODEL_ASSET);
            return;
        }
        pine_mesh->Retain();
        renderer->AddMaterials(pine_materials);
    }
    std::vector<Object*> pool(wall->children.begin(),wall->children.end());
    for (size_t i = 0; i < trees.size(); i++){
        const BackdropTree& t = trees[i];
        Object* o = NULL;
        if (i < pool.size()){
            o = pool[i];
        }else{
            o = new Object();
            o->SetPickability(false);
            o->SetMesh(pine_mesh);
            o->TakeMaterialNames(pine_materials);
            wall->AttachChild(o);
        }
        char name[32];
        snprintf(name,sizeof(name),"pine.%i",(int)i);
        o->name = name;
        o->SetPosition(vec3(t.x,t.y,t.z));
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),t.yaw));
        float s = model_scale * t.scale;
        o->SetScale(vec3(s,s,s));
        o->SetVisibility(true);
    }
    for (size_t i = trees.size(); i < pool.size(); i++){
        pool[i]->SetVisibility(false);
    }
}

void ApplicationArcher::PlaceAllBackdropPines(){
#if ARCHER_TEST_BAY
    for (int bay = 0; bay < (int)terrain_back_objects.size(); bay++){
        TerrainRegion region = TerrainBayRegion(bay);
        BackdropParams bparams;
        std::vector<StageBlock> bank;
        std::vector<BackdropTree> trees;
        BuildBackdropBlocks(stage.blocks,region.x_min,region.x_max,region.y_min,region.y_max,bparams,bank,&trees);
        PlaceBackdropPines(terrain_back_objects[bay],trees);
    }
#endif
}

/*
    The bank behind one bay (Backdrop.h). RENDER THREAD ONLY, from RemeshTerrainBay.

    Built from its own blocks, never Stage::blocks - nothing collides with it. Hidden for a bay with
    no ground, which is the upper one.

    CHECKED ON THE MESH: over the ground's grass a hump is seen, and it must not come forward over
    her walking line. BackdropParams::front_gap is meant to hold the whole surface behind the slab's
    back face; this measures how far forward of that face it actually came, and says so if it did.
*/
void ApplicationArcher::RemeshBackdrop(int bay){
#if ARCHER_TEST_BAY
    if (bay < 0 || bay >= (int)terrain_back_objects.size() || !terrain_back_objects[bay]){
        return;
    }
    Object* object = terrain_back_objects[bay];
    TerrainRegion region = TerrainBayRegion(bay);
    BackdropParams bparams;
    std::vector<StageBlock> bank;
    std::vector<int> grounds;
    std::vector<BackdropTree> trees;
    BuildBackdropBlocks(stage.blocks,region.x_min,region.x_max,region.y_min,region.y_max,bparams,bank,
                        &trees,&grounds);
    PlaceBackdropPines(object,trees);

    //Its own blocks and nothing else, so an all-space region: every one of them melts.
    TerrainRegion all;
    all.x_min = -1e30f; all.x_max = 1e30f;
    all.y_min = -1e30f; all.y_max = 1e30f;
    TerrainParams params = BackdropTerrainParams();
    TerrainStats stats;
    std::vector<vertex> verts;
    if (bank.empty() || !BuildTerrainVerts(bank,all,params,verts,&stats) || verts.empty()){
        object->SetVisibility(false);
        return;
    }
    Mesh* mesh = object->GetMesh();
    if (!mesh){
        mesh = new Mesh();
        object->SetMesh(mesh);
    }
    mesh->SetMeshData(verts.data(),(int)verts.size());
    object->SetVisibility(true);

    float reach = -1e30f;
    for (size_t v = 0; v < verts.size(); v++){
        const vec3& p = verts[v].pos;
        for (size_t g = 0; g < grounds.size(); g++){
            const StageBlock& ground = stage.blocks[grounds[g]];
            if (p.y > ground.Top() && p.x >= ground.Left() && p.x <= ground.Right()){
                reach = fmaxf(reach,p.z - ground.Back());
            }
        }
    }
    backdrop_reach[bay] = reach;
    debug->Info("Terrain bay %i back wall: %i columns and ridges behind %i ground block(s), %i pines, "
                "%i tris, %zu samples, reach %.2f past the slab's back over its grass\n",
                bay,(int)bank.size(),(int)grounds.size(),(int)trees.size(),stats.num_triangles,
                stats.num_samples,reach);
    if (reach > 0.0f){
        debug->Warn("Terrain bay %i: the backdrop comes %.2f forward of the slab's back face above its "
                    "grass - raise BackdropParams::front_gap\n",bay,reach);
    }
#else
    (void)bay;
#endif
}

/*
    The editor's round trip: boxes moved in the Inspector become the level, and the terrain is
    remeshed over them. RENDER THREAD, from PreRender - see the request flag in the header.

    THE OBJECTS ARE THE SOURCE OF TRUTH HERE, NOT Stage::blocks. The Inspector moves a block's
    Object (and its body, which follows), but the rules never hear about it - Stage still sweeps
    the archer against the box where BuildLevel put it. So each block's centre and size are read
    back off its object first: position is the centre, scale the full size, because every block is
    the unit cube scaled - see BuildBlocks. Rotation is ignored; a StageBlock is axis-aligned and
    so is the collision for the archer and the arrows.

    Then KeepBlockLayout, so the next restart rebuilds the moved boxes rather than the original
    ones - otherwise a fall off the world would snap every box back under a terrain that no longer
    matches them.

    Under physics_mutex for the whole of it: this writes the Stage the tick reads, and reads the
    objects the tick writes.
*/
void ApplicationArcher::RegenerateTerrain(){
    main_scene->AtTickBoundary([this](){
        int moved = 0;
        for (size_t i = 0;i < stage.blocks.size() && i < block_objects.size();i++){
            Object* object = block_objects[i];
            StageBlock& b = stage.blocks[i];
            if (!object || !b.f_alive){
                continue;       //a broken wall has no box left to read
            }
            vec3 p = object->GetWorldPosition();
            vec3 s = object->GetScale();
            float hw = fabsf(s.x) * 0.5f;
            float hh = fabsf(s.y) * 0.5f;
            //Depth and z too: scaling or sliding a box through the slab is how a piece is set back
            //or thinned in the editor. The rules never read either, so this cannot move collision.
            float hd = fabsf(s.z) * 0.5f;
            if (b.x != p.x || b.y != p.y || b.hw != hw || b.hh != hh ||
                b.z != p.z || b.HalfDepth() != hd){
                moved++;
            }
            b.x = p.x;
            b.y = p.y;
            b.hw = hw;
            b.hh = hh;
            b.z = p.z;
            b.depth = hd;
            if (fabsf(b.z) + STAGE_BLOCK_MIN_COVER > hd){
                debug->Warn("block_%i no longer covers the play plane (z %.2f, half-depth %.2f) - "
                            "crates at z 0 can fall through it\n",(int)i,b.z,hd);
            }
        }
        stage.KeepBlockLayout();
        //The terrain belongs to the main level's test bay; on the range this still keeps the
        //moved boxes, there is just nothing to mesh.
        for (int i = 0;i < (int)terrain_objects.size();i++){
            RemeshTerrainBay(i);
        }
        ApplyBlockoutVisibility();
        //The plants grow on the boxes too, so a moved box takes its garden with it - and its rocks.
        ScatterFoliageObjects();
        ScatterBoulderObjects();
        debug->Info("Terrain regenerated: %i of %i blocks moved, %i hidden under terrain\n",
                    moved,(int)stage.blocks.size(),(int)melted_blocks.size());
        last_regen_moved = moved;
        last_regen_hidden = (int)melted_blocks.size();
    });
    terrain_generation++;
}

void ApplicationArcher::PreRender(void){
    /*
        On the title, only the title. Everything below is the world's view work and reads
        main_scene for its camera and its tick, which here would be the title's - the wind grid
        baked for an ortho camera two units wide. The flags it services keep until the world is up.
    */
    if (title_scene && (main_scene == title_scene)){
        FitTitleQuad(main_window->width,main_window->height);
        return;
    }
    if (f_regenerate_terrain.exchange(false)){
        RegenerateTerrain();
    }
    if (f_rescatter_foliage.exchange(false)){
        main_scene->AtTickBoundary([this](){ ScatterFoliageObjects(); });
    }
    if (f_rope_skin_stale.exchange(false)){
        main_scene->AtTickBoundary([this](){ RebuildRopeSkinWeights(); });
    }
    UpdateWind();
}

//--- Wind ---------------------------------------------------------------------------------------

//The spacing of the grid the renderer bends plants with. Half a unit: the smallest eddy's radius,
//so an eddy still reads in the grass, and about 5000 nodes over a normal view (~0.5 ms to bake).
#define WIND_GRID_STEP      0.5f
//Plants stand up to a slab's depth behind the play plane, where the camera sees further out -
//so the grid covers a quarter more than the plane's own view.
#define WIND_GRID_PAD       0.25f
#define WIND_GRID_MAX       256

void ApplicationArcher::UpdateWind(){
    //Copied rather than read in place: the blocks belong to the physics thread, and the build
    //can take long enough that holding the simulation for it would show.
    std::vector<StageBlock> blocks;
    int64_t tick = 0;
    main_scene->AtTickBoundary([&](){
        blocks = stage.blocks;
        tick = (int64_t)main_scene->GetPhysicsTick();
    });
    std::lock_guard<std::mutex> lock(wind_mutex);
    if (wind.Build(blocks,wind_params)){
        const WindStats& st = wind.Stats();
        debug->Info("Wind: %dx%d nodes, %d obstacles (%d end walls left out), %d iterations, %.1f ms, %d corners\n",
                    st.nx,st.ny,st.obstacles,st.end_walls,st.iterations,st.build_ms,st.corners);
    }

    /*
        The grid for default.vert. Its origin snaps to whole steps, so as the camera moves the
        nodes stay where they were in the world and a plant between two of them sees the same
        wind - otherwise every pan would shimmer the whole meadow.
    */
    float x0, y0, x1, y1;
    if (WindViewRect(main_scene->camera,WIND_GRID_PAD,x0,y0,x1,y1)){
        auto t0 = std::chrono::steady_clock::now();
        float gx0 = floorf(x0 / WIND_GRID_STEP) * WIND_GRID_STEP;
        float gy0 = floorf(y0 / WIND_GRID_STEP) * WIND_GRID_STEP;
        wind_grid_w = std::min((int)ceilf((x1 - gx0) / WIND_GRID_STEP) + 1,WIND_GRID_MAX);
        wind_grid_h = std::min((int)ceilf((y1 - gy0) / WIND_GRID_STEP) + 1,WIND_GRID_MAX);
        wind_grid.resize(2 * wind_grid_w * wind_grid_h);
        wind.Bake(tick,gx0,gy0,WIND_GRID_STEP,wind_grid_w,wind_grid_h,wind_grid.data());
        //Simulation time, so a paused game holds the grass still, flutter and all.
        renderer->SetWindField(wind_grid.data(),wind_grid_w,wind_grid_h,gx0,gy0,
                               WIND_GRID_STEP,WIND_GRID_STEP,(float)((double)tick * ARCHER_DT));
        wind_bake_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
    }else{
        renderer->ClearWindField();
    }
    UpdateWindStreaks(tick);
    UpdateFireflies(tick);

    if (!f_show_wind){
        if (wind_view.IsVisible()){
            wind_view.SetVisible(false);
        }
        return;
    }
    wind_view.SetVisible(true);
    auto t0 = std::chrono::steady_clock::now();
    wind_view.Update(wind,tick,main_scene->camera);
    wind_view_ms = std::chrono::duration<float,std::milli>(std::chrono::steady_clock::now() - t0).count();
}

//--- Foliage ------------------------------------------------------------------------------------

//The archer.glb node for each FoliageKind, in that enum's order.
static const char* FOLIAGE_NODES[FOLIAGE_KIND_COUNT] = { "fern_1", "fern_2", "flower", "grass_1", "grass_2" };

/*
    The leaf model, and its four looks: fresh, dark, yellowing, turning.

    TWO WAYS TO TINT, because the lit shader IGNORES the material colour whenever there is a
    texture. The 2026-09-26 export of leaf_small has NO texture (and no colour - it came out plain
    white), so the colour column is what shows. Should it come back textured, the colour stops
    mattering and the brightness (how much light it reflects) and faint emissive warmth carry the
    variation instead - both columns are always written, so either export works.
*/
#define WIND_LEAF_NODE      "leaf_small"
static const struct { float cr, cg, cb; float brightness; float er, eg, eb, strength; } WIND_LEAF_TINT[4] = {
    { 0.36f, 0.56f, 0.18f,   1.00f, 0.0f, 0.0f, 0.0f, 0.00f },
    { 0.20f, 0.38f, 0.13f,   0.75f, 0.0f, 0.0f, 0.0f, 0.00f },
    { 0.62f, 0.62f, 0.20f,   1.10f, 1.0f, 0.8f, 0.2f, 0.05f },
    { 0.72f, 0.40f, 0.14f,   0.90f, 1.0f, 0.45f,0.1f, 0.08f },
};

/*
    The pool of wind-blown leaves. RENDER THREAD, at Init (SetMeshData uploads).

    leaf_small is re-baked the way the crate is: its node carries an UNAPPLIED transform (scale 3,
    +90 degrees about X), which is applied here, and the mesh is re-centred on its bounds - it was
    authored with its origin at the stem, and a leaf tumbling about its stem looks like a leaf on
    a string. After the node's rotation it lies flat, face up, blade along +Z, which is exactly the
    resting pose; the tumble and the rest are both rotations of that.
*/
void ApplicationArcher::BuildWindLeaves(){
    std::vector<Material> materials;
    Mesh* mesh = gltfloader.GetMeshFromNode(WIND_LEAF_NODE,&materials,false);
    if (!mesh){
        debug->Warn("No '%s' in %s - no leaves on the wind\n",WIND_LEAF_NODE,ARCHER_MODEL_ASSET);
        return;
    }
    std::vector<vertex> verts = mesh->GetVertices();
    if (verts.empty()){
        debug->Warn("'%s' has no CPU copy of its vertices - no leaves on the wind\n",WIND_LEAF_NODE);
        return;
    }
    quat node_rot = gltfloader.GetNodeRotation(WIND_LEAF_NODE);
    vec3 node_scale = gltfloader.GetNodeScale(WIND_LEAF_NODE);
    vec3 lo(1e9f,1e9f,1e9f), hi(-1e9f,-1e9f,-1e9f);
    for (vertex& v : verts){
        v.pos = node_rot * vec3(v.pos.x * node_scale.x,v.pos.y * node_scale.y,v.pos.z * node_scale.z);
        v.normal = node_rot * v.normal;
        v.tangent = node_rot * v.tangent;
        lo = vec3(fminf(lo.x,v.pos.x),fminf(lo.y,v.pos.y),fminf(lo.z,v.pos.z));
        hi = vec3(fmaxf(hi.x,v.pos.x),fmaxf(hi.y,v.pos.y),fmaxf(hi.z,v.pos.z));
    }
    vec3 centre = (lo + hi) * 0.5f;
    for (vertex& v : verts){
        v.pos = v.pos - centre;
    }
    mesh->SetMeshData(verts.data(),(int)verts.size());
    assetmanager->AddNewAsset("ar_wind_leaf_mesh",mesh);
    leaf_mesh = mesh;

    //Four copies of its materials, one per tint - they share the texture, and so its unit.
    for (int t = 0; t < WIND_LEAF_TINTS; t++){
        leaf_materials[t] = materials;
        for (Material& m : leaf_materials[t]){
            m.name += "@wind_leaf_" + std::to_string(t);
            m.glsl_material.color = vec4(WIND_LEAF_TINT[t].cr,WIND_LEAF_TINT[t].cg,WIND_LEAF_TINT[t].cb,1.0f);
            m.glsl_material.brightness *= WIND_LEAF_TINT[t].brightness;
            m.glsl_material.emissive = vec4(WIND_LEAF_TINT[t].er,WIND_LEAF_TINT[t].eg,WIND_LEAF_TINT[t].eb,
                                            WIND_LEAF_TINT[t].strength);
            //The export's 0.4 metallic, with no skybox to reflect, only darkens - the same fix
            //BuildArcherModel makes for the character.
            m.glsl_material.metallic = fminf(m.glsl_material.metallic,0.10f);
        }
        renderer->AddMaterials(leaf_materials[t]);
    }

    leaf_group = new Object();
    leaf_group->name = "wind_leaves";
    main_scene->AddObject(leaf_group);
    for (int i = 0; i < WIND_LEAF_POOL; i++){
        Object* o = new Object();
        o->name = "wind_leaf." + std::to_string(i);
        o->SetPickability(false);
        o->SetCastsShadow(false);
        o->SetMesh(mesh);
        o->TakeMaterialNames(leaf_materials[i % WIND_LEAF_TINTS]);
        o->SetVisibility(false);
        leaf_group->AttachChild(o);
        leaf_objects.push_back(o);
    }
    leaf_swarm.params.tints = WIND_LEAF_TINTS;
    leaf_swarm.params.density = 0.1f;
    leaf_swarm.params.pad = 1.5f;
    leaf_swarm.params.max_count = WIND_LEAF_POOL;
    vec3 size = hi - lo;
    debug->Info("Wind leaves: '%s' %.3f x %.3f x %.3f after its node transform, pool of %d\n",
                WIND_LEAF_NODE,size.x,size.y,size.z,WIND_LEAF_POOL);
}

/*
    One tick of the leaves, and their Objects posed from it. PHYSICS THREAD, from RunSimulationTick
    after UpdateCamera. Under wind_mutex: the render thread builds and retunes the field.

    A leaf in flight is its resting pose turned about its tumble axis; one lying down is turned
    only about +Y (its heading) and then TILTED toward the camera, since a leaf lying truly flat is
    edge-on to a side view and all but disappears.
*/
void ApplicationArcher::StepWindLeaves(){
    if (leaf_objects.empty()){
        return;
    }
    int shown = 0;
    if (f_wind_leaves){
        std::lock_guard<std::mutex> lock(wind_mutex);
        float x0, y0, x1, y1;
        //The bare view: the swarm grows it by its own `pad`, and needs the unpadded one to know
        //which leaves are on screen.
        if (wind.IsBuilt() && WindViewRect(main_scene->camera,0.0f,x0,y0,x1,y1)){
            leaf_swarm.params.max_count = std::min(std::max(leaf_swarm.params.max_count,0),(int)WIND_LEAF_POOL);
            leaf_swarm.params.count = std::min(std::max(leaf_swarm.params.count,0),(int)WIND_LEAF_POOL);
            leaf_swarm.Step(wind,(int64_t)main_scene->GetPhysicsTick(),x0,y0,x1,y1);
            float base = model_scale * leaf_scale;
            for (const Leaf& l : leaf_swarm.leaves){
                Object* o = leaf_objects[shown++];
                quat q;
                if (l.state == LEAF_FLYING){
                    q = quat(vec3(l.axis[0],l.axis[1],l.axis[2]),l.angle);
                }else{
                    q = quat(vec3(1.0f,0.0f,0.0f),l.tilt) * quat(vec3(0.0f,1.0f,0.0f),l.yaw);
                }
                float s = base * l.size * l.fade;
                o->SetPosition(vec3(l.x,l.y,l.z));
                o->SetRotation(q);
                o->SetScale(vec3(s,s,s));
                o->SetVisibility(true);
            }
        }
    }
    for (size_t i = shown; i < leaf_objects.size(); i++){
        if (leaf_objects[i]->IsVisible()){
            leaf_objects[i]->SetVisibility(false);
        }
    }
}

/*
    The streaks' shader, mesh and object. RENDER THREAD, at Init. The mesh starts empty and the
    object hidden; UpdateWindStreaks fills and shows it.
*/
void ApplicationArcher::BuildWindStreaks(){
    streak_shader = new Shader("shaders/wind_streak.vert","shaders/wind_streak.frag");
    streak_shader->uniform_callback = std::bind(&ApplicationArcher::SetStreakUniforms,this);
    streak_shader_index = renderer->AddCustomShader(streak_shader);
    streak_mesh = new Mesh();
    streak_mesh->num_materials = 1;
    streak_object = new Object();
    streak_object->name = "wind_streaks";
    streak_object->SetMesh(streak_mesh);
    streak_object->SetPickability(false);
    streak_object->SetCastsShadow(false);
    streak_object->SetVisibility(false);
    main_scene->AddObject(streak_object);
    streak_object->UpdatePhysicsState();
}

//Called by the custom-shader pass before the draw; the pass puts all three states back after.
void ApplicationArcher::SetStreakUniforms(){
    //Translucent: tested against the solid scene, but writing no depth of their own, and both
    //faces - a ribbon turned toward the camera can still show its back where it curls.
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    streak_shader->Setvec3("streak_color",streak_color);
}

void ApplicationArcher::UpdateWindStreaks(int64_t tick){
    if (!streak_object){
        return;
    }
    float x0, y0, x1, y1;
    if (!f_wind_streaks || !WindViewRect(main_scene->camera,0.0f,x0,y0,x1,y1)){
        streak_object->SetVisibility(false);
        streak_last_tick = tick;
        return;
    }
    //Catch up on the ticks since the last frame - none while paused, a few after a stall (capped,
    //so a long stall does not freeze a frame running them all).
    int64_t steps = (streak_last_tick < 0) ? 1 : tick - streak_last_tick;
    if ((steps < 0) || (steps > 8)){
        steps = 1;
    }
    for (int64_t k = steps - 1; k >= 0; k--){
        streak_swarm.Step(wind,tick - k,x0,y0,x1,y1);
    }
    streak_last_tick = tick;

    vec3 eye = main_scene->camera->GetPosition();
    streak_swarm.BuildRibbons(eye.x,eye.y,eye.z,streak_ribbons);
    if (streak_ribbons.empty()){
        streak_object->SetVisibility(false);
        return;
    }
    streak_vertices.resize(streak_ribbons.size());
    for (size_t i = 0; i < streak_ribbons.size(); i++){
        const StreakVertex& r = streak_ribbons[i];
        vertex& v = streak_vertices[i];
        v.pos = vec3(r.x,r.y,r.z);
        v.normal = vec3(r.alpha,0.0f,0.0f);         //the alpha - see wind_streak.vert
        v.tangent = vec3(1.0f,0.0f,0.0f);
        v.uv = vec2(r.u,r.v);
        v.matid = 0;
    }
    streak_mesh->SetMeshData(streak_vertices.data(),(int)streak_vertices.size());
    //SetMeshData puts the mesh back to MESH_MODE_NORMAL every time, so this goes after it.
    streak_mesh->mesh_mode = MESH_MODE_SHADER;
    streak_mesh->custom_shader_index = streak_shader_index;
    streak_object->SetVisibility(true);
}

//The fireflies' shader, mesh and object, and their light group. RENDER THREAD, at Init.
void ApplicationArcher::BuildFireflies(){
    firefly_shader = new Shader("shaders/firefly.vert","shaders/firefly.frag");
    firefly_shader->uniform_callback = std::bind(&ApplicationArcher::SetFireflyUniforms,this);
    firefly_shader_index = renderer->AddCustomShader(firefly_shader);
    firefly_mesh = new Mesh();
    firefly_mesh->num_materials = 1;
    firefly_object = new Object();
    firefly_object->name = "fireflies";
    firefly_object->SetMesh(firefly_mesh);
    firefly_object->SetPickability(false);
    firefly_object->SetCastsShadow(false);
    firefly_object->SetVisibility(false);
    main_scene->AddObject(firefly_object);
    firefly_object->UpdatePhysicsState();

    for (int i = 0; i < FIREFLY_LIGHTS; i++){
        PointLight* light = new PointLight();
        light->name = "firefly_light." + std::to_string(i);
        light->color = vec3(0.72f,1.0f,0.35f);
        light->brightness = 0.0f;
        //A glow, not a lamp: no shadow, which point lights could only get from the occluder field
        //anyway, and a soft source.
        light->f_casts_shadow = false;
        light->SetPickability(false);
        main_scene->AddObject(light);
        firefly_lights[i] = light;
    }
}

void ApplicationArcher::SetFireflyUniforms(){
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    firefly_shader->Setfloat("halo_strength",firefly_halo);
}

void ApplicationArcher::UpdateFireflies(int64_t tick){
    if (!firefly_object){
        return;
    }
    float x0, y0, x1, y1;
    if (!f_fireflies || !WindViewRect(main_scene->camera,0.0f,x0,y0,x1,y1)){
        firefly_object->SetVisibility(false);
        for (int i = 0; i < FIREFLY_LIGHTS; i++){
            firefly_lights[i]->brightness = 0.0f;
            firefly_light_level[i] = 0.0f;
        }
        firefly_last_tick = tick;
        return;
    }
    int64_t steps = (firefly_last_tick < 0) ? 1 : tick - firefly_last_tick;
    if ((steps < 0) || (steps > 8)){
        steps = 1;
    }
    for (int64_t k = steps - 1; k >= 0; k--){
        firefly_swarm.Step(wind,tick - k,x0,y0,x1,y1);
    }
    firefly_last_tick = tick;

    /*
        The light group. A light's brightness goes in as the SQUARE ROOT of its flies' glow: a
        point light's brightness is applied twice in default.frag (a long-standing quirk every
        light is tuned around), so this makes what reaches the ground go with the glow itself.
        Smoothed both ways, faster up than down, so a flash swells and fades rather than blinking
        the ground. Only while something steps - a paused game holds the lights with the flies.
    */
    if (steps > 0){
        firefly_swarm.LightGroup(FIREFLY_LIGHTS,x0,y0,x1,y1,firefly_group);
        for (int i = 0; i < FIREFLY_LIGHTS; i++){
            const FireflyLight& g = firefly_group[i];
            float& level = firefly_light_level[i];
            level += (g.intensity - level) * ((g.intensity > level) ? 0.25f : 0.06f);
            PointLight* light = firefly_lights[i];
            if (g.intensity > 0.0f){
                vec3 at = light->GetPosition();
                vec3 want(g.x,g.y,g.z);
                //A light that was dark jumps to its flies; a lit one glides after them.
                light->SetPosition((light->brightness <= 0.001f) ? want : at + (want - at) * 0.2f);
            }
            light->brightness = firefly_light_gain * sqrtf(fmaxf(level,0.0f));
        }
    }

    vec3 eye = main_scene->camera->GetPosition();
    firefly_swarm.BuildGlows(eye.x,eye.y,eye.z,firefly_glow_size,firefly_quads);
    if (firefly_quads.empty()){
        firefly_object->SetVisibility(false);
        return;
    }
    firefly_vertices.resize(firefly_quads.size());
    for (size_t i = 0; i < firefly_quads.size(); i++){
        const FireflyVertex& q = firefly_quads[i];
        vertex& v = firefly_vertices[i];
        v.pos = vec3(q.x,q.y,q.z);
        v.normal = vec3(q.brightness,0.0f,0.0f);     //the glow - see firefly.vert
        v.tangent = vec3(1.0f,0.0f,0.0f);
        v.uv = vec2(q.u,q.v);
        v.matid = 0;
    }
    firefly_mesh->SetMeshData(firefly_vertices.data(),(int)firefly_vertices.size());
    firefly_mesh->mesh_mode = MESH_MODE_SHADER;
    firefly_mesh->custom_shader_index = firefly_shader_index;
    firefly_object->SetVisibility(true);
}

json ApplicationArcher::FireflySummary(){
    int home = 0, bright = 0;
    for (const Firefly& f : firefly_swarm.flies){
        home += (f.home >= 0);
        bright += (f.brightness > 0.5f);
    }
    json lights = json::array();
    for (int i = 0; i < FIREFLY_LIGHTS; i++){
        if (firefly_lights[i]){
            vec3 p = firefly_lights[i]->GetPosition();
            lights.push_back({ {"x",p.x},{"y",p.y},{"z",p.z},{"brightness",firefly_lights[i]->brightness} });
        }
    }
    return json{ {"on",f_fireflies.load()},{"count",firefly_swarm.params.count},{"at_home",home},
                 {"flashing",bright},{"sync",firefly_swarm.params.sync},{"lights",lights} };
}

json ApplicationArcher::LeafSummary(){
    int flying = 0, resting = 0, fading = 0;
    for (const Leaf& l : leaf_swarm.leaves){
        flying += (l.state == LEAF_FLYING);
        resting += (l.state == LEAF_RESTING);
        fading += (l.state == LEAF_FADING);
    }
    return json{ {"on",f_wind_leaves.load()},{"model",leaf_mesh != NULL},{"count",(int)leaf_swarm.leaves.size()},
                 {"density",leaf_swarm.params.density},{"pad",leaf_swarm.params.pad},
                 {"flying",flying},{"resting",resting},{"fading",fading} };
}

void ApplicationArcher::SetMaterialWindFlex(const std::string& suffix, float flex){
    for (Material& m : renderer->materials){
        if ((m.name.size() > suffix.size()) &&
            (m.name.compare(m.name.size() - suffix.size(),suffix.size(),suffix) == 0)){
            m.glsl_material.wind_flex = flex;
        }
    }
}

void ApplicationArcher::ApplyFoliageWindFlex(){
    for (int k = 0; k < FOLIAGE_KIND_COUNT; k++){
        SetMaterialWindFlex(std::string("@") + FOLIAGE_NODES[k],foliage_wind_flex[k]);
    }
}

/*
    Loads the three plants and grows the first garden. RENDER THREAD - GetMeshFromNode uploads.

    Survivable per kind, like the bow: a node missing from the export is reported and that kind
    simply does not grow. The sizes the scatter spaces them by are measured off the vertices
    rather than typed in, so a re-exported fern that got bigger gets more room automatically.
*/
void ApplicationArcher::BuildFoliage(){
    for (int k = 0; k < FOLIAGE_KIND_COUNT; k++){
        Mesh* mesh = gltfloader.GetMeshFromNode(FOLIAGE_NODES[k],&foliage_materials[k],false);
        if (!mesh){
            debug->Err("No mesh on node '%s' in %s - that plant will not grow\n",
                       FOLIAGE_NODES[k],ARCHER_MODEL_ASSET);
            continue;
        }
        //Held here as well as by every plant drawing it, so a rescatter that happens to use none
        //of a kind cannot free the mesh out from under the next one.
        mesh->Retain();
        foliage_meshes[k] = mesh;
        //Each kind's OWN copy of its material, so it can sway: grass_1 is on the terrain tiles'
        //atlas and grass_2 on a props atlas, and a flex on the shared material would bend those
        //too. The copy shares the texture, and so its texture unit.
        for (Material& m : foliage_materials[k]){
            m.name += std::string("@") + FOLIAGE_NODES[k];
            m.glsl_material.wind_flex = foliage_wind_flex[k];
        }
        renderer->AddMaterials(foliage_materials[k]);

        //From the origin, which is where the plant stands: the props are authored base-down.
        float radius = 0.0f;
        float height = 0.0f;
        const std::vector<vertex>& verts = mesh->GetVertices();
        for (size_t i = 0; i < verts.size(); i++){
            float r = sqrtf(verts[i].pos.x * verts[i].pos.x + verts[i].pos.z * verts[i].pos.z);
            if (r > radius){ radius = r; }
            if (verts[i].pos.y > height){ height = verts[i].pos.y; }
        }
        foliage_mesh_radius[k] = radius;
        foliage_mesh_height[k] = height;
        debug->Info("Foliage '%s': radius %.3f, height %.3f at scale 1\n",FOLIAGE_NODES[k],radius,height);
    }
    //The character's scale, which is 1 if the model did not load - and then nothing is to scale
    //with anything anyway.
    foliage_scale = model_scale;

    foliage_group = new Object();
    foliage_group->name = "foliage";
    main_scene->AddObject(foliage_group);
    ScatterFoliageObjects();
}

/*
    Re-places the pool from `stage.blocks` as they are now. No GL.

    Called with physics_mutex held (RegenerateTerrain, PreRender) or before the threads start
    (Init) - it reads the Stage the tick writes and re-points Objects the render thread draws.

    EVERY BLOCK GROWS, THE TERRAIN'S AND THE TILES' INCLUDED. Both used to be masked out; neither
    needs to be. The terrain's top is pinned to the box's across its width and within its depth
    (see "THE SHAPE, THROUGH THE SLAB" in Terrain.h), which is exactly where the scatter puts
    plants - the fillet at the foot of a wall rises a little, and a plant there sits a little into
    the grass, which reads as grass. A tile's collider top is the tile's grass by construction.
*/
void ApplicationArcher::ScatterFoliageObjects(){
    if (!foliage_group || stage.GetLevel() != STAGE_LEVEL_MAIN){
        return;
    }
    std::vector<bool> grows(stage.blocks.size(),true);
    FoliageParams params = foliage_params;
    for (int k = 0; k < FOLIAGE_KIND_COUNT; k++){
        params.radius[k] = foliage_mesh_radius[k] * foliage_scale;
        params.height[k] = foliage_mesh_height[k] * foliage_scale;
    }
    std::vector<FoliagePlant> plants;
    ScatterFoliage(stage.blocks,grows,params,plants);

    for (int k = 0; k < FOLIAGE_KIND_COUNT; k++){
        foliage_counts[k] = 0;
    }
    size_t used = 0;
    for (size_t i = 0; i < plants.size(); i++){
        const FoliagePlant& p = plants[i];
        Mesh* mesh = foliage_meshes[p.kind];
        if (!mesh){
            continue;
        }
        if (used >= foliage_objects.size()){
            Object* o = new Object();
            //Not pickable: a click on a box being edited must not land on the fern in front of it.
            o->SetPickability(false);
            //No shadows, for now - small, many, and not worth a place in the shadow pass.
            o->SetCastsShadow(false);
            foliage_group->AttachChild(o);
            foliage_objects.push_back(o);
        }
        Object* o = foliage_objects[used++];
        char name[32];
        snprintf(name,sizeof(name),"%s.%i",FOLIAGE_NODES[p.kind],foliage_counts[p.kind]);
        o->name = name;
        o->SetMesh(mesh);
        o->TakeMaterialNames(foliage_materials[p.kind]);
        o->SetPosition(vec3(p.x,p.y,p.z));
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),p.yaw));
        float s = foliage_scale * p.scale;
        o->SetScale(vec3(s,s,s));
        o->SetVisibility(true);
        foliage_counts[p.kind]++;
    }
    for (size_t i = used; i < foliage_objects.size(); i++){
        foliage_objects[i]->SetVisibility(false);
    }
    //The fireflies live where the plants are, most where they grew in the shade.
    {
        std::vector<FireflyHome> homes;
        homes.reserve(plants.size());
        for (const FoliagePlant& p : plants){
            FireflyHome h;
            h.x = p.x;
            h.y = p.y;
            h.z = p.z;
            h.weight = 0.2f + 2.0f * p.occlusion;
            homes.push_back(h);
        }
        std::lock_guard<std::mutex> lock(wind_mutex);
        firefly_swarm.SetHomes(homes);
    }
    debug->Info("Foliage: %i ferns, %i low ferns, %i flowers, %i + %i grass\n",foliage_counts[FOLIAGE_FERN],
                foliage_counts[FOLIAGE_FERN_LOW],foliage_counts[FOLIAGE_FLOWER],
                foliage_counts[FOLIAGE_GRASS],foliage_counts[FOLIAGE_GRASS_2]);
}

//--- Rocks ---------------------------------------------------------------------------------------

//The archer.glb node for each BoulderKind, in that enum's order.
static const char* BOULDER_NODES[BOULDER_KIND_COUNT] = { "rock_big_1","rock_big_2", "rock_small_1" };

void ApplicationArcher::BuildBoulders(){
    for (int k = 0; k < BOULDER_KIND_COUNT; k++){
        Mesh* mesh = gltfloader.GetMeshFromNode(BOULDER_NODES[k],&boulder_materials[k],false);
        if (!mesh){
            debug->Err("No mesh on node '%s' in %s - no rocks of that kind\n",BOULDER_NODES[k],ARCHER_MODEL_ASSET);
            continue;
        }
        //Held here as well as by every rock drawing it - the foliage's reason.
        mesh->Retain();
        boulder_meshes[k] = mesh;
        renderer->AddMaterials(boulder_materials[k]);
        //The widest the footprint gets under any yaw, and the height - both from the origin, where
        //it stands. What Boulders.cpp keeps rocks apart and behind her walking line by.
        float radius = 0.0f;
        float height = 0.0f;
        const std::vector<vertex>& verts = mesh->GetVertices();
        for (size_t i = 0; i < verts.size(); i++){
            float r = sqrtf(verts[i].pos.x * verts[i].pos.x + verts[i].pos.z * verts[i].pos.z);
            if (r > radius){ radius = r; }
            if (verts[i].pos.y > height){ height = verts[i].pos.y; }
        }
        boulder_mesh_radius[k] = radius;
        boulder_mesh_height[k] = height;
        debug->Info("Rock '%s': radius %.3f, height %.3f at scale 1\n",BOULDER_NODES[k],radius,height);
    }
    boulder_group = new Object();
    boulder_group->name = "boulders";
    main_scene->AddObject(boulder_group);
    ScatterBoulderObjects();
}

/*
    Re-places the pool from `stage.blocks` as they are now. No GL. The same contexts as
    ScatterFoliageObjects, and called beside it: a moved box takes its rocks with it.
*/
void ApplicationArcher::ScatterBoulderObjects(){
    if (!boulder_group || stage.GetLevel() != STAGE_LEVEL_MAIN){
        return;
    }
    BoulderParams params = boulder_params;
    for (int k = 0; k < BOULDER_KIND_COUNT; k++){
        params.radius[k] = boulder_mesh_radius[k] * model_scale;
        params.height[k] = boulder_mesh_height[k] * model_scale;
    }
    std::vector<Boulder> rocks;
    ScatterBoulders(stage.blocks,params,rocks);

    for (int k = 0; k < BOULDER_KIND_COUNT; k++){
        boulder_counts[k] = 0;
    }
    size_t used = 0;
    for (size_t i = 0; i < rocks.size(); i++){
        const Boulder& b = rocks[i];
        Mesh* mesh = boulder_meshes[b.kind];
        if (!mesh){
            continue;
        }
        if (used >= boulder_objects.size()){
            Object* o = new Object();
            o->SetPickability(false);
            boulder_group->AttachChild(o);
            boulder_objects.push_back(o);
        }
        Object* o = boulder_objects[used++];
        char name[32];
        snprintf(name,sizeof(name),"%s.%i",BOULDER_NODES[b.kind],boulder_counts[b.kind]);
        o->name = name;
        o->SetMesh(mesh);
        o->TakeMaterialNames(boulder_materials[b.kind]);
        //Big ones are big enough to be worth a shadow; the small ones are the foliage's case.
        o->SetCastsShadow(IsBigBoulder(b.kind));
        o->SetPosition(vec3(b.x,b.y,b.z));
        //Tipped about a horizontal axis, after the yaw - a small rock lying the way it landed.
        quat yaw(vec3(0.0f,1.0f,0.0f),b.yaw);
        quat tilt = quat().identity();
        if (b.tilt != 0.0f){
            tilt = quat(vec3(cosf(b.tilt_axis_yaw),0.0f,sinf(b.tilt_axis_yaw)),b.tilt);
        }
        o->SetRotation(tilt * yaw);
        float s = model_scale * b.scale;
        o->SetScale(vec3(s,s,s));
        o->SetVisibility(true);
        boulder_counts[b.kind]++;
    }
    for (size_t i = used; i < boulder_objects.size(); i++){
        boulder_objects[i]->SetVisibility(false);
    }
    debug->Info("Rocks: %i big (%i + %i), %i small\n",
                boulder_counts[BOULDER_BIG_1] + boulder_counts[BOULDER_BIG_2],
                boulder_counts[BOULDER_BIG_1],boulder_counts[BOULDER_BIG_2],boulder_counts[BOULDER_SMALL_1]);
}

//--- Signs --------------------------------------------------------------------------------------

//The archer.glb node for each SignVariant, in that enum's order.
static const char* SIGN_NODES[SIGN_VARIANT_COUNT] = { "signpost" };

/*
    See the declaration. Survivable per variant, like the foliage: a model missing from the export
    is reported once and its signs are not stood up, and the level plays the same without them.
*/
void ApplicationArcher::BuildSigns(){
    if (!f_sign_models_loaded){
        f_sign_models_loaded = true;
        for (int v = 0; v < SIGN_VARIANT_COUNT; v++){
            if (LoadSignModel(gltfloader,SIGN_NODES[v],sign_models[v])){
                renderer->AddMaterials(sign_models[v].materials);
            }else{
                debug->Err("No '%s' in %s - signs of that kind will not stand\n",
                           SIGN_NODES[v],ARCHER_MODEL_ASSET);
            }
        }
        /*
            Paint, not ink: LIT, unlike the hit counter, because the letters are part of the board
            and should darken with it in shade. Dark and rough so they read against the wood from
            across the screen without catching a highlight.
        */
        Material m;
        m.name = "ar_sign_text";
        m.glsl_material.color = vec4(0.10f,0.06f,0.035f,1.0f);
        m.glsl_material.roughness = 0.9f;
        m.glsl_material.metallic = 0.0f;
        renderer->AddMaterial(m);
        material_sign_text = renderer->FindMaterialIndex(m.name);
    }

    for (size_t i = 0; i < stage.signs.size(); i++){
        const StageSign& s = stage.signs[i];
        if (s.variant < 0 || s.variant >= SIGN_VARIANT_COUNT){
            continue;
        }
        char name[32];
        snprintf(name,sizeof(name),"sign_%i",(int)i);
        Object* o = BuildSignObject(sign_models[s.variant],hit_glyphs,s.text,STAGE_SIGN_MAX_TEXTS,
                                    material_sign_text,name);
        if (!o){
            continue;
        }
        //The character's scale, like every prop out of the same file.
        o->SetPosition(vec3(s.x,s.y,s.z));
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),s.yaw_deg * 3.14159265358979f / 180.0f));
        o->SetScale(vec3(model_scale,model_scale,model_scale));
        main_scene->AddObject(o);
    }
}

//--- Scenery ------------------------------------------------------------------------------------

//The archer.glb node for each SceneryVariant, in that enum's order.
static const char* SCENERY_NODES[SCENERY_VARIANT_COUNT] = { "terrain_tile_big", "terrain_tile_round" };

/*
    Where a model's walkable top is: the AREA-WEIGHTED MEDIAN HEIGHT OF ITS UP-FACING TRIANGLES,
    and the x extent of those at that height. Not the highest vertex - grass blades stand above
    the surface, and on terrain_tile_big the highest point is a blade tip a quarter of a unit (at
    her scale) above the grass she should be walking on. Blades are near-vertical, so the
    up-facing test drops them without having to know they are there.

    The mesh is a flat triangle list (GLTFLoader expands the indices), so triangles are triplets.
    False if nothing faces up.
*/
static bool MeasureWalkableTop(const Mesh* mesh, float& out_x, float& out_y, float& out_w){
    struct Up{ float y, area; int first; };
    std::vector<Up> up;
    const std::vector<vertex>& v = mesh->GetVertices();
    float total = 0.0f;
    for (size_t i = 0; i + 2 < v.size(); i += 3){
        vec3 e1 = v[i + 1].pos - v[i].pos;
        vec3 e2 = v[i + 2].pos - v[i].pos;
        vec3 n(e1.y * e2.z - e1.z * e2.y,e1.z * e2.x - e1.x * e2.z,e1.x * e2.y - e1.y * e2.x);
        float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len < 1e-12f || n.y / len < 0.9f){
            continue;
        }
        Up u;
        u.y = (v[i].pos.y + v[i + 1].pos.y + v[i + 2].pos.y) / 3.0f;
        u.area = 0.5f * len;
        u.first = (int)i;
        up.push_back(u);
        total += u.area;
    }
    if (up.empty()){
        return false;
    }
    std::sort(up.begin(),up.end(),[](const Up& a, const Up& b){ return a.y < b.y; });
    float acc = 0.0f;
    float top = up.back().y;
    for (size_t i = 0; i < up.size(); i++){
        acc += up[i].area;
        if (acc >= 0.5f * total){
            top = up[i].y;
            break;
        }
    }
    float lo = 1e30f, hi = -1e30f;
    for (size_t i = 0; i < up.size(); i++){
        if (fabsf(up[i].y - top) > 0.04f){
            continue;
        }
        for (int k = 0; k < 3; k++){
            lo = fminf(lo,v[up[i].first + k].pos.x);
            hi = fmaxf(hi,v[up[i].first + k].pos.x);
        }
    }
    out_x = 0.5f * (lo + hi);
    out_y = top;
    out_w = hi - lo;
    return true;
}

//See the declaration. Survivable per variant, like the signs and the foliage.
void ApplicationArcher::BuildScenery(){
    if (!f_scenery_loaded){
        f_scenery_loaded = true;
        for (int v = 0; v < SCENERY_VARIANT_COUNT; v++){
            Mesh* mesh = gltfloader.GetMeshFromNode(SCENERY_NODES[v],&scenery_materials[v],false);
            if (!mesh){
                debug->Err("No '%s' in %s - that scenery will not stand\n",SCENERY_NODES[v],ARCHER_MODEL_ASSET);
                continue;
            }
            mesh->Retain();
            scenery_meshes[v] = mesh;
            renderer->AddMaterials(scenery_materials[v]);
            if (!MeasureWalkableTop(mesh,scenery_top_x[v],scenery_top_y[v],scenery_top_w[v])){
                //Nothing faces up: place it by its origin, which is all there is to go on.
                debug->Warn("Scenery '%s' has no up-facing surface - placed by its origin\n",SCENERY_NODES[v]);
            }
            debug->Info("Scenery '%s': walkable top at y %.3f, x %.3f, %.3f wide (%.2f at her scale)\n",
                        SCENERY_NODES[v],scenery_top_y[v],scenery_top_x[v],scenery_top_w[v],
                        scenery_top_w[v] * model_scale);
        }
    }

    for (size_t i = 0; i < stage.scenery.size(); i++){
        const StageScenery& s = stage.scenery[i];
        if (s.variant < 0 || s.variant >= SCENERY_VARIANT_COUNT || !scenery_meshes[s.variant]){
            continue;
        }
        char name[48];
        snprintf(name,sizeof(name),"%s_%i",SCENERY_NODES[s.variant],(int)i);
        Object* o = new Object();
        o->name = name;
        o->SetMesh(scenery_meshes[s.variant]);
        //TakeMaterialNames writes the list, so each Object hands over its own copy.
        std::vector<Material> materials = scenery_materials[s.variant];
        o->TakeMaterialNames(materials);
        //The measured top's centre onto (x, y). Unrotated, which is what a collider requires anyway.
        o->SetPosition(vec3(s.x - scenery_top_x[s.variant] * model_scale,
                            s.y - scenery_top_y[s.variant] * model_scale,s.z));
        o->SetRotation(quat(vec3(0.0f,1.0f,0.0f),s.yaw_deg * 3.14159265358979f / 180.0f));
        o->SetScale(vec3(model_scale,model_scale,model_scale));
        main_scene->AddObject(o);

        /*
            The level typed the collider; the mesh says how wide the grass really is. A quarter of a
            unit either way is the tolerance - her half-width is 0.35, so a collider that far out
            has her standing on the air past the lip or dropping off grass she can see.
        */
        if (s.collider_hw > 0.0f){
            float measured = scenery_top_w[s.variant] * model_scale;
            if (fabsf(2.0f * s.collider_hw - measured) > 0.25f){
                debug->Warn("%s: collider is %.2f wide but the model's walkable top is %.2f - "
                            "update its collider_hw in Stage.cpp\n",name,2.0f * s.collider_hw,measured);
            }
        }
    }
}

//--- Vines --------------------------------------------------------------------------------------

//The archer.glb nodes for the vine's pieces. Any that is missing falls back to Vine.cpp's stand-in.
static const char* VINE_TRUNK_NODE = "vine_trunk";
//The wrap has no stand-in: it only means something on the trunk tile it was modelled round.
static const char* VINE_WRAP_NODE  = "vine_curl";
static const char* VINE_LEAF_NODES[VINE_LEAF_KIND_COUNT] = { "vine_leaf_1", "vine_leaf_2" };

void ApplicationArcher::BuildVines(){
    std::vector<VinePath> paths;
    DeclareVines(STAGE_LEVEL_MAIN,paths);
    if (paths.empty()){
        return;
    }

    //--- The trunk tile ---
    std::vector<vertex> tile;
    std::vector<Material> trunk_materials;
    Mesh* trunk_source = gltfloader.GetMeshFromNode(VINE_TRUNK_NODE,&trunk_materials,false);
    f_vine_trunk_from_asset = (trunk_source != NULL);
    if (trunk_source){
        tile = trunk_source->GetVertices();
        renderer->AddMaterials(trunk_materials);
        vine_params.tile_scale = model_scale;
        /*
            The one authoring mistake that would otherwise produce a baffling result: a tile modelled
            along the wrong axis deforms into a flat ribbon wrapped round the curve. Said out loud,
            with the fix, rather than left to be worked out from a screenshot.
        */
        vec3 lo = tile.empty() ? vec3() : tile[0].pos, hi = lo;
        for (size_t i = 1; i < tile.size(); i++){
            const vec3& p = tile[i].pos;
            lo = vec3(fminf(lo.x,p.x),fminf(lo.y,p.y),fminf(lo.z,p.z));
            hi = vec3(fmaxf(hi.x,p.x),fmaxf(hi.y,p.y),fmaxf(hi.z,p.z));
        }
        vec3 size = hi - lo;
        debug->Info("Vine tile '%s': %zu tris, %.3f x %.3f x %.3f (x,y,z) at scale 1\n",
                    VINE_TRUNK_NODE,tile.size() / 3,size.x,size.y,size.z);
        if (size.z < size.x || size.z < size.y){
            debug->Warn("The vine tile is not longest along +Z (glTF), which is the axis it is laid "
                        "along. Model it along Blender's -Y.\n");
        }
    }else{
        MakeVinePlaceholderTile(tile);
        vine_params.tile_scale = 1.0f;
    }
    vine_params.tile_radius = VineTileRadius(tile);

    //--- The wrap ---
    /*
        The thin strands round the trunk, modelled round the trunk TILE and laid in step with it.
        So it only makes sense on the tile it was modelled round: laid over the placeholder it
        would be at the wrong scale and the wrong period, and is left off.
    */
    std::vector<vertex> wrap_tile;
    std::vector<Material> wrap_materials;
    Mesh* wrap_source = gltfloader.GetMeshFromNode(VINE_WRAP_NODE,&wrap_materials,false);
    f_vine_wrap_from_asset = false;
    if (wrap_source && trunk_source){
        wrap_tile = wrap_source->GetVertices();
        renderer->AddMaterials(wrap_materials);
        f_vine_wrap_from_asset = true;
        float wz0 = 0.0f, wlen = 0.0f, tz0 = 0.0f, tlen = 0.0f;
        SplineDeformMeasure(wrap_tile,wz0,wlen);
        SplineDeformMeasure(tile,tz0,tlen);
        debug->Info("Vine wrap '%s': %zu tris, z %.3f..%.3f, laid at the trunk's period %.3f\n",
                    VINE_WRAP_NODE,wrap_tile.size() / 3,wz0,wz0 + wlen,tlen);
    }else if (wrap_source){
        debug->Warn("'%s' is in the file but '%s' is not - a wrap is laid in step with the trunk "
                    "tile it was modelled round, so it is left off the placeholder trunk.\n",
                    VINE_WRAP_NODE,VINE_TRUNK_NODE);
    }

    //--- The leaves ---
    //One mesh per kind, shared by every leaf of it, and each kind's own scale to world: the file's
    //pieces are authored to her scale, the placeholder is in world units.
    Mesh* leaf_meshes[VINE_LEAF_KIND_COUNT] = {};
    std::vector<Material> leaf_materials[VINE_LEAF_KIND_COUNT];
    float leaf_to_world[VINE_LEAF_KIND_COUNT] = {};
    Mesh* placeholder_leaf = NULL;
    float leaf_length = 0.0f;
    for (int k = 0; k < VINE_LEAF_KIND_COUNT; k++){
        Mesh* mesh = gltfloader.GetMeshFromNode(VINE_LEAF_NODES[k],&leaf_materials[k],false);
        f_vine_leaf_from_asset[k] = (mesh != NULL);
        if (mesh){
            mesh->Retain();
            //Their own copies, in LEAF mode - bent by distance from the stem (the leaf's origin),
            //since a vine leaf hangs every which way. See material_t::wind_mode.
            for (Material& m : leaf_materials[k]){
                m.name += std::string("@") + VINE_LEAF_NODES[k];
                m.glsl_material.wind_flex = vine_leaf_wind_flex;
                m.glsl_material.wind_mode = 1;
            }
            renderer->AddMaterials(leaf_materials[k]);
            leaf_to_world[k] = model_scale;
        }else{
            if (!placeholder_leaf){
                std::vector<vertex> verts;
                MakeVinePlaceholderLeaf(verts);
                placeholder_leaf = new Mesh();
                placeholder_leaf->SetMeshData(verts.data(),(int)verts.size());
                placeholder_leaf->num_materials = 1;
                assetmanager->AddNewAsset("ar_vine_leaf_placeholder",placeholder_leaf);
            }
            mesh = placeholder_leaf;
            leaf_to_world[k] = 1.0f;
        }
        leaf_meshes[k] = mesh;
        //Stem to tip, for keeping tips out of the blocks. The longer kind decides, so neither pokes
        //through.
        const std::vector<vertex>& verts = mesh->GetVertices();
        for (size_t i = 0; i < verts.size(); i++){
            float z = verts[i].pos.z * leaf_to_world[k];
            if (z > leaf_length){ leaf_length = z; }
        }
    }
    vine_params.leaf_length = leaf_length;

    vine_group = new Object();
    vine_group->name = "vines";
    main_scene->AddObject(vine_group);

    for (size_t v = 0; v < paths.size(); v++){
        Spline spline;
        if (!BuildVineSpline(paths[v],spline)){
            continue;
        }
        std::vector<vertex> verts;
        int tiles = BuildVineTrunk(spline,paths[v],tile,vine_params,verts);
        if (tiles <= 0 || verts.empty()){
            continue;
        }
        Object* trunk = new Object();
        char name[32];
        snprintf(name,sizeof(name),"vine_%zu",v);
        trunk->name = name;
        //IDENTITY, because the vertices are already in world coordinates - like the terrain, and
        //for the same reason: the normals were computed from the final positions.
        trunk->SetPosition(vec3(0.0f,0.0f,0.0f));
        trunk->SetScale(vec3(1.0f,1.0f,1.0f));
        trunk->SetPickability(false);
        Mesh* mesh = new Mesh();
        mesh->SetMeshData(verts.data(),(int)verts.size());
        mesh->num_materials = trunk_source ? trunk_source->num_materials : 1;
        trunk->SetMesh(mesh);       //takes the reference
        if (trunk_source){
            trunk->TakeMaterialNames(trunk_materials);
        }else{
            trunk->SetMaterialSlot(0,material_vine);
        }
        vine_group->AttachChild(trunk);
        vine_trunks.push_back(trunk);

        //The wrap: its own Object, because it carries its own materials; world coordinates too.
        size_t wrap_tris = 0;
        if (f_vine_wrap_from_asset){
            std::vector<vertex> wrap_verts;
            int copies = BuildVineOverlay(spline,paths[v],wrap_tile,tile,vine_params,wrap_verts);
            if (copies > 0 && !wrap_verts.empty()){
                Object* wrap = new Object();
                snprintf(name,sizeof(name),"vine_%zu.wrap",v);
                wrap->name = name;
                wrap->SetPosition(vec3(0.0f,0.0f,0.0f));
                wrap->SetScale(vec3(1.0f,1.0f,1.0f));
                wrap->SetPickability(false);
                Mesh* wmesh = new Mesh();
                wmesh->SetMeshData(wrap_verts.data(),(int)wrap_verts.size());
                wmesh->num_materials = wrap_source->num_materials;
                wrap->SetMesh(wmesh);
                wrap->TakeMaterialNames(wrap_materials);
                vine_group->AttachChild(wrap);
                vine_wraps.push_back(wrap);
                wrap_tris = wrap_verts.size() / 3;
            }
        }

        std::vector<VineLeaf> leaves;
        ScatterVineLeaves(spline,paths[v],vine_params,&stage.blocks,leaves);
        for (size_t i = 0; i < leaves.size(); i++){
            const VineLeaf& l = leaves[i];
            Object* o = new Object();
            snprintf(name,sizeof(name),"vine_%zu.leaf%zu",v,i);
            o->name = name;
            //Not pickable, for the ferns' reason; no shadow, because a leaf's shadow on the trunk
            //it grows from is a speckle, not a shape.
            o->SetPickability(false);
            o->SetCastsShadow(false);
            o->SetMesh(leaf_meshes[l.kind]);
            if (f_vine_leaf_from_asset[l.kind]){
                o->TakeMaterialNames(leaf_materials[l.kind]);
            }else{
                o->SetMaterialSlot(0,material_vine_leaf);
            }
            o->SetPosition(l.position);
            o->SetRotation(l.rotation);
            float s = l.scale * leaf_to_world[l.kind];
            //The placeholder leaf is anchored at its stem too, so it can flutter the same way.
            if (!f_vine_leaf_from_asset[l.kind] && (material_vine_leaf < (int)renderer->materials.size())){
                renderer->materials[material_vine_leaf].glsl_material.wind_flex = vine_leaf_wind_flex;
                renderer->materials[material_vine_leaf].glsl_material.wind_mode = 1;
            }
            o->SetScale(vec3(s,s,s));
            vine_group->AttachChild(o);
            vine_leaves.push_back(o);
            vine_leaf_counts[l.kind]++;
        }
        debug->Info("Vine %zu: %.2f long, %i tiles, %zu tris + %zu wrap, %zu leaves\n",v,
                    spline.GetLength(),tiles,verts.size() / 3,wrap_tris,leaves.size());
    }
    debug->Info("Vines: %zu built, trunk from %s, leaves from %s / %s\n",vine_trunks.size(),
                f_vine_trunk_from_asset ? "archer.glb" : "the placeholder",
                f_vine_leaf_from_asset[VINE_LEAF_1] ? "archer.glb" : "the placeholder",
                f_vine_leaf_from_asset[VINE_LEAF_2] ? "archer.glb" : "the placeholder");
}

/*
    Work out which blockout boxes the terrain has replaced, and hide them - hide, never skip or
    delete, and never anything but SOLID.

    block_objects is indexed in step with Stage::blocks and BreakBlocks indexes straight into it,
    so removing entries would be a quiet corruption of the kick slice. Hiding costs one bool, keeps
    every collider exactly where it was, and makes the debug view below a one-liner.

    SetVisibility, NOT Hide(). Object::Hide also deactivates the body, and this used to call it -
    which quietly switched off the collider of every melted box, so a prop landing on the terrain
    fell straight through it. Visibility is the only thing that should change here.

    RECOMPUTED FROM Stage::blocks RATHER THAN REMEMBERED, because NewGame throws every block object
    away and builds a fresh set that all start visible - so this has to run again after each
    restart, against a block_objects that is not the one BuildTerrain saw. And every SOLID block is
    visited, not just the melted ones: after an edit a box may have been dragged OUT of a bay, and
    has to be shown again.

    NO GL IN HERE, which is what makes it safe to call from NewGame on the physics thread. Compare
    BuildTerrain, which is render-thread only for exactly that reason.
*/
void ApplicationArcher::ApplyBlockoutVisibility(){
    melted_blocks.clear();
    //A scenery collider is hidden the way a melted box is, bay or no bay: its model is its look.
    for (size_t i = 0;i < stage.blocks.size() && i < block_objects.size();i++){
        if (stage.blocks[i].f_invisible && block_objects[i]){
            block_objects[i]->SetVisibility(f_show_blockout);
        }
    }
#if ARCHER_TEST_BAY
    if (terrain_objects.empty()){
        return;     //nothing has been melted, so nothing is hidden
    }
    for (size_t i = 0;i < stage.blocks.size() && i < block_objects.size();i++){
        const StageBlock& b = stage.blocks[i];
        //Only SOLID melts - see the header note in Terrain.h. Leaving the other kinds alone also
        //keeps this from un-hiding a broken wall, which BreakBlocks hid for good.
        if (b.kind != BLOCK_SOLID || b.f_invisible || !block_objects[i]){
            continue;
        }
        bool f_melted = IsInTerrainBay(b);
        if (f_melted){
            melted_blocks.push_back((int)i);
        }
        block_objects[i]->SetVisibility(!f_melted || f_show_blockout);
    }
#endif
}

/*
    Show or hide the blockout underneath the terrain.

    The debug view that the hide-don't-delete decision above buys: with the boxes back on, any
    place the terrain has drifted from the collision is visible as a box poking through grass, or
    as grass with no box in it. It is the only way to check the thing the whole design rests on by
    looking rather than by measuring - and the measurement (TerrainStats::worst_dip) only samples
    the top faces, so this is the half that can catch a side or an underside.
*/
void ApplicationArcher::SetBlockoutVisible(bool f_visible){
    f_show_blockout = f_visible;
    ApplyBlockoutVisibility();
    debug->Info("Blockout %s\n",f_visible ? "shown" : "hidden");
}

//The archer.glb node the crates are drawn with.
static const char* CRATE_NODE = "wooden_crate";

/*
    The crate's mesh: archer.glb's wooden_crate, re-baked into the shape unit_mesh has - a 1x1x1
    box centred on its origin - so that a crate is still built by MakePlanarBody exactly as before,
    with its collider and its picture both coming from the one size in Stage.cpp. Nothing about a
    crate's physics, its obstacle box or its kick changes; only what is drawn.

    FITTED TO THE COLLIDER, NOT DRAWN AT HER SCALE, and this is the one prop that differs from the
    rest of the file on that. Everything else is drawn at model_scale because its size is only a
    look. A crate's size is a gameplay number - it is what she pushes, stacks and stands on, and the
    stack beside the step is exactly tall enough to be the way up - so the picture follows the box.
    The model is 0.50 across: at her scale it would be 1.01 against the 0.80 box the rules push.

    One UNIFORM factor, from the largest extent, so the normals stay right and a model that is not
    quite a cube keeps its proportions inside the box rather than being stretched to fill it.

    Survivable the way the vine is: no node in the export, and the crates stay the plain brown
    boxes they have always been, with material_crate.
*/
void ApplicationArcher::BuildCrateMesh(){
    crate_mesh = unit_mesh;
    f_crate_from_asset = false;
    Mesh* mesh = gltfloader.GetMeshFromNode(CRATE_NODE,&crate_materials,false);
    if (!mesh){
        debug->Warn("No '%s' in %s - the crates stay boxes\n",CRATE_NODE,ARCHER_MODEL_ASSET);
        return;
    }
    std::vector<vertex> verts = mesh->GetVertices();
    if (verts.empty()){
        debug->Warn("'%s' has no CPU copy of its vertices - the crates stay boxes\n",CRATE_NODE);
        return;
    }
    vec3 lo = verts[0].pos, hi = lo;
    for (size_t i = 1; i < verts.size(); i++){
        const vec3& p = verts[i].pos;
        lo = vec3(fminf(lo.x,p.x),fminf(lo.y,p.y),fminf(lo.z,p.z));
        hi = vec3(fmaxf(hi.x,p.x),fmaxf(hi.y,p.y),fmaxf(hi.z,p.z));
    }
    vec3 size = hi - lo;
    float extent = fmaxf(size.x,fmaxf(size.y,size.z));
    if (extent < 0.0001f){
        debug->Warn("'%s' has no size - the crates stay boxes\n",CRATE_NODE);
        return;
    }
    //Centred on the bounds rather than trusted to be at the origin, since unit_mesh is.
    vec3 centre = (lo + hi) * 0.5f;
    float k = 1.0f / extent;
    for (size_t i = 0; i < verts.size(); i++){
        verts[i].pos = (verts[i].pos - centre) * k;
    }
    mesh->SetMeshData(verts.data(),(int)verts.size());
    renderer->AddMaterials(crate_materials);
    //Registered for the reason unit_mesh is: NewGame throws every crate away and builds new ones,
    //and the last Object letting go must not free a mesh the next one is about to be handed.
    assetmanager->AddNewAsset("ar_crate_mesh",mesh);
    crate_mesh = mesh;
    f_crate_from_asset = true;
    debug->Info("Crate '%s': %.3f x %.3f x %.3f in the file, fitted to the box\n",
                CRATE_NODE,size.x,size.y,size.z);
}

/*
    THE ARCHERY STAND'S SHAPE, in archer.glb's own units - multiplied by model_scale when built.

    MEASURED off the mesh, not guessed: the node splits into four separate pieces (the board, a long
    back leg raking backwards, two short front legs splayed outward), and these are those pieces'
    bounds and axes. The board is a disc facing +Z - the camera - leaning back LEAN degrees, its
    thickness taken at the rim. A leg is a box from `top` to `bottom`, LEG_HALF_W thick.

    Per variant, when there are more: a second stand is a second one of these and a row in
    TargetVariant, not new code.
*/
struct StandShape{
    const char* node;
    vec3  board_centre;
    float board_radius;
    float board_half_thick;
    float board_lean_deg;
    vec3  leg_top[3];
    vec3  leg_bottom[3];
    float leg_half_w;
};
static const StandShape STAND_ARCHERY = {
    "archery_target",
    vec3(0.003f,0.402f,0.013f), 0.281f, 0.040f, 4.9f,
    { vec3( 0.010f,0.345f,-0.012f), vec3(-0.090f,0.190f,0.028f), vec3(0.096f,0.190f,0.028f) },
    { vec3( 0.010f,0.012f,-0.200f), vec3(-0.195f,0.008f,0.028f), vec3(0.202f,0.008f,0.028f) },
    0.025f
};

void ApplicationArcher::BuildStandMesh(){
    stand_mesh = gltfloader.GetMeshFromNode(STAND_ARCHERY.node,&stand_materials,false);
    if (!stand_mesh){
        debug->Warn("No '%s' in %s - archery stands are built as plain boards\n",
                    STAND_ARCHERY.node,ARCHER_MODEL_ASSET);
        return;
    }
    renderer->AddMaterials(stand_materials);
    //Held by the asset list for the crate's reason: NewGame rebuilds every stand.
    assetmanager->AddNewAsset("ar_archery_stand",stand_mesh);
}

/*
    One archery stand. Physics thread from NewGame, render thread from Init - no GL either way.

    THE MODEL STANDS ON ITS ORIGIN, unlike the unit-cube props, which are centred on theirs. So the
    object goes at the FEET, `p.y - p.h / 2`, and everything that wants the prop's middle - the box
    the archer bumps into - is lifted by obstacle_lift along the stand's own up.

    TURNED STAND_YAW_DEG FROM THE CAMERA TOWARD WHERE SHE COMES IN - the level's start - so its face
    is to her, with enough left toward the camera to read. A yaw about Y, which the planar locks
    leave alone: they stop spin about X and Y, not a rotation it was built with, and tipping stays a
    turn about Z.
*/
PropView ApplicationArcher::MakeTargetStand(const StageProp& p, int index){
    const StandShape& shape = STAND_ARCHERY;
    const float s = model_scale;
    char name[48];
    snprintf(name,sizeof(name),"stand_%i",index);

    float dir = (stage.StartPosition().x < p.x) ? -1.0f : 1.0f;
    quat yaw(vec3(0.0f,1.0f,0.0f),dir * toradians(STAND_YAW_DEG));

    Object* o = new Object();
    o->SetMesh(stand_mesh);
    o->name = name;
    //A hair above the floor, so the feet start clear of it rather than inside it.
    o->SetPosition(vec3(p.x,p.y - p.h * 0.5f + 0.02f,0.0f));
    o->SetRotation(yaw);
    o->SetScale(vec3(s,s,s));      //before AddPhysics, so nothing is rescaled - see Object::SetScale
    o->TakeMaterialNames(stand_materials);
    main_scene->AddObject(o);

    PropView view;
    view.object = o;
    view.kind = PROP_TARGET;
    view.variant = TARGET_STAND;
    view.index = index;
    view.heft = STAND_HEFT;
    view.half_extents = vec3(p.w * 0.5f,p.h * 0.5f,PROP_DEPTH * 0.5f);
    view.obstacle_lift = p.h * 0.5f;
    float lean = toradians(shape.board_lean_deg);
    view.board_centre = shape.board_centre * s;
    view.board_up = vec3(0.0f,cosf(lean),-sinf(lean));
    view.board_radius = shape.board_radius * s;

    Physics* body = o->AddPhysics(main_scene->physics_world);
    if (!body){
        return view;
    }
    body->SetCollisionCategoryBits(ARCHER_CAT_PROP);
    body->SetCollideWithMaskBits(ARCHER_MASK_PROP);
    /*
        The board: the cylinder's axis (its local +Y) turned onto the face's normal, (0, sin, cos) -
        a turn about X of 90 degrees less the lean. Surface on each collider as it is added, because
        SetFrictionCoefficient and SetBounciness act on the LAST one - see MakePlanarBody.
    */
    quat board_q(vec3(1.0f,0.0f,0.0f),toradians(90.0f - shape.board_lean_deg));
    body->AddCylinderCollider(shape.board_radius * s,shape.board_half_thick * s,
                              shape.board_centre * s,board_q,1.0f);
    body->SetBounciness(0.05f);
    body->SetFrictionCoefficient(STAND_FRICTION);
    //The legs: a box each, its local +Y turned onto the leg, and denser than the board so the
    //weight sits low. See the note on STAND_MASS.
    for (int k = 0; k < 3; k++){
        vec3 top = shape.leg_top[k] * s;
        vec3 bottom = shape.leg_bottom[k] * s;
        vec3 along = top - bottom;
        float length = along.length();
        if (length < 0.0001f){
            continue;
        }
        along = along * (1.0f / length);
        vec3 axis = vec3(0.0f,1.0f,0.0f).cross(along);
        float turn = acosf(clamp(along.y,-1.0f,1.0f));
        quat leg_q = quat().identity();
        if (axis.length() > 0.0001f){
            axis.normalize();
            leg_q = quat(axis,turn);
        }
        float hw = shape.leg_half_w * s;
        body->AddBoxCollider(vec3(hw,length * 0.5f,hw),(top + bottom) * 0.5f,leg_q,STAND_LEG_DENSITY);
        body->SetBounciness(0.05f);
        body->SetFrictionCoefficient(STAND_FRICTION);
    }
    //Dynamic, with gravity - AddPhysics leaves it off; see the long note in MakePlanarBody - and
    //the mass set AFTER the colliders, which keeps their centre of mass and scales their inertia.
    body->SetStatic(false);
    body->SetGravityEnabled(true);
    body->SetMass(STAND_MASS);
    body->SetLinearLockAxis(vec3(1.0f,1.0f,0.0f));
    body->SetAngularLockAxis(vec3(0.0f,0.0f,1.0f));

    /*
        CHECKED, not trusted: an arrow's flight across the board's centre must strike THIS body, and
        within the board's width of its centre. That is the exact question the arrows ask, and the
        answer it caught the first time was rp3d raycasting a scaled convex shape at its UNSCALED
        size - arrows sticking in the air in front of the board, or passing clean through it (see
        CylinderMesh in core/physics/Physics.cpp). Silent when it passes.
    */
    rp3d::RigidBody* rb = body->body ? body->body->rigidbody : NULL;
    if (rb){
        vec3 centre = o->GetPosition() + yaw * view.board_centre;
        PhysicsWorld::RaycastHit hit = main_scene->physics_world->Raycast(
            vec3(centre.x - 2.0f,centre.y,0.0f),vec3(centre.x + 2.0f,centre.y,0.0f),NULL);
        if (!hit.hit || hit.body != rb || fabsf(hit.point.x - centre.x) > view.board_radius + 0.01f){
            debug->Err("%s: a level shot at its board's centre %s at x %.2f - the board is %.2f either "
                       "side of %.2f. Arrows will not stick where the board is drawn.\n",name,
                       !hit.hit ? "misses everything" : (hit.body != rb ? "hits something else" : "hits it"),
                       hit.point.x,view.board_radius,centre.x);
        }
    }
    return view;
}

/*
    The straw man's body as three boxes, in the mesh's own units (origin at the foot of the stake,
    x across, y up) - measured off archer.glb's straw_man vertex by vertex, a slice per 1/24th of
    its height, the way StandShape was. Scaled by model_scale when built.

    THE STAKE STARTS 0.075 UP, not at 0: the foot is the hinge, on the floor, and a collider
    reaching down to it would be resting on the floor it swings from - a contact the solver would
    fight the joint over every tick.
*/
struct StrawBox{
    vec3 centre;
    vec3 half;
};
static const char* STRAW_NODE = "straw_man";
static const StrawBox STRAW_BOXES[] = {
    { vec3(0.000f,0.2175f,0.0f), vec3(0.025f,0.1425f,0.025f) },     //stake, 0.075 .. 0.36
    { vec3(0.000f,0.6600f,0.0f), vec3(0.085f,0.3000f,0.084f) },     //skirt, body and head, 0.36 .. 0.96
    { vec3(0.0125f,0.620f,0.0f), vec3(0.2285f,0.1000f,0.070f) },    //the arms, -0.216 .. 0.241
};

void ApplicationArcher::BuildStrawManMesh(){
    straw_mesh = gltfloader.GetMeshFromNode(STRAW_NODE,&straw_materials,false);
    if (!straw_mesh){
        debug->Warn("No '%s' in %s - straw men are not built\n",STRAW_NODE,ARCHER_MODEL_ASSET);
        return;
    }
    renderer->AddMaterials(straw_materials);
    //Held by the asset list for the crate's reason: NewGame rebuilds every straw man.
    assetmanager->AddNewAsset("ar_straw_man",straw_mesh);
}

/*
    One straw man. Physics thread from NewGame, render thread from Init - no GL either way.

    Stood on its origin at the feet, like the stand, facing the camera: its arms spread across the
    screen, and the hinge turns it in the screen plane, the one plane a side view shows.

    THE BODY IS FINISHED BEFORE THE SPRING IS MADE: dynamic, gravity on, mass set. SpringHinge reads
    the mass, the inertia and whether gravity acts off the body as it stands, and a spring measured
    against a static, weightless body would come out with k and c of zero.
*/
PropView ApplicationArcher::MakeStrawMan(const StageProp& p, int index){
    PropView view;
    view.kind = PROP_STRAWMAN;
    view.index = index;
    if (!straw_mesh){
        return view;
    }
    const float s = model_scale;
    char name[48];
    snprintf(name,sizeof(name),"strawman_%i",index);

    Object* o = new Object();
    o->SetMesh(straw_mesh);
    o->name = name;
    o->SetPosition(vec3(p.x,p.y - p.h * 0.5f,0.0f));
    o->SetScale(vec3(s,s,s));      //before AddPhysics, so nothing is rescaled - see Object::SetScale
    o->TakeMaterialNames(straw_materials);
    main_scene->AddObject(o);

    view.object = o;
    view.half_extents = vec3(p.w * 0.5f,p.h * 0.5f,PROP_DEPTH * 0.5f);
    view.obstacle_lift = p.h * 0.5f;

    Physics* body = o->AddPhysics(main_scene->physics_world);
    if (!body){
        return view;
    }
    //A prop to crates, debris and the level - a kicked crate knocks it - but NOT to the archer's
    //kinematic body, which is in every other prop's mask so that walking shoves a crate. Leaving it
    //out is the physical half of being passable; the rules' non-blocking obstacle is the other.
    body->SetCollisionCategoryBits(ARCHER_CAT_PROP);
    body->SetCollideWithMaskBits(ARCHER_MASK_PROP & ~ARCHER_CAT_ARCHER);
    for (size_t k = 0; k < sizeof(STRAW_BOXES) / sizeof(STRAW_BOXES[0]); k++){
        body->AddBoxCollider(STRAW_BOXES[k].half * s,STRAW_BOXES[k].centre * s,quat().identity(),1.0f);
        body->SetBounciness(0.05f);
        body->SetFrictionCoefficient(0.4f);
    }
    body->SetStatic(false);
    body->SetGravityEnabled(true);
    body->SetMass(STRAW_MASS);

    rp3d::RigidBody* rb = body->body ? body->body->rigidbody : NULL;
    if (!rb){
        return view;
    }
    SpringHinge::Settings settings;
    settings.pivot = o->GetPosition();
    settings.axis = vec3(0.0f,0.0f,1.0f);
    settings.hz = straw_hz;
    settings.damping_ratio = straw_damping_ratio;
    settings.f_limits = true;
    settings.min_angle = -toradians(STRAW_LIMIT_DEG);
    settings.max_angle = toradians(STRAW_LIMIT_DEG);
    view.spring = new SpringHinge(main_scene->physics_world,NULL,rb,settings);
    return view;
}

void ApplicationArcher::TickSprings(){
    for (size_t i = 0; i < prop_views.size(); i++){
        SpringHinge* spring = prop_views[i].spring;
        if (!spring || prop_views[i].f_lost){
            continue;
        }
        //The panel's sliders, picked up here rather than by a command: this is the physics thread,
        //between steps, the one place a spring may be retuned - and it covers every level's.
        if (spring->hz != straw_hz || spring->damping_ratio != straw_damping_ratio){
            spring->Retune(straw_hz,straw_damping_ratio);
        }
        spring->Tick();
    }
}

void ApplicationArcher::DestroySprings(){
    for (size_t i = 0; i < prop_views.size(); i++){
        delete prop_views[i].spring;
        prop_views[i].spring = NULL;
    }
}

void ApplicationArcher::BuildProps(){
    prop_views.clear();
    for (size_t i = 0; i < stage.props.size(); i++){
        const StageProp& p = stage.props[i];
        char name[48];

        switch (p.kind){
            case PROP_CRATE: {
                snprintf(name,sizeof(name),"crate_%i",(int)i);
                //Light enough that a walking archer visibly shifts it, heavy enough that it does
                //not fly off like a beach ball. This is the first thing anyone will touch, so it
                //is the first number worth tuning.
                Object* o = MakePlanarBody(crate_mesh,name,vec3(p.x,p.y,0.0f),
                                           vec3(p.w,p.h,PROP_DEPTH),material_crate,
                                           ARCHER_CAT_PROP,ARCHER_MASK_PROP,6.0f,false);
                //The file's own material over material_crate, when the crate came from the file.
                if (o && f_crate_from_asset){
                    o->TakeMaterialNames(crate_materials);
                }
                PropView view;
                view.object = o;
                view.kind = p.kind;
                view.index = (int)i;
                view.half_extents = vec3(p.w * 0.5f,p.h * 0.5f,PROP_DEPTH * 0.5f);
                prop_views.push_back(view);
            } break;

            case PROP_TARGET: {
                //The archery stand, when the model loaded. Without it the stand's box is built as
                //a plain board below - a board of the stand's size, which at least stands up.
                if (p.variant == TARGET_STAND && stand_mesh){
                    prop_views.push_back(MakeTargetStand(p,(int)i));
                    break;
                }
                snprintf(name,sizeof(name),"target_%i",(int)i);
                //A thin board standing on end - so it topples rather than slides, which is what
                //makes a hit readable from across the level with no HUD.
                Object* o = MakePlanarBody(unit_mesh,name,vec3(p.x,p.y,0.0f),
                                           vec3(p.w,p.h,PROP_DEPTH),material_target,
                                           ARCHER_CAT_PROP,ARCHER_MASK_PROP,TARGET_BOARD_MASS,false);
                /*
                    A floating target is the same dynamic board with gravity turned back off -
                    MakePlanarBody turned it on, see the long note there. Deliberately NOTHING
                    else: no damping, no sleep override. What an arrow does to a body that nothing
                    holds up and nothing slows down is the question the range's arch is asking,
                    and a damped answer would be a different question.
                */
                PropView view;
                if (p.f_floating && o && o->GetPhysics()){
                    Physics* body = o->GetPhysics();
                    body->SetGravityEnabled(false);
                    /*
                        A damping of its own, between FLOAT_DAMPING_MIN and _MAX - see the note
                        there. xorshift32 seeded from the prop's index, inline so this stream
                        belongs to the build and to nothing else (the same reasoning as
                        SpawnDebris): the same board gets the same damping every run, and no
                        other draw in the app can shift it.
                    */
                    uint32_t seed = 0x9E3779B9u ^ ((uint32_t)i * 2654435761u);
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    float f_lin = (float)(seed % 1000) / 999.0f;
                    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                    float f_ang = (float)(seed % 1000) / 999.0f;
                    view.base_linear_damping = body->GetLinearDamping();
                    view.base_angular_damping = body->GetAngularDamping();
                    body->SetLinearDamping(FLOAT_DAMPING_MIN + f_lin * (FLOAT_DAMPING_MAX - FLOAT_DAMPING_MIN));
                    body->SetAngularDamping(FLOAT_DAMPING_MIN + f_ang * (FLOAT_DAMPING_MAX - FLOAT_DAMPING_MIN));
                }
                view.object = o;
                view.kind = p.kind;
                view.f_floating = p.f_floating;
                view.index = (int)i;
                view.half_extents = vec3(p.w * 0.5f,p.h * 0.5f,PROP_DEPTH * 0.5f);
                prop_views.push_back(view);
            } break;

            case PROP_BRICKWALL: {
                /*
                    STATIC BRICKS, for now.

                    Twenty-one dynamic boxes stacked into a wall is twenty-one bodies the solver
                    pays for every tick to hold something perfectly still - and rp3d would have to
                    be argued with to keep the stack from settling into a slouch on its own. The
                    kick-and-break slice flips the bricks it breaks to DYNAMIC at the moment of the
                    kick, which is both cheaper and more controllable than starting them loose.
                    They are built here so that slice has a wall to knock down.
                */
                float bw = p.w / (float)(p.cols > 0 ? p.cols : 1);
                float bh = p.h / (float)(p.rows > 0 ? p.rows : 1);
                for (int r = 0; r < p.rows; r++){
                    for (int c = 0; c < p.cols; c++){
                        snprintf(name,sizeof(name),"brick_%i_%i_%i",(int)i,r,c);
                        float bx = p.x - p.w * 0.5f + bw * ((float)c + 0.5f);
                        float by = p.y - p.h * 0.5f + bh * ((float)r + 0.5f);
                        Object* o = MakePlanarBody(unit_mesh,name,vec3(bx,by,0.0f),
                                                   vec3(bw * 0.97f,bh * 0.94f,PROP_DEPTH),
                                                   material_breakable,ARCHER_CAT_PROP,ARCHER_MASK_PROP,1.5f,true);
                        PropView view;
                        view.object = o;
                        view.kind = p.kind;
                        view.index = -1;    //a brick is not one of Stage's props in its own right
                        view.half_extents = vec3(bw * 0.485f,bh * 0.47f,PROP_DEPTH * 0.5f);
                        prop_views.push_back(view);
                    }
                }
            } break;

            case PROP_ROPE_ANCHOR: {
                BuildRope(p);
                //A marker, not a rope. The rope slice hangs a chain of ball-and-socket joints from
                //here; apps/tank/CraneCharacter.cpp already builds that shape (a pendulum with a
                //load on the end) and is the working example to copy.
                snprintf(name,sizeof(name),"rope_anchor_%i",(int)i);
                Object* o = MakePlanarBody(unit_mesh,name,vec3(p.x,p.y,0.0f),
                                           vec3(1.20f,0.20f,PROP_DEPTH),material_ledge,
                                           ARCHER_CAT_LEVEL,ARCHER_MASK_LEVEL,0.0f,true);
                PropView view;
                view.object = o;
                view.kind = p.kind;
                view.index = (int)i;
                view.half_extents = vec3(0.60f,0.10f,PROP_DEPTH * 0.5f);
                prop_views.push_back(view);
                rope_anchor_object = o;
            } break;

            case PROP_STRAWMAN: {
                PropView view = MakeStrawMan(p,(int)i);
                if (view.object){
                    prop_views.push_back(view);
                }
            } break;

            default: break;
        }
    }
    debug->Info("Built %i prop bodies\n",(int)prop_views.size());
}

void ApplicationArcher::BuildArcher(){
    /*
        The hybrid body. See the long note at the top of ApplicationArcher.h - this is the half of
        it that exists at build time, and DriveArcherBody is the half that runs every tick.

        KINEMATIC: it is moved by having its velocity set, it ignores forces and gravity entirely,
        and it shoves dynamic bodies out of its way. It is NOT in the LEVEL category's way and does
        not need to be - Stage resolves the archer against the level by hand, and a second opinion
        from the solver on a question that already has an answer is how you get a character that
        grinds through walls.
    */
    /*
        THE SIZE HERE IS THE SIZE IN Stage.h, and it has to be: MakePlanarBody builds the collider
        from it, so anything else gives the archer a body whose shape disagrees with the box the
        rules are sweeping. The first version of this passed a unit cube and got exactly that - a
        1x1x1 collider around a 0.7x1.8 character, which overlapped things the archer was nowhere
        near.
    */
    archer_object = MakePlanarBody(unit_mesh,"archer",vec3(stage.pos.x,stage.pos.y,0.0f),
                                   vec3(ARCHER_HALF_W * 2.0f,ARCHER_HALF_H * 2.0f,ARCHER_DEPTH),
                                   material_archer,ARCHER_CAT_ARCHER,ARCHER_MASK_ARCHER,
                                   70.0f,false);
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (p){
        p->SetBodyType(rp3d::BodyType::KINEMATIC);
    }
}

/*
    The character, as a skinned model.

    EVERY FAILURE IN HERE IS SURVIVABLE ON PURPOSE. If the file is missing, or the skin is named
    something else, or a clip did not make it through the export, the app keeps running on the
    coloured box it has drawn since the first slice - because the prototype's job is the mechanics
    and losing all of them to a bad export would be the wrong trade. What is NOT survivable is
    failing quietly, so each of the three names this depends on is reported separately, and the
    names the file actually contains are listed next to the one that was wanted.

    RENDER THREAD ONLY - GetMeshFromNode uploads a mesh.
*/
void ApplicationArcher::BuildArcherModel(){
    //ARCHER_MODEL_ASSET is already loaded - Init reads it before BuildCrateMesh.

    /*
        Built HERE and handed to the loader rather than returned by it, because the archer needs
        Skeleton plus one override - see ArcherModel. GetSkeleton fills in whatever it is given.
    */
    archer_model = new ArcherModel();
    if (!gltfloader.GetSkeleton(ARCHER_MODEL_SKIN,assetmanager,archer_model)){
        delete archer_model;
        archer_model = NULL;
    }
    if (!archer_model){
        debug->Err("No skin called '%s' in %s - the archer stays a box\n",
                   ARCHER_MODEL_SKIN,ARCHER_MODEL_ASSET);
        std::vector<std::string> names = gltfloader.GetSkeletonNames();
        for (size_t i = 0; i < names.size(); i++){
            debug->Err("   the file has: %s\n",names[i].c_str());
        }
        return;
    }

    /*
        The skinned mesh, by NODE name rather than by mesh name - and that distinction has bitten
        this codebase before (see the note in bomber's BuildSkinnedActor). The export also carries
        a second, UNRIGGED copy of the same body sitting at the scene root, which is the source
        mesh the rig was built from; asking for the node by name is what leaves it behind.
    */
    std::vector<Material> loaded_materials;
    Mesh* skinned_mesh = gltfloader.GetMeshFromNode(ARCHER_MODEL_NODE,&loaded_materials,true);
    if (!skinned_mesh){
        debug->Err("No skinned mesh on node '%s' in %s - the archer stays a box\n",
                   ARCHER_MODEL_NODE,ARCHER_MODEL_ASSET);
        archer_model = NULL;
        return;
    }
    archer_model->SetMesh(skinned_mesh);
    archer_model->name = "archer_model";

    main_scene->renderer->AddMaterials(loaded_materials);
    archer_model->TakeMaterialNames(loaded_materials);
    archer_model->PickMaterials(loaded_materials,main_scene->renderer->materials);
    if (loaded_materials.empty()){
        //An export with no material at all would otherwise leave slot 0 at 0, which is the ground.
        //A flat colour is still a character; whatever ar_ground looks like on a person is not.
        debug->Err("No material in %s - the archer wears a flat colour\n",ARCHER_MODEL_ASSET);
        archer_model->SetMaterialSlot(0,material_archer_skin);
    }

    /*
        HOW BIG SHE IS, measured off the bind pose rather than typed in.

        The rig is authored around 0.89 units tall and the body box the whole level is built
        around is ARCHER_MODEL_HEIGHT, so something has to scale. Doing it from the bones means a
        re-export at a different size corrects itself instead of silently standing the character
        in a level built for someone twice her height - and the same walk means a different world
        speed at a different scale, which is exactly the number MeasureClips is about to need.

        Done BEFORE the scale is applied, so these are the rig's own units.
    */
    std::vector<Bone*> bones;
    archer_model->GetAllBones(archer_model,bones);
    float lo = 0.0f;
    float hi = 0.0f;
    for (size_t i = 0; i < bones.size(); i++){
        float y = bones[i]->GetWorldPosition().y;
        if (i == 0 || y < lo){ lo = y; }
        if (i == 0 || y > hi){ hi = y; }
    }
    float rig_height = hi - lo;
    if (rig_height > 0.01f){
        model_scale = ARCHER_MODEL_HEIGHT / rig_height;
    }
    /*
        Where her feet are once she has been scaled. The model is placed at the BOTTOM of the body
        box, so this is what stops her hovering or sinking.

        FROM THE SKIN, NOT THE BONES. The lowest bone is LeftToe_End at 0.0254 rig units, and that
        joint is inside the shoe: the sole is under it. Standing the lowest bone on the floor sank
        her by 0.0254 x 2.02 = 5.1cm in every pose, which looked like the collider sinking or the
        export not sitting on zero. Neither - measured off the .glb, the bind-pose sole is at
        0.0000 and Standing_DrawArrow holds it there to a tenth of a millimetre. The height above
        stays on the bones, because a scale measured to the top of the hair would make her
        shorter every time the hairstyle got taller.
    */
    float sole = lo;
    const std::vector<skinned_vertex>& skin = skinned_mesh->GetSkinnedVertices();
    for (size_t i = 0; i < skin.size(); i++){
        if (i == 0 || skin[i].pos.y < sole){
            sole = skin[i].pos.y;
        }
    }
    if (skin.empty()){
        debug->Err("No CPU copy of the skin - feet placed from the lowest bone, %.4f too low\n",lo);
    }
    model_foot_offset = sole * model_scale;
    archer_model->SetScale(vec3(model_scale,model_scale,model_scale));
    debug->Info("Archer rig is %.4f units over %i bones -> scale %.3f, foot offset %.4f "
                "(sole %.4f, lowest bone %.4f)\n",
                rig_height,(int)bones.size(),model_scale,model_foot_offset,sole,lo);
    debug->Info("Archer mesh: %u vertices, mode %i, skeleton num_bones %i, material slot %i\n",
                skinned_mesh->num_vertices,skinned_mesh->mesh_mode,archer_model->num_bones,
                archer_model->GetMaterialSlot(0));

    main_scene->AddObject(archer_model);

    /*
        The clips.

        EVERY clip in the table is loaded, including the four the game has no use for. The export
        is all-or-nothing and "did that export correctly" is a question about all of them, so they
        are all loaded and all previewable - see ANIM_FROM_CLIP.

        POSITION extraction is left off on every one of them. That is a decision, not an oversight:
        locomotion here is driven by the rules and the animation is slaved to it, which is what a
        platformer wants (see the caution at the end of animation_plan.md section 7). The root
        TRACK is still resolved, because MeasureClips reads it to find out how fast each clip
        thinks it is moving.

        YAW extraction is per clip, off the table's f_turns column. A pivot means its hip rotation
        and has to have it taken out to the character's transform; a run cycle's hip rotation is
        the gait and has to STAY ON THE BONE, or the whole body wags. Both of those are now real
        options - until the core change on 2026-09-22 the yaw came off the bone whichever it was.
    */
    int loaded = 0;
    for (int i = 0; i < CLIP_COUNT; i++){
        Animation* clip = gltfloader.LoadAnimation(ARCHER_CLIPS[i].name);
        if (!clip){
            debug->Err("No clip called '%s' in %s\n",ARCHER_CLIPS[i].name,ARCHER_MODEL_ASSET);
            continue;
        }
        //First, so that every measurement below is taken off the clip as it will be played.
        if (ARCHER_CLIPS[i].trim_start > 0.0f || ARCHER_CLIPS[i].trim_end > 0.0f){
            float full = clip->duration;
            clip->Trim(ARCHER_CLIPS[i].trim_start,ARCHER_CLIPS[i].trim_end);
            debug->Info("Clip %-22s trimmed to %.3f..%.3f of its %.3fs: %.3fs, %d ticks\n",
                        ARCHER_CLIPS[i].name,clip->trim_offset,clip->trim_offset + clip->duration,
                        full,clip->duration,(int)(clip->duration * ARCHER_TPS + 0.5f));
        }
        clip->looped = ARCHER_CLIPS[i].f_looping;
        clip->extract_yaw_root_motion = ARCHER_CLIPS[i].f_turns;
        /*
            And the translation, which had been left at its default of "stays on the bone" for
            every clip until 2026-09-22 - so every gait drew itself a stride AHEAD of the position
            Stage had already walked her to, and snapped back at the wrap. See f_extract_move.

            What comes off the bone is handed to ArcherModel::ApplyRootMotion, which keeps the yaw
            and throws the translation away, because in this game the rules are the only thing
            allowed to decide where she is.
        */
        clip->extract_horizontal_root_motion = ARCHER_CLIPS[i].f_extract_move;
        clip->extract_vertical_root_motion = ARCHER_CLIPS[i].f_extract_lift;
        archer_model->AddAnimation(clip);       //binds its tracks to these bones
        clip->SetRootBone(ARCHER_MODEL_ROOT_BONE);
        if (!clip->root_track){
            //Not fatal, but it means this clip's travel can never be measured or extracted - so
            //it is worth saying out loud rather than discovering as a clip that refuses to slide.
            debug->Err("Clip '%s' has no track for root bone '%s'\n",
                       ARCHER_CLIPS[i].name,ARCHER_MODEL_ROOT_BONE);
        }
        archer_clips[i] = clip;
        loaded++;
    }
    if (loaded < CLIP_COUNT){
        std::vector<std::string> names = gltfloader.GetAnimationNames();
        debug->Err("%i of %i clips loaded. The file contains:\n",loaded,CLIP_COUNT);
        for (size_t i = 0; i < names.size(); i++){
            debug->Err("   %s\n",names[i].c_str());
        }
    }

    MeasureClips();
    MeasureClipPhases();
    MeasureAirClips();
    MeasureKickClip();
    MeasureKneelClips();
    MeasureRopeClimb();
    MeasureLedgeClimb();
    MeasureLedgeHang();

    /*
        GETUP_TICKS lives in Stage.h, which has never seen a .glb, so like KICK_TICKS it cannot
        follow a re-export by itself: a shorter clip would be stretched to fill the old lock, a
        longer one squeezed. Say the number to type instead.
    */
    if (archer_clips[CLIP_LAYING_UP]){
        float duration = archer_clips[CLIP_LAYING_UP]->duration;
        int clip_ticks = (int)(duration * ARCHER_TPS + 0.5f);
        if (clip_ticks < GETUP_TICKS - 1 || clip_ticks > GETUP_TICKS + 1){
            debug->Warn("%s is %d ticks but GETUP_TICKS is %d, so it will play at %.2fx. "
                        "Set GETUP_TICKS to %d in Stage.h.\n",ARCHER_CLIPS[CLIP_LAYING_UP].name,
                        clip_ticks,GETUP_TICKS,duration / ((float)GETUP_TICKS * ARCHER_DT),clip_ticks);
        }
    }else{
        debug->Warn("No %s in the export - the level entry still locks the controls for %d ticks, "
                    "standing\n",ARCHER_CLIPS[CLIP_LAYING_UP].name,GETUP_TICKS);
    }

    /*
        The default crossfade, kept SHORT.

        A quarter of a second is a reasonable default for a cinematic character and far too long
        for a fast platformer - at ARCHER_RUN_SPEED she covers 2.25 units during one, which is
        most of a jump. 0.15s is 9 ticks, still long enough to hide a pose change. This is the
        blunt instrument that step 1 and step 4 replace: per-transition times where a transition
        still exists, and no transition at all for locomotion.
    */
    archer_model->animation_transition_time_max = 0.15f;

    /*
        EXCEPT INTO THE RUN-TO-STOP, WHICH HAS TO BE FASTER THAN ITS OWN FIRST BEAT.

        The clip's plant is 0.267s in and it is capped at 2.50x, so the plant lands 6.4 ticks after
        the clip starts - inside the 9 ticks the default crossfade takes. Measured with the default,
        the hip at the plant read 0.453 against the clip's authored 0.345: more than half of the
        thing the clip exists for had been blended away.

        Four ticks. The wildcard `from` is deliberate - she can enter this off any rung of the
        ladder, and the answer is the same from all of them. It is cheap because the poses either
        side are close: the clip opens on a running pose, which is what she is already in.
    */
    archer_model->SetBlendTime("",ARCHER_CLIPS[CLIP_STOP].name,0.067f);

    /*
        None at all from the draw into the hold at full draw. Standing_AimArrowIdle begins on the
        draw's last pose to three decimals, so there is nothing to blend - and a crossfade would
        blend the draw's held last frame with a loop that has already started moving, which is a
        small hitch rather than no hitch.
    */
    archer_model->SetBlendTime(ARCHER_CLIPS[CLIP_DRAW].name,ARCHER_CLIPS[CLIP_AIM_IDLE].name,0.0f);

    /*
        Out of the ledge hang into the climb, as long as the re-grip: the stretch of the move where
        Stage lifts her more than the clip does while her hands go from hooked over the lip to flat
        on top (LEDGE_CLIMB_REGRIP). Shorter, and the crossfade would plant the hands while the
        body was still hitching up under them. 0.25s at 60 ticks.
    */
    archer_model->SetBlendTime(ARCHER_CLIPS[CLIP_HANG].name,ARCHER_CLIPS[CLIP_CLIMB].name,
                               (float)LEDGE_CLIMB_TICKS * ARCHER_DT * (float)LEDGE_CLIMB_REGRIP_SAMPLES /
                               (float)(LEDGE_CLIMB_PATH_SAMPLES - 1));

    //Start her standing, and hide the box she has been standing in for five slices.
    if (archer_clips[CLIP_IDLE]){
        archer_model->SwitchToAnimation(archer_clips[CLIP_IDLE]);
        playing_clip = CLIP_IDLE;
    }
    if (archer_object){
        archer_object->SetVisibility(f_show_collider);
    }
}

/*
    The bow into her left hand and the nocked arrow into her right.

    Almost all of the thinking behind this is in apps/archer/Bow.h; what belongs here is only the
    wiring and the one decision the app owns - WHICH CLIP IS THE REFERENCE POSE.

    That is CLIP_DRAW (Standing_DrawArrow): the bow's grip is chosen so it stands upright, facing
    her forward, at the clip's LAST frame - full draw, the pose the bow is looked at in. Until the
    rig has a socket bone for the bow (bow_plan.md §8, items 8-9) this one clip decides how the bow
    sits in her hand in every other clip too.
*/
void ApplicationArcher::BuildBow(){
    if (!archer_model){
        //No skeleton means no hands to hang anything off. Not an error worth shouting about: the
        //model failing to load has already been reported by BuildArcherModel in far more detail.
        return;
    }
    Animation* reference = archer_clips[CLIP_DRAW];
    if (!bow_rig.Build(gltfloader,archer_model,renderer,reference)){
        debug->Err("The bow could not be equipped - she plays empty-handed\n");
    }
    //The aim override bends the chain that carries the bow, and checks itself against the bow.
    //The upper layer's mask is built here too, after the props are on, so it can leave them out.
    archer_model->BuildUpperMask();
    archer_model->BuildOverlay();
    archer_model->BuildAimChain();
    archer_model->BuildLegChains();
    archer_model->aim_probe = bow_rig.bow.object;
}

/*
    How fast each clip thinks it is moving, and how long it lasts.

    MEASURED FROM THE CLIP, which is the whole point. The gap between a clip's own stride speed and
    the speed the rules move the archer at IS the foot slide, and it is the first number worth
    knowing about any locomotion animation. Typing it into a table would mean a re-export with a
    longer stride silently keeping the old figure - and the symptom of that is feet that skate,
    which is exactly what this is for.

    The root track is the hip bone, so this is the hip's horizontal travel over the clip divided by
    its duration, in the RIG's units. Puppet::WorldClipSpeed multiplies by model_scale to get world
    units, because a rig scaled to twice its size covers twice the ground with the same clip.
*/
void ApplicationArcher::MeasureClips(){
    puppet.model_scale = model_scale;
    for (int i = 0; i < CLIP_COUNT; i++){
        Animation* clip = archer_clips[i];
        if (!clip){
            continue;
        }
        puppet.clip_duration[i] = clip->duration;
        puppet.clip_speed[i] = 0.0f;
        puppet.clip_turn_deg[i] = 0.0f;
        if (!clip->root_track || clip->duration <= 0.0f){
            continue;
        }
        RootPose start = clip->ComputeRootPose(0.0f);
        RootPose end   = clip->ComputeRootPose(clip->duration);
        /*
            How far this clip turns her, end against start.

            Measured for EVERY clip, not just the travelling ones, because it is what says whether
            the f_turns column is set right: a cycle reads near zero however much its hips swing
            on the way round, and a pivot reads most of a half turn. Not acted on - the column is.
        */
        puppet.clip_turn_deg[i] = (end.twist_angle - start.twist_angle) / ARCHER_DEG2RAD;
        if (!ARCHER_CLIPS[i].f_travels){
            continue;
        }
        vec3 travel = end.authored_position - start.authored_position;
        //Horizontal only: the vertical component of a hip track is the body bobbing, not the
        //character climbing, and adding it in would report a walk as faster than it is.
        float distance = sqrtf(travel.x * travel.x + travel.z * travel.z);
        puppet.clip_speed[i] = distance / clip->duration;
        debug->Info("Clip %-20s %6.3fs  travels %5.3f rig units -> %5.2f/s native, %5.2f/s at scale\n",
                    ARCHER_CLIPS[i].name,clip->duration,distance,
                    puppet.clip_speed[i],puppet.WorldClipSpeed(i));
    }
    /*
        And the headline: how much clip is missing.

        ARCHER_RUN_SPEED against the fastest thing that has been authored. Logged at startup
        because it is the single number that decides what the next animation pass is for, and it
        is far easier to act on as a ratio than as "the feet look wrong".
    */
    float best = 0.0f;
    for (int i = 0; i < CLIP_COUNT; i++){
        float s = puppet.WorldClipSpeed(i);
        if (s > best){ best = s; }
    }
    if (best > 0.01f){
        debug->Info("Fastest authored locomotion is %.2f units/s; the game runs at %.2f. "
                    "Feet slide by %.2fx at a full run.\n",best,ARCHER_RUN_SPEED,
                    ARCHER_RUN_SPEED / best);
    }
}

/*
    Where in each locomotion clip's cycle the left foot is planted.

    MEASURED BY POSING THE MODEL AND WATCHING THE TOE, rather than by reasoning about the track
    data. The clip is applied at a series of times and the toe bone's world height read off each
    time; its lowest point is the plant. That runs the engine's own evaluation - the same
    SampleRootMotion and ApplyInterval the game will use - so the number describes what will
    actually be on screen rather than what the file says in isolation.

    This is the whole of what phase sync needs. Clips are authored with their footfalls wherever
    the animator started, and on this export the three rungs plant at 0.83, 0.69 and 0.62 of their
    cycle - up to a fifth of a cycle apart. Blend two of them on a shared playhead without
    correcting and a left-foot-down pose gets mixed with a mid-stride one, which is the skate.
    The DIFFERENCE between two of these values is the correction, so nothing has to be re-authored.

    Init only, before anything is being drawn - it leaves the skeleton posed at the last sample,
    and the first tick poses it properly.
*/
void ApplicationArcher::MeasureClipPhases(){
    Bone* toe = archer_model ? archer_model->FindBone(ARCHER_MODEL_TOE_BONE) : NULL;
    if (!toe){
        debug->Err("No bone '%s' - locomotion clips cannot be brought into phase\n",
                   ARCHER_MODEL_TOE_BONE);
        return;
    }
    /*
        SAMPLED AT THE KEYFRAMES, not on a uniform grid, and that is a correctness point rather
        than an efficiency one.

        `ObjectAnimation::GetClosestKeyframe` returns the first keyframe at or AFTER the time asked
        for - a ceiling, not a nearest - so a uniform scan does not see a smooth curve, it sees
        each keyframe's value repeated across the samples leading up to it. Taking the sample index
        where the extreme first appears therefore reports a time up to one whole keyframe interval
        EARLY, which at 30fps is 0.033s. Asking at the keyframe times instead makes the answer
        exact, and costs fewer samples than the grid did. (Poses have been sampled BLENDED since
        2026-09-26 - ObjectAnimation::Sample - and at a keyframe's own time the blend is that key,
        so this still holds.)
    */
    //The right toe too, for the footsteps - the same pose, read at the same keyframes.
    Bone* right_toe = archer_model->FindBone(ARCHER_MODEL_RIGHT_TOE_BONE);
    for (int i = 0; i < PUPPET_LOCOMOTION_COUNT; i++){
        int index = PUPPET_LOCOMOTION[i];
        Animation* clip = archer_clips[index];
        if (!clip || !clip->root_track || clip->duration <= 0.0f){
            continue;
        }
        float lowest = 0.0f, lowest_right = 0.0f;
        float lowest_at = 0.0f, lowest_right_at = 0.0f;
        bool f_first = true;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            //Zero-width window: poses the root bone without reporting the sample as motion.
            clip->SampleRootMotion(key->time,key->time);
            clip->ApplyInterval(key->time);
            float y = toe->GetWorldPosition().y;
            float y_right = right_toe ? right_toe->GetWorldPosition().y : 0.0f;
            if (f_first || y < lowest){
                lowest = y;
                lowest_at = key->time;
            }
            if (f_first || y_right < lowest_right){
                lowest_right = y_right;
                lowest_right_at = key->time;
            }
            f_first = false;
        }
        puppet.clip_phase[index] = lowest_at / clip->duration;
        //Without the bone, half a cycle on is the best guess there is, and it is said so.
        puppet.clip_phase_right[index] = right_toe ? lowest_right_at / clip->duration
                                                   : fmodf(puppet.clip_phase[index] + 0.5f,1.0f);
        debug->Info("Clip %-20s plants the left foot at phase %.2f (toe at %.3f), the right at %.2f%s\n",
                    ARCHER_CLIPS[index].name,puppet.clip_phase[index],lowest,
                    puppet.clip_phase_right[index],right_toe ? "" : " (no right toe bone - guessed)");
    }
}

/*
    Where each LANDING clip's feet actually touch the floor.

    A landing authored on its own starts in the air and falls, because that is what a landing is to
    an animator - FallingIdle_ToLanding descends 0.52 world units before contact. By the time this
    game plays one she is ALREADY standing on the ground; Stage put her there, and that is what
    fired the landing. Playing those frames sinks her half a body into the floor and pops her out.

    So it is entered at its own contact frame instead. Same answer as the jump's anticipation and
    for the same reason: the clip is right, the part of it this game can use starts later.

    FOUND WITH THE TOE, not the hip, and that distinction is the whole measurement. The hip keeps
    moving after contact - that is the absorb, and it is the point of the clip - so the hip says
    nothing about when the feet arrive. The toe stops descending and stays put, so contact is the
    FIRST sample within a hair of the toe's lowest point. Taking the lowest point itself would find
    the middle of the plant rather than its beginning.
*/
void ApplicationArcher::MeasureAirClips(){
    Bone* toe = archer_model ? archer_model->FindBone(ARCHER_MODEL_TOE_BONE) : NULL;
    if (!toe){
        return;     //MeasureClipPhases has already said so; no need to say it twice
    }
    Bone* hips = archer_model->FindBone(ARCHER_MODEL_ROOT_BONE);
    const int LANDINGS[] = { CLIP_LAND_SOFT, CLIP_LAND_HARD };
    for (int i = 0; i < (int)(sizeof(LANDINGS) / sizeof(LANDINGS[0])); i++){
        int index = LANDINGS[i];
        Animation* clip = archer_clips[index];
        if (!clip || !clip->root_track || clip->duration <= 0.0f){
            continue;
        }
        //At the keyframes rather than on a grid - see the note in MeasureClipPhases for why a
        //uniform scan reports an extreme up to one keyframe interval early.
        float lowest = 0.0f;
        bool f_first = true;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            clip->SampleRootMotion(key->time,key->time);    //zero-width: poses, reports no motion
            clip->ApplyInterval(key->time);
            float y = toe->GetWorldPosition().y;
            if (f_first || y < lowest){ lowest = y; f_first = false; }
        }
        /*
            A hair above the lowest, in WORLD units. Generous enough that a foot settling over two
            or three frames counts as having landed on the first of them, tight enough that the
            descent before contact never does.
        */
        const float SETTLED = 0.01f;
        float contact_at = 0.0f;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            clip->SampleRootMotion(key->time,key->time);
            clip->ApplyInterval(key->time);
            if (toe->GetWorldPosition().y <= lowest + SETTLED){
                contact_at = key->time;
                break;
            }
        }
        puppet.clip_entry[index] = contact_at;
        /*
            The hips' height at that contact frame, above the model's origin - what they are held
            at in the air (air_hip_ref). The routine landing's, and the hard one's is reported
            beside it: when the two agree, the hold has nothing to let go of at either touchdown.
        */
        if (hips){
            clip->SampleRootMotion(contact_at,contact_at);
            clip->ApplyInterval(contact_at);
            float at_contact = hips->GetWorldPosition().y - archer_model->GetWorldPosition().y;
            if (index == CLIP_LAND_SOFT || air_hip_ref <= 0.0f){
                air_hip_ref = at_contact;
            }
            debug->Info("Clip %-22s hips at %.3f on its contact frame%s\n",ARCHER_CLIPS[index].name,
                        at_contact,(index == CLIP_LAND_SOFT) ? " - held there in the air" : "");
        }
        debug->Info("Clip %-22s %.3fs long; feet land at %.3fs, leaving %.3fs to play\n",
                    ARCHER_CLIPS[index].name,clip->duration,puppet.clip_entry[index],
                    clip->duration - puppet.clip_entry[index]);
    }

    /*
        And how long the RUNNING JUMP climbs for - its first frame to its highest hip.

        The hip is the right bone here where it was the wrong one for a landing, and the difference
        is what is being asked. A landing asks "when do the feet arrive", which only the feet know.
        This asks "when does the body stop going up", which is the hip's whole job.

        Read off the AUTHORED track rather than by posing, because the extraction flags take this
        clip's horizontal off the bone and the question is about the clip, not about what survives
        of it. Only the climb is measured: the descent is 0.566s against the game's 0.327s, so no
        one rate serves both halves, and the climb is the half the rules actually guarantee.
    */
    Animation* run_jump = archer_clips[CLIP_RUN_JUMP];
    if (run_jump && run_jump->root_track && run_jump->duration > 0.0f){
        //Straight off the keyframes. No posing needed - the hip's height IS the root track - and no
        //grid, for the reason in MeasureClipPhases: a uniform scan would put the apex a frame early.
        float highest = 0.0f;
        float highest_at = 0.0f;
        bool f_first = true;
        for (ObjectAnimationKeyFrame* key : run_jump->root_track->keyframes){
            if (!key->f_position){
                continue;
            }
            if (f_first || key->position.y > highest){
                highest = key->position.y;
                highest_at = key->time;
                f_first = false;
            }
        }
        puppet.run_jump_rise = highest_at;
        float fit = (PUPPET_RISE_TIME > 0.0f) ? (puppet.run_jump_rise / PUPPET_RISE_TIME) : 0.0f;
        debug->Info("Clip %-22s %.3fs long; climbs for %.3fs against the game's %.3fs -> %.2fx\n",
                    ARCHER_CLIPS[CLIP_RUN_JUMP].name,run_jump->duration,
                    puppet.run_jump_rise,(float)PUPPET_RISE_TIME,fit);
    }

    /*
        And where the RUN-TO-STOP plants - its lowest hip, the beat that wants to land on the tick
        the rules actually bring her to rest.

        The hip again rather than the toe, and for the same reason as the jump: the question is
        "when does the body drop into the plant", which is what the hip does. A stop has no single
        moment of contact to look for - her feet are on the floor throughout.
    */
    Animation* stop = archer_clips[CLIP_STOP];
    if (stop && stop->root_track && stop->duration > 0.0f){
        float lowest = 0.0f;
        float lowest_at = 0.0f;
        bool f_first = true;
        for (ObjectAnimationKeyFrame* key : stop->root_track->keyframes){
            if (!key->f_position){
                continue;
            }
            if (f_first || key->position.y < lowest){
                lowest = key->position.y;
                lowest_at = key->time;
                f_first = false;
            }
        }
        puppet.stop_plant = lowest_at;
        /*
            And the headline gap, which is the largest in the file after the climb. The rules stop
            her from a full sprint in PUPPET_STOP_TIME - five ticks - and the clip wants to take
            this long just to reach its plant. Reported rather than quietly clamped, because the
            answer is a decision about ARCHER_RUN_FRICTION rather than a bug.
        */
        debug->Info("Clip %-22s %.3fs long; plants at %.3fs against the rules' %.3fs stop -> "
                    "wants %.2fx, capped at %.2fx\n",
                    ARCHER_CLIPS[CLIP_STOP].name,stop->duration,puppet.stop_plant,
                    (float)PUPPET_STOP_TIME,puppet.stop_plant / (float)PUPPET_STOP_TIME,
                    puppet.StopRate());
    }
}

/*
    When the boot connects.

    MEASURED BY REACH, not by the root track, because a kick is the one move whose whole point
    happens at the far end of a limb - the hips barely move (0.000 units of travel across the
    clip) while the foot travels most of a body length. So this poses the model at every keyframe
    and asks which foot is furthest from the hips horizontally; the peak is the strike.

    BOTH FEET, and taking whichever reaches further, because nothing here knows which leg was
    authored to do the kicking and a re-export could swap it.

    ALL THREE KICKS, one KICK_SPECS row each. Besides the strike it reports where the boot IS at
    the strike - its height against the body's centre and how far it is past her leading edge,
    which are the row's y_offset and reach - and the boot's top speed on the way there, which is
    what the row's speed and lift were weighed against. Only the timing is checked with a warning;
    the box and the impact are the feel, and are logged beside the numbers in use to be judged.
*/
void ApplicationArcher::MeasureKickClip(){
    Bone* hip = archer_model ? archer_model->FindBone(ARCHER_MODEL_ROOT_BONE) : NULL;
    Bone* toe[2] = { archer_model ? archer_model->FindBone("mixamorig:LeftToeBase") : NULL,
                     archer_model ? archer_model->FindBone("mixamorig:RightToeBase") : NULL };
    if (!hip || !toe[0] || !toe[1]){
        return;
    }
    for (int kind = 0; kind < KICK_KIND_COUNT; kind++){
        const KickSpec& spec = KICK_SPECS[kind];
        int clip_index = PUPPET_KICK_CLIP[kind];
        Animation* clip = archer_clips[clip_index];
        if (!clip || !clip->root_track || clip->duration <= 0.0f){
            debug->Warn("No %s clip - that kick will play whatever was playing\n",spec.name);
            continue;
        }
        float furthest = 0.0f;
        float furthest_at = 0.0f;
        int   kicking = 0;          //which toe did it
        bool f_first = true;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            clip->SampleRootMotion(key->time,key->time);    //zero-width: poses, reports no motion
            clip->ApplyInterval(key->time);
            vec3 h = hip->GetWorldPosition();
            for (int i = 0; i < 2; i++){
                vec3 t = toe[i]->GetWorldPosition();
                float dx = t.x - h.x;
                float dz = t.z - h.z;
                float reach = sqrtf(dx * dx + dz * dz);
                if (f_first || reach > furthest){
                    furthest = reach;
                    furthest_at = key->time;
                    kicking = i;
                    f_first = false;
                }
            }
        }
        puppet.kick_strike[kind] = furthest_at;

        /*
            The boot at the strike, in the rules' terms. The model's origin is model_foot_offset
            under the body's bottom (see MeasureLedgeHang), so the body's centre - pos.y - is
            ARCHER_HALF_H above that. Reach is measured from the model's origin, which is her
            centre line, less ARCHER_HALF_W to put it past her leading edge where the box starts.

            And its top speed over the wind-up: the kicking toe's speed relative to her origin,
            keyframe to keyframe, up to the strike.
        */
        vec3 origin = archer_model->GetWorldPosition();
        float top_speed = 0.0f;
        vec3 prev;
        float prev_t = 0.0f;
        bool f_have_prev = false;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            if (key->time > furthest_at + 0.0001f){
                break;
            }
            clip->SampleRootMotion(key->time,key->time);
            clip->ApplyInterval(key->time);
            vec3 t = toe[kicking]->GetWorldPosition() - origin;
            if (f_have_prev && key->time > prev_t){
                vec3 d = t - prev;
                float v = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z) / (key->time - prev_t);
                if (v > top_speed){
                    top_speed = v;
                }
            }
            prev = t;
            prev_t = key->time;
            f_have_prev = true;
        }
        clip->SampleRootMotion(furthest_at,furthest_at);
        clip->ApplyInterval(furthest_at);
        vec3 boot = toe[kicking]->GetWorldPosition() - origin;
        float boot_y = boot.y - model_foot_offset - ARCHER_HALF_H;
        float boot_reach = sqrtf(boot.x * boot.x + boot.z * boot.z) - ARCHER_HALF_W;

        /*
            And the check the rules cannot make for themselves. The active window is in ticks into
            the move; at a playback rate of 1.0 those are ticks into the CLIP too, so the strike
            should sit inside it. Printed either way, because the interesting case is when it stops
            doing so.
        */
        float strike_tick = furthest_at * ARCHER_TPS;
        debug->Info("Clip %-22s %.3fs long; the boot connects at %.3fs (tick %.1f of %d), and the "
                    "rules' active window is ticks %d..%d%s\n",
                    spec.name,clip->duration,furthest_at,strike_tick,
                    (int)(clip->duration * ARCHER_TPS),spec.active_from,spec.active_to,
                    (strike_tick >= (float)spec.active_from && strike_tick <= (float)spec.active_to)
                        ? "" : "  <-- THE WINDOW DOES NOT COVER THE STRIKE");
        debug->Info("     %-22s boot at y %+.2f, %.2f past her edge, %.1f/s at most - box centred "
                    "y %+.2f +-%.2f reaching %.2f, imparts %.1f along and %.1f up\n",
                    spec.name,boot_y,boot_reach,top_speed,spec.y_offset,spec.half_height,
                    spec.reach,spec.speed,spec.lift);

        /*
            AND WHETHER THE ROW'S LENGTH STILL MATCHES THE CLIP AT ALL.

            This is the one number a re-export cannot fix for itself. Everything else in this app
            re-measures at load, but KICK_SPECS lives in Stage.h, which names no engine type and
            has never seen a .glb - so trimming frames off the end of the clip does NOT shorten the
            move, it makes the Puppet stretch what is left to fill the window it no longer fills.
            The symptom is a kick in slow motion, which looks like a rate bug and is not one.

            So the app prints the number to type. It cannot apply it, but it can stop it being a
            thing anyone has to notice for themselves. KICK_FRONT's row is KICK_TICKS.
        */
        int clip_ticks = (int)(clip->duration * ARCHER_TPS + 0.5f);
        if (clip_ticks < spec.ticks - 1 || clip_ticks > spec.ticks + 1){
            debug->Warn("%s is %d ticks but its KICK_SPECS row says %d, so it will play at %.2fx. "
                        "Set it to %d in Stage.cpp%s.\n",
                        spec.name,clip_ticks,spec.ticks,
                        clip->duration / ((float)spec.ticks * ARCHER_DT),clip_ticks,
                        (kind == KICK_FRONT) ? " (KICK_TICKS in Stage.h)" : "");
        }
    }
}

/*
    The kneel set, checked against the rules - which cannot read a .glb, so the app prints the
    numbers to type, the KICK_TICKS arrangement.

    THE TRANSITIONS ARE TIMED TO WHERE THE HIP SETTLES, not to the clip's end - see
    Puppet::clip_settle. Read off the authored hip track: the first keyframe after which the hip
    stays within KNEEL_SETTLE_FRACTION of its whole travel from the final height.

    THE HEIGHT is head over toes: HeadTop_End above the lowest toe, in Kneel_Idle (its lowest
    keyframe, so the box is never shorter than she gets) against Idle's first frame, as a ratio of
    the standing body. A ratio because the toe bone sits a little above the sole in both, and that
    offset cancels.
*/
/*
    Rope_Climbing's rise, keyframe by keyframe - see the declaration.

    At the KEYFRAMES rather than on a grid, for the reason in MeasureClipPhases: the closest-keyframe
    lookup is a ceiling, and a grid sampled through it reads every value early. Made MONOTONIC with
    a running maximum because the playhead is found by inverting this curve - the hips dip 0.001 in
    the mid-cycle pause, and a curve that goes back down has no inverse there.
*/
void ApplicationArcher::MeasureRopeClimb(){
    puppet.climb_times.clear();
    puppet.climb_rise.clear();
    puppet.climb_cycle_rise = 0.0f;
    Animation* clip = archer_clips[CLIP_ROPE_CLIMB];
    if (!clip || !clip->root_track || clip->root_track->keyframes.size() < 2){
        debug->Warn("No Rope_Climbing to climb with - climbing the rope keeps Hanging_Rope\n");
        return;
    }
    float y0 = 0.0f;
    float best = 0.0f;
    bool f_first = true;
    for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
        float y = clip->ComputeRootPose(key->time).authored_position.y;
        if (f_first){
            y0 = y;
            f_first = false;
        }
        float rise = (y - y0) * model_scale;
        if (rise < best){
            rise = best;
        }
        best = rise;
        puppet.climb_times.push_back(key->time);
        puppet.climb_rise.push_back(rise);
    }
    puppet.climb_cycle_rise = puppet.climb_rise.back();
    float native = (clip->duration > 0.0f) ? puppet.climb_cycle_rise / clip->duration : 0.0f;
    debug->Info("Rope_Climbing: %zu keys, rises %.3f a cycle of %.3fs - %.3f/s native, climbed at %.2f/s "
                "(the pose runs %.2fx)\n",puppet.climb_times.size(),puppet.climb_cycle_rise,clip->duration,
                native,ROPE_CLIMB_SPEED,(native > 0.0f) ? ROPE_CLIMB_SPEED / native : 0.0f);
}

/*
    Climb's hip path, against the copy Stage carries her along - the KICK_TICKS arrangement: Stage
    has never seen a .glb, so the app measures and prints what to type.

    Interpolated between KEYFRAMES rather than read off ComputeRootPose on a grid, whose
    closest-keyframe lookup would read every sample early (see MeasureRopeClimb). Up is the hips'
    Y, across their Z - the clip's forward, towards the wall.
*/
/*
    Hanging_Braced's fingers against the lip - LEDGE_HANG_DROP's check.

    The finger JOINTS, both middle fingers, averaged: they lie flat over the lip in this clip, so
    they are what rests on the stone, and a joint sits about a finger's half-thickness above the
    skin it is inside - hence the 0.02 aimed for rather than 0. Read off the posed model at its
    first frame: the hang is not extracted and has no layer over it, so this is the pose on
    screen. Only heights are compared, which the model's load-time yaw does not touch.
*/
void ApplicationArcher::MeasureLedgeHang(){
    Animation* clip = archer_clips[CLIP_HANG];
    if (!clip || !clip->root_track || clip->root_track->keyframes.empty() || !archer_model){
        return;
    }
    const char* names[] = { "mixamorig:LeftHandMiddle1", "mixamorig:LeftHandMiddle2",
                            "mixamorig:LeftHandMiddle3", "mixamorig:LeftHandMiddle4",
                            "mixamorig:RightHandMiddle1", "mixamorig:RightHandMiddle2",
                            "mixamorig:RightHandMiddle3", "mixamorig:RightHandMiddle4" };
    float t = clip->root_track->keyframes.front()->time;
    clip->SampleRootMotion(t,t);    //zero-width: poses, reports no motion
    clip->ApplyInterval(t);
    float sum = 0.0f;
    int n = 0;
    for (const char* name : names){
        Bone* b = archer_model->FindBone(name);
        if (b){
            sum += b->GetWorldPosition().y - archer_model->GetWorldPosition().y;
            n++;
        }
    }
    if (n == 0){
        return;
    }
    //The model's origin is model_foot_offset below the body's bottom, and the body's top hangs
    //LEDGE_HANG_DROP below the lip.
    const float rest = 0.02f;
    float above_top = sum / (float)n - ARCHER_HALF_H * 2.0f - model_foot_offset;
    float fingers = above_top - LEDGE_HANG_DROP;
    debug->Info("Clip %-22s fingers %.3f above the lip, hung LEDGE_HANG_DROP %.2f below it\n",
                ARCHER_CLIPS[CLIP_HANG].name,fingers,LEDGE_HANG_DROP);
    if (fabsf(fingers - rest) > 0.02f){
        debug->Warn("Hanging_Braced's fingers %s the lip by %.3f. Set LEDGE_HANG_DROP to %.2f in "
                    "Stage.h (and re-check LEDGE_CLIMB_UP, which MeasureLedgeClimb will print).\n",
                    (fingers > rest) ? "float above" : "sink into",fabsf(fingers - rest),
                    above_top - rest);
    }
}

void ApplicationArcher::MeasureLedgeClimb(){
    Animation* clip = archer_clips[CLIP_CLIMB];
    if (!clip || !clip->root_track || clip->root_track->keyframes.size() < 2 || clip->duration <= 0.0f){
        debug->Warn("No Climb clip - the ledge climb has nothing to follow\n");
        return;
    }
    std::vector<float> times;
    std::vector<vec3> hips;
    for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
        times.push_back(key->time);
        hips.push_back(clip->ComputeRootPose(key->time).authored_position);
    }
    auto at = [&](float t){
        if (t <= times.front()){ return hips.front(); }
        for (size_t i = 1; i < times.size(); i++){
            if (t <= times[i]){
                float f = (t - times[i - 1]) / (times[i] - times[i - 1]);
                return hips[i - 1] + (hips[i] - hips[i - 1]) * f;
            }
        }
        return hips.back();
    };
    vec3 start = at(0.0f);
    vec3 end = at(clip->duration);
    float rise = end.y - start.y;
    float across = end.z - start.z;
    if (fabsf(rise) < 1e-4f || fabsf(across) < 1e-4f){
        debug->Warn("Climb's hips do not travel - the ledge climb path cannot be measured\n");
        return;
    }
    /*
        The rise as Stage should take it: the clip's own, plus the gap to the rules' rise put
        where no hand is planted - LEDGE_CLIMB_REGRIP over the crossfade in, the rest over the
        stand-up. See LEDGE_CLIMB_REGRIP.
    */
    float rules_rise = ARCHER_HALF_H * 2.0f + LEDGE_HANG_DROP + 0.001f;  //hang to standing, STAGE_EPS included
    float standup = rules_rise - rise * model_scale - LEDGE_CLIMB_REGRIP;
    const int last = LEDGE_CLIMB_PATH_SAMPLES - 1;
    float up_frac[LEDGE_CLIMB_PATH_SAMPLES];
    float across_frac[LEDGE_CLIMB_PATH_SAMPLES];
    float worst = 0.0f;
    for (int i = 0; i < LEDGE_CLIMB_PATH_SAMPLES; i++){
        vec3 h = at(clip->duration * (float)i / (float)last);
        float regrip = fminf(1.0f,(float)i / (float)LEDGE_CLIMB_REGRIP_SAMPLES);
        float stand = fmaxf(0.0f,(float)(i - LEDGE_CLIMB_STANDUP_FROM) /
                                 (float)(last - LEDGE_CLIMB_STANDUP_FROM));
        up_frac[i] = ((h.y - start.y) * model_scale + LEDGE_CLIMB_REGRIP * regrip + standup * stand) /
                     rules_rise;
        across_frac[i] = (h.z - start.z) / across;
        worst = fmaxf(worst,fmaxf(fabsf(up_frac[i] - LEDGE_CLIMB_UP[i]),
                                  fabsf(across_frac[i] - LEDGE_CLIMB_ACROSS[i])));
    }
    float window = (float)LEDGE_CLIMB_TICKS * ARCHER_DT;
    float inset = across * model_scale - ARCHER_HALF_W;
    debug->Info("Clip %-22s %.3fs from the grab; rises %.3f and steps %.3f across (the rules: %.3f, "
                "%.3f of it in the re-grip and %.3f in the stand-up, and %.3f); LEDGE_CLIMB_TICKS %d "
                "plays it at %.2fx; path within %.3f of Stage's\n",
                ARCHER_CLIPS[CLIP_CLIMB].name,clip->duration,rise * model_scale,across * model_scale,
                rules_rise,LEDGE_CLIMB_REGRIP,standup,LEDGE_CLIMB_INSET + ARCHER_HALF_W,
                LEDGE_CLIMB_TICKS,clip->duration / window,worst);
    if (worst > 0.02f){
        std::string up_row, across_row;
        char buf[16];
        for (int i = 0; i < LEDGE_CLIMB_PATH_SAMPLES; i++){
            snprintf(buf,sizeof(buf),"%s%.3ff",i ? ", " : "",up_frac[i]);
            up_row += buf;
            snprintf(buf,sizeof(buf),"%s%.3ff",i ? ", " : "",across_frac[i]);
            across_row += buf;
        }
        debug->Warn("Climb's path has moved %.3f from LEDGE_CLIMB_UP/ACROSS, so her hands will slide "
                    "on the lip. Set them in Stage.cpp to\n    UP     { %s }\n    ACROSS { %s }\n",
                    worst,up_row.c_str(),across_row.c_str());
    }
    if (fabsf(inset - LEDGE_CLIMB_INSET) > 0.05f){
        debug->Warn("Climb ends %.2f past the lip but LEDGE_CLIMB_INSET is %.2f, so she steps %s "
                    "than the clip does. Set LEDGE_CLIMB_INSET to %.2f in Stage.h.\n",
                    inset,LEDGE_CLIMB_INSET,(inset > LEDGE_CLIMB_INSET) ? "less far" : "further",inset);
    }
}

void ApplicationArcher::MeasureKneelClips(){
    if (!archer_model){
        return;
    }
    const struct { int clip; int ticks; const char* define; } TRANSITIONS[] = {
        { CLIP_KNEEL_DOWN, KNEEL_DOWN_TICKS, "KNEEL_DOWN_TICKS" },
        { CLIP_KNEEL_UP,   KNEEL_UP_TICKS,   "KNEEL_UP_TICKS"   },
    };
    const float KNEEL_SETTLE_FRACTION = 0.02f;
    for (int i = 0; i < 2; i++){
        int index = TRANSITIONS[i].clip;
        Animation* clip = archer_clips[index];
        if (!clip || !clip->root_track || clip->duration <= 0.0f || clip->root_track->keyframes.empty()){
            continue;
        }
        float lo = 0.0f;
        float hi = 0.0f;
        bool f_first = true;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            if (!key->f_position){ continue; }
            if (f_first || key->position.y < lo){ lo = key->position.y; }
            if (f_first || key->position.y > hi){ hi = key->position.y; }
            f_first = false;
        }
        float final_y = clip->root_track->keyframes.back()->position.y;
        float band = (hi - lo) * KNEEL_SETTLE_FRACTION;
        //Walking backwards from the end, the last keyframe still OUTSIDE the band; settled after it.
        float settle = 0.0f;
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            if (key->f_position && fabsf(key->position.y - final_y) > band){
                settle = key->time;
            }
        }
        //One keyframe on, so the move is finished rather than nearly finished.
        for (ObjectAnimationKeyFrame* key : clip->root_track->keyframes){
            if (key->time > settle){
                settle = key->time;
                break;
            }
        }
        puppet.clip_settle[index] = settle;
        int settle_ticks = (int)(settle * ARCHER_TPS + 0.5f);
        debug->Info("Clip %-22s %.3fs long; the hip settles at %.3fs (tick %d), and %s is %d%s\n",
                    ARCHER_CLIPS[index].name,clip->duration,settle,settle_ticks,
                    TRANSITIONS[i].define,TRANSITIONS[i].ticks,
                    (settle_ticks < TRANSITIONS[i].ticks - 1 || settle_ticks > TRANSITIONS[i].ticks + 1)
                        ? "  <-- SET IT IN Stage.h" : "");
    }

    Bone* head = archer_model->FindBone("mixamorig:HeadTop_End");
    Bone* toe[2] = { archer_model->FindBone("mixamorig:LeftToeBase"),
                     archer_model->FindBone("mixamorig:RightToeBase") };
    Animation* idle = archer_clips[CLIP_IDLE];
    Animation* kneel = archer_clips[CLIP_KNEEL_IDLE];
    if (!head || !toe[0] || !toe[1] || !idle || !kneel || !idle->root_track || !kneel->root_track ||
        idle->root_track->keyframes.empty()){
        return;
    }
    //Head over the lower toe, with `clip` posed at `time`.
    auto height_at = [&](Animation* clip, float time) -> float {
        clip->SampleRootMotion(time,time);      //zero-width: poses, reports no motion
        clip->ApplyInterval(time);
        float floor = fminf(toe[0]->GetWorldPosition().y,toe[1]->GetWorldPosition().y);
        return head->GetWorldPosition().y - floor;
    };
    float standing = height_at(idle,idle->root_track->keyframes.front()->time);
    float kneeling = 0.0f;
    bool f_first = true;
    for (ObjectAnimationKeyFrame* key : kneel->root_track->keyframes){
        float h = height_at(kneel,key->time);
        if (f_first || h < kneeling){ kneeling = h; f_first = false; }
    }
    if (standing <= 0.01f){
        return;
    }
    float half_h = ARCHER_HALF_H * kneeling / standing;
    debug->Info("Clip %-22s kneels to %.0f%% of her standing height -> KNEEL_HALF_H %.3f "
                "(Stage.h has %.3f)%s\n",ARCHER_CLIPS[CLIP_KNEEL_IDLE].name,
                100.0f * kneeling / standing,half_h,KNEEL_HALF_H,
                (fabsf(half_h - KNEEL_HALF_H) > 0.03f) ? "  <-- SET IT IN Stage.h" : "");
}

/*
    The nocked arrow's mesh, turned into one that flies.

    The file's arrow is authored as a PROP (Bow.h): origin at the nock, pointing along +Z, the
    character's forward - and nocked on the bow at identity. Flight wants the convention the
    box arrow always had and that SyncArrowViews relies on - the arrow runs along +X, one rotation
    about Z aims it - plus one the box did not have: THE TIP IS AT THE ORIGIN (give or take
    ARROW_TIP_EMBED). Stage's arrow is a
    point swept through the level, and that point is where it strikes; putting the tip on it means
    an arrow flies point-first and sticks with its head in the target, where the centred box stuck
    with half its length buried.

    DECLARED BY THE PROP CONVENTION, not searched for: the nock is at the origin and the point is
    the furthest the mesh reaches along +Z (0.519 in the 2026-09-23 export; the fletching reaches
    a little behind the nock, to −0.017). An earlier version searched the bounding box for the
    long axis and the far end, from before the props had a convention; a mesh that breaks the
    convention is now reported rather than accommodated.

    The SIZE is the nocked arrow's size in the world, read through its own world matrix, so a
    flying arrow is the arrow she was holding - whatever the rig scale and the bone chain do.

    The copy is a new mesh with the vertices rotated and scaled (normals and tangents rotated with
    them), registered as the asset ar_arrow_flight. The roll about the shaft is whatever the axis
    swap leaves; the fletching reads the same at any roll from a side-on camera.
*/
/*
    How far the head reaches PAST the arrow's point, in world units - so a stuck arrow is IN what
    it hit rather than touching it. Stage parks a stuck arrow 0.02 outside the surface it struck
    (Stage::TickArrows), which with the tip exactly on the point left the head resting against the
    wall; measured on a close-up, it read as leaning, not stuck. In flight this is 0.12 of a
    1.22-unit arrow at 46 units a second, which is nothing anyone can see.
*/
#define ARROW_TIP_EMBED             0.12f


Mesh* ApplicationArcher::BuildFlightArrowMesh(){
    Object* nocked = bow_rig.arrow.object;
    Mesh* source = nocked ? nocked->GetMesh() : NULL;
    if (!source || source->GetVertices().empty()){
        return NULL;
    }
    const std::vector<vertex>& in = source->GetVertices();

    vec3 lo = in[0].pos;
    vec3 hi = in[0].pos;
    for (size_t i = 1; i < in.size(); i++){
        const vec3& p = in[i].pos;
        lo = vec3(fminf(lo.x,p.x),fminf(lo.y,p.y),fminf(lo.z,p.z));
        hi = vec3(fmaxf(hi.x,p.x),fmaxf(hi.y,p.y),fmaxf(hi.z,p.z));
    }
    vec3 size = hi - lo;

    //The one check the convention still needs: the shaft runs along Z. A prop exported lying some
    //other way would fly sideways, and this says so instead of leaving it to be spotted in flight.
    if (size.z < size.x || size.z < size.y || hi.z <= 0.0f){
        debug->Err("The arrow mesh is not along +Z (box %.3f x %.3f x %.3f, z up to %.3f). The "
                   "prop convention is origin at the nock and the point toward the character's "
                   "forward - Blender -Y, glTF +Z; see Bow.h. It will fly crooked.\n",
                   size.x,size.y,size.z,hi.z);
    }

    //Nock at the origin, point at +Z - the furthest the mesh reaches along it.
    vec3 nock(0.0f,0.0f,0.0f);
    vec3 tip(0.0f,0.0f,hi.z);

    //World length as drawn on the bow, over local length: the scale that makes the copy match.
    fmat4& world = nocked->GetWorldTransformScaleMatrix();
    float world_length = (world * tip - world * nock).length();
    float local_length = hi.z;
    float scale = (local_length > 0.0001f) ? world_length / local_length : 1.0f;

    //+90 about Y takes +Z to +X, the direction SyncArrowViews aims along.
    quat to_x = quat(vec3(0,1,0),1.5707963f);

    std::vector<vertex> out(in.begin(),in.end());
    for (size_t i = 0; i < out.size(); i++){
        out[i].pos = (to_x * (out[i].pos - tip)) * scale + vec3(ARROW_TIP_EMBED,0.0f,0.0f);
        out[i].normal = to_x * out[i].normal;
        out[i].tangent = to_x * out[i].tangent;
    }
    Mesh* mesh = new Mesh();
    mesh->SetMeshData(out.data(),(int)out.size());
    mesh->num_materials = source->num_materials;
    assetmanager->AddNewAsset("ar_arrow_flight",mesh);

    debug->Info("Flight arrow from '%s': nock to point %.3f in the file -> %.3f in the world "
                "(x%.3f)\n",BOW_ARROW_NODE,local_length,world_length,scale);

    /*
        THE RULES' ARROW LENGTH, checked against this mesh. Stage cannot read a .glb, so
        ARROW_LENGTH is typed in - and this is what keeps it honest, the same arrangement as
        KICK_TICKS. Origin to point: the prop convention puts the origin at the nock, which is
        where the string holds it and where the rules' ANCHOR is.
    */
    float nock_to_tip = world_length;
    if (fabsf(nock_to_tip - ARROW_LENGTH) > 0.03f){
        debug->Warn("The arrow is %.3f from nock to point but ARROW_LENGTH is %.3f, so the loosed "
                    "arrow will appear %.3f %s the nocked one. Set ARROW_LENGTH to %.2f in "
                    "Stage.h.\n",nock_to_tip,ARROW_LENGTH,fabsf(nock_to_tip - ARROW_LENGTH),
                    (ARROW_LENGTH > nock_to_tip) ? "ahead of" : "behind",nock_to_tip);
    }
    return mesh;
}

void ApplicationArcher::BuildArrowViews(){
    //The real arrow if the bow loaded, the box it replaced if not - see BuildFlightArrowMesh.
    Mesh* flight = BuildFlightArrowMesh();
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        char name[32];
        snprintf(name,sizeof(name),"arrow_%i",i);
        Object* o = new Object();
        o->name = name;
        if (flight){
            o->SetMesh(flight);
            //By NAME, the nocked arrow's own: its three primitives are three materials, and the
            //names resolve to whatever the loader registered them as.
            o->SetMaterialNames(bow_rig.arrow.object->GetMaterialNames());
        }else{
            o->SetMesh(arrow_mesh);
            o->SetMaterialSlot(0,material_arrow);
        }
        //An arrow is a marker, not a wall: it should not throw a shadow across the level it is
        //stuck in. See f_casts_shadow in core/Object.h, which exists for exactly this.
        o->SetVisibility(false);
        main_scene->AddObject(o);
        arrow_objects[i] = o;
    }
}

void ApplicationArcher::BuildAimArc(){
    for (int i = 0; i < AIM_ARC_POINTS; i++){
        char name[32];
        snprintf(name,sizeof(name),"arc_%i",i);
        Object* o = new Object();
        o->SetMesh(dot_mesh);
        o->name = name;
        o->SetMaterialSlot(0,material_dot);
        o->SetVisibility(false);
        o->SetPickability(false);
        main_scene->AddObject(o);
        arc_objects[i] = o;
    }
}

/*
    The hit counter's parts - see HIT_POPUP_SLOTS for the shape of it.

    RENDER THREAD, from Init: BuildTextMesh uploads a mesh. Every number the counter can show is
    baked here, once, and registered as an asset (ar_hit_1 .. ar_hit_99), so the AssetManager holds
    the reference that keeps each one alive while the popups swap between them - the same job it
    does for ar_unit_box.

    UNLIT, NO SHADOW, NOT PICKABLE: the number is a readout that happens to live in the world, the
    same argument as the aim arc's beads. A count that dims as it floats into shadow, or throws a
    smudge across the target it came out of, is a readout that gets in the way of itself.

    SURVIVABLE if the glyph file is missing: the counter still counts (archer_state reports it),
    it just shows nothing.
*/
void ApplicationArcher::BuildHitPopups(){
    {
        Material m;
        m.name = "ar_hit_text";
        m.glsl_material.color = vec4(1.0f,0.93f,0.40f,1.0f);
        m.glsl_material.f_unlit = 1;
        renderer->AddMaterial(m);
        material_hit_text = renderer->FindMaterialIndex(m.name);
    }

    //Metrics from shared_assets/meshes/fonts_glyphs.json, as tetris, breakout and pinball pass them.
    if (!LoadGlyphSetFromGLB(hit_glyphs,"meshes/glyphs_unispace.glb",0.509167f,1.0f)){
        debug->Warn("No glyphs loaded - target hits will count but not show\n");
        return;
    }
    TextLayout layout;
    layout.scale = 1.0f;            //sized per popup with the Object's scale, so one bake serves
    layout.align = TEXT_ALIGN_CENTER;
    layout.matid = 0;
    int built = 0;
    for (int n = 1; n <= HIT_NUMBERS_MAX; n++){
        char text[8];
        snprintf(text,sizeof(text),"%i",n);
        Mesh* mesh = BuildTextMesh(hit_glyphs,text,layout,NULL);
        if (!mesh){
            continue;
        }
        char name[16];
        snprintf(name,sizeof(name),"ar_hit_%i",n);
        assetmanager->AddNewAsset(name,mesh);
        hit_number_meshes[n] = mesh;
        built++;
    }

    for (int i = 0; i < HIT_POPUP_SLOTS; i++){
        char name[32];
        snprintf(name,sizeof(name),"hit_popup_%i",i);
        Object* o = new Object();
        o->name = name;
        o->SetMesh(hit_number_meshes[1]);
        o->SetMaterialSlot(0,material_hit_text);
        o->SetCastsShadow(false);
        o->SetPickability(false);
        o->Hide();
        main_scene->AddObject(o);
        hit_popups[i].object = o;
    }
    debug->Info("Hit counter: %i numbers baked, %i popups\n",built,HIT_POPUP_SLOTS);
}

/*
    One arrow into one target. Physics thread, from ResolveArrowsAgainstProps.

    THE RANGE RULE: a FLOATING target (built with gravity off, see StageProp::f_floating) gets its
    gravity back on its RANGE_GRAVITY_HITS-th hit and drops. Only on the range and only for a
    floating one - a standing board already has gravity, and on the main level a hit only counts.
    Woken as well, because a body the solver has put to sleep would otherwise ignore gravity until
    something next touched it.
*/
/*
    Which ring of an archery stand a hit at `point` went into, as points: 10 for the centre, down
    to 2 for the rim, 0 off the board (a leg).

    BY HEIGHT ON THE FACE, NOT BY DISTANCE FROM THE CENTRE. Every arrow flies in the plane z = 0,
    and that plane cuts the face along one line - near enough its vertical diameter - so height is
    the only thing about a hit that can vary, and it is the thing the player sees and aims at.
    Distance from the centre would also count the sideways offset of that line, which the player
    cannot change and which grows as the board is turned toward the camera: at the 10 degrees this
    was first built with, every hit read as the outer ring. Height does not care about the yaw.

    In the stand's own frame, through its current rotation, so a stand that has been shoved or is
    rocking is still scored against its own face rather than against where the face used to be.
*/
static int StandRingPoints(const PropView& view, const vec3& point){
    if (!view.object || view.board_radius <= 0.0f){
        return 0;
    }
    quat q = view.object->GetWorldRotation();
    vec3 centre = view.object->GetWorldPosition() + q * view.board_centre;
    vec3 up = q * view.board_up;
    float along = fabsf((point - centre).dot(up)) / view.board_radius;
    for (int r = 0; r < STAND_RING_COUNT; r++){
        if (along <= STAND_RINGS[r]){
            return STAND_POINTS[r];
        }
    }
    return 0;
}

void ApplicationArcher::RegisterTargetHit(PropView& view, const vec3& point){
    view.hits++;
    /*
        Just above where the arrow went in, and IN FRONT OF THE LEVEL, not just of the target.
        The first version sat at z 0.6, which clears a 0.8-deep board - but level blocks are
        BLOCK_DEPTH (3) deep, so a number rising from the target under the one-way platform floated
        straight into the platform and only its foot showed underneath. Past the blocks' near face
        at BLOCK_DEPTH / 2, nothing in the level can be in front of it.

        A stand shows the POINTS for that arrow rather than a running count - the ring is the news.
        A leg scores nothing and shows nothing.
    */
    vec3 popup_at(point.x,point.y + 0.35f,BLOCK_DEPTH * 0.5f + 0.2f);
    if (view.variant == TARGET_STAND){
        int points = StandRingPoints(view,point);
        archery_last_points = points;
        if (points > 0){
            view.score += points;
            archery_score += points;
            SpawnHitPopup(popup_at,points);
        }
        //Every stand hit, with its points - the table decides which deserve a word (nice_shot
        //says only the centre ring does). Inside the tick, like the hit, so it lands with it.
        cues.Signal("stand_hit",CuePayload().Set("points",(float)points));
        debug->Info("Stand %i: %i points (%i on it, %i this level)\n",
                    view.index,points,view.score,archery_score);
    }else{
        SpawnHitPopup(popup_at,view.hits);
    }

    if (stage.GetLevel() == STAGE_LEVEL_RANGE && view.f_floating && !view.f_gravity_restored
        && view.hits >= RANGE_GRAVITY_HITS){
        Physics* p = view.object ? view.object->GetPhysics() : NULL;
        if (p){
            p->SetGravityEnabled(true);
            //Its floating damping off again, so it falls like a board - see FLOAT_DAMPING_MIN.
            p->SetLinearDamping(view.base_linear_damping);
            p->SetAngularDamping(view.base_angular_damping);
            p->WakeUp();
        }
        view.f_gravity_restored = true;
        debug->Info("Floating target %i took hit %i - gravity on\n",view.index,view.hits);
    }
}

void ApplicationArcher::SpawnHitPopup(const vec3& at, int count){
    HitPopup& popup = hit_popups[next_hit_popup];
    next_hit_popup = (next_hit_popup + 1) % HIT_POPUP_SLOTS;
    if (!popup.object){
        return;
    }
    int n = count < 1 ? 1 : (count > HIT_NUMBERS_MAX ? HIT_NUMBERS_MAX : count);
    if (!hit_number_meshes[n]){
        return;         //no glyphs - see BuildHitPopups
    }
    popup.object->SetMesh(hit_number_meshes[n]);
    popup.origin = at;
    popup.spawn_tick = main_scene->GetPhysicsTick();
    popup.f_active = true;
    popup.object->SetPosition(at);
    popup.object->SetScale(vec3(0.001f,0.001f,0.001f));
    popup.object->Show();
}

/*
    Up and out: each popup rises HIT_POPUP_RISE over HIT_POPUP_TICKS, easing off as it goes, pops
    in over its first tenth and shrinks away over its last third - scale rather than fade, because
    the deferred pipeline draws no transparency and a shrinking number reads as leaving just as well.

    The glyph origin is the baseline, and a numeral's ink sits from about 0 to 0.8 above it, so the
    position is lowered by half that at the current scale to keep the number centred on its path
    as it grows and shrinks.
*/
void ApplicationArcher::UpdateHitPopups(){
    uint64_t now = main_scene->GetPhysicsTick();
    for (int i = 0; i < HIT_POPUP_SLOTS; i++){
        HitPopup& popup = hit_popups[i];
        if (!popup.f_active || !popup.object){
            continue;
        }
        float t = (float)(now - popup.spawn_tick) / (float)HIT_POPUP_TICKS;
        if (t >= 1.0f){
            popup.f_active = false;
            popup.object->Hide();
            continue;
        }
        float rise = 1.0f - (1.0f - t) * (1.0f - t);          //ease out
        float size = 1.0f;
        if (t < 0.1f){
            size = t / 0.1f;
        }else if (t > 0.67f){
            size = (1.0f - t) / 0.33f;
        }
        float s = HIT_POPUP_SCALE * (size > 0.001f ? size : 0.001f);
        popup.object->SetScale(vec3(s,s,s));
        popup.object->SetPosition(vec3(popup.origin.x,
                                       popup.origin.y + HIT_POPUP_RISE * rise - 0.4f * s,
                                       popup.origin.z));
    }
}

void ApplicationArcher::ClearHitPopups(){
    for (int i = 0; i < HIT_POPUP_SLOTS; i++){
        hit_popups[i].f_active = false;
        if (hit_popups[i].object){
            hit_popups[i].object->Hide();
        }
    }
}

/*
    The backdrop: one textured quad a long way behind the play plane.

    SIZED FOR THE WIDEST VIEW THE ZOOM CAN PRODUCE, not for the default one. The quad is fixed, so
    zooming out has to reveal more of the picture rather than running off the edge of it into
    black - which is the whole reason a backdrop is a big quad and not a screen-space blit.

    FITTED TO COVER. The image is portrait (1152x1536) and the view is 16:9, so fitting it to
    CONTAIN would put black bars down both sides. Instead it is scaled until its width fills the
    view and the extra height runs off the top and bottom; background_offset_y on the panel is
    what chooses which band of it you see.

    Unlit, casts no shadow, and not pickable: it is a picture, not scenery. Lighting it would mean
    the sun sliding across a painted sky, and at this distance it would be nearly black anyway.

    Survivable if the image is missing - the app has spent five slices against a plain background
    and can spend a sixth.
*/
void ApplicationArcher::BuildBackground(){
    Texture* texture = renderer->LoadTexture(BACKGROUND_ASSET);
    if (!texture){
        debug->Err("No backdrop: could not load %s\n",BACKGROUND_ASSET);
        return;
    }
    Material m = {};
    m.name = "ar_background";
    m.glsl_material.color = vec4(1,1,1,1);
    m.glsl_material.f_unlit = 1;
    m.glsl_material.diffuse_texture = 0;
    m.glsl_material.handle_diffuse = texture->texture_handle;
    m.diff_texture = texture;
    renderer->AddMaterial(m);
    material_background = renderer->FindMaterialIndex(m.name);

    /*
        How big it has to be.

        The view is a frustum, so what it covers at the backdrop's depth grows with the distance
        from the camera to it - and the camera can pull back as far as CAMERA_DISTANCE_MAX. The
        aspect is taken as 16:9 rather than read off the window; a backdrop that is slightly too
        big is invisible, one that is slightly too small is a black edge.
    */
    float far_distance = CAMERA_DISTANCE_MAX + BACKGROUND_DEPTH;
    float view_height = 2.0f * far_distance * tanf(0.5f * 38.0f * ARCHER_DEG2RAD);
    float view_width = view_height * (16.0f / 9.0f);
    float width = view_width * BACKGROUND_COVER;
    background_base = vec2(width,width / BACKGROUND_IMAGE_ASPECT);

    //flip_v TRUE, because a quad's own V starts at the bottom and every image in this engine
    //starts at the top - see the note on MakeQuad. Without it the sky is at her feet.
    Mesh* quad = MakeQuad(1.0f,1.0f,true);
    if (!quad){
        debug->Err("No backdrop: could not build the quad\n");
        return;
    }
    background_object = new Object();
    background_object->name = "background";
    background_object->SetMesh(quad);
    background_object->SetMaterialSlot(0,material_background);
    background_object->SetCastsShadow(false);
    background_object->SetPickability(false);
    background_object->SetPosition(vec3(0.0f,0.0f,-BACKGROUND_DEPTH));
    background_object->SetScale(vec3(background_base.x,background_base.y,1.0f));
    main_scene->AddObject(background_object);
    debug->Info("Backdrop %s at %.0f x %.0f units, %.0f behind the play plane\n",
                BACKGROUND_ASSET,background_base.x,background_base.y,BACKGROUND_DEPTH);
}

//--- The title and loading screen ---------------------------------------------------------------

/*
    An orthographic camera and one unlit quad, as in apps/bomber's CreateTitleScene. RENDER
    THREAD, from the top of Init - it is the first thing on screen, so it is built before anything
    slow.

    With an orthographic camera `zoom` is the half-HEIGHT, so at 1 the view runs y -1..1 and
    x -aspect..aspect, and Renderer::DrawFrame keeps the camera's aspect in step with the window
    by itself. The quad is 1x1 and its SCALE is its size, set by FitTitleQuad - a transform rather
    than a rebuilt mesh, because rebuilding means an upload on every frame of a resize drag.

    Survivable without the image: the material falls back to a dark green, and the loading text
    and the click still work over it.
*/
void ApplicationArcher::CreateTitleScene(){
    title_scene = CreateNewScene("Title Screen");

    Camera* camera = title_scene->camera;
    camera->name = "Title Camera";
    //Down -Z at the quad, which MakeQuad builds facing +Z. Engine forward is -Z.
    camera->SetPosition(vec3(0,0,1));
    camera->SetLookAt(vec3(0,0,0));
    camera->SetupOrthographic((float)renderer->GetViewportWidth(),(float)renderer->GetViewportHeight(),
                              1.0f,0.01f,10.0f);

    Material m = {};
    m.name = "ar_title";
    m.glsl_material.color = vec4(1,1,1,1);
    m.glsl_material.f_unlit = 1;
    Texture* texture = renderer->LoadTexture(TITLE_ASSET);
    if (texture){
        m.glsl_material.diffuse_texture = 0;
        m.glsl_material.handle_diffuse = texture->texture_handle;
        m.diff_texture = texture;
    }else{
        debug->Err("Title screen: could not load %s\n",TITLE_ASSET);
        m.glsl_material.color = vec4(0.05f,0.08f,0.06f,1.0f);
    }
    renderer->AddMaterial(m);

    //flip_v, for the same reason as the backdrop's - see the note on MakeQuad.
    Mesh* quad = MakeQuad(1.0f,1.0f,true);
    if (!quad){
        debug->Err("Title screen: could not build the quad\n");
        return;
    }
    title_quad = new Object();
    title_quad->name = "Title Art";
    title_quad->SetMesh(quad);
    title_quad->SetMaterialSlot(0,renderer->FindMaterialIndex(m.name));
    title_quad->SetCastsShadow(false);
    title_quad->SetPickability(false);
    title_scene->AddObject(title_quad);
    FitTitleQuad(main_window->width,main_window->height);
}

/*
    COVER: the art fills the window at its own aspect and the excess runs off one axis. The view
    is 2 tall, so the art is 2 tall unless the window is wider than it, in which case it grows
    until its width matches the window's instead.
*/
void ApplicationArcher::FitTitleQuad(int w, int h){
    if (!title_quad || (w <= 0) || (h <= 0)){
        return;
    }
    float window_aspect = (float)w / (float)h;
    float height = 2.0f * std::max(1.0f,window_aspect / TITLE_IMAGE_ASPECT);
    title_quad->SetScale(vec3(height * TITLE_IMAGE_ASPECT,height,1.0f));
}

/*
    Starts loading step `index` of `count`. RENDER THREAD, from Init only.

    Logs what the PREVIOUS step cost, so the log answers "where does the load go" without anyone
    having to add timers, then draws a frame showing this one. A NULL label is the call after the
    last step: it logs that step's time and draws nothing, because the next frame is the core
    loop's and shows "click to continue".

    One frame per step, drawn BEFORE the step runs, so a long step shows its own name for as long
    as it takes. There are few enough steps that the frames themselves cost nothing to speak of.
*/
void ApplicationArcher::LoadingStep(int index, int count, const char* label){
    auto now = std::chrono::steady_clock::now();
    if (index > 0){
        debug->Info("Loading: %s took %.0f ms\n",loading_label,
                    std::chrono::duration<float,std::milli>(now - loading_step_start).count());
    }
    loading_step_start = now;
    loading_step = index;
    loading_steps = std::max(count,1);
    loading_label = label ? label : "";
    if (label){
        DrawLoadingFrame();
    }
}

/*
    One frame of the title scene with the loading text over it, from INSIDE Init.

    The core frame loop has not started yet - it starts when Init returns - so this is the same
    sequence Application::DrawFrame runs, minus everything that is not the title: no PreRender
    (the world's view work, half built), no ImGui (its panels read the level being built), and
    only the app's own overlay. It services a resize itself for the same reason, since the core
    loop is not there yet to do it; Init's own Resize lands here first.
*/
void ApplicationArcher::DrawLoadingFrame(){
    if (!title_scene || !renderer || !main_window){
        return;
    }
    if (main_window->f_resized){
        main_window->f_resized = false;
        renderer->Resize(main_window->width,main_window->height);
    }
    FitTitleQuad(main_window->width,main_window->height);
    title_scene->DrawFrame();
    if (overlay){
        overlay->Begin(main_window->width,main_window->height);
        DrawOverlay();
        renderer->BeginGPUPass(Renderer::GPU_PASS_OVERLAY);
        overlay->Draw();
        renderer->EndGPUPass(Renderer::GPU_PASS_OVERLAY);
    }
    renderer->EndGPUFrame();
    main_window->SwapWindowBuffers();
}

#define TITLE_TEXT          UIColor(242,232,204,255)    //the parchment of the painted signs
#define TITLE_TEXT_DIM      UIColor(242,232,204,150)
#define TITLE_BAND          UIColor(  8, 14, 10,150)    //under the text, so busy art cannot eat it
#define TITLE_BAR_BACK      UIColor(242,232,204, 45)
#define TITLE_BAR_FILL      UIColor(150,196, 80,235)    //the moss

/*
    The title's text, low and centred over the art. RENDER THREAD: from DrawLoadingFrame during
    Init, and from the core loop while the title is the live scene.

    While loading: LOADING, a bar, and the name of the step under way. Once loaded: CLICK TO
    CONTINUE, breathing on the title scene's own tick count so it pauses with the simulation like
    every other duration here.

    Sized off the window's height, so the text is the same fraction of the picture at any size.
*/
void ApplicationArcher::DrawOverlay(void){
    if (!overlay || !overlay->IsReady() || !main_window){
        return;
    }
    if (!f_loading && !(title_scene && (main_scene == title_scene))){
        return;
    }
    const float w = (float)main_window->width;
    const float h = (float)main_window->height;
    const float size = clamp(h * 0.042f,16.0f,72.0f);
    const float cx = w * 0.5f;
    const float baseline = h * 0.86f;
    const float bar_w = size * 9.0f;
    const float pad = size * 0.9f;

    if (f_loading){
        const float bar_top = baseline + size * 0.45f;
        const float bar_h = std::max(3.0f,size * 0.16f);
        const float label_size = size * 0.5f;
        const float label_baseline = bar_top + bar_h + label_size * 1.6f;
        overlay->AddRect(vec2(cx - bar_w * 0.5f - pad,baseline - size * 1.1f),
                         vec2(cx + bar_w * 0.5f + pad,label_baseline + label_size * 0.7f),
                         size * 0.4f,TITLE_BAND);
        overlay->AddText("LOADING",vec2(cx,baseline),size,TITLE_TEXT,UI_ALIGN_CENTER);
        float fraction = clamp((float)loading_step / (float)loading_steps,0.0f,1.0f);
        vec2 bar_min(cx - bar_w * 0.5f,bar_top);
        vec2 bar_max(cx + bar_w * 0.5f,bar_top + bar_h);
        overlay->AddRect(bar_min,bar_max,bar_h * 0.5f,TITLE_BAR_BACK);
        if (fraction > 0.0f){
            overlay->AddRect(bar_min,vec2(bar_min.x + bar_w * fraction,bar_max.y),bar_h * 0.5f,TITLE_BAR_FILL);
        }
        overlay->AddText(loading_label,vec2(cx,label_baseline),label_size,TITLE_TEXT_DIM,UI_ALIGN_CENTER);
        return;
    }

    const char* text = "CLICK TO CONTINUE";
    vec2 extent = overlay->MeasureText(text,size);
    overlay->AddRect(vec2(cx - extent.x * 0.5f - pad,baseline - size * 1.1f),
                     vec2(cx + extent.x * 0.5f + pad,baseline + size * 0.6f),
                     size * 0.4f,TITLE_BAND);
    double phase = (double)(title_scene->GetPhysicsTick() % TITLE_PULSE_TICKS) / (double)TITLE_PULSE_TICKS;
    float breath = 0.5f + 0.5f * cosf((float)(phase * 2.0 * 3.14159265358979));
    uint8_t alpha = (uint8_t)(140.0f + 115.0f * breath);
    overlay->AddText(text,vec2(cx,baseline),size,UIColor(242,232,204,alpha),UI_ALIGN_CENTER);
}

/*
    The title's click covers the whole window while the title is up and nothing otherwise. RENDER
    THREAD, from core whenever the window changes size - which includes the first frame after
    Init, with the title already live. UpdateTitle clears it on the way out, since leaving the
    title is not a resize.
*/
void ApplicationArcher::LayoutTouchButtons(int w, int h){
    InputController* input = main_window ? main_window->inputcontroller : NULL;
    if (!input || (title_tap_button < 0)){
        return;
    }
    if (title_scene && (main_scene == title_scene)){
        input->SetTouchButtonRect(title_tap_button,{0.0f,0.0f,(float)w,(float)h});
    }else{
        input->SetTouchButtonRect(title_tap_button,InputController::TouchRect());
    }
}

/*
    PHYSICS THREAD, from UpdateView while the title is the live scene.

    Both edges are READ every pass and ACTED ON only when the input is ours - raw input reports
    keys typed into other programs, and a space typed into an editor should not start the game
    behind it, nor an Escape close it. IsInputLive rather than HasFocus, so archer_hold 'continue'
    gets through on a minimised window the way every other scripted action does.

    Continue goes back to the level the title was entered from, by RequestActiveScene, which lands
    at the top of the next pass. At startup that is the world, stepped once at the end of Init so
    its first frame is not empty.

    Escape or Back exits. By the flag rather than Window::Close, because this is not the thread
    that pumps the window's messages - see RequestQuitAll. The frame loop sees it and shuts down
    the usual way, this thread included.
*/
void ApplicationArcher::UpdateTitle(InputController* input){
    if (!input){
        return;
    }
    bool f_continue = input->WasKeyReleased(INPUT_ARCHER_CONTINUE);
    bool f_exit = input->WasKeyReleased(INPUT_ARCHER_MENU);
    if (!input->IsInputLive()){
        return;
    }
    if (f_exit){
        debug->Info("Exit from the title screen\n");
        Window::RequestQuitAll();
        return;
    }
    if (!f_continue){
        return;
    }
    Scene* back = title_return_scene ? title_return_scene : world_scene;
    RequestActiveScene(back);
    debug->Info("Title screen dismissed, back to '%s'\n",back->name.c_str());
}

/*
    The title's own state, in and out. PHYSICS THREAD from OnActiveSceneChanged, and once from the
    end of Init, whose direct write to main_scene does not go through it.

    The click rect covers the window only while the title is up - LayoutTouchButtons places it on
    a resize, but going to the title is not one. And the panels go down, because they cover the
    art: f_show_ui is saved and put back, on the same thread ServiceUIToggle writes it from, so
    pressing U and this cannot fight.
*/
void ApplicationArcher::EnterTitle(){
    InputController* input = main_window ? main_window->inputcontroller : NULL;
    if (input && (title_tap_button >= 0)){
        input->SetTouchButtonRect(title_tap_button,
                                  {0.0f,0.0f,(float)main_window->width,(float)main_window->height});
    }
    f_title_saved_show_ui = f_show_ui;
    f_show_ui = false;
}

void ApplicationArcher::LeaveTitle(){
    InputController* input = main_window ? main_window->inputcontroller : NULL;
    if (input && (title_tap_button >= 0)){
        input->SetTouchButtonRect(title_tap_button,InputController::TouchRect());
    }
    f_show_ui = f_title_saved_show_ui;
}

void ApplicationArcher::SetupLights(){
    {   //The sun. Its position is moved with the camera every tick (see UpdateCamera) because the
        //level is 84 units wide and one shadow ortho covering all of it would be useless - a
        //shadow map spread that thin has no edges left.
        DirectionalLight* sun = new DirectionalLight();
        sun->name = "Sun";
        //Nearly overhead, leaning left and only a little toward the camera. A side view wants its
        //shadows ON THE GROUND beside things, where they read as contact - a sun raked in from the
        //front throws them backwards, behind the very objects casting them, and the level renders
        //looking flatly unlit for reasons that are nothing to do with the lighting being broken.
        sun->SetPosition(SUN_OFFSET);
        sun->SetLookAt(vec3(0.0f,0.0f,0.0f));
        sun->color = vec3(1.0f,0.96f,0.88f);
        sun->brightness = 4.0f;
        sun->viewport.zoom = SUN_SHADOW_EXTENT;     //PlaceCamera rescales it with the zoom
        main_scene->AddObject(sun);
        sun_light = sun;                //UpdateCamera drags it along every tick
    }
    {   //A cool fill from the front, so faces pointing at the camera are not pure shadow. A second
        //directional light costs one entry in the light SSBO and nothing else.
        DirectionalLight* fill = new DirectionalLight();
        fill->name = "Fill";
        fill->SetPosition(vec3(18.0f,8.0f,26.0f));
        fill->SetLookAt(vec3(0.0f,3.0f,0.0f));
        fill->color = vec3(0.48f,0.62f,1.0f);
        fill->brightness = 1.2f;
        fill->f_casts_shadow = false;
        fill->viewport.zoom = 30.0f;
        main_scene->AddObject(fill);
        fill_light = fill;              //kept so the range scene can share it
    }
}

void ApplicationArcher::SetupCamera(){
    Camera* camera = main_scene->camera;
    camera->SetType(CAMERA_TYPE_PERSPECTIVE);
    /*
        Perspective, not orthographic, and square-on rather than tilted.

        Orthographic is the obvious choice for a side view and is the wrong one here: the whole
        premise is 3D assets in a 2D game, and with no foreshortening at all they render as
        cardboard cut-outs. A narrow perspective from a good way back keeps the read flat enough to
        judge a jump by - which is the thing a platformer camera must never compromise - while
        still giving blocks visible sides and letting the sun put a real edge on every shadow.

        fov is VERTICAL and in degrees. 38 at 26 units away shows about 17.9 units of height, which
        is the jump arc (3.2) with the level's tallest structures and enough sky to see an arrow
        at the top of its lob.
    */
    camera->SetupPerspective(renderer->width,renderer->height,CAMERA_FOV,0.5f,240.0f);
    camera_target = vec3(stage.pos.x,stage.pos.y + 0.5f,0.0f);
    camera_ideal = camera_target;
    camera->SetPosition(vec3(camera_target.x,camera_target.y,CAMERA_DISTANCE));
    camera->SetLookAt(camera_target);
    camera->CalculateLookatMatrix();
}

//--- The other scenes ---------------------------------------------------------------------------

/*
    A test level as a scene of its own - the range (bow_plan.md section 7) and the rope test - see
    the note on ArcherLevel.

    BUILT BY RUNNING THE SAME BUILDERS AGAIN with the level swapped in, rather than by a second set
    of builders. BuildBlocks, BuildProps, BuildArcher and SetupCamera all build into main_scene from
    `stage`, so pointing those two at the new level and calling them is the whole of it - and it
    means a crate, a target or a rope there is built by exactly the code that builds one in the
    main level, which is the property a test level most needs.

    main_scene IS WRITTEN DIRECTLY HERE, which is the one place that is allowed. RequestActiveScene
    exists because the physics and render threads both read main_scene; Init runs before either
    exists, so there is nothing to race. It is put back before returning, and the swap undone, so
    Init's caller finds the main level live exactly as if this had not run.

    NOT BUILT: the terrain (it belongs to the main level's test bay, and BuildTerrain uploads a mesh
    per bay), the character model and bow (shared - see ShareCharacterWith), the lights, the
    backdrop, the input map and the MCP tools, all of which are app-wide. The command handlers ARE
    registered again, because a Scene owns its own handler table and the tools submit to whichever
    scene is live.
*/
Scene* ApplicationArcher::BuildExtraLevel(int level, const char* name){
    Scene* scene = CreateNewScene(name);
    scene->physics_world = new PhysicsWorld();
    scene->physics_world->SetGravity(vec3(0.0f,ARCHER_PROP_GRAVITY,0.0f));
    scene->physics_world->SetDebugRendering(false);

    parked_levels.push_back(ArcherLevel());
    ArcherLevel& parked = parked_levels.back();
    parked.stage.SetLevel(level);
    SwapLevel(parked);          //the members are now the new level's, still empty
    Scene* live = main_scene;
    main_scene = scene;

    BuildBlocks();
    BuildProps();
    BuildSigns();
    BuildScenery();
    //Render thread, like the main level's - see the declaration.
    BuildRopeSkin();
    BuildArcher();
    //BuildArcherModel hides the collider box once there is a model to look at; do the same for
    //this body, or the range has her standing inside a green crate.
    if (archer_object && archer_model){
        archer_object->SetVisibility(f_show_collider);
    }
    ShareCharacterWith(scene);
    SetupCamera();
    RegisterCommandHandlers();

    main_scene = live;
    SwapLevel(parked);
    parked.scene = scene;
    debug->Info("Built the %s scene: %i blocks, %i props\n",name,
                (int)parked.stage.blocks.size(),(int)parked.prop_views.size());
    return scene;
}

/*
    The Objects both scenes draw - see ArcherLevel for why she is one character and not two.

    Only ROOT objects go in: the bow and the nocked arrow are children of hand bones, which are
    children of the model, and a scene draws a child by walking its parent. Adding them as well
    would draw them twice.
*/
void ApplicationArcher::ShareCharacterWith(Scene* scene){
    if (!scene){
        return;
    }
    if (archer_model){
        scene->AddObject(archer_model);
    }
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (arrow_objects[i]){
            scene->AddObject(arrow_objects[i]);
        }
    }
    for (int i = 0; i < AIM_ARC_POINTS; i++){
        if (arc_objects[i]){
            scene->AddObject(arc_objects[i]);
        }
    }
    for (int i = 0; i < ROPE_MARK_COUNT; i++){
        if (rope_marks[i]){
            scene->AddObject(rope_marks[i]);
        }
    }
    for (int i = 0; i < HIT_POPUP_SLOTS; i++){
        if (hit_popups[i].object){
            scene->AddObject(hit_popups[i].object);
        }
    }
    if (background_object){
        scene->AddObject(background_object);
    }
    if (sun_light){
        scene->AddObject(sun_light);
    }
    if (fill_light){
        scene->AddObject(fill_light);
    }
}

/*
    Member for member, the live level against the parked one. See ArcherLevel.

    It does NOT touch parked.scene. That field names the scene whose state is parked, and only the
    caller knows which scene it has just left - so every caller sets it straight after, and
    OnActiveSceneChanged tests it to find the slot to swap with.
*/
void ApplicationArcher::SwapLevel(ArcherLevel& parked){
    std::swap(stage,parked.stage);
    std::swap(archer_object,parked.archer_object);
    std::swap(block_objects,parked.block_objects);
    std::swap(plant_objects,parked.plant_objects);
    std::swap(spring_plant_objects,parked.spring_plant_objects);
    std::swap(balance_bar,parked.balance_bar);
    std::swap(balance_marker,parked.balance_marker);
    std::swap(blockout_group,parked.blockout_group);
    std::swap(prop_views,parked.prop_views);
    std::swap(debris,parked.debris);
    std::swap(terrain_objects,parked.terrain_objects);
    std::swap(terrain_back_objects,parked.terrain_back_objects);
    std::swap(backdrop_reach,parked.backdrop_reach);
    std::swap(melted_blocks,parked.melted_blocks);
    std::swap(rope_segments,parked.rope_segments);
    std::swap(rope_joint,parked.rope_joint);
    std::swap(rope_joints,parked.rope_joints);
    std::swap(rope_anchor_object,parked.rope_anchor_object);
    std::swap(rope_skin,parked.rope_skin);
    std::swap(rope_bones,parked.rope_bones);
    std::swap(rope_skin_cuts,parked.rope_skin_cuts);
    std::swap(rope_seg_len,parked.rope_seg_len);
    std::swap(solver_velocity_iterations,parked.solver_velocity_iterations);
    std::swap(solver_position_iterations,parked.solver_position_iterations);
    std::swap(arrow_stuck,parked.arrow_stuck);
    std::swap(camera_target,parked.camera_target);
    std::swap(camera_ideal,parked.camera_ideal);
    std::swap(orbit_follow_offset,parked.orbit_follow_offset);
}

/*
    The engine has just made `to` the live scene - physics thread, physics_mutex held, before
    anything else this pass has read it (see Application::OnActiveSceneChanged).

    Swaps with the slot holding `to`, which then holds the level just left. Any other switch - to a
    scene this app did not build, or the same scene again - finds no slot and leaves the members
    alone, because swapping them would park the live level under the wrong name.
*/
void ApplicationArcher::OnActiveSceneChanged(Scene* from, Scene* to){
    if (!to){
        return;
    }
    /*
        THE TITLE IS NEVER SWAPPED IN OR OUT. Going to it leaves the live members as they are - they
        are the level just left, frozen, and continue goes back to it. Leaving it is leaving THAT
        level: a switch from the title straight to another one (the Scene panel can) must park the
        live members under the level they belong to, not under the title, or they could never be
        swapped back.
    */
    if (to == title_scene){
        title_return_scene = from;
        EnterTitle();
        return;
    }
    if (from == title_scene){
        LeaveTitle();
        from = title_return_scene ? title_return_scene : world_scene;
    }
    for (size_t i = 0; i < parked_levels.size(); i++){
        if (parked_levels[i].scene == to){
            SwapLevel(parked_levels[i]);
            parked_levels[i].scene = from;      //what is parked there now is the level just left
            RefreshViewAfterSwitch();
            return;
        }
    }
}

/*
    Everything the view reads, re-read from the level that has just become live - WITHOUT a tick.

    A switch while paused would otherwise draw the new scene with the shared character still
    standing where she was in the old one, and report the old level's numbers, until something
    stepped it. This is the view half of a tick and nothing else: it places the model and the
    arrows, moves the camera, and republishes the snapshot, but it does not run the Puppet or the
    rules, so switching scenes never advances either.
*/
void ApplicationArcher::RefreshViewAfterSwitch(){
    if (archer_model){
        archer_model->SetPosition(vec3(stage.pos.x,stage.pos.y - ARCHER_HALF_H - model_foot_offset,0.0f));
    }
    SyncArrowViews();
    spring_cue_plant = -1;      //the cue was the other level's; the next tick works this one's out
    SyncSpringPlants();
    SyncAimArc();
    ClearHitPopups();
    UpdateCamera();
    PublishSnapshot();
}

void ApplicationArcher::SetupInput(){
    InputController* input = main_scene->inputcontroller;

    //Two mappings per direction because muscle memory differs and both cost nothing:
    //KeyState::f_isdown counts HELD MAPPINGS rather than being a boolean, so an action stays down
    //while either of its keys is.
    input->AddKeyMap('A',INPUT_ARCHER_LEFT);
    input->AddKeyMap(VK_LEFT,INPUT_ARCHER_LEFT);
    input->AddKeyMap('D',INPUT_ARCHER_RIGHT);
    input->AddKeyMap(VK_RIGHT,INPUT_ARCHER_RIGHT);
    /*
        And the left stick's X, which is the same action asked for by degree instead of by switch.

        Analog index 0 is the left stick's X on every pad this engine reads. The dead zone is the
        number worth thinking about: the default of 50 out of 32767 is nothing, and a worn stick
        resting off-centre at that threshold would walk her slowly across the level on its own.
        6000 is about 18%, the usual figure, and it is what breakout settled on for the same
        reason. AddGamePadMap also DECLARES the action as a scalar, which is what makes both
        GetAxis and a scripted HoldAxis work on it.
    */
    input->AddGamePadMap(0,INPUT_ARCHER_MOVE,3000);
    //And the right stick's Y (index 3, positive up) for the aim. The fuller 6000 dead zone here
    //because the aim is a rate: a stick resting slightly off-centre would not stand still, it
    //would drift the bow up or down for as long as nobody touched it.
    input->AddGamePadMap(3,INPUT_ARCHER_AIM,6000);

    input->AddKeyMap(GAMEPAD_KEY_A,INPUT_ARCHER_JUMP);
    input->AddKeyMap(GAMEPAD_KEY_B,INPUT_ARCHER_KICK);
    input->AddKeyMap(GAMEPAD_KEY_Y,INPUT_ARCHER_ACTION);
    input->AddKeyMap(GAMEPAD_KEY_X,INPUT_ARCHER_KNEEL);
    input->AddKeyMap(GAMEPAD_KEY_L1,INPUT_ARCHER_DRAW);
    //The shoulders mirror J and L on the keyboard: bow on the left, knife on the right. The knife
    //has no rules yet, so R1 does nothing until that slice lands - see the 'L' mapping below.
    input->AddKeyMap(GAMEPAD_KEY_R1,INPUT_ARCHER_KNIFE);

    //Drop-through is on S alone. Down is the AIM, and one key meaning two things is how a control
    //scheme starts fighting itself - see the layout note in ApplicationArcher.h.
    input->AddKeyMap('S',INPUT_ARCHER_DOWN);

    input->AddKeyMap(VK_SPACE,INPUT_ARCHER_JUMP);
    input->AddKeyMap('J',INPUT_ARCHER_DRAW);
    input->AddKeyMap(VK_UP,INPUT_ARCHER_AIM_UP);
    input->AddKeyMap(VK_DOWN,INPUT_ARCHER_AIM_DOWN);
    input->AddKeyMap('E',INPUT_ARCHER_ACTION);
    //J, K, L in a row: bow, kick, knife. The knife has no rules behind it yet; the mapping is here
    //so the layout is decided once rather than argued about again when that slice lands.
    input->AddKeyMap('K',INPUT_ARCHER_KICK);
    input->AddKeyMap('L',INPUT_ARCHER_KNIFE);
    input->AddKeyMap('C',INPUT_ARCHER_KNEEL);

    //Restart is on Home and Start, well away from everything else. It used to be R, next to E,
    //and one slip off the action key threw the whole level away.
    input->AddKeyMap(VK_HOME,INPUT_ARCHER_RESTART);
    input->AddKeyMap(GAMEPAD_KEY_START,INPUT_ARCHER_RESTART);
    input->AddKeyMap(VK_F1,INPUT_ARCHER_TOGGLE_UI);
    input->AddKeyMap(VK_F2,INPUT_ARCHER_TOGGLE_BLOCKOUT);
    //'P' alongside the default VK_PAUSE, because most keyboards no longer have a Pause key.
    //INPUT_PAUSE is handled by Scene::BeginPass itself, so this is the whole feature.
    input->AddKeyMap('P',INPUT_PAUSE);

    /*
        Input recording is F9 alone, core's default - it used to be on the pad's Back as well, which
        is now the menu below. A dev feature gives way to a button the game needs.

        Escape and Back: the menu - to the title from a level, out of the game from the title. The
        window stops closing itself on Escape, which is core's default for apps without a menu
        (see f_escape_closes_window); exiting is now UpdateTitle's to decide.
    */
    main_window->f_escape_closes_window = false;
    input->AddKeyMap(VK_ESCAPE,INPUT_ARCHER_MENU);
    input->AddKeyMap(GAMEPAD_KEY_BACK,INPUT_ARCHER_MENU);
    //Not recorded: a replay that sent her to the title would stop playing the level it recorded.
    input->SetRecorded(INPUT_ARCHER_MENU,false);
    //T, the teleport. Not recorded either: it goes where the cursor is, which a recording does not
    //keep, so a replayed T would put her somewhere else. A run that uses it replays from after it.
    input->AddKeyMap('T',INPUT_ARCHER_TELEPORT);
    input->SetRecorded(INPUT_ARCHER_TELEPORT,false);

    /*
        The title's continue. Space and A are also the jump, and Start the restart, which is fine:
        the title reads only this action, and each of those edges is spent on the title's own pass
        before the world is live to see it. Not Enter - see INPUT_ARCHER_CONTINUE.

        The click is a touch button, the same way apps/bomber's menu gets the mouse - bound here
        and PLACED in LayoutTouchButtons, which covers the window with it while the title is up
        and nothing otherwise. Never drawn: f_draw_touch_buttons is for on-screen game controls.
    */
    input->AddKeyMap(VK_SPACE,INPUT_ARCHER_CONTINUE);
    input->AddKeyMap(GAMEPAD_KEY_A,INPUT_ARCHER_CONTINUE);
    input->AddKeyMap(GAMEPAD_KEY_START,INPUT_ARCHER_CONTINUE);
    title_tap_button = input->AddTouchButton(InputController::TouchRect(),INPUT_ARCHER_CONTINUE,"continue");
    f_draw_touch_buttons = false;
    //Not recorded: a recording starts in the world, where this action means nothing.
    input->SetRecorded(INPUT_ARCHER_CONTINUE,false);
    //The view toggles are the person's, not the game's - a replay should not flip the panels.
    input->SetRecorded(INPUT_ARCHER_TOGGLE_UI,false);
    input->SetRecorded(INPUT_ARCHER_TOGGLE_BLOCKOUT,false);
    //What a recording file calls each action. The archer_hold tool's names where it has one, so a
    //line in a recording and a tool call say the same thing.
    input->NameAction(INPUT_ARCHER_LEFT,"left");
    input->NameAction(INPUT_ARCHER_RIGHT,"right");
    input->NameAction(INPUT_ARCHER_DOWN,"down");
    input->NameAction(INPUT_ARCHER_JUMP,"jump");
    input->NameAction(INPUT_ARCHER_DRAW,"draw");
    input->NameAction(INPUT_ARCHER_AIM_UP,"up");
    input->NameAction(INPUT_ARCHER_AIM_DOWN,"aim_down");
    input->NameAction(INPUT_ARCHER_ACTION,"action");
    input->NameAction(INPUT_ARCHER_KICK,"kick");
    input->NameAction(INPUT_ARCHER_KNIFE,"knife");
    input->NameAction(INPUT_ARCHER_KNEEL,"kneel");
    input->NameAction(INPUT_ARCHER_RESTART,"restart");
    input->NameAction(INPUT_ARCHER_TOGGLE_UI,"toggle_ui");
    input->NameAction(INPUT_ARCHER_TOGGLE_BLOCKOUT,"toggle_blockout");
    input->NameAction(INPUT_ARCHER_MOVE,"move");
    input->NameAction(INPUT_ARCHER_AIM,"aim");
    input->NameAction(INPUT_ARCHER_CONTINUE,"continue");
    input->NameAction(INPUT_ARCHER_MENU,"menu");
    input->NameAction(INPUT_ARCHER_TELEPORT,"teleport");
}

/*
    Where a recording starts from, written into its `state` line - see
    Application::CaptureRecordingState. PHYSICS THREAD, at a pass boundary.

    Only what RestoreRecordingState below can put back: her position, velocity, facing and aim.
    The rest of the level is not captured, because it cannot be restored piecemeal - a knocked-over
    prop or a swinging rope is rp3d body state. Instead `restart` asks for the level to be rebuilt
    first, which puts every prop, target and the rope back at rest where the level builds them. So
    a recording is most faithful when it starts with the level undisturbed around her, and a
    person who wants the world as they left it can set `restart` false in the file.

    Starting a recording while she is on the rope or hanging off a ledge is allowed and records
    fine, but replays from standing (or falling) at that spot - those modes are not restorable.
*/
json ApplicationArcher::CaptureRecordingState(){
    return json{
        {"restart",true},
        {"x",stage.pos.x},
        {"y",stage.pos.y},
        {"vx",stage.vel.x},
        {"vy",stage.vel.y},
        {"facing",stage.facing},
        {"aim_deg",stage.aim_deg},
        //Standing, as opposed to anything else. It matters more than it looks: placed in the air,
        //her first tick of input is air control rather than ground acceleration, and a recording
        //that starts with a step replayed 0.03 units short of the original.
        {"on_ground",stage.mode == MODE_GROUND && stage.f_on_ground},
        {"coyote_ticks",stage.coyote_ticks},
        //How long the level had been running: the seed for anything random in it - the debris a
        //broken wall throws, the kick's shout. A replay restarts the level, which zeroes this.
        {"level_ticks",stage.ticks},
        //What the cues remember past a tick - each one's last pick and firing, each group's last
        //line - so a replay avoids the same repeats and honours the same gaps the original did.
        {"cue_history",cues.CaptureHistory()},
        //The spring she stands on, and every spring plant's state: a recording that starts on the
        //pad mid-bounce has to replay from that bounce, not from a cap at rest.
        {"spring_on",stage.spring_on},
        {"spring_plants",[&](){
            json list = json::array();
            for (const StageSpringPlant& p : stage.spring_plants){
                list.push_back(json::array({p.q,p.qd}));
            }
            return list;
        }()}
    };
}

void ApplicationArcher::RestoreRecordingState(const json& state){
    //Off the rope first, whatever else happens: the joint belongs to a body NewGame is about to
    //rebuild, and a placement with the joint still in place would be dragged straight back.
    if (stage.mode == MODE_ROPE){
        DetachArcherFromRope(false);
    }
    if (state.value("restart",true)){
        NewGame();
    }
    //Back to where the level's clock stood, so the random bits come out as they did. A file from
    //before this was written has none, and replays with the restart's 0 as it always did.
    if (state.contains("level_ticks")){
        stage.ticks = state.value("level_ticks",(uint64_t)0);
    }
    //A file from before the cues has none, and replays as a fresh session would - the same for
    //every replay of it, which is all a comparison needs.
    auto history = state.find("cue_history");
    cues.RestoreHistory(history != state.end() ? *history : json());
    PlaceArcher(v2(state.value("x",stage.pos.x),state.value("y",stage.pos.y)));
    stage.vel = v2(state.value("vx",0.0f),state.value("vy",0.0f));
    stage.facing = (state.value("facing",stage.facing) < 0.0f) ? -1.0f : 1.0f;
    if (state.contains("aim_deg")){
        stage.aim_deg = clamp(state.value("aim_deg",stage.aim_deg),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
    }
    if (state.value("on_ground",false)){
        stage.mode = MODE_GROUND;
        stage.f_on_ground = true;
        stage.coyote_ticks = state.value("coyote_ticks",0);
        stage.spring_on = state.value("spring_on",-1);
        if (stage.spring_on >= (int)stage.spring_plants.size()){
            stage.spring_on = -1;
        }
    }
    if (state.contains("spring_plants") && state["spring_plants"].size() == stage.spring_plants.size()){
        for (size_t i = 0; i < stage.spring_plants.size(); i++){
            StageSpringPlant& p = stage.spring_plants[i];
            p.q = p.prev_q = state["spring_plants"][i][0].get<float>();
            p.qd = state["spring_plants"][i][1].get<float>();
        }
        SyncSpringPlants();
    }
}

//ARCHER_CMD_PLACE's body, shared with RestoreRecordingState. Physics thread.
void ApplicationArcher::PlaceArcher(v2 pos){
    stage.pos = pos;
    stage.vel = v2(0.0f,0.0f);
    stage.mode = MODE_AIR;      //which is also what ends a get-up early
    stage.getup_ticks = 0;
    stage.hang_block = -1;
    stage.hang_branch = -1;
    stage.climb_ticks = 0;
    stage.grab_cooldown = 0;
    stage.f_on_ground = false;
    stage.coyote_ticks = 0;
    stage.buffer_ticks = 0;
    stage.spring_on = -1;       //or the plant she was on would go on carrying her weight
    stage.launch_lift = 0.0f;
    stage.bow_mode = BOW_IDLE;
    stage.draw_ticks = 0;
}

/*
    Where the mouse cursor meets the play plane (z = 0), her feet there - lifted clear of anything
    solid. PHYSICS THREAD, from UpdateView; the placing itself is ARCHER_CMD_PLACE, the same command
    archer_place sends, so the key and the tool cannot come to mean different things.

    LIFTED, because archer_place's warning is the whole problem with a cursor: a point inside a
    block ejects her on the next tick, and out of a tall one that is upward onto its roof - which
    looks like the teleport having worked and then her walking over what should have stopped her.
    So a body that would overlap a block is stood on that block's top instead, and again for
    whatever that puts her into. One-way platforms do not count: she can stand inside one, and
    pressing T just under one's top would otherwise pop her up onto it every time. A tree's arms
    are the same.

    In the air is fine - she falls from there, which is usually what a test wants.
*/
void ApplicationArcher::TeleportToCursor(InputController* input){
    if (!input || !main_scene || !main_scene->camera){
        return;
    }
    int2 px = input->GetRelativeMousePosition();
    ray r = main_scene->camera->GetPixelRay(px);
    plane play;
    play.pos = vec3(0.0f,0.0f,0.0f);
    play.normal = vec3(0.0f,0.0f,1.0f);
    vec3 at;
    if (!r.intersects_plane(play,at)){
        return;
    }
    float x = at.x;
    float feet = at.y;
    //A handful of passes is enough for any stack in these levels; the cap is only so a malformed
    //level cannot hang the physics thread.
    for (int pass = 0; pass < 8; pass++){
        bool f_moved = false;
        for (const StageBlock& b : stage.blocks){
            //A tree's arms are one-way platforms to the rules whatever their kind says.
            if (!b.f_alive || b.kind == BLOCK_PLATFORM || b.tree >= 0){
                continue;
            }
            bool f_overlap = (x + ARCHER_HALF_W > b.Left()) && (x - ARCHER_HALF_W < b.Right()) &&
                             (feet + 2.0f * ARCHER_HALF_H > b.Bottom()) && (feet < b.Top());
            if (f_overlap){
                feet = b.Top();
                f_moved = true;
            }
        }
        if (!f_moved){
            break;
        }
    }
    SimCommand cmd;
    cmd.type = ARCHER_CMD_PLACE;
    //A hair above whatever she stands on, so the first tick lands her rather than starting inside.
    cmd.value[0] = x;
    cmd.value[1] = feet + ARCHER_HALF_H + 0.01f;
    main_scene->SubmitCommand(cmd);
    debug->Info("Teleport (T) to (%.2f, %.2f)\n",x,feet);
}

void ApplicationArcher::RegisterCommandHandlers(){
    //Restarting throws the game away, which is only safe at the top of a tick on the physics
    //thread - which is exactly where a command handler runs.
    main_scene->RegisterCommandHandler(ARCHER_CMD_RESTART,
        [this](const SimCommand& cmd) -> objectid_t {
            (void)cmd;
            NewGame();
            return OBJECTID_INVALID;
        });

    /*
        Set the aim angle outright, in value[0], in degrees.

        A player tilts the aim over time with Up/Down and a script could do the same with HoldKey -
        but a script trying to land an arrow on a target wants to SOLVE for the angle, and
        bisecting on "hold up for 7 ticks" is a much worse instrument than setting 41.5 degrees and
        looking at where the arc says it lands. This is a measuring tool, and it is on the command
        queue rather than being a setter because the caller is on the wrong thread.
    */
    main_scene->RegisterCommandHandler(ARCHER_CMD_AIM,
        [this](const SimCommand& cmd) -> objectid_t {
            stage.aim_deg = clamp(cmd.value[0],BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
            return OBJECTID_INVALID;
        });

    /*
        Put the archer somewhere, in value[0]/value[1].

        A DEVELOPMENT TOOL and unapologetically so: the level is 84 units long with two gaps in it,
        and iterating on the ledge at x 44 should not require flying the whole approach by script
        every time - a run that spends thirty seconds getting there and then falls in a pit has
        measured nothing. It goes through the command queue like everything else, so it lands at a
        known point in a known tick and would be recorded by a replay rather than corrupting one.

        It clears the movement state as well as the position, which is the part worth stating: a
        teleport that keeps the old velocity, hang and climb-timer drops the archer into the new
        spot still hanging off a ledge that is now somewhere else entirely.
    */
    main_scene->RegisterCommandHandler(ARCHER_CMD_CAMERA,
        [this](const SimCommand& cmd) -> objectid_t {
            camera_mode = (cmd.subtype == ARCHER_CAM_ORBIT) ? ARCHER_CAM_ORBIT : ARCHER_CAM_SIDE;
            return OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(ARCHER_CMD_SOUND_VOLUME,
        [this](const SimCommand& cmd) -> objectid_t {
            sound_volume = clamp(cmd.value[0],0.0f,1.0f);
            return OBJECTID_INVALID;
        });

    main_scene->RegisterCommandHandler(ARCHER_CMD_PLACE,
        [this](const SimCommand& cmd) -> objectid_t {
            //Off the rope first, as RestoreRecordingState does: placed with the joint still in
            //place, she is dragged straight back to where she was hanging.
            if (stage.mode == MODE_ROPE){
                DetachArcherFromRope(false);
            }
            PlaceArcher(v2(cmd.value[0],cmd.value[1]));
            return OBJECTID_INVALID;
        });

    /*
        Hand the animation to the panel, to a single clip, or back to the game.

        The parameters it sets are the SAME struct the rules fill - see the note at the top of
        Puppet.h. That is what makes this a measuring tool rather than a debug hack: a walk cycle
        checked at 4 units a second here is the walk cycle the game plays at 4 units a second.
    */
    main_scene->RegisterCommandHandler(ARCHER_CMD_ANIM,
        [this](const SimCommand& cmd) -> objectid_t {
            int source = (int)cmd.subtype;
            if (source < ANIM_FROM_GAME || source > ANIM_FROM_CLIP){
                source = ANIM_FROM_GAME;
            }
            anim_source = source;
            if (source == ANIM_FROM_CLIP){
                int clip = (int)cmd.value[0];
                if (clip >= 0 && clip < CLIP_COUNT){
                    preview_clip = clip;
                }
                preview_rate = cmd.value[1];
            }else if (source == ANIM_FROM_PANEL){
                panel_params.speed = cmd.value[2];
                panel_params.ground_speed = fabsf(cmd.value[2]);
                //Facing follows the sign of the requested speed, because asking for "-4" and then
                //having to set the facing separately is two ways to say one thing, and the two
                //disagreeing is the state that produces a moonwalk.
                panel_params.facing = (cmd.value[2] < 0.0f) ? -1.0f : 1.0f;
                panel_params.f_on_ground = (cmd.value[3] > 0.5f);
                panel_params.mode = panel_params.f_on_ground ? MODE_GROUND : MODE_AIR;
            }
            //Whatever was playing is no longer necessarily right, so let the next tick decide
            //rather than leaving a preview clip running under the game's control.
            playing_clip = -1;
            return OBJECTID_INVALID;
        });
}

void ApplicationArcher::NewGame(){
    stage.Reset();
    /*
        The level is the outermost scope, so a restart ends every scope the cues had open - the
        creak is cut, a kick's planned shout is dropped - by each cue's own rule. The edges the
        scopes were opened on start again from nothing, since the Stage they were read off is new.
        History is kept: a restart is not a new session.
    */
    cues.Reset();
    f_was_nocked = false;
    f_was_kicking = false;
    f_step_valid = false;
    shake_trauma = 0.0f;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        arrow_in_flight[i] = false;
    }
    //The stands are rebuilt below with nothing in them, so the points they held go with them.
    archery_score = 0;
    archery_last_points = -1;
    kick_score = 0;
    //A restart is a level entry too - see the note in Init. The blocks exist again by now, so she
    //is laid on the real floor.
    if (f_level_entry_getup){
        stage.StartGetUp();
    }
    //The level geometry is rebuilt from Stage every time, rather than being reset in place. It is
    //a few dozen boxes once per restart, and it means a change to BuildLevel cannot leave stale
    //geometry behind - which the prop bodies, with their accumulated velocities and tip-overs,
    //absolutely would.
    for (size_t i = 0; i < block_objects.size(); i++){
        if (block_objects[i]){
            block_objects[i]->Destroy();
        }
    }
    //And the plants, which BuildBlocks makes again from the new Stage. Missing this left one more
    //copy of every trunk standing in the level per restart.
    for (size_t i = 0; i < plant_objects.size(); i++){
        if (plant_objects[i]){
            plant_objects[i]->Destroy();
        }
    }
    plant_objects.clear();
    spring_plant_objects.clear();
    balance_bar = NULL;
    balance_marker = NULL;
    /*
        THE TERRAIN DELIBERATELY SURVIVES A RESTART, and this is not an oversight.

        Two reasons, and the second one is a hard constraint rather than a preference:

          - It cannot have changed. The terrain is a pure function of Stage::blocks, and
            Stage::Reset rebuilds those from BuildLevel plus whatever layout the last
            RegenerateTerrain kept - which is exactly what the terrain was last meshed from - so
            remeshing would produce the same vertices at some cost.
          - THIS FUNCTION RUNS ON THE PHYSICS THREAD. BuildTerrainMesh ends in Mesh::SetMeshData,
            which calls glNamedBufferData immediately, and the physics thread may not touch GL -
            see the thread note in core/MarchingCubes.h. A restart that remeshed here would be a
            crash in the driver with a stack trace pointing at the wrong thing entirely.

        What DOES have to happen is re-hiding: BuildBlocks below has just made a fresh set of
        block objects, and every one of them starts visible.
    */
    //BEFORE the props go: an arrow still holding one would be following a deleted object on the
    //next tick. See ReleaseStuckArrows.
    ReleaseStuckArrows(NULL);
    //The straw men's joints hold raw pointers to their bodies, so they go first - Destroy only
    //marks, but DeleteDestroyedObjects below is where the bodies really leave.
    DestroySprings();
    for (size_t i = 0; i < prop_views.size(); i++){
        if (prop_views[i].object){
            prop_views[i].object->Destroy();
        }
    }
    //And the rubble, or a restart leaves the last run's broken wall lying in the new one.
    for (size_t i = 0; i < debris.size(); i++){
        if (debris[i].object){
            debris[i].object->Destroy();
        }
    }
    debris.clear();
    //The rope's joints have to go before its bodies do, and both before BuildProps makes new ones.
    DestroyRope();
    main_scene->DeleteDestroyedObjects();
    BuildBlocks();
    //The blocks are new objects and start visible; put the melted ones back under the terrain.
    //No GL in here, unlike BuildTerrain - see the note above.
    ApplyBlockoutVisibility();
    BuildProps();

    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (p){
        p->SetBodyWorldPosition(vec3(stage.pos.x,stage.pos.y,0.0f));
        p->SetVelocity(vec3());
    }
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (arrow_objects[i]){
            arrow_objects[i]->SetVisibility(false);
        }
    }
    debug->Info("New game\n");
}

//--- Chrome -------------------------------------------------------------------------------------

void ApplicationArcher::UpdateView(void){
    if (!main_scene){
        return;
    }
#ifdef USE_SOUND
    /*
        Sound keeps SIMULATED time: held on a paused pass that does not tick, let go on one that
        does - so stepping plays one tick of every sound per step and a swoosh still peaks on the
        step its arrow strikes. UpdateView runs on every pass and after the tick, and a paused loop
        still passes once a tick-length, so each step lets out about one tick of sound. See
        SoundSystem::SetPaused.

        And held for the whole time the title is up: a level left by Escape is frozen behind it, so
        its sounds are too, and a bow's creak picks up where it stopped on the way back.
    */
    if (soundsystem){
        soundsystem->SetPaused((main_scene == title_scene) ||
                               (main_scene->IsPhysicsPaused() && !main_scene->IsTickingThisPass()));
        //The panel's volume is the master bus, so it turns down what is already playing too. On
        //every pass, so the slider answers while paused; only when it moved.
        if (soundsystem->GetBusGain(SOUND_BUS_MASTER) != sound_volume){
            soundsystem->SetBusGain(SOUND_BUS_MASTER,sound_volume);
        }
    }
#endif
    //Here rather than in the tick, so a table saved while the game is paused is picked up too.
    PollCueTable();
    InputController* input = main_scene->inputcontroller;
    //The title screen waits for its click and nothing else: no camera, no picking, no toggles.
    if (main_scene == title_scene){
        UpdateTitle(input);
        return;
    }
    /*
        Escape, or Back: to the title, as a pause. Read every pass, acted on only when the input is
        ours - IsInputLive, as for continue, so archer_hold 'menu' works on a minimised window.
        The rest of the way in is OnActiveSceneChanged's, when the switch lands next pass.
    */
    bool f_menu = input->WasKeyReleased(INPUT_ARCHER_MENU);
    if (f_menu && input->IsInputLive() && title_scene){
        RequestActiveScene(title_scene);
        debug->Info("To the title screen from '%s'\n",main_scene->name.c_str());
    }
    /*
        T: to the cursor. HasFocus rather than IsInputLive, because it is cursor-driven - the rule
        for those, see the note on IsInputLive - and no scripted hold means it; archer_place is the
        scripted form. Not while a panel has the keyboard, or typing a T into a text field would
        throw her across the level.
    */
    bool f_teleport = input->WasKeyPressed(INPUT_ARCHER_TELEPORT);
    if (f_teleport && input->HasFocus() && !UIWantsKeyboard()){
        TeleportToCursor(input);
    }
    /*
        F1 the engine panels, F2 the blockout back on top of the terrain.

        Both edges are READ every pass and ACTED ON only while this window has focus. Raw input
        reports keys typed into other programs (RIDEV_INPUTSINK), so ungated, an F1 pressed in an
        editor toggled this app's panels behind it - and with the app started --minimized for an
        agent, nobody would even see that it had happened. HasFocus rather than IsInputLive,
        because these are the person's own view toggles and no scripted hold ever means them;
        reading the edge regardless is what stops a press made elsewhere firing when focus returns.
        They are still not gated on anim_source, which is the other thing IsInputLive-style gates
        in GatherInput do: a debug view is the engine's, not the game's.
    */
    bool f_toggle_ui = input->WasKeyReleased(INPUT_ARCHER_TOGGLE_UI);
    bool f_toggle_blockout = input->WasKeyReleased(INPUT_ARCHER_TOGGLE_BLOCKOUT);
    if (f_toggle_ui && input->HasFocus()){
        f_show_engine_ui = !f_show_engine_ui;
        f_show_scene_window = f_show_engine_ui;
        f_show_inspector_window = f_show_engine_ui;
        f_show_engine_window = f_show_engine_ui;
    }
    if (f_toggle_blockout && input->HasFocus()){
        SetBlockoutVisible(!f_show_blockout);
    }

    /*
        Hover and click-to-select, which is what puts something in the Inspector.

        Not on by default: an app that never calls CheckObjectSelection has no selection and no
        error saying so. Here, above the orbit's early return, so it works in both camera modes.
        Focus-gated because the left-button release edge fires for a click NEXT TO the window too,
        and would clear the selection whenever the person clicked in another program. The panels
        need no gate here - CheckObjectSelection already stands down while ImGui wants the mouse.
        Nothing in the game itself uses the left button, so selecting cannot also fire an action.
    */
    if (input->HasFocus()){
        CheckObjectSelection();
    }

    /*
        The wheel pulls the camera in and out.

        DRAINED EVERY PASS whether or not it will be acted on, because GetDelta is an accumulator:
        skipping the read while the window is in the background does not discard those notches, it
        saves them up and applies the lot in one jump when focus comes back.

        Gated on HasFocus() rather than IsInputLive(), which is the rule for anything cursor-driven
        - see the long note on IsInputLive. A scripted hold is an ACTION arriving from somewhere
        else; a wheel notch is a hand on a mouse that is, by definition, over this window.

        Multiplicative, so a notch moves the camera by a tenth of wherever it already is: the same
        gesture is a small nudge up close and a big sweep far out, which is what a zoom should feel
        like. Placing the camera here rather than leaving it to the next tick is what makes it work
        while the simulation is paused.
    */
    int wheel = input->GetDelta(INPUT_MOUSE_WHEEL);
    /*
        The orbit's mouse movement, drained for the same reason and on EVERY pass, in either mode.
        InputController only clears a delta that was read, so reading it only while the middle
        button is down - or only in orbit mode - lets movement pile up in between, and the first
        frame of a drag applies all of it at once. Raw deltas rather than cursor position, because
        they keep coming with the pointer against the edge of the screen.
    */
    int orbit_dx = input->GetDelta(INPUT_MOUSE_DELTA_X);
    int orbit_dy = input->GetDelta(INPUT_MOUSE_DELTA_Y);

    if (camera_mode == ARCHER_CAM_ORBIT){
        UpdateOrbitCamera(orbit_dx,orbit_dy,wheel);
        return;
    }

    //And not while the pointer is over a panel, or scrolling the clip list also flies the camera
    //across the level. UIWantsMouse is ImGui's own answer behind a name that exists in every
    //build, so this needs no #ifdef - see the note on it in core/Application.h.
    if (wheel != 0 && input->HasFocus() && !UIWantsMouse()){
        camera_distance *= powf(1.0f - CAMERA_ZOOM_PER_NOTCH,(float)wheel);
        camera_distance = clamp(camera_distance,CAMERA_DISTANCE_MIN,CAMERA_DISTANCE_MAX);
    }
    /*
        Placed every pass rather than only on a notch, so that the distance slider and switching
        back from the orbit both show while the simulation is paused - neither goes through the
        wheel, and the tick that would otherwise place the camera is not running.
    */
    PlaceCamera();
}

//--- The tick -----------------------------------------------------------------------------------

void ApplicationArcher::RunSimulationTick(void){
    //Nothing of the level runs under the title - the rules below are the world's, and `stage` is
    //the world's the whole time the title is up.
    if (!main_scene || (main_scene == title_scene)){
        return;
    }
    InputController* input = main_scene->inputcontroller;

    //Restart is read whether or not anything else is happening. Already on the physics thread
    //inside the tick, so this is a direct call rather than a command - a command would be a round
    //trip through the queue to arrive back here one tick later.
    //
    //ACTED ON only when the input is ours, though, for the reason in GatherInput: raw input sees
    //keys typed into other programs, and an 'r' typed anywhere on the machine threw the level
    //away. That is the worst of these for an agent's run, because it silently resets every number
    //the run was measuring. Read regardless, so a press made elsewhere does not fire later.
    bool f_restart = input->WasKeyReleased(INPUT_ARCHER_RESTART);
    if (f_restart && input->IsInputLive()){
        NewGame();
        return;
    }

    ArcherInput intent;
    GatherInput(intent);

    //The props as boxes and the rope as points, BEFORE the tick - the archer is about to be
    //resolved against the one and offered the other.
    RefreshObstacles();
    RefreshRopePoints();
    //The drawn rope follows the same link positions the rules were just handed.
    UpdateRopeSkin();
    CheckRopeSkinCuts();
    MeasureRopeStretch();
    //And while the solver is the one moving the archer, its answer is the truth: read it back
    //before the rules run on it.
    if (stage.mode == MODE_ROPE){
        SyncArcherFromRope();
    }

    StageEvents events;
    stage.Tick(intent,events);

    HandleEvents(events);
    SignalCues(events);
    //The rope handoff, in both directions. Immediately after the tick that decided it, so the
    //joint exists (or is gone) before anything else this tick reads the body.
    if (events.f_grabbed_rope){
        AttachArcherToRope(events.grabbed_rope_id);
    }
    if (events.f_released_rope){
        DetachArcherFromRope(events.f_rope_jump);
    }
    if (stage.mode == MODE_ROPE){
        PumpRope(intent.move_axis);
        //Climbing moved the grip: make the joint again at the new distance down the rope.
        if (rope_joint && stage.rope_s != rope_grip_s){
            ReanchorRopeJoint();
        }
    }
    /*
        Where she will come down, for the animation to meet it (Stage::PredictLanding). After the
        rope handoff, which is where a release gets its jump boost, so the forecast starts from the
        velocity she will actually fly with.
    */
    {
        auto t0 = std::chrono::steady_clock::now();
        landing_forecast = stage.PredictLanding(intent);
        landing_forecast_us = (float)std::chrono::duration<double,std::micro>(
                                  std::chrono::steady_clock::now() - t0).count();
    }
    /*
        The spring plants' timing cue - see spring_cue. Off below a boost of 1 u/s: a cap she has
        settled on still quivers, and a cue that flashed red at a millimetre of rebound would teach
        nothing.
    */
    spring_cue_plant = -1;
    spring_cue = 0.0f;
    spring_cue_peak = 0.0f;
    if (stage.SpringBoostActive()){
        spring_cue_peak = stage.PredictSpringBoostPeak(intent);
        if (spring_cue_peak > 1.0f){
            spring_cue_plant = (stage.f_on_ground && stage.spring_on >= 0) ? stage.spring_on : stage.spring_left;
            spring_cue = stage.SpringBoostNow() / spring_cue_peak;
        }
    }
    ApplyPushes(events);
    ApplyKicks(events);
    BreakBlocks(events);
    UpdateDebris();
    ResolveArrowsAgainstProps();
    //After everything that pushes a straw man this tick, before the step - all of it is force, and
    //rp3d sums the lot.
    TickSprings();
    ForecastArrowImpacts();
    SignalFootsteps();
    /*
        THE CUES FIRE HERE, once, after everything that can signal has - the rules' events, the
        props' hits and scores, the arrows' forecasts - so what the log says happened in a tick,
        and in which order, does not depend on which part of the game found it first. The clock is
        the LEVEL's, which a replay restores, so a replay decides exactly what the original did.
    */
    cues.SetListener(stage.pos.x);
    //The shake decays BEFORE the cues fire, so a shake added this tick starts at its full strength.
    shake_trauma = fmaxf(0.0f,shake_trauma - 1.0f / fmaxf(camera_tuning.shake_ticks,1.0f));
    cues.Tick(stage.ticks);
    UpdateHitPopups();
    DriveArcherBody();
    SyncArcherView();
    SyncArcherAnimation();
    SyncBow();
    UpdateRopeAttachMarkers();
    SyncArrowViews();
    SyncSpringPlants();
    SyncBalanceGauge();
    SyncAimArc();
    UpdateTargets();
    ReapFallenProps();
    UpdateCamera();
    //After the camera, so the leaves are kept in the view this tick will draw.
    StepWindLeaves();
    PublishSnapshot();
}

void ApplicationArcher::GatherInput(ArcherInput& out){
    InputController* input = main_scene->inputcontroller;

    /*
        THE EDGES ARE READ UNCONDITIONALLY, and only then is the result thrown away if the input is
        not ours to act on.

        WasKeyPressed/WasKeyReleased are one-shot flags that a read consumes. Returning early
        without reading them does not discard them - it DEFERS them, so a key released while the
        window was in the background fires on the tick focus comes back, which for the draw key
        means an arrow loosing itself several seconds after the player let go. Raw Input reports
        key-up unfocused (RIDEV_INPUTSINK), so this is not a hypothetical.
    */
    bool f_jump_pressed  = input->WasKeyPressed(INPUT_ARCHER_JUMP);
    bool f_draw_released = input->WasKeyReleased(INPUT_ARCHER_DRAW);
    bool f_action        = input->WasKeyPressed(INPUT_ARCHER_ACTION);
    bool f_kick          = input->WasKeyPressed(INPUT_ARCHER_KICK);
    bool f_kneel         = input->WasKeyPressed(INPUT_ARCHER_KNEEL);

    //Act on input only when it is ours to act on: this window in front, or a scripted hold running
    //(which is not OS input, and happens precisely when the window is NOT in front). One predicate
    //owned by the engine - see InputController::IsInputLive.
    if (!input->IsInputLive()){
        out = ArcherInput();
        return;
    }

    /*
        And while the animation is being driven by hand, she takes no orders from the keyboard.

        The same reasoning as the focus check above and for the same reason it is HERE rather than
        earlier: the edges have already been read and discarded, so leaving puppet mode does not
        fire a jump that was pressed while the panel had the controls. The game keeps ticking -
        the props still fall, the arrows still fly - she simply stands where she was left.
    */
    if (anim_source != ANIM_FROM_GAME){
        out = ArcherInput();
        return;
    }

    float move = 0.0f;
    if (input->IsKeyDown(INPUT_ARCHER_LEFT)){   move -= 1.0f; }
    if (input->IsKeyDown(INPUT_ARCHER_RIGHT)){  move += 1.0f; }
    /*
        One read for the stick, whoever is deflecting it - a thumb and a scripted HoldAxis both
        land in the same KeyState, so this does not have to know which. ADDED to the keys rather
        than replacing them, and then clamped: a key still means "all of it", the stick means
        "this much of it", and holding both cannot ask for more than full.
    */
    move += input->GetAxis(INPUT_ARCHER_MOVE);
    out.move_axis = clamp(move,-1.0f,1.0f);

    float aim = 0.0f;
    if (input->IsKeyDown(INPUT_ARCHER_AIM_UP)){   aim += 1.0f; }
    if (input->IsKeyDown(INPUT_ARCHER_AIM_DOWN)){ aim -= 1.0f; }
    //The stick, squared with its sign kept. Linear, the first third of the throw already tilts at
    //a third of full rate, and lining up a long shot means nudging the stick and letting go; the
    //square gives half a throw a quarter of the rate, which is where the fine control is wanted,
    //and still reaches the keys' full rate at the end of the throw.
    float aim_stick = input->GetAxis(INPUT_ARCHER_AIM);
    aim += aim_stick * fabsf(aim_stick);
    out.aim_axis = clamp(aim,-1.0f,1.0f);

    out.f_jump_down     = input->IsKeyDown(INPUT_ARCHER_JUMP);
    out.f_jump_pressed  = f_jump_pressed;
    out.f_draw_down     = input->IsKeyDown(INPUT_ARCHER_DRAW);
    out.f_draw_released = f_draw_released;
    out.f_down_held     = input->IsKeyDown(INPUT_ARCHER_DOWN);
    out.f_action_pressed = f_action;
    out.f_kick_pressed  = f_kick;
    out.f_kneel_pressed = f_kneel;
}

//--- Sound --------------------------------------------------------------------------------------

/*
    The sound device, the cue table and the output between them.

    WHAT THE GAME SOUNDS LIKE IS NOT IN THIS FILE. It is assets/cues/archer.json, which says for
    each event what plays, how loud, when and what stops it (cue_plan.md; the format is in
    core/CueSystem.h). This file only reports what happened - SignalCues and friends.

    The cues DECIDE in every build and every situation, and log what they decided: with sound, the
    table's sounds are registered and played through a CueSoundOutput; with USE_SOUND=0 or with
    no device, the cues get no output at all, and still decide and log exactly the same - which is
    what lets a replay be checked on a machine that makes no noise.
*/
void ApplicationArcher::SetupSound(){
#ifdef USE_SOUND
    soundsystem = new SoundSystem();
    soundsystem->Initialise();
    //Only a working device gets an output. A table checked against a device that has nothing
    //registered would refuse every sound in it, and the cues would not even decide.
    if (soundsystem->f_initialised){
        cue_output = new CueSoundOutput(soundsystem);
    }
#endif
    cues.Init(ARCHER_TPS,cue_output);
    /*
        The table's non-sound actions. Each is scaled by the cue's gain, which is the table's
        gain_by curves - so how hard a landing shakes is written beside how loud it sounds.
          shake   amount (trauma added, 0..1), axes [across, up]
          rumble  low, high (the heavy and light motor, 0..1)
        Physics thread, inside cues.Tick, like everything else a cue does.
    */
    //Read by type, never with json::value: in this build a wrong type there is an abort, and a
    //typo in a table being tuned must not be able to take the game down.
    auto num = [](const json& o, const char* key, float fallback) -> float {
        auto it = o.find(key);
        return (it != o.end() && it->is_number()) ? it->get<float>() : fallback;
    };
    cues.SetActionHandler("shake",[this,num](const CueAction& a){
        float ax = 0.5f, ay = 1.0f;
        auto axes = a.params.find("axes");
        if (axes != a.params.end() && axes->is_array() && axes->size() == 2 &&
            (*axes)[0].is_number() && (*axes)[1].is_number()){
            ax = (*axes)[0].get<float>();
            ay = (*axes)[1].get<float>();
        }
        AddShake(num(a.params,"amount",0.3f) * a.gain,ax,ay);
    });
    cues.SetActionHandler("rumble",[this,num](const CueAction& a){
        Rumble(num(a.params,"low",0.0f) * a.gain,num(a.params,"high",0.0f) * a.gain);
    });
    std::string error;
    if (!cues.LoadTable(ARCHER_CUE_TABLE,error)){
        debug->Err("Cue table: %s - the game will be silent until it loads\n",error.c_str());
    }
    //For the poll. A packed build has no file to watch, and needs none.
    if (ResolveAssetPath(ARCHER_CUE_TABLE,cue_table_path)){
        //_stat64 by name: MinGW's `stat` is an inline alias for a symbol this static link lacks.
        struct _stat64 st;
        cue_table_mtime = (_stat64(cue_table_path.c_str(),&st) == 0) ? (int64_t)st.st_mtime : 0;
    }else{
        cue_table_path.clear();
    }
    cue_table_polled = std::chrono::steady_clock::now();
    //The swoosh builds to the impact; how far in it peaks is how far ahead the flight has to be
    //forecast. The cue does the timing itself - this is only the horizon, and it is the table's
    //measurement, so nothing here asks the sound system anything.
    arrow_swoosh_peak = cues.PeakOf("arrow_swoosh");
    debug->Info("arrow_swoosh peaks %.3fs in - flights are forecast %.1f ticks ahead\n",
                arrow_swoosh_peak,arrow_swoosh_peak * ARCHER_TPS);
}

void ApplicationArcher::PollCueTable(){
    if (cue_table_path.empty()){
        return;
    }
    auto t = std::chrono::steady_clock::now();
    if (t - cue_table_polled < std::chrono::seconds(1)){
        return;
    }
    cue_table_polled = t;
    struct _stat64 st;
    if (_stat64(cue_table_path.c_str(),&st) != 0 || (int64_t)st.st_mtime == cue_table_mtime){
        return;
    }
    cue_table_mtime = (int64_t)st.st_mtime;
    std::string error;
    if (cues.Reload(error)){
        arrow_swoosh_peak = cues.PeakOf("arrow_swoosh");    //a re-cut swoosh re-measures itself
        debug->Info("Cue table reloaded: %s\n",cues.File().c_str());
    }else{
        debug->Err("Cue table NOT reloaded, keeping the last good one: %s\n",error.c_str());
    }
}

/*
    Once per tick, straight after the rules - physics thread, so each event is told on the tick
    of the thing it reports. The cues act on all of it at the END of the tick (RunSimulationTick),
    after the props and the arrows' forecasts have added theirs.

    What is reported, and what the table can hang a cue on:

      scope `nocked`       the arrow on the string - from the NOCK, not the press, so the first
                           ticks of a draw (her reaching back to the quiver with the string slack)
                           are not part of it. Ended by whatever ends the draw: a loose, a release
                           before the nock, a ledge grab, a restart. Read off the nock's EDGE
                           rather than off each of those, so a new way to end a draw cannot leave
                           a creak playing over it.
      signal `shot`        power 0..1
      scope `kick`         from the press to the kick's last tick, cut short if it loses the
                           ground. Carries `strike_shift`: how many ticks later (or earlier) this
                           kick's boot lands than K's kick's, which the table's timings were set on.
      signal `kick_connected`   in the kick, once, when the boot finds something: dir, x
      signal `jumped`      x
      signal `landed`      speed (how hard she came down), x
      signal `block_broken`   count, x - once a tick, however many bricks went
      signal `arrow_hit`   x, speed - level hits here, prop hits from ResolveArrowsAgainstProps
      signal `stand_hit`   points (RegisterTargetHit)
      scope `arrow`        one per flight, instance = the arrow's slot (ForecastArrowImpacts)
      signal `arrow_impact`   in (ticks until the strike), x, speed - every tick of the flight
*/
void ApplicationArcher::SignalCues(const StageEvents& events){
    bool f_nocked = stage.IsNocked();
    if (!f_nocked && f_was_nocked){
        cues.EndScope("nocked");
    }
    if (f_nocked && !f_was_nocked){
        cues.BeginScope("nocked");
    }
    f_was_nocked = f_nocked;

    if (events.f_shot){
        cues.Signal("shot",CuePayload().Set("power",events.shot_power));
    }

    /*
        The kick's scope. Its timings were found on Kick_Front, so each kick says how much later
        its boot lands - the middle of its active window against Kick_Front's - and the table
        counts from there. Ends on the tick kick_ticks goes back to 0, which is also a kick cut
        short: the cues drop what it had planned from that tick on.
    */
    bool f_kicking = stage.kick_ticks > 0;
    if (!f_kicking && f_was_kicking){
        cues.EndScope("kick");
    }
    if (events.f_kick_started){
        const KickSpec& kick = stage.Kick();
        int strike_shift = ((kick.active_from + kick.active_to) - (KICK_ACTIVE_FROM + KICK_ACTIVE_TO)) / 2;
        cues.BeginScope("kick",0,CuePayload().Set("strike_shift",(float)strike_shift));
    }
    f_was_kicking = f_kicking;
    //`dir` which way the boot went (+1 right), for a shake along it; `x` where it landed.
    if (events.f_kick_connected){
        float dir = events.kicks.empty() ? stage.facing : events.kicks[0].dir;
        float x = events.kicks.empty() ? stage.pos.x : events.kicks[0].x;
        cues.Signal("kick_connected",CuePayload().Set("dir",dir).Set("x",x));
    }

    //Leaving the ground and coming back to it. `speed` is how hard she came down - a routine
    //jump lands at about 18.7, a drop of 5.5 units at 25 (see PUPPET_HARD_LAND_VEL), stepping
    //off a kerb well under 5.
    if (events.f_jumped){
        cues.Signal("jumped",CuePayload().Set("x",stage.pos.x));
    }
    if (events.f_landed){
        cues.Signal("landed",CuePayload().Set("speed",events.land_speed).Set("x",stage.pos.x));
    }
    //A wall coming apart: once a tick however many bricks went, with how many and where the first was.
    if (!events.broken_blocks.empty()){
        int b = events.broken_blocks[0];
        float x = (b >= 0 && b < (int)stage.blocks.size()) ? stage.blocks[b].x : stage.pos.x;
        cues.Signal("block_broken",CuePayload().Set("count",(float)events.broken_blocks.size()).Set("x",x));
    }

    //The level's strikes. The props' come from ResolveArrowsAgainstProps, which finds them.
    for (size_t i = 0; i < events.arrow_hits.size(); i++){
        SignalArrowHit(events.arrow_hits[i].point.x,events.arrow_hits[i].speed);
    }
}

void ApplicationArcher::SignalArrowHit(float x, float speed){
    cues.Signal("arrow_hit",CuePayload().Set("x",x).Set("speed",speed));
}

/*
    A footstep is a foot PLANTING IN THE CLIP ON SCREEN - not a timer and not a distance walked.
    The walk and the runs are one blend space with their footfalls phase-locked (SetBlendPair), and
    each clip's two plants are measured at load (MeasureClipPhases), so a step is simply the
    playhead crossing one. That keeps the sound on the foot it belongs to at any speed, through
    the rate stretch, and across the blend from a walk into a run.

    ONLY THE LEADING CLIP'S steps fire (the plan's rule for markers): the blend pair's heavier
    side. Both sides plant together - that is what the phase lock is for - so firing from both
    would step twice.

    THE PHASE IS TAKEN RELATIVE TO THE LEFT PLANT, which is what the phase lock holds equal
    between the two clips of the blend, so when the lead passes from the walk to the run the
    phase carries on continuously and no step is doubled or lost at the handover. A lead that
    arrives some other way - a crossfade in from a landing - starts the count afresh rather than
    reading a jump in the playhead as a crossing.

    Read before SyncArcherAnimation, so the playhead is the one the pose on screen was made from.
    The engine advances it only on ticks that run, by the simulation's step, so steps replay to
    the tick. Payload: foot (0 left, 1 right), speed (|vel.x|) and x.
*/
void ApplicationArcher::SignalFootsteps(){
    Animation* lead = archer_model ? archer_model->current_animation : NULL;
    if (lead && archer_model->blend_animation && archer_model->blend_factor > 0.5f){
        lead = archer_model->blend_animation;
    }
    int clip = -1;
    for (int i = 0; lead && i < PUPPET_LOCOMOTION_COUNT; i++){
        if (archer_clips[PUPPET_LOCOMOTION[i]] == lead){
            clip = PUPPET_LOCOMOTION[i];
        }
    }
    if (clip < 0 || lead->duration <= 0.0f || stage.mode != MODE_GROUND || !stage.f_on_ground){
        f_step_valid = false;
        return;
    }
    auto wrap = [](float p){ return p - floorf(p); };
    float rel = wrap(lead->time_index / lead->duration - puppet.clip_phase[clip]);
    //Where the right foot plants, in the same left-relative phase.
    float right = wrap(puppet.clip_phase_right[clip] - puppet.clip_phase[clip]);
    bool f_same_run = f_step_valid && (lead == step_prev_lead ||
                                       step_prev_lead == archer_model->current_animation ||
                                       step_prev_lead == archer_model->blend_animation);
    if (f_same_run){
        //Forward only, and never more than half a cycle in a tick: anything else is a playhead
        //that was set rather than played, which is no step.
        float moved = wrap(rel - step_prev_rel);
        if (moved > 0.0f && moved < 0.5f){
            const float plants[2] = { 0.0f, right };
            for (int foot = 0; foot < 2; foot++){
                float to_plant = wrap(plants[foot] - step_prev_rel);
                if (to_plant > 0.0f && to_plant <= moved){
                    cues.Signal("footstep",CuePayload().Set("foot",(float)foot)
                                                       .Set("speed",fabsf(stage.vel.x))
                                                       .Set("x",stage.pos.x));
                }
            }
        }
    }
    step_prev_rel = rel;
    step_prev_lead = lead;
    f_step_valid = true;
}

/*
    See the declaration. After ResolveArrowsAgainstProps, so an arrow that struck a prop this tick
    is already stuck, its flight is over, and it has nothing to forecast.
*/
void ApplicationArcher::ForecastArrowImpacts(){
    int horizon = (int)ceilf(arrow_swoosh_peak * ARCHER_TPS) + 1;
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    rp3d::RigidBody* exclude = archer_object ? archer_object->GetRigidBody() : NULL;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        const Arrow& a = stage.arrows[i];
        bool f_flying = a.f_live && !a.f_stuck;
        if (!f_flying){
            if (arrow_in_flight[i]){
                cues.EndScope("arrow",i);
                arrow_in_flight[i] = false;
            }
            continue;
        }
        if (!arrow_in_flight[i]){
            cues.BeginScope("arrow",i);
            arrow_in_flight[i] = true;
        }
        //No swoosh loaded, nothing to forecast for. The flight is still a scope.
        if (arrow_swoosh_peak <= 0.0f){
            continue;
        }
        StageArrowImpact f = stage.PredictArrowImpact(i,horizon,&arrow_path);
        int ticks = f.f_hits ? f.ticks : -1;
        float x = f.point.x;
        //A prop before the block? Segment k of the path is the one flown in tick k + 1.
        for (size_t k = 0; world && k + 1 < arrow_path.size(); k++){
            if (ticks > 0 && (int)k + 1 >= ticks){
                break;
            }
            vec3 from(arrow_path[k].x,arrow_path[k].y,0.0f);
            vec3 to(arrow_path[k + 1].x,arrow_path[k + 1].y,0.0f);
            if (from.x == to.x && from.y == to.y){
                continue;
            }
            PhysicsWorld::RaycastHit hit = world->Raycast(from,to,exclude);
            Object* struck = (hit.hit && hit.body) ? (Object*)hit.body->getUserData() : NULL;
            bool f_prop = false;
            for (size_t v = 0; struck && v < prop_views.size(); v++){
                f_prop = f_prop || (prop_views[v].object == struck);
            }
            if (f_prop){
                ticks = (int)k + 1;
                x = hit.point.x;
                break;
            }
        }
        if (ticks < 0){
            continue;
        }
        float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y);
        cues.Signal("arrow_impact",CuePayload().Set("in",(float)ticks).Set("x",x).Set("speed",speed),i);
    }
}

void ApplicationArcher::HandleEvents(const StageEvents& events){
    //Sound and particles hang off here once there are any; for now the log is the feedback, and
    //only for the things worth a line. A landing every time the archer walks down a step would
    //drown the log that the MCP runs are read out of.
    if (events.f_grabbed_ledge){
        debug->Info("Caught a ledge at (%.2f,%.2f)\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_knelt){
        debug->Info("Kneeling at (%.2f,%.2f)\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_stood){
        debug->Info("Stood up at (%.2f,%.2f)\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_stand_blocked){
        debug->Info("No room to stand up at (%.2f,%.2f); staying down\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_climbed){
        debug->Info("Climbed up onto the ledge, standing at (%.2f,%.2f)\n",stage.pos.x,stage.pos.y);
    }
    if (events.f_kick_connected){
        debug->Info("Kick connected: %i props, %i blocks broken\n",
                    (int)events.kicks.size(),(int)events.broken_blocks.size());
    }
    if (events.f_shot){
        debug->Info("Shot at %.1f deg, power %.2f\n",events.shot_aim_deg,events.shot_power);
    }
    for (size_t i = 0; i < events.arrow_hits.size(); i++){
        const StageEvents::ArrowHit& h = events.arrow_hits[i];
        int kind = (h.block >= 0 && h.block < (int)stage.blocks.size()) ? stage.blocks[h.block].kind : -1;
        //A hit on the cracked wall is the one the kick-and-break slice will care about, so it is
        //worth naming now rather than being one more anonymous thud.
        debug->Info("Arrow %i hit block %i (%s) at (%.2f,%.2f) doing %.1f\n",
                    h.arrow,h.block,(kind == BLOCK_BREAKABLE) ? "breakable" : "solid",
                    h.point.x,h.point.y,h.speed);
    }
}

/*
    The other half of the arrow hit test: the half that knows about rigid bodies.

    Stage has already swept every arrow against the LEVEL and stuck the ones that hit it. What it
    cannot see is the props, because they are rp3d's and Stage names no engine type. So for every
    arrow still flying, the segment it covered this tick is handed to the solver as a raycast, and
    a hit is turned into a shove plus a stuck arrow.

    Order matters: this runs AFTER stage.Tick, so `prev_pos -> pos` is exactly the ground the arrow
    covered on the tick that just ran, and an arrow the rules already stuck in a wall is skipped
    before we ever ask.
*/
void ApplicationArcher::ResolveArrowsAgainstProps(){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world){
        return;
    }
    rp3d::RigidBody* exclude = archer_object ? archer_object->GetRigidBody() : NULL;

    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        Arrow& a = stage.arrows[i];
        if (!a.f_live || a.f_stuck){
            continue;
        }
        vec3 from(a.prev_pos.x,a.prev_pos.y,0.0f);
        vec3 to(a.pos.x,a.pos.y,0.0f);
        if (from.x == to.x && from.y == to.y){
            continue;       //a degenerate segment is not a query rp3d can answer
        }

        PhysicsWorld::RaycastHit hit = world->Raycast(from,to,exclude);
        if (!hit.hit || !hit.body){
            continue;
        }
        //Object::AddPhysics stamps every body with its owning Object, so this cast is valid for
        //anything in this world.
        Object* struck = (Object*)hit.body->getUserData();
        if (!struck){
            continue;
        }

        PropView* view = NULL;
        for (size_t v = 0; v < prop_views.size(); v++){
            if (prop_views[v].object == struck){
                view = &prop_views[v];
                break;
            }
        }
        if (!view){
            //A level block. Stage owns those and has already had its say, so there is nothing to
            //do here - and doing something would stick the arrow twice, in two different places.
            continue;
        }

        float speed = sqrtf(a.vel.x * a.vel.x + a.vel.y * a.vel.y);
        Physics* p = struck->GetPhysics();
        if (p && !p->IsStatic() && speed > 0.001f){
            /*
                A force applied for exactly one tick at the point of impact.

                Scaled by the struck body's own mass so that the velocity it picks up is
                ARROW_SPEED_TRANSFER of the arrow's speed WHATEVER the target weighs - which is
                what makes this one number tunable instead of needing a different one per prop.
                Applied at the hit POINT rather than at the centre, which is the whole reason a
                target board topples rather than sliding away flat: off-centre force is torque, and
                the solver works that out for free.

                rp3d clears external forces at the end of every step, so this is genuinely an
                impulse and not something that keeps pushing.
            */
            vec3 dir(a.vel.x / speed,a.vel.y / speed,0.0f);
            float dt = GetPhysicsTimestep();
            //Divided by the prop's HEFT - 1 for everything but an archery stand, which takes a sixth
            //of what a board does and so stays standing. See STAND_HEFT for why a mass alone
            //could not do this.
            float force = (speed * arrow_speed_transfer * p->GetMass()) / (dt * view->heft);
            p->WakeUp();
            p->AddWorldForceAt(dir * force,hit.point);
        }

        //Stuck where it struck, in Stage, which keeps the arrow's position the rules' business
        //even though this answer came from the solver.
        stage.StickArrow(i,v2(hit.point.x,hit.point.y));
        //...and pinned to the thing it went into, so it rides a crate that is kicked and goes down
        //with a target that topples instead of hanging in the air where the target used to be.
        StickArrowToProp(i,struck,v2(hit.point.x,hit.point.y));
        SignalArrowHit(hit.point.x,speed);

        if (view->kind == PROP_STRAWMAN){
            view->hits++;       //counted, not scored - a straw man scores the kick
        }
        if (view->kind == PROP_TARGET){
            debug->Info("Arrow %i struck target at (%.2f,%.2f) doing %.1f\n",
                        i,hit.point.x,hit.point.y,speed);
            RegisterTargetHit(*view,hit.point);
        }
    }
}

/*
    The archer's kinematic body, driven to where Stage says the archer is.

    By VELOCITY, not by position, and that is the entire trick - see the note at the top of
    ApplicationArcher.h. A kinematic body is stopped by nothing, so integrating this velocity for
    one timestep lands it exactly on Stage's answer; but on the way there it has a real velocity
    for the solver to resolve crate contacts against, which a setTransform teleport does not.
    That is what makes kicking a crate cost no kicking code.
*/
void ApplicationArcher::DriveArcherBody(){
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    //ON THE ROPE THE SOLVER IS DRIVING. Writing a velocity here as well would be the second
    //integrator this whole arrangement exists to avoid.
    if (stage.mode == MODE_ROPE){
        return;
    }
    vec3 now = p->GetBodyWorldPosition();
    vec3 want(stage.pos.x,stage.pos.y,0.0f);
    vec3 delta = want - now;

    /*
        A TELEPORT IS NOT A KICK.

        Falling off the level puts the archer back at the start, which as a velocity is several
        hundred units a second - enough to scatter every crate it passes on the way, one tick after
        the player stopped being anywhere near them. Anything that big is not movement, so it is
        applied as a position instead and the solver is never told about it.
    */
    float far_enough = ARCHER_RUN_SPEED * GetPhysicsTimestep() * 4.0f;
    if (delta.x * delta.x + delta.y * delta.y > far_enough * far_enough){
        p->SetBodyWorldPosition(want);
        p->SetVelocity(vec3());
        return;
    }
    p->SetVelocity(delta * (1.0f / GetPhysicsTimestep()));
}

/*
    Hand Stage every live prop as a plain box, before the tick.

    This is the whole of how "a crate blocks you" is wired, and the reason it is six numbers rather
    than a pointer: Stage names no engine type, so it cannot be given a body, a collider or an
    Object. It is given where the thing is and how big it is, plus an id it never interprets and
    hands straight back on a push event.

    REBUILT EVERY TICK, never kept in sync. These are rigid bodies - a crate shoved last tick is
    somewhere else now - and a stale box is an invisible wall standing where a crate used to be.
    Clearing and refilling a vector of a couple of dozen PODs costs nothing next to being wrong.

    A KNOCKED-OVER prop is deliberately left out. Its axis-aligned box stops describing it the
    moment it topples, and a board lying on the floor should be stepped over rather than walked
    into - leaving it out gets both right for free.
*/
/*
    The boot landing on a prop.

    Far harder than ApplyPushes, and that difference is the whole reason the two are separate
    events rather than one with a magnitude on it: a shove is ARCHER_PUSH_SPEED and moves a crate
    at walking pace, a kick is KICK_SPEED with KICK_LIFT under it and sends it. Set rather than
    added, like the shove, so it stays bounded.

    A BRICK IN A WALL IS THE INTERESTING CASE. The wall is built from static bodies - twenty-one
    dynamic boxes holding each other up is a lot of solver time spent keeping something perfectly
    still, and rp3d would have to be argued with to stop the stack slouching. So the bricks stand
    static until something frees them, and a kick frees THE WHOLE WALL at once, not just the bricks
    the boot touched. Freeing only those leaves the rest hanging in the air over the hole, which
    looks like a bug and is one; freeing all of them lets the wall come down, which is the thing
    the player asked for. The ones near the boot get the impulse, the rest simply lose their
    footing - which is what a wall collapsing IS.
*/
void ApplicationArcher::ApplyKicks(const StageEvents& events){
    for (size_t k = 0; k < events.kicks.size(); k++){
        const StageEvents::StageKick& kick = events.kicks[k];
        if (kick.id < 0 || kick.id >= (int)prop_views.size()){
            continue;
        }
        //Whichever of the three kicks it was - each lands with its own weight, see KICK_SPECS.
        const KickSpec& spec = KICK_SPECS[(kick.kind >= 0 && kick.kind < KICK_KIND_COUNT) ? kick.kind : KICK_FRONT];
        PropView& hit = prop_views[kick.id];
        if (!hit.object){
            continue;
        }

        /*
            A STRAW MAN IS STRUCK, NOT SET MOVING. Everything below sets a velocity, which on a body
            held by a hinge is a fight with the joint; one tick of force at the boot's height is an
            impulse instead, and the hinge turns it into a swing - lever arm and inertia included,
            which is the reason it is on a hinge. On its own line through the foot rather than at the
            kick box's x, which is off to one side of it: the push is horizontal, so only the height
            makes torque. And a point for it, whichever kick it was.
        */
        if (hit.kind == PROP_STRAWMAN){
            vec3 foot = hit.object->GetWorldPosition();
            Physics* p = hit.object->GetPhysics();
            if (p && hit.spring){
                float force = (spec.speed * straw_kick_punch * p->GetMass()) / GetPhysicsTimestep();
                p->WakeUp();
                p->AddWorldForceAt(vec3(kick.dir * force,0.0f,0.0f),vec3(foot.x,kick.y,0.0f));
            }
            hit.score++;
            kick_score++;
            SpawnHitPopup(vec3(foot.x,foot.y + hit.half_extents.y * 2.0f + 0.3f,BLOCK_DEPTH * 0.5f + 0.2f),hit.score);
            debug->Info("Straw man %i kicked (%s at height %.2f): %i on it, %i this level\n",
                        hit.index,spec.name,kick.y - foot.y,hit.score,kick_score);
            continue;
        }

        //Which bodies this kick is about: a lone crate or target is itself, a brick is its whole
        //wall. `index` is the Stage prop it came from, which for every brick of one wall is the
        //same number - that is what makes the wall identifiable at all.
        bool f_wall = (hit.kind == PROP_BRICKWALL);
        for (size_t i = 0; i < prop_views.size(); i++){
            PropView& view = prop_views[i];
            if (!view.object || view.f_lost){
                continue;
            }
            if (f_wall){
                if (view.kind != PROP_BRICKWALL || view.index != hit.index){
                    continue;
                }
            }else if (i != (size_t)kick.id){
                continue;
            }

            Physics* p = view.object->GetPhysics();
            if (!p){
                continue;
            }
            if (p->IsStatic()){
                //Freed. Gravity has to be turned on explicitly - SetStatic(false) does not do it,
                //and a brick without it hangs in the air looking like a broken solver. See the
                //gravity note in MakePlanarBody.
                p->SetStatic(false);
                p->SetGravityEnabled(true);
                p->SetLinearLockAxis(vec3(1.0f,1.0f,0.0f));
                p->SetAngularLockAxis(vec3(0.0f,0.0f,1.0f));
            }
            //A brick off a wall is rubble from here on: it stops being something the archer can
            //walk into, or the pile becomes the wall all over again.
            if (view.kind == PROP_BRICKWALL){
                view.f_broken = true;
            }

            //Impulse falls off with distance from the boot, so a wall bursts outward from where it
            //was struck rather than every brick leaving at the same speed in the same direction.
            vec3 pp = view.object->GetWorldPosition();
            float dx = pp.x - kick.x;
            float dy = pp.y - kick.y;
            float dist = sqrtf(dx * dx + dy * dy);
            //A gentle falloff on purpose: the bricks the boot actually touched are thrown, and
            //the rest of the wall still gets enough of a shove to come apart rather than settling
            //back into the same shape one row lower.
            float falloff = 1.0f / (1.0f + dist * dist * 0.30f);

            /*
                An archery stand is kicked OVER, not away: a gentler shove and a spin that throws its
                top away from the boot. A crate's full KICK_SPEED at the centre of mass would send a
                stand sliding off upright, and upright is the one way a kick must not leave it.
            */
            bool f_stand = (view.variant == TARGET_STAND);
            float scale = f_stand ? STAND_KICK_SCALE : 1.0f;

            vec3 v = p->GetVelocity();
            float want = kick.dir * spec.speed * falloff * scale;
            if ((kick.dir > 0.0f && v.x < want) || (kick.dir < 0.0f && v.x > want)){
                v.x = want;
            }
            float lift = spec.lift * falloff * scale;
            if (v.y < lift){
                v.y = lift;
            }
            p->WakeUp();
            p->SetVelocity(vec3(v.x,v.y,0.0f));
            if (f_stand){
                //Kicked towards +X, the top goes +X: a turn about Z the negative way. Tuned on
                //Kick_Front, so the other kicks spin it in proportion to how hard they shove.
                float spin = STAND_KICK_SPIN * (spec.speed / KICK_SPEED);
                p->SetAngularVelocity(vec3(0.0f,0.0f,-kick.dir * spin * falloff));
            }
        }
    }
}

/*
    A block the kick destroyed.

    Stage has already cleared its f_alive, so as far as the rules are concerned it is gone - the
    archer walks through where it stood and arrows fly through it. What is left is the half the
    rules cannot reach: a static collider still standing in the physics world, which crates and
    debris would pile against forever, and nothing on screen to say it broke.
*/
void ApplicationArcher::BreakBlocks(const StageEvents& events){
    for (size_t i = 0; i < events.broken_blocks.size(); i++){
        int index = events.broken_blocks[i];
        if (index < 0 || index >= (int)block_objects.size() || !block_objects[index]){
            continue;
        }
        Object* object = block_objects[index];
        vec3 centre = object->GetWorldPosition();
        vec3 size = object->GetScale();

        Physics* p = object->GetPhysics();
        if (p){
            //Deactivated rather than destroyed: the block objects are a vector indexed in step
            //with Stage::blocks, and deleting one out of the middle of that would put every index
            //after it out of alignment with the rules. It stops colliding, which is what matters.
            p->SetActive(false);
        }
        object->SetVisibility(false);

        const StageBlock& block = stage.blocks[index];
        //Away from the archer, because a wall you kicked should fall away from you.
        float dir = (stage.pos.x <= block.x) ? 1.0f : -1.0f;
        SpawnDebris(centre,vec3(size.x * 0.5f,size.y * 0.5f,size.z * 0.5f),
                    vec3(dir,0.35f,0.0f),material_breakable);
        //With the level tick, which seeds the debris - a replay that breaks it on a different one
        //throws different rubble.
        debug->Info("Broke block %i at (%.2f,%.2f), level tick %llu\n",index,centre.x,centre.y,
                    (unsigned long long)stage.ticks);
    }
}

/*
    Burst one block into chunks.

    Deliberately irregular - the chunks are different sizes and leave at different speeds, because
    a block that shatters into identical cubes all travelling the same way reads as a formation
    rather than as rubble.

    THE RANDOMNESS IS LOCAL AND SEEDED FROM THE TICK, not rand() and not the engine's RRandom.
    rand() is not reproducible across runs, which would make a recorded session diverge the moment
    a wall came down. RRandom would be reproducible but is a SHARED stream - the UI and the MCP
    thread draw from the same one off-tick, so the number this gets depends on what else happened
    to ask for a number first, which is an open problem in its own right. A local xorshift seeded
    from the tick and the chunk index is reproducible, costs nothing, and cannot be perturbed by
    anything outside this function.
*/
void ApplicationArcher::SpawnDebris(const vec3& centre, const vec3& half_extents,
                                    const vec3& impulse_dir, int material){
    //The LEVEL's tick (Stage::ticks, from the build), not the scene's, which keeps counting across
    //restarts: a replay restarts the level and restores this counter (RestoreRecordingState), so
    //the wall comes down in the same pieces. Seeded from the scene's tick it did not - a replay
    //never starts at the absolute tick the recording did.
    uint32_t seed = (uint32_t)(stage.ticks * 2654435761u) ^ 0x9E3779B9u;
    for (int i = 0; i < ARCHER_DEBRIS_PER_BLOCK; i++){
        if ((int)debris.size() >= ARCHER_MAX_DEBRIS){
            return;     //the cap is the point; see the note on it
        }
        //xorshift32, inline so the stream belongs to this burst and to nothing else.
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float fx = (float)(seed % 1000) / 1000.0f - 0.5f;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float fy = (float)(seed % 1000) / 1000.0f - 0.5f;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float scale = 0.30f + (float)(seed % 1000) / 1000.0f * 0.35f;
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        float fspeed = (float)(seed % 1000) / 1000.0f;

        vec3 size(half_extents.x * 2.0f * scale,half_extents.y * 2.0f * scale * 0.6f,
                  half_extents.z * 2.0f * scale);
        vec3 at(centre.x + fx * half_extents.x * 1.2f,
                centre.y + fy * half_extents.y * 1.4f,
                0.0f);

        char name[48];
        snprintf(name,sizeof(name),"debris_%i_%i",(int)debris.size(),i);
        Object* chunk = MakePlanarBody(unit_mesh,name,at,size,material,
                                       ARCHER_CAT_DEBRIS,ARCHER_MASK_DEBRIS,1.2f,false);
        if (!chunk){
            continue;
        }
        Physics* p = chunk->GetPhysics();
        if (p){
            float speed = 3.0f + fspeed * 5.0f;
            p->SetVelocity(vec3(impulse_dir.x * speed,
                                impulse_dir.y * speed + 2.0f + fy * 3.0f,0.0f));
            p->SetAngularVelocity(vec3(0.0f,0.0f,fx * 12.0f));
        }
        DebrisView view;
        view.object = chunk;
        view.reap_tick = main_scene->GetPhysicsTick() + ARCHER_DEBRIS_TICKS;
        debris.push_back(view);
    }
}

void ApplicationArcher::UpdateDebris(){
    uint64_t now = main_scene->GetPhysicsTick();
    bool f_any_destroyed = false;
    for (size_t i = 0; i < debris.size(); ){
        Object* object = debris[i].object;
        bool f_expired = (now >= debris[i].reap_tick) ||
                         (object && object->GetWorldPosition().y < -40.0f);
        if (!f_expired){
            i++;
            continue;
        }
        if (object){
            object->Destroy();
            f_any_destroyed = true;
        }
        debris.erase(debris.begin() + i);
    }
    if (f_any_destroyed){
        //Object::Destroy only MARKS. Without this the chunks stop rendering but their rigid bodies
        //stay in the physics world for the life of the run - and a player demolishing a wall makes
        //a lot of them. Safe here: RunSimulationTick holds physics_mutex, so the render thread is
        //not walking the object list. Same reasoning as breakout's UpdateDebris.
        main_scene->DeleteDestroyedObjects();
    }
}

/*
    The rope: a chain of light links hanging from the anchor, joined end to end.

    Built once, from the anchor prop's declared length. Each link is joined to the one above by a
    ball-and-socket at the point where they meet, and the top one to a STATIC anchor body - which is
    what makes the whole thing hang rather than fall.

    THE LINKS ARE NOT ALLOWED TO SLEEP. rp3d puts a body that has been still for a moment to sleep,
    and a hanging rope is still by definition - so without this the rope goes to sleep on the first
    frame and the archer swings into a bar of iron. apps/tank/CraneCharacter.cpp does the same for
    the same reason, and it is the single easiest thing to leave out.
*/
void ApplicationArcher::BuildRope(const StageProp& anchor){
    DestroyRope();
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world){
        return;
    }

    int links = (int)(anchor.h / ROPE_LINK_LENGTH + 0.5f);
    if (links < 2){
        links = 2;
    }
    float seg_len = anchor.h / (float)links;
    rope_seg_len = seg_len;
    rp3d::RigidBody* previous = NULL;

    //A static body at the anchor point for the top link to hang from. Invisible - the visible bar
    //is the prop itself, built by the caller.
    Object* fixed = MakePlanarBody(unit_mesh,"rope_fixed",vec3(anchor.x,anchor.y,0.0f),
                                   vec3(0.12f,0.12f,0.12f),material_ledge,
                                   ARCHER_CAT_ROPE,ARCHER_MASK_ROPE,0.0f,true);
    if (fixed){
        fixed->SetVisibility(false);
        previous = fixed->GetRigidBody();
        rope_segments.push_back(fixed);     //index 0 is the fixed point, not a handhold
    }

    for (int i = 0; i < links; i++){
        char name[32];
        snprintf(name,sizeof(name),"rope_%i",i);
        float cy = anchor.y - seg_len * ((float)i + 0.5f);
        Object* link = MakePlanarBody(unit_mesh,name,vec3(anchor.x,cy,0.0f),
                                      vec3(ROPE_SEGMENT_THICK,seg_len * 0.92f,ROPE_SEGMENT_THICK),
                                      material_ledge,ARCHER_CAT_ROPE,ARCHER_MASK_ROPE,
                                      rope_link_mass,false);
        if (!link){
            continue;
        }
        Physics* lp = link->GetPhysics();
        if (lp && lp->body && lp->body->rigidbody){
            //See the note above: a sleeping rope is a rigid rope.
            lp->body->rigidbody->setIsAllowedToSleep(false);
            //A little damping, or the rope keeps swinging for a minute after it is let go and
            //reads as being in space rather than on a windy cliff.
            lp->SetLinearDamping(0.12f);
            lp->SetAngularDamping(0.20f);
        }
        if (previous && link->GetRigidBody()){
            vec3 pivot(anchor.x,anchor.y - seg_len * (float)i,0.0f);
            rp3d::BallAndSocketJointInfo info(previous,link->GetRigidBody(),
                                              (rp3d::Vector3&)pivot);
            info.isCollisionEnabled = false;
            rp3d::BallAndSocketJoint* joint =
                dynamic_cast<rp3d::BallAndSocketJoint*>(world->rp_world->createJoint(info));
            if (joint){
                rope_joints.push_back(joint);
            }
        }
        previous = link->GetRigidBody();
        rope_segments.push_back(link);
    }
    debug->Info("Built a rope of %i links from (%.2f,%.2f)\n",
                (int)rope_segments.size() - 1,anchor.x,anchor.y);
    //A restart rebuilds the chain under a skin that is already there - hide the new boxes too.
    ApplyRopeLinkVisibility();
}

void ApplicationArcher::DestroyRope(){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (world){
        //The archer's own joint first - it refers to a link that is about to stop existing.
        if (rope_joint){
            world->rp_world->destroyJoint(rope_joint);
            rope_joint = NULL;
        }
        for (size_t i = 0; i < rope_joints.size(); i++){
            //NULL where the test bench cut it - already destroyed.
            if (rope_joints[i]){
                world->rp_world->destroyJoint(rope_joints[i]);
            }
        }
    }
    rope_joints.clear();
    for (size_t i = 0; i < rope_segments.size(); i++){
        if (rope_segments[i]){
            rope_segments[i]->Destroy();
        }
    }
    rope_segments.clear();
}

//--- The drawn rope -------------------------------------------------------------------------------

//The archer.glb node for each ROPE_PART_*, in that enum's order.
static const char* ROPE_PART_NODES[] = { "rope_segment", "rope_ring", "rope_collar", "rope_tassel" };

void ApplicationArcher::LoadRopeParts(){
    if (f_rope_parts_loaded){
        return;
    }
    f_rope_parts_loaded = true;
    rope_materials.clear();
    for (int k = 0; k < ROPE_PART_COUNT; k++){
        std::vector<Material> mats;
        Mesh* mesh = gltfloader.GetMeshFromNode(ROPE_PART_NODES[k],&mats,false);
        f_rope_part_from_asset[k] = (mesh != NULL);
        rope_parts[k].clear();
        if (!mesh){
            continue;
        }
        rope_parts[k] = mesh->GetVertices();
        /*
            The node's own scale, which GetMeshFromNode does not apply: rope_ring arrives scaled 2
            in Blender and never applied, and without this it is drawn at half the size it is
            modelled at. Uniform only - a squashed piece is taken at its largest axis and said so.
        */
        vec3 ns = gltfloader.GetNodeScale(ROPE_PART_NODES[k]);
        float node_scale = fmaxf(ns.x,fmaxf(ns.y,ns.z));
        if (fabsf(ns.x - ns.y) > 1e-3f || fabsf(ns.y - ns.z) > 1e-3f){
            debug->Warn("'%s' has a non-uniform node scale (%.3f,%.3f,%.3f); drawn at %.3f. Apply the "
                        "scale in Blender to keep its shape.\n",ROPE_PART_NODES[k],ns.x,ns.y,ns.z,node_scale);
        }
        rope_part_scale[k] = model_scale * ROPE_MESH_SCALE * node_scale;

        //One material list for the whole skin: each part's matid is remapped onto it by name.
        for (size_t i = 0; i < rope_parts[k].size(); i++){
            vertex& v = rope_parts[k][i];
            std::string mname = (v.matid >= 0 && v.matid < (int)mats.size()) ? mats[v.matid].name : "";
            int slot = -1;
            for (size_t m = 0; m < rope_materials.size(); m++){
                if (rope_materials[m].name == mname){
                    slot = (int)m;
                }
            }
            if (slot < 0 && v.matid >= 0 && v.matid < (int)mats.size() &&
                rope_materials.size() < NUM_MATERIAL_SLOTS){
                rope_materials.push_back(mats[v.matid]);
                slot = (int)rope_materials.size() - 1;
            }
            v.matid = (slot >= 0) ? slot : 0;
        }
        float z0 = 0.0f, zl = 0.0f;
        SplineDeformMeasure(rope_parts[k],z0,zl);
        debug->Info("Rope part '%s': %zu tris, %.3f long at scale 1, x%.3f to world%s\n",
                    ROPE_PART_NODES[k],rope_parts[k].size() / 3,zl,rope_part_scale[k],
                    (node_scale != 1.0f) ? " (node scale included)" : "");
    }
    if (!rope_materials.empty()){
        renderer->AddMaterials(rope_materials);
    }
    //No segment in the file: the vine's stand-in tile, which is in world units already. The pieces
    //are decoration and simply go missing.
    if (rope_parts[ROPE_PART_SEGMENT].empty()){
        MakeVinePlaceholderTile(rope_parts[ROPE_PART_SEGMENT]);
        rope_part_scale[ROPE_PART_SEGMENT] = ROPE_MESH_SCALE;
    }
}

bool ApplicationArcher::MakeRopeMeshInput(RopeMeshInput& in){
    const StageProp* anchor = NULL;
    for (size_t i = 0; i < stage.props.size(); i++){
        if (stage.props[i].kind == PROP_ROPE_ANCHOR){
            anchor = &stage.props[i];
            break;
        }
    }
    if (!anchor || rope_segments.size() < 2 || !main_scene){
        return false;
    }
    LoadRopeParts();

    in = RopeMeshInput();
    in.anchor = vec3(anchor->x,anchor->y,0.0f);
    in.length = anchor->h;
    in.links = (int)rope_segments.size() - 1;     //index 0 is the fixed anchor body
    in.tile = &rope_parts[ROPE_PART_SEGMENT];
    in.tile_scale = rope_part_scale[ROPE_PART_SEGMENT];
    if (!rope_parts[ROPE_PART_RING].empty()){
        in.ring = &rope_parts[ROPE_PART_RING];
        in.ring_scale = rope_part_scale[ROPE_PART_RING];
    }
    if (!rope_parts[ROPE_PART_TASSEL].empty()){
        in.tassel = &rope_parts[ROPE_PART_TASSEL];
        in.tassel_scale = rope_part_scale[ROPE_PART_TASSEL];
        //A cut end is frayed, and the tassel is the rope's frayed end. Only drawn once there is a cut.
        in.cut_end = in.tassel;
        in.cut_end_scale = in.tassel_scale;
    }
    if (!rope_parts[ROPE_PART_COLLAR].empty()){
        in.collar = &rope_parts[ROPE_PART_COLLAR];
        in.collar_scale = rope_part_scale[ROPE_PART_COLLAR];
        //Centred pieces, so each is placed by its middle: half its height clear of whatever is
        //above it, plus the gap.
        float z0 = 0.0f, zl = 0.0f;
        SplineDeformMeasure(rope_parts[ROPE_PART_COLLAR],z0,zl);
        float h = zl * in.collar_scale;
        float first = ROPE_COLLAR_GAP + 0.5f * h;
        in.collar_at = { first, first + h + ROPE_COLLAR_GAP, in.length - ROPE_COLLAR_GAP - 0.5f * h };
    }
    return true;
}

void ApplicationArcher::BuildRopeSkin(){
    //A level being built starts with none - the members were swapped in empty.
    rope_skin = NULL;
    rope_bones.clear();
    rope_skin_cuts.clear();
    RopeMeshInput in;
    if (!MakeRopeMeshInput(in)){
        return;
    }
    std::vector<skinned_vertex> verts;
    if (!BuildRopeMesh(in,verts) || verts.empty()){
        debug->Err("The rope's skin did not build - the links stay visible\n");
        return;
    }

    Skeleton* skin = new Skeleton();
    skin->name = "rope_skin";
    skin->num_bones = in.links;
    skin->SetPickability(false);
    Mesh* mesh = new Mesh();
    mesh->SetSkinnedMeshData(verts.data(),(int)verts.size());
    mesh->num_materials = rope_materials.empty() ? 1 : (int)rope_materials.size();
    skin->SetMesh(mesh);
    if (f_rope_part_from_asset[ROPE_PART_SEGMENT] && !rope_materials.empty()){
        skin->TakeMaterialNames(rope_materials);
    }else{
        skin->SetMaterialSlot(0,material_crate);
    }
    /*
        One bone per link, bound where BuildRope lays the link: its centre, no rotation. The
        inverse bind is then just the translation back, and UpdateRopeSkin only ever has to copy
        the link's pose on - no offset, because a link's origin IS its centre.
    */
    for (int i = 0; i < in.links; i++){
        Bone* bone = new Bone();
        char name[32];
        snprintf(name,sizeof(name),"rope_bone_%i",i);
        bone->name = name;
        bone->bone_index = i;
        vec3 c = RopeLinkBindCentre(in,i);
        bone->inverse_bind_matrix = fmat4().identity();
        bone->inverse_bind_matrix.set_position(vec3(-c.x,-c.y,-c.z));
        bone->SetPosition(c,false);
        skin->AttachChild(bone);
        rope_bones.push_back(bone);
    }
    main_scene->AddObject(skin);
    rope_skin = skin;
    ApplyRopeLinkVisibility();
    debug->Info("Rope skin: %i bones, %zu tris, segment %s, ring %s, collars %s, tassel %s\n",
                in.links,verts.size() / 3,
                f_rope_part_from_asset[ROPE_PART_SEGMENT] ? "archer.glb" : "placeholder",
                in.ring ? "yes" : "no",in.collar ? "3" : "no",in.tassel ? "yes" : "no");
}

/*
    ONE TICK BEHIND THE SOLVER, ON PURPOSE. This runs before the physics step, beside
    RefreshRopePoints and SyncArcherFromRope, so the skin shows the links where they were when the
    tick began - measured mid-swing, bones up to 0.039 behind their links, which is one tick of the
    swing's motion. Reading them after the step instead would put the rope a tick AHEAD of her,
    since she is drawn from the same pre-step read; her hands would slide along it. The two are
    drawn from the same instant, which is the thing that has to hold.
*/
void ApplicationArcher::UpdateRopeSkin(){
    //The chain is rebuilt with the same anchor and so the same count; a mismatch means it was not,
    //and posing the wrong links would tear the mesh - better left in its bind pose.
    if (!rope_skin || rope_bones.size() + 1 != rope_segments.size()){
        return;
    }
    for (size_t i = 0; i < rope_bones.size(); i++){
        Object* link = rope_segments[i + 1];
        if (!link){
            continue;
        }
        rope_bones[i]->SetPosition(link->GetWorldPosition(),false);
        rope_bones[i]->SetRotation(link->GetWorldRotation(),false);
    }
}

std::vector<int> ApplicationArcher::RopeCutJoints() const{
    std::vector<int> cuts;
    int links = (int)rope_segments.size() - 1;
    for (int j = 0; j < links; j++){
        if (j >= (int)rope_joints.size() || !rope_joints[j]){
            cuts.push_back(j);
        }
    }
    return cuts;
}

//Physics thread, every tick. Twelve pointer checks; the rebuild itself only happens on a change.
void ApplicationArcher::CheckRopeSkinCuts(){
    if (rope_skin && RopeCutJoints() != rope_skin_cuts){
        f_rope_skin_stale = true;
    }
}

/*
    Render thread (PreRender), inside AtTickBoundary. The same bind, the same bones - only the
    weights and the cut-end pieces change, so the new mesh goes into the old Mesh's buffer and every
    bone carries on posed where it was.
*/
void ApplicationArcher::RebuildRopeSkinWeights(){
    if (!rope_skin || !rope_skin->GetMesh()){
        return;
    }
    std::vector<int> cuts = RopeCutJoints();
    if (cuts == rope_skin_cuts){
        return;     //raised for the level that was live then, and this one is already right
    }
    RopeMeshInput in;
    if (!MakeRopeMeshInput(in)){
        return;
    }
    in.cuts = cuts;
    std::vector<skinned_vertex> verts;
    if (!BuildRopeMesh(in,verts) || verts.empty()){
        debug->Err("The rope's skin did not rebuild for %zu cuts - it stays as it was\n",cuts.size());
        return;
    }
    rope_skin->GetMesh()->SetSkinnedMeshData(verts.data(),(int)verts.size());
    rope_skin_cuts = cuts;
    debug->Info("Rope skin re-weighted: %zu cut%s, %zu tris\n",cuts.size(),
                (cuts.size() == 1) ? "" : "s",verts.size() / 3);
}

/*
    The beads are drawn this far TOWARD THE CAMERA from where they are measured. Every point they
    mark is inside something - the link inside the rope, the top of her box inside the box, her
    hands inside her hands - and a bead inside a mesh is not seen. In the side view the shift is a
    few percent of the distance from the middle of the screen, so near her it reads true; the
    numbers in archer_state are measured at z 0 and carry no such offset.
*/
#define ROPE_MARK_Z     0.9f

void ApplicationArcher::BuildRopeAttachMarkers(){
    //Red the rope's end of the joint, blue hers, yellow the left hand, orange-ish (the crate) the
    //right - the two hands apart, because in a climb they do different things.
    const int materials[ROPE_MARK_COUNT] = { material_breakable, material_platform,
                                             material_archer_hang, material_crate };
    const char* names[ROPE_MARK_COUNT] = { "rope_mark_link", "rope_mark_body",
                                           "rope_mark_hand_l", "rope_mark_hand_r" };
    for (int i = 0; i < ROPE_MARK_COUNT; i++){
        Object* o = new Object();
        o->name = names[i];
        o->SetMesh(dot_mesh);
        o->SetMaterialSlot(0,materials[i]);
        o->SetScale(vec3(1.0f,1.0f,1.0f));
        o->SetVisibility(false);
        o->SetPickability(false);
        o->SetCastsShadow(false);
        main_scene->AddObject(o);
        rope_marks[i] = o;
    }
    if (archer_model){
        hand_bones[0] = archer_model->FindBone(BOW_GRIP_BONE);
        hand_bones[1] = archer_model->FindBone(BOW_NOCK_BONE);
    }
}

/*
    Measured every tick she is on the rope, drawn only when asked.

    THE HANDS ARE JUDGED AGAINST THE ROPE AS DRAWN, a curve through the chain's joints (the same
    shape the skin bends to), rather than against the grip point: during a climb only ONE hand
    grips, and the other is meant to be off reaching. What says the gripping hand is holding is
    that its distance DOWN the rope stays put while she climbs, and that it is close to the rope -
    both are published per hand. A hand whose distance down creeps while it is on the rope is
    sliding.
*/
void ApplicationArcher::UpdateRopeAttachMarkers(){
    rope_joint_gap = -1.0f;
    rope_hands_off = -1.0f;
    for (int h = 0; h < 2; h++){
        rope_hand_s[h] = -1.0f;
        rope_hand_off[h] = -1.0f;
    }
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    bool f_on = (stage.mode == MODE_ROPE && rope_joint && p && rope_grip_link > 0 &&
                 rope_grip_link < (int)rope_segments.size() && rope_segments[rope_grip_link]);
    vec3 at[ROPE_MARK_COUNT];
    if (f_on){
        Object* link = rope_segments[rope_grip_link];
        at[ROPE_MARK_LINK] = link->GetWorldPosition() + link->GetWorldRotation() * rope_grip_local;
        at[ROPE_MARK_BODY] = p->GetBodyWorldPosition() +
                             p->GetBodyWorldOrientation() * vec3(0.0f,ROPE_GRIP_BODY_Y,0.0f);
        for (int h = 0; h < 2; h++){
            at[ROPE_MARK_HAND_L + h] = hand_bones[h] ? hand_bones[h]->GetWorldPosition() : at[ROPE_MARK_BODY];
        }
        vec3 flat_link = vec3(at[ROPE_MARK_LINK].x,at[ROPE_MARK_LINK].y,0.0f);
        vec3 flat_body = vec3(at[ROPE_MARK_BODY].x,at[ROPE_MARK_BODY].y,0.0f);
        vec3 mid = (at[ROPE_MARK_HAND_L] + at[ROPE_MARK_HAND_R]) * 0.5f;
        rope_joint_gap = flat_link.distance(flat_body);
        rope_hands_off = flat_link.distance(vec3(mid.x,mid.y,0.0f));

        /*
            Each hand against the chain, LINK BY LINK: the nearest point on the nearest link's
            centre line, and the distance down the rope that point is in the BIND length - the same
            units as rope_grip_s. Not along a curve through the chain, which was the first version:
            the joints open a few centimetres each under her weight, so a curve through them is
            longer than the rope and every distance down it read long, most of all at the bottom.
            A link is rigid, so within one the bind distance is exact.
        */
        for (int h = 0; h < 2; h++){
            vec3 hand = at[ROPE_MARK_HAND_L + h];
            hand.z = 0.0f;      //she and the rope are in the play plane; her hands are a little in front
            float best = -1.0f;
            float best_s = -1.0f;
            for (size_t i = 1; i < rope_segments.size(); i++){
                Object* o = rope_segments[i];
                if (!o){
                    continue;
                }
                vec3 c = o->GetWorldPosition();
                vec3 down = o->GetWorldRotation() * vec3(0.0f,-1.0f,0.0f);
                vec3 top = c - down * (0.5f * rope_seg_len);
                top.z = 0.0f;
                float along = (hand - top).dot(down);
                if (along < 0.0f){ along = 0.0f; }
                if (along > rope_seg_len){ along = rope_seg_len; }
                vec3 on = top + down * along;
                on.z = 0.0f;
                float off = hand.distance(on);
                if (best < 0.0f || off < best){
                    best = off;
                    best_s = (float)(i - 1) * rope_seg_len + along;
                }
            }
            rope_hand_s[h] = best_s;
            rope_hand_off[h] = best;
        }
    }
    for (int i = 0; i < ROPE_MARK_COUNT; i++){
        if (!rope_marks[i]){
            continue;
        }
        bool f_shown = f_on && f_show_rope_attach;
        if (f_shown){
            rope_marks[i]->SetPosition(vec3(at[i].x,at[i].y,at[i].z + ROPE_MARK_Z));
        }
        rope_marks[i]->SetVisibility(f_shown);
    }
}

//--- The rope test bench --------------------------------------------------------------------------

/*
    How far each joint of the chain has opened, every tick. Joint 0 holds the first link to the fixed
    anchor body; joint j the bottom of link j to the top of link j+1 - the order BuildRope makes
    them in, which is rope_joints' order. A link is rigid, so every bit of the rope's stretch is in
    these gaps, and their sum is how much longer the rope is than it was built.
*/
void ApplicationArcher::MeasureRopeStretch(){
    RopeStretch& r = rope_stretch;
    int links = (int)rope_segments.size() - 1;
    r.joints = (links > 0) ? links : 0;
    r.gaps.assign(r.joints,-1.0f);
    r.rest_length = (float)r.joints * rope_seg_len;
    r.loaded_length = r.rest_length;
    r.worst_gap = 0.0f;
    r.worst_joint = -1;
    r.cuts = 0;
    for (int j = 0; j < r.joints; j++){
        if (j >= (int)rope_joints.size() || !rope_joints[j]){
            r.cuts++;
            continue;
        }
        Object* above = rope_segments[j];
        Object* below = rope_segments[j + 1];
        if (!above || !below){
            continue;
        }
        //The fixed body IS the anchor point; a link's end is half a link along its own -Y.
        vec3 half = vec3(0.0f,0.5f * rope_seg_len,0.0f);
        vec3 top_end = (j == 0) ? above->GetWorldPosition()
                                : above->GetWorldPosition() - above->GetWorldRotation() * half;
        vec3 next_top = below->GetWorldPosition() + below->GetWorldRotation() * half;
        float gap = top_end.distance(next_top);
        r.gaps[j] = gap;
        r.loaded_length += gap;
        if (gap > r.worst_gap){
            r.worst_gap = gap;
            r.worst_joint = j;
        }
    }
    r.peak_age++;
    if (r.worst_gap >= r.peak_gap || r.peak_age > ROPE_PEAK_TICKS){
        r.peak_gap = r.worst_gap;
        r.peak_joint = r.worst_joint;
        r.peak_age = 0;
    }
}

/*
    Cut the chain at joint `joint`: everything below falls. A test of what the rest of the code does
    with a broken rope, and the first half of "cut it with an arrow" - but NOT a rule: nothing here
    decides when a rope breaks. What follows needs nothing from here: the next tick offers Stage no
    points below the cut, so if her grip was down there she lets go (RefreshRopePoints), and the
    skin sees the missing joint and is re-weighted into two pieces (CheckRopeSkinCuts). Restart
    rebuilds the chain whole. Physics thread, or inside AtTickBoundary.
*/
bool ApplicationArcher::CutRopeJoint(int joint){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world || joint < 0 || joint >= (int)rope_joints.size() || !rope_joints[joint]){
        return false;
    }
    world->rp_world->destroyJoint(rope_joints[joint]);
    rope_joints[joint] = NULL;
    //Wake the loose part, or a piece that had settled hangs in the air where it was cut - and let it
    //land on things (ARCHER_MASK_ROPE_LOOSE). A second cut further down finds these already loose.
    for (size_t i = (size_t)joint + 1; i < rope_segments.size(); i++){
        Physics* lp = rope_segments[i] ? rope_segments[i]->GetPhysics() : NULL;
        if (lp){
            lp->SetCollideWithMaskBits(ARCHER_MASK_ROPE_LOOSE);
            lp->WakeUp();
        }
    }
    debug->Info("Cut the rope at joint %i\n",joint);
    return true;
}

//This scene's solver only - each scene has its own PhysicsWorld, so the rope scene can be given more
//without the main level paying for it.
void ApplicationArcher::SetRopeSolverIterations(int velocity, int position){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world){
        return;
    }
    if (velocity < 1){ velocity = 1; }
    if (velocity > 200){ velocity = 200; }
    if (position < 1){ position = 1; }
    if (position > 200){ position = 200; }
    world->rp_world->setNbIterationsVelocitySolver((rp3d::uint16)velocity);
    world->rp_world->setNbIterationsPositionSolver((rp3d::uint16)position);
    solver_velocity_iterations = velocity;
    solver_position_iterations = position;
}

void ApplicationArcher::ApplyRopeLinkVisibility(){
    //Index 0 is the fixed anchor body, which is never drawn.
    for (size_t i = 1; i < rope_segments.size(); i++){
        if (rope_segments[i]){
            rope_segments[i]->SetVisibility(!rope_skin || f_show_rope_links);
        }
    }
}

/*
    Where the archer may catch the rope, handed to Stage as plain numbers before the tick.

    A POINT EVERY ROPE_GRAB_STEP DOWN THE CHAIN, each carrying its distance down (`s`), rather than
    one per link: she catches the rope at the point nearest her hands, and that distance is the
    grip Stage then climbs from. The top ROPE_FIRST_GRABBABLE links are still left out - see the
    note on that constant - which also makes the top of that span the highest she can climb.
    The id is only the point's number; what matters on the grab is its `s`.
*/
void ApplicationArcher::RefreshRopePoints(){
    stage.ClearRopePoints();
    int links = (int)rope_segments.size() - 1;
    if (links < 1 || rope_seg_len <= 0.0f){
        return;
    }
    float from = (float)(ROPE_FIRST_GRABBABLE - 1) * rope_seg_len;
    /*
        ONLY WHAT STILL HANGS FROM THE ANCHOR. Below the first cut joint the chain is a loose piece
        - falling, or lying on the floor - and a grip there is a grip on nothing: Stage lets go of
        any grip past the last point offered, so this is also what drops her when the rope is cut
        above her hands. The cut joint's own distance belongs to the link below it (see
        RopeDistanceToLink), so it is left out.
    */
    int held = RopeAnchoredLinks();
    float to = (float)held * rope_seg_len;
    if (held < links){
        to -= 2e-4f;
    }
    int id = 0;
    for (float s = from; s <= to + 1e-4f; s += ROPE_GRAB_STEP){
        int link = -1;
        vec3 local;
        if (!RopeDistanceToLink(s,link,local)){
            continue;
        }
        Object* o = rope_segments[link];
        vec3 p = o->GetWorldPosition() + o->GetWorldRotation() * local;
        stage.AddRopePoint(p.x,p.y,id++,s);
    }
}

//How many links, from the top, are still jointed all the way up to the anchor: the first cut
//joint's number, or all of them.
int ApplicationArcher::RopeAnchoredLinks() const{
    int links = (int)rope_segments.size() - 1;
    for (int j = 0; j < links; j++){
        if (j >= (int)rope_joints.size() || !rope_joints[j]){
            return j;
        }
    }
    return (links > 0) ? links : 0;
}

bool ApplicationArcher::RopeDistanceToLink(float s, int& link, vec3& local) const{
    int links = (int)rope_segments.size() - 1;      //index 0 is the fixed anchor body
    if (links < 1 || rope_seg_len <= 0.0f){
        return false;
    }
    int k = (int)floorf(s / rope_seg_len);
    if (k < 0){ k = 0; }
    if (k > links - 1){ k = links - 1; }
    if (!rope_segments[k + 1] || !rope_segments[k + 1]->GetRigidBody()){
        return false;
    }
    link = k + 1;
    //A link hangs along its own -Y from its top, so a point further down it is further along -Y
    //from its centre. That is its frame in the bind pose, and so in every pose after.
    local = vec3(0.0f,((float)k + 0.5f) * rope_seg_len - s,0.0f);
    return true;
}

/*
    THE HANDOFF. This is the one moment in the app where the archer stops being the rules' and
    starts being the solver's, and every line of it is undoing an assumption made elsewhere:

      - the body has been KINEMATIC, moved by DriveArcherBody to wherever Stage said. It becomes
        DYNAMIC, and DriveArcherBody stands down for as long as MODE_ROPE lasts.
      - it has collided with NOTHING, because Stage was resolving the world by hand. Now nothing is,
        so it needs a real collision mask or the swing goes through the floor.
      - it arrives with the velocity Stage had, which is what makes catching a rope at a run throw
        you further than catching it standing still. That continuity is the whole feel of the
        mechanic and it is one line.
*/
void ApplicationArcher::AttachArcherToRope(int segment){
    (void)segment;      //the grip is stage.rope_s, set by the same grab
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    int link = -1;
    vec3 local;
    if (!p || !RopeDistanceToLink(stage.rope_s,link,local)){
        return;
    }
    Object* o = rope_segments[link];

    /*
        SHE IS MOVED SO THAT THE GRIP IS ON THE ROPE, which is not where she was standing.

        The joint anchors her at ROPE_GRIP_BODY_Y above her centre, so she is placed with that point
        on the rope where she caught it. Leaving her where she stood put the attachment wherever the
        reach test happened to allow - through her chest, or BELOW HER CENTRE OF MASS, which is not a
        pendulum but an inverted one: she slowly turns over and hangs upside down, and nothing in the
        solver is wrong about it.

        The reach and the grip are DIFFERENT numbers on purpose - Stage::FindRopePoint measures from
        ARCHER_HALF_H * 0.6, chest height, where hands rest; a hanging grip is overhead.

        The move is bounded by ROPE_GRAB_REACH and lands on the one tick that is already a
        discontinuity - kinematic to dynamic, and a mode change with it.
    */
    vec3 grip = o->GetWorldPosition() + o->GetWorldRotation() * local;
    p->SetBodyWorldPosition(vec3(grip.x,grip.y - ROPE_GRIP_BODY_Y,0.0f));
    //Upright to start with, whatever tumble the body was left in. The swing should build from
    //hanging rather than from wherever the last one finished.
    p->SetBodyWorldOrientation(quat(vec3(0,0,1),0.0f));
    p->SetBodyType(rp3d::BodyType::DYNAMIC);
    p->SetGravityEnabled(true);
    p->SetCollideWithMaskBits(ARCHER_MASK_ON_ROPE);
    p->SetVelocity(vec3(stage.vel.x,stage.vel.y,0.0f));
    //See ROPE_HANG_DAMPING: she is a pendulum hanging inside a pendulum, and only the rope's was
    //damped. Linear damping stays OFF - that is the swing itself, and it is meant to keep going.
    p->SetAngularDamping(ROPE_HANG_DAMPING);
    p->WakeUp();

    //A fresh catch: no joint yet, and the grip exactly where the geometry puts it.
    rope_grip_link = -1;
    rope_grip_offset = vec3();
    ReanchorRopeJoint();
    debug->Info("Caught the rope %.2f down, on link %i, at (%.2f,%.2f)\n",stage.rope_s,link,grip.x,grip.y);
}

/*
    The joint, at wherever Stage says the grip is.

    rp3d fixes a ball-and-socket's anchors when the joint is created, so moving the grip means
    making the joint again - with EXPLICIT LOCAL anchors, the link's at the new distance down it and
    hers at ROPE_GRIP_BODY_Y. Nothing is teleported: the new anchor is a climb step (a centimetre a
    tick) from where she is, and the solver pulls her to it. So she climbs WHILE the rope swings,
    and a climb shortens the pendulum for real - which pumps or damps the swing the way it does for
    a person on a rope. Crossing into the next link is the same call, onto the other body.
*/
void ApplicationArcher::ReanchorRopeJoint(){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    int link = -1;
    vec3 local;
    if (!world || !p || !archer_object->GetRigidBody() || !RopeDistanceToLink(stage.rope_s,link,local)){
        return;
    }
    /*
        ONTO THE NEXT LINK, THE GRIP IS CARRIED ACROSS rather than snapped to the geometry.

        The bottom of one link and the top of the next are one point in the bind pose and NOT under
        load: with her 70 kg on 1.2 kg links the joints between them open 0.13 - 0.20 each,
        measured. Re-anchoring onto the new link's geometric end put that whole gap into the joint
        in one tick, and the solver's correction kicked her - swings of up to a link's length, once
        9.9. So on a change of link the anchor is taken where the old one IS, in the new link's
        frame, and the difference from the geometry is an offset.

        AND THE OFFSET FADES OUT over the next ROPE_GRIP_BLEND of climbing. The first version kept
        it for the whole link and was WRONG: the point carried across already included the
        previous link's offset, so the offsets ADDED UP, one joint's stretch per crossing - after
        a few links the anchor sat far off the end of the link it was on, her weight hung on that
        lever, and the chain ran away (reported with her 8 units under the floor on a 9-unit rope).
        Fading keeps the anchor continuous at the crossing and back on the link's geometry soon
        after, so each crossing carries only its own joint's stretch. rope_grip_drift reports it.
    */
    if (link != rope_grip_link && rope_grip_link > 0 && rope_grip_link < (int)rope_segments.size() &&
        rope_segments[rope_grip_link]){
        Object* was = rope_segments[rope_grip_link];
        Object* now = rope_segments[link];
        vec3 held = was->GetWorldPosition() + was->GetWorldRotation() * rope_grip_local;
        quat inv = now->GetWorldRotation();
        inv.inverse();
        vec3 carried = inv * (held - now->GetWorldPosition());
        rope_grip_offset = carried - local;
        rope_grip_cross_s = stage.rope_s;
    }else if (link != rope_grip_link){
        rope_grip_offset = vec3();
        rope_grip_cross_s = stage.rope_s;
    }
    float fade = 1.0f - fabsf(stage.rope_s - rope_grip_cross_s) / ROPE_GRIP_BLEND;
    if (fade < 0.0f){ fade = 0.0f; }
    rope_grip_drift = rope_grip_offset.length() * fade;
    local = local + rope_grip_offset * fade;

    /*
        AND SHE IS MOVED THE STEP, so the new joint is already satisfied when it is made.

        Leaving the step to the solver was what stretched the rope. A joint closes its error by moving
        both bodies in inverse proportion to their mass, and she is 70 kg against a 1.2 kg link - so
        98% of every climb step was the link being pulled DOWN to her rather than her going up it.
        Measured on the rope bench: hanging still the rope is 3% over its rest length; climbing it grew
        to +24%, the top joints open 0.2, and relaxed back to exactly its rest length within two
        seconds of stopping. She had also risen only 5.4 for 5.9 climbed.

        Moving her body the step first makes the climb hers alone, as climbing is. Bounded, so this
        only ever absorbs a climb step and the joint's own small error - a big one (a hard landing on
        the rope, a crossing mid-swing) is still the solver's to resolve.
    */
    if (archer_object->GetRigidBody() && rope_segments[link]){
        vec3 target = rope_segments[link]->GetWorldPosition() + rope_segments[link]->GetWorldRotation() * local;
        vec3 held = p->GetBodyWorldPosition() + p->GetBodyWorldOrientation() * vec3(0.0f,ROPE_GRIP_BODY_Y,0.0f);
        vec3 step = target - held;
        step.z = 0.0f;
        float limit = 3.0f * ROPE_CLIMB_SPEED * ARCHER_DT + 0.02f;
        float l = step.length();
        if (l > limit){
            step = step * (limit / l);
        }
        p->SetBodyWorldPosition(p->GetBodyWorldPosition() + step);
    }
    if (rope_joint){
        world->rp_world->destroyJoint(rope_joint);
        rope_joint = NULL;
    }
    vec3 body_local = vec3(0.0f,ROPE_GRIP_BODY_Y,0.0f);
    rp3d::BallAndSocketJointInfo info(rope_segments[link]->GetRigidBody(),archer_object->GetRigidBody(),
                                      (rp3d::Vector3&)local,(rp3d::Vector3&)body_local);
    info.isCollisionEnabled = false;
    rope_joint = dynamic_cast<rp3d::BallAndSocketJoint*>(world->rp_world->createJoint(info));
    rope_grip_s = stage.rope_s;
    rope_grip_link = link;
    rope_grip_local = local;
    p->WakeUp();
}

/*
    And back again. The velocity the solver built up is READ OUT and handed to Stage, which is the
    payoff of the whole mechanic - let go at the bottom of the arc and you keep the speed, let go at
    the top and you do not.
*/
void ApplicationArcher::DetachArcherFromRope(bool f_jump){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (world && rope_joint){
        world->rp_world->destroyJoint(rope_joint);
    }
    rope_joint = NULL;
    rope_grip_s = -1.0f;
    rope_grip_link = -1;
    if (!p){
        return;
    }

    vec3 v = p->GetVelocity();
    vec3 at = p->GetBodyWorldPosition();
    //Letting go with JUMP adds height; letting go with action keeps only what the swing gave.
    float vy = v.y + (f_jump ? ROPE_JUMP_BOOST : 0.0f);

    p->SetBodyType(rp3d::BodyType::KINEMATIC);
    p->SetCollideWithMaskBits(ARCHER_MASK_ARCHER);
    p->SetVelocity(vec3());
    //Upright again, and undamped. A kinematic body keeps whatever rotation it was last given, so
    //without this the collider box stays leaning at whatever angle she let go at - for the rest of
    //the level, since nothing else ever writes the archer's orientation.
    p->SetBodyWorldOrientation(quat(vec3(0,0,1),0.0f));
    p->SetAngularDamping(0.0f);

    stage.pos = v2(at.x,at.y);
    stage.vel = v2(v.x,vy);
    debug->Info("Let go of the rope at (%.2f,%.2f) doing (%.2f,%.2f)%s\n",
                at.x,at.y,v.x,vy,f_jump ? " with a jump" : "");
}

//While swinging, the archer's position IS the body's. Read it back so the rules, the camera and
//every telemetry reader agree with what is on screen.
void ApplicationArcher::SyncArcherFromRope(){
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    vec3 at = p->GetBodyWorldPosition();
    vec3 v = p->GetVelocity();
    stage.pos = v2(at.x,at.y);
    stage.vel = v2(v.x,v.y);
}

/*
    Pumping the swing.

    A force rather than a velocity, and that is the point: a rope you can steer by setting your
    speed is a rope with no timing in it. A force has to be applied in the right phase of the arc to
    build anything, which is the entire skill of a rope swing and costs one line to express.
*/
void ApplicationArcher::PumpRope(float move_axis){
    Physics* p = archer_object ? archer_object->GetPhysics() : NULL;
    if (!p){
        return;
    }
    if (move_axis < 0.01f && move_axis > -0.01f){
        return;     //not leaning either way
    }
    p->WakeUp();
    p->AddWorldForceAt(vec3(move_axis * ROPE_PUMP_FORCE,0.0f,0.0f),
                       p->GetBodyWorldPosition());
}

void ApplicationArcher::RefreshObstacles(){
    stage.ClearObstacles();
    for (size_t i = 0; i < prop_views.size(); i++){
        const PropView& view = prop_views[i];
        //Rubble and toppled boards are stepped over, not walked into. See PropView::f_broken.
        if (!view.object || view.f_lost || view.f_knocked || view.f_broken){
            continue;
        }
        Physics* p = view.object->GetPhysics();
        //Static props - a brick in a standing wall - block without being shoved. That is what
        //makes the brick wall a wall until the kick slice frees the bricks it breaks.
        bool f_pushable = (p && !p->IsStatic());
        //The box's middle: the object's origin, lifted along its own up for a prop that stands on
        //its origin rather than being centred on it - see PropView::obstacle_lift.
        vec3 pp = view.object->GetWorldPosition()
                + (view.object->GetWorldRotation() * vec3(0.0f,1.0f,0.0f)) * view.obstacle_lift;
        //A straw man is in the boot's way and in nobody else's: she walks through it.
        bool f_blocks = (view.kind != PROP_STRAWMAN);
        stage.AddObstacle(pp.x,pp.y,view.half_extents.x,view.half_extents.y,(int)i,f_pushable,f_blocks);
    }
}

/*
    Shove whatever the archer leaned on.

    Stage decides WHETHER something is being pushed, in which direction and how fast, because all
    three fall out of the sweep it already does; this decides what that means for a rigid body. The
    velocity is SET rather than added, so it is bounded by construction however many ticks the lean
    lasts - and the archer is capped to the same speed by the rules, so the two move together
    instead of the archer grinding through a crate it is outrunning.

    No upward component. This is a shove along the floor, not a kick; the crate should slide rather
    than hop. A kick is a separate verb with a key of its own, and belongs to the kick slice.
*/
void ApplicationArcher::ApplyPushes(const StageEvents& events){
    for (size_t i = 0; i < events.pushes.size(); i++){
        const StageEvents::StagePush& push = events.pushes[i];
        if (push.id < 0 || push.id >= (int)prop_views.size()){
            continue;
        }
        PropView& view = prop_views[push.id];
        Physics* p = view.object ? view.object->GetPhysics() : NULL;
        if (!p || p->IsStatic()){
            continue;
        }
        vec3 v = p->GetVelocity();
        float want = push.dir * push.speed;
        //Only if it would speed the prop up - a crate already sliding away faster than the archer
        //walks must not be slowed to their pace by the hand still resting on it.
        if ((push.dir > 0.0f && v.x < want) || (push.dir < 0.0f && v.x > want)){
            v.x = want;
        }
        p->WakeUp();
        p->SetVelocity(vec3(v.x,v.y,0.0f));
    }
}

void ApplicationArcher::SyncArcherView(){
    if (!archer_object){
        return;
    }
    bool f_on_wall = (stage.mode == MODE_HANG || stage.mode == MODE_CLIMB);
    bool f_sliding = (stage.SlideAccel() != 0.0f);
    archer_object->SetMaterialSlot(0,f_on_wall ? material_archer_hang :
                                     (f_sliding ? material_archer_slide : material_archer));
    /*
        And the model. Its slot 0 is whatever BuildArcherModel gave it - the export's own material,
        or the flat skin - remembered the first time here, so the tint comes off to exactly that.
    */
    if (archer_model){
        if (archer_model_material < 0){
            archer_model_material = archer_model->GetMaterialSlot(0);
        }
        archer_model->SetMaterialSlot(0,f_sliding ? material_archer_slide : archer_model_material);
    }
    //The box is the fallback character when there is no model, and a debug draw when there is.
    archer_object->SetVisibility(!archer_model || f_show_collider);
}

/*
    The model: what it plays, how fast, and which way it faces.

    THE WHOLE OF THE ANIMATION LAYER PASSES THROUGH ONE STRUCT HERE, and that is the design rather
    than a tidy-up. ArcherAnimParams is filled by the rules, or by the panel's sliders, or not at
    all when a single clip is being previewed - and Puppet::Tick cannot tell which. That is what
    makes the animation prototype separable from the physics one: everything below this line
    behaves identically whether there is a level underneath her or not.

    Physics thread, after Stage has decided where she is.
*/
/*
    How far the bow is bent, and that is the whole of what this app drives on the bow per tick.

    THE POSITION IS THE SKELETON'S JOB, not this function's. The bow is a child of a hand bone, so
    Object composes its world transform from the posed bone every frame with no code at all - see
    the long note in Bow.h. Anything here that moved the bow would be a second opinion about where
    her hand is.

    TWO SOURCES FOR THE BEND, chosen by whether the draw POSE is on screen:

      - It is (standing, or previewing the draw clip): THE STRING FOLLOWS HER HAND - Bow::TrackHand,
        measured off the posed skeleton every tick. Nothing bends and no arrow is on the string
        until her hand gets there, which in Standing_DrawArrow is 0.6s of 1.067 in: she reaches back
        to the quiver first and carries the arrow over (bow_plan.md §8 item 7).
      - It is not (a draw at a run, until the mask layer puts the draw on her upper body): the old
        rule - the bend from draw_ticks and the arrow on the string for the whole draw. Her hand is
        nowhere near the string in a run cycle, so tracking it would show nothing at all until the
        arrow flew, and some feedback beats none.

    The rules' POWER is unchanged either way: it is draw_ticks, as Stage::Loose reads it. Making the
    visible draw and the power agree is the gameplay decision still open (bow_plan.md §8 item 7).

    Physics thread, like every other Sync* beside it, and after the animation pass that posed her
    this tick, so the hand TrackHand reads is the hand on screen. Safe for the usual reason:
    SetShapekey and SetPosition write floats on Objects and touch no GL.
*/
void ApplicationArcher::SyncBow(){
    bool f_drawing = (stage.bow_mode == BOW_DRAWING);
    //On the base (standing) or on the upper layer (any stance, once it is mostly on).
    bool f_draw_pose = (anim_source == ANIM_FROM_CLIP) ? Puppet::IsDrawPose(playing_clip)
                     : (Puppet::IsDrawPose(puppet.choice.clip) ||
                        (puppet.upper_weight > 0.5f && Puppet::IsDrawPose(puppet.choice.upper_clip)));
    float draw01 = 0.0f;
    if (f_draw_pose){
        //Previewing the clip draws it whatever the rules think, so a new draw clip can be checked
        //on its own - which is what the preview is for.
        bool f_was_on_string = bow_rig.f_hand_on_string;
        draw01 = bow_rig.TrackHand(f_drawing || anim_source == ANIM_FROM_CLIP);
        f_arrow_nocked = bow_rig.f_hand_on_string;
        if (f_arrow_nocked && !f_was_on_string && f_drawing){
            CheckNockTicks();
        }
    }else{
        bow_rig.TrackHand(false);
        /*
            No draw pose to read the hand off - a draw at a run - so the string shows the rules'
            own pull: nothing until the nock, then the same ramp the power takes. After
            HandleEvents, so a draw cancelled or loosed this tick ends with the string empty.
        */
        if (stage.IsNocked()){
            float pull = (float)(stage.draw_ticks - BOW_NOCK_TICKS) /
                         (float)(BOW_DRAW_TICKS - BOW_NOCK_TICKS);
            draw01 = (pull < 0.0f) ? 0.0f : ((pull > 1.0f) ? 1.0f : pull);
        }
        f_arrow_nocked = stage.IsNocked();
    }
    bow_rig.SetDraw(draw01);
    //The in-hand arrow only exists with the draw pose on screen - TrackHand(false) clears it.
    bow_rig.SetArrowNocked(f_arrow_nocked,bow_rig.f_arrow_in_hand);
    bow_draw_shown = draw01;
}

/*
    BOW_NOCK_TICKS, checked against the hand. Called on the tick her hand reaches the string in a
    real draw. Stage cannot see the hand, so the rules' nock is a typed constant, and this is what
    keeps it honest - the same arrangement as KICK_TICKS. A gap of a tick or two is the animation
    running one tick behind the rules and means nothing; more than that means the clip or
    BOW_DRAW_TICKS moved, and the message says what to type. Once per distinct value, not per draw.
*/
void ApplicationArcher::CheckNockTicks(){
    int seen = stage.draw_ticks;
    int gap = seen - BOW_NOCK_TICKS;
    if (gap < -2 || gap > 2){
        if (nock_warned_at != seen){
            debug->Warn("Her hand reached the string at draw tick %d but BOW_NOCK_TICKS is %d, so "
                        "the rules %s. Set BOW_NOCK_TICKS to %d in Stage.h.\n",seen,BOW_NOCK_TICKS,
                        (gap > 0) ? "let her loose an arrow that is not on the string yet"
                                  : "refuse a shot with the arrow already on the string",seen);
            nock_warned_at = seen;
        }
    }
}

void ApplicationArcher::SyncArcherAnimation(){
    if (!archer_model){
        return;
    }
    //The cycle base of the playhead pinned THIS tick - posed next tick, see climb_base_posed.
    bool  f_pinned = false;
    float base_pinned = 0.0f;

    /*
        Previewing one clip bypasses the decisions entirely.

        SwitchToAnimation rather than TransitionToAnimation, because a preview that blends out of
        whatever was playing is a preview of the blend. The question being asked here is "did this
        clip export correctly", and that wants the clip from its first frame.
    */
    if (anim_source == ANIM_FROM_CLIP){
        if (preview_clip >= 0 && preview_clip < CLIP_COUNT && archer_clips[preview_clip]){
            if (preview_clip != playing_clip){
                archer_model->SwitchToAnimation(archer_clips[preview_clip]);
                playing_clip = preview_clip;
                //A new clip turns her from wherever she is now, not from wherever the last one
                //left off - see ArcherModel::clip_yaw.
                archer_model->clip_yaw = 0.0f;
            }
            archer_model->SetAnimationRate(preview_rate);
        }
        //A clip preview is the clip as exported - nothing bends it and nothing layers over it.
        archer_model->aim_weight = 0.0f;
        archer_model->upper_weight = 0.0f;
        archer_model->leg_weight = 0.0f;
        archer_model->overlay_weight = 0.0f;
    }else{
        ArcherAnimParams params;
        if (anim_source == ANIM_FROM_PANEL){
            params = panel_params;
        }else{
            DescribeArcher(stage,params);
            //The forecast needs this tick's input, which the rules' state does not hold - so it
            //is the app's to hand over, from RunSimulationTick's Stage::PredictLanding.
            params.land_in_ticks = landing_forecast.f_lands ? landing_forecast.ticks : -1;
            params.land_speed = landing_forecast.speed;
        }
        puppet.Tick(params);

        /*
            The aim, handed to the model's post-pose override (ArcherModel::ApplyAnimation). The
            TARGET only: the model reads where the posed bow already points (the live neutral,
            animation_plan.md Step 2) and turns her by the difference. From the SAME params as
            the clip choice, so the panel's aim slider bends her exactly as the game's aim does.
        */
        archer_model->aim_target_deg = params.aim_deg;
        archer_model->aim_weight = puppet.aim_weight;
        archer_model->aim_facing = (params.facing < 0.0f) ? -1.0f : 1.0f;

        /*
            The loose legs: the Puppet's weight and lead, and the gravity of the world she is
            swinging in - the rope scene's is not the rules' ARCHER_GRAVITY, and legs that fell
            faster than the rope does would lead the swing instead of trailing it.
        */
        archer_model->leg_weight = puppet.leg_weight;
        archer_model->leg_lead_deg = puppet.leg_lead_deg;
        if (main_scene->physics_world){
            rp3d::Vector3 g = main_scene->physics_world->rp_world->getGravity();
            archer_model->leg_params.gravity = vec3(g.x,g.y,g.z) * puppet.leg_gravity;
        }

        /*
            The upper-body layer. Its time is PINNED to the rules' draw progress when the Puppet
            gives a phase - the visual follows the rules, as the bend does - and runs on its own
            clock for a loop. A non-looping clip on its own clock is the draw fading out after a
            release: it holds the frame it was let go on.
        */
        int upper = puppet.choice.upper_clip;
        Animation* upper_anim = (upper >= 0 && upper < CLIP_COUNT) ? archer_clips[upper] : NULL;
        /*
            The layer's own crossfade (Puppet::upper_mix). When a new one starts, the clip it is
            leaving is frozen at the time it was LAST SHOWN - which is still in upper_time_shown,
            since this tick's has not been worked out yet.
        */
        if (puppet.upper_xfade_serial != upper_xfade_seen){
            upper_xfade_seen = puppet.upper_xfade_serial;
            upper_from_time = upper_time_shown;
        }
        int from = puppet.choice.upper_from_clip;
        archer_model->upper_from_clip = (from >= 0 && from < CLIP_COUNT) ? archer_clips[from] : NULL;
        archer_model->upper_from_time = upper_from_time;
        archer_model->upper_mix = puppet.upper_mix;
        if (upper != upper_clip_shown){
            upper_loop_time = 0.0f;
            upper_clip_shown = upper;
        }
        if (upper_anim){
            if (puppet.choice.upper_phase >= 0.0f){
                upper_time_shown = puppet.choice.upper_phase * upper_anim->duration;
            }else if (upper_anim->looped){
                upper_loop_time += ARCHER_DT;
                if (upper_anim->duration > 0.0f){
                    upper_loop_time = fmodf(upper_loop_time,upper_anim->duration);
                }
                upper_time_shown = upper_loop_time;
            }
        }
        archer_model->upper_clip = upper_anim;
        archer_model->upper_time = upper_time_shown;
        archer_model->upper_weight = puppet.upper_weight;
        //The whole-body overlay - today, the fall pose (Puppet::fall_weight).
        int overlay = puppet.choice.overlay_clip;
        archer_model->overlay_clip = (overlay >= 0 && overlay < CLIP_COUNT) ? archer_clips[overlay] : NULL;
        archer_model->overlay_time = puppet.choice.overlay_time;
        archer_model->overlay_weight = puppet.choice.overlay_weight;

        int clip = puppet.choice.clip;
        if (clip >= 0 && clip < CLIP_COUNT && archer_clips[clip]){
            /*
                Only on a CHANGE. Asking for the clip that is already playing would restart it
                every tick.
            */
            int second = puppet.choice.blend_clip;
            Animation* follow = (second >= 0 && second < CLIP_COUNT) ? archer_clips[second] : NULL;
            /*
                INSIDE THE LADDER the pair is handed over whole, every tick, and the pose never
                passes through a transition at all.

                That is the blend space: the weight is a continuous parameter, so there is no event
                to interrupt or to catch halfway. SetBlendPair keeps the shared phase across a
                change of PAIR as well as of weight, which is what makes crossing a rung invisible -
                see the note on it. Calling it every tick is correct and cheap; it re-bases only
                when the pair actually changes.

                Entering the ladder from somewhere else - standing up out of the idle, landing,
                letting go of a ledge - still goes through an ordinary crossfade, because those
                really are events and a 0.15s fade is the right shape for them.

                So the test is WHETHER THE POSE CAN BE CARRIED OVER, not whether there are two
                clips to blend. It can whenever no crossfade is in flight and the new pair has a
                clip in common with what is on screen, in either slot - SetBlendPair then solves
                the shared phase so that the clip they have in common does not move.

                Only asking "is there a follower" left both ENDS of the ladder falling out to a
                crossfade they did not need. Past the fast run the weight is already 1.0 and the
                pose already IS the fast run, so fading to it "from" the slow run went backwards
                into a clip no longer visible and out again - a lurch at exactly full sprint. And
                coming back down, the clip that had been playing alone becomes the FOLLOWER at
                most of the weight, which is why both slots have to be checked and not just the
                leader.
            */
            /*
                ONLY FOR A CYCLE. SetBlendPair carries the pose over as a PHASE and wraps it with
                floorf, so a one-shot sitting on its last frame - phase 1.0 - is handed back its
                first. Every tick it was the only clip on screen, the draw went through here and
                started again the instant it reached full, when it was meant to hold there. A
                one-shot has no phase to carry; it is either a change of clip, below, or nothing.
            */
            Animation* lead = archer_clips[clip];
            Animation* posed_lead = archer_model->current_animation;
            Animation* posed_follow = archer_model->blend_animation;
            bool f_continuous = lead->looped
                                && !archer_model->previous_animation
                                && (lead == posed_lead || lead == posed_follow
                                    || (follow && (follow == posed_lead
                                                   || follow == posed_follow)));
            if (f_continuous){
                archer_model->SetBlendPair(lead,follow,puppet.choice.blend,
                                           puppet.choice.blend_phase_offset);
                if (clip != playing_clip){
                    archer_model->clip_yaw = 0.0f;
                }
                playing_clip = clip;
            }else if (clip != playing_clip){
                /*
                    RECORDED ONLY IF IT TOOK. "Only on a change" and "assume it worked" are
                    individually reasonable and together permanent: once playing_clip says Idle,
                    nothing ever asks for Idle again, so a single refused request leaves her
                    playing the wrong clip for the rest of the session. The refusal left is a
                    non-interruptible clip that has not finished - the kick - which is precisely
                    the case where the rules move on before the animation is willing to.
                */
                /*
                    Seeded BEFORE the transition, because a crossfade blends from the pose the
                    destination is standing at - so setting the playhead afterwards would fade
                    into the start frame and only then jump to where it was asked to begin.
                */
                if (puppet.choice.start_time >= 0.0f){
                    lead->time_index = puppet.choice.start_time;
                }
                if (archer_model->TransitionToAnimation(lead)){
                    playing_clip = clip;
                    archer_model->clip_yaw = 0.0f;
                }
            }
            archer_model->SetAnimationRate(puppet.choice.rate);
            /*
                A PINNED playhead (the rope climb): set, every tick, on the clip being shown - the
                rate is 0, so the engine holds it there and still poses from it. During the fade in
                the engine advances it by one tick first, which is a sixtieth of a second of pose
                for the length of a 0.15s crossfade.
            */
            if (puppet.choice.pinned_time >= 0.0f && playing_clip == clip){
                lead->time_index = puppet.choice.pinned_time;
                f_pinned = true;
                base_pinned = puppet.choice.lift_base;
            }
        }
    }

    /*
        Where she stands, and which way she points.

        The model is placed at the BOTTOM of the body box rather than at its centre, because a
        character's origin is between their feet and Stage's is the middle of a 1.8-unit box. Get
        this wrong by half a body and she stands waist-deep in the floor, which is the first thing
        anyone notices and the last thing anyone suspects.
    */
    vec3 centre = vec3(stage.pos.x,stage.pos.y,0.0f);
    vec3 feet = centre - vec3(0.0f,ARCHER_HALF_H + model_foot_offset,0.0f);
    /*
        Facing is the Puppet's answer PLUS whatever the clip has turned her through.

        Two separate ideas added together, deliberately. The Puppet's yaw is which way the GAME
        says she is pointing; clip_yaw is a turn the ANIMATOR authored, and it is relative to that
        rather than instead of it - so a pivot clip works the same whichever way she was already
        facing. Setting only the first is what made every clip's rotation look like it had not
        been exported: see the note on ArcherModel.
    */
    //No filtering here any more: clip_yaw only ever accumulates from a clip whose
    //extract_yaw_root_motion is set, which is the f_turns column, set once at load. A cycle's hip
    //rotation stays on the bone where it belongs instead of arriving here to be discarded.
    float yaw = puppet.yaw_deg * ARCHER_DEG2RAD + archer_model->clip_yaw;

    /*
        AND ON THE ROPE, A TILT AS WELL - the one state where which way is up for her is not
        straight up.

        Everywhere else she stands on something and only her yaw is in question. Hanging from a
        rope she is a pendulum, and the solver is already swinging one: the archer's body is
        DYNAMIC for the whole of MODE_ROPE, jointed to the link she caught, and free to rotate
        about Z because MakePlanarBody locks the other two axes. That rotation was simply not being
        looked at - so the debug box leaned with the rope while the character inside it stayed bolt
        upright, which is the picture that started this.

        IT IS NOT A RULES QUANTITY and could not be. Stage::TickArcher stands aside for the whole
        of MODE_ROPE precisely so the swing can be emergent, so there is nothing in the rules that
        knows about this angle and nothing that should. It is read off the collider, next to the
        position that is already read off the same body in SyncArcherFromRope.

        TAKEN FROM WHERE HER OWN UP POINTS rather than from the quaternion's z component, because
        the planar lock is a constraint rather than a guarantee - it is solved, so it leaks a
        little - and a rotation read off one component of a not-quite-planar quaternion goes wrong
        slowly and unreadably. Rotating (0,1,0) and asking where it ended up cannot.

        COMPOSED OUTSIDE THE YAW, which matters: the tilt is about the WORLD's Z, the axis pointing
        at the camera. Inside the yaw it would be about HER z, so it would flip every time she
        turned to face the other way and she would lean out of the swing instead of into it.
    */
    quat rotation = quat(vec3(0,1,0),yaw);
    float roll = 0.0f;
    Physics* body = (stage.mode == MODE_ROPE && archer_object) ? archer_object->GetPhysics() : NULL;
    if (body){
        vec3 up = body->GetBodyWorldOrientation() * vec3(0.0f,1.0f,0.0f);
        roll = atan2f(-up.x,up.y);
        quat tilt = quat(vec3(0,0,1),roll);
        rotation = tilt * rotation;
        /*
            AND THE FEET HANG OFF THE CENTRE THROUGH THE SAME TILT. The collider turns about its
            centre; the model turns about its origin, which is between her feet. Placing the feet
            straight below the centre and then tilting put the model's middle at
            centre - (0,h) + R(0,h): right when upright, and at 90 degrees of swing h*sqrt(2),
            about 1.3 units, down and to the side - the model lying beside her own collider
            rather than in it, which is what was reported.
        */
        /*
            And the climb's own rise taken back out. Rope_Climbing is not in place - its hips go up
            0.28 rig units a cycle while the gripping hand stays still - and the rules' body is
            already carrying her up the rope, so the model is lowered by as much as this pose has
            risen (PuppetChoice::lift, from the clip's first frame). That leaves the hips riding the
            body and the gripping hand where the rope is.

            FOR THE POSE ON SCREEN, which is last tick's, not this one's. The engine poses the rig
            in Scene::UpdateAnimations, BEFORE this tick runs, so what is drawn is the playhead
            pinned last tick. Lowering it by this tick's lift was a tick out of step: invisible for
            most of the cycle, but at the loop the lift fell from a whole cycle's rise to 0 while
            the pose was still on its last frame, and for one frame she was drawn 0.57 too high.
            So: the distance climbed NOW, less the start of the cycle the SHOWN pose belongs to
            (PuppetChoice::lift_base). That keeps the gripping hand on the rope to the tick.
        */
        climb_lift_posed = f_climb_posed ? stage.rope_climbed - climb_base_posed : 0.0f;
        feet = centre + tilt * vec3(0.0f,-(ARCHER_HALF_H + model_foot_offset) - climb_lift_posed,0.0f);
    }else{
        climb_lift_posed = 0.0f;
        /*
            In the air the hips ride the body - see air_hip_ref. Read off the pose on screen (posed
            before this tick) against the model's own position, so whatever crossfade, lead-in or
            overlay made that pose, this is how far its hips are from standing height. A per-clip
            number cannot know that mid-crossfade: the first version lowered by the landing clip's
            descent alone, and dropped her 0.5 under the body while the pose was still mostly
            Falling_Idle.
        */
        bool f_air = (stage.mode == MODE_AIR && puppet.air_clip != CLIP_RUN_JUMP &&
                      anim_source != ANIM_FROM_CLIP && air_hip_ref > 0.0f);
        float step = 1.0f / (float)PUPPET_FALL_BLEND_TICKS;
        air_hip_weight = f_air ? fminf(1.0f,air_hip_weight + step) : 0.0f;
        float hold = air_hip_weight;
        Bone* hips = archer_model->FindBone(ARCHER_MODEL_ROOT_BONE);
        if (hips && hold > 0.0f){
            float posed = hips->GetWorldPosition().y - archer_model->GetWorldPosition().y;
            feet.y -= (posed - air_hip_ref) * hold;
        }
    }
    f_climb_posed = f_pinned;
    climb_base_posed = base_pinned;
    /*
        ON A BRANCH, HER LEAN - Stage::lean, sideways, + away from the camera - as a roll of the
        whole model about her feet, until there is an animation to carry it. About the WORLD's X, the
        axis she walks along, and outside the yaw for the rope tilt's reason: inside it the lean would
        flip sides every time she turned round.
    */
    if (stage.f_on_ground && stage.branch_on >= 0 && stage.lean != 0.0f){
        rotation = quat(vec3(1,0,0),-stage.lean) * rotation;
    }
    archer_model->SetPosition(feet);
    archer_model->SetRotation(rotation);
    archer_model->leg_drawn_yaw = yaw;
    archer_model->leg_drawn_tilt = body ? quat(vec3(0,0,1),roll) : quat(0.0f,0.0f,0.0f,1.0f);
    model_yaw_drawn = yaw / ARCHER_DEG2RAD;
    model_roll_drawn = roll / ARCHER_DEG2RAD;
}

/*
    How far a prop has turned IN THE PLAY PLANE - its tip about Z - read off where its up vector
    points.

    Not 2 * atan2(q.z, q.w), which is what this used to be and is exact only for a rotation that is
    purely about Z. An archery stand is built YAWED toward the archer (STAND_YAW_DEG), and that
    formula folds the yaw into the answer: a stand standing perfectly upright read as tipped, and
    every arrow stuck in it was carried round by an angle the stand had never turned through. The
    up vector ignores a turn about Y by construction, and for every prop that has no yaw the two
    agree exactly.
*/
static float PropPlaneAngle(Object* prop){
    vec3 up = prop->GetWorldRotation() * vec3(0.0f,1.0f,0.0f);
    return atan2f(-up.x,up.y);
}

/*
    Pin an arrow to the prop it just went into.

    Recorded in the PROP's frame rather than the world's, which is the whole point: the prop is
    about to move, and a world position would be a record of where it USED to be. Props are planar
    - MakePlanarBody locks them to the XY plane and to rotation about Z - so "the prop's frame" is
    a position and one angle, and this is 2D rather than a matrix inverse.

    The angle is stored as a DIFFERENCE too. An arrow that went into a board at 20 degrees is still
    at 20 degrees to that board after it has fallen over, which is what makes the board look like
    it fell WITH the arrow in it rather than past it.
*/
void ApplicationArcher::StickArrowToProp(int index, Object* prop, const v2& point){
    if (index < 0 || index >= ARROW_MAX_LIVE || !prop){
        return;
    }
    vec3 at = prop->GetWorldPosition();
    float prop_angle = PropPlaneAngle(prop);

    float dx = point.x - at.x;
    float dy = point.y - at.y;
    float c = cosf(-prop_angle);
    float s = sinf(-prop_angle);

    StuckArrow& stuck = arrow_stuck[index];
    stuck.prop = prop;
    stuck.local = v2(dx * c - dy * s,dx * s + dy * c);
    stuck.local_angle = stage.arrows[index].angle - prop_angle;
}

/*
    Let go of one prop's arrows, or of all of them.

    CALLED BEFORE THE PROPS ARE DESTROYED on a restart, and that is not tidiness. Object::~Object
    deletes its children, and while these arrows are not children, the same restart rebuilds the
    prop list - so an arrow still holding a destroyed prop would be following a dangling pointer on
    the next tick. It is the one failure here that crashes rather than looks wrong.
*/
void ApplicationArcher::ReleaseStuckArrows(Object* prop){
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (!prop || arrow_stuck[i].prop == prop){
            arrow_stuck[i] = StuckArrow();
        }
    }
}

void ApplicationArcher::SyncArrowViews(){
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        Object* o = arrow_objects[i];
        if (!o){
            continue;
        }
        Arrow& a = stage.arrows[i];
        StuckArrow& stuck = arrow_stuck[i];

        /*
            LET GO THE MOMENT THE ARROW STOPS BEING A STUCK ONE.

            Not only when it ages out: the pool is a ring of ARROW_MAX_LIVE, so this slot can come
            back round as a NEW arrow in flight, and one still holding a prop would be fired
            already welded to a crate. Checking f_stuck as well as f_live is what makes a recycled
            slot safe without the loose() path having to know this exists.
        */
        if (stuck.prop && (!a.f_live || !a.f_stuck)){
            stuck = StuckArrow();
        }

        o->SetVisibility(a.f_live && (!stuck.prop || stuck.prop->IsVisible()));
        if (!a.f_live){
            continue;
        }

        float angle = a.angle;
        if (stuck.prop){
            /*
                CARRIED BY THE PROP. Recomputed every tick rather than written once, because the
                prop is still being solved - a kicked crate is in the air for the best part of a
                second and a toppling board turns through ninety degrees on its way down.
            */
            vec3 at = stuck.prop->GetWorldPosition();
            float prop_angle = PropPlaneAngle(stuck.prop);
            float c = cosf(prop_angle);
            float s = sinf(prop_angle);
            a.pos = v2(at.x + stuck.local.x * c - stuck.local.y * s,
                       at.y + stuck.local.x * s + stuck.local.y * c);
            angle = stuck.local_angle + prop_angle;
            /*
                AND WRITTEN BACK INTO Stage, which looks like the wrong direction and is the
                established one: StickArrow already takes a point the SOLVER found, and
                SyncArcherFromRope writes a whole position and velocity back every tick of a swing.
                A stuck arrow is inert in the rules - it only ages - so nothing downstream is being
                driven by this. What it buys is that there is still one answer to "where is arrow
                7", instead of a drawn position and a reported one that disagree the moment
                anything moves.
            */
            a.angle = angle;
        }

        o->SetPosition(vec3(a.pos.x,a.pos.y,0.0f));
        //The mesh runs along +X, so one rotation about Z aims it. A loose stuck arrow keeps the
        //angle it arrived at, which is why Stage stops updating `angle` once it sticks; one stuck
        //in a prop is turned by the prop instead, above.
        o->SetRotation(quat(vec3(0.0f,0.0f,1.0f),angle));
    }
}

/*
    Cut the aim arc short at the first PROP it would hit.

    Stage::PredictArc already stops the arc at level geometry, and cannot do more than that: props
    are rigid bodies and the rules name no engine type. So without this the preview keeps the
    promise it makes about walls and breaks it about the one thing the player is usually aiming AT
    - the arc sails straight through a target board and lands somewhere behind it, while the arrow
    itself stops dead in the board, because ResolveArrowsAgainstProps asks the solver and the
    preview did not.

    So the preview asks too, over exactly the same segments, and the two halves of "what will this
    arrow hit" are now asked in the same two places for the preview as for the flight. Costs one
    raycast per bead, and only while the bow is drawn.

    Returns the new point count, with the last point moved to the point of impact.
*/
int ApplicationArcher::TruncateArcAgainstProps(v2* points, int count){
    PhysicsWorld* world = main_scene ? main_scene->physics_world : NULL;
    if (!world || !points || count < 1){
        return count;
    }
    rp3d::RigidBody* exclude = archer_object ? archer_object->GetRigidBody() : NULL;

    //From the ANCHOR, like the rules' own first sweep - so a crate between the string and the
    //arrowhead stops the preview too. See ARROW_LENGTH in Stage.h.
    v2 from = stage.AnchorPosition();
    for (int i = 0; i < count; i++){
        v2 to = points[i];
        if (from.x == to.x && from.y == to.y){
            from = to;
            continue;
        }
        PhysicsWorld::RaycastHit hit = world->Raycast(vec3(from.x,from.y,0.0f),
                                                      vec3(to.x,to.y,0.0f),exclude);
        if (hit.hit){
            points[i] = v2(hit.point.x,hit.point.y);
            return i + 1;
        }
        from = to;
    }
    return count;
}

void ApplicationArcher::SyncAimArc(){
    //Only with an arrow on the string: before the nock, letting go does not shoot (BOW_NOCK_TICKS),
    //and an arc drawn then would promise a shot the rules will not take.
    bool f_drawing = stage.IsNocked();
    if (!f_drawing){
        for (int i = 0; i < AIM_ARC_POINTS; i++){
            if (arc_objects[i]){
                arc_objects[i]->SetVisibility(false);
            }
        }
        return;
    }

    v2 points[AIM_ARC_POINTS];
    int n = stage.PredictArc(points,AIM_ARC_POINTS);
    n = TruncateArcAgainstProps(points,n);
    for (int i = 0; i < AIM_ARC_POINTS; i++){
        Object* o = arc_objects[i];
        if (!o){
            continue;
        }
        o->SetVisibility(i < n);
        if (i >= n){
            continue;
        }
        o->SetPosition(vec3(points[i].x,points[i].y,0.0f));
        //The last bead is where it stops - either where it hits, or the end of the preview. That
        //is the one the player is really reading, so it gets its own colour.
        o->SetMaterialSlot(0,(i == n - 1) ? material_dot_hot : material_dot);
        //Beads shrink along the arc, so which end is which is readable without following it.
        float t = 1.0f - (0.55f * (float)i / (float)AIM_ARC_POINTS);
        o->SetScale(vec3(t,t,t));
    }
}

void ApplicationArcher::UpdateTargets(){
    for (size_t i = 0; i < prop_views.size(); i++){
        PropView& view = prop_views[i];
        if (view.kind != PROP_TARGET || !view.object || view.f_knocked){
            continue;
        }
        //How far off vertical the board has tipped. GetWorldUp is the object's own up in world
        //space, so the dot against world up is the cosine of the tilt - no Euler angles, and no
        //question of which order they would have been in.
        vec3 up = view.object->GetWorldUp();
        float cos_tilt = clamp(up.y,-1.0f,1.0f);
        float tilt_deg = todegrees(acosf(cos_tilt));
        if (tilt_deg >= TARGET_KNOCKED_DEG){
            view.f_knocked = true;
            //A plain board goes green. A stand keeps its own painted material - slot 0 is the whole
            //model, and a green archery stand would say nothing a fallen one does not already.
            if (view.variant != TARGET_STAND){
                view.object->SetMaterialSlot(0,material_target_hit);
            }
            debug->Info("Target %i knocked over (%.0f degrees)\n",view.index,tilt_deg);
        }
    }
}

/*
    Props that have left the level.

    A crate kicked into one of the gaps is gone, and gameplay-wise that is fine - what is not fine
    is that nothing stops it. There is no floor under the gaps, so it falls forever: a live rigid
    body accelerating toward infinity, reported to every telemetry reader as a target at y -658
    doing 156 units a second. That is not a physics fault, it is a missing reaper, and it is the
    kind of thing that looks like a physics fault for an hour.

    Deactivated rather than destroyed. Object::Destroy only MARKS, and the actual delete walks the
    scene's object list - which is not a thing to start doing from inside a tick. Deactivating
    takes the body out of the simulation immediately and safely, and the next restart rebuilds
    every prop from Stage anyway.
*/
void ApplicationArcher::ReapFallenProps(){
    for (size_t i = 0; i < prop_views.size(); i++){
        PropView& view = prop_views[i];
        if (view.f_lost || !view.object){
            continue;
        }
        //Well below the lowest thing anything is meant to stand on, so a prop resting at the
        //bottom of a pit - if one is ever added - would not be swept up by this.
        if (view.object->GetWorldPosition().y > -40.0f){
            continue;
        }
        Physics* p = view.object->GetPhysics();
        if (p){
            p->SetVelocity(vec3());
            p->SetActive(false);
        }
        view.object->SetVisibility(false);
        view.f_lost = true;
        debug->Info("Prop %s fell out of the level and was retired\n",view.object->name.c_str());
    }
}

void ApplicationArcher::UpdateCamera(){
    /*
        A trailing camera with lead - one camera for every scene, tuned by camera_tuning (the
        panel's camera sliders).

        Welding the camera to the archer makes a fast platformer unreadable - the world slides
        under a character who never moves, and the eye has nothing to track. So the camera aims at
        a point AHEAD of the archer in the direction they are travelling and eases toward it, which
        both shows more of where they are going and lets the character move within the frame.

        The lead follows the VELOCITY, not the facing: turning round while drawing a bow should not
        swing the camera across the level. It is a fraction of the half-width ON SCREEN, so it
        shrinks with the zoom; in world units, 3.0 at the default distance was two thirds of the
        whole view once zoomed right in, and a turn swung her out of it.

        THE ORBIT FOLLOWS HER AS WELL, at the side camera's horizontal rate.
        camera_target is the orbit's pivot, and the pivot is kept at orbit_follow_offset from her;
        the camera is moved by exactly the step the pivot takes, so the angle and distance the
        mouse set are left alone and the whole view simply travels with her. The offset is taken
        from the view on the tick the orbit is switched on, so switching moves nothing on screen,
        and a shift+middle pan adds to it - a panned view keeps its framing and keeps following.
        The backdrop and the sun follow the pivot, in both modes, through PlaceCamera.
    */
    const ArcherCameraTuning& tune = camera_tuning;
    vec3 body(stage.pos.x,stage.pos.y + 0.5f,0.0f);

    if (camera_mode == ARCHER_CAM_ORBIT){
        if (last_camera_mode != ARCHER_CAM_ORBIT){
            orbit_follow_offset = camera_target - body;
        }
        last_camera_mode = camera_mode;
        camera_ideal = body + orbit_follow_offset;
        vec3 step = (camera_ideal - camera_target) * tune.follow_x;
        camera_target += step;
        Camera* camera = main_scene ? main_scene->camera : NULL;
        if (camera){
            camera->SetPosition(camera->GetPosition() + step);
        }
        PlaceCamera();
        return;
    }
    last_camera_mode = camera_mode;

    //What the view shows at this zoom, as half-extents around its target. The camera sits
    //CAMERA_HEIGHT-in-proportion above the target and looks at it, so the target is the centre.
    float height = CAMERA_HEIGHT * camera_distance / CAMERA_DISTANCE;
    float half_h = sqrtf(camera_distance * camera_distance + height * height) *
                   tanf(CAMERA_FOV * 0.5f * ARCHER_DEG2RAD);
    float aspect = (renderer && renderer->height > 0) ? (float)renderer->width / (float)renderer->height
                                                      : 16.0f / 9.0f;
    float half_w = half_h * aspect;
    camera_half_h = half_h;     //what a shake is sized against

    float lead = 0.0f;
    if (stage.vel.x > 0.5f || stage.vel.x < -0.5f){
        lead = (stage.vel.x / ARCHER_RUN_SPEED) * tune.lead * half_w;
    }
    /*
        DOWN, THE CAMERA LEADS HER BY EXACTLY ITS OWN LAG. A lerp closing `follow` of the gap a
        tick trails anything moving at a steady speed by speed * dt / follow - 5.7 units at her top
        fall speed on 0.10, and two screens on the range's old 0.025, which is how the rope
        scene's pits lost her. Aiming that far below her cancels the trail: at a steady fall the
        camera sits on her, it still eases in as she speeds up, and it cannot overshoot a landing,
        because the lead goes with the speed on the tick she stops. Only falling - a jump's rise
        and the rope's swing keep the plain ease.
    */
    float fall_lead = (stage.vel.y < 0.0f) ? stage.vel.y * ARCHER_DT / tune.follow_y : 0.0f;
    camera_ideal = vec3(body.x + lead,body.y + fall_lead,0.0f);

    camera_target.x += (camera_ideal.x - camera_target.x) * tune.follow_x;
    camera_target.y += (camera_ideal.y - camera_target.y) * tune.follow_y;
    camera_target.z = 0.0f;

    /*
        AND SHE NEVER LEAVES THE FRAME. Whatever the rates, the zoom and the speed, her body box is
        held inside `keep_in` of the view's half-extents - the ease decides where the camera goes,
        this only stops it being somewhere she is not. At the default rates it acts only when
        zoomed well in, or when a turn at a sprint swings the lead across.
    */
    float room_x = fmaxf(0.0f,tune.keep_in * half_w - ARCHER_HALF_W);
    float room_y = fmaxf(0.0f,tune.keep_in * half_h - ARCHER_HALF_H);
    camera_target.x = clamp(camera_target.x,stage.pos.x - room_x,stage.pos.x + room_x);
    camera_target.y = clamp(camera_target.y,stage.pos.y - room_y,stage.pos.y + room_y);

    PlaceCamera();
}

//--- Shake and rumble: the cue table's non-sound actions -----------------------------------------

/*
    See the declaration. Trauma adds and is capped at 1; the axes are blended by how much each
    shake brought, so a landing's vertical jolt on top of a kick's sideways one comes out as both.
*/
void ApplicationArcher::AddShake(float amount, float axis_x, float axis_y){
    if (amount <= 0.0f){
        return;
    }
    float before = shake_trauma;
    shake_trauma = fminf(shake_trauma + amount,1.0f);
    float added = shake_trauma - before;
    float total = before + added;
    if (total > 0.0f){
        shake_axis_x = (shake_axis_x * before + fabsf(axis_x) * added) / total;
        shake_axis_y = (shake_axis_y * before + fabsf(axis_y) * added) / total;
    }
}

/*
    Where the shake has the view on this tick. SMOOTH NOISE, not random numbers: a value per
    noise step, eased between with a smoothstep, so the view wobbles at `shake_hz` rather than
    teleporting every frame - and it is the level's tick that is sampled, so the same shake on the
    same tick is the same wobble in every replay. CueHash01 is the hash, a different key per axis.
    Zero in the orbit camera, which is a debugging view, and when the player has turned it off.
*/
vec3 ApplicationArcher::ShakeOffset() const{
    const ArcherCameraTuning& tune = camera_tuning;
    if (shake_trauma <= 0.0f || tune.shake_scale <= 0.0f || camera_mode != ARCHER_CAM_SIDE){
        return vec3(0.0f,0.0f,0.0f);
    }
    float t = (float)stage.ticks * tune.shake_hz / ARCHER_TPS;
    float i = floorf(t);
    float f = t - i;
    f = f * f * (3.0f - 2.0f * f);
    auto noise = [&](uint32_t axis) -> float {
        float a = CueHash01(0x5A4Bu,axis,(uint64_t)i,7);
        float b = CueHash01(0x5A4Bu,axis,(uint64_t)i + 1,7);
        return (a + (b - a) * f) * 2.0f - 1.0f;
    };
    float size = shake_trauma * shake_trauma * tune.shake_max * camera_half_h * tune.shake_scale;
    return vec3(noise(0) * shake_axis_x * size,noise(1) * shake_axis_y * size,0.0f);
}

void ApplicationArcher::Rumble(float low, float high){
    InputController* input = main_scene ? main_scene->inputcontroller : NULL;
    if (!input || !input->HasFocus()){
        return;
    }
    //The motors take 0..65000; the pad's own poll decays them, so the strength is also how long.
    input->lmotor = std::max(input->lmotor,(int)(clamp(low,0.0f,1.0f) * 65000.0f));
    input->rmotor = std::max(input->rmotor,(int)(clamp(high,0.0f,1.0f) * 65000.0f));
}

/*
    Everything that hangs off where the camera is.

    Its own function because a wheel notch has to move the camera on a pass that does NOT tick -
    otherwise zooming in to look at an animation does nothing while the simulation is paused, which
    is exactly when you want to look at one.
*/
void ApplicationArcher::PlaceCamera(){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (camera && camera_mode == ARCHER_CAM_SIDE){
        //Height in proportion to distance, so the zoom keeps one pitch - see CAMERA_HEIGHT.
        float height = CAMERA_HEIGHT * camera_distance / CAMERA_DISTANCE;
        //The shake moves the camera and what it looks at together - a slide of the view, never a
        //turn - and moves nothing else: the backdrop and the sun below stay on camera_target.
        vec3 shake = ShakeOffset();
        camera->SetPosition(vec3(camera_target.x + shake.x,camera_target.y + height + shake.y,camera_distance));
        camera->SetLookAt(camera_target + shake);
        camera->CalculateLookatMatrix();
    }

    /*
        The backdrop, at whatever fraction of the camera's motion its distance implies.

        follow 1 pins it to the camera and it never appears to move, which reads as infinitely far;
        follow 0 nails it to the world and it slides past as fast as the ground does. The thing to
        notice while tuning is that this is the ONLY thing that says how far away it is - the quad's
        actual depth just keeps it behind the geometry.
    */
    if (background_object){
        background_object->SetPosition(vec3(camera_target.x * background_follow,
                                            camera_target.y * background_follow + background_offset_y,
                                            -BACKGROUND_DEPTH));
        background_object->SetScale(vec3(background_base.x * background_scale,
                                         background_base.y * background_scale,1.0f));
    }

    //Drag the sun along with the view. The level is 84 units wide and the shadow ortho is 22, so a
    //sun fixed at the origin would leave everything past the first screen unshadowed - and the
    //fault would look like broken shadows rather than like a light pointed somewhere else.
    //Held as a pointer rather than looked up by name: this runs every tick, and Scene::FindObject
    //is a walk of the whole tree comparing strings.
    if (sun_light){
        //Fit the ortho to how much of the level is on screen - see SUN_SHADOW_EXTENT. The side
        //camera's distance is camera_distance; the orbit keeps its own, as the length to the pivot.
        float distance = camera_distance;
        if (camera && camera_mode == ARCHER_CAM_ORBIT){
            distance = (camera->GetPosition() - camera_target).length();
        }
        sun_light->viewport.zoom = SUN_SHADOW_EXTENT * distance / CAMERA_DISTANCE;

        vec3 target(camera_target.x,camera_target.y,0.0f);
        sun_light->SetPosition(target + SUN_OFFSET);
        sun_light->SetLookAt(target);

        /*
            Snap the light to its own texel grid. It follows an eased camera, so it moves by a
            fraction of a texel nearly every tick, and every edge in the map is re-rasterised a
            little differently each time: the shadow's stair steps crawl along its edges while she
            runs, which is far more visible than the steps themselves. Moving the eye only within
            the light's image plane, and only by whole texels, keeps every texel boundary nailed to
            the same place in the world. The direction never changes, so neither does the grid's
            orientation, and a zoom notch is a single re-snap rather than a continuous shimmer.
        */
        float texel = (2.0f * sun_light->viewport.zoom) / sun_light->viewport.width;
        vec3 left = sun_light->GetLeft();
        vec3 up = sun_light->GetUp();
        vec3 eye = sun_light->GetPosition();
        float l = eye.dot(left);
        float u = eye.dot(up);
        eye += left * (roundf(l / texel) * texel - l) + up * (roundf(u / texel) * texel - u);
        sun_light->SetPosition(eye);
    }
}

/*
    The free orbit: apps/isoanimation's scheme, in the form apps/bomber settled on.

    It starts from wherever the side camera was. The side camera is always looking straight at
    camera_target, so taking that as the pivot means switching modes moves nothing on screen.

    The deltas arrive already drained - see UpdateView for why that has to happen every pass and
    not only while the button is down.
*/
void ApplicationArcher::UpdateOrbitCamera(int dx, int dy, int wheel){
    Camera* camera = main_scene ? main_scene->camera : NULL;
    InputController* input = main_scene ? main_scene->inputcontroller : NULL;
    if (!camera || !input){
        return;
    }
    //Focus and the panels for the same reasons as the wheel in UpdateView: a drag across a slider
    //is not a drag of the view.
    bool f_ours = input->HasFocus() && !UIWantsMouse();

    if (f_ours && input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        if (input->IsKeyDown(INPUT_SHIFT)){
            //Pan, carrying the pivot with the camera so the viewing angle is left alone.
            vec3 d = camera->MoveSidewaysBy(-dx / 100.0f);
            d += camera->MoveUpBy(dy / 100.0f);
            camera_target += d;
            //Kept as an offset from her too, so a panned view goes on following her.
            orbit_follow_offset += d;
        }else{
            //Up/down turns the camera round its own left axis, then re-aims at the pivot keeping
            //the current up - which is what allows a full turn over the top.
            vec3 p = camera->GetPosition() - camera_target;
            quat q(camera->GetLeft(),-dy / 50.0f);
            p = q * p;
            camera->SetPosition(p + camera_target);
            vec3 up = camera->GetUp();
            camera->SetLookAt(camera_target,&up);

            //Left/right turns round world Y, the look direction with it.
            p = camera->GetPosition() - camera_target;
            q.set_rotation(vec3(0,1,0),-dx / 50.0f);
            p = q * p;
            camera->SetPosition(p + camera_target);
            camera->RotateBy(q);
        }
    }

    /*
        The wheel dollies toward the pivot by the same 10% a notch as the side camera zooms, so the
        two feel the same, and within the same limits. Along the line to the pivot, so the view
        direction does not change. The limits are there because a proportional step compounds:
        without them a few seconds of scrolling out leaves the level a speck on the backdrop.
    */
    if (wheel != 0 && f_ours){
        vec3 offset = camera->GetPosition() - camera_target;
        float distance = offset.length();
        if (distance > 0.0001f){
            float wanted = distance * powf(1.0f - CAMERA_ZOOM_PER_NOTCH,(float)wheel);
            wanted = clamp(wanted,CAMERA_DISTANCE_MIN,CAMERA_DISTANCE_MAX);
            camera->SetPosition(camera_target + offset * (wanted / distance));
        }
    }
}

void ApplicationArcher::PublishSnapshot(){
    ArcherSnapshot s;
    s.tick = main_scene->GetPhysicsTick();
    s.stage_ticks = stage.ticks;
    s.level = stage.GetLevel();
    s.x = stage.pos.x;
    s.y = stage.pos.y;
    s.vx = stage.vel.x;
    s.vy = stage.vel.y;
    s.facing = stage.facing;
    s.mode = stage.mode;
    s.f_on_ground = stage.f_on_ground;
    s.coyote_ticks = stage.coyote_ticks;
    s.kick_ticks = stage.kick_ticks;
    s.kick_kind = stage.kick_kind;
    s.bow_mode = stage.bow_mode;
    s.draw_ticks = stage.draw_ticks;
    s.draw_power = stage.DrawPower();
    s.aim_deg = stage.ShotAimDeg();     //what the arrow is aimed at, sway included
    s.aim_sway_deg = stage.AimSwayDeg();
    if (archer_model){
        s.aim_drawn_deg = archer_model->aim_drawn_deg;
        s.aim_weight = archer_model->aim_weight;
    }
    s.aim_neutral_deg = archer_model ? archer_model->aim_pose_deg : 0.0f;
    s.upper_clip = puppet.choice.upper_clip;
    s.upper_weight = puppet.upper_weight;
    s.kneel_phase = (stage.mode == MODE_KNEEL) ? stage.kneel_phase : -1;
    s.body_height = stage.BodyHeight();
    if (bow_rig.arrow.object){
        vec3 nock = bow_rig.arrow.object->GetWorldPosition();
        s.nock_fwd = (nock.x - stage.pos.x) * stage.facing;
        s.nock_up = nock.y - stage.pos.y;
    }
    v2 anchor = stage.AnchorPosition();
    s.anchor_fwd = (anchor.x - stage.pos.x) * stage.facing;
    s.anchor_up = anchor.y - stage.pos.y;
    s.string_draw = bow_draw_shown;
    s.f_arrow_on_string = f_arrow_nocked;
    s.hand_off_string = bow_rig.hand_off_string;
    s.f_arrow_in_hand = bow_rig.f_arrow_in_hand;
    s.hand_off_quiver = bow_rig.hand_off_quiver;
    s.live_arrows = stage.NumLiveArrows();
    s.arrows_shot = stage.arrows_shot;
    s.arrows_hit_blocks = stage.arrows_hit_blocks;
    s.f_paused = main_scene->IsPhysicsPaused();

    s.clip = playing_clip;
    s.blend_clip = puppet.choice.blend_clip;
    s.blend = puppet.choice.blend;
    s.blend_phase_offset = puppet.choice.blend_phase_offset;
    s.clip_rate = puppet.choice.rate;
    s.clip_wanted_rate = puppet.choice.wanted_rate;
    s.f_clip_placeholder = puppet.choice.f_placeholder;
    s.model_yaw = model_yaw_drawn;
    s.model_roll = model_roll_drawn;
    s.rope_joint_gap = rope_joint_gap;
    s.rope_hands_off = rope_hands_off;
    s.leg_weight = archer_model ? archer_model->leg_weight : 0.0f;
    s.leg_lead_deg = archer_model ? archer_model->leg_lead_deg : 0.0f;
    for (int leg = 0; leg < 2; leg++){
        s.leg_swing_deg[leg] = archer_model ? archer_model->leg_swing_deg[leg] : 0.0f;
        s.leg_knee_deg[leg] = archer_model ? archer_model->leg_knee_deg[leg] : 0.0f;
    }
    s.climb_pinned_time = puppet.choice.pinned_time;
    s.climb_lift_posed = climb_lift_posed;
    s.landing_forecast = landing_forecast;
    s.spring_on = stage.spring_on;
    s.launch_lift = stage.launch_lift;
    s.slide_accel = stage.SlideAccel();
    s.ramp_on = stage.ramp_on;
    s.slope_deg = stage.SlopeUnderFeetDeg();
    s.spring_cue = (spring_cue_plant >= 0) ? spring_cue : -1.0f;
    s.spring_boost = stage.SpringBoostNow();
    s.spring_boost_peak = spring_cue_peak;
    s.stomp_ticks = stage.stomp_ticks;
    s.spring_air_ticks = stage.spring_air_ticks;
    s.branch_on = (stage.f_on_ground) ? stage.branch_on : -1;
    s.lean_deg = stage.lean * 57.2957795f;
    s.lean_rate_deg = stage.lean_rate * 57.2957795f;
    s.balance_danger = stage.BalanceDanger();
    s.hang_branch = (stage.mode == MODE_HANG || stage.mode == MODE_CLIMB) ? stage.hang_branch : -1;
    s.spring_plants.resize(stage.spring_plants.size());
    for (size_t i = 0; i < stage.spring_plants.size(); i++){
        const StageSpringPlant& p = stage.spring_plants[i];
        float unit = (p.kind == SPRING_LEAF) ? 57.2957795f : 1.0f;
        s.spring_plants[i].kind = p.kind;
        s.spring_plants[i].q = p.q * unit;
        s.spring_plants[i].qd = p.qd * unit;
    }
    s.fall_weight = puppet.fall_weight;
    s.air_hip_weight = air_hip_weight;
    s.landing_forecast_us = landing_forecast_us;
    s.rope_grip_s = (stage.mode == MODE_ROPE) ? rope_grip_s : -1.0f;
    for (int h = 0; h < 2; h++){
        s.rope_hand_s[h] = rope_hand_s[h];
        s.rope_hand_off[h] = rope_hand_off[h];
    }
    //Here, on the physics thread, which is the one that starts them - not in the MCP handler.
    s.sounds_playing = soundsystem ? soundsystem->GetNumPlaying() : -1;
    s.shake_trauma = shake_trauma;
    {
        vec3 shake = ShakeOffset();
        s.shake_dx = shake.x;
        s.shake_dy = shake.y;
    }
#ifdef USE_SOUND
    if (soundsystem){
        soundsystem->ListVoices(s.voices);
    }
#endif
    s.sound_volume = sound_volume;
    s.arrows_on_props = 0;
    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        if (arrow_stuck[i].prop){ s.arrows_on_props++; }
    }
    s.anim_source = anim_source;

    for (size_t i = 0; i < prop_views.size(); i++){
        const PropView& view = prop_views[i];
        if (view.kind != PROP_TARGET || !view.object || view.f_lost){
            continue;
        }
        ArcherSnapshot::TargetView t;
        vec3 p = view.object->GetWorldPosition();
        t.x = p.x;
        t.y = p.y;
        vec3 up = view.object->GetWorldUp();
        t.tilt_deg = todegrees(acosf(clamp(up.y,-1.0f,1.0f)));
        t.f_knocked = view.f_knocked;
        t.hits = view.hits;
        t.f_floating = view.f_floating;
        t.f_gravity = view.object->GetPhysics() ? view.object->GetPhysics()->IsGravityEnabled() : true;
        t.linear_damping = view.object->GetPhysics() ? view.object->GetPhysics()->GetLinearDamping() : 0.0f;
        t.angular_damping = view.object->GetPhysics() ? view.object->GetPhysics()->GetAngularDamping() : 0.0f;
        t.f_stand = (view.variant == TARGET_STAND);
        t.score = view.score;
        s.targets.push_back(t);
    }
    s.archery_score = archery_score;
    s.archery_last_points = archery_last_points;
    for (size_t i = 0; i < prop_views.size(); i++){
        const PropView& view = prop_views[i];
        if (view.kind != PROP_STRAWMAN || !view.object || !view.spring){
            continue;
        }
        const SpringHinge* h = view.spring;
        ArcherSnapshot::StrawView v;
        v.x = view.object->GetWorldPosition().x;
        v.angle_deg = todegrees(h->GetAngle());
        v.joint_angle_deg = todegrees(h->GetJointAngle());
        v.rate = h->GetRate();
        v.kicks = view.score;
        v.arrows = view.hits;
        v.hz = h->hz;
        v.inertia = h->inertia;
        v.stiffness = h->stiffness;
        v.gravity_stiffness = h->gravity_stiffness;
        v.damping = h->damping;
        s.strawmen.push_back(v);
    }
    s.kick_score = kick_score;

    for (int i = 0; i < ARROW_MAX_LIVE; i++){
        const Arrow& a = stage.arrows[i];
        if (!a.f_live){
            continue;
        }
        ArcherSnapshot::ArrowView av;
        av.x = a.pos.x;
        av.y = a.pos.y;
        av.vx = a.vel.x;
        av.vy = a.vel.y;
        av.f_stuck = a.f_stuck;
        s.arrows.push_back(av);
    }

    /*
        Where a shot loosed right now would end up.

        The single most useful number for a program trying to hit something: with it, a script can
        solve for the angle by bisection - set an aim, read the landing point, adjust - instead of
        loosing an arrow and waiting to see. Costs one run of the same integrator the arrow uses.
    */
    v2 arc[AIM_ARC_POINTS];
    int n = stage.PredictArc(arc,AIM_ARC_POINTS);
    //The same truncation the on-screen arc gets, so a script reading this number and a player
    //reading the beads are told the same thing.
    n = TruncateArcAgainstProps(arc,n);
    if (n > 0){
        s.predicted_x = arc[n - 1].x;
        s.predicted_y = arc[n - 1].y;
        s.f_predicted = true;
    }

    std::lock_guard<std::mutex> lock(snapshot_mutex);
    snapshot = s;
}

//--- MCP ----------------------------------------------------------------------------------------
#ifdef USE_MCP

void ApplicationArcher::WaitTicks(int ticks){
    if (ticks <= 0){
        return;
    }
    /*
        Counted on the scene the wait STARTED on, and over when the live scene changes. Each scene
        keeps its own tick count and only the live one advances, so a wait that re-read main_scene
        across a switch - archer_hold 'continue' off the title, whose count is hundreds ahead of
        the world's - sat out the whole eight-second cap.
    */
    Scene* scene = main_scene;
    uint64_t target = scene->GetPhysicsTick() + (uint64_t)ticks;
    for (int waited = 0; waited < 8000 && main_scene == scene && scene->GetPhysicsTick() < target; waited += 4){
        Sleep(4);
    }
}

json ApplicationArcher::BuildStateJson(){
    ArcherSnapshot s;
    {
        std::lock_guard<std::mutex> lock(snapshot_mutex);
        s = snapshot;
    }

    static const char* mode_names[] = { "ground","air","hang","climb","rope","kneel","getup" };

    json targets = json::array();
    for (size_t i = 0; i < s.targets.size(); i++){
        targets.push_back(json{
            {"x",s.targets[i].x},
            {"y",s.targets[i].y},
            {"tilt_deg",s.targets[i].tilt_deg},
            {"knocked",s.targets[i].f_knocked},
            {"hits",s.targets[i].hits},
            {"stand",s.targets[i].f_stand},
            {"score",s.targets[i].score},
            {"floating",s.targets[i].f_floating},
            {"gravity",s.targets[i].f_gravity},
            {"damping",json::array({s.targets[i].linear_damping,s.targets[i].angular_damping})}
        });
    }
    json strawmen = json::array();
    for (size_t i = 0; i < s.strawmen.size(); i++){
        const ArcherSnapshot::StrawView& v = s.strawmen[i];
        strawmen.push_back(json{
            {"x",v.x},
            //Measured off the bodies, and as rp3d's joint reads it: the two should agree, and the
            //limits are set assuming they do.
            {"angle_deg",v.angle_deg},
            {"joint_angle_deg",v.joint_angle_deg},
            {"rate",v.rate},
            {"kicks",v.kicks},
            {"arrows",v.arrows},
            //What SpringHinge made of the tuning - see core/physics/SpringHinge.h.
            {"spring",json{{"hz",v.hz},{"inertia",v.inertia},{"k",v.stiffness},
                           {"k_gravity",v.gravity_stiffness},{"c",v.damping}}}
        });
    }
    json arrows = json::array();
    for (size_t i = 0; i < s.arrows.size(); i++){
        arrows.push_back(json{
            {"x",s.arrows[i].x},
            {"y",s.arrows[i].y},
            {"vx",s.arrows[i].vx},
            {"vy",s.arrows[i].vy},
            {"stuck",s.arrows[i].f_stuck}
        });
    }

    json result = json{
        {"tick",s.tick},
        {"stage_ticks",s.stage_ticks},
        //Stage::PredictLanding as of this tick: `ticks` from now, 1 being the next one.
        {"landing_forecast",json{
            {"lands",s.landing_forecast.f_lands},
            {"caught",s.landing_forecast.f_caught},
            {"ticks",s.landing_forecast.ticks},
            {"speed",s.landing_forecast.speed},
            {"x",s.landing_forecast.pos.x},
            {"y",s.landing_forecast.pos.y},
            {"cost_us",s.landing_forecast_us}}},
        //The bounce pad and the leaf: which she stands on (-1 none), what a throw gave her, her pull
        //down a steep leaf, and each spring - a pad's cap height from rest, a leaf's angle in degrees.
        {"spring_plants",[&](){
            json list = json::array();
            for (const ArcherSnapshot::SpringPlantView& v : s.spring_plants){
                list.push_back(json{{"kind",(v.kind == SPRING_LEAF) ? "leaf" : "pad"},
                                    {"q",v.q},{"rate",v.qd}});
            }
            //cue: how well timed a jump now would be, 0..1 (-1 none) - what the plant's colour shows.
            return json{{"standing_on",s.spring_on},{"launch_lift",s.launch_lift},
                        {"slide_accel",s.slide_accel},{"cue",s.spring_cue},
                        {"boost",s.spring_boost},{"boost_peak",s.spring_boost_peak},
                        {"stomp_ticks",s.stomp_ticks},{"air_ticks",s.spring_air_ticks},
                        {"plants",list}};
        }()},
        //The slide: the ramp she is on (-1 none), the slope under her feet in degrees (+ rising to
        //the right, whatever it is - leaf, branch or ramp), and the pull down it (0 when it holds her).
        {"slide",json{{"ramp",s.ramp_on},{"slope_deg",s.slope_deg},{"accel",s.slide_accel}}},
        //Balance on a branch: which (-1 none), her lean (+ away from the camera) and its rate in
        //degrees, and how near falling, 0..1 - what the gauge beside her shows.
        {"balance",json{{"branch",s.branch_on},{"lean_deg",s.lean_deg},{"lean_rate_deg",s.lean_rate_deg},
                        {"danger",s.balance_danger},{"hanging_from",s.hang_branch}}},
        {"level",(s.level == STAGE_LEVEL_RANGE) ? "range" : (s.level == STAGE_LEVEL_ROPE) ? "rope" : "main"},
        {"archer",json{
            {"x",s.x},{"y",s.y},{"vx",s.vx},{"vy",s.vy},
            {"facing",(s.facing > 0.0f) ? "right" : "left"},
            {"mode",mode_names[(s.mode >= 0 && s.mode <= MODE_GETUP) ? s.mode : 0]},
            {"on_ground",s.f_on_ground},
            {"kneel",(s.kneel_phase == KNEEL_LOWERING) ? json("lowering") :
                     (s.kneel_phase == KNEEL_HELD) ? json("held") :
                     (s.kneel_phase == KNEEL_RISING) ? json("rising") : json(nullptr)},
            {"body_height",s.body_height},
            {"coyote_ticks",s.coyote_ticks},
            //Which of the three kicks, and how far through it: null when she is not kicking.
            {"kick",(s.kick_ticks > 0) ? json{
                {"name",KICK_SPECS[s.kick_kind].name},
                {"ticks",s.kick_ticks},
                {"of",KICK_SPECS[s.kick_kind].ticks},
                {"active_from",KICK_SPECS[s.kick_kind].active_from},
                {"active_to",KICK_SPECS[s.kick_kind].active_to}} : json(nullptr)}
        }},
        {"bow",json{
            {"drawing",s.bow_mode == BOW_DRAWING},
            {"draw_ticks",s.draw_ticks},
            {"draw_ticks_full",BOW_DRAW_TICKS},
            {"draw_power",s.draw_power},
            {"aim_deg",s.aim_deg},
            {"aim_sway_deg",s.aim_sway_deg},
            //The aim override's check - see ArcherSnapshot::aim_drawn_deg.
            {"aim_drawn_deg",s.aim_drawn_deg},
            {"aim_error_deg",s.aim_drawn_deg - s.aim_deg},
            {"aim_weight",s.aim_weight},
            //The live neutral: where the clips alone point the bow, before the aim turns her.
            {"aim_pose_deg",s.aim_neutral_deg},
            {"upper_clip",(s.upper_clip >= 0 && s.upper_clip < CLIP_COUNT) ? json(ARCHER_CLIPS[s.upper_clip].name)
                                                                           : json(nullptr)},
            {"upper_weight",s.upper_weight},
            {"nock",json{{"fwd",s.nock_fwd},{"up",s.nock_up}}},
            {"anchor",json{{"fwd",s.anchor_fwd},{"up",s.anchor_up}}},
            {"string_draw",s.string_draw},
            {"arrow_on_string",s.f_arrow_on_string},
            {"hand_off_string",s.hand_off_string},
            {"arrow_in_hand",s.f_arrow_in_hand},
            {"hand_off_quiver",s.hand_off_quiver},
            {"predicted_landing",s.f_predicted ? json{{"x",s.predicted_x},{"y",s.predicted_y}}
                                               : json(nullptr)}
        }},
        {"arrows_shot",s.arrows_shot},
        {"arrows_in_blocks",s.arrows_hit_blocks},
        {"live_arrows",arrows},
        {"targets",targets},
        /*
            READ LIVE, NOT OUT OF THE SNAPSHOT - the snapshot cannot answer this question at all.

            PublishSnapshot runs at the end of RunSimulationTick, and RunSimulationTick only runs
            on a pass that TICKS. So while the simulation is paused nothing is published and the
            snapshot keeps reporting whatever was true on the last tick that ran - which is, by
            construction, a tick on which it was NOT paused. `paused` could therefore only ever
            read false, however long the game had been sitting still. That cost a long detour
            reading a stalled tick counter as a hung physics thread; everything resumed the moment
            the pause was simply switched off.

            Everything else here stays snapshot-served, which is the point of the snapshot. This
            one field is about the simulation rather than about the game, and it is the one the
            snapshot structurally cannot carry.
        */
        {"paused",main_scene->IsPhysicsPaused()},
        /*
            What the MODEL is doing, which is not the same question as what the archer is doing.

            `wanted_rate` against `rate` is the one worth reading: it is how fast the clip would
            have to play to keep her feet on the ground, against how fast it is allowed to. Any
            gap between them is foot slide, and having it as a number means a run across the level
            can be measured rather than watched.
        */
        {"animation",json{
            {"source",(s.anim_source == ANIM_FROM_GAME) ? "game" :
                      (s.anim_source == ANIM_FROM_PANEL) ? "panel" : "clip"},
            {"clip",(s.clip >= 0 && s.clip < CLIP_COUNT) ? json(ARCHER_CLIPS[s.clip].name)
                                                         : json(nullptr)},
            //The blend space. A non-null blend_clip means two cycles are playing at once, mixed
            //by `blend`, with the follower shifted by `blend_phase_offset` to put their footfalls
            //together - see Puppet::Choose.
            {"blend_clip",(s.blend_clip >= 0 && s.blend_clip < CLIP_COUNT)
                          ? json(ARCHER_CLIPS[s.blend_clip].name) : json(nullptr)},
            {"blend",s.blend},
            {"blend_phase_offset",s.blend_phase_offset},
            {"rate",s.clip_rate},
            {"wanted_rate",s.clip_wanted_rate},
            {"placeholder",s.f_clip_placeholder},
            {"model_yaw_deg",s.model_yaw},
            //Zero everywhere but the rope, where it is the lean the solver is swinging her at.
            {"model_roll_deg",s.model_roll},
            //On the rope only (-1 off it): how far the joint has opened, and how far her DRAWN
            //hands are from the rope's end of it. See BuildRopeAttachMarkers.
            {"rope_joint_gap",s.rope_joint_gap},
            {"rope_hands_off",s.rope_hands_off},
            //The climb's pinned playhead (-1 when not pinned) and the lift the model is lowered by,
            //which belongs to the playhead pinned the tick before - see climb_lift_posed.
            {"climb_pinned_time",s.climb_pinned_time},
            {"climb_lift",s.climb_lift_posed},
            {"fall_pose",s.fall_weight},
            {"air_hip_hold",s.air_hip_weight},
            //The loose legs: how much of the chain is on, the pump's lead, and per leg the thigh's
            //swing off the clip's pose and the knee's bend (+ the way a knee folds), degrees.
            {"legs",{{"weight",s.leg_weight},{"lead_deg",s.leg_lead_deg},
                     {"left",{{"swing_deg",s.leg_swing_deg[0]},{"knee_deg",s.leg_knee_deg[0]}}},
                     {"right",{{"swing_deg",s.leg_swing_deg[1]},{"knee_deg",s.leg_knee_deg[1]}}}}},
            //Where the joint holds the rope (distance down it), and each hand against the drawn
            //rope: distance down it and distance off it. A gripping hand's `s` stays put while she
            //climbs; if it creeps, the hand is sliding. -1 off the rope.
            {"rope_grip_s",s.rope_grip_s},
            {"rope_hand_left",{{"s",s.rope_hand_s[0]},{"off",s.rope_hand_off[0]}}},
            {"rope_hand_right",{{"s",s.rope_hand_s[1]},{"off",s.rope_hand_off[1]}}}
        }},
        //Arrows currently riding a prop rather than sitting in the world. Invisible until
        //something moves, which is exactly why it is worth a line: an arrow pinned to a prop it
        //is no longer in looks identical to a correct one until that prop is kicked.
        {"arrows_on_props",s.arrows_on_props},
        //How many sounds are audible, as of the last tick - the only way to tell over MCP that a
        //sound fired, since a successful Play logs nothing. -1 with no sound system.
        {"sounds_playing",s.sounds_playing},
        //The camera shake: trauma 0..1 (it decays over camera_tuning.shake_ticks) and how far it
        //has the view moved this tick, in world units. What shakes is the cue table's.
        {"shake",{ {"trauma",s.shake_trauma}, {"dx",s.shake_dx}, {"dy",s.shake_dy} }},
        //Archery stands' points this level, and what the last arrow into one scored (0 = a leg,
        //-1 = none yet). See StandRingPoints.
        {"archery_score",s.archery_score},
        {"archery_last_points",s.archery_last_points},
        //Kicks landed on straw men this level - one point each - and each straw man's swing.
        {"kick_score",s.kick_score},
        {"strawmen",strawmen}
    };
    return result;
}

void ApplicationArcher::RegisterMCPTools(){
    //Registered from Init(). The server only starts accepting requests after Init() returns, so
    //registration can never race a client's tools/list.

    MCPServer::Get()->RegisterTool("archer_state",
        "The whole game state: where the archer is and what they are doing, the bow's draw and aim, "
        "every live arrow, every target and whether it has been knocked over, and - the useful one - "
        "where an arrow loosed RIGHT NOW would land, under 'bow.predicted_landing'. That last field "
        "is computed with the same integrator the arrow flies on, so a script can solve for an aim "
        "angle by bisection instead of shooting and looking. Read from a snapshot the physics thread "
        "publishes at the end of every tick, so it never disturbs the game it is measuring. The "
        "level runs from x -12 to 176 with the ground surface at y 0; the game runs at 60 ticks a "
        "second and every duration is a tick count.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the current frame, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_run",
        "Hold left or right for a number of SIMULATION TICKS and block until it has played out. "
        "This is how a program plays: the hold emits ordinary input events, so the simulation "
        "cannot tell it from a key. The archer accelerates over about 6 ticks and tops out at 9 "
        "units a second, so a short hold nudges and a long one sprints. While the simulation is "
        "paused the hold does not count down - use sim_step. Pass 'amount' to push the STICK that "
        "far instead of pressing a key, which is the only way to ask for a speed between standing "
        "and a full sprint - 0.35 is a walk, 0.6 a jog. The rules read the magnitude, so the speed "
        "is amount x 9 units a second.",
        json{
            {"type","object"},
            {"properties", {
                {"direction", {{"type","string"},{"description","'left' or 'right'"}}},
                {"ticks", {{"type","number"},{"description","simulation ticks to hold, default 30, capped at 600"}}},
                {"amount", {{"type","number"},{"description","stick deflection 0..1; omit for a full key press"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"direction"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            std::string dir = args.value("direction",std::string("right"));
            int ticks = (int)clamp(args.value("ticks",30.0f),0.0f,600.0f);
            float sign = (dir == "left") ? -1.0f : 1.0f;
            /*
                A KEY OR A STICK, and they are genuinely different requests rather than two
                spellings of one. A key can only ask for everything; the stick is the only way to
                ask for a speed in between, which is what the locomotion blend space exists to
                cover and what nothing could reach before this.
            */
            if (args.contains("amount")){
                float amount = clamp(args.value("amount",1.0f),0.0f,1.0f);
                input->HoldAxis(INPUT_ARCHER_MOVE,sign * amount,(uint32_t)ticks);
            }else{
                uint32_t action = (dir == "left") ? INPUT_ARCHER_LEFT : INPUT_ARCHER_RIGHT;
                input->HoldKey(action,(uint32_t)ticks);
            }
            WaitTicks(ticks + 2);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_jump",
        "Jump, holding the key for a number of ticks. THE HOLD LENGTH IS THE JUMP HEIGHT: releasing "
        "early cuts the climb, so 3 ticks is a hop and 25 ticks is the full 3.2-unit jump. Combine "
        "with archer_run to jump while moving - a running jump clears about 6.5 units of gap, which "
        "is what the two gaps in this level are built around.",
        json{
            {"type","object"},
            {"properties", {
                {"hold_ticks", {{"type","number"},{"description","ticks to hold jump, default 25 (a full jump), capped at 120"}}},
                {"run", {{"type","string"},{"description","optionally run 'left' or 'right' for the whole jump"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            int hold = (int)clamp(args.value("hold_ticks",25.0f),1.0f,120.0f);
            std::string run = args.value("run",std::string(""));
            //The run is held for the whole flight, not just the launch: air control is 0.6 of
            //ground control, so letting go mid-jump lands noticeably shorter.
            int flight = hold + 50;
            if (run == "left"){
                input->HoldKey(INPUT_ARCHER_LEFT,(uint32_t)flight);
            }else if (run == "right"){
                input->HoldKey(INPUT_ARCHER_RIGHT,(uint32_t)flight);
            }
            input->HoldKey(INPUT_ARCHER_JUMP,(uint32_t)hold);
            WaitTicks(flight + 2);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_aim",
        "Point the bow at an angle, in degrees, RELATIVE TO THE WAY THE ARCHER IS FACING: 0 is "
        "straight ahead, positive is up, and the range is -85 to +85. Facing left mirrors it, so "
        "the same angle means the same shot in both directions. Takes effect at the top of the next "
        "tick. Read 'bow.predicted_landing' back from archer_state to see where that angle puts an "
        "arrow before spending one.",
        json{
            {"type","object"},
            {"properties", {
                {"degrees", {{"type","number"},{"description","-85 .. +85, relative to facing; + is up"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"degrees"})}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;
            cmd.type = ARCHER_CMD_AIM;
            cmd.value[0] = clamp(args.value("degrees",0.0f),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
            main_scene->SubmitCommand(cmd);
            WaitTicks(2);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_draw",
        "Start drawing the bow and RETURN IMMEDIATELY, without waiting for the loose. The draw runs "
        "for hold_ticks and then looses on its own, so this is how to catch the game mid-draw - "
        "with the aim arc on screen - for a screenshot. Every other tool here blocks until its "
        "action has played out, which is right for measuring and useless for posing.",
        json{
            {"type","object"},
            {"properties", {
                {"hold_ticks", {{"type","number"},{"description","ticks to hold the draw before it looses, default 240 (4 seconds), capped at 600"}}},
                {"degrees", {{"type","number"},{"description","optional: set the aim angle first, -85 .. +85"}}},
                {"settle_ticks", {{"type","number"},{"description","ticks to wait before returning, so the arc is drawn, default 8"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input || !main_scene){
                return json{ {"error","no input controller"} };
            }
            if (args.contains("degrees")){
                SimCommand cmd;
                cmd.type = ARCHER_CMD_AIM;
                cmd.value[0] = clamp(args.value("degrees",0.0f),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
                main_scene->SubmitCommand(cmd);
                WaitTicks(2);
            }
            int hold = (int)clamp(args.value("hold_ticks",240.0f),1.0f,600.0f);
            int settle = (int)clamp(args.value("settle_ticks",8.0f),0.0f,120.0f);
            input->HoldKey(INPUT_ARCHER_DRAW,(uint32_t)hold);
            //Just long enough for the draw to start and SyncAimArc to place the beads - NOT for
            //the hold to finish, which is the whole point of this tool.
            WaitTicks(settle);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_shoot",
        "Draw the bow for a number of ticks and loose. A full draw is 36 ticks and gives an arrow "
        "46 units a second. The arrow is on the string only from tick 23 (BOW_NOCK_TICKS): letting "
        "go before that CANCELS the draw and nothing is shot, and at tick 23 the shot is the "
        "minimum 23.5 units a second. Optionally sets the aim first, so "
        "one call is one complete, measurable shot. Returns once the arrow is away - poll "
        "archer_state, or pass wait_ticks, to see where it ended up. Arrows fly under their own "
        "gravity (24, against the archer's 42) and are swept against the level, so a fast arrow "
        "cannot pass through a thin wall.",
        json{
            {"type","object"},
            {"properties", {
                {"draw_ticks", {{"type","number"},{"description","ticks to hold the draw, default 36 (full), capped at 120"}}},
                {"degrees", {{"type","number"},{"description","optional: set the aim angle first, -85 .. +85"}}},
                {"wait_ticks", {{"type","number"},{"description","extra ticks to let the arrow fly before reporting, default 60"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input || !main_scene){
                return json{ {"error","no input controller"} };
            }
            if (args.contains("degrees")){
                SimCommand cmd;
                cmd.type = ARCHER_CMD_AIM;
                cmd.value[0] = clamp(args.value("degrees",0.0f),BOW_AIM_MIN_DEG,BOW_AIM_MAX_DEG);
                main_scene->SubmitCommand(cmd);
                WaitTicks(2);
            }
            int draw = (int)clamp(args.value("draw_ticks",(float)BOW_DRAW_TICKS),1.0f,120.0f);
            int fly  = (int)clamp(args.value("wait_ticks",60.0f),0.0f,900.0f);
            input->HoldKey(INPUT_ARCHER_DRAW,(uint32_t)draw);
            //The release is read with WasKeyReleased, which only becomes true on the tick AFTER
            //the hold ends - so the wait has to clear the hold plus that edge before the arrow
            //even exists. Since backlog item 84 these are stepped ticks too, so this works under
            //sim_step as well as free-running.
            WaitTicks(draw + 3 + fly);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_hold",
        "Hold any one control down for a number of SIMULATION TICKS and block until it has played "
        "out. The general form of archer_run and archer_jump, and the way to reach the controls "
        "that have no tool of their own: 'down' drops through a one-way platform and lets go of a "
        "ledge, 'action' and 'knife' are wired but not yet used. Several of these can be layered by "
        "calling with wait false and then holding the next one. Actions: left, right, down, jump, "
        "draw, kick, kneel (a toggle: any hold is one press), action, knife, and the arrow keys "
        "'up' / 'aim_down' - which tilt the aim, and on the rope CLIMB it. Held while 'kick' is "
        "pressed they choose the kick: 'aim_down' the low push kick (Kick_Front_2), 'up' the high "
        "kick (Kick_Front_3); archer_state's archer.kick says which one is running. 'continue' "
        "dismisses the title screen the app starts on - nothing else in the level moves until it has - "
        "and goes back to the level it was entered from. 'menu' (Escape) goes to the title from a "
        "level, pausing it; ON THE TITLE, 'menu' EXITS THE APP.",
        json{
            {"type","object"},
            {"properties", {
                {"action", {{"type","string"},{"description","left, right, down, jump, draw, kick, kneel, action, knife, up, aim_down, continue or menu"}}},
                {"ticks", {{"type","number"},{"description","simulation ticks to hold it, default 20, capped at 600"}}},
                {"wait", {{"type","boolean"},{"description","block until the hold has finished, default true; false returns at once so another hold can be layered on top"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"action"})}
        },
        [this](const json& args) -> json {
            InputController* input = main_scene ? main_scene->inputcontroller : NULL;
            if (!input){
                return json{ {"error","no input controller"} };
            }
            std::string name = args.value("action",std::string(""));
            uint32_t action = 0;
            if (name == "left"){        action = INPUT_ARCHER_LEFT;   }
            else if (name == "right"){  action = INPUT_ARCHER_RIGHT;  }
            else if (name == "down"){   action = INPUT_ARCHER_DOWN;   }
            else if (name == "jump"){   action = INPUT_ARCHER_JUMP;   }
            else if (name == "draw"){   action = INPUT_ARCHER_DRAW;   }
            else if (name == "kick"){   action = INPUT_ARCHER_KICK;   }
            else if (name == "kneel"){  action = INPUT_ARCHER_KNEEL;  }
            else if (name == "action"){ action = INPUT_ARCHER_ACTION; }
            else if (name == "knife"){  action = INPUT_ARCHER_KNIFE;  }
            //The arrow keys: the aim's tilt - and on the rope, climbing. `down` is already S.
            else if (name == "up"){       action = INPUT_ARCHER_AIM_UP;   }
            else if (name == "aim_down"){ action = INPUT_ARCHER_AIM_DOWN; }
            //The title screen's click - see UpdateTitle.
            else if (name == "continue"){ action = INPUT_ARCHER_CONTINUE; }
            //Escape: to the title from a level - and out of the app from the title.
            else if (name == "menu"){     action = INPUT_ARCHER_MENU;     }
            else{
                return json{ {"error","unknown action '" + name + "'; expected left, right, down, jump, draw, kick, kneel, action, knife, up, aim_down, continue or menu"} };
            }
            int ticks = (int)clamp(args.value("ticks",20.0f),0.0f,600.0f);
            input->HoldKey(action,(uint32_t)ticks);
            if (args.value("wait",true)){
                //Plus two, so the RELEASE edge has been read by a tick as well as the hold - an
                //action read with WasKeyReleased is not delivered until then.
                WaitTicks(ticks + 2);
            }
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_place",
        "Put the archer at (x, y) and clear their movement state - which also ends the level-entry "
        "get-up if it is still playing. A DEVELOPMENT TOOL: the level "
        "runs from x -12 to 176 with two gaps in it, and iterating on one part of it should not mean "
        "flying the whole approach by script every time. Useful landmarks: the ground surface is "
        "y 0, the start is (-6, 0.9), the grabbable-only ledge stands at x 44..48 with its lip at "
        "4.2 (jump from x 43.1 to catch it), the cracked wall is at x 57 and the brick wall at "
        "x 49.5, and the tree stands at x 80 (arms at 2.5 right, 5.0 left, 7.5 right; stand under "
        "the first at x 81.45 and jump), the bounce pad's cap is at x 105 (top 1.2; drop onto it from "
        "(105, 5) to test it), and the leaf grows right from the shelf at x 110..118, top 7.5 (stand on "
        "its end at (116, 8.4)), a high branch runs from the canopy's end (133, 13.0) to a perch at x 145..152, "
        "and a low practice branch runs at height 2.6 between stumps at x 155..157 and 168..170 (stand on "
        "the left stump at (156, 3.5) and walk right; Up/Down keep her balance, and going over hangs her from it: Jump climbs back up, Down lets go). y is the archer's CENTRE, so standing on the ground is y 0.9. DO NOT PLACE INSIDE "
        "SOLID GEOMETRY: the archer is ejected out of it on the next tick, and out of a tall block "
        "that means upward onto its roof - which looks like the placement having worked and then "
        "the archer walking over things it should have been stopped by. x 44 is inside the ledge; "
        "43.1 is beside it.",
        json{
            {"type","object"},
            {"properties", {
                {"x", {{"type","number"},{"description","world x"}}},
                {"y", {{"type","number"},{"description","world y of the archer's centre; 0.9 stands on the ground"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }},
            {"required", json::array({"x"})}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;
            cmd.type = ARCHER_CMD_PLACE;
            cmd.value[0] = args.value("x",0.0f);
            cmd.value[1] = args.value("y",0.9f);
            main_scene->SubmitCommand(cmd);
            WaitTicks(3);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    /*
        The game's reactions as the cues decided them - see CueLog and cue_plan.md. This is how a
        replay is compared with another: clear, replay, read, and diff the lines. They are
        CueLog's own lock's, not the simulation's, so this reads them directly.
    */
    MCPServer::Get()->RegisterTool("cue_log",
        "What the game's reactions did, one line per decision: the LEVEL tick (which a replay "
        "restores, so two replays of one recording number their lines alike), the cue, 'play' or "
        "'stop', the sound, and its gain before the master volume, with pitch, pan and a start "
        "offset when they are not the default. To compare two runs: call with clear true, "
        "input_replay with wait true, read, and diff. Muting (archer_sound volume 0) does not "
        "change the lines.",
        json{
            {"type","object"},
            {"properties", {
                {"last", {{"type","number"},{"description","only the last N lines; default all that are kept (1024)"}}},
                {"clear", {{"type","boolean"},{"description","empty the log AFTER reading it, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            int last = (int)clamp(args.value("last",0.0f),0.0f,(float)CueLog::CAPACITY);
            std::vector<std::string> lines = cues.log.Lines((size_t)last);
            uint64_t total = cues.log.Total();
            if (args.value("clear",false)){
                cues.log.Clear();
            }
            return json{ {"total",total}, {"lines",lines} };
        });

    MCPServer::Get()->RegisterTool("archer_sound",
        "The master volume, and every sound playing as of the last tick: name, bus, gain, pitch, "
        "pan, how far in and how long. Pass 'volume' to set the master (0..1, the panel's slider) - "
        "0 to replay recordings silently at someone's desk; cue_log is unaffected by it.",
        json{
            {"type","object"},
            {"properties", {
                {"volume", {{"type","number"},{"description","master volume 0..1; omit to leave it"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (args.contains("volume")){
                SimCommand cmd;
                cmd.type = ARCHER_CMD_SOUND_VOLUME;
                cmd.value[0] = args.value("volume",sound_volume);
                main_scene->SubmitCommand(cmd);
                WaitTicks(2);
            }
            ArcherSnapshot s;
            {
                std::lock_guard<std::mutex> lock(snapshot_mutex);
                s = snapshot;
            }
            json voices = json::array();
            for (const SoundVoiceInfo& v : s.voices){
                voices.push_back(json{ {"name",v.name}, {"handle",v.handle}, {"bus",v.bus},
                                       {"gain",v.gain}, {"pitch",v.pitch}, {"pan",v.pan},
                                       {"position",v.position}, {"length",v.length},
                                       {"looping",v.f_looping}, {"keep",v.f_keep}, {"held",v.f_held} });
            }
            return json{ {"volume",s.sound_volume}, {"sounds_playing",s.sounds_playing}, {"voices",voices} };
        });

    MCPServer::Get()->RegisterTool("archer_camera",
        "Choose and tune the camera. 'mode': 'side' (the game's camera - trails her with lead, in "
        "every scene) or 'orbit' (the free orbit - middle-drag turns it, shift+middle pans, wheel "
        "dollies; its pivot follows her, keeping whatever angle and distance it was left at). "
        "Switching keeps the current view. All optional, and the panel's camera sliders are the "
        "same numbers: 'distance' (the side camera's zoom, 5..60, default 26), 'follow_x' / "
        "'follow_y' (per-tick ease, 0.01..1, default 0.10), 'lead' (how far ahead of a sprint, as a "
        "fraction of the half-width, default 0.21), 'keep_in' (her body box stays inside this "
        "fraction of the view, default 0.70). The camera SHAKE (what shakes is the cue table's; these "
        "are what a shake is): 'shake_scale' (0 off .. 2, default 1 - the player's setting), "
        "'shake_max' (how far a full shake moves the view, as a fraction of its half-height, default "
        "0.04), 'shake_ticks' (from full to nothing, default 24), 'shake_hz' (default 14). "
        "archer_state's 'shake' says what it is doing. Returns the values in force.",
        json{
            {"type","object"},
            {"properties", {
                {"mode", {{"type","string"},{"description","side or orbit"}}},
                {"distance", {{"type","number"}}},
                {"follow_x", {{"type","number"}}},
                {"follow_y", {{"type","number"}}},
                {"lead", {{"type","number"}}},
                {"keep_in", {{"type","number"}}},
                {"shake_scale", {{"type","number"}}},
                {"shake_max", {{"type","number"}}},
                {"shake_ticks", {{"type","number"}}},
                {"shake_hz", {{"type","number"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            std::string mode = args.value("mode",std::string());
            if (!mode.empty()){
                if (mode != "side" && mode != "orbit"){
                    return json{ {"error","mode must be 'side' or 'orbit'"} };
                }
                SimCommand cmd;
                cmd.type = ARCHER_CMD_CAMERA;
                cmd.subtype = (mode == "orbit") ? ARCHER_CAM_ORBIT : ARCHER_CAM_SIDE;
                //Commands drain on every pass, paused or not, so this lands even on a paused scene.
                SubmitCommandAndWait(cmd,1000);
            }
            //View-only numbers, written the way the panel's sliders write them.
            if (args.contains("distance")){
                camera_distance = clamp(args.value("distance",CAMERA_DISTANCE),CAMERA_DISTANCE_MIN,CAMERA_DISTANCE_MAX);
            }
            if (args.contains("follow_x")){ camera_tuning.follow_x = clamp(args.value("follow_x",0.1f),0.01f,1.0f); }
            if (args.contains("follow_y")){ camera_tuning.follow_y = clamp(args.value("follow_y",0.1f),0.01f,1.0f); }
            if (args.contains("lead")){     camera_tuning.lead = clamp(args.value("lead",0.21f),0.0f,1.0f); }
            if (args.contains("keep_in")){  camera_tuning.keep_in = clamp(args.value("keep_in",0.7f),0.1f,1.0f); }
            if (args.contains("shake_scale")){ camera_tuning.shake_scale = clamp(args.value("shake_scale",1.0f),0.0f,2.0f); }
            if (args.contains("shake_max")){   camera_tuning.shake_max = clamp(args.value("shake_max",0.04f),0.0f,0.5f); }
            if (args.contains("shake_ticks")){ camera_tuning.shake_ticks = clamp(args.value("shake_ticks",24.0f),1.0f,240.0f); }
            if (args.contains("shake_hz")){    camera_tuning.shake_hz = clamp(args.value("shake_hz",14.0f),0.5f,30.0f); }
            return json{ {"mode",(camera_mode == ARCHER_CAM_ORBIT) ? "orbit" : "side"},
                         {"distance",camera_distance},
                         {"follow_x",camera_tuning.follow_x},{"follow_y",camera_tuning.follow_y},
                         {"lead",camera_tuning.lead},{"keep_in",camera_tuning.keep_in},
                         {"shake_scale",camera_tuning.shake_scale},{"shake_max",camera_tuning.shake_max},
                         {"shake_ticks",camera_tuning.shake_ticks},{"shake_hz",camera_tuning.shake_hz} };
        });

    /*
        The panel's debug views, for a caller with no mouse. Plain flags written the way the panel
        writes them - they only choose what is drawn, so there is nothing to hand to the tick.
    */
    MCPServer::Get()->RegisterTool("archer_debug_view",
        "Turn the debug views on or off; only the fields given change. `collider`: draw her physics "
        "box (green). `rope_links`: draw the rope's rp3d links, which are hidden under the skinned "
        "rope. `rope_attach`: while she is on the rope, four beads - RED the rope's end of the "
        "joint, BLUE her end (the top of her box), YELLOW her left hand, ORANGE her right - drawn a "
        "little toward the camera so they are not hidden inside the meshes. The numbers behind "
        "them are always in archer_state's animation block: rope_joint_gap, rope_hands_off, "
        "rope_grip_s, and per hand its distance down the drawn rope and off it. `wind`: the wind "
        "field over what the camera sees - arrows coloured by speed (blue still, green the mean "
        "wind, red 2.5x it), grey streamlines from the upwind edge, pink streamlines and circles "
        "for the eddies, orange crosses on the shedding corners; tune it with archer_wind. "
        "Returns all four.",
        json{
            {"type","object"},
            {"properties", {
                {"collider",    {{"type","boolean"}}},
                {"rope_links",  {{"type","boolean"}}},
                {"rope_attach", {{"type","boolean"}}},
                {"wind",        {{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            if (args.contains("collider") && args["collider"].is_boolean()){
                f_show_collider = args["collider"].get<bool>();
                if (archer_object){
                    archer_object->SetVisibility(!archer_model || f_show_collider);
                }
            }
            if (args.contains("rope_links") && args["rope_links"].is_boolean()){
                f_show_rope_links = args["rope_links"].get<bool>();
                ApplyRopeLinkVisibility();
            }
            if (args.contains("rope_attach") && args["rope_attach"].is_boolean()){
                f_show_rope_attach = args["rope_attach"].get<bool>();
            }
            if (args.contains("wind") && args["wind"].is_boolean()){
                f_show_wind = args["wind"].get<bool>();
            }
            return json{ {"collider",f_show_collider},{"rope_links",f_show_rope_links},
                         {"rope_attach",f_show_rope_attach},{"wind",f_show_wind.load()} };
        });

    /*
        The wind field (wind_plan.md): its tuning, and a sample at a point. Builds the field if
        nothing has yet - the debug view is not the only way in.
    */
    MCPServer::Get()->RegisterTool("archer_wind",
        "The wind field over the current level. Optional tuning, each changing only itself: `speed` "
        "(units/s, signed, + blows toward +x), `eddy_strength` (a corner eddy's swirl as a fraction "
        "of the wind), `eddy_strouhal` (shedding rate), `wave_strength`, `wave_length`, "
        "`gust_strength`, `gust_width`, `gust_period` (ticks). Debug view options `arrows`, "
        "`streamlines`, `eddies`, `arrow_spacing`. Leaves: `leaves` (on/off), `leaf_density` (per "
        "square unit), `leaf_pad` (the region around the view they live in, as a fraction of it). "
        "Streaks: `streaks` (on/off), `streak_count`, `streak_alpha`, `streak_width`. Fireflies: "
        "`fireflies` (on/off), `firefly_count`, `firefly_sync` (0..1, how hard a flash pulls "
        "neighbours into step), `firefly_light_gain`, `firefly_glow_size`. `sample`: [x, y] returns the wind there at the "
        "current tick - the total, the mean flow alone, the gust factor and the distance to the "
        "nearest block. Returns the params, the build stats and the shedding corners.",
        json{
            {"type","object"},
            {"properties", {
                {"speed",         {{"type","number"}}},
                {"eddy_strength", {{"type","number"}}},
                {"eddy_strouhal", {{"type","number"}}},
                {"wave_strength", {{"type","number"}}},
                {"wave_length",   {{"type","number"}}},
                {"gust_strength", {{"type","number"}}},
                {"gust_width",    {{"type","number"}}},
                {"gust_period",   {{"type","integer"}}},
                {"arrows",        {{"type","boolean"}}},
                {"streamlines",   {{"type","boolean"}}},
                {"eddies",        {{"type","boolean"}}},
                {"arrow_spacing", {{"type","number"}}},
                {"sample",        {{"type","array"},{"items",{{"type","number"}}}}},
                {"leaves",        {{"type","boolean"}}},
                {"leaf_density",  {{"type","number"}}},
                {"leaf_pad",      {{"type","number"}}},
                {"streaks",       {{"type","boolean"}}},
                {"streak_count",  {{"type","integer"}}},
                {"streak_alpha",  {{"type","number"}}},
                {"streak_width",  {{"type","number"}}},
                {"fireflies",     {{"type","boolean"}}},
                {"firefly_count", {{"type","integer"}}},
                {"firefly_sync",  {{"type","number"}}},
                {"firefly_light_gain",{{"type","number"}}},
                {"firefly_glow_size",{{"type","number"}}}
            }}
        },
        [this](const json& args) -> json {
            std::vector<StageBlock> blocks;
            int64_t tick = 0;
            main_scene->AtTickBoundary([&](){
                blocks = stage.blocks;
                tick = (int64_t)main_scene->GetPhysicsTick();
            });
            std::lock_guard<std::mutex> lock(wind_mutex);
            auto num = [&](const char* key, float& v){
                if (args.contains(key) && args[key].is_number()){
                    v = args[key].get<float>();
                }
            };
            num("speed",wind_params.speed);
            num("eddy_strength",wind_params.eddy_strength);
            num("eddy_strouhal",wind_params.eddy_strouhal);
            num("wave_strength",wind_params.wave_strength);
            num("wave_length",wind_params.wave_length);
            num("gust_strength",wind_params.gust_strength);
            num("gust_width",wind_params.gust_width);
            if (args.contains("gust_period") && args["gust_period"].is_number()){
                wind_params.gust_period = std::max(args["gust_period"].get<int>(),30);
            }
            if (args.contains("arrows") && args["arrows"].is_boolean()){ wind_view.options.f_arrows = args["arrows"].get<bool>(); }
            if (args.contains("streamlines") && args["streamlines"].is_boolean()){ wind_view.options.f_streamlines = args["streamlines"].get<bool>(); }
            if (args.contains("eddies") && args["eddies"].is_boolean()){ wind_view.options.f_eddies = args["eddies"].get<bool>(); }
            num("arrow_spacing",wind_view.options.arrow_spacing);
            if (args.contains("leaves") && args["leaves"].is_boolean()){ f_wind_leaves = args["leaves"].get<bool>(); }
            num("leaf_density",leaf_swarm.params.density);
            num("leaf_pad",leaf_swarm.params.pad);
            if (args.contains("streaks") && args["streaks"].is_boolean()){ f_wind_streaks = args["streaks"].get<bool>(); }
            if (args.contains("streak_count") && args["streak_count"].is_number()){
                streak_swarm.params.count = std::min(std::max(args["streak_count"].get<int>(),0),200);
            }
            num("streak_alpha",streak_swarm.params.alpha);
            num("streak_width",streak_swarm.params.width);
            if (args.contains("fireflies") && args["fireflies"].is_boolean()){ f_fireflies = args["fireflies"].get<bool>(); }
            if (args.contains("firefly_count") && args["firefly_count"].is_number()){
                firefly_swarm.params.count = std::min(std::max(args["firefly_count"].get<int>(),0),200);
            }
            num("firefly_sync",firefly_swarm.params.sync);
            num("firefly_light_gain",firefly_light_gain);
            num("firefly_glow_size",firefly_glow_size);
            wind.Build(blocks,wind_params);

            const WindStats& st = wind.Stats();
            json corners = json::array();
            for (const WindCorner& c : wind.Corners()){
                corners.push_back({ {"x",c.x},{"y",c.y},{"drop",c.drop} });
            }
            json out = {
                {"tick",tick},
                {"params",{ {"speed",wind_params.speed},{"eddy_strength",wind_params.eddy_strength},
                            {"eddy_strouhal",wind_params.eddy_strouhal},{"wave_strength",wind_params.wave_strength},
                            {"wave_length",wind_params.wave_length},{"gust_strength",wind_params.gust_strength},
                            {"gust_width",wind_params.gust_width},{"gust_period",wind_params.gust_period} }},
                {"stats",{ {"nx",st.nx},{"ny",st.ny},{"obstacles",st.obstacles},{"end_walls",st.end_walls},
                           {"components",st.components},{"iterations",st.iterations},{"residual",st.residual},
                           {"build_ms",st.build_ms},{"corners",st.corners} }},
                {"bounds",{ wind.MinX(),wind.MinY(),wind.MaxX(),wind.MaxY() }},
                {"corners",corners},
                {"view_vertices",wind_view.VertexCount()},
                {"view_ms",wind_view_ms},
                {"grid",{ {"w",wind_grid_w},{"h",wind_grid_h},{"bake_ms",wind_bake_ms} }},
                {"leaves",LeafSummary()},
                {"streaks",{ {"on",f_wind_streaks.load()},{"count",streak_swarm.params.count},
                             {"alive",(int)std::count_if(streak_swarm.streaks.begin(),streak_swarm.streaks.end(),
                                                         [](const Streak& k){ return k.f_alive; })},
                             {"vertices",(int)streak_ribbons.size()} }},
                {"fireflies",FireflySummary()}
            };
            if (args.contains("sample") && args["sample"].is_array() && (args["sample"].size() >= 2)){
                float x = args["sample"][0].get<float>(), y = args["sample"][1].get<float>();
                WindVec v = wind.Velocity(x,y,tick);
                WindVec m = wind.MeanFlow(x,y);
                out["sample"] = { {"x",x},{"y",y},{"velocity",{v.x,v.y}},{"mean",{m.x,m.y}},
                                  {"gust",wind.GustFactor(x,tick)},{"distance",wind.Distance(x,y)} };
            }
            return out;
        });

    MCPServer::Get()->RegisterTool("archer_legs",
        "The loose legs (core/DynamicChain on each leg, on while she hangs from the rope or stops "
        "mid-climb). Optional `stiffness` and `damping`, each 0..1 per tick - the same two as the "
        "panel's sliders: 0 stiffness is dead weight, 1 is the clip exactly. Returns them, with the "
        "Puppet's weight, pump lead and gravity share; the per-leg swing is in archer_state's "
        "animation.legs.",
        json{
            {"type","object"},
            {"properties", {
                {"stiffness", {{"type","number"}}},
                {"damping",   {{"type","number"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!archer_model){
                return json{ {"error","no model"} };
            }
            json out;
            main_scene->AtTickBoundary([&](){
                DynamicChainParams& p = archer_model->leg_params;
                if (args.contains("stiffness") && args["stiffness"].is_number()){
                    p.stiffness = fminf(fmaxf(args["stiffness"].get<float>(),0.0f),1.0f);
                }
                if (args.contains("damping") && args["damping"].is_number()){
                    p.damping = fminf(fmaxf(args["damping"].get<float>(),0.0f),1.0f);
                }
                out = json{ {"stiffness",p.stiffness},{"damping",p.damping},
                            {"weight",puppet.leg_weight},{"lead_deg",puppet.leg_lead_deg},
                            {"gravity_share",puppet.leg_gravity} };
            });
            return out;
        });

    /*
        The rope test bench - see MeasureRopeStretch. Settings first, then the readout, all inside
        one tick boundary except the rebuild a link-mass change needs, which is a restart command.
    */
    MCPServer::Get()->RegisterTool("rope_test",
        "The rope test bench, for the ACTIVE scene's rope (scene_set 'Rope' for the bare one). Every "
        "field is optional; with none it only reports. `velocity_iterations` / `position_iterations`: "
        "this scene's rp3d solver (core default 12 / 10; each scene has its own world). `link_mass`: "
        "kg per link (default 1.2) - applied by REBUILDING the level, which restarts it. `cut`: "
        "destroy joint N (0 holds the first link to the anchor, N the bottom of link N to the next); "
        "everything below falls, she lets go if she held it, and the drawn rope is re-weighted into "
        "separate pieces a frame later (skin_cuts). `reset_peak`: start the peak window again. Returns per-joint gaps "
        "(how far each joint has opened - every bit of stretch is there, links are rigid), the "
        "rope's loaded length against its rest length, the worst gap now and over the last 3 s, and "
        "on the rope her joint's own gap and how far its anchor sits off the link (grip_drift).",
        json{
            {"type","object"},
            {"properties", {
                {"velocity_iterations", {{"type","number"}}},
                {"position_iterations", {{"type","number"}}},
                {"link_mass",           {{"type","number"}}},
                {"cut",                 {{"type","number"}}},
                {"reset_peak",          {{"type","boolean"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            bool f_rebuild = false;
            if (args.contains("link_mass") && args["link_mass"].is_number()){
                float m = args["link_mass"].get<float>();
                if (m < 0.01f){ m = 0.01f; }
                main_scene->AtTickBoundary([this,m](){ rope_link_mass = m; });
                SimCommand cmd;
                cmd.type = ARCHER_CMD_RESTART;
                main_scene->SubmitCommand(cmd);
                WaitTicks(3);
                f_rebuild = true;
            }
            bool f_cut = false;
            json out;
            main_scene->AtTickBoundary([&](){
                if (args.contains("velocity_iterations") || args.contains("position_iterations")){
                    SetRopeSolverIterations((int)args.value("velocity_iterations",(float)solver_velocity_iterations),
                                            (int)args.value("position_iterations",(float)solver_position_iterations));
                }
                if (args.contains("cut") && args["cut"].is_number()){
                    f_cut = CutRopeJoint((int)args["cut"].get<float>());
                }
                if (args.value("reset_peak",false)){
                    rope_stretch.peak_gap = 0.0f;
                    rope_stretch.peak_age = 0;
                }
                const RopeStretch& r = rope_stretch;
                json gaps = json::array();
                for (size_t i = 0; i < r.gaps.size(); i++){
                    gaps.push_back(r.gaps[i]);
                }
                //The cuts the drawn rope is weighted for - a frame behind a fresh cut, see
                //CheckRopeSkinCuts - and how much of the chain still hangs from the anchor.
                json skin_cuts = json::array();
                for (size_t i = 0; i < rope_skin_cuts.size(); i++){
                    skin_cuts.push_back(rope_skin_cuts[i]);
                }
                out = json{
                    {"velocity_iterations",solver_velocity_iterations},
                    {"position_iterations",solver_position_iterations},
                    {"link_mass",rope_link_mass},
                    {"rebuilt",f_rebuild},
                    {"cut_done",f_cut},
                    {"joints",r.joints},
                    {"cuts",r.cuts},
                    {"skin_cuts",skin_cuts},
                    {"anchored_links",RopeAnchoredLinks()},
                    {"rest_length",r.rest_length},
                    {"loaded_length",r.loaded_length},
                    {"worst_gap",r.worst_gap},
                    {"worst_joint",r.worst_joint},
                    {"peak_gap",r.peak_gap},
                    {"peak_joint",r.peak_joint},
                    {"gaps",gaps},
                    {"on_rope",stage.mode == MODE_ROPE},
                    {"grip_s",(stage.mode == MODE_ROPE) ? rope_grip_s : -1.0f},
                    {"grip_joint_gap",rope_joint_gap},
                    {"grip_drift",(stage.mode == MODE_ROPE) ? rope_grip_drift : -1.0f},
                    {"archer_y",stage.pos.y}
                };
            });
            return out;
        });

    MCPServer::Get()->RegisterTool("archer_restart",
        "Rebuild the level and put the archer back at the start. Everything the props have "
        "accumulated - kicked crates, toppled targets, embedded arrows - is thrown away and rebuilt "
        "from archer/Stage's BuildLevel, so this is the way to get a clean measurement. IF the "
        "panel's 'level entry get-up' is ticked (it is off by default) she then gets up: mode "
        "'getup' for 210 ticks (3.5 s), during which EVERY input is ignored - runs, jumps, draws "
        "and holds sent then do nothing. Wait it out, or archer_place ends it at once.",
        json{ {"type","object"}, {"properties",json::object()} },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;
            cmd.type = ARCHER_CMD_RESTART;
            main_scene->SubmitCommand(cmd);
            WaitTicks(3);
            return MaybeAttachScreenshot(BuildStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_terrain_regenerate",
        "The panel's 'Regenerate terrain' button. Reads every block's centre and size back off its "
        "Object - so move boxes first with object_set_transform (position is the centre, scale the "
        "full size; they are the block_N children of 'blockout') - writes them into the rules, keeps "
        "that layout across restarts, and remeshes both terrain bays over it. Bay 0 melts solid "
        "blocks centred at x -40..-12 below y 6, bay 1 the ones above. Returns how many blocks had "
        "moved and how many are now hidden under terrain.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            //The render thread does the work - see f_regenerate_terrain - so wait for its counter.
            int before = terrain_generation.load();
            f_regenerate_terrain = true;
            for (int i = 0; i < 200 && terrain_generation.load() == before; i++){
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            if (terrain_generation.load() == before){
                return json{ {"error","the render thread did not regenerate within 2 s"} };
            }
            json out = json{ {"moved_blocks",last_regen_moved},{"hidden_blocks",last_regen_hidden},
                             {"generation",terrain_generation.load()} };
            return MaybeAttachScreenshot(out,args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("archer_anim",
        "Inspect and drive the character's ANIMATION, separately from the game. Three modes, set "
        "with 'source'. 'clip' plays one clip on a loop and ignores the game entirely - this is how "
        "you check that an export came through, and it works on the four clips the game has no use "
        "for as well as the five it does. 'panel' feeds the animation layer a ground speed and a "
        "grounded flag BY HAND, so a walk cycle can be judged at 4 units a second with no level "
        "underneath her. 'game' hands the controls back to the rules. With no arguments it reports "
        "every clip: how long it is, how fast its own root track says it travels, and - the number "
        "worth reading - what playback rate it would need to keep the feet planted at that speed. "
        "In 'panel' and 'clip' mode the archer ignores the keyboard and stands where she was left; "
        "the rest of the game keeps running.",
        json{
            {"type","object"},
            {"properties", {
                {"source", {{"type","string"},{"description","'game', 'panel' or 'clip'; omit to only report"}}},
                {"clip", {{"type","string"},{"description","clip name for source 'clip', e.g. Idle, Walking, Kick_Front, Twirl"}}},
                {"rate", {{"type","number"},{"description","playback rate for source 'clip'; negative plays it backwards, default 1"}}},
                {"speed", {{"type","number"},{"description","ground speed for source 'panel', signed: negative backs up. The game runs at 9"}}},
                {"on_ground", {{"type","boolean"},{"description","for source 'panel', default true"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG, default false"}}}
            }}
        },
        [this](const json& args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!archer_model){
                return json{ {"error","no character model loaded - see the log for which name failed"} };
            }
            std::string source = args.value("source",std::string(""));
            if (source.size()){
                SimCommand cmd;
                cmd.type = ARCHER_CMD_ANIM;
                if (source == "clip"){
                    cmd.subtype = ANIM_FROM_CLIP;
                    std::string want = args.value("clip",std::string("Idle"));
                    int found = -1;
                    for (int i = 0; i < CLIP_COUNT; i++){
                        if (want == ARCHER_CLIPS[i].name){ found = i; }
                    }
                    if (found < 0){
                        return json{ {"error","no clip called '" + want + "' - call with no arguments to list them"} };
                    }
                    if (!archer_clips[found]){
                        return json{ {"error","clip '" + want + "' is in the table but was not in the export"} };
                    }
                    cmd.value[0] = (float)found;
                    cmd.value[1] = args.value("rate",1.0f);
                }else if (source == "panel"){
                    cmd.subtype = ANIM_FROM_PANEL;
                    cmd.value[2] = args.value("speed",0.0f);
                    cmd.value[3] = args.value("on_ground",true) ? 1.0f : 0.0f;
                }else if (source == "game"){
                    cmd.subtype = ANIM_FROM_GAME;
                }else{
                    return json{ {"error","source must be 'game', 'panel' or 'clip'"} };
                }
                main_scene->SubmitCommand(cmd);
                WaitTicks(3);
            }

            /*
                The clip table, read straight off the Puppet.

                NOT from the snapshot, and that is safe rather than sloppy: these are written once
                by MeasureClips during Init, on the render thread, and the MCP server does not
                accept a request until Init has returned. Nothing writes them again.
            */
            json clips = json::array();
            for (int i = 0; i < CLIP_COUNT; i++){
                float world = puppet.WorldClipSpeed(i);
                clips.push_back(json{
                    {"name",ARCHER_CLIPS[i].name},
                    {"loaded",archer_clips[i] != NULL},
                    {"duration",puppet.clip_duration[i]},
                    {"looping",ARCHER_CLIPS[i].f_looping},
                    {"native_speed",world},
                    //What it would take to keep the feet planted at a full run. Infinite for a
                    //clip that does not travel, which is reported as null rather than as a number
                    //that looks like an answer.
                    {"rate_at_run_speed",(world > 0.01f) ? json(ARCHER_RUN_SPEED / world) : json(nullptr)}
                });
            }
            json result = BuildStateJson();
            result["clips"] = clips;
            result["model"] = json{
                {"scale",model_scale},
                {"height",ARCHER_MODEL_HEIGHT},
                {"foot_offset",model_foot_offset}
            };
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });
}
#endif

//--- The debug panel ----------------------------------------------------------------------------
#ifdef USE_IMGUI

/*
    The props in archer.glb worth placing by hand. Not the rope and vine pieces (tiles, meaningless
    alone), not the character, and not her bow and arrows (sockets). Registered under their node
    names, which is what the menu lists and what PlaceMenuSpawn recognises.
*/
static const char* ARCHER_PLACEABLE_NODES[] = {
    "rock_big_1", "rock_big_2", "rock_small_1", "pine_tree", "tree_stump", "logs", "barrel", "wooden_crate",
    "wooden_panel", "signpost", "post_leaves", "pillar_lamp", "post_lamp", "sunflower", "flower",
    "fern_1", "fern_2", "grass_1", "grass_2", "leaf_small", "straw_man", "archery_target", "terrain_tile_big", "terrain_tile_round",
};

void ApplicationArcher::RegisterPlaceableProps(){
    std::vector<std::string> names;
    for (const char* name:ARCHER_PLACEABLE_NODES){
        names.push_back(name);
    }
    //Render thread, archer.glb already loaded - Init. It Fatal()s anywhere else.
    GetAssetsFromGLTF(names);
}

void ApplicationArcher::PlaceMenuSpawn(SimCommand& cmd){
    //At her feet: the one place on screen that is certainly on the play plane and in view. The
    //Inspector moves it from there.
    cmd.flags |= SIM_CMD_FLAG_POSITION;
    cmd.position = vec3(stage.pos.x,stage.pos.y - ARCHER_HALF_H,0.0f);
    for (const char* name:ARCHER_PLACEABLE_NODES){
        if (AssetIDFromName(name) == cmd.asset){
            cmd.flags |= SIM_CMD_FLAG_SCALE;
            cmd.scale = vec3(model_scale,model_scale,model_scale);
            break;
        }
    }
}

void ApplicationArcher::DrawImGuiUI(void){
    //Before the dockspace, so the dock is laid out under the bar rather than behind it.
    RenderDebugMenuBar();
    RenderApplicationUI();
    Application::DrawImGuiUI();

    //Runs on the RENDER thread with physics_mutex held, so the live Stage can be read directly.
    //It must never wait on the physics thread - see the threading note in ApplicationArcher.h.
    ImGui::Begin("Archer");

    ImGui::Text("%s",stage.DebugLine().c_str());
    ImGui::Separator();

    ImGui::Text("mode      %s%s",
                (stage.mode == MODE_GROUND) ? "ground" :
                (stage.mode == MODE_AIR) ? "air" :
                (stage.mode == MODE_HANG) ? "hang" :
                (stage.mode == MODE_CLIMB) ? "climb" :
                (stage.mode == MODE_GETUP) ? "getting up - no input" :
                (stage.mode == MODE_KNEEL) ? ((stage.kneel_phase == KNEEL_HELD) ? "kneel" :
                                              (stage.kneel_phase == KNEEL_RISING) ? "kneel, rising" :
                                                                                    "kneel, lowering") : "rope",
                stage.f_on_ground ? "" : "  (airborne)");
    ImGui::Text("coyote    %i    buffer %i",stage.coyote_ticks,stage.buffer_ticks);
    ImGui::Text("velocity  %.2f, %.2f",stage.vel.x,stage.vel.y);

    ImGui::Separator();
    ImGui::Text("aim       %.0f deg %s, sway %+.1f",stage.aim_deg,(stage.facing > 0.0f) ? "right" : "left",
                stage.AimSwayDeg());
    if (archer_model){
        //The override's check: the drawn bow against the rules' aim, and the live neutral it
        //corrected from. At full weight the drawn angle IS the aim; the pose angle is what the
        //clips alone would have pointed at. And the upper layer's clip and weight.
        ImGui::Text("bow at    %.1f deg (%+.1f), body %.0f%%, pose %.1f",
                    archer_model->aim_drawn_deg,archer_model->aim_drawn_deg - stage.ShotAimDeg(),
                    archer_model->aim_weight * 100.0f,archer_model->aim_pose_deg);
        int up = puppet.choice.upper_clip;
        ImGui::Text("upper     %s %.0f%%",(up >= 0 && up < CLIP_COUNT) ? ARCHER_CLIPS[up].name : "-",
                    puppet.upper_weight * 100.0f);
        //The loose legs: how much is on, and each thigh's swing off the clip. The two knobs are how
        //loose - a float each, read by the next tick's step.
        ImGui::Text("legs      %.0f%%, lead %+.0f, swing L %+.1f R %+.1f deg",archer_model->leg_weight * 100.0f,
                    archer_model->leg_lead_deg,archer_model->leg_swing_deg[0],archer_model->leg_swing_deg[1]);
        ImGui::SliderFloat("leg stiffness",&archer_model->leg_params.stiffness,0.0f,0.3f,"%.3f");
        ImGui::SliderFloat("leg damping",&archer_model->leg_params.damping,0.0f,0.3f,"%.3f");
    }
    ImGui::ProgressBar((float)stage.draw_ticks / (float)BOW_DRAW_TICKS,ImVec2(-1,0),"draw");
    //The string as drawn on screen, beside the rules' draw above: with the draw pose on screen it
    //follows her hand, so the two bars disagree until her hand reaches the string. That gap IS the
    //open timing question in bow_plan.md §8 item 7.
    ImGui::ProgressBar(bow_draw_shown,ImVec2(-1,0),f_arrow_nocked ? "string (arrow on)" : "string");
    ImGui::Text("arrows    %i live, %i shot, %i in walls",
                stage.NumLiveArrows(),stage.arrows_shot,stage.arrows_hit_blocks);

    ImGui::Separator();
    //The one number worth a slider: how much of an arrow's speed its target takes. Everything else
    //in the feel lives in Stage.h, where it belongs and where the rules test can check it - this
    //one is the app's, because it is about rigid bodies the rules never see.
    ImGui::SliderFloat("arrow punch",&arrow_speed_transfer,0.0f,0.25f,"%.3f");

    int knocked = 0;
    for (size_t i = 0; i < prop_views.size(); i++){
        if (prop_views[i].kind == PROP_TARGET && prop_views[i].f_knocked){
            knocked++;
        }
    }
    ImGui::Text("targets   %i knocked over",knocked);
    if (archery_last_points >= 0){
        ImGui::Text("archery   %i points, last arrow %i",archery_score,archery_last_points);
    }else{
        ImGui::Text("archery   %i points",archery_score);
    }
    ImGui::Text("kicks     %i on straw men",kick_score);
    //The straw man's swing. Written here like the arrow punch above; TickSprings, on the physics
    //thread, notices a change and retunes the springs there.
    ImGui::SliderFloat("straw hz",&straw_hz,0.3f,4.0f,"%.2f");
    ImGui::SliderFloat("straw damping",&straw_damping_ratio,0.0f,1.0f,"%.2f");
    ImGui::SliderFloat("straw kick",&straw_kick_punch,0.0f,3.0f,"%.2f");

    if (ImGui::Button("Restart")){
        //From the render thread, so it goes on the queue rather than being called here.
        SimCommand cmd;
        cmd.type = ARCHER_CMD_RESTART;
        SubmitUICommand(cmd);
    }
    ImGui::SameLine();
    //Written directly, like the sliders below: NewGame reads it under the same lock.
    ImGui::Checkbox("level entry get-up",&f_level_entry_getup);
    ImGui::SetItemTooltip("Start each level lying down and getting up, controls locked for 3.5 s. "
                          "Takes effect at the next restart.");
    //The master bus, set from this every pass in UpdateView - so it turns down what is playing too.
    if (soundsystem){
        ImGui::SliderFloat("volume",&sound_volume,0.0f,1.0f,"%.2f");
    }
    /*
        What each sound is hung on, when and how loud, is the cue table's now - the kick's swing
        tick and shout chance used to be sliders here. Edit the file and save: it reloads within a
        second, paused or not. The cue panel with a slider per cue is step 5 of cue_plan.md.
    */
    ImGui::TextDisabled("cues: %s (%d)",cues.File().empty() ? "not loaded" : cues.File().c_str(),
                        (int)cues.CueNames().size());
    ImGui::SetItemTooltip("Every sound's timing, chance and gain is in this file. Save it and it "
                          "reloads within a second; a table that fails to parse is logged and the "
                          "last good one kept. cue_log over MCP says what fired.");

    /*
        --- The terrain -----------------------------------------------------------------------

        Move boxes in the Inspector - the blockout checkbox shows the melted ones, and they are all
        under "blockout" in the Scene tree - then regenerate. The button only raises a flag: the
        remesh is GL and takes physics_mutex, and this code already holds it (see PreRender).
        The checkbox is written directly, like the sliders above: ApplyBlockoutVisibility does no
        GL and only touches visibility flags.
    */
#if ARCHER_TEST_BAY
    ImGui::Separator();
    if (ImGui::Button("Regenerate terrain")){
        f_regenerate_terrain = true;
    }
    ImGui::SameLine();
    bool f_blockout = f_show_blockout;
    if (ImGui::Checkbox("show blockout (F2)",&f_blockout)){
        SetBlockoutVisible(f_blockout);
    }
    ImGui::TextDisabled("%i boxes under terrain, regenerated %i times",
                        (int)melted_blocks.size(),terrain_generation.load());
#endif

    /*
        --- The foliage -----------------------------------------------------------------------

        Written directly, like the sliders above, and rescattered only when a slider is LET GO:
        a scatter is a few milliseconds, which is fine once and a stutter every frame of a drag.
        Regenerate terrain rescatters too, so moving a box needs nothing from here.
    */
    if (foliage_group && ImGui::CollapsingHeader("Foliage")){
        bool f_rescatter = false;
        ImGui::SliderFloat("open density",&foliage_params.density_open,0.0f,4.0f,"%.2f /unit");
        f_rescatter |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("corner density",&foliage_params.density_corner,0.0f,30.0f,"%.1f /unit");
        f_rescatter |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("corner reach",&foliage_params.ao_radius,0.5f,6.0f,"%.2f");
        f_rescatter |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("corner falloff",&foliage_params.ao_gamma,0.3f,3.0f,"%.2f");
        f_rescatter |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("spacing",&foliage_params.spacing,0.4f,1.6f,"%.2f");
        f_rescatter |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("plant size",&foliage_scale,0.5f,5.0f,"%.2f");
        f_rescatter |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("= her")){
            foliage_scale = model_scale;
            f_rescatter = true;
        }
        if (ImGui::Button("Rescatter")){
            f_rescatter = true;
        }
        ImGui::SameLine();
        ImGui::Text("%i ferns, %i low ferns, %i flowers, %i + %i grass",foliage_counts[FOLIAGE_FERN],
                    foliage_counts[FOLIAGE_FERN_LOW],foliage_counts[FOLIAGE_FLOWER],
                    foliage_counts[FOLIAGE_GRASS],foliage_counts[FOLIAGE_GRASS_2]);
        if (f_rescatter){
            f_rescatter_foliage = true;
        }
    }

    //The drawn rope over the physics chain. The links are hidden under it; this puts them back.
    if (ImGui::CollapsingHeader("Rope")){
        ImGui::Text("skin: %s, %i bones over %i links",rope_skin ? "built" : "none on this level",
                    (int)rope_bones.size(),rope_segments.empty() ? 0 : (int)rope_segments.size() - 1);
        ImGui::Text("segment %s, ring %s, collar %s, tassel %s",
                    f_rope_part_from_asset[ROPE_PART_SEGMENT] ? "archer.glb" : "placeholder",
                    f_rope_part_from_asset[ROPE_PART_RING] ? "archer.glb" : "none",
                    f_rope_part_from_asset[ROPE_PART_COLLAR] ? "archer.glb" : "none",
                    f_rope_part_from_asset[ROPE_PART_TASSEL] ? "archer.glb" : "none");
        if (ImGui::Checkbox("show the physics links",&f_show_rope_links)){
            ApplyRopeLinkVisibility();
        }
        ImGui::Checkbox("show the attachment (red rope, blue body, yellow left hand, orange right)",&f_show_rope_attach);
        if (rope_joint_gap >= 0.0f){
            ImGui::Text("joint gap %.3f, hands off the rope %.3f",rope_joint_gap,rope_hands_off);
            ImGui::Text("grip %.3f down; left hand %.3f down %.3f off, right %.3f down %.3f off",
                        rope_grip_s,rope_hand_s[0],rope_hand_off[0],rope_hand_s[1],rope_hand_off[1]);
            ImGui::Text("anchor %.3f off the link's geometry",rope_grip_drift);
        }else{
            ImGui::TextDisabled("not on the rope");
        }

        /*
            The test bench (MeasureRopeStretch, and `rope_test` over MCP). The iterations apply at
            once and to this scene only; a link mass applies on Rebuild, which restarts the level.
        */
        ImGui::Separator();
        const RopeStretch& rs = rope_stretch;
        ImGui::Text("stretch: %.2f loaded against %.2f rest (+%.0f%%), %i joints, %i cut",rs.loaded_length,
                    rs.rest_length,(rs.rest_length > 0.0f) ? 100.0f * (rs.loaded_length / rs.rest_length - 1.0f) : 0.0f,
                    rs.joints,rs.cuts);
        ImGui::Text("worst joint %i opened %.3f; peak %.3f at joint %i",rs.worst_joint,rs.worst_gap,
                    rs.peak_gap,rs.peak_joint);
        int vel = solver_velocity_iterations;
        int pos = solver_position_iterations;
        bool f_iter = ImGui::SliderInt("velocity iterations",&vel,1,100);
        f_iter |= ImGui::SliderInt("position iterations",&pos,1,100);
        if (f_iter){
            SetRopeSolverIterations(vel,pos);
        }
        ImGui::SliderFloat("link mass",&rope_link_mass,0.1f,40.0f,"%.1f kg");
        ImGui::SameLine();
        if (ImGui::SmallButton("Rebuild")){
            SimCommand cmd;
            cmd.type = ARCHER_CMD_RESTART;
            main_scene->SubmitCommand(cmd);
        }
        ImGui::SliderInt("joint",&rope_cut_request,0,rs.joints > 0 ? rs.joints - 1 : 0);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cut")){
            CutRopeJoint(rope_cut_request);
        }
    }

    //The wind (wind_plan.md). Everything but the show flag is under wind_mutex, for the MCP tool.
    if (ImGui::CollapsingHeader("Wind")){
        bool f_show = f_show_wind;
        if (ImGui::Checkbox("show the wind field",&f_show)){
            f_show_wind = f_show;
        }
        std::lock_guard<std::mutex> lock(wind_mutex);
        WindViewOptions& o = wind_view.options;
        ImGui::Checkbox("arrows",&o.f_arrows);
        ImGui::SameLine();
        ImGui::Checkbox("streamlines",&o.f_streamlines);
        ImGui::SameLine();
        ImGui::Checkbox("eddies",&o.f_eddies);
        ImGui::SliderFloat("arrow spacing",&o.arrow_spacing,0.5f,3.0f);
        bool f_changed = false;
        f_changed |= ImGui::SliderFloat("speed (u/s)",&wind_params.speed,-8.0f,8.0f);
        f_changed |= ImGui::SliderFloat("eddy strength",&wind_params.eddy_strength,0.0f,2.0f);
        f_changed |= ImGui::SliderFloat("shedding (strouhal)",&wind_params.eddy_strouhal,0.05f,0.5f);
        f_changed |= ImGui::SliderFloat("waves",&wind_params.wave_strength,0.0f,0.5f);
        f_changed |= ImGui::SliderFloat("wave length",&wind_params.wave_length,2.0f,30.0f);
        f_changed |= ImGui::SliderFloat("gusts",&wind_params.gust_strength,0.0f,1.5f);
        f_changed |= ImGui::SliderFloat("gust width",&wind_params.gust_width,2.0f,40.0f);
        f_changed |= ImGui::SliderInt("gust period (ticks)",&wind_params.gust_period,60,2000);
        if (f_changed){
            wind.SetParams(wind_params);
        }
        //How far each plant bends - material_t::wind_flex on its own copy of its material.
        static const char* flex_labels[FOLIAGE_KIND_COUNT] = { "flex fern", "flex low fern", "flex flower", "flex grass 1", "flex grass 2" };
        bool f_flex = false;
        for (int k = 0; k < FOLIAGE_KIND_COUNT; k++){
            f_flex |= ImGui::SliderFloat(flex_labels[k],&foliage_wind_flex[k],0.0f,1.0f,"%.3f");
        }
        if (f_flex){
            ApplyFoliageWindFlex();
        }
        bool f_leaves = f_wind_leaves;
        if (ImGui::Checkbox("leaves on the wind",&f_leaves)){
            f_wind_leaves = f_leaves;
        }
        {
            LeafParams& lp = leaf_swarm.params;
            ImGui::SliderFloat("leaf density (per u^2)",&lp.density,0.0f,1.0f);
            ImGui::SliderFloat("leaf padding",&lp.pad,0.0f,3.0f);
            ImGui::SliderInt("leaf cap",&lp.max_count,0,WIND_LEAF_POOL);
            ImGui::SliderFloat("leaf size",&leaf_scale,0.2f,3.0f);
            ImGui::SliderFloat("leaf fall (u/s)",&lp.fall_speed,0.05f,3.0f);
            ImGui::SliderFloat("leaf drag (s)",&lp.drag_time,0.02f,2.0f);
            ImGui::SliderFloat("leaf flutter",&lp.flutter,0.0f,3.0f);
            ImGui::SliderFloat("leaf lift (u/s)",&lp.lift_speed,0.5f,10.0f);
            int flying = 0, resting = 0;
            for (const Leaf& l : leaf_swarm.leaves){
                flying += (l.state == LEAF_FLYING);
                resting += (l.state != LEAF_FLYING);
            }
            ImGui::Text("%d leaves (%d flying, %d lying)",(int)leaf_swarm.leaves.size(),flying,resting);
        }
        bool f_streaks = f_wind_streaks;
        if (ImGui::Checkbox("wind streaks",&f_streaks)){
            f_wind_streaks = f_streaks;
        }
        {
            StreakParams& sp = streak_swarm.params;
            ImGui::SliderInt("streak count",&sp.count,0,200);
            ImGui::SliderFloat("streak alpha",&sp.alpha,0.0f,1.0f);
            ImGui::SliderFloat("streak width",&sp.width,0.01f,0.4f);
            ImGui::SliderInt("streak trail (samples)",&sp.points,4,96);
            ImGui::SliderFloat("streak gust bias",&sp.gust_bias,0.0f,1.0f);
            ImGui::ColorEdit3("streak colour",&streak_color.x);
            int alive = 0;
            for (const Streak& k : streak_swarm.streaks){
                alive += k.f_alive;
            }
            ImGui::Text("%d streaks alive, %zu ribbon vertices",alive,streak_ribbons.size());
        }
        bool f_flies = f_fireflies;
        if (ImGui::Checkbox("fireflies",&f_flies)){
            f_fireflies = f_flies;
        }
        {
            FireflyParams& fp = firefly_swarm.params;
            ImGui::SliderInt("firefly count",&fp.count,0,200);
            ImGui::SliderFloat("firefly sync",&fp.sync,0.0f,1.0f);
            ImGui::SliderFloat("firefly period (s)",&fp.period,1.0f,12.0f);
            ImGui::SliderFloat("firefly glow size",&firefly_glow_size,0.1f,2.0f);
            ImGui::SliderFloat("firefly halo",&firefly_halo,0.0f,1.5f);
            ImGui::SliderFloat("firefly light gain",&firefly_light_gain,0.0f,6.0f);
            int lit = 0;
            for (const Firefly& f : firefly_swarm.flies){
                lit += (f.home >= 0);
            }
            ImGui::Text("%d fireflies at home; lights %.2f %.2f %.2f",lit,firefly_lights[0] ? firefly_lights[0]->brightness : 0.0f,
                        firefly_lights[1] ? firefly_lights[1]->brightness : 0.0f,firefly_lights[2] ? firefly_lights[2]->brightness : 0.0f);
        }
        if (ImGui::SliderFloat("flex vine leaves",&vine_leaf_wind_flex,0.0f,3.0f,"%.3f")){
            for (int k = 0; k < VINE_LEAF_KIND_COUNT; k++){
                SetMaterialWindFlex(std::string("@") + VINE_LEAF_NODES[k],vine_leaf_wind_flex);
            }
            if (material_vine_leaf < (int)renderer->materials.size()){
                renderer->materials[material_vine_leaf].glsl_material.wind_flex = vine_leaf_wind_flex;
            }
        }
        if (wind.IsBuilt()){
            const WindStats& st = wind.Stats();
            ImGui::Text("%dx%d nodes, %d obstacles (%d end walls out), %d corners",st.nx,st.ny,st.obstacles,st.end_walls,st.corners);
            ImGui::Text("solve: %d iterations, %.1f ms; view %d vertices, %.2f ms a frame",st.iterations,st.build_ms,
                        wind_view.VertexCount(),wind_view_ms);
            ImGui::Text("plant grid: %dx%d, %.2f ms a frame",wind_grid_w,wind_grid_h,wind_bake_ms);
        }else{
            ImGui::TextDisabled("not built yet - show it, or ask archer_wind");
        }
    }

    //The vines are static - built once - so this only says what was built and from what.
    if (vine_group && ImGui::CollapsingHeader("Vines")){
        ImGui::Text("%i vines, %i + %i leaves",(int)vine_trunks.size(),
                    vine_leaf_counts[VINE_LEAF_1],vine_leaf_counts[VINE_LEAF_2]);
        ImGui::Text("trunk: %s, wrap: %s, leaves: %s / %s",
                    f_vine_trunk_from_asset ? "archer.glb" : "placeholder",
                    f_vine_wrap_from_asset ? "archer.glb" : "none",
                    f_vine_leaf_from_asset[VINE_LEAF_1] ? "archer.glb" : "placeholder",
                    f_vine_leaf_from_asset[VINE_LEAF_2] ? "archer.glb" : "placeholder");
    }

    /*
        --- The animation ---------------------------------------------------------------------

        Written directly rather than through the command queue, for the same reason the arrow
        punch slider is: this runs with physics_mutex held, and these are scalars the physics
        thread only reads. NewGame above is the exception because it rebuilds the scene.
    */
    ImGui::Separator();
    if (!archer_model){
        ImGui::TextWrapped("No character model. Check the log for which of the skin, node or clip "
                           "names in " ARCHER_MODEL_ASSET " did not resolve.");
    }else if (ImGui::CollapsingHeader("Animation",ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::Text("rig scaled %.3fx to stand %.2f tall",model_scale,ARCHER_MODEL_HEIGHT);

        //Which camera. Only the mode is written here; UpdateView and the tick do the moving, on
        //the physics thread, and the orbit picks up from wherever the side camera was looking.
        ImGui::RadioButton("side camera",&camera_mode,ARCHER_CAM_SIDE); ImGui::SameLine();
        ImGui::RadioButton("free orbit",&camera_mode,ARCHER_CAM_ORBIT);
        if (camera_mode == ARCHER_CAM_ORBIT){
            ImGui::TextDisabled("middle-drag orbits, shift+middle pans, wheel dollies");
        }

        //The wheel is the way to do this, but the wheel cannot be scripted and cannot be nudged by
        //exactly one unit, so the slider is here too - and it is the only way back to the default.
        //The SIDE camera's distance; the orbit keeps its own, in where the camera actually is.
        ImGui::SliderFloat("camera",&camera_distance,CAMERA_DISTANCE_MIN,CAMERA_DISTANCE_MAX,"%.1f");
        ImGui::SameLine();
        if (ImGui::SmallButton("reset")){
            camera_distance = CAMERA_DISTANCE;
        }
        //How the side camera follows her - see ArcherCameraTuning. Read by the next tick.
        ImGui::SliderFloat("follow across",&camera_tuning.follow_x,0.01f,1.0f,"%.3f");
        ImGui::SliderFloat("follow up/down",&camera_tuning.follow_y,0.01f,1.0f,"%.3f");
        ImGui::SliderFloat("lead",&camera_tuning.lead,0.0f,0.6f,"%.2f");
        ImGui::SliderFloat("keep in frame",&camera_tuning.keep_in,0.2f,1.0f,"%.2f");
        //What shakes is the cue table's (the `shake` actions); these are what a shake is.
        ImGui::SliderFloat("shake",&camera_tuning.shake_scale,0.0f,2.0f,"%.2f");
        ImGui::SetItemTooltip("The player's setting: 0 turns camera shake off, 1 is as the cue table "
                              "wrote it. Trauma now %.2f.",shake_trauma);
        ImGui::SliderFloat("shake size",&camera_tuning.shake_max,0.0f,0.2f,"%.3f");
        ImGui::SetItemTooltip("How far a full shake moves the view, as a fraction of its half-height.");
        ImGui::SliderFloat("shake ticks",&camera_tuning.shake_ticks,4.0f,90.0f,"%.0f");
        ImGui::SetItemTooltip("How long a full shake takes to die away.");
        ImGui::SliderFloat("shake hz",&camera_tuning.shake_hz,2.0f,30.0f,"%.1f");
        if (ImGui::SmallButton("reset follow")){
            camera_tuning = ArcherCameraTuning();
        }

        /*
            The backdrop, which is a picture and therefore entirely a matter of taste.

            `follow` is how much of the camera's motion it copies: 1 pins it to the camera and it
            reads as infinitely far, 0 nails it to the world and it slides past as fast as the
            ground. `scale` and `offset` choose which part of a portrait image a 16:9 view shows.
        */
        if (background_object){
            ImGui::SliderFloat("bg follow",&background_follow,0.0f,1.0f,"%.2f");
            ImGui::SliderFloat("bg scale",&background_scale,0.4f,2.5f,"%.2f");
            ImGui::SliderFloat("bg offset y",&background_offset_y,-40.0f,40.0f,"%.1f");
        }

        //Who fills ArcherAnimParams. The three are indistinguishable downstream - see Puppet.h.
        ImGui::RadioButton("game",&anim_source,ANIM_FROM_GAME);  ImGui::SameLine();
        ImGui::RadioButton("panel",&anim_source,ANIM_FROM_PANEL); ImGui::SameLine();
        ImGui::RadioButton("clip",&anim_source,ANIM_FROM_CLIP);

        ImGui::Checkbox("show collider",&f_show_collider);
        ImGui::SameLine();
        //The one toggle worth having in front of you the whole time: play every clip at 1.0 and
        //watch the feet skate, or stretch the rate to plant them and watch the cycle speed up.
        //Neither looks right yet, and seeing WHY is the point of this pass.
        ImGui::Checkbox("match feet to speed",&puppet.f_match_feet);

        if (anim_source == ANIM_FROM_PANEL){
            //The rules' own numbers as the range, so what is dialled in here is a speed the game
            //can actually produce rather than an abstract slider.
            ImGui::SliderFloat("speed",&panel_params.speed,-ARCHER_RUN_SPEED,ARCHER_RUN_SPEED,"%.2f");
            panel_params.ground_speed = fabsf(panel_params.speed);
            panel_params.facing = (panel_params.speed < 0.0f) ? -1.0f : 1.0f;
            ImGui::Checkbox("on ground",&panel_params.f_on_ground);
            panel_params.mode = panel_params.f_on_ground ? MODE_GROUND : MODE_AIR;
        }
        if (anim_source == ANIM_FROM_CLIP){
            ImGui::SliderFloat("preview rate",&preview_rate,-3.0f,3.0f,"%.2f");
        }

        /*
            Every clip in the file, including the ones the game has no use for.

            The export is all-or-nothing, so "did that come through correctly" is a question about
            all nine - and a clip nobody can play is a clip nobody can check. Clicking one switches
            to preview mode, which is the only thing anyone wants when they click a clip name.
        */
        ImGui::Separator();
        ImGui::Text("clip                 secs   native  at run");
        for (int i = 0; i < CLIP_COUNT; i++){
            ImGui::PushID(i);
            bool f_ready = (archer_clips[i] != NULL);
            if (!f_ready){
                ImGui::TextDisabled("%-18s  MISSING FROM EXPORT",ARCHER_CLIPS[i].name);
                ImGui::PopID();
                continue;
            }
            if (ImGui::SmallButton((i == playing_clip) ? "||" : ">")){
                anim_source = ANIM_FROM_CLIP;
                preview_clip = i;
                playing_clip = -1;      //force a restart, so a preview always plays from frame 0
            }
            ImGui::SameLine();
            float world = puppet.WorldClipSpeed(i);
            if (world > 0.01f){
                //What planting the feet at a full run would cost this clip. Over about 1.8 it is
                //no longer a stride, it is a fast-forward - see PUPPET_RATE_MAX.
                ImGui::Text("%-18s %5.2f  %5.2f/s  %4.1fx",ARCHER_CLIPS[i].name,
                            puppet.clip_duration[i],world,ARCHER_RUN_SPEED / world);
            }else{
                ImGui::Text("%-18s %5.2f   in place",ARCHER_CLIPS[i].name,puppet.clip_duration[i]);
            }
            //The measured net turn, next to whether the table says this clip is a turn. They are
            //meant to agree: a big number with no T is a pivot being thrown away, and a T on a
            //clip reading near zero is a cycle about to wag the whole character.
            if (ARCHER_CLIPS[i].f_turns || puppet.clip_turn_deg[i] > 15.0f
                                        || puppet.clip_turn_deg[i] < -15.0f){
                ImGui::SameLine();
                ImGui::TextColored(ARCHER_CLIPS[i].f_turns ? ImVec4(0.6f,0.9f,0.6f,1.0f)
                                                           : ImVec4(0.95f,0.7f,0.3f,1.0f),
                                   "  %s%.0f deg",ARCHER_CLIPS[i].f_turns ? "T " : "? ",
                                   puppet.clip_turn_deg[i]);
            }
            ImGui::PopID();
        }

        //And what is actually on screen. `wanted` against the rate it got IS the foot slide.
        ImGui::Separator();
        const char* playing = (playing_clip >= 0 && playing_clip < CLIP_COUNT)
                            ? ARCHER_CLIPS[playing_clip].name : "none";
        ImGui::Text("playing   %s%s",playing,puppet.choice.f_placeholder ? "   (PLACEHOLDER)" : "");
        //The blend space, when one is running. The phase offset is what puts the two clips'
        //footfalls together and comes out of the measured column above, not out of a table.
        int second = puppet.choice.blend_clip;
        if (second >= 0 && second < CLIP_COUNT){
            ImGui::Text("blending  %s",ARCHER_CLIPS[second].name);
            ImGui::ProgressBar(puppet.choice.blend,ImVec2(-1,0),"weight");
            ImGui::Text("phase off %+.2f cycle",puppet.choice.blend_phase_offset);
        }
        ImGui::Text("rate      %.2f   wanted %.2f",puppet.choice.rate,puppet.choice.wanted_rate);
        ImGui::Text("model yaw %.0f deg",puppet.yaw_deg);
        if (puppet.choice.f_placeholder){
            ImGui::TextWrapped("Nothing is authored for this state, so Idle is standing in. "
                               "Missing today: run, jump, fall, land, draw, hang and climb.");
        }
    }

    ImGui::Separator();
    ImGui::TextWrapped("A/D or arrows run.  Space jumps - hold it for height.  J draws the bow, "
                       "release to loose; Up/Down tilt the aim.  E catches the rope over the second gap - lean "
                       "into the swing to build it, then let go with E to keep the speed or with "
                       "Space to add height.  K kicks: it punts a crate far "
                       "harder than walking into one does, and brings down the brick wall or the "
                       "cracked wall.  Down+K is a low stepping push kick that drives a crate along "
                       "the floor; Up+K a high kick that lifts it and reaches the top of a stack.  "
                       "Jump at a ledge too high to land on and you CATCH it: Space "
                       "then climbs up, S lets go, and holding away from it refuses the grab.  "
                       "S also drops through a platform.  Home (or Start) restarts, F1 shows the engine panels.");

    ImGui::End();
}
#endif
