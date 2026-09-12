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

    Shader();
    Shader(const char* vert,const char* frag);
    ~Shader();

    void CreateComputeShader(const char* filename);

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

    //Names already reported missing, so a per-frame setter on a stripped uniform says so once
    //rather than sixty times a second.
    std::set<std::string> reported_missing;
};
#endif
