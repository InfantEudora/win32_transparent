#ifndef _UI_OVERLAY_H_
#define _UI_OVERLAY_H_

#include <stdint.h>
#include <vector>
#include <string>
//GLuint and the enums. This file used to carry the Android/desktop #if itself; glad.h is that
//switch now, for every consumer at once - on Android it is <GLES3/gl31.h> and nothing else.
#include "glad.h"
#include "type_vec2.h"
#include "UIFont.h"
//ATLAS_TEXTURE_UNIT and THEME_TEXTURE_UNIT below are entries in the engine-wide texture unit map,
//not numbers this class chooses. Its own header rather than Renderer.h so that this stays a
//leaf include.
#include "TextureUnits.h"

class Shader;

/*
    The 2D overlay: rounded rectangles and text, in screen space, in one pass.

    Backlog item 81, and the full reasoning is in docs/ui_overlay_plan.md. The short version is
    that an SDF glyph and an SDF rounded box are the SAME SHADER - same blend state, same pass,
    same vertex format - so building them as two things would mean writing two of everything.

    --- WHAT THIS DELIBERATELY IS NOT ------------------------------------------------------------
    A UI framework. There is no layout, no wrapping, no hit-testing, no focus and no widget state,
    and there should not be: three draw calls plus InputController's own rect list is a game pad,
    and a game pad is all a game here needs. Item 24 already measured everything above the text
    primitive at "weeks rather than days" - the way this stays a weekend is by refusing to start.

    Hit-testing in particular is already elsewhere and must stay there: InputController::
    SubmitPointer walks its own rect list and never consults this class. Input and drawing are
    decoupled, and nothing flows back from here.

    --- THREAD -----------------------------------------------------------------------------------
    RENDER THREAD ONLY, all of it. Begin, the Add calls and Draw touch the vertex vector and GL, with no
    lock anywhere. The batch is rebuilt from scratch every frame - see Begin - so there is nothing
    for another thread to read and nothing to keep in sync.

    That is the whole reason this needs none of TextMesh's SetText/RebuildIfDirty machinery.
    TextMesh bakes geometry into a Mesh and Mesh::SetMeshData uploads immediately, so it has to
    straddle the physics/render split. An overlay that regenerates its whole vertex list per frame
    from whatever the app hands it has no dirty flag, no staleness, and no cross-thread upload.
    It also falls out for free on Android, where GPU objects do not survive an EGL context loss.

    --- COORDINATES ------------------------------------------------------------------------------
    PIXELS, top-left origin, +Y DOWN. That matches InputController's TouchRect exactly, which
    matters because the buttons those describe are the first thing that will be drawn here, and a
    draw path that disagreed with the hit-test about where a rectangle is would be a genuinely
    nasty bug to find.

    Pixels rather than millimetres is a narrowing of what docs/ui_overlay_plan.md section 5 asked
    for, and it is deliberate: a rasteriser legitimately speaks pixels, and it is LAYOUT that has
    to speak millimetres or it will not survive a change of screen. Keeping the conversion in the
    caller (one multiply, against a dpi the platform supplies) is the same seam ImGui uses, and
    avoids inventing a dpi abstraction before item 70 lands a real GetDisplayDPI on win32.
*/

//RGBA, one byte each, in that order in memory - so the low byte is red, matching how the vertex
//attribute is read back as a normalised vec4.
inline uint32_t UIColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255){
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
}

#define UI_ALIGN_LEFT   0
#define UI_ALIGN_CENTER 1
#define UI_ALIGN_RIGHT  2

/*
    The four insets of a NINE-SLICE, in pixels of the source sprite.

    A themed panel cannot simply be stretched: its corners are drawn at a size the artist chose,
    and scaling them is immediately visible as a skewed bevel. So the sprite is cut into nine
    regions - four corners that never scale, four edges that scale along one axis only, and a
    centre that scales both ways - and these four numbers are where the cuts fall.

    Kept as source-pixel insets rather than as fractions because that is what the art means: the
    bomber panel's corner piece is 30 px of chamfered wood whatever size the window is drawn at.
    Fractions would make the corner grow with the window, which is the exact thing nine-slicing
    exists to prevent.

    Measured values for the assets cut from the mockups are in docs/ui_sprites.md.
*/
struct ui_nine_inset{
    float left   = 0.0f;
    float top    = 0.0f;
    float right  = 0.0f;
    float bottom = 0.0f;
};

