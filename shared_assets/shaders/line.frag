#version 430 core
//Fragment stage for line meshes - see line.vert for why lines have their own program. Unlit
//by design: a debug line should be the colour it was given, wherever it is.
layout (location = 0) out vec4 color;

layout (location = 0) in vec4 vcolor;

void main(){
    color = vcolor;
}
