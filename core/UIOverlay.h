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
    One corner of one quad. 48 bytes.

    Compare ImGui's ImDrawVert at 20 (pos, uv, col) and core/type_vertex.h's `vertex` at 48
    (pos, normal, tangent, uv, matid). This sits at the same size as the scene vertex while
    carrying completely different things, and it is its own type for exactly that reason: reusing
    `vertex` would mean paying for a normal and a tangent that the overlay shader never reads.

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
    void AddQuad(vec2 min, vec2 max, vec2 uv0, vec2 uv1,
                 float radius, float outline, float distance_scale, uint32_t color);
    //The shader, the buffers and the atlas - everything that dies with a context. Shared by Init
    //and ReUploadGPUObjects so the two cannot drift, which is the same argument the rest of this
    //stage makes for having one code path rather than a desktop one and an Android one.
    bool CreateGPUObjects();
    bool InitBuffers();
    bool InitFontTexture(const uint8_t* pixels);

    ui_font_header  font = {};
    Shader*         shader = NULL;
    GLuint          atlas_tex = 0;
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
