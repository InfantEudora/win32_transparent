#include "Mesh.h"
#include <cstddef>      //offsetof, for the GLES attribute offsets below

/*
    WHY THIS FILE HAS TWO OF EVERY BUFFER SETUP.

    Desktop GL 4.5 has Direct State Access: glCreateBuffers/glNamedBufferData/
    glVertexArrayAttribFormat address an object by HANDLE, so a VAO can be configured without
    binding it. GLES has no DSA at any version up to 3.2 - not a subset of it, none of it - so the
    Android arm uses the classic bind-then-call API, where format and buffer are latched into
    whatever is bound at the time of the call.

    That is the whole difference. Both arms describe the same vertex layout and produce the same
    VAO; neither is a fallback or a simplification of the other.
*/
#if defined(__ANDROID__)
static void UploadBufferData(GLenum target, GLuint buffer, size_t size, const void* data, GLenum usage){
    glBindBuffer(target, buffer);
    glBufferData(target, size, data, usage);
}
#endif

/*
    THE TWO ARMS SPELL THE SAME LAYOUT TWO DIFFERENT WAYS, so they are tied together here.

    The desktop arm passes hand-computed offsets (11*sizeof(float) and friends) because
    glVertexArrayAttribFormat takes a plain relative offset; the GLES arm passes offsetof because
    glVertexAttribPointer wants a pointer-shaped one. Two independent descriptions of one struct
    is exactly how two platforms quietly start drawing different things - reorder a field in
    type_vertex.h and only one arm notices.

    So it fails the build instead. These cost nothing at runtime and are checked on both platforms,
    which means an Android-only build still catches a change that would break desktop.
*/
static_assert(offsetof(vertex, pos)             == 0*sizeof(float),                  "vertex.pos moved");
static_assert(offsetof(vertex, normal)          == 3*sizeof(float),                  "vertex.normal moved");
static_assert(offsetof(vertex, tangent)         == 6*sizeof(float),                  "vertex.tangent moved");
static_assert(offsetof(vertex, uv)              == 9*sizeof(float),                  "vertex.uv moved");
static_assert(offsetof(vertex, matid)           == 11*sizeof(float),                 "vertex.matid moved");

static_assert(offsetof(line_vertex, pos)        == 0*sizeof(float),                  "line_vertex.pos moved");
static_assert(offsetof(line_vertex, color)      == 3*sizeof(float),                  "line_vertex.color moved");

static_assert(offsetof(skinned_vertex, pos)     == 0*sizeof(float),                  "skinned_vertex.pos moved");
static_assert(offsetof(skinned_vertex, normal)  == 3*sizeof(float),                  "skinned_vertex.normal moved");
static_assert(offsetof(skinned_vertex, tangent) == 6*sizeof(float),                  "skinned_vertex.tangent moved");
static_assert(offsetof(skinned_vertex, uv)      == 9*sizeof(float),                  "skinned_vertex.uv moved");
static_assert(offsetof(skinned_vertex, matid)   == 11*sizeof(float),                 "skinned_vertex.matid moved");
static_assert(offsetof(skinned_vertex, bones)   == 11*sizeof(float) + 1*sizeof(int), "skinned_vertex.bones moved");
static_assert(offsetof(skinned_vertex, weights) == 11*sizeof(float) + 4*sizeof(int), "skinned_vertex.weights moved");

meshid_t Mesh::mesh_ids = 0;

Mesh::Mesh(){
}

void Mesh::GenerateUniqueID(){
    if (id == MESHID_INVALID)
        id = mesh_ids++;
}

meshid_t Mesh::GetID(){
    return id;
};

