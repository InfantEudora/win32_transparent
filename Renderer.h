#ifndef _RENDERER_H_
#define _RENDERER_H_
class Renderer;
#include "Shader.h"
#include "glad.h"

struct Vertex{
    float pos[3];
    float normal[3];
    float texcoord[2];
};

/*
    A class responsible of managing the OpenGL state and pipeline.
    You should be able to make a renderer without a window,
     and that just renders a bunch of things to a buffer.
*/
class Renderer{
    public:
    Renderer(int w, int h);

    int width = 1;
    int height = 1;

    void DrawFrame(Shader* shader);
    bool CheckFrameBuffer();
    bool Init();
    bool InitFBO();
    bool InitVBO();
    void ResolveAA();

    //We'll have one multisampled framebuffer with a single color and depth buffer.
    //And a resolve buffer, where the mutisampling is resolved to.
    GLuint msaa_fbo_id = -1; //Main FBO consisting of:
    GLuint color_rbo_id = -1; // Main color
    GLuint depth_rbo_id = -1; // Main depth

    GLuint resolve_fbo_id = -1;  //Resolve frame buffer
    GLuint resolve_rbo_id = -1;  //Corresponding render buffer

    GLuint cube_vbo = -1;
    GLuint cube_vao = -1;
    UINT texture_id = -1;

    //Our example cube
    void InitCheckerPatternTexture();
    float rotation = 0;
};


#endif