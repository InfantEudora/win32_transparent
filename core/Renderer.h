#ifndef _RENDERER_H_
#define _RENDERER_H_
class Renderer;
#include "Shader.h"
#include "glad.h"
#include "Object.h"
#include "skeleton/Skeleton.h"
#include "Camera.h"
#include "CubeMap.h"
#include "Material.h"
#include "Light.h"
#include "InputController.h"
#include "PerfTimer.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>

//This should have the same layout as in the shader
#define NUM_MATERIAL_SLOTS  4
#define NUM_MORPH_FACTOR_SLOTS	4
//The wind field's SSBO binding - see Renderer::SetWindField. 0-6 are taken (instance data,
//materials, lights, readback, bones / volumes, morphs, vertex pull); default.vert declares 7.
#define SSBO_WIND               7

//The wind SSBO's header, mirrored by WindBuffer in default.vert (std430: three 16-byte rows,
//then the vec2 grid at offset 48).
struct wind_header_t {
    float rect[4];      //x0, y0, 1/cell_x, 1/cell_y
    int   size[4];      //width, height, enabled, unused
    float params[4];    //time in seconds, unused x3
};

//THE TEXTURE UNIT MAP - which pass owns which unit, and why they are packed the way they are.
//Its own header because UIOverlay.h needs the same numbers and must not pull in all of this one.
#include "TextureUnits.h"

typedef struct {
    fmat4 mat_transformscale;                   // Matrix holding object rotation, scale and translation
    int material_slot[NUM_MATERIAL_SLOTS];      // We could do that each instance has a material assigned to a fixed number of slots
    float morph_factors[NUM_MORPH_FACTOR_SLOTS];      // Amount each morph target should add to final mesh
    int objectindex;                            // The object's index in a batch it was rendered with this frame. (So not object->id)
    int num_bones;                              // Number of bones. Duplicate info for each instance... because it's mehs info
    int num_vertices;
    int num_morph_targets;
}instancedata_t;

typedef struct {
    fmat4 mat_transformscale;                   // Matrix holding object rotation, scale and translation for a single bone
    fmat4 mat_inversebind;
}bonedata_t;

/*
typedef struct{
    int data_in[4];         // Stored pixel coordinates of mouse
    int data_out[4];        // Holds objid
    float fdata_out[4];     // Holds ztest
}readback_buffer_t;*/

//A callback for debugging
void opengl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, char const* message, void const* user_param);

typedef enum RenderPipeline{
    PIPELINE_NONE      = 0,
    PIPELINE_MSAA      = 1,
    PIPELINE_DEFERRED  = 2
}RenderPipelineType;

/*
    A class responsible of managing the OpenGL state and pipeline.
    You should be able to make a renderer without a window,
     and that just renders a bunch of things to a buffer.
*/
class Renderer{
    public:
    Renderer(int w, int h);
    //Releases the GL objects this class owns outright (pass timer queries, picking PBOs). The
    //context outlives nothing, so this matters for correctness rather than for leaks at exit.
    ~Renderer();

    std::mutex physics_mutex;     //Makes sure all object states are updated, not just one.

    int width = 1;
    int height = 1;

    //Lets an app confine the actual 3D draw calls to a sub-rectangle of the window
    //(e.g. an 800x800 square docked to one side) while every FBO/blit/readback above
    //stays sized to the full window - see DrawFrame/DeferredPass for the two glViewport
    //calls that use these, and the comment there for why ImGui isn't affected. -1 (the
    //default) means "use the full window", so every app that never touches these fields
    //renders exactly as before.
    //viewport_y is measured from the BOTTOM of the window, because that is where GL's viewport
    //origin is - a band along the bottom of the screen is viewport_y = 0, and a band along the top
    //is viewport_y = height - viewport_height. It is deliberately not flipped to match the
    //window's top-left convention: everything else here is passed straight to glViewport, and a
    //silent flip in one of four numbers would be worse than the mismatch.
    int viewport_x = 0;
    int viewport_y = 0;
    int viewport_width = -1;
    int viewport_height = -1;
    int GetViewportWidth() const { return viewport_width > 0 ? viewport_width : width; }
    int GetViewportHeight() const { return viewport_height > 0 ? viewport_height : height; }

    /*
        WHOLE-FRAME RENDER SCALE: the scene drawn at 1/N of the window and scaled back up to it.

        Not to be confused with SetCustomShaderScale further down, which puts only the
        Shader::f_lowres custom shaders on a reduced target. The two compound - a volume at custom
        scale 2 under render scale 2 is drawn at a quarter of the window.

        width/height (and GetViewport*) KEEP MEANING WINDOW PIXELS, which is what every camera
        setup, ray pick and HUD in the apps reads them as. render_width/render_height are what the
        scene's buffers are actually sized to - the G-buffer, MSAA, SSAO, the resolve - and are the
        window divided by the scale, rounded up so the last column and row have a texel behind them.
        The upscale then draws the resolved scene into a window-sized target, and the overlay, the
        ImGui panels, both screenshots and the present all happen on that, at full resolution: the
        world is pixelated and the text is not.

        At scale 1 none of this exists - no target, no pass - and the frame is exactly what it was.

        Safe from any thread: both calls only record a request, and DrawFrame applies it when it
        starts, since the panel that sets it runs mid-frame and a change reallocates buffers the
        rest of that frame is drawing into.
    */
    enum upscale_filter_t{
        UPSCALE_NEAREST = 0,    //Solid NxN blocks - the default, and the point for a pixelated look
        UPSCALE_BILINEAR,
        UPSCALE_BICUBIC,        //Catmull-Rom: sharper than bilinear, a slight ring on hard edges
        UPSCALE_FILTER_COUNT
    };
    void SetRenderScale(int scale);         //1 (off) to 4
    int  GetRenderScale() const { return render_scale; }
    void SetUpscaleFilter(int filter);
    int  GetUpscaleFilter() const { return upscale_filter; }
    static const char* GetUpscaleFilterName(int filter);
    int render_width = 1;
    int render_height = 1;

