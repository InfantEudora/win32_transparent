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
#include "Material.h"
/*
    Will load mesh data from .obj files, and optional materials from .mat file.
    Can only parse triangle faces.

    Mesh will have materials mapped to the indices within the .mat file.
    ie. 0,1,2,3 if there are 4 materials.

    But, some of these materials might be the same as already loaded materials.
    We can have materials be unique by name, index, or match on all the material properties.
*/
class OBJLoader{
public:
    static Mesh* ParseOBJFile(const char* filename);
    static Mesh* ParseOBJFileData(uint8_t* data, size_t size, const char* filename);
    bool ParseOBJLine(const char* line);

    void ParseOBJMatFile(const char* filename);
    void ParseOBJMatFileData(uint8_t* data, size_t size);
    bool ParseOBJMatLine(const char* line);

    Material* SwitchMaterialByName(const char* name);
private:
    Mesh* BuildMesh();
    std::vector<vec3>face_vertexlist;
    std::vector<vec3>face_normallist;
    std::vector<vec2>face_uvlist;
    std::vector<int32_t>face_matlist;   //Each face has the same material

    std::vector<int3>face_vertexindexlist;
    std::vector<int3>face_normalindexlist;
    std::vector<int3>face_uvindexlist;

    std::vector<Material>materials;
    int current_material_index = 0;    // Index in materials. 0 is valid, set when there are no materials with .obj file.
    Material* current_material = NULL;  // Material we are currently loading from file, or the one we are using while reading .obj data.
};

#endif