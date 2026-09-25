#ifndef _ARCHER_SIGN_H_
#define _ARCHER_SIGN_H_

#include "Object.h"
#include "GLTFLoader.h"
#include "TextMesh.h"

#include <vector>

/*
    Words on a prop: a signpost, a notice board, a crate stencil - any mesh with somewhere to write.

    The prop says WHERE the writing goes and the level says WHAT it says. Neither knows the other:
    a signpost is authored once in Blender with its boards marked, and every level that stands one
    up hands it a list of strings. The same signpost reads DROP / ROPE in one place and anything
    else in the next, with no numbers typed in for where its boards are.

    --- WHAT TO AUTHOR IN BLENDER ---------------------------------------------------------------
    On the prop, one CUBE EMPTY per place text can go, PARENTED TO THE PROP, named text_0,
    text_1, ... The number is the slot: a level's first string goes on text_0. Blender's own
    ".001" suffix is ignored, so two props can both have a text_0.

    THE EMPTY'S CUBE IS THE TEXT'S BOX. Scale it (not its display size) until the wireframe covers
    the area to write on: the string is centred in it and made as large as fits either its width
    or its height, whichever runs out first. Its DEPTH is the letters' relief - put the cube's
    middle on the board's face and the letters stand half proud and sit half sunk, which hides the
    gap an uneven plank would otherwise show behind them.

    UNROTATED, IT READS FROM BLENDER'S FRONT VIEW (numpad 1): along its X, up its Z, facing -Y -
    the same way every prop here faces. Rotate the empty to write on a board that is turned or
    tilted; the text follows it.

    --- WHY IT WORKS WITHOUT MORE ------------------------------------------------------------------
    The glTF exporter writes a child's transform RELATIVE TO ITS PARENT, and relabels Blender's
    axes as (x, z, -y) on the way. So an unrotated empty arrives with no rotation at all, its
    local X, Y, Z already the text mesh's right, up and toward-the-reader (core/TextMesh.h), its
    scale the box's half-extents in that order, and its position in the prop's own mesh units -
    wherever the prop itself stands in the .blend, which the engine ignores
    (asset_origin_convention). Checked on an export from Blender 4.5, 2026-09-25.

    RENDER THREAD for both functions: loading uploads the prop's mesh, and building bakes text.
*/

//Slots a prop can have. Far more than any sign needs; it bounds the name parse, nothing else.
#define SIGN_MAX_SLOTS 16

//One text_<n> empty, in the prop's mesh units.
struct SignSlot{
    int   index = 0;
    vec3  position = vec3(0.0f,0.0f,0.0f);          //the box's centre
    quat  rotation = quat(0.0f,0.0f,0.0f,1.0f);     //quat() leaves it uninitialised
    vec3  half = vec3(1.0f,1.0f,1.0f);  //half-extents: along the line, up the face, out of it
};

//A prop that can carry text: its mesh, its materials and where the text can go.
struct SignModel{
    Mesh*                   mesh = NULL;
    std::vector<Material>   materials;
    std::vector<SignSlot>   slots;      //by index, ascending
};

/*
    Reads `node` and its text_<n> children out of an already loaded file. False, and logged, if
    the node has no mesh. A prop with no slots loads - it is drawn and simply cannot be written on -
    but that is logged too, because it is nearly always a forgotten parent.
*/
bool LoadSignModel(GLTFLoader& loader, const char* node, SignModel& out);

/*
    The prop as an Object, with one child per string in `texts` - texts[i] on slot i. A NULL or
    empty string leaves its slot blank, and a string with no slot to go on is logged and dropped.

    The children sit in the prop's mesh units, so positioning and scaling the returned Object
    places the whole sign. `text_material` is a renderer material index for the letters.
*/
Object* BuildSignObject(const SignModel& model, const GlyphSet& glyphs, const char* const* texts,
                        int count, int text_material, const char* name);

#endif