    /*
        The world comes in as an argument, and the Renderer does not keep it.

        These used to walk a `std::vector<Object*> objects` member living here, which made the
        Renderer the owner of the world and meant every Scene sharing a Renderer shared one set of
        objects - so switching Scene changed the camera, the physics world and the tick clock, and
        changed nothing whatsoever about what was on screen. The list belongs to Scene now.

        The split is: Scene keeps what PERSISTS, the Renderer keeps only what it REBUILDS each
        frame from it - renderable_objects, visible_lights, glsl_lights, the batches. Taking the
        list as a parameter rather than a Scene* is deliberate: the Renderer has no business
        knowing what a Scene is, and this way it does not.
    */
    void CullObjects(const std::vector<Object*>& objects);
    void CullLights(const std::vector<Object*>& objects);
    void GetAllRenderableVisableSubObjects(Object* object,std::vector<Object*>&objects);
    void GetAllVisibleSubLights(Object* light,std::vector<Light*>&lights);
    void RebuildUniqueMeshList();
    void ClearBatches();
    void ClearObjectBatches();
    void FillBactches();
    void PrepareObjects(const std::vector<Object*>& objects);

    void DrawSkyBox(Camera* camera);

    //0 renders normal meshes, 1 renders only skinned meshes. For MESH_MODE_SHADER, pass the
    //custom shader index to draw only the meshes assigned to that shader; -1 draws all of them
    //regardless, which is only useful when the bound shader does not matter.
    //`f_occluder_pass` marks a pass that is building shadows rather than the picture - the depth
    //passes and the occluder field. Objects with f_casts_shadow false are left out of the instance
    //list for those, and only those, so clearing that flag removes an object's shadow without
    //removing the object. See Object::f_casts_shadow.
    void RenderUniqueMeshes(int normal_or_skinned, int custom_shader_index = -1, bool f_occluder_pass = false);

    /*
        Registers a shader for the custom-material pass and returns its INDEX, which is the tag
        that puts a mesh in that shader's sub-pass. Several can be live at once - one app's ground
        decal and another's raymarched volume are separate entries, not a single global slot.

        This block is the contract a custom shader signs up to; Renderer::CustomShaderPass
        explains how the pass itself is ordered.

        TO DRAW SOMETHING WITH IT, two lines on the mesh:

            mesh->mesh_mode = MESH_MODE_SHADER;
            mesh->custom_shader_index = renderer->AddCustomShader(my_shader);

        Both are needed. custom_shader_index defaults to -1, "not assigned", and an untagged
        MESH_MODE_SHADER mesh is skipped with a message rather than drawn by whichever shader
        happens to be registered first.

        WHAT THE ENGINE SETS ON YOUR SHADER, every frame, before your callback:

          mat_worldcam        the camera matrix
          eye_position        the ray origin, for anything raymarched
          render_target_size  the pixel size of what gl_FragCoord is measured against. Divide by
                              this to turn gl_FragCoord into a 0..1 screen uv for the G-buffer
                              below, NOT by textureSize(gbuffer_depth,0): the two agree until the
                              shader opts into Shader::f_lowres, and then they do not.

        And what it does NOT: the shadow matrices, the cloud-shadow and occluder-field uniforms.
        Those go to the shaders the renderer owns. Reusing shaders/default.vert is a convenience
        that hands you the usual varyings, not a requirement - but note that vshadow is then
        meaningless here, because mat_shadow is never set.

        A MISSING UNIFORM IS A WARNING, NOT A DEATH. GLSL strips a uniform that is declared but
        unused, so a custom shader that does not happen to use mat_worldcam is the ordinary case,
        not a mistake - it used to be fatal on the first frame. If your shader genuinely cannot
        proceed without a value, check what the setter returns.

        WHAT IS BOUND WHEN YOUR SHADER RUNS:

          texture units   TEXUNIT_GBUFFER_DEPTH / _POSITION / _NORMAL, the deferred G-buffer, so
                          you can see the solid scene behind you. READ THE TEXUNIT_GBUFFER_* BLOCK
                          AT THE TOP OF THIS FILE BEFORE USING IT - depth is the only channel that
                          can tell you whether anything was drawn at a pixel, and reaching for
                          position first is the trap everyone falls into.
          SSBO 0          instance data        SSBO 1  materials
          SSBO 2          lights               SSBO 4  bone instances

        YOUR OWN UNIFORMS go in Shader::uniform_callback, which is called after all of that and
        before the draw. It may also change cull face, depth mask and the depth test - a volume
        wants inside faces and no depth write; a ground decal wants the defaults - and all three
        are put back after each sub-pass.

        The renderer does not take ownership of the shader, and indices are handed out in
        registration order and never move. To hot-reload one, replace the entry at its index
        (see ApplicationShip::ReloadVolumeShader) rather than registering a second copy, which
        would leave every tagged mesh pointing at the stale one.
    */
    int AddCustomShader(Shader* shader);
    Shader* GetCustomShader(int index);
    //Draws every MESH_MODE_SHADER mesh, one sub-pass per registered custom shader. Runs last of
    //the geometry passes, with the deferred G-buffer bound as input - see the definition.
    void CustomShaderPass(Camera* camera);

