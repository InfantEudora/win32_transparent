#version 430 core

//When rendering to multiple color targets
layout (location = 0) out vec4 color;

//gl_Position = fragment position from camera view
//in vec4 gl_FragCoord;  contains the window relative coordinate (x, y, z, 1/w)
    //The z component is the depth value that would be used for the fragment's depth if no shader contained any writes to gl_FragDepth.
//gl_FragDepth

//Passed from vertex shader.
layout (location = 0)  in vec3 vposition;       //Vertex position in world space, now fragment position in worldspace.
layout (location = 1)  in vec3 vnormal;         //Vertex normals
layout (location = 2)  in vec2 vuv;             //Texture UV coordinates
layout (location = 3)  flat in int vmatindex;   //Material index

//We force the two texture to be bound to GL_TEXTURE0+0
//It's set with glBindTextureUnit
layout (binding = 0) uniform sampler2D material_texture[2];   //Input texture

struct Material{
	vec4 color;
    int texture_unit;
    int pad1;
    int pad2;
    int pad3;
};

layout (std430, binding = 1) buffer MaterialBuffer{
	Material materials[];
};

void main(){
    Material m = materials[vmatindex];

    vec4 col = texture(material_texture[m.texture_unit], vuv);
    col *= m.color;

    color = col;
}