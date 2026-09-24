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
        debug->Err("Build: no reference clip - the full-draw checks are taken at its last frame "
                   "and cannot be made without it\n");
        return false;
    }
    Bone* socket = skeleton->FindBone(BOW_SOCKET);
    if (!socket){
        debug->Err("No '%s' bone in the rig - the bow has nowhere to go and she plays "
                   "empty-handed. It is a non-deforming child of %s; see BOW_SOCKET in Bow.h.\n",
                   BOW_SOCKET,BOW_GRIP_BONE);
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

    //The socket IS the grip: zero offset - the bow's origin is its grip, by the prop convention -
    //and the fixed axis turn, nothing measured (see BOW_SOCKET_AXIS_FIX). Attached first,
    //transform second: AttachChild is about the hierarchy, the local transform about where the
    //child sits inside it.
    bow.parent = socket;
    bow.grip = BOW_SOCKET_AXIS_FIX;
    if (!socket->AttachChild(bow.object)){
        debug->Err("Could not attach '%s' to '%s'\n",BOW_OBJECT_NAME,BOW_SOCKET);
        return false;
    }
    bow.object->SetPosition(vec3(0.0f,0.0f,0.0f));
    bow.object->SetRotation(bow.grip);
    debug->Info("Equipped '%s' on socket bone '%s'\n",BOW_OBJECT_NAME,BOW_SOCKET);

    //Where the bow actually points at full draw. Upright and forward is +Y and +Z in the rig;
    //this is the number to read when the socket is re-posed.
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
            //The opening: the end of the quiver along its +Y, the prop's up.
            Mesh* qmesh = quiver.object->GetMesh();
            if (qmesh && !qmesh->GetVertices().empty()){
                float top = -1e9f;
                for (const vertex& v : qmesh->GetVertices()){
                    if (v.pos.y > top){ top = v.pos.y; }
                }
                quiver_opening = vec3(0.0f,top,0.0f);
            }
        }
    }

    /*
        The arrow between the quiver and the string: a second Object sharing the arrow's mesh, on
        the drawing hand's socket - see TrackHand for why it is a second Object. Zero offset and
        the axis fix, like every prop on a socket: its origin is the nock, and the socket's head is
        where the animator put the nock.
    */
    Bone* arrow_socket = skeleton->FindBone(BOW_ARROW_SOCKET);
    if (arrow_socket && arrow.object && quiver.object){
        arrow_hand.object = new Object();
        arrow_hand.object->SetMesh(arrow.object->GetMesh());
        arrow_hand.object->name = BOW_ARROW_HAND_OBJECT_NAME;
        arrow_hand.object->SetMaterialNames(arrow.object->GetMaterialNames());
        if (arrow_socket->AttachChild(arrow_hand.object)){
            arrow_hand.parent = arrow_socket;
            arrow_hand.grip = BOW_SOCKET_AXIS_FIX;
            arrow_hand.object->SetPosition(vec3(0.0f,0.0f,0.0f));
            arrow_hand.object->SetRotation(arrow_hand.grip);
            arrow_hand.object->Hide();
            debug->Info("Equipped '%s' on socket bone '%s'; the quiver opens %.3f up its axis\n",
                        BOW_ARROW_HAND_OBJECT_NAME,BOW_ARROW_SOCKET,quiver_opening.y);
        }
    }else if (!arrow_socket){
        debug->Info("No '%s' bone - the arrow appears on the string, never in her hand\n",
                    BOW_ARROW_SOCKET);
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
    nock_hand = skeleton->FindBone(BOW_NOCK_BONE);
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

float Bow::TrackHand(bool f_drawing){
    if (!f_drawing || !bow.object || !nock_hand || !f_nock_measured){
        f_hand_on_string = false;
        f_arrow_in_hand = false;
        return 0.0f;
    }

    /*
        In WORLD space, through the bow's own matrix, rather than by carrying the hand into the
        bow's space: that needs an inverse, and this engine's fmat4::inverse_transform is rigid
        only while the rig is scaled 2.02x. Three points through the matrix instead - the nock at
        rest, the nock at full pull, the hand - and the scale falls out of the pull's own length,
        so the distance can be reported in rig units against BOW_HAND_ON_STRING.
    */
    fmat4& world = bow.object->GetWorldTransformScaleMatrix();
    vec3 nock0 = world * NockAt(0.0f);
    vec3 pull = (world * NockAt(1.0f)) - nock0;
    float pull_len2 = pull.dot(pull);
    if (pull_len2 < 1e-10f){
        return 0.0f;
    }
    vec3 hand = nock_hand->GetWorldPosition();
    float w = (hand - nock0).dot(pull) / pull_len2;
    if (w < 0.0f){ w = 0.0f; }
    if (w > 1.0f){ w = 1.0f; }
    float world_per_rig = sqrtf(pull_len2) / nock_pull.length();
    hand_off_string = (hand - (nock0 + pull * w)).length() / world_per_rig;

    if (hand_off_string < BOW_HAND_ON_STRING){
        f_hand_on_string = true;
    }

    //The step before: has she taken an arrow out of the quiver yet? Same units, same latch.
    if (arrow_hand.object && quiver.object && !f_hand_on_string){
        vec3 opening = quiver.object->GetWorldTransformScaleMatrix() * quiver_opening;
        hand_off_quiver = (arrow_hand.object->GetWorldPosition() - opening).length() / world_per_rig;
        if (hand_off_quiver < BOW_HAND_AT_QUIVER){
            f_arrow_in_hand = true;
        }
    }
    //On the string it is the bow's arrow now; the one in her hand has been handed over.
    if (f_hand_on_string){
        f_arrow_in_hand = false;
    }
    return f_hand_on_string ? w : 0.0f;
}

void Bow::SetArrowNocked(bool f_nocked, bool f_in_hand){
    if (arrow.object){
        if (f_nocked){
            arrow.object->Show();
        }else{
            arrow.object->Hide();
        }
    }
    if (arrow_hand.object){
        if (f_in_hand && !f_nocked){
            arrow_hand.object->Show();
        }else{
            arrow_hand.object->Hide();
        }
    }
}