    /*
        How many window pixels across one pixel of the reduced-resolution custom-shader target is.

        1 (the default) switches the whole thing off: no target is allocated, and every custom
        shader draws straight into the frame exactly as it always did. 2 is a quarter of the
        fragments, 3 a ninth, 4 a sixteenth. Clamped to 1..8.

        THIS IS BOTH A COST AND A LOOK, and deliberately one number rather than two: the upscale
        is nearest-neighbour, so N is equally "render at 1/N" and "show as NxN blocks". Two knobs
        would let somebody ask for big pixels at full resolution, which is paying for a look that
        the cheap version gives away.

        It applies to every shader that has opted in with Shader::f_lowres - which is where the
        per-shader half of this lives, and which is also where what a low-res shader owes in
        return is written down. A scale set while nothing has opted in is remembered and costs
        nothing.

        RENDER THREAD ONLY, and not from inside a uniform_callback: it may allocate. The natural
        place is Application::PreRender. Returns false if the target could not be built, having
        left the scale at 1 so the frame still draws.
    */
    bool SetCustomShaderScale(int scale);
    int GetCustomShaderScale(){return lowres_scale;}
    //Binds the cloud shadow map (if an app supplied one) and tells `s` whether to use it.
    void UploadCloudShadow(Shader* s);
    //Binds the occluder field (if an app enabled one) and tells `s` whether to use it.
    void UploadFieldShadow(Shader* s);

    void DeferredPass(Camera* camera);
    /*
        Screen-space ambient occlusion, in two halves because the two want different points in
        the frame. SSAOPass needs only the G-buffer, so it runs straight after DeferredPass:
        occlusion, then an edge-aware blur across and down. CompositeSSAO multiplies the result
        into the MSAA colour after the solid and skinned passes and before the custom-material
        pass, so translucent effects sit on top of the occlusion rather than under it. See
        shaders/ssao_compute.comp for the method and the ssao_* settings below for the knobs.
    */
    void SSAOPass(Camera* camera);
    void CompositeSSAO();
    //`objects` is the scene's object list - see the note on CullObjects for why it is passed in.
    void DrawFrame(const std::vector<Object*>& objects, Camera* camera, Shader* shader, InputController* input);

    bool CheckFrameBuffer();
    /*
        `vert_filename` and `frag_filename` name the programs the RENDERER itself owns - the
        G-buffer fill of PIPELINE_DEFERRED, and its skinned twin. They are arguments rather than
        the constants they used to be so that this one call reads identically in the Android
        port, where the same apps run against a different set of sources
        (shaders/default_android.vert and friends).

        skinned_vert_filename is the vertex stage for skinned meshes, linked against the SAME
        fragment shader - only the vertex stage differs between the two. It is defaulted because
        no app has ever wanted another one, and NULL skips that second program entirely for an
        app that owns no skinned mesh.

        In PIPELINE_MSAA the renderer owns no scene program at all - the app's own shader is
        handed to DrawFrame per frame - so the shader names go unused there. They are still
        given, so that changing an app's pipeline stays a one-word edit.
    */
    bool Init(const char* vert_filename, const char* frag_filename,
              int pipeline = PIPELINE_DEFERRED,
              const char* skinned_vert_filename = "shaders/default_skinned.vert");
    void SetOpenGLState();
    bool SetNumAASamples(int desired);
    bool Resize(int new_width, int new_height);
    bool RebuildMSAAFBO();
    bool RebuildDeferredFBO();

    bool RebuildShadowFBO(int shadow_width, int shadow_height);

    /*
        Turns on the occluder field. `camera` is an orthographic camera the APP owns and aims
        down the field axis over the area worth having shadows in - the renderer only reads it.
        `axis` is world up for that app. Safe to call again to re-aim or resize.
    */
    bool EnableFieldShadows(Camera* camera, const vec3& axis, int size = 1024);
    bool RebuildFieldFBO(int size);
    void RenderFieldPass();
    //Jump-floods the field's distance channel. Called by RenderFieldPass once the heights are in.
    void FieldDistancePass();

    void ClearDepthPasses();
    void RenderSingleDepthPass(Camera* camera, Shader* shader, int mesh_mode);
    void RenderDepthPasses(Shader* shader, int mesh_mode);
    void FinishDepthPasses();

    /*
        THE WIND FIELD - what bends materials with wind_flex > 0 (see material_t). A grid of 2D
        velocities in world X/Y, row-major from (x0,y0), `cell_x` by `cell_y` apart, width x height
        of them; x and y interleaved. `time` is in seconds and drives the flutter on top of the bend
        - pass simulation time (tick * timestep) so a paused sim holds the plants still.

        RENDER THREAD (it uploads). Call once per frame with the part of the field the camera can
        see; outside the grid there is no wind. ClearWindField switches it off, which is also the
        state every app starts in, so an app that never calls this pays nothing but one branch per
        vertex on a flag that is always zero.

        It travels in an SSBO (binding SSBO_WIND) rather than a texture ON PURPOSE: the texture
        unit map is full on the device (11 reserved + 5 materials = 16), and the header carries the
        grid's mapping, so no shader needs a uniform set for it - any shader that declares the
        buffer sees the current field. default.vert is the only one that does.
    */
    void SetWindField(const float* velocity_xy, int width, int height, float x0, float y0,
                      float cell_x, float cell_y, float time);
    void ClearWindField();

    bool InitSSBO();
    void ResolveAA();
    void BlitBufferTarget(GLuint framebuffer_id, GLenum attachment);
    void SelectViewBuffer(int view_id);

    void SetVSync(bool enable);
    bool GetVSync();

