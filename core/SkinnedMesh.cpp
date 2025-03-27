#include "SkinnedMesh.h"

meshid_t SkinnedMesh::mesh_ids = 0;

SkinnedMesh::SkinnedMesh(){

}

void SkinnedMesh::GenerateUniqueID(){
    id = mesh_ids++;
}

meshid_t SkinnedMesh::GetID(){
    return id;
};

void SkinnedMesh::SetMeshData(skinned_vertex* verts, int vertex_count){
    //Copy the data in
    vertices.clear();
    for (int i=0;i<vertex_count;i++){
        vertices.push_back(verts[i]);
    }

    GenerateUniqueID();
    InitVBOVAO();
    glNamedBufferData(vbo, sizeof(skinned_vertex) * vertex_count, (float*)&vertices.at(0), GL_STATIC_DRAW);
    num_vertices = vertex_count;
}

bool SkinnedMesh::InitVBOVAO(){
    glCreateBuffers(1, (GLuint*)&vbo);
    glCreateVertexArrays(1, (GLuint*)&vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(skinned_vertex));

    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_TANGENT);
    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_UVCOORD);
    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_BONES);
    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_WEIGHTS);
    glEnableVertexArrayAttrib(vao,SKINNED_ATTRIB_MATINDEX);

    glVertexArrayAttribFormat(vao, SKINNED_ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(vao, SKINNED_ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(vao, SKINNED_ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, 6*sizeof(float));
    glVertexArrayAttribFormat(vao, SKINNED_ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 9*sizeof(float));
    glVertexArrayAttribIFormat(vao, SKINNED_ATTRIB_BONES, 3, GL_INT, 11*sizeof(int));
    glVertexArrayAttribFormat(vao, SKINNED_ATTRIB_WEIGHTS, 3, GL_FLOAT, GL_TRUE, 14*sizeof(float));
    glVertexArrayAttribIFormat(vao, SKINNED_ATTRIB_MATINDEX, 1, GL_INT, 17*sizeof(int));

    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_TANGENT, 0);
    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_UVCOORD, 0);
    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_BONES, 0);
    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_WEIGHTS, 0);
    glVertexArrayAttribBinding(vao, SKINNED_ATTRIB_MATINDEX, 0);
    return true;
}

void SkinnedMesh::RenderInstances(int num_instances){
    glBindVertexArray(vao);

    glDrawArraysInstanced(GL_TRIANGLES, 0, num_vertices,num_instances);
}