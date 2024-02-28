#ifndef _SHADER_H_
#define _SHADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

#include "type_fmat3.h"
#include "type_fmat4.h"

class Shader{
public:
    int progid = -1;        // ID of the compiled shader program.

    //Names for the currently loaded shader files.
    std::string vname; //Vertex shader name
    std::string fname; //Fragment

    bool f_compiled = false;

    Shader();
    Shader(const char* vert,const char* frag);
    ~Shader();

    void CreateComputeShader(const char* comp);

    int CompileVertex(char* data, size_t sz);
    int CompileFragment(char* data, size_t sz);
    int CompileCompute(char* data, size_t sz);
    int LinkProgram(int count, ...);
    void Use();

    //Uniforms

    void Setint(const char* name, int value);
    void Setvec3(const char* name, const vec3& value);
    void Setmat3(const char* name, const fmat3& matrix);
    void Setmat4(const char* name, const fmat4& matrix);

};

#endif
