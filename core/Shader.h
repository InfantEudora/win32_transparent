#ifndef _SHADER_H_
#define _SHADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <set>
#include <functional>

#include "type_vec2.h"
#include "type_vec3.h"
#include "type_vec4.h"
#include "type_fmat3.h"
#include "type_fmat4.h"

class Shader{
public:
    int progid = -1;        // ID of the compiled shader program.

    //Names for the currently loaded shader files.
    std::string vname; //Vertex shader name
    std::string fname; //Fragment

    /*
        Every file this program's source actually came from, in the order it was read: the vertex
        and fragment (or compute) files, plus everything they #include, at whatever depth.

        It exists for hot reload. A reload has to ReleaseFile each of these before rebuilding or
        LoadFile answers from the cache and recompiles the bytes read at start-up - and an
        #included file is the one you most want to be able to edit, since shaders/density.glsl is
        shared between raymarch_volume.frag and cloud_shadow.comp precisely so that editing it
        changes both. vname/fname alone cannot tell you about it.
    */
    std::vector<std::string> source_files;

    bool f_compiled = false;

    /*
        Whether a compile or link error takes the process down.

        True is the historical behaviour and stays the default, so every existing call site is
        unchanged: an engine shader that will not compile means there is nothing left to run, and
        dying at the point of failure with the GLSL log on stderr is the right answer for that.

        False is for the case where a shader failing to compile is the NORMAL outcome rather than
        a catastrophe - a bench whose whole purpose is compiling shaders that do not work yet, or
        anything hot-reloading one while a user edits it. The four build functions then log through
        debug->Err, leave the log in compile_log below, and return failure; progid is left at -1
        and the caller decides what to do.

        Reload() below ignores this and is always soft, for the reason stated there.
    */
    bool f_fatal_on_error = true;

    //The GLSL info log from the last build, success or failure - every stage's, appended in the
    //order they ran, each prefixed with which stage produced it. Empty means the driver had
    //nothing to say, which is what a clean compile looks like. This is what a UI panel or an MCP
    //tool shows the person who just broke the shader; stderr is not reachable from either.
    std::string compile_log;

    //Set by CreateComputeShader, so Reload knows which of the two build paths built this program.
    //A compute shader keeps its path in fname - it has no vertex stage for vname to describe.
    bool f_is_compute = false;

    /*
        CUSTOM-MATERIAL PASS ONLY: draw this program into the renderer's reduced-resolution target
        instead of straight into the frame. Ignored by every other pass and by every shader the
        renderer owns.

        WHAT IT IS FOR. A raymarched volume is the one thing in this engine whose cost is per
        PIXEL rather than per vertex - tens to hundreds of 3D texture fetches each - so it is the
        one thing worth drawing at a fraction of the window's resolution and scaling back up. At
        Renderer::SetCustomShaderScale(2) that is a quarter of the fragments and therefore a
        quarter of the marches.

        AND IT IS ALSO A LOOK. The upscale is nearest-neighbour, so what comes back is honestly
        blocky. For apps/bomber that is the point - the blast is meant to read as chunky pixels
        against a smooth world - and for anything that does NOT want that, this flag is the wrong
        tool and the answer is fewer march steps, not fewer pixels.

        WHAT THE SHADER OWES IN RETURN, and it is not optional: gl_FragCoord is in the LOW-RES
        target's pixels while the G-buffer is still full size, so a shader that reaches for the
        scene behind it must divide by the uniform `render_target_size` (which the renderer sets
        on every custom shader, and which is simply the window size when this flag is false)
        rather than by textureSize(gbuffer_depth,0). Getting that wrong does not fail loudly - it
        samples the wrong part of the scene and the volume is occluded by the wrong geometry.

        The low-res target has NO DEPTH BUFFER, so this suits a shader that resolves its own
        occlusion against the G-buffer - which is what the volume contract on
        Renderer::AddCustomShader already asks for - and not one that wants to depth-test.
    */
    bool f_lowres = false;

    /*
        CUSTOM-MATERIAL PASS ONLY: also draw this program's meshes into the deferred G-buffer,
        using the renderer's own deferred shader. Off by default, and off is right for a volume.

        WHAT IT IS FOR. A custom shader replaces how a surface is COLOURED. Some of them replace
        what a surface IS as well - a raymarched volume has no surface at all, and the box it is
        drawn on is a container, not geometry. Those must stay out of the G-buffer, which is why
        MESH_MODE_SHADER was taken out of DeferredPass: a volume that wrote depth would occlude
        itself, and would swallow every mouse pick made through it.

        But a custom shader can equally be an ordinary solid thing that simply computes its own
        colour - apps/bomber's water tiles are a slab of floor with an animated pattern on them.
        For those, staying out of the G-buffer is a bug with three faces: they cannot be picked,
        they are invisible to anything sampling the scene's depth, and - the one that actually
        shows - A VOLUME THAT CLAMPS ITS MARCH TO THE G-BUFFER MARCHES STRAIGHT THROUGH THEM. In
        bomber that is a fireball spilling below the waterline on exactly the tiles a blast is
        allowed to cross, next to grass tiles where it stops correctly. Measured, not feared: 2.6%
        of the frame differed between the same blast over water and over grass.

        The geometry is drawn with the plain deferred shader, not with this program, and that is
        the whole trick: position, normal, depth and object id are properties of the SHAPE, which
        is ordinary. Only the colour was ever custom.

        SET IT WHEN your custom shader draws something solid that occupies the space its mesh
        says it does. Leave it alone for anything translucent, and for anything that would be
        lying about where its surface is.
    */
    bool f_writes_gbuffer = false;

