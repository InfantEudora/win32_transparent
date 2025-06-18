#version 430 core

layout (location = 0) out vec4 dposition;
layout (location = 1) out vec4 dnormal;
//layout (location = 2) out vec4 dalbedo

//Passed from vertex shader.
layout (location = 0)  in vec3 vposition;       //Vertex position in world space, now fragment position in worldspace.
layout (location = 1)  in vec3 vnormal;         //Vertex normals
layout (location = 2)  in vec2 vuv;             //Texture UV coordinates

layout (location = 6)  flat in int vmatindex;   //Material index
layout (location = 7)  flat in int vobjid;      //ObjectID from vertex shader

layout (binding = 0) uniform sampler2D material_texture[16];   //Input texture

struct Material{
	vec4 color;
    int diffuse_texture;
    int normal_texture;
    float brightness;
    int pad3;
    //sampler2D handle_diffuse;
    //sampler2D handle_normal;
    uvec2 handle_diffuse;
    uvec2 handle_normal;
};

#define PI 	3.14159265359

float metallic = 0.5f;
float roughness = 0.5f;
uniform vec3 eye_position  = vec3(0.0,0.5,8.0);

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 3) buffer ReadbackBuffer{
	int data_in[4];
    int data_out[4];
    float fdata_out[4];
};

float GetTransparency(){
    Material m = materials[vmatindex];
    if (m.diffuse_texture >= 0){
        return texture(material_texture[m.diffuse_texture], vuv).w;
    }
    return m.color.w;
}

void main(){

    Material m = materials[vmatindex];
    vec3 albedo;
    if (m.diffuse_texture >= 0){
        albedo = texture(material_texture[m.diffuse_texture], vuv).xyz * m.color.xyz;
    }else{
        albedo = m.color.xyz;
    }
    float alpha = GetTransparency();
    dposition = vec4(vposition,alpha);
    dnormal = vec4(vnormal,1);
    data_out[0] = vobjid;
    float z = gl_FragCoord.z;
    fdata_out[0] = z;
}