#include "ApplicationTestFX.h"
#include "Debug.h"
#include "Primitives.h"
#include "Directory.h"
#include "File.h"
#include "type_helpers.h"
#ifdef USE_MCP
//MCPServer.h and nothing else from it: with USE_MCP=0 that class is not compiled, and the
//header also pulls winsock in with the include-order constraint it documents - both
//pointless in a build with no debug server. See the USE_MCP block in engine.mk.
#include "MCPServer.h"
#endif

#include <algorithm>
#include <math.h>
#include <stdio.h>
#include <string.h>

static Debugger* debug = new Debugger("ApplicationTestFX",DEBUG_ALL);

/*
    Uniforms the BENCH sets, which are therefore not knobs.

    The reflected-uniform panel shows every active uniform that is not in this list, so adding a
    slider is adding a `uniform` line to a .frag and nothing else. Everything here is either
    pushed by SetEffectUniforms below or by Renderer::CustomShaderPass before it, and a slider for
    one of them would be a slider that fights the engine and loses every frame.

    Matched after glGetActiveUniform's "[0]" suffix has been stripped, so the array names appear
    here in their plain form.
*/
static const char* RESERVED_UNIFORMS[] = {
    "iResolution","iTime","iTimeDelta","iFrame","iFrameRate","iMouse","iDate",
    "iChannelResolution","iChannelTime",
    "iChannel0","iChannel1","iChannel2","iChannel3",
    "iCamPos","iCamRight","iCamUp","iCamForward",
    "mat_worldcam","eye_position",
    "gbuffer_depth","gbuffer_position","gbuffer_normal",
};

static bool IsReservedUniform(const std::string& name){
    for (const char* reserved:RESERVED_UNIFORMS){
        if (name == reserved){
            return true;
        }
    }
    return false;
}

//"shaders/bluecube.frag" -> "bluecube.frag". What the panel and the tools show; the asset name is
//what the file layer is given.
static std::string FileNameOf(const std::string& asset_name){
    size_t slash = asset_name.find_last_of("/\\");
    return (slash == std::string::npos) ? asset_name : asset_name.substr(slash + 1);
}

ApplicationTestFX::ApplicationTestFX():Application(){
    debug->Info("Created new ApplicationTestFX.\n");

    /*
        The engine's Scene and Inspector windows are off here, and that is not a cosmetic choice:
        there are three objects in this scene, two of them hidden, and the thing being worked on
        is on the OTHER side of the screen from where those panels dock. The Engine window stays
        on for frame timing, which is worth having when a march loop starts costing something.
    */
    f_show_scene_window = false;
    f_show_inspector_window = false;
    f_show_engine_window = true;
}

void ApplicationTestFX::Init(void){
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_DEFERRED)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }
    //PIPELINE_DEFERRED, even though this app draws almost nothing: the deferred pass is what
    //fills the G-buffer CustomShaderPass binds on units 1-3, and an effect that composites
    //against scene depth needs it whether or not the test geometry is on.
    renderer->alpha_clip = 0.5f;
    renderer->f_render_skybox = false;

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    /*
        120, not the default 50.

        iTime is the tick counter times the timestep, so the tick rate IS the frame rate of every
        animated effect here. At 50 Hz a slow rotation visibly steps. This app simulates nothing -
        there is no physics world and no gameplay - so the extra ticks cost a loop iteration each
        and buy smooth motion.
    */
    SetPhysicsTPS(120.0f);

    main_scene = CreateNewScene("Effect Scene");

    /*
        Framed for bluecube.frag, which is what comes up first: close enough that the slab fills
        the frame the way the reference stills do, a little to the right and above centre so the
        narrow side face and the top edge both read, and aimed slightly below the origin so the
        slab sits high in frame with the fog bank under it.

        A default, not a constraint - the middle mouse button orbits from here like any other app.
    */
    camera_target = vec3(0.0f,-0.04f,0.0f);
    main_scene->camera->SetPosition(vec3(0.48f,0.26f,2.65f));
    main_scene->camera->SetLookAt(camera_target);

    /*
        Draw from the shared stream ONCE, here, before anything else runs.

        Application::rrand is a single shared stream and there is an open backlog item about
        off-tick draws shifting it out from under the simulation. Init is the one place that
        cannot do that: it runs before the physics thread exists. See core/RRandom.h.
    */
    rrand = new RRandom(1);
    BuildNoiseTexture();

    RefreshEffectList();
    RefreshTextureList();
    BuildQuad();
    BuildTestGeometry();

    //Whatever is first alphabetically, unless bluecube is there - the app should come up showing
    //something rather than an empty screen and a file list.
    int start = FindEffect("bluecube.frag");
    if (start < 0){
        start = effect_names.empty() ? -1 : 0;
    }
    if (start >= 0){
        BuildEffect(start);
    }else{
        debug->Err("No *.frag found under the shaders folder - there is nothing to draw\n");
    }

#ifdef USE_MCP
    //Guarded because this app's own tools call into MCPServer, which USE_MCP=0 does not
    //compile. The CORE tools are switched a different way - see engine.mk.
    RegisterMCPTools();
#endif
}

