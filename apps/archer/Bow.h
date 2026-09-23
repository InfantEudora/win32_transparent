#ifndef _ARCHER_BOW_H_
#define _ARCHER_BOW_H_

#include "Object.h"
#include "Skeleton.h"
#include "GLTFLoader.h"
#include "Renderer.h"

/*
    The bow and the nocked arrow: getting them into the archer's hands and keeping them there.

    See apps/archer/bow_plan.md for the whole argument. What follows is what a reader of the code
    needs, and the two things that are easy to get wrong.

    --- THE RIG MOVES THE BOW, NOT A CLIP -------------------------------------------------------
    `Bone` is an Object (core/skeleton/Bone.h), animation poses bones by writing their LOCAL
    transform, and Object::GetWorldTransformScaleMatrix composes through the parent chain. So a
    plain Object attached to a hand bone rides that hand through every clip, for free, with no
    per-frame code at all. That is the whole mechanism and it is why this file has no Update() for
    position.

    The asset also contains a clip - `Bow_Draw` - which animates the bow node's TRANSLATION. It is
    the hand-follow, baked, for the one second of a draw, and playing it is a trap: nothing
    animates the bow in the other twenty-nine clips, so a bow driven that way FREEZES IN MID-AIR
    the moment the draw ends and stays there while she runs off.

    THAT TRAP IS ARMED BY NAMING. Animation::LinkObjects binds a clip's tracks to objects BY NAME
    over the whole subtree (ObjectAnimation.cpp:68), so an Object called "bow" would be found by
    `Bow_Draw` and its translation channel would fight the bone it is parented to. Hence
    BOW_OBJECT_NAME below, which is deliberately not the node name.

    --- THE GRIP IS IN THE FILE, EXPRESSED AGAINST A POSED FRAME --------------------------------
    The items are NOT placed at the hands in bind pose - the rig is in an A/T-pose there, and the
    bow sits 0.51 away, centred in front of the chest. They are placed at the hands at frame 0 of
    `Standing_DrawArrow`, because that is the pose the artist had the character in when the bow was
    put in her hand.

    So the grip is recovered rather than read: pose the reference clip at t=0, take the bone's world
    transform, and express the item's rest transform in it. Measured, it comes out as

        bow   in mixamorig:LeftHand    translation (0,0,0)   rotation 196.25 deg
        arrow in mixamorig:RightHand   translation (0,0,0)   rotation  90.00 deg about X

    EXCEPT THE BOW'S ROTATION, which is re-taken at the clip's LAST frame - full draw - because
    her left hand turns 50 degrees over the draw and the export never turns the bow with it. So
    196.25 is its frame-0 grip and not the one in use; see the note in Bow::Build.

    - ZERO TRANSLATION for both, because the items were snapped to the bones. That is not a
    coincidence, it is the signature of how they were authored, which makes it an invariant worth
    asserting: see grip_error and BOW_GRIP_EPSILON. If someone re-poses frame 0 of the reference
    clip the grip moves silently, and that assert is what turns a mystery into a log line.
*/

//The nodes in the .glb, and the bones they belong in.
#define BOW_NODE                    "bow"
#define BOW_ARROW_NODE              "arrow"
#define BOW_GRIP_BONE               "mixamorig:LeftHand"    //she holds the bow in her left
#define BOW_NOCK_BONE               "mixamorig:RightHand"   //and draws with her right

/*
    The names the attached Objects take, which MUST NOT be the node names - see the LinkObjects
    trap above. Anything else would do; these are just readable in object_list over MCP.
*/
#define BOW_OBJECT_NAME             "bow_held"
#define BOW_ARROW_OBJECT_NAME       "arrow_nocked"

//How far an item's rest position may sit from its bone's position at the reference frame before
//the authoring assumption is considered broken. Generous: the measured error is zero to five
//decimal places, so anything this size means something really moved.
#define BOW_GRIP_EPSILON            0.01f

//Which morph target on the bow mesh bends it. The export carries exactly one, named "Drawn".
#define BOW_SHAPEKEY_DRAWN          0

/*
    One thing held in one hand.

    `grip` is the item's LOCAL rotation inside its bone, and there is no local position because the
    measured one is zero - see the header note. It is kept rather than applied and forgotten so the
    inspector and the log can show what was derived.
*/
struct HeldItem{
    Object* object = NULL;
    Bone*   bone = NULL;
    quat    grip = quat().identity();
    float   grip_error = 0.0f;      //distance between item rest and bone at the reference frame
    bool    f_grip_suspect = false; //grip_error exceeded BOW_GRIP_EPSILON; see the header
};

class Bow{
public:
    /*
        Pulls both items out of the already-loaded .glb, works out their grips against
        `reference_clip` at t=0, and attaches them to their bones.

        RENDER THREAD ONLY - it ends in Mesh::SetMeshData by way of GetMeshFromNode, which talks to
        GL immediately, and it poses the skeleton, which nothing else may be doing at the time.
        Application::Init is the place.

        IT LEAVES THE SKELETON POSED at the reference clip's LAST frame (the bow's rotation is taken
    there - see the header note). That is harmless because
        the animation update re-poses every frame before anything is drawn, and it is exactly what
        MeasureClipPhases already does, but it is worth knowing if a caller measures something
        immediately afterwards.

        Returns false and logs if either item could not be equipped. A missing bow is survivable -
        the game plays, the archer just has empty hands - so callers are not expected to treat this
        as fatal.
    */
    bool Build(GLTFLoader& loader, Skeleton* skeleton, Renderer* renderer, Animation* reference_clip);

    /*
        How far the bow is drawn, 0 unbent to 1 full.

        DRIVEN FROM Stage::draw_ticks BY THE CALLER, never from the Bow_Draw clip's own weight
        track, and that is a correctness point rather than a preference: Bow_Draw is 1.100s and
        BOW_DRAW_TICKS is 0.600s, so the clip reaches full bend nearly half a second after the shot
        reaches full power. draw_ticks is the number that sets the arrow's speed, so deriving the
        bend from it makes a fully bent bow and a full-power shot the same fact.
    */
    void SetDraw(float draw01);

    //Whether the nocked arrow is on the string. The app hides it when one is loosed.
    void SetArrowNocked(bool f_nocked);

    HeldItem bow;
    HeldItem arrow;

private:
    bool EquipItem(HeldItem& item, const char* node_name, const char* bone_name,
                   const char* object_name, GLTFLoader& loader, Skeleton* skeleton,
                   Renderer* renderer);
};

#endif