/*
    Rebuilds the VBO/VAO from the vertices this mesh already holds.

    NOTHING IN THIS TREE CALLS THIS, AND THAT IS EXPECTED. A Win32 GL context is never lost, so
    there is no way to reach it from here. It exists because GPU objects do not survive the EGL
    context that made them, and on Android that context is destroyed and recreated on something as
    ordinary as backgrounding the app - even with the orientation locked. Every VAO in the scene
    then belongs to a dead context and has to be made again.

    Kept in core rather than in the port so the two do not diverge over it, and so an app does not
    have to remember. See Renderer::ReUploadAllMeshes, which is what actually walks the scene.

    Only the plain `vertices` path. Line, skinned and morph meshes would each need the same
    treatment and do not have it yet. A no-op for a mesh with no CPU-side vertices, which is what
    makes it safe to call blindly over a whole tree.
*/
void Mesh::ReUploadMeshData(){
    if (vertices.size() > 0){
        //The old VBO/VAO died with the context, so these handles name nothing. Zeroing them is
        //what makes InitVBOVAO build a fresh pair instead of returning early on `vbo == 0`.
        vbo = 0;
        vao = 0;

        InitVBOVAO();
#if defined(__ANDROID__)
        UploadBufferData(GL_ARRAY_BUFFER, vbo, sizeof(vertex) * vertices.size(), (float*)&vertices.at(0), GL_STATIC_DRAW);
#else
        glNamedBufferData(vbo, sizeof(vertex) * vertices.size(), (float*)&vertices.at(0), GL_STATIC_DRAW);
#endif
    }
}

void Mesh::SetMeshData(vertex* verts, int vertex_count){
    vec3 fmin = {};
    vec3 fmax = {};
    if (vertex_count > 0){
        fmin = verts[0].pos;
        fmax = verts[0].pos;
    }

    //Copy the data in
    vertices.clear();
    for (int i=0;i<vertex_count;i++){
        vertices.push_back(verts[i]);
        if (verts[i].pos.x > fmax.x){
            fmax.x = verts[i].pos.x;
        }
        if (verts[i].pos.x < fmin.x){
            fmin.x = verts[i].pos.x;
        }
        if (verts[i].pos.y > fmax.y){
            fmax.y = verts[i].pos.y;
        }
        if (verts[i].pos.y < fmin.y){
            fmin.y = verts[i].pos.y;
        }
        if (verts[i].pos.z > fmax.z){
            fmax.z = verts[i].pos.z;
        }
        if (verts[i].pos.z < fmin.z){
            fmin.z = verts[i].pos.z;
        }
    }
    extents = fmax - fmin;

    GenerateUniqueID();
    InitVBOVAO();
#if defined(__ANDROID__)
    UploadBufferData(GL_ARRAY_BUFFER, vbo, sizeof(vertex) * vertex_count, (float*)&vertices.at(0), GL_STATIC_DRAW);
#else
    glNamedBufferData(vbo, sizeof(vertex) * vertex_count, (float*)&vertices.at(0), GL_STATIC_DRAW);
#endif
    num_vertices = vertex_count;
    mesh_mode = MESH_MODE_NORMAL;
}

void Mesh::SetLineMeshData(line_vertex* verts, int vertex_count){
    line_vertices.clear();
    for (int i=0;i<vertex_count;i++){
        line_vertices.push_back(verts[i]);
    }
    GenerateUniqueID();
    InitLineVBOVAO();
#if defined(__ANDROID__)
    UploadBufferData(GL_ARRAY_BUFFER, vbo, sizeof(line_vertex) * vertex_count, (float*)&line_vertices.at(0), GL_DYNAMIC_DRAW);
#else
    glNamedBufferData(vbo, sizeof(line_vertex) * vertex_count, (float*)&line_vertices.at(0), GL_DYNAMIC_DRAW);
#endif
    num_vertices = vertex_count;
    mesh_mode = MESH_MODE_LINE;
}