/*
    The seeded noise from Application::rrand, uploaded as a single-channel 2D texture.

    Preferring this over a GLSL hash is deliberate and not only about speed: the bytes are seeded
    and reproducible, the simulation can draw from the SAME stream, and a value read in a shader
    can be checked against the same value read in C++. A hash function in GLSL can be none of
    those. core/RRandom.h documents this path ("for an app that wants this noise on the GPU") and
    this is its first caller.

    Raw GL rather than a Texture because Texture::Create2D only allocates GL_RGB8 or GL_RGBA8, and
    three quarters of that would be duplicate noise.
*/
void ApplicationTestFX::BuildNoiseTexture(void){
    rrand->Generate(256,256);
    noise_side = rrand->GetSquareSide();
    const uint8_t* bytes = rrand->GetBuffer();
    if ((noise_side <= 0) || !bytes){
        debug->Err("No noise to upload - rrand produced an empty buffer\n");
        return;
    }

    /*
        MIPMAPPED, which is not optional here even though the source is a single flat level.

        White noise is the worst possible thing to minify: every texel differs from its neighbour,
        so a ray hitting a surface at a glancing angle samples an essentially random texel per
        pixel and the result crawls and stripes. On the ground plane receding to the horizon it
        showed up as vertical banding that read as a shader bug in the fog, not as sampling - which
        is where an hour went before this was measured.

        Levels for the full chain, filled by glGenerateTextureMipmap, with a trilinear min filter
        so the hardware picks the level that matches the footprint. MAG stays LINEAR because
        magnified noise should be soft blobs, which is exactly what an effect wants of it.
    */
    int levels = 1;
    for (int s = noise_side;s > 1;s >>= 1){
        levels++;
    }

    glCreateTextures(GL_TEXTURE_2D,1,&noise_tex_id);
    glTextureParameteri(noise_tex_id,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTextureParameteri(noise_tex_id,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
    glTextureParameteri(noise_tex_id,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTextureParameteri(noise_tex_id,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTextureStorage2D(noise_tex_id,levels,GL_R8,noise_side,noise_side);
    //One byte per texel, so no row padding to worry about at any width.
    glTextureSubImage2D(noise_tex_id,0,0,0,noise_side,noise_side,GL_RED,GL_UNSIGNED_BYTE,bytes);
    glGenerateTextureMipmap(noise_tex_id);

    channel_name[0] = "rrand noise";
    debug->Ok("Uploaded %ix%i bytes of seeded noise as iChannel0\n",noise_side,noise_side);
}

/*
    The screen-filling quad, and the one custom shader that draws it.

    MakeQuad(2,2) spans -1..+1 in XY, which is already clip space - shaders/fullscreen.vert passes
    position.xy straight through as gl_Position and derives vuv from it. So the object's transform
    is never read and deliberately left at the identity: moving this quad in the Inspector would
    do nothing, which is the correct behaviour for a fullscreen pass and worth knowing before
    someone tries it.
*/
void ApplicationTestFX::BuildQuad(void){
    effect_shader = new Shader();
    //A compile error must not kill the bench. This is the flag core/Shader.h's f_fatal_on_error
    //exists for, and setting it BEFORE the first build is why Shader::Build is a method rather
    //than only a constructor.
    effect_shader->f_fatal_on_error = false;
    effect_shader->uniform_callback = std::bind(&ApplicationTestFX::SetEffectUniforms,this);
    effect_shader_index = renderer->AddCustomShader(effect_shader);

    effect_mesh = MakeQuad(2.0f,2.0f);
    if (!effect_mesh){
        debug->Fatal("Failed to build the fullscreen quad\n");
    }
    effect_mesh->num_references++;
    //mesh_mode is a property of the MESH, which is why this quad is not shared with anything.
    effect_mesh->mesh_mode = MESH_MODE_SHADER;
    effect_mesh->custom_shader_index = effect_shader_index;

    effect_quad = new Object();
    effect_quad->name = "Effect Quad";
    effect_quad->SetMesh(effect_mesh);
    //No material: the shader computes its own colour and never touches the material buffer.
    effect_quad->SetMaterialSlot(0,-1);
    effect_quad->SetPickability(false);
    main_scene->AddObject(effect_quad);
}

/*
    A lit cube and a ground plane, hidden until the checkbox is on.

    They exist so an effect has a real G-buffer to composite against: TEXUNIT_GBUFFER_DEPTH/
    _POSITION/_NORMAL are bound for every custom shader whether or not anything was drawn, and an
    effect that fades where it meets geometry cannot be developed against an empty scene. Read the
    TEXUNIT_GBUFFER_* block at the top of core/Renderer.h before using them: DEPTH is the only
    channel that can say whether anything was drawn at a pixel, and reaching for position first is
    the trap.

    The sun stays on either way - it costs one shadow pass over an empty scene and it means
    turning the geometry on does not also change the lighting.
*/
void ApplicationTestFX::BuildTestGeometry(void){
    sun = new DirectionalLight();
    sun->name = "Directional Light (Sun)";
    sun->SetPosition(vec3(-6,9,7));
    sun->color = vec3(1.0f,0.93f,0.84f);
    sun->brightness = 6.0f;
    sun->viewport.zoom = 6;
    sun->SetLookAt(vec3());
    main_scene->AddObject(sun);

    {
        Material m;
        m.name = "fx_test_surface";
        m.glsl_material.color = vec4(0.72f,0.74f,0.78f,1.0f);
        m.glsl_material.metallic = 0.15f;
        m.glsl_material.roughness = 0.55f;
        renderer->AddMaterial(m);
    }
    int material = renderer->FindMaterialIndex("fx_test_surface");

    test_cube = new Object();
    test_cube->name = "Test Cube";
    test_cube->SetMesh(MakeBox(vec3(1,1,1)));
    test_cube->SetPosition(vec3(0,0.5f,0));
    test_cube->SetMaterialSlot(0,material);
    test_cube->SetVisibility(false);
    main_scene->AddObject(test_cube);

    test_ground = new Object();
    test_ground->name = "Test Ground";
    test_ground->SetMesh(MakeQuad(12.0f,12.0f));
    //MakeQuad lies in XY facing +Z; a floor is that rotated a quarter turn back about X.
    test_ground->SetRotation(quat(vec3(1,0,0),-TYPE_PI * 0.5f));
    test_ground->SetMaterialSlot(0,material);
    test_ground->SetVisibility(false);
    main_scene->AddObject(test_ground);
}

//--- the effect list ----------------------------------------------------------------------------

/*
    Every *.frag under the shaders folder, as ASSET NAMES.

    Directory::GetFiles takes an asset name for the folder and resolves it through the search path
    itself (see core/File.h and the note in Directory.cpp), so "shaders" finds this app's own
    folder - its root comes first - and not shared_assets. What comes back is full resolved paths;
    the file name is pulled back out and re-prefixed with the category, because "shaders/x.frag" is
    what everything else in this engine calls an asset and it is what reads well in a compile log.

    Called at startup and from the panel's Rescan button, so adding an effect is dropping a file in
    the folder rather than a rebuild.
*/
void ApplicationTestFX::RefreshEffectList(void){
    //The scan itself is outside the lock: it hits the disk, and holding a mutex the render thread
    //wants across a directory walk would stall the frame for as long as Windows takes to answer.
    std::vector<std::string> found = Directory::GetFiles("shaders","*.frag");
    std::vector<std::string> next;
    for (const std::string& path:found){
        next.push_back("shaders/" + FileNameOf(path));
    }
    std::sort(next.begin(),next.end());

    std::lock_guard<std::mutex> lock(effect_list_mutex);
    std::string current = (effect_index >= 0 && effect_index < (int)effect_names.size())
                          ? effect_names.at(effect_index) : std::string();
    effect_names = next;

    //The index is a position in a list that just changed, so re-find what was compiled rather
    //than trusting the old number. A rescan that silently switched effect would be baffling.
    effect_index = -1;
    for (int i = 0;i < (int)effect_names.size();i++){
        if (effect_names.at(i) == current){
            effect_index = i;
            break;
        }
    }
    debug->Info("Found %zu effect(s) under shaders/\n",effect_names.size());
}

std::vector<std::string> ApplicationTestFX::EffectNamesSnapshot(void){
    std::lock_guard<std::mutex> lock(effect_list_mutex);
    return effect_names;
}

std::string ApplicationTestFX::CurrentEffectName(void){
    std::lock_guard<std::mutex> lock(effect_list_mutex);
    if ((effect_index < 0) || (effect_index >= (int)effect_names.size())){
        return std::string();
    }
    return effect_names.at(effect_index);
}

/*
    Every image under the textures folder, for the channel combos.

    ONE scan of everything, filtered here, rather than one scan per extension: Directory::GetFiles
    logs an error when a pattern matches nothing, so four patterns over a folder holding only a
    readme would print four errors at every startup - and an engine that cries wolf about a
    perfectly normal empty folder is how a real missing-asset error gets skimmed past. It also
    means adding a format is a line in this list rather than another directory walk.
*/
void ApplicationTestFX::RefreshTextureList(void){
    texture_names.clear();
    const char* extensions[] = {".png",".jpg",".jpeg",".tga",".bmp"};
    std::vector<std::string> found = Directory::GetFiles("textures","*.*");
    for (const std::string& path:found){
        std::string file = FileNameOf(path);
        //Lower-cased for the compare only; the name kept is the one on disk.
        std::string lower = file;
        std::transform(lower.begin(),lower.end(),lower.begin(),::tolower);
        for (const char* extension:extensions){
            if ((lower.size() > strlen(extension)) &&
                (lower.compare(lower.size() - strlen(extension),strlen(extension),extension) == 0)){
                texture_names.push_back("textures/" + file);
                break;
            }
        }
    }
    std::sort(texture_names.begin(),texture_names.end());
}

int ApplicationTestFX::FindEffect(const std::string& name){
    if (name.empty()){
        return -1;
    }
    std::lock_guard<std::mutex> lock(effect_list_mutex);
    for (int i = 0;i < (int)effect_names.size();i++){
        if ((effect_names.at(i) == name) || (FileNameOf(effect_names.at(i)) == name)){
            return i;
        }
    }
    //Nothing matched exactly, so fall back to a substring - "blue" should find bluecube.frag.
    //Ambiguity is reported rather than resolved by picking the first: silently compiling the
    //wrong effect is worse than saying which two were meant.
    int match = -1;
    for (int i = 0;i < (int)effect_names.size();i++){
        if (effect_names.at(i).find(name) != std::string::npos){
            if (match != -1){
                return -2;
            }
            match = i;
        }
    }
    return match;
}

/*
    Compiles effect_names[index] into the one registered effect shader. RENDER THREAD ONLY.

    In place, on the same Shader object, for the reason Renderer::AddCustomShader gives: indices
    are handed out in registration order and the quad's mesh holds one. Building a second Shader
    and appending it would leave the quad drawing with the stale program while the new one drew
    nothing.
*/
bool ApplicationTestFX::BuildEffect(int index){
    //Resolved to a plain string under the lock, so nothing below is holding a reference into a
    //vector another thread may rescan while a compile is running - and a compile is the longest
    //thing this app does.
    std::string name;
    bool f_switch = false;
    {
        std::lock_guard<std::mutex> lock(effect_list_mutex);
        if (!effect_shader || (index < 0) || (index >= (int)effect_names.size())){
            return false;
        }
        name = effect_names.at(index);
        f_switch = (index != effect_index);
        effect_index = index;
    }

    /*
        Give the source files back first.

        Not only on a reload - SWITCHING effect needs this too. LoadFile answers from the
        BinaryAsset cache, so edit A while B is up, switch back to A, and without this you get the
        bytes A was last compiled from. That is the same failure ReleaseFile was added for, in the
        one shape that is easy to miss because it looks like a fresh load.
    */
    for (const std::string& path:effect_shader->source_files){
        ReleaseFile(path.c_str());
    }

    int previous_prog = effect_shader->progid;
    bool f_ok = effect_shader->Build(FX_VERTEX_SHADER,name.c_str());

    effect_compile_log = effect_shader->compile_log;
    f_effect_ok = f_ok;

    if (!f_ok){
        //Keep the last good program on screen behind the error. This is the whole reason
        //core/Shader.h grew f_fatal_on_error: a bench whose purpose is compiling shaders that do
        //not work yet cannot exit on one.
        effect_shader->progid = previous_prog;
        debug->Err("%s did not compile; the previous effect is still drawing\n",name.c_str());
        return false;
    }
    if ((previous_prog != -1) && (previous_prog != effect_shader->progid)){
        glDeleteProgram(previous_prog);
    }
    /*
        A SWITCH starts from the file's defaults; only a reload of the same effect carries edited
        values across. ReflectUniforms cannot tell the two apart - it sees the previous map either
        way - and without this the `frost` dialled in for one effect silently became the `frost`
        of the next: bluecube2.frag was first judged with three of bluecube.frag's values in it,
        and it looked like the new file's numbers were wrong.
    */
    if (f_switch){
        std::lock_guard<std::mutex> lock(fx_mutex);
        fx_uniforms.clear();
    }
    ReflectUniforms();
    debug->Ok("Compiled %s (program %i, %zu knobs)\n",
              name.c_str(),effect_shader->progid,fx_uniforms.size());
    return true;
}

/*
    Rebuilds the knob list from the freshly linked program.

    The DEFAULT of each knob is read back out of the program with glGetUniform*, which is the only
    way to learn it: `uniform float fog_density = 0.04;` carries its value in the GLSL and nothing
    on this side of the boundary knows about it until the program exists.

    A value the user has already edited SURVIVES a reload when the name and type still match. That
    is the difference between iterating on a shader and re-dialling six sliders after every
    compile; the cost is that a changed default in the file does not show until the effect is
    switched away from and back, which the panel's Defaults button also does on demand. A reload
    only: BuildEffect empties the map on a switch, so two effects that happen to share a uniform
    name do not share its value.
*/
void ApplicationTestFX::ReflectUniforms(void){
    std::map<std::string,FxUniform> previous;
    {
        std::lock_guard<std::mutex> lock(fx_mutex);
        previous = fx_uniforms;
    }
    std::map<std::string,FxUniform> next;

    GLint count = 0;
    glGetProgramiv(effect_shader->progid,GL_ACTIVE_UNIFORMS,&count);
    for (int i = 0;i < (int)count;i++){
        GLchar name[128] = {};
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(effect_shader->progid,(GLuint)i,sizeof(name),&length,&size,&type,name);

        std::string uniform_name = name;
        //An array comes back as "iChannelResolution[0]". Skipped either way - this panel drives
        //scalars and vectors, and an array of them wants a widget nobody has asked for yet - but
        //the reserved ones (iChannelResolution, iChannelTime) are skipped SILENTLY while somebody
        //else's array is worth knowing about, since its absence from the panel is otherwise a
        //mystery.
        if (uniform_name.find('[') != std::string::npos){
            std::string base = uniform_name.substr(0,uniform_name.find('['));
            if (!IsReservedUniform(base)){
                debug->Info("Uniform array '%s' has no panel widget and is not settable\n",name);
            }
            continue;
        }
        if (IsReservedUniform(uniform_name)){
            continue;
        }

        FxUniform knob;
        knob.type = type;
        switch (type){
            case GL_FLOAT:      knob.num_components = 1; break;
            case GL_FLOAT_VEC2: knob.num_components = 2; break;
            case GL_FLOAT_VEC3: knob.num_components = 3; break;
            case GL_FLOAT_VEC4: knob.num_components = 4; break;
            case GL_INT:        knob.num_components = 1; knob.f_integer = true; break;
            case GL_BOOL:       knob.num_components = 1; knob.f_integer = true; break;
            //Samplers, matrices and everything else. Not a knob - a sampler is a channel and a
            //matrix has no sensible widget - so it is skipped rather than shown as a broken row.
            default: continue;
        }

        if (knob.f_integer){
            glGetUniformiv(effect_shader->progid,glGetUniformLocation(effect_shader->progid,uniform_name.c_str()),knob.ivalues);
        }else{
            glGetUniformfv(effect_shader->progid,glGetUniformLocation(effect_shader->progid,uniform_name.c_str()),knob.values);
        }

        //Carry an edited value across the reload, but only when it is still the same shape.
        std::map<std::string,FxUniform>::iterator it = previous.find(uniform_name);
        if ((it != previous.end()) && (it->second.type == knob.type)){
            knob = it->second;
        }
        next[uniform_name] = knob;
    }

    std::lock_guard<std::mutex> lock(fx_mutex);
    fx_uniforms = next;
}

/*
    Called by the renderer from inside the MESH_MODE_SHADER pass, with the effect program bound.
    RENDER THREAD.

    Everything an effect can read that the engine does not already provide is set here. See the
    table in docs/testfx_plan.md section 5 and the declarations in shaders/shadertoy.glsl.
*/
void ApplicationTestFX::SetEffectUniforms(void){
    if (!effect_shader || (effect_shader->progid == -1)){
        return;
    }

    float vw = (float)renderer->GetViewportWidth();
    float vh = (float)renderer->GetViewportHeight();
    //z = 1 is shadertoy's pixel aspect ratio, which is 1 on anything that is not a CRT.
    effect_shader->Setvec3("iResolution",vec3(vw,vh,1.0f));

    /*
        TIME FROM THE TICK, NOT FROM A CLOCK.

        physics_tick is a std::atomic<uint64_t>, so reading it here is fine. What this buys is
        that sim_pause freezes the effect and sim_step advances it by an exact number of frames -
        so a screenshot of frame N is reproducible and a timing bug can be told apart from a
        shader bug. Durations in this codebase are ticks; this is no exception.
    */
    uint64_t tick = main_scene ? main_scene->GetPhysicsTick() : 0;
    float dt = GetPhysicsTimestep();
    float time = (float)tick * dt;
    effect_shader->Setfloat("iTime",time);
    effect_shader->Setfloat("iTimeDelta",dt);
    effect_shader->Setfloat("iFrameRate",1.0f / dt);
    effect_shader->Setint("iFrame",(int)tick);
    effect_shader->Setvec4("iMouse",fx_mouse);
    //Declared so a snippet using it compiles, and fed from the TICK rather than from the wall
    //clock so that this app stays reproducible. Year/month/day are zero on purpose: a bench whose
    //whole claim is "frame N looks like this" must not have a value in it that changes at midnight.
    effect_shader->Setvec4("iDate",vec4(0,0,0,time));

    float channel_time[FX_NUM_CHANNELS];
    vec3 channel_resolution[FX_NUM_CHANNELS];
    for (int i = 0;i < FX_NUM_CHANNELS;i++){
        channel_time[i] = time;
        if (i == 0 && channel_texture[0] == NULL && noise_tex_id != 0){
            channel_resolution[i] = vec3((float)noise_side,(float)noise_side,1.0f);
        }else if (channel_texture[i]){
            channel_resolution[i] = vec3((float)channel_texture[i]->width,
                                         (float)channel_texture[i]->height,1.0f);
        }else{
            channel_resolution[i] = vec3(0,0,0);
        }
    }
    effect_shader->Setfloatv("iChannelTime",channel_time,FX_NUM_CHANNELS);
    effect_shader->Setvec3v("iChannelResolution",channel_resolution,FX_NUM_CHANNELS);

    /*
        The camera basis, pre-scaled by fov and aspect, so a native effect raymarches the scene's
        REAL camera with

            dir = normalize(iCamForward + uv.x * iCamRight + uv.y * iCamUp)

        for uv in -1..+1. Built on the CPU rather than by inverting a view-projection in GLSL
        because there is no general matrix inverse in core/type_fmat4.h - inverse_transform is
        rigid-only - and because this is four vec3s a frame against a per-pixel inversion.

        Object::GetLeft() returns the +X axis of the object's rotation, which for a camera looking
        down its own -Z is screen RIGHT. The name is a historical quirk of ref_left = (1,0,0) in
        core/Object.cpp, not a sign error here; Camera::GetPixelRay uses it the same way.
    */
    Camera* camera = main_scene ? main_scene->camera : NULL;
    if (camera){
        float aspect = (vh > 0.0f) ? (vw / vh) : 1.0f;
        float half_height = tanf(toradians(camera->viewport.fov) * 0.5f);
        effect_shader->Setvec3("iCamPos",camera->GetPosition());
        effect_shader->Setvec3("iCamForward",camera->GetForward());
        effect_shader->Setvec3("iCamRight",camera->GetLeft() * (half_height * aspect));
        effect_shader->Setvec3("iCamUp",camera->GetUp() * half_height);
    }

    //The channels. Unit 0 falls back to the seeded noise, which is what makes a fresh .frag that
    //samples iChannel0 work with nothing configured.
    for (int i = 0;i < FX_NUM_CHANNELS;i++){
        if (channel_texture[i]){
            glBindTextureUnit(TEXUNIT_FX_CHANNEL0 + i,channel_texture[i]->texture_id);
        }else if ((i == 0) && (noise_tex_id != 0)){
            glBindTextureUnit(TEXUNIT_FX_CHANNEL0,noise_tex_id);
        }
    }

    //The knobs, pushed from the C++ side rather than written into the program by whoever moved
    //the slider - see the note on fx_uniforms in the header.
    {
        std::lock_guard<std::mutex> lock(fx_mutex);
        for (std::map<std::string,FxUniform>::iterator it = fx_uniforms.begin();it != fx_uniforms.end();++it){
            const FxUniform& knob = it->second;
            const char* name = it->first.c_str();
            if (knob.f_integer){
                effect_shader->Setint(name,knob.ivalues[0]);
                continue;
            }
            switch (knob.num_components){
                case 1: effect_shader->Setfloat(name,knob.values[0]); break;
                case 2: effect_shader->Setvec2(name,vec2(knob.values[0],knob.values[1])); break;
                case 3: effect_shader->Setvec3(name,vec3(knob.values[0],knob.values[1],knob.values[2])); break;
                case 4: effect_shader->Setvec4(name,vec4(knob.values[0],knob.values[1],knob.values[2],knob.values[3])); break;
            }
        }
    }

    /*
        The quad neither writes depth nor gets rejected by it.

        Without the depth write off, a fullscreen quad at gl_Position.z = 0 would stamp itself
        across the whole depth buffer; without the test off it would be rejected wherever the test
        geometry is nearer. Both are what ApplicationShip::SetVolumeUniforms does and for the same
        reasons. Renderer::CustomShaderPass restores them after the pass, so nothing leaks into
        the next frame's depth passes.

        Cull face is deliberately left alone: MakeQuad winds counter-clockwise seen from +Z and
        fullscreen.vert passes the vertices through untouched, so the default GL_BACK keeps it.
    */
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
}

//--- requests across the thread boundary --------------------------------------------------------

void ApplicationTestFX::RequestEffect(int index){
    std::lock_guard<std::mutex> lock(fx_request_mutex);
    fx_requested_effect = index;
    f_fx_request_pending = true;
}

bool ApplicationTestFX::WaitForEffectRequest(int timeout_ms){
    //Polled rather than signalled, the same shape as Application::StepPhysicsAndWait. A frame at
    //any sane rate is well inside this; a timeout means the render thread is not running.
    for (int waited_ms = 0;waited_ms < timeout_ms;waited_ms += 5){
        {
            std::lock_guard<std::mutex> lock(fx_request_mutex);
            if (!f_fx_request_pending){
                return true;
            }
        }
        Sleep(5);
    }
    return false;
}

/*
    Frame thread, once per frame, before the scene is drawn.

    Compiling is GL work and the panel's buttons run here too, but the fx_select/fx_reload tools do
    not - they run on their own threads. So everything that builds a program funnels through this
    one point. UpdateView is NOT the place, close as it sounds: it runs on the physics thread,
    which may not touch GL at all.
*/
void ApplicationTestFX::PreRender(void){
    int requested = -1;
    {
        std::lock_guard<std::mutex> lock(fx_request_mutex);
        if (!f_fx_request_pending){
            return;
        }
        requested = fx_requested_effect;
    }

    BuildEffect(requested);

    std::lock_guard<std::mutex> lock(fx_request_mutex);
    f_fx_request_pending = false;
}

/*
    Physics thread, every pass - including the ones that simulate nothing, which here is all of
    them. Camera and mouse only: this app has no simulation, so there is no RunSimulationTick.
*/
void ApplicationTestFX::UpdateView(void){
    if (!main_scene || !main_scene->inputcontroller){
        return;
    }
    InputController* input = main_scene->inputcontroller;
    Camera* camera = main_scene->camera;
    if (!camera){
        return;
    }

    /*
        The mouse, in the space fragCoord is in: viewport pixels with y pointing UP.

        GetRelativeMousePosition measures from the window's TOP-left, so the flip is the whole
        conversion. Sampled here, on the thread that owns KeyState, and read from the render
        thread - see the note on fx_mouse in the header.
    */
    int2 cursor = input->GetRelativeMousePosition();
    float mx = (float)cursor.x;
    float my = (float)renderer->GetViewportHeight() - (float)cursor.y;
    bool f_down = input->IsKeyDown(INPUT_CLICK_LEFT);
    if (f_down && !f_fx_mouse_down){
        fx_mouse_click = vec2(mx,my);
    }
    if (f_down){
        fx_mouse = vec4(mx,my,fx_mouse_click.x,fx_mouse_click.y);
    }else{
        //Shadertoy's sign convention: zw goes negative once the button is released, so a shader
        //can tell "never clicked / not clicking" from "clicked here".
        fx_mouse = vec4(fx_mouse.x,fx_mouse.y,-fabsf(fx_mouse_click.x),-fabsf(fx_mouse_click.y));
    }
    f_fx_mouse_down = f_down;

    //Orbit on the middle mouse button, the same gesture the other apps use. Shift drags the
    //target instead of turning around it.
    if (input->IsKeyDown(INPUT_CLICK_MIDDLE)){
        int dx = input->GetDelta(INPUT_MOUSE_X);
        int dy = input->GetDelta(INPUT_MOUSE_Y);
        if (input->IsKeyDown(INPUT_SHIFT)){
            vec3 d = camera->MoveSidewaysBy(-dx / 200.0f);
            d += camera->MoveUpBy(dy / 200.0f);
            camera_target += d;
        }else{
            //Pitch about the camera's own right axis first, then yaw about world up, so the
            //horizon stays level however far round it goes.
            vec3 p = camera->GetPosition() - camera_target;
            quat q(camera->GetLeft(),-dy / 120.0f);
            p = q * p;
            camera->SetPosition(p + camera_target);
            vec3 up = camera->GetUp();
            camera->SetLookAt(camera_target,&up);

            p = camera->GetPosition() - camera_target;
            q.set_rotation(vec3(0,1,0),-dx / 120.0f);
            p = q * p;
            camera->SetPosition(p + camera_target);
            camera->RotateBy(q);
        }
    }

    /*
        Wheel zoom, as a fraction of the current distance so it slows down as it closes in.

        CLAMPED, and the clamp is not defensive tidiness. A step proportional to the distance is
        geometric in both directions, so scrolling out compounds: forty notches take the camera
        from 2.6 units to nearly 300, the effect becomes a speck, and it is not obvious from
        looking at a black screen that the camera is the problem rather than the shader. That
        happened during the first tuning session and cost two screenshots figuring out what had
        gone wrong. 0.4 to 40 covers everything from inside the effect to well outside it.
    */
    int wheel = input->GetDelta(INPUT_MOUSE_WHEEL);
    if (wheel != 0){
        float distance = (camera->GetPosition() - camera_target).length();
        float target = clamp(distance * (1.0f - 0.12f * (float)wheel),0.4f,40.0f);
        camera->MoveForwardBy(distance - target);
    }
}

//--- UI -----------------------------------------------------------------------------------------

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationTestFX::DrawImGuiUI(void){
    RenderApplicationUI();
    RenderEffectPanel();
}
#endif //USE_IMGUI

#ifdef USE_IMGUI
//Panel code, so it is not in a build without ImGui. The engine calls DrawImGuiUI
//unconditionally; with USE_IMGUI=0 the base class version is an empty one. See engine.mk.
void ApplicationTestFX::RenderEffectPanel(void){
    //Clear of the engine's own panels, which dock into the left edge, and wide enough that a
    //uniform's name is not truncated - ImGui puts a widget's label to its RIGHT, so a narrow
    //window turns "contact_strength" into "contact_str..." and the panel stops being readable at
    //exactly the moment it has something to say. FirstUseEver, so imgui.ini wins afterwards.
    ImGui::SetNextWindowPos(ImVec2(340,16),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440,720),ImGuiCond_FirstUseEver);
    ImGui::Begin("Effect");

    //Every widget below leaves this many pixels at the right for its label.
    ImGui::PushItemWidth(-170.0f);

    //--- which shader ---------------------------------------------------------------------
    //A snapshot, not the live vector: fx_list rescans the folder from an MCP thread, and walking
    //a vector another thread is rewriting is a freed buffer rather than a stale name.
    std::vector<std::string> names = EffectNamesSnapshot();
    std::string current_name = CurrentEffectName();
    std::string current = current_name.empty() ? std::string("<none>") : FileNameOf(current_name);
    if (ImGui::BeginCombo("Shader",current.c_str())){
        for (int i = 0;i < (int)names.size();i++){
            bool f_selected = (names.at(i) == current_name);
            if (ImGui::Selectable(FileNameOf(names.at(i)).c_str(),f_selected)){
                //A request rather than a call, even here: DrawImGuiUI runs on the render thread,
                //but it runs holding physics_mutex and inside ImGui's frame. PreRender is the one
                //place a program is built, and having exactly one keeps that true.
                RequestEffect(i);
            }
            if (f_selected){
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Reload")){
        RequestEffect(effect_index);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rescan folder")){
        RefreshEffectList();
        RefreshTextureList();
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults")){
        //Drops every edited value so the next reflect takes what the GLSL declares. Reflecting
        //happens on the build, so this has to be followed by one.
        {
            std::lock_guard<std::mutex> lock(fx_mutex);
            fx_uniforms.clear();
        }
        RequestEffect(effect_index);
    }

    if (f_effect_ok){
        ImGui::TextColored(ImVec4(0.5f,0.9f,0.5f,1.0f),"compiled - program %i",
                           effect_shader ? effect_shader->progid : -1);
    }else{
        ImGui::TextColored(ImVec4(1.0f,0.5f,0.4f,1.0f),"NOT COMPILED - showing the last good one");
    }
    if (!effect_compile_log.empty()){
        /*
            The GLSL log, in a scrolling child rather than an InputTextMultiline.

            InputText would want a writable char* and the only one to hand is the std::string's
            own buffer, which ImGui may legitimately write into even with a read-only flag. A
            child plus TextWrapped cannot, and the one thing InputText would have bought - being
            able to select the text - is a Copy button instead.

            Note the line numbers a GLSL error carries: "1(98)" means line 98 of source 1, not of
            the .frag. Shader::ResolveIncludes emits `#line 1 <n>` around each spliced chunk
            precisely so that mapping exists, and the Source files tree below prints the numbers.
        */
        if (ImGui::Button("Copy log")){
            ImGui::SetClipboardText(effect_compile_log.c_str());
        }
        ImGui::BeginChild("##compile_log",ImVec2(0,110),true,ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::TextUnformatted(effect_compile_log.c_str());
        ImGui::EndChild();
    }

    if (effect_shader && !effect_shader->source_files.empty()){
        if (ImGui::TreeNode("Source files")){
            /*
                The numbering a compile error uses is PER STAGE and restarts at 0 for the file
                being compiled: source_files holds the vertex stage and its includes first, then
                the fragment and its includes, but Shader::ResolveIncludes counts from zero again
                when it starts on the fragment. So the fragment's own numbers begin wherever fname
                appears in the list, and that is what is printed - a "2(17)" in the log above is
                then the file this tree calls 2.
            */
            int frag_start = 0;
            for (size_t i = 0;i < effect_shader->source_files.size();i++){
                if (effect_shader->source_files.at(i) == effect_shader->fname){
                    frag_start = (int)i;
                    break;
                }
            }
            for (size_t i = 0;i < effect_shader->source_files.size();i++){
                int number = (int)i - frag_start;
                if (number < 0){
                    ImGui::Text("    %s  (vertex stage)",effect_shader->source_files.at(i).c_str());
                }else{
                    ImGui::Text("%i   %s",number,effect_shader->source_files.at(i).c_str());
                }
            }
            ImGui::TreePop();
        }
    }

    //--- time -----------------------------------------------------------------------------
    ImGui::Separator();
    if (main_scene){
        uint64_t tick = main_scene->GetPhysicsTick();
        ImGui::Text("iFrame %llu    iTime %.3f",(unsigned long long)tick,
                    (float)tick * GetPhysicsTimestep());
        bool f_paused = main_scene->IsPhysicsPaused();
        if (ImGui::Checkbox("Paused",&f_paused)){
            main_scene->PausePhysics(f_paused);
        }
        ImGui::SameLine();
        if (ImGui::Button("Step")){
            main_scene->StepPhysics(1);
        }
        ImGui::SameLine();
        if (ImGui::Button("Step 10")){
            main_scene->StepPhysics(10);
        }
    }

    //--- test geometry --------------------------------------------------------------------
    if (ImGui::Checkbox("Test geometry",&f_test_geometry)){
        //Direct rather than through a SimCommand: nothing here is simulation state, there is no
        //simulation, and DrawImGuiUI already holds physics_mutex so it cannot race a tick.
        if (test_cube){
            test_cube->SetVisibility(f_test_geometry);
        }
        if (test_ground){
            test_ground->SetVisibility(f_test_geometry);
        }
    }

    //--- channels -------------------------------------------------------------------------
    ImGui::Separator();
    ImGui::Text("Channels");
    for (int i = 0;i < FX_NUM_CHANNELS;i++){
        char label[32];
        snprintf(label,sizeof(label),"iChannel%i",i);
        std::string shown = channel_name[i].empty() ? std::string("<none>") : channel_name[i];
        if (ImGui::BeginCombo(label,shown.c_str())){
            if (ImGui::Selectable("<none>",channel_name[i].empty())){
                channel_texture[i] = NULL;
                channel_name[i].clear();
            }
            if ((i == 0) && (noise_tex_id != 0)){
                if (ImGui::Selectable("rrand noise",channel_name[0] == "rrand noise")){
                    channel_texture[0] = NULL;      //NULL on channel 0 means the noise
                    channel_name[0] = "rrand noise";
                }
            }
            for (const std::string& name:texture_names){
                if (ImGui::Selectable(FileNameOf(name).c_str(),channel_name[i] == name)){
                    Texture* texture = renderer->LoadTexture(name.c_str());
                    if (texture){
                        channel_texture[i] = texture;
                        channel_name[i] = name;
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    //--- the reflected uniforms -----------------------------------------------------------
    ImGui::Separator();
    {
        std::lock_guard<std::mutex> lock(fx_mutex);
        ImGui::Text("Uniforms (%zu)",fx_uniforms.size());
        for (std::map<std::string,FxUniform>::iterator it = fx_uniforms.begin();it != fx_uniforms.end();++it){
            FxUniform& knob = it->second;
            const char* name = it->first.c_str();
            if (knob.type == GL_BOOL){
                bool value = (knob.ivalues[0] != 0);
                if (ImGui::Checkbox(name,&value)){
                    knob.ivalues[0] = value ? 1 : 0;
                }
                continue;
            }
            if (knob.f_integer){
                ImGui::DragInt(name,knob.ivalues,1.0f,-64,64);
                continue;
            }
            switch (knob.num_components){
                //No range on these: a shader's units are its own business, and a slider clamped to
                //a guess is worse than a drag that goes wherever the value needs to go.
                case 1: ImGui::DragFloat(name,knob.values,0.01f,0,0,"%.4f"); break;
                case 2: ImGui::DragFloat2(name,knob.values,0.01f,0,0,"%.4f"); break;
                //A three-component uniform is a colour often enough to be worth the extra widget,
                //and a colour is the one thing a set of three drags is genuinely bad at. The
                //swatch goes FIRST, on the left: after the drag it lands past the right edge of
                //the window and is the one thing that gets clipped away.
                case 3:
                    ImGui::ColorEdit3((std::string("##c") + name).c_str(),knob.values,
                                      ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoLabel);
                    ImGui::SameLine();
                    ImGui::DragFloat3(name,knob.values,0.01f,0,0,"%.4f");
                    break;
                case 4: ImGui::DragFloat4(name,knob.values,0.01f,0,0,"%.4f"); break;
            }
        }
    }

    ImGui::PopItemWidth();
    ImGui::End();
}
#endif //USE_IMGUI

//--- MCP ----------------------------------------------------------------------------------------

json ApplicationTestFX::EffectStateJson(void){
    json state;
    state["effect"] = CurrentEffectName();
    state["compiled"] = f_effect_ok;
    state["program"] = effect_shader ? effect_shader->progid : -1;
    state["compile_log"] = effect_compile_log;
    state["vertex_shader"] = FX_VERTEX_SHADER;
    if (effect_shader){
        state["source_files"] = effect_shader->source_files;
    }

    uint64_t tick = main_scene ? main_scene->GetPhysicsTick() : 0;
    state["iFrame"] = tick;
    state["iTime"] = (float)tick * GetPhysicsTimestep();
    state["iTimeDelta"] = GetPhysicsTimestep();
    state["paused"] = main_scene ? (bool)main_scene->IsPhysicsPaused() : false;
    state["test_geometry"] = f_test_geometry;

    json channels = json::array();
    for (int i = 0;i < FX_NUM_CHANNELS;i++){
        channels.push_back(channel_name[i]);
    }
    state["channels"] = channels;

    json uniforms = json::object();
    {
        std::lock_guard<std::mutex> lock(fx_mutex);
        for (std::map<std::string,FxUniform>::iterator it = fx_uniforms.begin();it != fx_uniforms.end();++it){
            const FxUniform& knob = it->second;
            if (knob.f_integer){
                uniforms[it->first] = knob.ivalues[0];
                continue;
            }
            json value = json::array();
            for (int c = 0;c < knob.num_components;c++){
                value.push_back(knob.values[c]);
            }
            //A one-component uniform reads back as a number, not as a one-element array, because
            //that is what fx_set accepts for it and the two should look the same.
            uniforms[it->first] = (knob.num_components == 1) ? json(knob.values[0]) : value;
        }
    }
    state["uniforms"] = uniforms;
    return state;
}

#ifdef USE_MCP
//This app's own MCP tools. Present only when USE_MCP=1; see the block in engine.mk for why
//the core half of the same switch is a swapped translation unit rather than an #ifdef.
void ApplicationTestFX::RegisterMCPTools(void){
    MCPServer::Get()->RegisterTool("fx_list",
        "List the effects this bench can compile - every *.frag under apps/testfx/assets/shaders "
        "- and say which one is current and whether it compiled. The folder is rescanned on each "
        "call, so an effect added while the app is running shows up here without a restart.",
        json{{"type","object"},{"properties",json::object()}},
        [this](const json& args) -> json {
            //Rescanning here is what makes "drop a file in the folder" work without a restart, and
            //it happens on this MCP thread rather than on the render thread because it touches no
            //GL at all - only the app's own list, which is what effect_list_mutex is for.
            RefreshEffectList();
            std::vector<std::string> names = EffectNamesSnapshot();
            std::string current = CurrentEffectName();
            json effects = json::array();
            for (const std::string& name:names){
                effects.push_back(json{
                    {"name",name},
                    {"file",FileNameOf(name)},
                    {"current",name == current}
                });
            }
            json result;
            result["effects"] = effects;
            result["count"] = (int)names.size();
            result["current"] = current;
            result["compiled"] = f_effect_ok;
            result["compile_log"] = effect_compile_log;
            return result;
        });

    MCPServer::Get()->RegisterTool("fx_select",
        "Switch to another effect and compile it. `name` is an asset name "
        "(\"shaders/bluecube.frag\"), a file name (\"bluecube.frag\") or a unique substring of "
        "either - an ambiguous substring is refused rather than resolved by guessing. The source "
        "is re-read from disk, so this doubles as a reload of an effect you switched away from and "
        "edited. Returns the full state, compile log included; a shader that does not compile is "
        "reported and the previous one keeps drawing.",
        json{
            {"type","object"},
            {"properties", {
                {"name", {{"type","string"},{"description","effect to compile, as listed by fx_list"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the switch"}}}
            }},
            {"required",json::array({"name"})}
        },
        [this](const json& args) -> json {
            std::string name = args.value("name",std::string());
            int index = FindEffect(name);
            if (index == -1){
                //Rescan before giving up, so a file dropped in the folder a moment ago is found
                //by the call that names it rather than only by the fx_list after it. "Adding an
                //effect is dropping a file in the folder" is the app's whole premise; needing two
                //calls to act on that is the kind of small tax that turns into a habit.
                RefreshEffectList();
                index = FindEffect(name);
            }
            if (index == -2){
                return json{ {"error","\"" + name + "\" matches more than one effect - see fx_list"} };
            }
            if (index < 0){
                return json{ {"error","no effect matching \"" + name + "\" - see fx_list"} };
            }
            RequestEffect(index);
            if (!WaitForEffectRequest()){
                return json{ {"error","the render thread did not compile it in time"} };
            }
            return MaybeAttachScreenshot(EffectStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("fx_reload",
        "Recompile the CURRENT effect from disk and return the compile log - the edit-and-look "
        "loop, without a rebuild or a relaunch. Every source file is released first, #included "
        "ones too, so editing a shared .glsl reaches this. A shader that fails to compile leaves "
        "the last working one on screen and reports the GLSL error rather than taking the app "
        "down. The uniform knobs keep the values you set, so a reload does not cost you six "
        "sliders; pass reset_uniforms to take the defaults out of the file instead.",
        json{
            {"type","object"},
            {"properties", {
                {"reset_uniforms", {{"type","boolean"},{"description","drop edited uniform values and take the shader's own defaults"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the reload"}}}
            }}
        },
        [this](const json& args) -> json {
            if (effect_index < 0){
                return json{ {"error","no effect is selected - see fx_list"} };
            }
            if (args.value("reset_uniforms",false)){
                std::lock_guard<std::mutex> lock(fx_mutex);
                fx_uniforms.clear();
            }
            RequestEffect(effect_index);
            if (!WaitForEffectRequest()){
                return json{ {"error","the render thread did not compile it in time"} };
            }
            return MaybeAttachScreenshot(EffectStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("fx_set",
        "Set one of the current effect's uniforms. `name` is the uniform as declared in the GLSL "
        "and `value` is a number for a float/int/bool or an array of 2-4 numbers for a vector - "
        "the same shape fx_state reports it in. Only uniforms the bench does not own can be set: "
        "iTime, iResolution, the camera basis and the channels are driven by the app and are not "
        "in the list. The value is held on the C++ side and pushed to the program on the render "
        "thread every frame, so it survives a reload and never writes to a program mid-draw.",
        json{
            {"type","object"},
            {"properties", {
                {"name", {{"type","string"},{"description","uniform name, as listed by fx_state"}}},
                {"value", {{"description","a number, or an array of 2-4 numbers"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the change"}}}
            }},
            {"required",json::array({"name","value"})}
        },
        [this](const json& args) -> json {
            std::string name = args.value("name",std::string());
            std::vector<float> values;
            const json& value = args.at("value");
            if (value.is_number()){
                values.push_back(value.get<float>());
            }else if (value.is_boolean()){
                values.push_back(value.get<bool>() ? 1.0f : 0.0f);
            }else if (value.is_array()){
                for (const json& v:value){
                    if (!v.is_number()){
                        return json{ {"error","value array must hold numbers"} };
                    }
                    values.push_back(v.get<float>());
                }
            }else{
                return json{ {"error","value must be a number or an array of numbers"} };
            }

            {
                std::lock_guard<std::mutex> lock(fx_mutex);
                std::map<std::string,FxUniform>::iterator it = fx_uniforms.find(name);
                if (it == fx_uniforms.end()){
                    return json{ {"error","the current effect has no settable uniform called \"" +
                                          name + "\" - see fx_state"} };
                }
                FxUniform& knob = it->second;
                if ((int)values.size() != knob.num_components){
                    return json{ {"error","\"" + name + "\" takes " +
                                          std::to_string(knob.num_components) + " component(s), got " +
                                          std::to_string(values.size())} };
                }
                for (int c = 0;c < knob.num_components;c++){
                    knob.values[c] = values.at(c);
                    knob.ivalues[c] = (int)lroundf(values.at(c));
                }
            }

            //One frame, so the screenshot below (and the state read back) is of the new value
            //rather than of the one it replaced. SetEffectUniforms pushes on the render thread.
            Sleep(20);
            return MaybeAttachScreenshot(EffectStateJson(),args.value("include_screenshot",false));
        });

    /*
        A sixth tool, beyond the five docs/testfx_plan.md section 7 names.

        The plan makes the test geometry a checkbox and nothing else, which quietly excludes the
        one caller the bench is most for: an effect that fades where it meets geometry cannot be
        developed against an empty scene (section 2 says so), so an agent working headless on
        exactly that kind of effect would have needed somebody at the keyboard to turn the scene
        on. That is the opposite of "the same loop runs headless".
    */
    MCPServer::Get()->RegisterTool("fx_scene",
        "Turn the test geometry - a lit cube on a ground plane - on or off, and report what the "
        "scene behind the effect currently is. The geometry exists so an effect has a real "
        "G-buffer to composite against: gbuffer_depth/position/normal are bound for every custom "
        "shader whether or not anything was drawn, and with nothing drawn the depth channel reads "
        "1.0 everywhere, which is a legitimate but not very interesting thing to develop against. "
        "Call with no argument to just read the state.",
        json{
            {"type","object"},
            {"properties", {
                {"test_geometry", {{"type","boolean"},{"description","show the cube and ground plane"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot after the change"}}}
            }}
        },
        [this](const json& args) -> json {
            if (args.contains("test_geometry")){
                bool f_on = args.at("test_geometry").get<bool>();
                /*
                    Visibility is object state the render thread walks in CullObjects, and an MCP
                    handler holds no lock at all - so this goes through AtTickBoundary, which is
                    the documented way for another thread to touch the scene (see core/Scene.h).
                    A SimCommand would be the other way and is the better one where a command
                    type exists; none does for visibility, and inventing one in an app that has
                    no simulation would be ceremony rather than determinism.
                */
                if (main_scene){
                    main_scene->AtTickBoundary([this,f_on](){
                        f_test_geometry = f_on;
                        if (test_cube){
                            test_cube->SetVisibility(f_on);
                        }
                        if (test_ground){
                            test_ground->SetVisibility(f_on);
                        }
                    });
                }
                //One frame, so a screenshot below is of the scene as changed. Outside the lock -
                //AtTickBoundary must not wait on the render thread, and a screenshot does.
                Sleep(20);
            }
            return MaybeAttachScreenshot(EffectStateJson(),args.value("include_screenshot",false));
        });

    MCPServer::Get()->RegisterTool("fx_state",
        "Everything about the current effect: which file, whether it compiled, the compile log, "
        "the source files it was built from, the tick and iTime, what is on each iChannel, and "
        "every settable uniform with its current value. This is the one to call to find out what "
        "fx_set can be pointed at.",
        json{
            {"type","object"},
            {"properties", {
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot alongside the state"}}}
            }}
        },
        [this](const json& args) -> json {
            return MaybeAttachScreenshot(EffectStateJson(),args.value("include_screenshot",false));
        });
}
#endif //USE_MCP
