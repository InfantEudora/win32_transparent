#include "Mesh.h"

// Vertex data for a unit cube centered about the origin.
// Each face contains 2 triangles.
Vertex g_cube[36] = {
    // Positive Z.
    { 0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f},
    {-0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f},
    {-0.5f,  0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 0.0f},
    { 0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   0.0f, 1.0f},
    {-0.5f, -0.5f,  0.5f,   0.0f, 0.0f, 1.0f,   1.0f, 1.0f},

    // Negative Z.
    {-0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   0.0f, 0.0f},
    {-0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   1.0f, 0.0f},
    { 0.5f,  0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   1.0f, 0.0f},
    {-0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,   0.0f, 0.0f, -1.0f,   1.0f, 1.0f},

    // Positive Y.
    {-0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f, -0.5f,   0.0f, 1.0f, 0.0f,   1.0f, 1.0f},

    // Negative Y.
    {-0.5f, -0.5f, -0.5f,   0.0f, -1.0f, 0.0f,   0.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   0.0f, -1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f, -0.5f,   0.0f, -1.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,   0.0f, -1.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   0.0f, -1.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f,  0.5f,   0.0f, -1.0f, 0.0f,   1.0f, 1.0f},

    // Positive X.
    { 0.5f,  0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f,  0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    { 0.5f, -0.5f, -0.5f,   1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    { 0.5f, -0.5f,  0.5f,   1.0f, 0.0f, 0.0f,   1.0f, 1.0f},

    // Negative X.
    {-0.5f,  0.5f,  0.5f,   -1.0f, 0.0f, 0.0f,   0.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   -1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    {-0.5f,  0.5f, -0.5f,   -1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f,  0.5f, -0.5f,   -1.0f, 0.0f, 0.0f,   1.0f, 0.0f},
    {-0.5f, -0.5f,  0.5f,   -1.0f, 0.0f, 0.0f,   0.0f, 1.0f},
    {-0.5f, -0.5f, -0.5f,   -1.0f, 0.0f, 0.0f,   1.0f, 1.0f}
};

meshid_t Mesh::mesh_ids = 0;

Mesh::Mesh(){
    GenerateUniqueID();
    InitVBOVAO();
}

void Mesh::GenerateUniqueID(){
    id = mesh_ids++;
}

meshid_t Mesh::GetID(){
    return id;
};

bool Mesh::InitVBOVAO(){
    glCreateBuffers(1, (GLuint*)&vbo);
    //glNamedBufferStorage(vbo, sizeof(g_cube), (float*)g_cube, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferData(vbo, sizeof(g_cube), (float*)g_cube, GL_STATIC_DRAW);
    glCreateVertexArrays(1, (GLuint*)&vao);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    return true;
}

void Mesh::RenderInstances(int num_instances){
    int num_vertices = 36;

    glEnableVertexArrayAttrib(vao,ATTRIB_VERTEX);
    glEnableVertexArrayAttrib(vao,ATTRIB_NORMAL);
    glEnableVertexArrayAttrib(vao,ATTRIB_UVCOORD);

    glVertexArrayAttribFormat(vao, ATTRIB_VERTEX, 3, GL_FLOAT, GL_FALSE, 0*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_NORMAL, 3, GL_FLOAT, GL_TRUE , 3*sizeof(float));
    glVertexArrayAttribFormat(vao, ATTRIB_UVCOORD, 2, GL_FLOAT, GL_FALSE, 6*sizeof(float));

    glVertexArrayAttribBinding(vao, ATTRIB_VERTEX, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_NORMAL, 0);
    glVertexArrayAttribBinding(vao, ATTRIB_UVCOORD, 0);

    glBindVertexArray(vao);

    glDrawArraysInstanced(GL_TRIANGLES, 0, num_vertices,num_instances);
}