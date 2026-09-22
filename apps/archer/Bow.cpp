#include "Bow.h"
#include "Debug.h"

#include <math.h>

static Debugger* debug = new Debugger("Bow",DEBUG_INFO);

/*
    Loads one node's mesh, works out where it sits in its bone, and hangs it there.

    The skeleton is expected to be ALREADY POSED at the reference frame when this is called - Build
    does that once for both items rather than posing twice, since posing is the expensive half.
*/
bool Bow::EquipItem(HeldItem& item, const char* node_name, const char* bone_name,
                    const char* object_name, GLTFLoader& loader, Skeleton* skeleton,
                    Renderer* renderer){
    item.bone = skeleton->FindBone(bone_name);
    if (!item.bone){
        debug->Err("No bone '%s' - '%s' cannot be equipped\n",bone_name,node_name);
        return false;
    }

    /*
        BY NODE NAME, and unskinned.

        The bow and the arrow are separate nodes in the same .glb as the character, each with
        several primitives - four and three - which is several materials on one mesh. That is
        exactly what vertex.matid and the object's material slots are for, and it is also why the
        bow has no room to grow: NUM_MATERIAL_SLOTS is 4 and the bow already uses all four.
    */
    std::vector<Material> materials;
    Mesh* mesh = loader.GetMeshFromNode(node_name,&materials,false);
    if (!mesh){
        debug->Err("No mesh on node '%s' - nothing to equip\n",node_name);
        return false;
    }

    item.object = new Object();
    item.object->SetMesh(mesh);     //takes the reference; the Object frees it on Destroy
    item.object->name = object_name;
    renderer->AddMaterials(materials);
    item.object->TakeMaterialNames(materials);

    /*
        --- THE GRIP ----------------------------------------------------------------------------
        The item's rest transform expressed in the bone's space at the reference frame. See the
        header: the items were snapped to the hands in that pose, so this is recovering a number
        the artist already decided rather than inventing one.

        world = parent_world * local  (Object::GetWorldRotation), so local = parent_world^-1 * world.
    */
    vec3 item_rest_pos = loader.GetNodePosition(node_name);
    quat item_rest_rot = loader.GetNodeRotation(node_name);
    vec3 bone_pos = item.bone->GetWorldPosition();
    quat bone_rot = item.bone->GetWorldRotation();

    quat bone_inverse = bone_rot;
    bone_inverse.inverse();
    item.grip = bone_inverse * item_rest_rot;
    item.grip.normalize();

    /*
        THE INVARIANT, and the reason it is checkable at all.

        A zero offset is not a happy accident - it is what "the artist snapped the item to the
        bone" looks like in the numbers. So it can be asserted, and if it ever stops holding, the
        authoring assumption behind the whole grip has changed and this says so in one line rather
        than leaving a bow hovering next to a hand for someone to debug from the attachment code.

        Logged rather than fatal: a suspect grip still renders, and being able to LOOK at what went
        wrong is worth more than refusing to start.
    */
    item.grip_error = (item_rest_pos - bone_pos).length();
    item.f_grip_suspect = (item.grip_error > BOW_GRIP_EPSILON);

    //Attached first, transform second: AttachChild is about the hierarchy and the local transform
    //is about where the child sits inside it, and doing it in this order means nothing the attach
    //does to the transform can be silently inherited.
    if (!item.bone->AttachChild(item.object)){
        debug->Err("Could not attach '%s' to bone '%s'\n",object_name,bone_name);
        return false;
    }
    //Zero, deliberately - see the header. Not "close enough to zero to ignore": measured zero.
    item.object->SetPosition(vec3(0.0f,0.0f,0.0f));
    item.object->SetRotation(item.grip);

    debug->Info("Equipped '%s' on %s: grip (%.4f,%.4f,%.4f,%.4f), offset %.5f%s\n",
                object_name,bone_name,
                item.grip.x,item.grip.y,item.grip.z,item.grip.w,
                item.grip_error,item.f_grip_suspect ? "  <-- SUSPECT" : "");
    if (item.f_grip_suspect){
        debug->Err("'%s' rest sits %.4f from bone '%s' at the reference frame, over the %.4f "
                   "tolerance. The items are supposed to be SNAPPED to the bones in that pose - so "
                   "either the reference clip's first frame was re-posed, or the item was moved. "
                   "The bow will be in the wrong place; see the grip note in Bow.h.\n",
                   node_name,item.grip_error,bone_name,BOW_GRIP_EPSILON);
    }
    return true;
}