    Shader();
    Shader(const char* vert,const char* frag);
    ~Shader();

    void CreateComputeShader(const char* filename);

    /*
        The vertex+fragment build, as a method rather than only as a constructor.

        The two-argument constructor is now exactly this, which is what it has always done - the
        point of splitting it out is that a caller can set f_fatal_on_error (or anything else)
        BEFORE the compile happens. A constructor that compiles cannot offer that, and "construct
        it, then tell it not to die" is too late.

        Returns whether the program links. Safe to call again on the same Shader; Reload() is
        this plus the file-cache handling.
    */
    bool Build(const char* vert, const char* frag);
    //The same, for a compute program. CreateComputeShader is this with the result thrown away.
    bool BuildCompute(const char* comp);

    /*
        Rebuilds this program from its source files, in place.

        IN PLACE IS THE POINT. Nothing holding a Shader* goes stale, so there is no
        `renderer->custom_shaders.at(index) = new_one` dance to remember, no uniform_callback to
        re-bind, and no window where a tagged mesh points at a program that is about to be
        deleted. Every hand-rolled reload in this repo was that dance; they are now one call.

        It releases EVERY entry in source_files first, not just vname/fname. LoadFile answers from
        the BinaryAsset cache, so without that a "reload" recompiles the bytes read at start-up and
        produces an identical program - and an #included file (shaders/density.glsl) is the one
        most worth editing live. See ReleaseFile in core/File.h.

        FILE_RELEASE_EMBEDDED is an answer, not a failure: a packed build has no file behind the
        asset, so this reports "not available in this build" in compile_log and returns false
        rather than rebuilding identical bytes and claiming success.

        A RELOAD IS ALWAYS SOFT, whatever f_fatal_on_error says. That flag is about the STARTUP
        build, where a broken shader means there is nothing to run; a reload always has a working
        program to fall back on, and exiting the process over a typo is precisely what this exists
        to stop. On failure the old program stays bound and compile_log says why.

        RENDER THREAD ONLY - it is GL work. A tool or a keypress on another thread raises a flag;
        see Application::RequestShaderReload.
    */
    bool Reload();

    /*
        Every live Shader, one call, with the registry held for the duration.

        The constructor adds and the destructor removes, so this costs nothing to maintain and
        every shader in every app is findable without the app registering anything. It is what
        lets the core `shader_reload` MCP tool work in an app that knows nothing about it.

        `fn` runs with the registry locked, so it must not create or destroy a Shader. Reloading
        one is fine - that is what this is for.
    */
    static void ForEachShader(const std::function<void(Shader*)>& fn);

    int CompileVertex(char* data, size_t sz);
    int CompileFragment(char* data, size_t sz);
    int CompileCompute(char* data, size_t sz);
    int LinkProgram(int count, ...);
    void Use();

    /*
        Uniforms.

        Every setter returns whether the uniform was there. A missing one is a warning, never
        fatal - see the block above Shader::UniformLocation for why that is a deliberate choice
        and not laziness - so a caller that genuinely cannot proceed without a uniform checks the
        return itself, at a site that knows what the uniform means.

        The v-forms take a count of *elements*, not of floats.
    */
    bool Setint(const char* name, int value);
    bool Setbool(const char* name, bool value);
    bool Setfloat(const char* name, float value);
    bool Setvec2(const char* name, const vec2& value);
    bool Setvec3(const char* name, const vec3& value);
    bool Setvec4(const char* name, const vec4& value);
    bool Setmat3(const char* name, const fmat3& matrix);
    bool Setmat4(const char* name, const fmat4& matrix);

    bool Setintv(const char* name, const int* values, int count);
    bool Setfloatv(const char* name, const float* values, int count);
    bool Setvec2v(const char* name, const vec2* values, int count);
    bool Setvec3v(const char* name, const vec3* values, int count);
    bool Setvec4v(const char* name, const vec4* values, int count);

    std::function<void(void)> uniform_callback; //Called when renderer needs to set uniforms

private:
    int UniformLocation(const char* name);

    //Appends one build stage's info log to compile_log, tagged with which stage it came from, so
    //the whole story of a build is in one string rather than spread over four fields.
    void RecordLog(const char* stage, const char* log);

    //Names already reported missing, so a per-frame setter on a stripped uniform says so once
    //rather than sixty times a second.
    std::set<std::string> reported_missing;
};
#endif
