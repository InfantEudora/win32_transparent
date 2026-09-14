#ifndef _APPLICATION_TESTFX_H_
#define _APPLICATION_TESTFX_H_

#include "Application.h"
#include "Texture.h"
#ifdef USE_IMGUI
#include "imgui.h"
#endif

#include <map>
#include <mutex>
#include <string>
#include <vector>

/*
    testfx - a fullscreen shader bench. See docs/testfx_plan.md for the design; this is the
    summary a reader of the code needs.

    One window, one screen-filling quad, one fragment shader, an "Effect" panel to pick which
    shader and to drive its uniforms, and fx_* MCP tools so the whole loop is reachable without a
    keyboard. Edit a .frag, press Reload, look at it. It is NOT a shadertoy clone: there is no
    in-app editor, snippets are pasted into a file in a normal editor, and the compatibility claim
    is exactly that the i* uniform names are honoured so a snippet compiles unchanged.

    --- IT RENDERS THROUGH THE CUSTOM-MATERIAL PASS, NOT AROUND IT ------------------------------
    That is the single most important decision here and it is what makes an effect transplantable:
    whatever the bench draws, a game can draw the same way, because it IS the same way. The quad
    is one Object holding a MakeQuad(2,2) mesh tagged MESH_MODE_SHADER, drawn by
    Renderer::CustomShaderPass like the ship's clouds and breakout's shield. Three things make
    that work without touching the renderer:

      - shaders/fullscreen.vert IGNORES THE CAMERA. MakeQuad(2,2) spans -1..+1 in XY, which is
        already clip space, so the vertex stage is gl_Position = vec4(position.xy,0,1). The quad
        covers the viewport at any window size and from any camera position, which is the point.
      - NOTHING CULLS IT. Renderer::CullObjects renders every visible object - this engine has no
        frustum test - so a quad whose vertices are nowhere near the camera cannot vanish.
      - THE UNIFORM CALLBACK OWNS THE DEPTH STATE. SetEffectUniforms turns off the depth write and
        the depth test, exactly as ApplicationShip::SetVolumeUniforms does; CustomShaderPass puts
        both back after the pass.

    --- TIME COMES FROM THE SIMULATION TICK -----------------------------------------------------
    iTime is GetPhysicsTick() * GetPhysicsTimestep(), not a wall clock. Durations in this codebase
    are ticks and this is no exception, but the payoff is that sim_pause freezes the effect and
    sim_step advances it by an exact number of frames - so a screenshot of frame N is
    reproducible, and "is this a timing bug or a shader bug" becomes answerable. The app runs at
    120 TPS: it simulates nothing, ticks are nearly free, and 50 Hz would visibly step a slow
    rotation.

    --- WHERE THE THREADS ARE -------------------------------------------------------------------
    Init, PreRender, DrawImGuiUI and SetEffectUniforms all run on the RENDER thread (DrawImGuiUI
    additionally holds physics_mutex). UpdateView runs on the PHYSICS thread. The fx_* MCP tool
    handlers run on their own threads and hold nothing, so everything they touch is either behind
    fx_mutex or is a request flag PreRender services - compiling a shader is GL work and only the
    render thread may do it.
*/

/*
    Texture units the four iChannels are bound to.

    Not chosen freely: unit 0 is the shadow map, 1-3 are the G-buffer CustomShaderPass binds for
    us (TEXUNIT_GBUFFER_*), material textures are handed out from 4 up, 24 is the skybox cubemap,
    25 is TEXUNIT_APP_RESERVED, and 26/27 are the cloud-shadow and occluder-field maps. 28 is
    simply the next free unit going up, and four channels fit under the 32 that desktop GL
    guarantees a fragment stage. Kept as a define because the same number appears in
    shaders/shadertoy.glsl's layout(binding = N).
*/
#define TEXUNIT_FX_CHANNEL0     28
#define FX_NUM_CHANNELS         4

//The vertex stage every effect here shares. See the block at the top of this file.
#define FX_VERTEX_SHADER        "shaders/fullscreen.vert"

/*
    One knob, as the reflected-uniform panel sees it.

    A "knob" is any active uniform of the effect that the bench did not put there itself - so a
    new slider is a new `uniform float` line in the GLSL and nothing else: no C++, no rebuild.
    The value lives HERE rather than in the program because the panel and the fx_set tool run on
    threads that are not allowed to touch GL; SetEffectUniforms pushes the whole map on the render
    thread, where writing to a program is safe.
*/
struct FxUniform{
    unsigned int type = 0;      //the GLSL type glGetActiveUniform reported, e.g. GL_FLOAT_VEC3
    int   num_components = 1;   //1..4
    bool  f_integer = false;    //int/bool travel as ints, everything else as floats
    float values[4] = {0,0,0,0};
    int   ivalues[4] = {0,0,0,0};
};

class ApplicationTestFX : public Application{
public:
    ApplicationTestFX();

    void Init(void) override;
    void PreRender(void) override;
    void UpdateView(void) override;
#ifdef USE_IMGUI
    void DrawImGuiUI(void) override;
#endif

    //Lets the core camera_get/camera_set tools see and move the point the camera orbits.
    vec3* GetCameraTargetPtr() override { return &camera_target; }

private:
    //--- the effect ---------------------------------------------------------------------------
    //One Shader, registered with the renderer ONCE, rebuilt in place whenever the effect changes.
    //Registering a second one per effect would leave the quad's custom_shader_index pointing at
    //whichever was registered first - see Renderer::AddCustomShader.
    Shader* effect_shader = NULL;
    int     effect_shader_index = -1;
    Mesh*   effect_mesh = NULL;
    Object* effect_quad = NULL;

