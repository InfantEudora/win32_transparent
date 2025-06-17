#include "Mesh.h"

meshid_t Mesh::mesh_ids = 0;

Mesh::Mesh(){

}

void Mesh::GenerateUniqueID(){
    id = mesh_ids++;
}

meshid_t Mesh::GetID(){
    return id;
};

void Mesh::SetMeshData(vertex* verts, int vertex_count){
    //Copy the data in
    vertices.clear();
    for (int i=0;i<vertex_count;i++){
        vertices.push_back(verts[i]);
    }

    GenerateUniqueID();
    InitVBOVAO();
    glNamedBufferData(vbo, sizeof(vertex) * vertex_count, (float*)&vertices.at(0), GL_STATIC_DRAW);
    num_vertices = vertex_count;
}

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
    glNamedBufferData(ssbo, sizeof(morph_vertex) * vertex_count, (float*)&morph_vertices.at(0), GL_STATIC_DRAW);
}

bool Mesh::InitSSBO(){
    glCreateBuffers(1, (GLuint*)&ssbo);
    glNamedBufferData(ssbo, 0 , NULL, GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, ssbo);
    return true;
}

bool Mesh::InitVBOVAO(){
    glCreateBuffers(1, (GLuint*)&vbo);
    glCreateVertexArrays(1, (GLuint*)&vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(vertex));

    glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(vao,ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(vao,ATTRIB_TANGENT);
    glEnableVertexArrayAttrib(vao,ATTRIB_UVCOORD);
    glEnableVertexArrayAttrib(vao,ATTRIB_MATINDEX);

    glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, 6*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(float));
    glVertexArrayAttribIFormat(vao, ATTRIB_MATINDEX, 1, GL_INT, 11*sizeof(float));

    glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_TANGENT, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_UVCOORD, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_MATINDEX, 0);
    return true;
}

void Mesh::SetSkinnedMeshData(skinned_vertex* verts, int vertex_count){
    //Copy the data in
    skinned_vertices.clear();
    for (int i=0;i<vertex_count;i++){
        skinned_vertices.push_back(verts[i]);
    }

    GenerateUniqueID();
    InitSkinnedVBOVAO();
    glNamedBufferData(vbo, sizeof(skinned_vertex) * vertex_count, (float*)&skinned_vertices.at(0), GL_STATIC_DRAW);
    num_vertices = vertex_count;
}

bool Mesh::InitSkinnedVBOVAO(){
    glCreateBuffers(1, (GLuint*)&vbo);
    glCreateVertexArrays(1, (GLuint*)&vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(skinned_vertex));

    glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(vao,ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(vao,ATTRIB_TANGENT);
    glEnableVertexArrayAttrib(vao,ATTRIB_UVCOORD);
    glEnableVertexArrayAttrib(vao,ATTRIB_MATINDEX);
    glEnableVertexArrayAttrib(vao,ATTRIB_BONES);
    glEnableVertexArrayAttrib(vao,ATTRIB_WEIGHTS);


    glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, 6*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(float));
    glVertexArrayAttribIFormat(vao, ATTRIB_MATINDEX, 1, GL_INT, 11*sizeof(float));
    glVertexArrayAttribIFormat(vao, ATTRIB_BONES, 3, GL_INT, 11*sizeof(float) + 1*sizeof(int));
    glVertexArrayAttribFormat(vao, ATTRIB_WEIGHTS, 3, GL_FLOAT, GL_TRUE, 11*sizeof(float) + 4*sizeof(int));


    glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_TANGENT, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_UVCOORD, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_MATINDEX, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_BONES, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_WEIGHTS, 0);

    return true;
}

void Mesh::RenderInstances(int num_instances){
    glBindVertexArray(vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, num_vertices,num_instances);
}

bool Mesh::IsNormalMesh(){
    return (skinned_vertices.size() == 0);
}

bool Mesh::IsSkinnedMesh(){
    return (skinned_vertices.size() != 0);
}