    //Thread-safe: call from any thread (e.g. an MCP tool handler on the TCP receive
    //thread). glReadPixels is only valid on the thread owning the GL context (the render
    //thread), so this just flags a request and blocks until CaptureScreenshotIfRequested,
    //called from DrawFrame at the end of the render thread's next frame, has captured and
    //PNG-encoded resolve_fbo_id's fully-resolved color output and signalled it's ready.
    //Returns an empty vector on timeout (render thread not running or stuck).
    /*
        f_include_ui chooses WHERE in the frame the capture happens, not what is drawn.

        ImGui renders into whatever framebuffer is bound, and Renderer::DrawFrame leaves
        resolve_fbo_id bound when it returns - so that one buffer holds the resolved scene at the
        end of DrawFrame, and the scene with the panels composited on top once
        Window::ImGuiRenderDrawData has run. Both window paths then present that same buffer. So
        there are two useful moments to read it, and this picks between them.

        true (the default) is what the window actually looks like, which is what a caller who
        cannot see the monitor almost always wants. Everything the debug UI shows - telemetry
        readouts, the object inspector, buttons, sliders - is ImGui and exists nowhere else, so a
        capture without it silently drops all of it. false gives the clean 3D scene, for checking
        geometry or colour without panels in the way.
    */
    std::vector<uint8_t> RequestScreenshot(bool f_include_ui = true, int timeout_ms = 2000);

    //Called at BOTH capture points - f_after_ui says which one this is. A pending request is
    //serviced only at the point it asked for, so the other call is a cheap no-op.
    void CaptureScreenshotIfRequested(bool f_after_ui);

    void UploadMaterials();
    void UploadLights();
    void UploadCubeMap(CubeMap* cubemap);
    void SetSkyboxCubemap(CubeMap* cubemap);

    Material* GetMaterial(int index);
    int FindMaterialIndex(const std::string &name);
    int AddMaterial(Material& newmat);
    void AddMaterials(std::vector<Material>& list);
    int GetNumMaterials();
    void UpdateObjectMaterials();

    //DeleteDestroyedObjects moved to Scene, which owns the list it erases from. See Scene.h.

    /*
        Re-uploads every mesh reachable from `objects`, children included, after the GL context has
        been replaced.

        NOTHING ON WINDOWS CALLS THIS, and it is here anyway. A Win32 context is never lost, so
        this is currently a path only Android needs - but it is the generic answer to a hole every
        app falls into on a platform that does lose one, and having it in core means an app does
        not have to know. The Android port hit it as a black screen with a working ImGui overlay:
        ImGui re-initialises itself and the scene does not, so everything drawn by the engine had
        VAOs belonging to the dead context and only the overlay came back.

        DEDUPLICATED BY Mesh*, which is correctness and not efficiency. ReUploadMeshData() zeroes
        vbo/vao and generates a new pair, so a second call for the same mesh in the same context
        leaks the pair the first one made - and Tetris has 200 board cells sharing one cube.
    */
    void ReUploadAllMeshes(const std::vector<Object*>& objects);

    Texture* LoadTexture(const char* filename,int target = GL_TEXTURE_2D, int depth = 1);

    //We'll have one multisampled framebuffer with a single color and depth buffer.
    //And a resolve buffer, where the mutisampling is resolved to.
    GLuint msaa_fbo_id = -1; //Main FBO consisting of:
    GLuint color_rbo_id = -1; // Main color
    GLuint depth_rbo_id = -1; // Main depth

    GLuint resolve_fbo_id = -1;  //Resolve frame buffer
    GLuint resolve_tex_id = -1;  //Resolves into a texture

    GLuint instdata_ssbo = -1;  //Shader Storage Buffer holding per-instance object data for each unique mesh
    GLuint materialdata_ssbo = -1;  //Shader Storage Buffer holding all different materials
    GLuint lights_ssbo = -1;  //Shader Storage Buffer holding all different lights
    //GLuint readback_ssbo = -1;  //Shader Storage Buffer for reading back data
    GLuint boneinstdata_ssbo = -1;  //Shader Storage Buffer for bone data
    GLuint wind_ssbo = -1;          //The wind field - see SetWindField. Always bound, header zero = off

    //Deferred stuff: Non-MSAA?
    GLuint deferred_fbo_id = -1; //Deferred FBO consisting of:
    GLuint deferred_depth_tex_id = -1; // Main depth buffer
    GLuint deferred_position_tex_id = -1; // Position of objects
    GLuint deferred_normal_tex_id = -1; // Normals of objects
    GLuint deferred_objectid_tex_id = -1; // Object IDs of objects for selection

    //Buffers for stages that require the output of the deferred pipeline
    //All three RGBA16F with the multiplier in rgb, so the two a view buffer shows can be blitted
    //as they are. Raw and final sit on the deferred FBO (ATTACHMENT4 and 2) for that blit only;
    //neither is ever a draw buffer.
    GLuint ssao_raw_tex_id = -1;  // SSAO before the blur - VIEW_SSAO_RAW
    GLuint ssao_blur_tex_id = -1; // Between the two blur directions
    GLuint ssao_tex_id = -1;      // SSAO, blurred: what CompositeSSAO multiplies in

