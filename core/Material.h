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
//A named struct rather than typedef struct{...}material_t; - the default member initialisers
//below make it non-C-compatible, and only a C-compatible type may take its linkage name from
//a typedef. The layout is identical either way; the old spelling drew -Wnon-c-typedef-for-linkage.
struct material_t {
    vec4 color = {0,1,1,1};
    int diffuse_texture = -1;       // The OpenGL texture unit the material is bound to. 0 to 32 typically.
    int normal_texture = -1;        // The OpenGL texture unit the material is bound to
    float brightness = 1;           // Values above 1 automatically make the object emissive. (Maybe: Seperate into a seperate flag.)
    /*
        METALLIC COSTS YOU THE DIFFUSE TERM AND PAYS YOU BACK IN REFLECTIONS, SO A SCENE WITH
        NOTHING TO REFLECT CANNOT AFFORD IT. With f_render_skybox false and
        f_environment_reflections off there is no environment, so a high metallic gives up its
        diffuse and gets nothing in return: Breakout's tough bricks at 0.92 rendered almost black,
        and read correctly at 0.45 with a little emissive. That is what metallic means and not a
        bug, but the failure looks exactly like a material that failed to load, so it is worth
        knowing before spending an evening on it.
    */
    float metallic = 0.8;
    float roughness = 0.5;
    /*
        Unlit: the surface IS this colour. No lights, no ambient, no reflections, no shadow - the
        albedo (texture if there is one, else `color`) goes to the screen as it is, plus whatever
        `emissive` adds. What a HUD element, a marker or a stylised game actually wants, and which
        before this could only be faked by making something emissive and hoping nothing lit it.

        Not the same as brightness or emissive. `brightness` scales the light a surface REFLECTS,
        so it does nothing in the dark; `emissive` ADDS light on top of whatever the lighting did,
        so the lit result is still in there. This removes the lighting entirely.

        An int rather than a bool because this struct is mirrored in GLSL, where bool is not a
        layout-compatible type. It lives in what used to be pad[0], so unlike `emissive` below it
        cost no space and moved no offset - which is why this one did not have to go at the end.
    */
    int f_unlit = 0;
    int pad[2];
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
};

//We want to know more about the material than GLSL
struct Material {
    material_t glsl_material;
    std::string name;
    Texture* diff_texture = NULL;   //Optional diffuse texture
    Texture* norm_texture = NULL;   //Optional normal map texture
};

#endif
