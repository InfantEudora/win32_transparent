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
    int diffuse_texture = -1;       // The OpenGL texture unit the material is bound to. 0 to 32 typically.
    int normal_texture = -1;        // The OpenGL texture unit the material is bound to
    float brightness = 1;           // Values above 1 automatically make the object emissive. (Maybe: Seperate into a seperate flag.)
    float metallic = 0.8;
    float roughness = 0.5;
    int pad[3];
    uint64_t handle_diffuse = 0;    // The texture handle for OpenGL Bindless Textures
    uint64_t handle_normal = 0;     // The texture handle for OpenGL Bindless Textures
    // Light the surface emits on its own, added to the lit result and unaffected by any light or
    // shadow. xyz is glTF's emissiveFactor (0..1), w a strength multiplier on top of it, which is
    // what actually lets something glow - emissiveFactor alone is clamped to 1 and so can never be
    // brighter than a fully lit white surface. Distinct from `brightness` above, which scales the
    // light a surface REFLECTS: crank that and an unlit object stays black.
    //
    // Appended at the end of the struct on purpose. This layout is repeated by hand in five
    // shaders (default/deferred/custom .frag, default/default_skinned .vert), and adding a field
    // here instead of filling pad[3] keeps every existing offset - including the two 8-byte
    // texture handles - exactly where it was. std430: vec4 at offset 64, struct grows 64 -> 80.
    vec4 emissive = {0,0,0,1};
}material_t;

//We want to know more about the material than GLSL
typedef struct{
    material_t glsl_material;
    std::string name;
    Texture* diff_texture = NULL;   //Optional diffuse texture
    Texture* norm_texture = NULL;   //Optional normal map texture
}Material;

#endif
