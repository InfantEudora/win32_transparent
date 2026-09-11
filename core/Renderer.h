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
#include <mutex>
#include <condition_variable>
#include <cstdint>

//This should have the same layout as in the shader
#define NUM_MATERIAL_SLOTS  4
#define NUM_MORPH_FACTOR_SLOTS	4

//Texture units CustomShaderPass binds the deferred G-buffer to, so a custom material can see
//what the scene looks like behind it (a raymarched volume needs it to stop the march at solid
//geometry). Units 1-3: unit 0 is the shadow map, material textures are handed out from 4 up
//(see UploadMaterials) and 24 is the skybox cubemap, so these three are the gap in between.
//Kept as defines because the same numbers appear in the custom shaders' layout(binding = N).
#define TEXUNIT_GBUFFER_DEPTH     1
#define TEXUNIT_GBUFFER_POSITION  2
#define TEXUNIT_GBUFFER_NORMAL    3

//Above the skybox cubemap at 24, so it is clear of the material textures growing up from 4.
//An app binds its own data here (currently the ship app's 3D cloud noise); UploadMaterials warns
//if the material textures ever reach this far, since the collision would otherwise show up as a
//volume sampling somebody's diffuse map.
#define TEXUNIT_APP_RESERVED      25

//Cloud shadow map: the volumetric transmittance map an app may hand the renderer, sampled by
//default.frag's CalcCloudShadow. Above TEXUNIT_APP_RESERVED for the same reason that one is
//above the cubemap - it is the next free unit going up, well clear of the material textures.
#define TEXUNIT_CLOUD_SHADOW      26

//Occluder field: the top-down min/max height map an app may ask the renderer to build, sampled
//by default.frag's CalcFieldShadow to shadow point lights without a cube map. Next unit up.
#define TEXUNIT_FIELD_SHADOW      27

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

    void CullObjects();
    void CullLights();
    void GetAllRenderableVisableSubObjects(Object* object,std::vector<Object*>&objects);
    void GetAllVisibleSubLights(Object* light,std::vector<Light*>&lights);
    void RebuildUniqueMeshList();
    void ClearBatches();
    void ClearObjectBatches();
    void FillBactches();
    void PrepareObjects();

    void DrawSkyBox(Camera* camera);

    //0 renders normal meshes, 1 renders only skinned meshes. For MESH_MODE_SHADER, pass the
    //custom shader index to draw only the meshes assigned to that shader; -1 draws all of them
    //regardless, which is only useful when the bound shader does not matter.
    void RenderUniqueMeshes(int normal_or_skinned, int custom_shader_index = -1);

    //Registers a shader for the custom-material pass and returns its index, which is what goes
    //in Mesh::custom_shader_index. Several can be live at once - one app's ground decal and
    //another's raymarched volume are separate entries, not a single global slot.
    int AddCustomShader(Shader* shader);
    Shader* GetCustomShader(int index);
    //Draws every MESH_MODE_SHADER mesh, one sub-pass per registered custom shader. Runs last of
    //the geometry passes, with the deferred G-buffer bound as input - see the definition.
    void CustomShaderPass(Camera* camera);
    //Binds the cloud shadow map (if an app supplied one) and tells `s` whether to use it.
    void UploadCloudShadow(Shader* s);
    //Binds the occluder field (if an app enabled one) and tells `s` whether to use it.
    void UploadFieldShadow(Shader* s);

    void DeferredPass(Camera* camera);
    void SSAOPass(Camera* camera);
    void DrawFrame(Camera* camera, Shader* shader, InputController* input);

    bool CheckFrameBuffer();
    bool Init(int pipeline = PIPELINE_MSAA);
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
    void DeleteDestroyedObjects();

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

    //Deferred stuff: Non-MSAA?
    GLuint deferred_fbo_id = -1; //Deferred FBO consisting of:
    GLuint deferred_depth_tex_id = -1; // Main depth buffer
    GLuint deferred_position_tex_id = -1; // Position of objects
    GLuint deferred_normal_tex_id = -1; // Normals of objects
    GLuint deferred_objectid_tex_id = -1; // Object IDs of objects for selection

    //Buffers for stages that require the output of the deferred pipeline
    GLuint ssao_tex_id = -1; // SSAO output texture

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
    //Radius of the light source in world units, for the penumbra estimate. Not a property of any
    //particular light yet: light_t has no size field, and giving it one is the natural next step
    //if two lights in a scene ever want different softness. 0 gives hard shadows.
    float  field_light_radius = 0.30f;

    Shader* deferred_shader = NULL;         // Shader that outputs data to textures
    Shader* deferred_shader_skinned = NULL; // Shader that outputs data to textures
    //Shaders for the custom-material pass, indexed by Mesh::custom_shader_index. Use
    //AddCustomShader rather than pushing here, so the index the caller stores is the real one.
    std::vector<Shader*> custom_shaders;
    Shader* ssao_compute_shader = NULL;
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
    int pipeline = PIPELINE_MSAA;     // Which pipeline to initialise
    bool f_normal_mapping = true;     // Enable/disable normal mapping
    bool f_render_skybox = true;      // Enable/disable skybox rendering
    bool f_use_reflections = false;   // Enable/disable skybox reflections
    bool f_backface_culling = true;   //
    bool f_ssao = false;              //
    bool f_msaa = true;               //
    int view_buffer = 0;              // Output different intermediate buffers to view
    int shadow_texture_size = 4096;   // Size for a single shadow texture


    //Counters/Timers
    PerfTimer* tmr_frame = NULL;
    int last_texture_unit = 0;
    int num_texture_units = 24;
    int cubemap_texture_unit = 24; //We reserve the last texture unit for the skybox cubemap, so we can easily bind it in the shader without needing to change other texture bindings.

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

    std::vector<Object*>objects;                        //All known objects

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
};


#endif