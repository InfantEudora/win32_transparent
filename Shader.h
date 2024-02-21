#ifndef _SHADER_H_
#define _SHADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <iostream>

#include "type_fmat3.h"

class Shader{
private:
    static char* LoadFile(const char* filename);
public:
    int progid = -1;        //ID of the compiled shader program.
    int vertid = -1;             //ID of the vertex shader if it exists.
    int fragid = -1;             //IF of the fragment shader

    //Names for the currently loaded shader files.
    std::string vname; //Vertex shader name
    std::string fname; //Fragment

    bool f_compiled = false;

    Shader(const char* vert,const char* frag);
    ~Shader();

    int CompileVertex(char* data);
    int CompileFragment(char* data);
    int LinkProgram();
    void Use();

    //Uniforms
    void Setmat3(const char* name, const fmat3& matrix);

};

#endif
