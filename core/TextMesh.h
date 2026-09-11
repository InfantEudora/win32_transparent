#ifndef _TEXT_MESH_H_
#define _TEXT_MESH_H_

#include "Mesh.h"
#include "type_vec2.h"
#include "type_vec3.h"

/*
    Text as geometry - backlog item 24, the mesh half of it.

    A string becomes ONE Mesh: the glyphs' triangles, copied and shifted along a pen, welded into
    a single vertex buffer. That is all this file does. There is no texture, no atlas and no font
    library anywhere in it.

    --- WHY THERE IS NO ATLAS ------------------------------------------------------------------
    An atlas is TEXTURE bookkeeping: it exists so a quad knows which rectangle of pixels to
    sample. Glyphs that are geometry have no pixels, so there is nothing to pack and nothing to
    look up. What a layout actually needs from a font is METRICS - how far the pen moves per
    character, and how far down a newline goes - and for the monospaced set in
    data/glyphs_unispace.glb those are two floats (see fonts_glyphs.json).

    The distinction is worth keeping because an SDF-atlas text path is still wanted (item 24 and
    docs/text_rendering_options.md recommend it). That one will share this file's LAYOUT and none
    of its storage. Letting "atlas" into this API would weld the two together and the second
    implementation would have to change the first.

    --- WHY IT IS NOT IN Mesh ------------------------------------------------------------------
    Mesh is the GPU-buffer class: Mesh.h includes glad, stdint and type_vertex.h, and nothing
    else. Glyphs arrive through GLTFLoader from a file on disk, so a Mesh::BuildTextMesh would
    make the vertex-buffer class know about tinygltf, about a path under data/ and about
    codepoints - and every app that touches a vertex buffer would pull that in.

    So the builder LOADS NOTHING. It is handed the glyphs, which is what makes it standalone:
    BuildTextMesh depends on Mesh, vertex and <vector>. LoadGlyphSetFromGLB, declared at the
    bottom, is the one function that knows about glTF, and it lives in its own translation unit
    (TextMeshGLB.cpp) so dropping the file drops the dependency.

    --- THREAD ---------------------------------------------------------------------------------
    BuildTextMesh is RENDER THREAD ONLY: it ends in Mesh::SetMeshData, which calls
    glNamedBufferData immediately (Mesh.cpp:51). So Application::Init and Application::PreRender
    are fine, and RunSimulationTick is not.

    MeasureText is the deliberate exception - pure maths, no GL, no state - so the simulation can
    decide WHERE a label goes on the tick the score changes, and leave the building to the frame.
    That split is the whole reason MeasureText exists separately.

    --- CONVENTIONS ----------------------------------------------------------------------------
      - ONE WORLD UNIT PER LINE at scale 1. Glyph ink spans y -0.2025 to 0.7967 about its
        baseline, so text is in em units and the caller scales the Object (or the layout).
      - THE ORIGIN IS THE LEFT END OF THE FIRST BASELINE, matching the glyphs' own convention
        (pen x = 0, baseline y = 0). Alignment shifts each line relative to that, so a score
        growing from 9999 to 10000 can stay centred without the app knowing the advance.
      - +X right, +Y up, the mesh 0.1 deep on +Z. They are extruded solids, so they light and
        shadow like anything else; there is nothing flat about them.
      - WINDING is whatever the glyph meshes carry. Translation and a positive uniform scale both
        preserve it, so back-face culling behaves.
*/

//Printable ASCII, which is what the export script generates and all this indexes by.
#define GLYPHSET_FIRST_CHAR 0x20
#define GLYPHSET_LAST_CHAR  0x7E
#define GLYPHSET_NUM_CHARS  (GLYPHSET_LAST_CHAR - GLYPHSET_FIRST_CHAR + 1)

//How each line sits relative to the text object's origin.
#define TEXT_ALIGN_LEFT     0
#define TEXT_ALIGN_CENTER   1
#define TEXT_ALIGN_RIGHT    2