//When mesh is in normal mode, you can add morph meshes (meshes of the same size)
void Mesh::SetMorphMeshData(morph_vertex* verts, int vertex_count){

    //Copy the data in
    morph_vertices.clear();
    for (int i=0;i<vertex_count;i++){
        morph_vertices.push_back(verts[i]);
    }
    //These must be whole multiples
    num_morph_targets = vertex_count / num_vertices;

    //We'll store these in a Shader Storage Buffer since these can be a random amount.
    InitSSBO();
#if defined(__ANDROID__)
    //glBufferData, not glBufferStorage: GLES has no immutable buffer storage (glBufferStorage is
    //desktop GL 4.4, or EXT_buffer_storage on mobile, which is not guaranteed). The store ends up
    //mutable rather than immutable, which costs nothing here - this buffer is written once and
    //only ever read by the shader afterwards.
    UploadBufferData(GL_SHADER_STORAGE_BUFFER, ssbo, sizeof(morph_vertex) * vertex_count, (float*)&morph_vertices.at(0), GL_STATIC_DRAW);
#else
    glNamedBufferStorage(ssbo, sizeof(morph_vertex) * vertex_count, (float*)&morph_vertices.at(0),0);
#endif
}

#if defined(__ANDROID__)
bool Mesh::InitSSBO(){
    glGenBuffers(1, (GLuint*)&ssbo);
    return true;
}
#else
bool Mesh::InitSSBO(){
    glCreateBuffers(1, (GLuint*)&ssbo);
    return true;
}
#endif

#if defined(__ANDROID__)
//GLES equivalent of the DSA setup below: format and buffer are latched into the bound VAO by
//glVertexAttribPointer/glVertexAttribIPointer at call time, so vao and vbo both have to be bound
//first - unlike glVertexArrayAttribFormat, which addresses an unbound vao by handle.
bool Mesh::InitVBOVAO(){
    if (vbo == 0){
        glGenBuffers(1, (GLuint*)&vbo);
        glGenVertexArrays(1, (GLuint*)&vao);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(ATTRIB_VERTEX);
        glVertexAttribPointer(ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, pos));

        glEnableVertexAttribArray(ATTRIB_NORMAL);
        glVertexAttribPointer(ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE, sizeof(vertex), (void*)offsetof(vertex, normal));

        glEnableVertexAttribArray(ATTRIB_TANGENT);
        glVertexAttribPointer(ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, sizeof(vertex), (void*)offsetof(vertex, tangent));

        glEnableVertexAttribArray(ATTRIB_UVCOORD);
        glVertexAttribPointer(ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(vertex), (void*)offsetof(vertex, uv));

        /*
        AND THE INTEGER FIELDS AS ATTRIBUTES, which the desktop arm deliberately does not do.

        The SSBO vertex pull the #else arm relies on (SSBO_VERTEX_PULL, see Mesh.h) IS NOT
        AVAILABLE HERE. GLES 3.1 makes per-stage shader-storage-block limits implementation
        defined and lets them be zero, and on the test device they are: linking a vertex
        shader with one storage block fails with

        The number of vertex shader storage blocks (1) is greater than the maximum
        number allowed (0).

        So the whole frame is lost, not the field. GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0 is
        a conformant answer, so this is not a driver bug to wait out.

        glVertexAttribIPointer, not glVertexAttribPointer: matid is an int and must arrive as
        one. The float entry point would convert it, and the shader reads `in int`.
        */
        glEnableVertexAttribArray(ATTRIB_MATINDEX);
        glVertexAttribIPointer(ATTRIB_MATINDEX, 1, GL_INT, sizeof(vertex), (void*)offsetof(vertex, matid));


        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    return true;
}
#else
bool Mesh::InitVBOVAO(){
    if (vbo == 0){
        glCreateBuffers(1, (GLuint*)&vbo);
        glCreateVertexArrays(1, (GLuint*)&vao);
        glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(vertex));

        glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
        glEnableVertexArrayAttrib(vao,ATTRIB_NORMAL);
        glEnableVertexArrayAttrib(vao,ATTRIB_TANGENT);
        glEnableVertexArrayAttrib(vao,ATTRIB_UVCOORD);

        glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
        glVertexArrayAttribFormat(vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
        glVertexArrayAttribFormat(vao, ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, 6*sizeof(float));
        glVertexArrayAttribFormat(vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(float));
        //No matid attribute: it is pulled from the VBO as an SSBO, see SSBO_VERTEX_PULL in Mesh.h.

        glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
        glVertexArrayAttribBinding(vao, ATTRIB_NORMAL, 0);
        glVertexArrayAttribBinding(vao, ATTRIB_TANGENT, 0);
        glVertexArrayAttribBinding(vao, ATTRIB_UVCOORD, 0);
    }
    return true;
}
#endif

