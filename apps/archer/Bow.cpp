#include "Bow.h"
#include "Debug.h"

#include <math.h>

static Debugger* debug = new Debugger("Bow",DEBUG_INFO);

/*
    One node's mesh as a free-standing Object, unskinned, with its materials registered.

    BY NODE NAME, and the node's own transform is not looked at - see the prop convention in Bow.h.
    Each prop has several primitives (the bow four, the arrow three), which is several materials
    on one mesh; that is what vertex.matid and the object's material slots are for, and it is also
    why the bow has no room to grow: NUM_MATERIAL_SLOTS is 4 and the bow already uses all four.
*/
Object* Bow::LoadProp(const char* node_name, const char* object_name, GLTFLoader& loader,
                      Renderer* renderer){
    std::vector<Material> materials;
    Mesh* mesh = loader.GetMeshFromNode(node_name,&materials,false);
    if (!mesh){
        debug->Err("No mesh on node '%s' - nothing to equip\n",node_name);
        return NULL;
    }
    Object* object = new Object();
    object->SetMesh(mesh);      //takes the reference; the Object frees it on Destroy
    object->name = object_name;
    renderer->AddMaterials(materials);
    object->TakeMaterialNames(materials);
    return object;
}

/*
    Where the string's nock is, and how far the draw pulls it - read off the `Drawn` shape key.

    The key moves only the middle of the string (20 of the string's 50 vertices in this export,
    all by the same 0.272), so "the vertices that move furthest" IS the nock, with no knowledge of
    which primitive is the string. Half the largest move is the cut-off: a key that also flexed the
    limbs would move them less than the string's middle, and a limb that moved as far would be
    part of the draw's contact point anyway.

    Averaged rather than taking one vertex, because the string is a thin tube and its middle is a
    ring of vertices around the true centre line.
*/
void Bow::MeasureNock(){
    f_nock_measured = false;
    Mesh* mesh = bow.object ? bow.object->GetMesh() : NULL;
    if (!mesh){
        return;
    }
    const std::vector<vertex>& verts = mesh->GetVertices();
    const std::vector<morph_vertex>& morphs = mesh->GetMorphVertices();
    //One target, so entry i is vertex i's delta - see the note on Mesh::GetMorphVertices.
    if (verts.empty() || morphs.size() < verts.size()){
        debug->Err("Bow has no shape key - the nocked arrow sits at the grip and does not move "
                   "with the draw\n");
        return;
    }

    float furthest = 0.0f;
    for (size_t i = 0; i < verts.size(); i++){
        float d = morphs[i].pos.length();
        if (d > furthest){ furthest = d; }
    }
    if (furthest < 0.0001f){
        debug->Err("Bow's shape key moves nothing - the nocked arrow sits at the grip\n");
        return;
    }

    vec3 rest_sum(0.0f,0.0f,0.0f);
    vec3 pull_sum(0.0f,0.0f,0.0f);
    int count = 0;
    for (size_t i = 0; i < verts.size(); i++){
        if (morphs[i].pos.length() >= furthest * 0.5f){
            rest_sum = rest_sum + verts[i].pos;
            pull_sum = pull_sum + morphs[i].pos;
            count++;
        }
    }
    nock_rest = rest_sum * (1.0f / (float)count);
    nock_pull = pull_sum * (1.0f / (float)count);
    f_nock_measured = true;

    //The pull should run straight back along -Z, if the bow was exported facing her forward.
    vec3 dir = nock_pull;
    dir.normalize();
    float c = dir.dot(vec3(0.0f,0.0f,-1.0f));
    if (c > 1.0f){ c = 1.0f; }
    if (c < -1.0f){ c = -1.0f; }
    nock_pull_axis_error = acosf(c) * 57.29578f;

    debug->Info("Nock measured off the '%s' shape key: %d vertices, rest (%.4f,%.4f,%.4f), "
                "pulled (%.4f,%.4f,%.4f) at full draw, %.1f deg off -Z\n",
                "Drawn",count,nock_rest.x,nock_rest.y,nock_rest.z,
                nock_pull.x,nock_pull.y,nock_pull.z,nock_pull_axis_error);
    if (nock_pull_axis_error > BOW_PULL_AXIS_TOLERANCE_DEG){
        debug->Err("The bow string pulls %.1f deg away from -Z. The prop convention is that the "
                   "bow faces the character's forward - Blender -Y, glTF +Z - so the string pulls "
                   "back along -Z. The nocked arrow will sit crooked on it; check the bow's "
                   "orientation in Blender (see Bow.h).\n",nock_pull_axis_error);
    }
}

