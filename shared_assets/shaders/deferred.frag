#version 430 core

layout (location = 0) out vec4 dposition;
layout (location = 1) out vec4 dnormal;
layout (location = 2) out int dobjectid;
//layout (location = 2) out vec4 dalbedo

//Passed from vertex shader.
layout (location = 0)  in vec3 vposition;       //Vertex position in world space, now fragment position in worldspace.
layout (location = 1)  in vec3 vnormal;         //Vertex normals
layout (location = 2)  in vec2 vuv;             //Texture UV coordinates

layout (location = 6)  flat in int vmatindex;   //Material index
layout (location = 7)  flat in int vobjid;      //ObjectID from vertex shader

/*
    Same array, same binding, same size as default.frag: unit 0 is the shadow map, materials get
    units 4 and up from Renderer::UploadMaterials, and an index into this array IS a texture unit.
    This used to be `layout (binding = 1) ... material_texture[15]`, which made index 4 unit 5 -
    every lookup in this pass was one unit off, and nobody saw it because this pass only produces
    an alpha for the G-buffer.
*/
layout (binding = 0) uniform sampler2D material_texture[24];

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    float brightness;
    float metallic;
    float roughness;
    int f_unlit;       //see material_t in core/Material.h; was pad2
    int pad3;
    int pad4;
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
    //Self-emitted light: xyz is glTF's emissiveFactor, w a strength multiplier. Must stay last
    //to match material_t in Material.h - see the comment there.
    vec4 emissive;
};

#define PI 	3.14159265359

//uniform vec3 eye_position  = vec3(0.0,0.5,8.0);

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 3) buffer ReadbackBuffer{
	int data_in[4];
    int data_out[4];
    float fdata_out[4];
};

/*
    THE ONLY WAY A MATERIAL'S TEXTURE MAY BE SAMPLED IN THIS SHADER - and the reason the Intel
    Iris Xe drew nothing at all for a day.

    material_texture[] is an array of samplers, and GLSL only defines indexing one with a value
    that is the same for every fragment of the draw call. m.diffuse_texture is not that: it comes
    from the per-vertex material id, so it changes from triangle to triangle inside one object.
    `texture(material_texture[m.diffuse_texture], vuv)` was therefore undefined, and on the
    Intel Iris Xe (driver 32.0.101.7085, measured 2026-09-16) the undefined behaviour is that
    the FRAME dies: nothing draws for the rest of the process, ImGui included, glGetError stays 0
    and GL debug output says nothing. It only ever survived because the material id arrived as
    garbage there and every lookup collapsed to material 0, the same texture for everything -
    which was the visible Intel bug this pass was hiding behind. NVidia reads a sampler per lane
    and never noticed either way.

    Every case indexes with a LITERAL, which is defined however the index diverges. The
    derivatives come in as arguments, taken in main() where control flow is still uniform,
    because implicit-derivative texture() inside a divergent branch is a second undefined thing.
*/
vec4 SampleMaterialTexture(int unit, vec2 uv, vec2 dx, vec2 dy){
    switch (unit){
        case 1:  return textureGrad(material_texture[1],  uv, dx, dy);
        case 2:  return textureGrad(material_texture[2],  uv, dx, dy);
        case 3:  return textureGrad(material_texture[3],  uv, dx, dy);
        case 4:  return textureGrad(material_texture[4],  uv, dx, dy);
        case 5:  return textureGrad(material_texture[5],  uv, dx, dy);
        case 6:  return textureGrad(material_texture[6],  uv, dx, dy);
        case 7:  return textureGrad(material_texture[7],  uv, dx, dy);
        case 8:  return textureGrad(material_texture[8],  uv, dx, dy);
        case 9:  return textureGrad(material_texture[9],  uv, dx, dy);
        case 10: return textureGrad(material_texture[10], uv, dx, dy);
        case 11: return textureGrad(material_texture[11], uv, dx, dy);
        case 12: return textureGrad(material_texture[12], uv, dx, dy);
        case 13: return textureGrad(material_texture[13], uv, dx, dy);
        case 14: return textureGrad(material_texture[14], uv, dx, dy);
        case 15: return textureGrad(material_texture[15], uv, dx, dy);
        case 16: return textureGrad(material_texture[16], uv, dx, dy);
        case 17: return textureGrad(material_texture[17], uv, dx, dy);
        case 18: return textureGrad(material_texture[18], uv, dx, dy);
        case 19: return textureGrad(material_texture[19], uv, dx, dy);
        case 20: return textureGrad(material_texture[20], uv, dx, dy);
        case 21: return textureGrad(material_texture[21], uv, dx, dy);
        case 22: return textureGrad(material_texture[22], uv, dx, dy);
        case 23: return textureGrad(material_texture[23], uv, dx, dy);
        //A unit nothing was bound to: opaque, so a bad index cannot punch a hole in the G-buffer.
        default: return vec4(1.0);
    }
}

void main(){
    //Derivatives first, while every fragment is still executing the same code.
    vec2 uv_dx = dFdx(vuv);
    vec2 uv_dy = dFdy(vuv);

    //vmatindex can be -1 for "no material"; the forward pass paints that magenta, here it is
    //simply opaque. Clamped rather than branched so the struct read is not inside a divergent
    //branch either.
    Material m = materials[max(vmatindex,0)];
    float alpha = m.color.w;
    if (m.diffuse_texture >= 0){
        alpha = SampleMaterialTexture(m.diffuse_texture, vuv, uv_dx, uv_dy).w;
    }
    dposition = vec4(vposition,alpha);
    dnormal = vec4(vnormal,1);
    dobjectid = vobjid;
    //data_out[0] = vobjid;
    float z = gl_FragCoord.z;
    //fdata_out[0] = z;
}
