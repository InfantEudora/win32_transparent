#ifndef _ARCHER_BOW_H_
#define _ARCHER_BOW_H_

#include "Object.h"
#include "Skeleton.h"
#include "GLTFLoader.h"
#include "Renderer.h"

/*
    The bow and the nocked arrow: getting them into the archer's hands and keeping them there.

    See apps/archer/bow_plan.md for the whole argument - §4 "The target" for the prop convention
    this file now follows. What follows is what a reader of the code needs.

    --- THE PROP CONVENTION ---------------------------------------------------------------------
    Since the 2026-09-23 export the bow and the arrow are authored as PROPS, not as things placed
    in a pose:
      - the ORIGIN is the attach point: the bow's is the grip, the arrow's is its nock end;
      - they FACE THE SAME WAY THE CHARACTER DOES - toward the camera in Blender's front view,
        Blender -Y, which the glTF exporter turns into +Z, which is the rig's forward;
      - WHERE THEY SIT IN THE BLENDER SCENE MEANS NOTHING. The node transforms are ignored, the
        same as every other asset here (models are spread across the scene for authoring).
    So nothing here reads GetNodePosition or GetNodeRotation any more. Everything is either
    declared by that convention or measured from the meshes themselves.

    --- THE RIG MOVES THE BOW, NOT A CLIP -------------------------------------------------------
    `Bone` is an Object (core/skeleton/Bone.h), animation poses bones by writing their LOCAL
    transform, and Object::GetWorldTransformScaleMatrix composes through the parent chain. So a
    plain Object attached to a hand bone rides that hand through every clip, for free, with no
    per-frame code at all.

    The bow's GRIP is the one rotation that bone needs: chosen so that at FULL DRAW (the last frame
    of Standing_DrawArrow) the bow stands upright facing her forward - which, by the convention, is
    the bow at identity in the rig's space. Full draw rather than frame 0 because her left hand
    turns 50 degrees over the draw and full draw is the pose that is looked at. The cost is that
    every other clip carries it 50 degrees differently; a socket bone in the rig removes that, and
    is the next step (bow_plan.md §8, items 8-9).

    Named BOW_OBJECT_NAME rather than the node name, deliberately: Animation::LinkObjects binds a
    clip's tracks BY NAME over the whole subtree, so an Object called "bow" would be driven by any
    clip that animates a node called "bow". This export has no such clip (Bow_Draw is gone), but
    the name costs nothing and the trap re-arms the day one comes back.

    --- THE ARROW RIDES THE BOW, AT THE NOCK ----------------------------------------------------
    The nocked arrow is a CHILD OF THE BOW, not of the drawing hand. Where an arrow points is
    decided by the bow - nock on the string, shaft past the grip - and a rigid grip in a hand
    cannot express that: it pointed back over her shoulder at full draw (bow_plan.md §9).

    Both props face the same way, so INSIDE THE BOW THE ARROW'S ROTATION IS IDENTITY. Its position
    is the string's nock point, which moves as the bow is drawn - and that is MEASURED from the
    bow's `Drawn` shape key rather than typed: the vertices the key moves furthest are the middle
    of the string, their rest position is the nock at draw 0, and their delta is how far the draw
    pulls it. SetDraw moves the arrow by the same number that bends the string, so the two cannot
    disagree. Measured in this export: rest z -0.094, pulled 0.272 straight back along -Z.

    Two numbers are checked at build and logged, each able to mean only one thing:
      - nock_pull_axis_error: the pull should be along -Z. If it is not, the bow was not exported
        facing her forward, and the arrow will be nocked crooked.
      - nock_gap: how far the drawing hand is from the nock at full draw. The hand is not what
        holds the arrow any more, so nothing corrects this - it is how well the draw pose and the
        bow agree, and it is the number the aim work (animation_plan.md, Step 3) keeps honest.
*/

//The nodes in the .glb, and the bones they belong in.
#define BOW_NODE                    "bow"
#define BOW_ARROW_NODE              "arrow"
#define BOW_QUIVER_NODE             "quiver"
#define BOW_GRIP_BONE               "mixamorig:LeftHand"    //she holds the bow in her left
#define BOW_NOCK_BONE               "mixamorig:RightHand"   //and draws with her right