bool Bow::Build(GLTFLoader& loader, Skeleton* skeleton, Renderer* renderer,
                Animation* reference_clip){
    if (!skeleton || !renderer){
        debug->Err("Build: no skeleton or no renderer\n");
        return false;
    }
    if (!reference_clip || reference_clip->duration <= 0.0f){
        debug->Err("Build: no reference clip - the bow's grip is taken at its full-draw frame and "
                   "cannot be worked out without it\n");
        return false;
    }
    Bone* hand = skeleton->FindBone(BOW_GRIP_BONE);
    if (!hand){
        debug->Err("No bone '%s' - the bow cannot be equipped\n",BOW_GRIP_BONE);
        return false;
    }

    bow.object = LoadProp(BOW_NODE,BOW_OBJECT_NAME,loader,renderer);
    arrow.object = LoadProp(BOW_ARROW_NODE,BOW_ARROW_OBJECT_NAME,loader,renderer);
    if (!bow.object){
        return false;
    }
    MeasureNock();

    /*
        --- MEASURE IN THE RIG'S OWN SPACE, NOT IN THE WORLD ----------------------------------
        By the time this runs the skeleton has already been SCALED to stand ARCHER_MODEL_HEIGHT
        tall (about 2.02x). The grip is a rotation and would survive a uniform scale, but nock_gap
        is a distance and the log reports it in rig units beside the nock's own numbers; and
        neutralising the whole transform is what cannot be got wrong later by moving this call.
    */
    vec3 saved_position = skeleton->GetPosition();
    quat saved_rotation = skeleton->GetRotation();
    vec3 saved_scale    = skeleton->GetScale();
    skeleton->SetPosition(vec3(0.0f,0.0f,0.0f));
    skeleton->SetRotation(quat().identity());
    skeleton->SetScale(vec3(1.0f,1.0f,1.0f));

    /*
        Full draw: the reference clip's LAST frame. The same two calls MeasureClipPhases uses, in
        the same order and for the same reason - the zero-width SampleRootMotion window poses the
        root bone without reporting the sample as motion, and ApplyInterval poses everything else.
    */
    float full = reference_clip->duration;
    reference_clip->SampleRootMotion(full,full);
    reference_clip->ApplyInterval(full);

    /*
        --- THE GRIP ----------------------------------------------------------------------------
        With a socket bone, the socket IS the grip: zero offset and the fixed axis turn, nothing
        measured (see BOW_SOCKET_AXIS_FIX).

        Without one, the fallback: at full draw the bow should stand as authored - upright, facing
        her forward - which in the rig's space is identity. world = parent_world * local, so the
        local rotation that gives an identity world rotation is the inverse of the hand's.
    */
    Bone* socket = skeleton->FindBone(BOW_SOCKET);
    f_bow_on_socket = (socket != NULL);
    if (socket){
        bow.parent = socket;
        bow.grip = BOW_SOCKET_AXIS_FIX;
    }else{
        bow.parent = hand;
        bow.grip = hand->GetWorldRotation();
        bow.grip.inverse();
        bow.grip.normalize();
    }
    //Attached first, transform second: AttachChild is about the hierarchy and the local transform
    //is about where the child sits inside it.
    if (!bow.parent->AttachChild(bow.object)){
        debug->Err("Could not attach '%s' to '%s'\n",BOW_OBJECT_NAME,bow.parent->name.c_str());
        return false;
    }
    //Zero: the bow's origin IS its grip, by the prop convention.
    bow.object->SetPosition(vec3(0.0f,0.0f,0.0f));
    bow.object->SetRotation(bow.grip);
    if (f_bow_on_socket){
        debug->Info("Equipped '%s' on socket bone '%s'\n",BOW_OBJECT_NAME,BOW_SOCKET);
    }else{
        debug->Info("Equipped '%s' on %s: no '%s' bone in the file, so the grip is the fallback "
                    "(%.4f,%.4f,%.4f,%.4f), upright at full draw (%.3fs)\n",
                    BOW_OBJECT_NAME,BOW_GRIP_BONE,BOW_SOCKET,
                    bow.grip.x,bow.grip.y,bow.grip.z,bow.grip.w,full);
    }

    //Where the bow actually points at full draw, whichever way it was attached. Upright and
    //forward is +Y and +Z in the rig; this is the number to read when a socket is re-posed.
    {
        quat r = bow.object->GetWorldRotation();
        vec3 up = r * vec3(0.0f,1.0f,0.0f);
        vec3 fwd = r * vec3(0.0f,0.0f,1.0f);
        neutral_pitch_deg = atan2f(fwd.y,fwd.z) * 57.29578f;
        debug->Info("At full draw the bow's up is (%.2f,%.2f,%.2f) and its front (%.2f,%.2f,%.2f); "
                    "upright facing her forward would be (0,1,0) and (0,0,1). The pose aims "
                    "%.1f deg above level - the aim override's neutral\n",
                    up.x,up.y,up.z,fwd.x,fwd.y,fwd.z,neutral_pitch_deg);
    }

    //The quiver: on its socket or not at all. There is no sensible guess at where one goes.
    Bone* quiver_socket = skeleton->FindBone(BOW_QUIVER_SOCKET);
    if (quiver_socket){
        quiver.object = LoadProp(BOW_QUIVER_NODE,"quiver_worn",loader,renderer);
        if (quiver.object && quiver_socket->AttachChild(quiver.object)){
            quiver.parent = quiver_socket;
            quiver.grip = BOW_SOCKET_AXIS_FIX;
            quiver.object->SetPosition(vec3(0.0f,0.0f,0.0f));
            quiver.object->SetRotation(quiver.grip);
            debug->Info("Equipped 'quiver_worn' on socket bone '%s'\n",BOW_QUIVER_SOCKET);
        }
    }

    bool f_arrow = false;
    if (arrow.object){
        //Identity inside the bow: both props face her forward, see Bow.h.
        arrow.parent = bow.object;
        arrow.grip = quat().identity();
        if (bow.object->AttachChild(arrow.object)){
            arrow.object->SetRotation(arrow.grip);
            arrow.object->SetPosition(NockAt(0.0f));
            f_arrow = true;
        }else{
            debug->Err("Could not attach '%s' to the bow\n",BOW_ARROW_OBJECT_NAME);
        }
    }

    /*
        --- THE CHECK: how far the drawing hand is from the nock at full draw ---------------------
        Nothing uses this - the arrow rides the bow, not the hand - which is exactly why it is
        worth printing: it is the one place the draw pose and the bow can disagree without
        anything else noticing.
    */
    Bone* nock_hand = skeleton->FindBone(BOW_NOCK_BONE);
    if (nock_hand && f_nock_measured){
        vec3 nock_world = bow.object->GetWorldTransformScaleMatrix() * NockAt(1.0f);
        nock_gap = (nock_world - nock_hand->GetWorldPosition()).length();
        debug->Info("At full draw the nock is %.3f rig units (%.3f in the world) from %s\n",
                    nock_gap,nock_gap * saved_scale.x,BOW_NOCK_BONE);
    }

    //The props are CHILDREN of bones, so they inherit this scale the way the rest of her does -
    //which is why the measurements above were made without it.
    skeleton->SetScale(saved_scale);
    skeleton->SetRotation(saved_rotation);
    skeleton->SetPosition(saved_position);

    //The bend starts at zero whatever the export left the weight at, so the first frame drawn is
    //an unbent bow rather than whatever the artist happened to save.
    SetDraw(0.0f);
    return f_arrow;
}

void Bow::SetDraw(float draw01){
    if (!bow.object){
        return;
    }
    if (draw01 < 0.0f){ draw01 = 0.0f; }
    if (draw01 > 1.0f){ draw01 = 1.0f; }
    bow.object->SetShapekey(BOW_SHAPEKEY_DRAWN,draw01);
    //The arrow follows the string by the same number - see Bow.h.
    if (arrow.object && arrow.parent == bow.object){
        arrow.object->SetPosition(NockAt(draw01));
    }
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
