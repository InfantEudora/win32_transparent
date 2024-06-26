#ifndef _RENDERER_H_
#define _RENDERER_H_
class Renderer;
#include "Shader.h"
#include "glad.h"
#include "Object.h"
#include "Camera.h"
#include "Texture.h"
#include "Material.h"
#include "InputController.h"
#include "PerfTimer.h"

//This should have the same layout as in the shader
#define NUM_MATERIAL_SLOTS  4

typedef struct {
    fmat4 mat_transformscale;               // Matrix holding object rotation, scale and translation
    int material_slot[NUM_MATERIAL_SLOTS];  // We could do that each instance has a material assigned to a fixed number of slots
    int objectindex;                           // The Object's index it was rendered with this frame. (So not object->id)
    int pad1[3];
}instancedata_t;

typedef struct{
    int data_in[4];     //Stored pixel coordinates of mouse
    int data_out[4];    //Holds objid
    float fdata_out[4];    //Holds ztest
}readback_buffer_t;

//A callback for debugging
void opengl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, char const* message, void const* user_param);

#define PIPELINE_NONE       0
#define PIPELINE_MSAA       1
#define PIPELINE_DEFERRED   2


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
    void GetAllRenderableVisableSubObjects(Object* object,std::vector<Object*>&objects);
    void UpdateState();
    void RebuildUniqueMeshList();
    void ClearBatches();
    void FillBactches();
    void DrawObjects();

    void UpdateReadbackBuffer();
    void RenderUniqueMeshes();

    void RenderDebugLines();

    void DeferredPass(Camera* camera);
    void SSAOPass(Camera* camera);
    void DrawFrame(Camera* camera, Shader* shader, InputController* input);

    bool CheckFrameBuffer();
    bool Init();
    void SetState();
    void SetNumAASamples(int desired);
    bool InitFBO();
    bool InitDeferredFBO();

    bool InitSSBO();
    void ResolveAA();

    static void SetVSync(bool enable);

    void UploadMaterials();
    Material* GetMaterial(int index);
    int FindMaterialIndex(const char* name);
    int AddMaterial(Material& newmat);
    void AddMaterials(std::vector<Material>& list);
    int GetNumMaterials();

    Texture* LoadTexture(const char* filename);

    //We'll have one multisampled framebuffer with a single color and depth buffer.
    //And a resolve buffer, where the mutisampling is resolved to.
    GLuint msaa_fbo_id = -1; //Main FBO consisting of:
    GLuint color_rbo_id = -1; // Main color
    GLuint depth_rbo_id = -1; // Main depth

    GLuint resolve_fbo_id = -1;  //Resolve frame buffer
    GLuint resolve_tex_id = -1;  //Resolves into a texture
    //GLuint resolve_rbo_id = -1;  //Corresponding render buffer

    GLuint instdata_ssbo = -1;  //Shader Storage Buffer holding per-instance object data for each unique mesh
    GLuint materialdata_ssbo = -1;  //Shader Storage Buffer holding all different materials
    GLuint readback_ssbo = -1;  //Shader Storage Buffer for reading back data

    //Deferred stuff: Non-MSAA?
    GLuint deferred_fbo_id = -1; //Deferred FBO consisting of:
    GLuint deferred_depth_tex_id = -1; // Main depth buffer
    GLuint deferred_position_tex_id = -1; // Position of objects
    GLuint deferred_normal_tex_id = -1; // Normals of objects
    GLuint ssao_tex_id = -1; // SSAO output texture

    Shader* deferred_shader = NULL;     //Shader that outputs data to textures
    Shader* ssao_compute_shader = NULL;

    //Settings
    int aa_samples = 1;
    int pipeline = PIPELINE_MSAA;   //Which pipeline to initialise
    bool f_normal_mapping = true;   //Enable/disable normal mapping
    bool f_backface_culling = true;   //Enable/disable normal mapping

    //Counters/Timers
    PerfTimer* tmr_frame = NULL;

    //These will differ per frame
    std::vector<Mesh*> unique_meshes;               // An array of unique meshes
    std::vector<Object*>renderable_objects;         // All objects we will render this frame
    std::vector<std::vector<objectid_t>*>batch_ids; // An array of arrays containing the object id's per unique mesh, these form batches
    std::vector<instancedata_t>instancedata;        // Object data per unique mesh instance
    std::vector<material_t>glsl_materials;          // List of all materials for direct upload to SSBO
    std::vector<Material>materials;                 // List of all materials
    std::vector<Texture*>textures;                   // List of all textures

    std::vector<line>debug_lines;

    readback_buffer_t readbackbuffer;               //A single buffer for reading back data from shader

    std::vector<Object*>objects;                    //All known objects
};


#endif