    /*
        The reduced-resolution target the custom-material pass draws Shader::f_lowres shaders
        into, and the full-screen pass that scales it back up. See SetCustomShaderScale above for
        what it is for; this block is about how it is built.

        RGBA16F to match msaa_fbo's own colour format, because a volume's radiance runs well past
        1 at the core and the whole point of the effect is that it does. NO DEPTH ATTACHMENT: a
        volume disables the depth test anyway and resolves occlusion against the G-buffer itself,
        so a depth buffer here would cost memory and answer nothing.

        Allocated lazily on the first SetCustomShaderScale above 1 and rebuilt by Resize, so an
        app that never asks pays nothing - which is every app but bomber today.
    */
    GLuint lowres_fbo_id = -1;
    GLuint lowres_tex_id = -1;
    //Size of that texture, which is the window size divided by lowres_scale and ROUNDED UP - so
    //the last block is allowed to hang off the edge rather than leaving a strip undrawn.
    int lowres_tex_width = 0;
    int lowres_tex_height = 0;
    int lowres_scale = 1;
    //The upscale. Attribute-less: it builds its own full-screen triangle from gl_VertexID, so it
    //needs a bound VAO and no vertex buffer at all - hence the one below, which exists only to
    //satisfy core profile's "you must have a VAO" and holds nothing.
    Shader* lowres_composite_shader = NULL;
    GLuint lowres_vao = -1;

    bool RebuildLowResFBO(void);
    //Draws the low-res target over the frame, one nearest-neighbour block per low-res pixel.
    void CompositeLowRes(void);

    //The render scale's half of that - see SetRenderScale. A window-sized colour target the
    //resolved scene is scaled up into, which the rest of the frame (overlay, ImGui, screenshots,
    //the present) then draws on. It shares lowres_vao, which is just as empty for this pass.
    GLuint upscale_fbo_id = -1;
    GLuint upscale_tex_id = -1;
    int upscale_tex_width = 0;
    int upscale_tex_height = 0;
    Shader* upscale_shader = NULL;
    int render_scale = 1;
    int upscale_filter = UPSCALE_NEAREST;
    //Requests, from whichever thread - applied by ApplyRenderScale at the top of DrawFrame.
    std::atomic<int> requested_render_scale{1};
    std::atomic<int> requested_upscale_filter{UPSCALE_NEAREST};
    //render_width/height from width/height and render_scale.
    void UpdateRenderSize();
    //Takes the requests; reallocates every scene buffer when the scale changed. Render thread.
    void ApplyRenderScale();
    bool RebuildUpscaleFBO();
    //Draws resolve_tex_id into upscale_fbo_id with the chosen filter, and leaves that bound.
    void UpscaleFrame();
    //The app's viewport (GetViewport*, window pixels) in render-target pixels - what every
    //scene pass hands glViewport.
    void GetSceneViewport(int& x, int& y, int& w, int& h) const;
    void SetSceneViewport() const;
    //The body of CustomShaderPass, run once for the full-res shaders and once for the low-res
    //ones. `f_lowres` selects which half it draws and `f_lowres_pass` says whether there is a
    //low-res half at all this frame - which is what keeps an opted-in shader drawn, at full
    //resolution, when the scale is 1. Returns how many sub-passes it ran.
    int CustomShaderSubPasses(Camera* camera, bool f_lowres, bool f_lowres_pass);

    //Shadow
    GLuint shadow_fbo_id = -1;  // Framebuffer for getting depth of a light sournce
    GLuint shadow_tex_id = -1;  // Texture where shadow depth info is stored

    /*
        Cloud shadows. The renderer does not build this and knows nothing about volumes - an app
        fills a 3D transmittance texture and drops its id here, and the renderer binds it and
        hands default.frag the matrix that projects a world position into it. -1 means no map,
        which is the case for every app but the ship one, and then f_cloud_shadows goes to the
        shaders as 0 and CalcCloudShadow returns 1.

        mat_cloud_shadow is deliberately its own matrix rather than the depth map's mat_shadow:
        the two frustums are fitted to different things. See shaders/cloud_shadow.comp.
    */
    GLuint cloud_shadow_tex_id = -1;
    fmat4 mat_cloud_shadow;

    /*
        The occluder field: a single top-down texture describing where the world's occluders are,
        which default.frag then MARCHES to shadow point lights. It exists because the obvious way
        to shadow an omnidirectional light - six faces of a cube map per light, per frame - buys
        generality that a fixed overhead view cannot use. Drop to a top-down view and an occluder
        is well described by a prism: a footprint extruded between two heights. Both of those fit
        in one texture, and that texture is light-INDEPENDENT, so N lights cost N marches against
        it rather than N shadow maps.

        Enabled per app via EnableFieldShadows and off everywhere else, because it is a whole
        extra geometry pass that only pays for itself when something actually samples it.

        The camera is not owned here. It is the app's, because only the app knows which part of
        its world is worth covering, and pointing it at a live scene camera would be a mistake -
        anything that moves the camera (Tetris shakes its own on a line clear) would drag the
        field's world mapping along under the shadows.
    */
    GLuint field_fbo_id = -1;
    GLuint field_tex_id = -1;
    int    field_texture_size = 1024;
    bool   f_field_shadows = false;
    Camera* field_camera = NULL;
    vec3   field_axis = vec3(0,0,1);
    fmat4  mat_field;
    Shader* field_shader = NULL;
    //The jump flood that fills the distance channel, and the ping-pong pair it runs on. The pair
    //holds seed COORDINATES, not distances - see shaders/field_jfa.comp.
    Shader* field_jfa_shader = NULL;
    GLuint field_seed_tex_id[2] = {(GLuint)-1,(GLuint)-1};
    //Maximum samples along the ray from surface to light. An upper bound rather than a count now
    //that the march sphere-traces: over open space it steps by the distance field and reaches the
    //light in a handful, and only a ray running along a surface uses its whole budget. It is also
    //the floor on step size (dist/steps), which is what stops such a ray stalling short of the
    //light. 0 disables the lookup without rebuilding anything.
    int    field_shadow_steps = 64;
    //How far along the surface normal the march starts. A receiver is by definition ON an
    //occluder's surface, so at t=0 it is inside its own slab and shadows itself - this is the
    //field's version of shadow_bias.
    float  field_normal_bias = 0.15f;
    //The scene's DEFAULT light radius in world units, for the penumbra estimate: 0 gives hard
    //shadows and bigger is softer. Every light starts out deferring to this, so moving it still
    //softens the whole scene at once - but it is no longer the only answer available. A light
    //that sets its own Light::radius is not affected by it.
    //
    //Not a uniform. It is resolved per light in UploadLights and travels in the SSBO, because the
    //shader is marching for one particular lamp and has no business knowing there is a
    //scene-wide anything.
    float  field_light_radius = 0.30f;

