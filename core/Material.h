#ifndef _MATERIAL_H_
#define _MATERIAL_H_

#include <stdint.h>
#include <string>
#include "glad.h"
#include "type_vec4.h"
#include "Texture.h"
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
//material_t matches layout in shader
typedef struct {
    vec4 color = {0,1,1,1};
    int diffuse_texture = -1;       // The OpenGL texture unit the material is bound to
    int normal_texture = -1;       // The OpenGL texture unit the material is bound to
    int pad[2];
    uint64_t handle_diffuse = 0; // The texture handle for OpenGL Bindless Textures
    uint64_t handle_normal = 0; // The texture handle for OpenGL Bindless Textures
}material_t;

//We want to know more about the material that GLSL
typedef struct{
    material_t glsl_material;
    std::string name;
    Texture* diff_texture = NULL;   //Optional diffuse texture
    Texture* norm_texture = NULL;   //Optional normal map texture
}Material;

#endif
