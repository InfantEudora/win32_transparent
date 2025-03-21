#ifndef _GLTF_LOADER_H_
#define _GLTF_LOADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "File.h"
#include "Mesh.h"
#include "type_int3.h"
#include "type_vec2.h"
#include "type_vec3.h"
#include "Material.h"
#include <string>

#include "tinygltf/tiny_gltf.h"
/*
    Loader uses tinygltf to parse file. It can load a single file.
    List the Models (nodes) which can be loaded as a mesh.
*/
class GLTFLoader{
public:
    void    LoadGLTFFile(const char* filename);
    Mesh*   GetMeshFromNode(const char* node_name,std::vector<Material>*optional_mat_list_out = NULL); //This creates a mesh
private:
    tinygltf::Node*  FindNode(std::string node_name);

    int              GetIndex(const tinygltf::Accessor& index_accessor, int offset);
    vec2             Getvec2(unsigned char* data, int byte_offset);
    vec3             Getvec3(unsigned char* data, int byte_offset);

    vertex           GetVertex(tinygltf::BufferView* pb, tinygltf::BufferView* nb, tinygltf::BufferView* ub, int index);

    std::vector<Material> materials;    //Material list we load from a single file.

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
};

#endif //_GLTF_LOADER_H_