    Shader* deferred_shader = NULL;         // Shader that outputs data to textures
    Shader* deferred_shader_skinned = NULL; // Shader that outputs data to textures
    //Shaders for the custom-material pass, indexed by Mesh::custom_shader_index. Use
    //AddCustomShader rather than pushing here, so the index the caller stores is the real one.
    std::vector<Shader*> custom_shaders;
    Shader* ssao_compute_shader = NULL;
    Shader* ssao_blur_shader = NULL;
    Shader* ssao_composite_shader = NULL;   // lowres_composite.vert + ssao_composite.frag
    Shader* line_shader = NULL;             // Seperate shader for rendering line meshes.

    Shader* skybox_shader = NULL;
    CubeMap* skybox = NULL;
    Mesh* skybox_mesh = NULL;

    Shader* skinned_shader = NULL;


    //Settings
    int aa_samples = 1;
    float alpha_clip = 0.5f;          // At what value pixels with alpha will get discarded in fragment shader
    //Width of a cone light's soft edge, in cosine space. Pushed to the surface shaders here and
    //to the volume shader by whichever app owns it, so a cone's edge matches between the hull
    //it lights and the fog around it.
    float cone_softness = 0.15f;
    //How far the sun's shadow filter reaches either side, in shadow-map TEXELS, not world units -
    //see CalcShadow in default.frag. 0 is one bilinear tap (already no stair steps), 1 a 3x3 grid,
    //and the cost goes with the square: (2*ceil(r)+1)^2 gathers per lit fragment, capped at r = 3.
    float shadow_pcf_radius = 1.0f;
    int pipeline = PIPELINE_MSAA;     // Which pipeline to initialise
    bool f_normal_mapping = true;     // Enable/disable normal mapping
    bool f_render_skybox = true;      // Enable/disable skybox rendering
    bool f_use_reflections = false;   // Enable/disable skybox reflections
    bool f_backface_culling = true;   //
    bool f_ssao = false;              // Darken the frame by the ambient occlusion
    bool f_msaa = true;               //
    int view_buffer = 0;              // Output different intermediate buffers to view - view_buffer_t
    /*
        What view_buffer shows instead of the frame. The two SSAO views make the pass run even
        with f_ssao off - looking at it is how its settings get tuned - but only f_ssao puts it on
        the frame, so a view is always the occlusion ALONE, never occlusion times colour.
    */
    enum view_buffer_t{
        VIEW_FINAL = 0,
        VIEW_POSITION,          //G-buffer world positions
        VIEW_NORMAL,            //G-buffer normals
        VIEW_SSAO,              //The multiplier CompositeSSAO applies, blurred
        VIEW_SSAO_RAW,          //...and the same before the blur, 4x4 pattern and all
        VIEW_BUFFER_COUNT
    };
    static const char* GetViewBufferName(int view);

    /*
        SSAO settings, pushed to the shaders every frame so the Renderer panel can drag them live.
        A struct so that `ssao = SSAOSettings()` is the panel's reset, with the defaults written
        once, here. Lengths are WORLD units, not pixels: the G-buffer is world space
        (ssao_compute.comp says why), so a crease is judged at the same size near and far.
    */
    struct SSAOSettings{
        float radius = 1.5f;        // How far round a point to look for occluders
        float bias = 0.025f;        // How far behind geometry a sample must be to count - stops a flat surface occluding itself
        int   kernel_size = 32;     // Samples per pixel; the cost goes with it
        float strength = 1.0f;      // 0 leaves the frame untouched, 1 applies the occlusion as measured
        float power = 1.0f;         // Contrast: above 1 deepens creases, below 1 lifts them
        //The blur. Radius in PIXELS either side, Gaussian with sigma = radius/2: 4 is the smallest
        //that erases the kernel's 4x4 rotation tile (ssao_compute.comp), 0 turns the blur off.
        int   blur_radius = 4;
        //How far off a pixel's surface plane a neighbour may lie before the blur stops at it, as a
        //fraction of the pixel's distance from the eye (see ssao_blur.comp for why it scales).
        float blur_edge_tolerance = 0.05f;
    };
    SSAOSettings ssao;
    /*
        The same three things - settings, on/off, view buffer - asked for from another thread (the
        renderer_ssao MCP tool). The render thread reads `ssao` all through the frame and the panel
        writes it from the render thread too, so another thread only ever leaves a request, taken
        at the top of the next DrawFrame the way a render scale is. -1 leaves that one alone.
    */
    void RequestSSAO(const SSAOSettings& settings, int enabled, int view);
    //What a new request should start from: a still-pending one if there is one, so two requests
    //in the same frame add up rather than the second undoing the first.
    SSAOSettings GetSSAORequestBase();
    //The pending request itself, taken by ApplySSAORequest at the top of DrawFrame.
    std::mutex ssao_request_mutex;
    bool f_ssao_request_pending = false;
    SSAOSettings ssao_request;
    int ssao_request_enabled = -1;
    int ssao_request_view = -1;
    void ApplySSAORequest();
    int shadow_texture_size = 4096;   // Size for a single shadow texture