    /*
        Asset names ("shaders/bluecube.frag") of every *.frag found under the shaders folder, and
        which of them is currently compiled.

        Behind their own mutex because the folder is rescanned from more than one thread: the
        render thread does it from the panel's Rescan button, and fx_list does it from an MCP
        thread on every call so that a file dropped in the folder a second ago is listed. A
        std::vector being rewritten under a reader is not a stale read, it is a freed buffer.

        LOCK ORDER, where both are taken: effect_list_mutex first, then fx_mutex. EffectStateJson
        and the panel both do it in that order and nothing does the reverse.
    */
    std::mutex effect_list_mutex;
    std::vector<std::string> effect_names;
    int  effect_index = -1;
    bool f_effect_ok = false;
    std::string effect_compile_log;

    //--- the reflected uniforms ---------------------------------------------------------------
    //Written by the panel (render thread) and by fx_set (an MCP thread), read by
    //SetEffectUniforms (render thread), so all three go through fx_mutex. The panel could get
    //away without it - DrawImGuiUI is on the render thread too - but fx_set cannot, and one
    //rule for the map is cheaper to keep true than two.
    std::mutex fx_mutex;
    std::map<std::string,FxUniform> fx_uniforms;

    //--- what the render thread has been asked to do ------------------------------------------
    //Compiling is GL work, so the panel's buttons and the fx_select/fx_reload tools only raise a
    //request here and PreRender acts on it. The tools then wait for f_fx_request_pending to
    //clear, so what they report is what the compiler actually said.
    std::mutex fx_request_mutex;
    int  fx_requested_effect = -1;
    bool f_fx_request_pending = false;

    //--- channels -----------------------------------------------------------------------------
    //iChannel0 defaults to the noise texture below; the rest start empty. An empty channel binds
    //nothing, and a shader sampling it reads whatever was last bound to that unit - which is why
    //the panel says so rather than pretending the channel is black.
    Texture* channel_texture[FX_NUM_CHANNELS] = {NULL,NULL,NULL,NULL};
    std::string channel_name[FX_NUM_CHANNELS];
    //Every image found under the textures folder, for the panel's four combo boxes.
    std::vector<std::string> texture_names;

    //The seeded noise from Application::rrand, on the GPU. Raw GL rather than a Texture because
    //Texture::Create2D only allocates GL_RGB8/GL_RGBA8 and this is one channel of bytes.
    unsigned int noise_tex_id = 0;
    int noise_side = 0;

    //--- test geometry ------------------------------------------------------------------------
    //Off by default, one checkbox on. An effect that fades where it meets geometry cannot be
    //developed against an empty scene, and that is most effects worth transplanting.
    bool f_test_geometry = false;
    Object* test_cube = NULL;
    Object* test_ground = NULL;
    DirectionalLight* sun = NULL;

    //--- camera -------------------------------------------------------------------------------
    vec3 camera_target = vec3(0,0,0);

    /*
        The mouse, sampled on the physics thread and read on the render thread.

        Shadertoy's convention: xy is the current position while the button is held (and the last
        one otherwise), zw is where the button went down, negated while it is up. In VIEWPORT
        pixels with y pointing up, which is the space fragCoord is in.

        Four plain floats crossing a thread boundary with no lock. A torn read here is a mouse
        position one frame old in one component, which is not worth a mutex - but it is worth
        saying out loud rather than leaving someone to wonder.
    */
    vec4 fx_mouse = vec4(0,0,0,0);
    vec2 fx_mouse_click = vec2(0,0);
    bool f_fx_mouse_down = false;

    //--- setup --------------------------------------------------------------------------------
    void BuildQuad();
    void BuildNoiseTexture();
    void RefreshEffectList();
    void RefreshTextureList();
    void BuildTestGeometry();

    //Compiles effect_names[index] into effect_shader. RENDER THREAD ONLY.
    bool BuildEffect(int index);
    //Rebuilds fx_uniforms from the freshly linked program, keeping any value already edited.
    void ReflectUniforms();
    //The Shader::uniform_callback the custom-material pass calls, with the program bound.
    void SetEffectUniforms();

    //--- requests, from whichever thread ------------------------------------------------------
    void RequestEffect(int index);
    //Blocks until PreRender has serviced a request. Not callable from the render thread.
    bool WaitForEffectRequest(int timeout_ms = 2000);

    //--- UI -----------------------------------------------------------------------------------
#ifdef USE_IMGUI
    void RenderEffectPanel();
#endif

    //--- MCP ----------------------------------------------------------------------------------
#ifdef USE_MCP
    void RegisterMCPTools();
#endif
    json EffectStateJson();
    //Matches an effect by exact asset name, by file name, or by a unique substring of either.
    //Returns -1 for no match and -2 for an ambiguous one, so a caller can tell the two apart.
    int FindEffect(const std::string& name);

    //Reads of the list above, taken under effect_list_mutex so a caller on any thread gets a
    //consistent answer it can then use with the lock released. A copy of five short strings is
    //not worth optimising away.
    std::vector<std::string> EffectNamesSnapshot();
    std::string CurrentEffectName();
};

#endif
