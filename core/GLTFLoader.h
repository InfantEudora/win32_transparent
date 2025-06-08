#ifndef _GLTF_LOADER_H_
#define _GLTF_LOADER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include "File.h"
#include "Mesh.h"
#include "SkinnedMesh.h"
#include "type_int3.h"
#include "type_vec2.h"
#include "type_vec3.h"
#include "Material.h"
#include <string>
#include <vector>

#include "skeleton/Skeleton.h"
#include "AssetManager.h"
#include "ObjectAnimation.h"

#include "tinygltf/tiny_gltf.h"
/*
    Loader uses tinygltf to parse file. It can load a single file.
    List the Models (nodes) which can be loaded as a mesh.
*/

#define ANIM_TARGET_PATH_NONE                       -1
#define ANIM_TARGET_PATH_SCALE                      0
#define ANIM_TARGET_PATH_ROTATION                   1
#define ANIM_TARGET_PATH_TRANSLATION                2

#define ANIMSAMPLER_INTERPOLATION_TYPE_NONE         -1
#define ANIMSAMPLER_INTERPOLATION_TYPE_LINEAR       0
#define ANIMSAMPLER_INTERPOLATION_TYPE_STEP         1
#define ANIMSAMPLER_INTERPOLATION_TYPE_CUBICSPLINE  2

class GLTFLoader{
public:
    void                LoadGLTFFile(const char* filename);
    Mesh*               GetMeshFromNode(const char* node_name,std::vector<Material>*optional_mat_list_out = NULL); //This creates a mesh
    SkinnedMesh*        GetSkinnedMeshFromNode(const char* node_name,std::vector<Material>*optional_mat_list_out = NULL); //This creates a skinnedmesh
    Skeleton*           GetSkeleton(const char* skeleton_name, AssetManager* assetmanager, Skeleton* optional_target = NULL);
    Bone*               GetBone(int node_index, int& bone_count, std::vector<fmat4>&invbinmatrices, AssetManager* assetmanager = NULL);
    Animation*          LoadAnimation(const char* animation_name);

    //Info
    void                ListNodes();

    //Loaded node names from file
    std::vector<std::string>node_names;
private:
    int                     GetAccesorComponentTypeSize(const tinygltf::Accessor& accessor);
    int                     GetAccesorTypeSize(const tinygltf::Accessor& accessor);
    tinygltf::Node*         FindNode(std::string node_name);
    tinygltf::Skin*         FindSkin(std::string skin_name);
    tinygltf::Animation*    FindAnimation(std::string skin_name);
    void                    LoadInverseBindMatrices(std::vector<fmat4>& matrices, int accesor_index);

    int              GetIndex(const tinygltf::Accessor& index_accessor, int offset);
    float            Getfloat(unsigned char* data, int byte_offset);
    vec2             Getvec2(unsigned char* data, int byte_offset);
    vec3             Getvec3(unsigned char* data, int byte_offset);
    vec4             Getvec4(unsigned char* data, int byte_offset);
    fmat4            Getfmat4(unsigned char* data, int byte_offset);
    int3             Getint3_uint8_4(unsigned char* data, int byte_offset);

    vertex           GetVertex(tinygltf::BufferView* pb, tinygltf::BufferView* nb, tinygltf::BufferView* ub, int index);
    skinned_vertex   GetSkinnedVertex(tinygltf::BufferView* pb, tinygltf::BufferView* nb, tinygltf::BufferView* ub, tinygltf::BufferView* bb, tinygltf::BufferView* wb,int index);

    std::vector<Material> materials;    //Material list we load from a single file.

    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
};

#endif //_GLTF_LOADER_H_