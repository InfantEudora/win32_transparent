#version 430 core

layout (location = 0) out vec4 dposition;
layout (location = 1) out vec4 dnormal;
//layout (location = 2) out vec4 dalbedo

//Passed from vertex shader.
layout (location = 0)  in vec3 vposition;       //Vertex position in world space, now fragment position in worldspace.
layout (location = 1)  in vec3 vnormal;         //Vertex normals
layout (location = 2)  in vec2 vuv;             //Texture UV coordinates
layout (location = 3)  flat in int vmatindex;   //Material index
layout (location = 4)  flat in int vobjid;      //ObjectID from vertex shader

layout (binding = 0) uniform sampler2D material_texture[2];   //Input texture

struct Material{
	vec4 color;
    int texture_unit;
    int pad1;
    int pad2;
    int pad3;
};

#define PI 	3.14159265359

float metallic = 0.5f;
float roughness = 0.5f;
uniform vec3 eye_position  = vec3(0.0,0.5,8.0);

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

layout (std430, binding = 2) buffer ReadbackBuffer{
	int data_in[4];
    int data_out[4];
    float fdata_out[4];
};

float GetTransparency(){
    Material m = materials[vmatindex];
    return texture(material_texture[m.texture_unit], vuv).w;
}

void main(){

    Material m = materials[vmatindex];
    vec3 albedo = texture(material_texture[m.texture_unit], vuv).xyz * m.color.xyz;
    float alpha = GetTransparency();
    dposition = vec4(vposition,alpha);
    dnormal = vec4(vnormal,1);
    data_out[0] = vobjid;
    float z = gl_FragCoord.z;
    fdata_out[0] = z;
}