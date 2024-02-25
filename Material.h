#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include <stdint.h>
#include <string>
#include "glad.h"
#include "type_vec4.h"

/*
    Material should be something the shaders can access and modify.
    Each mesh will have an material id associated per vertex.
    That should be the material id in a seperate material buffer.

    We might want to be able to paint different materials on a complex object.
    It's material indices should then be stored in a seperate VAO.

    First, we'll try to interleave them, so the storage is:
    vertex: {VVV NNN UU M}

*/
//GLSL really wants things to be padded to 16 bytes
typedef struct {
    vec4 color;
    int texture_unit;   //The OpenGL texture unit the material is bound to
    int pad[3];
    //Texture unit
    //Texture index within that unit
}material_t;

#endif
