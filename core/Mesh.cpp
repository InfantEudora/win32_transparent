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

//TODO: This might now be broken because materials.
/*
void Mesh::LoadUnitCube(){
    GenerateUniqueID();
    InitVBOVAO();
    glNamedBufferData(vbo, sizeof(g_cube), (float*)g_cube, GL_STATIC_DRAW);
    num_vertices = 36;
}*/

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

bool Mesh::InitVBOVAO(){
    glCreateBuffers(1, (GLuint*)&vbo);
    //glNamedBufferStorage(vbo, sizeof(g_cube), (float*)g_cube, GL_DYNAMIC_STORAGE_BIT);
    glCreateVertexArrays(1, (GLuint*)&vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(vertex));

    glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(vao,ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(vao,ATTRIB_UVCOORD);
    glEnableVertexArrayAttrib(vao,ATTRIB_TANGENT);
    glEnableVertexArrayAttrib(vao,ATTRIB_MATINDEX);

    glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_TANGENT, 3, GL_FLOAT, GL_TRUE, 8*sizeof(float));
    glVertexArrayAttribIFormat(vao, ATTRIB_MATINDEX, 1, GL_INT, 11*sizeof(float));

    glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_UVCOORD, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_TANGENT, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_MATINDEX, 0);

    return true;
}

void Mesh::RenderInstances(int num_instances){
    glBindVertexArray(vao);
    glDrawArraysInstanced(GL_TRIANGLES, 0, num_vertices,num_instances);
}