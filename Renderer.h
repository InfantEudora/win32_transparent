#ifndef _RENDERER_H_
#define _RENDERER_H_
class Renderer;
#include "Shader.h"
#include "glad.h"
#include "Object.h"
#include "Camera.h"

//This should have the same layout as in the shader
typedef struct {
    fmat4 mat_transformscale; //Matrix holding object rotation, scale and translation
    vec3 position;
}InstanceData;

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

    void CullObjects();
    void RebuildUniqueMeshList();
    void ClearBatches();
    void FillBactches();
    void DrawObjects();
    void BuildSSBO();
    void RenderUniqueMeshes();

    void DrawFrame(Shader* shader);

    bool CheckFrameBuffer();
    bool Init();
    bool InitFBO();
    bool InitSSBO();
    void ResolveAA();

    //We'll have one multisampled framebuffer with a single color and depth buffer.
    //And a resolve buffer, where the mutisampling is resolved to.
    GLuint msaa_fbo_id = -1; //Main FBO consisting of:
    GLuint color_rbo_id = -1; // Main color
    GLuint depth_rbo_id = -1; // Main depth

    GLuint resolve_fbo_id = -1;  //Resolve frame buffer
    GLuint resolve_rbo_id = -1;  //Corresponding render buffer

    GLuint instdata_ssbo = -1;  //Shader Storage Buffer holding per-instance object data for each unique mesh

    UINT texture_id = -1;

    //These will differ per frame
    std::vector<Mesh*> unique_meshes;               // An array of unique meshes
    std::vector<Object*>renderable_objects;         // All objects we will render this frame
    std::vector<std::vector<objectid_t>*>batch_ids; // An array of arrays containing the object id's per unique mesh, these form batches
    std::vector<InstanceData>instancedata;          // Object data per unique mesh instance

    std::vector<Object*>objects;                    //All known objects


    //Our example cube
    void InitCheckerPatternTexture();
    Object* cube = NULL;
    Camera* camera = NULL;
};


#endif