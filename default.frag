#version 430 core

//When rendering to multiple color targets
layout (location = 0) out vec4 color;

//gl_Position = fragment position from camera view
//in vec4 gl_FragCoord;  contains the window relative coordinate (x, y, z, 1/w)
    //The z component is the depth value that would be used for the fragment's depth if no shader contained any writes to gl_FragDepth.
//gl_FragDepth

//Passed from vertex shader.
layout (location = 0)  in vec3 vposition;      //Vertex position in world space, now fragment position in worldspace.
layout (location = 1)  in vec3 vnormal;        //Vertex normals
layout (location = 2)  in vec2 vuv;            //Texture UV coordinates

uniform sampler2D  albedo_texture;   //Input texture

void main(){
    vec4 col = texture(albedo_texture, vuv);
    color = col;
}