//The nine regions, row-major from the top-left: 0 1 2 / 3 4 5 / 6 7 8. Named so the debug colours
//and any later textured path index them the same way.
#define UI_NINE_COUNT 9

/*
    Splits `min..max` into the nine regions, writing UI_NINE_COUNT pairs into out_min/out_max.

    FREE FUNCTION, not a method, because both the debug drawing below and whatever eventually
    samples a theme texture have to agree about where the cuts are. Two copies of this arithmetic
    that drifted apart would show up as a one-pixel seam in the finished UI and be very hard to
    place; one function cannot drift.

    Handles the case that makes naive implementations fail: a target RECTANGLE SMALLER THAN ITS OWN
    INSETS. Left plus right can exceed the width the caller asked for, and the corners then overlap
    and the edge regions come out inside-out. The insets are scaled down together when that
    happens, so a panel squeezed below its natural size degrades to just its corners instead of
    turning into garbage.
*/
void UINineSliceRegions(vec2 min, vec2 max, const ui_nine_inset& inset,
                        vec2* out_min, vec2* out_max);

/*
    One corner of one quad. 52 bytes, since `sprite` joined it.

    Compare ImGui's ImDrawVert at 20 (pos, uv, col) and core/type_vertex.h's `vertex` at 48
    (pos, normal, tangent, uv, matid). It is its own type rather than a reuse of `vertex`, which
    would mean paying for a normal and a tangent the overlay shader never reads.

    `local` and `half_extent` are what make the rounded box analytic. `local` varies across the
    quad (it is +/- half_extent at the corners and interpolates between), `half_extent` is
    constant across it, and the fragment shader has everything it needs to evaluate a rounded-box
    distance field without the corner ever existing as geometry. That is what buys exact corners
    at any size, free antialiasing, and outlines as abs(d) - w.
*/
struct ui_vertex{
    vec2     pos;               //screen pixels, top-left origin
    vec2     uv;                //atlas; for an untextured quad, the font's solid texel
    vec2     local;             //offset from the rect's centre, pixels
    vec2     half_extent;       //the rect's half size, pixels
    float    radius;            //corner radius, pixels
    float    outline;           //0 = filled; >0 = half-width of an outline, pixels
    float    distance_scale;    //pixels per unit of sampled field - see UIFontDistanceScale
    /*
        0 = this quad is a distance field (a glyph, or a rounded box). 1 = it is a THEMED SPRITE,
        sampled from the theme atlas as ordinary colour.

        A per-vertex float rather than a uniform, because the whole design here is one draw call
        for the entire overlay - a uniform would mean a draw call per mode and the batch would stop
        being a batch. It is `flat` in the shader: all six vertices of a quad carry the same value,
        so interpolating it would be arithmetic that cannot change the answer.
    */
    float    sprite;
    uint32_t color;             //RGBA8, see UIColor
};

class UIOverlay{
public:
    /*
        Loads the baked font and builds the shader and the vertex buffer. RENDER THREAD.

        Returns false and logs if either the font or the shader is missing; the overlay is then
        inert - every Add* is a no-op and Draw draws nothing - rather than absent, so an app does
        not have to guard every call to survive a missing asset.
    */
    bool Init(const char* font_asset = "fonts/mono_sdf.fnt",
              const char* vert_asset = "shaders/ui_overlay.vert",
              const char* frag_asset = "shaders/ui_overlay.frag");

    bool IsReady() const { return f_ready; }

    /*
        Builds every GPU object this owns AGAIN, after the context they lived in went away.
        RENDER THREAD, and the new context must already be current.

        ANDROID. GPU objects do not survive the EGL context, and the port destroys and recreates
        its window - and with it the context - whenever the app is backgrounded, orientation lock
        or not. So this is not an edge case to be defensive about; it is what happens when the
        user takes a call. On win32 a context is never lost and nothing calls this.

        Re-reads nothing from disk. The font bytes are still in RAM because core/File.h's LoadFile
        never frees - "one file, one buffer, one owner", valid for the life of the process - so
        the atlas is re-uploaded from the same pointer Init used. That is docs/ui_overlay_plan.md
        section 7's "keep the pixels rather than freeing them", already satisfied by the file
        layer rather than needing a copy of its own.

        Safe to call when Init failed or never ran: it stays inert rather than half-built.
    */
    bool ReUploadGPUObjects();

    //Discards last frame's quads and records the size of the surface being drawn to. Call once a
    //frame before any Add*. `screen_w/h` are the WINDOW's size, not the 3D viewport's - the
    //overlay covers the whole window even where the scene is drawn into a sub-rectangle.
    void Begin(int screen_w, int screen_h);

