#ifndef _OBJLOADER_H_
#define _OBJLOADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "File.h"
#include "Mesh.h"
#include "type_int3.h"
#include "type_vec2.h"
#include "type_vec3.h"
/*
    Will load mesh data from .obj files
*/

class OBJLoader{
public:
    static Mesh* ParseOBJFile(const char* filename);
    static Mesh* ParseOBJFileData(uint8_t* data, size_t size);

    bool ParseOBJLine(const char* line);
private:
    Mesh* BuildMesh();
    std::vector<vec3>face_vertexlist;
    std::vector<vec3>face_normallist;
    std::vector<vec2>face_uvlist;

    std::vector<int3>face_vertexindexlist;
    std::vector<int3>face_normalindexlist;
    std::vector<int3>face_uvindexlist;
};


#endif