void Mesh::SetSkinnedMeshData(skinned_vertex* verts, int vertex_count){
    //Copy the data in
    skinned_vertices.clear();
    for (int i=0;i<vertex_count;i++){
        skinned_vertices.push_back(verts[i]);
    }

    GenerateUniqueID();
    InitSkinnedVBOVAO();
#if defined(__ANDROID__)
    UploadBufferData(GL_ARRAY_BUFFER, vbo, sizeof(skinned_vertex) * vertex_count, (float*)&skinned_vertices.at(0), GL_STATIC_DRAW);
#else
    glNamedBufferData(vbo, sizeof(skinned_vertex) * vertex_count, (float*)&skinned_vertices.at(0), GL_STATIC_DRAW);
#endif
    num_vertices = vertex_count;
    mesh_mode = MESH_MODE_SKINNED;
}

#if defined(__ANDROID__)
bool Mesh::InitLineVBOVAO(){
    if (vbo == 0){
        glGenBuffers(1, (GLuint*)&vbo);
        glGenVertexArrays(1, (GLuint*)&vao);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        glEnableVertexAttribArray(ATTRIB_VERTEX);
        glVertexAttribPointer(ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(line_vertex), (void*)offsetof(line_vertex, pos));

        glEnableVertexAttribArray(ATTRIB_MATINDEX);
        glVertexAttribIPointer(ATTRIB_MATINDEX, 1, GL_INT, sizeof(line_vertex), (void*)offsetof(line_vertex, color));

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
    return true;
}
#else
bool Mesh::InitLineVBOVAO(){
    if (vbo == 0){
        glCreateBuffers(1, (GLuint*)&vbo);
        glCreateVertexArrays(1, (GLuint*)&vao);
        glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(line_vertex));

        glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
        glEnableVertexArrayAttrib(vao,ATTRIB_MATINDEX);

        glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
        glVertexArrayAttribIFormat(vao, ATTRIB_MATINDEX, 1, GL_INT, 3*sizeof(float));

        glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
        glVertexArrayAttribBinding(vao, ATTRIB_MATINDEX, 0);
    }
    return true;
}

#endif