    //A filled rounded rectangle. `radius` is clamped to half the shorter side, so a big enough
    //radius gives a capsule or a circle rather than an inside-out artefact.
    void AddRect(vec2 min, vec2 max, float radius, uint32_t color);

    //The same rectangle's outline, `thickness` pixels wide, centred on the edge.
    void AddRectOutline(vec2 min, vec2 max, float radius, float thickness, uint32_t color);

    /*
        The nine regions of a nine-slice, each in a flat colour naming what it does.

        A SCAFFOLD, and deliberately the first half of the job. Drawing a themed panel needs a
        second texture in this batch - the atlas here is the R8 font, and painted wood is neither
        single-channel nor a distance field - which is a change to the vertex format, the shader
        and the asset pipeline at once. The geometry is separable from all of it, and it is the
        part that is easy to get subtly wrong, so it goes first and gets looked at.

        Colours name the ROLE, not the cell: corners, the two edge pairs, and the centre. That is
        what wants checking - which regions move when the panel is resized - and four colours
        answer it at a glance where nine arbitrary ones would just be a grid. No two adjacent
        regions share a role, so every cut is still visible.
    */
    void AddNineSliceDebug(vec2 min, vec2 max, const ui_nine_inset& inset, uint8_t alpha = 255);

    /*
        THE THEME ATLAS: one texture holding this app's UI artwork, sampled as colour.

        `tex_id` is a GL texture the CALLER owns and keeps alive - normally one the Renderer
        already loaded, so this reuses the engine's existing decode, mipmapping and the unpack
        alignment fix rather than growing a second image path. w/h are its pixel size, which is
        what turns a sprite's pixel rect into UVs.

        ONE TEXTURE PER APP, which is the point: the theme is a property of the app, so a single
        sheet means the whole overlay - glyphs, boxes and artwork - still draws in ONE call. Passing
        0 unsets it and every AddSprite call becomes a no-op rather than sampling something stale.
    */
    void SetThemeTexture(uint32_t tex_id, int w, int h);
    bool HasTheme() const { return theme_tex != 0; }

    /*
        One themed sprite, stretched to fill min..max. `src_*` is its rectangle in the theme atlas,
        in PIXELS - the sheet's own coordinates, so a caller never converts to UVs by hand.

        `color` TINTS it: the texel is multiplied by it, so white is the artwork untouched and
        anything else recolours it. That is what hover and disabled states cost - a tint, not a
        second sprite.
    */
    void AddSprite(vec2 min, vec2 max, int src_x, int src_y, int src_w, int src_h,
                   uint32_t color = 0xFFFFFFFF);

    /*
        A themed sprite drawn as a NINE-SLICE: corners unscaled, edges stretched along one axis,
        centre stretched both ways. `inset` is in pixels of the source sprite.

        Sides STRETCH rather than tile. That is the simpler of the two and it is right for art whose
        edges are smooth; art with a repeating rhythm along its edges - visible plank joints, studs -
        wants tiling instead, because stretching lengthens the rhythm with the panel. Revisit here
        when the first piece of art actually needs it.

        An inset of zero on an axis means no cut on that axis, so left/right insets with top and
        bottom zero is a THREE-slice, which is what a button wants.
    */
    void AddNineSliceSprite(vec2 min, vec2 max, int src_x, int src_y, int src_w, int src_h,
                            const ui_nine_inset& inset, uint32_t color = 0xFFFFFFFF);

    /*
        One line of text. `pos` is the LEFT END OF THE BASELINE for UI_ALIGN_LEFT, and alignment
        moves the line relative to it - so a score growing from 9999 to 10000 stays centred
        without the caller knowing the advance.

        `size_px` is an em size, matching the font's own em_px. Newlines are not handled: this
        draws one line, and a caller wanting two calls it twice. That is the scope.
    */
    void AddText(const char* text, vec2 pos, float size_px, uint32_t color,
                 int align = UI_ALIGN_LEFT);

    //Width and height of `text` at `size_px`, without touching GL. Height is one line.
    vec2 MeasureText(const char* text, float size_px) const;

    //Uploads the batch and draws it in ONE call. RENDER THREAD, and it must run with the target
    //framebuffer already bound - see the note in the .cpp about where in the frame that is.
    void Draw();

    //Read-only, for a caller that wants to size something against the font's own metrics.
    const ui_font_header& GetFontHeader() const { return font; }

