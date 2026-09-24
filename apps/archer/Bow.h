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
    plain Object attached to a bone rides it through every clip, for free, with no per-frame code
    at all. The bone is a SOCKET authored in the rig for the purpose - see below.

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

    Both props REQUIRE their socket: a rig without socket_bow plays empty-handed and says so, and
    one without socket_quiver shows no quiver. (Until 2026-09-23 a missing bow socket fell back to
    a grip recovered from the draw clip's last frame; that was removed once the sockets existed,
    since it was the one rotation the whole socket work was there to stop depending on.)
*/
#define BOW_SOCKET                  "socket_bow"
#define BOW_QUIVER_SOCKET           "socket_quiver"
/*
    The drawing hand's socket, for the arrow BETWEEN the quiver and the string. Keyed inside
    Standing_DrawArrow by the animator - it holds still until she grabs the arrow at the quiver,
    turns it through about 170 degrees on the way to the bow, and settles as her hand reaches the
    string - because the hand re-grips the arrow on the way and one fixed rotation could not hold
    it right at both ends. Optional: without it the arrow simply appears on the string.
*/
#define BOW_ARROW_SOCKET            "socket_arrow"
#define BOW_SOCKET_AXIS_FIX         quat(0.70710678f,0.0f,0.0f,0.70710678f)    //+90 deg about X

/*
    The names the attached Objects take, which should not be the node names - see the LinkObjects
    note above. Anything else would do; these are just readable in object_list over MCP.
*/
#define BOW_OBJECT_NAME             "bow_held"
#define BOW_ARROW_OBJECT_NAME       "arrow_nocked"
#define BOW_ARROW_HAND_OBJECT_NAME  "arrow_in_hand"

//Which morph target on the bow mesh bends it. The export carries exactly one, named "Drawn".
#define BOW_SHAPEKEY_DRAWN          0

//How close the drawing hand has to come to the string's pull line, in rig units, before the arrow
//is on the string and the string follows the hand. Measured against Standing_DrawArrow: the hand
//(its bone is the wrist) arrives at 0.08 and pulls at 0.02-0.11; during the reach and the carry
//it is never under 0.14. See Bow::TrackHand.
#define BOW_HAND_ON_STRING          0.12f

//How close the in-hand arrow's nock has to come to the quiver's opening, in rig units, for her to
//have taken an arrow out. Measured against Standing_DrawArrow with socket_arrow keyed: 0.076 a
//frame before the grab, 0.042 at it, and never under 0.13 again once she has moved off.
#define BOW_HAND_AT_QUIVER          0.06f

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
        Pulls the props out of the already-loaded .glb, attaches the bow and the quiver to their
        sockets and the arrow to the bow, measures the nock off the bow's shape key, and takes the
        full-draw checks (the pose's neutral aim, the nock gap) at `reference_clip`'s LAST frame.

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

        The caller decides where the number comes from: TrackHand below while the draw pose is on
        screen, Stage::draw_ticks when it is not (a draw at a run, until the mask layer).
    */
    void SetDraw(float draw01);

    /*
        --- THE STRING FOLLOWS THE HAND ------------------------------------------------------------
        Where the drawing hand is along the string's pull line, as a draw fraction - measured live,
        every tick, off the posed skeleton. So the visible bend is wherever her hand actually is:
        right at every clip speed, under the aim override, and through whatever loop or overdraw
        clip follows the draw, with nothing to re-measure when a clip changes.

        ONLY ONCE THE HAND IS ON THE STRING. Standing_DrawArrow spends its first 0.6s of 1.067
        reaching back to the quiver and carrying the arrow over (bow_plan.md §8 item 7), and a
        projection taken then is meaningless - at frame 0 it reads 1.17, a full draw, with her hand
        behind her back. So the hand has to come within BOW_HAND_ON_STRING of the pull line first;
        from then until the draw ends it is LATCHED on, which is also the moment the arrow is on the
        string (f_hand_on_string). The latch is what stops the arrow flickering when the hand grazes
        the threshold mid-pull (the measured gap peaks at 0.11 around clip time 0.8).

        Returns the draw fraction, 0 while the hand has not reached the string. `f_drawing` false
        releases the latch and returns 0.

        --- AND BEFORE THAT, THE ARROW IN HER HAND -------------------------------------------------
        The same shape of latch one step earlier: once the arrow on socket_arrow comes within
        BOW_HAND_AT_QUIVER of the quiver's opening she has taken one out, and it rides her hand
        (f_arrow_in_hand) until the string latch hands it to the bow. Measured rather than timed,
        so re-timing the clip moves the grab with it.

        TWO ARROW OBJECTS, NOT ONE RE-PARENTED. The in-hand arrow is a child of the socket and the
        nocked one a child of the bow, and the hand-off is a visibility swap. Moving one Object
        between parents mid-draw would edit a children list on the physics thread that the render
        thread may be walking at that moment.
    */
    float TrackHand(bool f_drawing);
    bool  f_hand_on_string = false;
    bool  f_arrow_in_hand = false;  //taken from the quiver, not yet on the string
    float hand_off_string = 0.0f;   //the last measured distance from the pull line, rig units
    float hand_off_quiver = 0.0f;   //the in-hand arrow's nock to the quiver opening, rig units

    //Whether the nocked arrow is on the string, and whether the other one is in her hand. The app
    //sets both from TrackHand's latches, or just the first for a draw with no draw pose.
    void SetArrowNocked(bool f_nocked, bool f_in_hand = false);

    //The nock in the BOW's local space at draw01. The arrow's origin is put exactly here.
    vec3 NockAt(float draw01) const { return nock_rest + nock_pull * draw01; }

    HeldItem bow;
    HeldItem arrow;
    HeldItem arrow_hand;            //the same arrow, between the quiver and the string
    HeldItem quiver;                //not held, but carried the same way - on a socket
    //Where the quiver is open, in the quiver's own space: the end of its mesh along +Y, the prop's
    //up. Read off the mesh at load.
    vec3 quiver_opening = vec3(0.0f,0.0f,0.0f);

    vec3  nock_rest = vec3(0.0f,0.0f,0.0f);     //bow-local, draw 0
    vec3  nock_pull = vec3(0.0f,0.0f,0.0f);     //bow-local, added at draw 1
    bool  f_nock_measured = false;              //false: no shape key found, arrow sits at the grip
    float nock_pull_axis_error = 0.0f;          //degrees between the pull and -Z; see the header
    float nock_gap = 0.0f;                      //hand to nock at full draw, in RIG units
    /*
        Where the draw pose ALREADY points the bow, in degrees above her forward, measured at full
        draw. The aim override turns her by aim_deg MINUS this, so a pose authored aiming a little
        up or down still ends exactly on aim_deg. Measured, never typed: a re-posed draw corrects
        itself. The plane is her forward (+Z) against up (+Y); the socket's small sideways lean is
        ignored, because the side view cannot see it.
    */
    float neutral_pitch_deg = 0.0f;

private:
    Object* LoadProp(const char* node_name, const char* object_name, GLTFLoader& loader,
                     Renderer* renderer);
    void MeasureNock();

    Bone* nock_hand = NULL;         //BOW_NOCK_BONE, found once in Build
};

#endif