/*
    What a font amounts to when glyphs are geometry: an array of meshes and two numbers.

    Fill it from a .glb with LoadGlyphSetFromGLB, or by hand from Primitives - the builder does
    not care where the meshes came from, only that it can read their vertices back.
*/
struct GlyphSet{
    //Indexed by (codepoint - GLYPHSET_FIRST_CHAR). NULL is LEGAL and means "advance the pen, draw
    //nothing": space has no geometry at all, and a glyph missing from the file must still take up
    //its column or every table after it shifts.
    Mesh*   glyph[GLYPHSET_NUM_CHARS] = {};

    //Pen step per character. ONE number because this set is monospaced - a proportional font
    //makes it float advance[GLYPHSET_NUM_CHARS] and changes exactly one line of the layout loop
    //(GlyphAdvance in TextMesh.cpp), which is why nothing outside that helper reads it.
    float   advance = 0.0f;

    //Baseline to baseline, for a newline.
    float   line_height = 1.0f;

    //True once at least one glyph and a usable advance are present. A set with advance 0 would
    //stack every character on the same spot, which is worth refusing rather than rendering.
    bool IsValid() const;

    //The mesh for a character, or NULL for space, an unknown character or anything outside
    //printable ASCII. Never an error - see the note on `glyph` above.
    Mesh* GetGlyph(char c) const;
};

struct TextLayout{
    float   scale = 1.0f;               //multiplies everything; 1 = one world unit per line
    int     align = TEXT_ALIGN_LEFT;
    //Material slot written into every vertex, so one bake can be white and the next red without a
    //second GlyphSet. Per-CHARACTER colour would need a different function; this is per string.
    int     matid = 0;
};

/*
    Width and height of `text` in the same units BuildTextMesh would produce, without touching
    the GPU. Width is the widest line's advance total (so it includes the trailing sidebearing of
    the last glyph, which is what you want for centring), height is lines * line_height * scale.

    Safe on any thread, including the physics thread.
*/
vec2 MeasureText(const GlyphSet& glyphs, const char* text, const TextLayout& layout = TextLayout());

/*
    Bakes `text` into one mesh. Newlines start a new line; every other unknown character advances
    the pen and draws nothing.

    `reuse`, when given, is re-filled instead of a new Mesh being allocated - and that is the
    normal case for anything that changes, because Mesh has no destructor: deleting one drops its
    VBO and VAO on the floor, so rebuilding a score label by delete/new would leak two GL objects
    per point scored. SetMeshData re-uploads into the same buffer (InitVBOVAO is a no-op once vbo
    is non-zero), so reuse costs one glNamedBufferData and nothing else.

    RETURNS NULL when the text is empty or contains no ink ("", "   ", "\n"). That is legitimate
    input, not an error, so it is not logged - hide the Object instead. `reuse` is left untouched
    in that case rather than emptied, because Mesh::SetMeshData(v,0) reaches vertices.at(0) on an
    empty vector and this build has exceptions off.

    RENDER THREAD ONLY.
*/
Mesh* BuildTextMesh(const GlyphSet& glyphs, const char* text,
                    const TextLayout& layout = TextLayout(), Mesh* reuse = NULL);

/*
    Fills a GlyphSet from a .glb of one mesh per glyph, as tools/blender_glyph_meshes.py writes:
    every node named glyph_<4 hex digits of codepoint>[_<char>], identity transform, geometry
    already in glyph space.

    `advance` and `line_height` are passed in rather than read from the file because glTF has
    nowhere to put them - the export script writes them to a JSON sidecar (fonts_glyphs.json)
    instead. Two floats at the call site keeps this function free of a JSON parser; a loader for
    the sidecar can be layered on later without changing anything here.

    Returns false and logs if the file yields no glyphs. RENDER THREAD ONLY - it builds meshes.
*/
bool LoadGlyphSetFromGLB(GlyphSet& out, const char* filename, float advance, float line_height);

#endif