/*
    --- SOCKET BONES ----------------------------------------------------------------------------
    Non-deforming bones in the rig whose head is a prop's attach point and whose orientation is
    how the prop sits there - authored in Blender, and animated by the rig in every clip for free.
    A prop is attached to its socket at zero offset and ONE FIXED ROTATION, BOW_SOCKET_AXIS_FIX,
    and nothing is measured.

    Why a fixed rotation and not identity: the glTF exporter converts MESH coordinates to Y-up
    (Blender -Y forward, +Z up become glTF +Z, +Y) but writes a bone's own local axes through
    unchanged. So a prop lined up with a bone in Blender (Copy Transforms, head/tail 0) is a
    quarter turn about X away from the same prop at identity inside that bone here. Confirmed on
    the 2026-09-23 export: the bow, posed on socket_bow with a constraint in Blender, exported at
    exactly socket * BOW_SOCKET_AXIS_FIX, 0.00 degrees apart - and the old export's arrow grip,
    recovered by measurement, was the same quarter turn. With the fix, "looks right on the socket
    in Blender" and "looks right in the game" are the same statement.

    A bow with no socket in the file falls back to the full-draw grip described above; a quiver
    with no socket is simply not shown.
*/
#define BOW_SOCKET                  "socket_bow"
#define BOW_QUIVER_SOCKET           "socket_quiver"
#define BOW_SOCKET_AXIS_FIX         quat(0.70710678f,0.0f,0.0f,0.70710678f)    //+90 deg about X

/*
    The names the attached Objects take, which should not be the node names - see the LinkObjects
    note above. Anything else would do; these are just readable in object_list over MCP.
*/
#define BOW_OBJECT_NAME             "bow_held"
#define BOW_ARROW_OBJECT_NAME       "arrow_nocked"

//Which morph target on the bow mesh bends it. The export carries exactly one, named "Drawn".
#define BOW_SHAPEKEY_DRAWN          0

//How far off -Z the string may pull, in degrees, before the bow is reported as not facing her
//forward. Generous: the export measures zero.
#define BOW_PULL_AXIS_TOLERANCE_DEG 5.0f

/*
    One prop and what it hangs from.

    `grip` is the prop's LOCAL rotation inside its parent. There is no local position for the bow
    because its origin IS the grip; the arrow's local position is the nock and changes with the
    draw, so it lives on Bow rather than here.
*/
struct HeldItem{
    Object* object = NULL;
    Object* parent = NULL;          //a hand bone for the bow, the bow for the arrow
    quat    grip = quat().identity();
};

class Bow{
public:
    /*
        Pulls both props out of the already-loaded .glb, gives the bow its grip against
        `reference_clip`'s LAST frame, measures the nock off the bow's shape key, and attaches the
        bow to the hand and the arrow to the bow.

        RENDER THREAD ONLY - it ends in Mesh::SetMeshData by way of GetMeshFromNode, which talks to
        GL immediately, and it poses the skeleton, which nothing else may be doing at the time.
        Application::Init is the place.

        IT LEAVES THE SKELETON POSED at the reference clip's last frame. Harmless - the animation
        update re-poses every frame before anything is drawn, as MeasureClipPhases also relies on -
        but worth knowing if a caller measures something immediately afterwards.

        Returns false and logs if either prop could not be equipped. A missing bow is survivable -
        the game plays, the archer just has empty hands - so callers are not expected to treat this
        as fatal.
    */
    bool Build(GLTFLoader& loader, Skeleton* skeleton, Renderer* renderer, Animation* reference_clip);

    /*
        How far the bow is drawn, 0 unbent to 1 full. Bends the string AND moves the nocked arrow
        with it - one number for both, so they cannot drift.

        DRIVEN FROM Stage::draw_ticks BY THE CALLER, never from a clip's own weight track: the
        draw's length is the rules' BOW_DRAW_TICKS, and draw_ticks is the number that sets the
        arrow's speed, so deriving the bend from it makes a fully bent bow and a full-power shot
        the same fact.
    */
    void SetDraw(float draw01);

    //Whether the nocked arrow is on the string. The app hides it when one is loosed.
    void SetArrowNocked(bool f_nocked);

    //The nock in the BOW's local space at draw01. The arrow's origin is put exactly here.
    vec3 NockAt(float draw01) const { return nock_rest + nock_pull * draw01; }

    HeldItem bow;
    HeldItem arrow;
    HeldItem quiver;                //not held, but carried the same way - on a socket
    bool     f_bow_on_socket = false;   //false: the full-draw grip fallback is in use

    vec3  nock_rest = vec3(0.0f,0.0f,0.0f);     //bow-local, draw 0
    vec3  nock_pull = vec3(0.0f,0.0f,0.0f);     //bow-local, added at draw 1
    bool  f_nock_measured = false;              //false: no shape key found, arrow sits at the grip
    float nock_pull_axis_error = 0.0f;          //degrees between the pull and -Z; see the header
    float nock_gap = 0.0f;                      //hand to nock at full draw, in RIG units

private:
    Object* LoadProp(const char* node_name, const char* object_name, GLTFLoader& loader,
                     Renderer* renderer);
    void MeasureNock();
};

#endif