    //Counters/Timers
    PerfTimer* tmr_frame = NULL;

    /*
        PER-PASS GPU TIMING.

        What this is for: tmr_frame above, and every other PerfTimer in this engine, measures the
        CPU. GL is asynchronous, so a CPU timer around a draw call measures how long it took to
        SUBMIT the work, not how long the GPU took to do it - the two are unrelated, and this
        repo has concluded that twice the hard way (see the occluder-field measurement in
        docs/engine_backlog.md, which had to caveat its own number, and the note that frame time
        here is quantised to the refresh interval because the driver forces vsync on).
        GL_TIME_ELAPSED asks the GPU itself, so it is immune to both.

        THE ONE RULE: SCOPES MUST NOT NEST OR OVERLAP. Only one GL_TIME_ELAPSED query can be
        active per context, so there is deliberately no "whole frame" timer wrapping these - it
        would be illegal, not merely inaccurate. Two passes that want splitting (the occluder
        field and its jump flood) are therefore two ADJACENT scopes inside RenderFieldPass, not
        an outer and an inner one. BeginGPUPass refuses a nested Begin and says so rather than
        letting it turn into a GL_INVALID_OPERATION from somewhere unrelated later.

        The consequence of that rule is that anything not inside a scope is invisible, so the
        column does not add up to the frame. That is honest and worth keeping in view: a gap
        between the sum and the frame time is real work nobody has named yet.

        A result lags a frame or two behind the pass it measures - see GPUPassTimer - and lands
        in an ordinary PerfTimer, so a GPU pass reads out of the Engine panel with the same
        60-sample rolling average as everything else.
    */
    enum gpu_pass_t{
        GPU_PASS_SHADOW = 0,    //Shadow map: the depth passes, skinned and static
        GPU_PASS_FIELD,         //Occluder field heights (apps that called EnableFieldShadows)
        GPU_PASS_FIELD_JFA,     //...and the jump flood that turns them into a distance field
        GPU_PASS_DEFERRED,      //G-buffer - a second full geometry pass, see DrawFrame
        GPU_PASS_SSAO,          //Occlusion from the G-buffer (f_ssao, or an SSAO view buffer)
        GPU_PASS_SSAO_BLUR,     //...its edge-aware blur, both directions
        GPU_PASS_SKYBOX,
        GPU_PASS_COLOR,         //The lit pass: static meshes, then lines
        GPU_PASS_SKINNED,
        GPU_PASS_SSAO_COMPOSITE,//...multiplied into the frame, ahead of the translucent pass
        GPU_PASS_CUSTOM,        //CustomShaderPass - volumes and anything else translucent
        GPU_PASS_RESOLVE,       //MSAA resolve
        GPU_PASS_BLIT,          //Only when view_buffer selects an intermediate to look at
        GPU_PASS_UPSCALE,       //Only at a render scale above 1 - see SetRenderScale
        GPU_PASS_OVERLAY,       //UIOverlay - the app's own 2D HUD (Application::DrawFrame)
        GPU_PASS_IMGUI,         //The debug panels (Application::DrawFrame)
        GPU_PASS_COUNT
    };

    /*
        Two query objects per pass, used alternately.

        Reading a query's result stalls until the GPU has actually finished it, so reading the
        one just written would sync the CPU to the GPU every frame and destroy the thing being
        measured. Writing into the slot from TWO frames ago instead means its result has had a
        whole frame to become available, and the read never waits. Checking availability in
        BeginGPUPass - right before clobbering the slot - rather than in EndGPUPass also means
        the rare not-ready-yet case needs no "try again next frame" bookkeeping: the pass simply
        contributes no sample that frame.
    */
    struct GPUPassTimer{
        GLuint queries[2] = {0,0};
        int write_index = 0;
        bool f_has_run[2] = {false,false};
        bool f_begun_this_frame = false;
        PerfTimer* timer = NULL;        //Microseconds, to match every other timer in the panel
        /*
            The same scope on the CPU's wall clock: how long issuing the pass took, INCLUDING any
            wait the driver made the thread do inside it (a buffer still in use, a sync). Beside
            the GPU number it answers which side a slow pass is slow on; summed and taken from
            tmr_frame it is the time spent outside every scope.
        */
        PerfTimer* cpu_timer = NULL;
        std::chrono::steady_clock::time_point cpu_start;
    };

    bool InitGPUPassTimers();
    void DestroyGPUPassTimers();
    void BeginGPUPass(int pass);
    void EndGPUPass(int pass);
    //Call once per frame, after the LAST pass of the frame has ended (which is ImGui's, in
    //Application::DrawFrame - not the end of Renderer::DrawFrame). Files a zero for every pass
    //that did not run, so a pass being switched off decays out of the rolling average instead of
    //sitting there showing what it used to cost.
    void EndGPUFrame();
    const GPUPassTimer* GetGPUPassTimer(int pass);
    static const char* GetGPUPassName(int pass);
    bool GPUTimersSupported(){return f_gpu_timers_supported;}

    //The mouse-over readback in DrawFrame, on the CPU clock deliberately: what it costs is a
    //stall measured in wall clock, and a GPU timer around it would report the near-zero time the
    //GPU spent rather than the time the frame lost waiting. Kept after the readback went
    //asynchronous (see ReadPickingAsync) precisely so the difference stays visible in the panel.
    PerfTimer* tmr_pick_readback = NULL;