bool Bow::Build(GLTFLoader& loader, Skeleton* skeleton, Renderer* renderer,
                Animation* reference_clip){
    if (!skeleton || !renderer){
        debug->Err("Build: no skeleton or no renderer\n");
        return false;
    }
    if (!reference_clip){
        debug->Err("Build: no reference clip - the grip is expressed against its first frame and "
                   "cannot be worked out without it\n");
        return false;
    }

    /*
        Pose the reference clip at its first frame, which is the pose the items were placed in.

        Same two calls MeasureClipPhases uses, in the same order and for the same reason: the
        zero-width SampleRootMotion window poses the root bone without reporting the sample as
        motion, and ApplyInterval poses everything else.
    */
    reference_clip->SampleRootMotion(0.0f,0.0f);
    reference_clip->ApplyInterval(0.0f);

    /*
        --- MEASURE IN THE RIG'S OWN SPACE, NOT IN THE WORLD ----------------------------------
        GetWorldPosition walks the parent chain, and by the time this runs the skeleton has already
        been SCALED to stand ARCHER_MODEL_HEIGHT tall - roughly 2.02x. GLTFLoader::GetNodePosition
        reports the node in the rig's own units. Comparing the two directly compares a scaled
        quantity with an unscaled one, and the answer is the true offset plus whatever the scale
        added: measured 0.53759 for a grip whose real offset is zero, which is exactly the size of
        error that looks like a genuinely misplaced bow.

        Rotation escaped it - a uniform scale does not rotate anything, and the skeleton happened
        to be unrotated here - but that is luck about call order rather than a property worth
        relying on. Neutralising the whole transform for the measurement removes all three
        components at once and cannot be got wrong later by moving this call.
    */
    vec3 saved_position = skeleton->GetPosition();
    quat saved_rotation = skeleton->GetRotation();
    vec3 saved_scale    = skeleton->GetScale();
    skeleton->SetPosition(vec3(0.0f,0.0f,0.0f));
    skeleton->SetRotation(quat().identity());
    skeleton->SetScale(vec3(1.0f,1.0f,1.0f));

    bool f_bow   = EquipItem(bow,BOW_NODE,BOW_GRIP_BONE,BOW_OBJECT_NAME,loader,skeleton,renderer);
    bool f_arrow = EquipItem(arrow,BOW_ARROW_NODE,BOW_NOCK_BONE,BOW_ARROW_OBJECT_NAME,
                             loader,skeleton,renderer);

    //The items are CHILDREN of bones, so they inherit this scale the way the rest of her does -
    //which is why the grip above had to be derived without it.
    skeleton->SetScale(saved_scale);
    skeleton->SetRotation(saved_rotation);
    skeleton->SetPosition(saved_position);

    if (f_bow && bow.object){
        //The bend starts at zero whatever the export left the weight at, so the first frame drawn
        //is an unbent bow rather than whatever the artist happened to save.
        SetDraw(0.0f);
    }
    return f_bow && f_arrow;
}

void Bow::SetDraw(float draw01){
    if (!bow.object){
        return;
    }
    if (draw01 < 0.0f){ draw01 = 0.0f; }
    if (draw01 > 1.0f){ draw01 = 1.0f; }
    bow.object->SetShapekey(BOW_SHAPEKEY_DRAWN,draw01);
}

void Bow::SetArrowNocked(bool f_nocked){
    if (!arrow.object){
        return;
    }
    if (f_nocked){
        arrow.object->Show();
    }else{
        arrow.object->Hide();
    }
}