#if defined(__ANDROID__)
bool Mesh::InitSkinnedVBOVAO(){
    glGenBuffers(1, (GLuint*)&vbo);
    glGenVertexArrays(1, (GLuint*)&vao);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    glEnableVertexAttribArray(ATTRIB_VERTEX);
    glVertexAttribPointer(ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, pos));

    glEnableVertexAttribArray(ATTRIB_NORMAL);
    glVertexAttribPointer(ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, normal));

    glEnableVertexAttribArray(ATTRIB_TANGENT);
    glVertexAttribPointer(ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, tangent));

    glEnableVertexAttribArray(ATTRIB_UVCOORD);
    glVertexAttribPointer(ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, uv));

    /*
    AND THE INTEGER FIELDS AS ATTRIBUTES, which the desktop arm deliberately does not do.

    The SSBO vertex pull the #else arm relies on (SSBO_VERTEX_PULL, see Mesh.h) IS NOT
    AVAILABLE HERE. GLES 3.1 makes per-stage shader-storage-block limits implementation
    defined and lets them be zero, and on the test device they are: linking a vertex
    shader with one storage block fails with

    The number of vertex shader storage blocks (1) is greater than the maximum
    number allowed (0).

    So the whole frame is lost, not the field. GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0 is
    a conformant answer, so this is not a driver bug to wait out.

    glVertexAttribIPointer, not glVertexAttribPointer: matid is an int and must arrive as
    one. The float entry point would convert it, and the shader reads `in int`.
    */
    glEnableVertexAttribArray(ATTRIB_MATINDEX);
    glVertexAttribIPointer(ATTRIB_MATINDEX, 1, GL_INT, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, matid));

    /*
        Bone indices are the same case, and the desktop arm pulls them from the same SSBO.
        Without this a skinned mesh here poses every vertex against bone 0.

        THREE, NOT FOUR, and the count is load-bearing rather than a style choice.
        skinned_vertex::bones is an int3 and weights a vec3 - GetSkinnedVertex keeps three
        influences and drops the fourth on purpose. The static_asserts at the top of this file
        pin bones at word 12 and weights at word 15, so a 4-wide read here would take words
        12,13,14 AND weights.x, handing the shader a float bit-pattern as a bone index (a weight
        of 0.5 arrives as 1056964608). Harmless only while the shader declares ivec3; an
        out-of-range bone lookup the moment it declares ivec4 and touches .w.

        Going to four influences is a real option, but it is FOUR coordinated changes, not this
        number: int3 -> int4, vec3 -> vec4, GetSkinnedVertex keeping the fourth, and the word
        stride the desktop vertex-pull shaders hardcode (18) becoming 20.
    */
    glEnableVertexAttribArray(ATTRIB_BONES);
    glVertexAttribIPointer(ATTRIB_BONES, 3, GL_INT, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, bones));


    glEnableVertexAttribArray(ATTRIB_WEIGHTS);
    glVertexAttribPointer(ATTRIB_WEIGHTS, 3, GL_FLOAT, GL_TRUE, sizeof(skinned_vertex), (void*)offsetof(skinned_vertex, weights));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return true;
}
#else
bool Mesh::InitSkinnedVBOVAO(){
    glCreateBuffers(1, (GLuint*)&vbo);
    glCreateVertexArrays(1, (GLuint*)&vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(skinned_vertex));

    glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(vao,ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(vao,ATTRIB_TANGENT);
    glEnableVertexArrayAttrib(vao,ATTRIB_UVCOORD);
    glEnableVertexArrayAttrib(vao,ATTRIB_WEIGHTS);


    glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, 6*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(float));
    //No matid or bones attributes: both are pulled from the VBO as an SSBO, see Mesh.h.
    glVertexArrayAttribFormat(vao, ATTRIB_WEIGHTS, 3, GL_FLOAT, GL_TRUE, 11*sizeof(float) + 4*sizeof(int));


    glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_TANGENT, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_UVCOORD, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_WEIGHTS, 0);

    return true;
}
#endif

void Mesh::RenderInstances(int num_instances){
    glBindVertexArray(vao);
    //The vertex buffer a second time, as an SSBO: this is how the shaders read matid and the
    //bone indices, which Intel does not deliver correctly as vertex attributes - see Mesh.h.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, SSBO_VERTEX_PULL, vbo);
    if (ssbo > 0){
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo);
    }
    if (mesh_mode == MESH_MODE_LINE){
        glDrawArraysInstanced(GL_LINES, 0, num_vertices,num_instances);
    }else{
        glDrawArraysInstanced(GL_TRIANGLES, 0, num_vertices,num_instances);
    }
}

bool Mesh::IsNormalMesh(){
    return (mesh_mode == MESH_MODE_NORMAL);
}

bool Mesh::IsSkinnedMesh(){
    return (mesh_mode == MESH_MODE_SKINNED);
}

bool Mesh::IsLineMesh(){
    return (mesh_mode == MESH_MODE_LINE);
}

vec3 Mesh::GetExtents(){
    return extents;
}