    /*
        ASYNCHRONOUS MOUSE-OVER PICKING.

        The problem this solves: glReadPixels into client memory cannot return until the GPU has
        finished everything queued ahead of it, so it stalls the render thread for most of a
        frame. Measured on apps/ship before this existed: 5.56 ms of a 6.23 ms "Renderer Time",
        i.e. ~89% of the CPU-side cost of the whole renderer was this one call waiting.

        The fix is to read into a PIXEL PACK BUFFER instead. With a buffer bound to
        GL_PIXEL_PACK_BUFFER, glReadPixels' last argument stops being a client pointer and becomes
        a byte offset into that buffer, and the call becomes a GPU-side copy that returns
        immediately. The result is collected a frame later, by which time it is simply sitting
        there. Two buffers alternate so the one being mapped is never the one just written.

        THE COST IS THAT THE ANSWER IS ONE FRAME OLD, and that is not free here. The object id
        buffer holds instancedata_t::objectindex - an index into renderable_objects AS IT WAS
        WHEN THE PIXEL WAS DRAWN, explicitly not a stable object id. renderable_objects is rebuilt
        by culling every single frame, so resolving a one-frame-old index against the current
        vector names a different object whenever the visible set shifts - which, with a moving
        camera, is constantly. picking_id_snapshot therefore keeps the id list that each in-flight
        read belongs to, and the index is resolved against that. It is a few hundred ids copied
        per frame against 5.5 ms saved.

        (The tidier fix is for the shader to write the object's real id rather than its index,
        which would make a stale value self-validating. That means changing what objectindex means
        for every app and shader that fills it, so it is deliberately not bundled in here.)
    */
    //Three deep because the driver is allowed to be that far ahead - see the members below.
    static const int PICK_PBO_SLOTS = 3;
    bool InitPickingPBOs();
    void DestroyPickingPBOs();
    //Collects the previous frame's result into `input`, then issues this frame's read. Does both
    //or neither: with nothing in flight yet it only issues, which is why the first frame of
    //hovering reports nothing.
    void ReadPickingAsync(InputController* input, int mouse_x, int mouse_y);
    //How far UploadMaterials got handing out units this upload. Absolute, not an array index -
    //see TEXUNIT_MATERIAL_FIRST. Reset at the top of every UploadMaterials, so between calls it
    //reads as "one past the last material unit in use".
    int last_texture_unit = TEXUNIT_MATERIAL_FIRST;
    //One past the top of the material range, i.e. the first unit that does not exist.
    int num_texture_units = TEXUNIT_MATERIAL_FIRST + NUM_MATERIAL_UNITS;
    //What the last UploadMaterials reported, so the count is logged on a CHANGE rather than every
    //frame - see the block at the end of UploadMaterials. -1 forces the first frame to say it.
    int last_reported_material_units = -1;

    //These will differ per frame
    std::vector<Mesh*> unique_meshes;                           // An array of unique meshes
    std::vector<std::vector<objectid_t>*>unique_mesh_batches;   // An array of arrays containing the object id's per unique mesh, these form batches

    std::vector<Object*>renderable_objects;                     // All objects we will render this frame
    std::vector<Light*>visible_lights;                          // All lights we will use this frame

    std::vector<instancedata_t>instancedata;                    // Object data per unique mesh instance
    std::vector<bonedata_t>boneinstancedata;                    // Bone data per unique mesh instance. Holds number of bones * number of instances
    std::vector<material_t>glsl_materials;                      // List of all materials for direct upload to SSBO
    std::vector<light_t>glsl_lights;                            // List of all active lights for direct upload to SSBO
    std::vector<Material>materials;                             // List of all materials
    std::vector<Texture*>textures;                              // List of all textures

    std::vector<line>debug_lines;

    //readback_buffer_t readbackbuffer;                   //A single buffer for reading back data from shader

    //The `objects` list that used to live here is Scene::objects now - see CullObjects above.

    private:
    //Settings
    bool f_vsync = false;

    //Screenshot request/result handoff between whichever thread calls RequestScreenshot
    //and the render thread that services it in CaptureScreenshotIfRequested.
    std::mutex screenshot_mutex;
    std::condition_variable screenshot_cv;
    bool screenshot_requested = false;
    bool screenshot_include_ui = true;
    bool screenshot_ready = false;
    std::vector<uint8_t> screenshot_png;

    //Rotating picking readback - see ReadPickingAsync. THREE slots, not the two the GPU pass
    //timers use: a timer query is polled and skipped when unready, but a stale hover is visible,
    //so this wants the buffer it reuses to be genuinely finished rather than merely probably
    //finished. With vsync on, the driver runs a frame or two ahead, and two slots measurably
    //were not enough - the map blocked and the readback cost MORE than the sync version it
    //replaced. Each slot carries the fence that says when its data actually landed.
    GLuint picking_pbo[PICK_PBO_SLOTS] = {0,0,0};
    GLsync picking_fence[PICK_PBO_SLOTS] = {0,0,0};
    int picking_pbo_write_index = 0;
    bool f_picking_pbo_has_data[PICK_PBO_SLOTS] = {false,false,false};
    //renderable_objects as it stood when each in-flight read was issued, by id. Without this the
    //index that comes back resolves against a differently-culled vector - see ReadPickingAsync.
    std::vector<objectid_t> picking_id_snapshot[PICK_PBO_SLOTS];

    GPUPassTimer gpu_pass_timers[GPU_PASS_COUNT];
    bool f_gpu_timers_supported = false;
    //Which pass owns the one active GL_TIME_ELAPSED query, or -1. Exists to catch a nested
    //Begin at the call site that made the mistake - see the block comment on gpu_pass_t.
    int active_gpu_pass = -1;
};


#endif