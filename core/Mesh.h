#ifndef _MESH_H_
#define _MESH_H_
class Mesh;
#include <vector>
#include "glad.h"
#include "stdint.h"
#include "type_vertex.h"
//Basic
#define ATTRIB_VERTEX   0
#define ATTRIB_NORMAL   1
#define ATTRIB_TANGENT  2
//UVs
#define ATTRIB_UVCOORD  3
#define ATTRIB_MATINDEX 4
/*
    THE INTEGER VERTEX FIELDS - matid, and bones in skinned_vertex - ARE NOT VERTEX ATTRIBUTES.

    They used to be: `in int matindex` at ATTRIB_MATINDEX, `in ivec3 bones` at ATTRIB_BONES, set
    up with glVertexArrayAttribIFormat. That is the textbook spelling and correct on the NVidia
    RTX A500. On the Intel Iris Xe (driver 32.0.101.7085, measured 2026-09-16 with a colour probe
    on the raw attribute, genuine int bits in the buffer) an integer attribute arrives as
    per-triangle garbage - at index 4 and at index 8 alike, with nothing else changed - while the
    four float attributes next to it in the same buffer arrive perfectly. So material_slot[matindex]
    read out of bounds and every object drew with material 0, and the bone indices were garbage
    too, which is why skinned characters were invisible there. GL debug output said nothing.

    So the shaders read these fields the way they already read everything else that has to be
    right on both vendors: from an SSBO. Mesh::RenderInstances binds the mesh's own VBO at
    SSBO_VERTEX_PULL as well, and default.vert / default_skinned.vert index it with gl_VertexID
    (draws are non-indexed, so gl_VertexID is the row) using the strides pinned by the
    static_asserts in Mesh.cpp. The VAO carries only the four float attributes. Verified correct
    on both GPUs the same day - the probe paints the same material mosaic on each.

    A float attribute carrying the id was tried and not cleanly evaluated: every such build ran
    while deferred.frag still had the sampler-array bug described there, which killed the frame
    whenever real material ids reached it, and that was mistaken for the attribute path failing.
    It may well work; the pull is kept because it does not depend on the answer.

    ATTRIB_MATINDEX stays what it was for the line VAO - the line colour, read by
    shaders/line.vert. Lines have a program of their own now rather than borrowing default.vert,
    whose material-index input used to read the colour word; that was never the Intel bug, but
    a program whose inputs match every VAO it is drawn with is one less undefined thing.
*/
#define SSBO_VERTEX_PULL 6
//Skinning
#define ATTRIB_BONES    5
#define ATTRIB_WEIGHTS  6
//More nonsense

#define MESHID_INVALID 0xFFFFFFFF

//Mode is set based on vertex type which the renderer may choose to render differently.
#define MESH_MODE_INVALID -1
#define MESH_MODE_NORMAL  0
#define MESH_MODE_SKINNED 1
#define MESH_MODE_LINE    2
#define MESH_MODE_SHADER  3

typedef uint32_t meshid_t;

class Mesh{
public:
    Mesh();
    bool InitVBOVAO();
    bool InitLineVBOVAO();
    bool InitSkinnedVBOVAO();

    bool InitSSBO();

    GLuint vbo = 0;    //Vertex Buffer
    GLuint vao = 0;    //Attribute Buffer
    GLuint ssbo = 0;   //Shader Storage used for Morph Targets

    void RenderInstances(int num_instances);
    void GenerateUniqueID();
    meshid_t GetID();

    void SetMeshData(vertex* verts, int vertex_count);

    //Rebuilds this mesh's VBO/VAO from the vertices it already holds - see the definition for
    //when that is needed and why nothing here calls it.
    void ReUploadMeshData();
    void SetLineMeshData(line_vertex* verts, int vertex_count);
    void SetSkinnedMeshData(skinned_vertex* verts, int vertex_count);
    void SetMorphMeshData(morph_vertex* verts, int vertex_count);

    const std::vector<vertex>& GetVertices() const {return vertices;};
    //In bind pose - the CPU copy SetSkinnedMeshData keeps. Where the skin actually is, which is
    //not where the bones are: a toe joint sits inside the shoe, not under it.
    const std::vector<skinned_vertex>& GetSkinnedVertices() const {return skinned_vertices;};

    bool IsNormalMesh();
    bool IsSkinnedMesh();
    bool IsLineMesh();

    vec3 GetExtents();

    /*
        Shared ownership, by hand-rolled count. Every holder - an Object drawing it, an Asset in the
        AssetManager, an app keeping a pointer to hand out later - takes one reference with Retain
        and gives it back with Release, and the last Release deletes the mesh.

        THE COUNT IS PRIVATE SO THAT THIS PAIR IS THE ONLY WAY TO MOVE IT. It used to be a public
        int, bumped with ++ in six apps and decremented in two places in Object that disagreed:
        DeleteMesh freed the mesh at zero and SetMesh did not, so replacing an Object's mesh leaked
        the old one. An app that wants to keep a generated mesh around should register it with
        AssetManager::AddNewAsset(name, mesh) rather than call Retain itself - that holds the
        reference AND makes the mesh findable by name, which is where step 2 of the asset work
        (colliders on assets) picks it up.

        Release returns true when it deleted the mesh, so a caller can drop its pointer; after that
        the pointer is dangling and must not be touched. No GL happens here - Mesh has no
        destructor of its own - so this is safe on the physics thread, which is where
        Scene::DeleteDestroyedObjects runs it.
    */
    void Retain();
    bool Release();
    int  GetNumReferences() const {return num_references;};

    uint32_t num_vertices = 0;
    int     num_materials = 0;
    int     num_morph_targets = 0;
    int     mesh_mode = MESH_MODE_INVALID;

    //Which of the renderer's custom shaders draws this mesh, for MESH_MODE_SHADER meshes only -
    //an index into Renderer::custom_shaders, as handed out by Renderer::AddCustomShader. Ignored
    //in every other mesh mode.
    //
    //-1 MEANS NOT ASSIGNED, AND IS THE DEFAULT ON PURPOSE. It used to default to 0, which saved
    //an app with exactly one custom shader from tagging its mesh - and cost every app with two,
    //because forgetting to tag a mesh then silently drew it with the FIRST shader instead. A
    //mesh drawn by the wrong shader looks like a shader bug and is hunted as one; a mesh that
    //draws nothing and says why in the log is found in a minute. Renderer::RenderUniqueMeshes
    //skips an untagged mesh and warns once.
    int custom_shader_index = -1;

    //So the warning above is one line and not one line per frame.
    bool f_warned_no_custom_shader = false;

    int32_t batch_index = -1;
    int32_t batch_num_instances = 0;

private:
    std::vector<vertex>vertices;
    std::vector<skinned_vertex>skinned_vertices;
    std::vector<line_vertex>line_vertices;
    std::vector<morph_vertex>morph_vertices;
    static meshid_t mesh_ids;   //Total amount of different meshes.
    meshid_t id = MESHID_INVALID;
    vec3    extents; //The size an AABB should be to encompass the mesh
    int     num_references = 0; //See Retain/Release.
};

#endif