    int GetNumQuads() const { return (int)(vertices.size() / 6); }

private:
    /*
        THE QUAD RASTERISED AND THE SHAPE THE DISTANCE FIELD DESCRIBES, SEPARATELY.

        They are the same for an ordinary quad and AddQuad below passes them that way. They are
        NOT the same for an outline, whose quad has to grow to cover the half that falls outside
        the rect, nor for a cell of a nine-slice, which is one of nine quads making up one
        rectangle and must be clipped by that rectangle rather than by itself - otherwise every
        interior cut gets a coverage ramp it should not have, and leaks the scene behind through
        a one-pixel seam. The full reasoning, and the measurement, are in the .cpp.
    */
    void AddQuadShaped(vec2 min, vec2 max, vec2 shape_min, vec2 shape_max,
                       vec2 uv0, vec2 uv1,
                       float radius, float outline, float distance_scale, uint32_t color,
                       float sprite);
    //`sprite` defaults to 0 - a distance-field quad - so every existing caller is unchanged and
    //only the themed paths below have to say otherwise.
    void AddQuad(vec2 min, vec2 max, vec2 uv0, vec2 uv1,
                 float radius, float outline, float distance_scale, uint32_t color,
                 float sprite = 0.0f);
    //The two shape-aware forms the nine-slice paths draw their cells through. Public AddRect and
    //AddSprite are these with the shape set to the quad.
    void AddRectShaped(vec2 min, vec2 max, vec2 shape_min, vec2 shape_max,
                       float radius, uint32_t color);
    void AddSpriteShaped(vec2 min, vec2 max, vec2 shape_min, vec2 shape_max,
                         int src_x, int src_y, int src_w, int src_h, uint32_t color);
    //The shader, the buffers and the atlas - everything that dies with a context. Shared by Init
    //and ReUploadGPUObjects so the two cannot drift, which is the same argument the rest of this
    //stage makes for having one code path rather than a desktop one and an Android one.
    bool CreateGPUObjects();
    bool InitBuffers();
    bool InitFontTexture(const uint8_t* pixels);

    ui_font_header  font = {};
    Shader*         shader = NULL;
    /*
        THE TEXTURE UNIT THE ATLAS IS BOUND TO, and it is deliberately NOT 0.

        Texture units are global GL state, not per-program, so a unit this overlay binds every
        frame is a unit nothing else can keep anything in. It used to use 0 and that is exactly
        what happened on the Android port: materials there are bound to real units 0..7 by
        Renderer::UploadMaterials (the desktop renderer uses bindless handles instead and never
        noticed), so whichever material was handed unit 0 had its texture replaced by this font
        atlas on the first frame the overlay drew. The symptom was a grass tile sampling the SDF
        font and rendering red - red because a single-channel atlas read as .rgb is (r,0,0).

        THE NUMBER IS NO LONGER THIS CLASS'S TO PICK. It comes from the engine-wide map in
        core/TextureUnits.h, which is mirrored line for line by shaders/texture_units.glsl, so a
        unit cannot be claimed here and quietly reused by a shader over there. The overlay sits at
        4 and 5, inside the reserved run the map packs into 0..10 precisely so that every engine
        unit still exists on a device whose GL_MAX_TEXTURE_IMAGE_UNITS is 16 - which is the
        guaranteed floor and exactly what the test device reports.
    */
    static const int ATLAS_TEXTURE_UNIT = TEXUNIT_UI_ATLAS;

    GLuint          atlas_tex = 0;
    /*
        The theme atlas and its unit, the one above the font atlas - see the note there.
        Borrowed, never owned: SetThemeTexture takes a texture the caller loaded and this class
        must not delete it.
    */
    static const int THEME_TEXTURE_UNIT = TEXUNIT_UI_THEME;
    GLuint          theme_tex = 0;
    int             theme_w = 0;
    int             theme_h = 0;
    GLuint          vbo = 0;
    GLuint          vao = 0;

    //Borrowed, not owned - it points into the file layer's own buffer, which outlives everything
    //here. See ReUploadGPUObjects for why this is kept at all.
    const uint8_t*  atlas_pixels = NULL;
    //Remembered so a rebuild needs no arguments, and so it cannot be given different ones than
    //the build it is meant to reproduce.
    std::string     vert_name;
    std::string     frag_name;

    int             screen_w = 1;
    int             screen_h = 1;
    bool            f_ready = false;

    std::vector<ui_vertex> vertices;
};

#endif
