#include "GLTFLoader.h"
#include "File.h"

#include "Debug.h"
static Debugger *debug = new Debugger("GLTFLoader", DEBUG_INFO);

void GLTFLoader::LoadGLTFFile(const char* input_filename){
    std::map<int, std::string> mode_strings;
    mode_strings[TINYGLTF_MODE_POINTS] = "TINYGLTF_MODE_POINTS";
    mode_strings[TINYGLTF_MODE_LINE] = "TINYGLTF_MODE_LINE";
    mode_strings[TINYGLTF_MODE_LINE_LOOP] = "TINYGLTF_MODE_LINE_LOOP";
    mode_strings[TINYGLTF_MODE_LINE_STRIP] = "TINYGLTF_MODE_LINE_STRIP";
    mode_strings[TINYGLTF_MODE_TRIANGLES] = "TINYGLTF_MODE_TRIANGLES";
    mode_strings[TINYGLTF_MODE_TRIANGLE_STRIP] = "TINYGLTF_MODE_TRIANGLE_STRIP";
    mode_strings[TINYGLTF_MODE_TRIANGLE_FAN] = "TINYGLTF_MODE_TRIANGLE_FAN";

    std::string err;
    std::string warn;

    size_t file_data_sz = 0;
    uint8_t* file_data = NULL;  // Data loaded from disk
    file_data = LoadFile(input_filename,&file_data_sz);

    bool ret = false;
    //ret = loader.LoadBinaryFromFile(&model, &err, &warn, input_filename.c_str());
    ret = loader.LoadBinaryFromMemory(&model,&err,&warn,file_data,file_data_sz);

    if (!warn.empty()) {
        debug->Warn("%s\n", warn.c_str());
    }
    if (!err.empty()) {
        debug->Err("%s\n", err.c_str());
    }
    if (!ret) {
        debug->Err("Failed to load .glTF : %s\n", input_filename);
    }else{
        debug->Ok("Loaded .glTF : %s\n", input_filename);
    }

    // Check file structure
    debug->Trace("Model has %i scenes\n",model.scenes.size());
    for (int scene_index=0;scene_index<model.scenes.size();scene_index++){
        debug->Trace("Model.scenes[%i].name : %s\n",scene_index, model.scenes[scene_index].name.c_str());
    }
    debug->Info("Model has %i nodes\n",model.nodes.size());
    node_names.clear();
    for (int node_index=0;node_index<model.nodes.size();node_index++){
        debug->Info("Model.nodes[%i].name     : %s\n",node_index, model.nodes[node_index].name.c_str());
        debug->Info("Model.nodes[%i].mesh     : %i\n",node_index, model.nodes[node_index].mesh);
        debug->Trace("Model.nodes[%i].children : %i\n",node_index, model.nodes[node_index].children.size());
        debug->Trace("Model.nodes[%i].weights  : %i\n",node_index, model.nodes[node_index].weights.size());

        for (int i=0;i<model.nodes[node_index].children.size();i++){
            int child_index = model.nodes[node_index].children.at(i);
            tinygltf::Node& child = model.nodes.at(child_index);
            debug->Trace(" - Child[%i] -> model.nodes[%i].name : %s\n",i,child_index, child.name.c_str());
        }
        node_names.push_back(model.nodes[node_index].name);
    }

    debug->Trace("Model has %i skins\n",model.skins.size());

    for (int skin_index=0;skin_index<model.skins.size();skin_index++){
        debug->Trace("Model.skins[%i].name                : %s\n",skin_index, model.skins[skin_index].name.c_str());
        debug->Trace("Model.skins[%i].inverseBindMatrices : in accessor [%i]\n",skin_index, model.skins[skin_index].inverseBindMatrices);
        debug->Trace("Model.skins[%i].skeleton            : %i\n",skin_index, model.skins[skin_index].skeleton);
        debug->Trace("Model.skins[%i].joints              : %i\n",skin_index, model.skins[skin_index].joints.size());
    }

    debug->Info("Model has %i meshes\n",model.meshes.size());
    for (int mesh_index=0;mesh_index<model.meshes.size();mesh_index++){
        debug->Info("Model.meshes[%i].name       : %s\n",mesh_index, model.meshes[mesh_index].name.c_str());
        debug->Info("Model.meshes[%i].primitives : %i\n",mesh_index, model.meshes[mesh_index].primitives.size());
        debug->Info("Model.meshes[%i].weights    : %i\n",mesh_index, model.meshes[mesh_index].weights.size());

        //Default weights per morph target
        for (int i=0;i<model.meshes[mesh_index].weights.size();i++){
            double weight = model.meshes[mesh_index].weights.at(i);
            debug->Trace("Model.meshes[%i].weights[%i] : %.3f\n",i, weight);
        }

        //List primitives
        tinygltf::Mesh& mesh = model.meshes[mesh_index];
        for (size_t i = 0; i < mesh.primitives.size(); i++){
            tinygltf::Primitive &primitive = mesh.primitives[i];
            debug->Info("Model.meshes[%i].primitive[%i].indices  : contained in accessor[%i]\n",mesh_index, i,primitive.indices);

            //If indices are -1 ... it could be points or lines
            if (primitive.indices < 0) {
                continue;
            }

            debug->Info("Model.meshes[%i].primitive[%i].mode     : %s(%i)\n",mesh_index, i, mode_strings[primitive.mode].c_str(), primitive.mode);
            debug->Info("Model.meshes[%i].primitive[%i].material : %i\n",mesh_index, i, primitive.material);

            std::map<std::string, int>::const_iterator it(primitive.attributes.begin());
            std::map<std::string, int>::const_iterator itEnd(primitive.attributes.end());

            int attrib_index = 0;
            for (; it != itEnd; it++) {
                debug->Info("Model.meshes[%i].primitive[%i].attributes[%i] : %s -> accessor: %i\n",mesh_index, i,attrib_index, it->first.c_str(),it->second);
                attrib_index++;

                const tinygltf::Accessor &accessor = model.accessors[it->second];
                int size = 1;
                if (accessor.type == TINYGLTF_TYPE_SCALAR) {
                    size = 1;
                } else if (accessor.type == TINYGLTF_TYPE_VEC2) {
                    size = 2;
                } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
                    size = 3;
                } else if (accessor.type == TINYGLTF_TYPE_VEC4) {
                    size = 4;
                } else {
                    debug->Fatal("Invalid accessor.type: %i\n",accessor.type);
                }
            }

            //List morph targets
            for (size_t mt_i = 0; mt_i < primitive.targets.size(); mt_i++){
                std::map<std::string, int>& morph_target = primitive.targets.at(mt_i);
                debug->Info("Model.meshes[%i].primitive[%i].targets[%i].size() = %i\n",mesh_index, i,mt_i,morph_target.size());

                std::map<std::string, int>::const_iterator it(morph_target.begin());
                std::map<std::string, int>::const_iterator itEnd(morph_target.end());

                int attrib_index = 0;
                for (; it != itEnd; it++) {
                    debug->Info("Model.meshes[%i].primitive[%i].targets[%i] : %s -> accessor: %i\n",mesh_index, i,mt_i, it->first.c_str(),it->second);
                    attrib_index++;

                    const tinygltf::Accessor &accessor = model.accessors[it->second];
                    int size = 1;
                    if (accessor.type == TINYGLTF_TYPE_SCALAR) {
                        size = 1;
                    } else if (accessor.type == TINYGLTF_TYPE_VEC2) {
                        size = 2;
                    } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
                        size = 3;
                    } else if (accessor.type == TINYGLTF_TYPE_VEC4) {
                        size = 4;
                    } else {
                        debug->Fatal("Invalid accessor.type: %i\n",accessor.type);
                    }
                }
            }
        }
    }
    debug->Info("Model has %i textures\n",model.textures.size());
    for (int texture_index=0;texture_index<model.textures.size();texture_index++){
        debug->Info("Model.textures[%i].name : %s\n",texture_index, model.textures[texture_index].name.c_str());
    }
    debug->Info("Model has %i images\n",model.images.size());
    for (int image_index=0;image_index<model.images.size();image_index++){
        debug->Info("Model.images[%i].name       : %s\n",image_index, model.images[image_index].name.c_str());
        debug->Trace("Model.images[%i].mime_type  : %s\n",image_index, model.images[image_index].mimeType.c_str());
        debug->Trace("Model.images[%i].bufferView : %i\n",image_index, model.images[image_index].bufferView);
        debug->Trace("Model.images[%i].uri        : %s\n",image_index, model.images[image_index].uri.c_str());
    }
    debug->Trace("Model has %i materials\n",model.materials.size());
    for (int material_index=0;material_index<model.materials.size();material_index++){
        debug->Trace("Model.materials[%i].name                       : %s\n",material_index, model.materials[material_index].name.c_str());
        debug->Trace("Model.materials[%i].pbr.baseColorTexture.index : %i\n",material_index, model.materials[material_index].pbrMetallicRoughness.baseColorTexture.index);
        debug->Trace("Model.materials[%i].pbr.baseColorFactor        : %.1f %.1f %.1f %.1f\n",material_index
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(0)
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(1)
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(2)
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(3));

    }
    debug->Trace("Model has %i buffers\n",model.buffers.size());
    for (int buffer_index=0;buffer_index<model.buffers.size();buffer_index++){
        debug->Trace("Model.buffers[%i].name : %s\n",buffer_index, model.buffers[buffer_index].name.c_str());
    }
    debug->Trace("Model has %i bufferViews\n",model.bufferViews.size());
    for (int bufferview_index=0;bufferview_index<model.bufferViews.size();bufferview_index++){
        debug->Trace("Model.bufferViews[%i].name       : %s\n",bufferview_index, model.bufferViews[bufferview_index].name.c_str());
        debug->Trace("Model.bufferViews[%i].buffer     : %i\n",bufferview_index, model.bufferViews[bufferview_index].buffer);
        debug->Trace("Model.bufferViews[%i].byteOffset : %i\n",bufferview_index, model.bufferViews[bufferview_index].byteOffset);
        debug->Trace("Model.bufferViews[%i].byteLength : %i\n",bufferview_index, model.bufferViews[bufferview_index].byteLength);
        debug->Trace("Model.bufferViews[%i].byteStride : %i\n",bufferview_index, model.bufferViews[bufferview_index].byteStride);
        debug->Trace("Model.bufferViews[%i].target     : %i\n",bufferview_index, model.bufferViews[bufferview_index].target);

        //Now list all the accessors the use this bufferView
        bool accessor_found = false;
        int sparse_accessor = -1;
        for (size_t a_i = 0; a_i < model.accessors.size(); ++a_i){
            tinygltf::Accessor& accessor = model.accessors[a_i];
            if (accessor.bufferView == bufferview_index){
                debug->Trace("Model.accessors[%i] uses this bufferView\n",a_i);
                accessor_found = true;
                if (accessor.sparse.isSparse){
                    debug->Warn("Accessor is sparse...\n");
                    sparse_accessor = a_i;
                }
                break;
            }
        }

        if (sparse_accessor > -1){

        }else{
            //Shit gets more complicated.
        }

        //Ie, could be image data or other data
        if (!accessor_found){
            debug->Trace("No accessors use this bufferView\n");
        }

    }

    debug->Trace("Model has %i accessors\n",model.accessors.size());
    for (int accessor_index=0;accessor_index<model.accessors.size();accessor_index++){
        debug->Trace("Model.accessors[%i].name          : %s\n",accessor_index, model.accessors[accessor_index].name.c_str());
        debug->Trace("Model.accessors[%i].bufferView    : %i\n",accessor_index, model.accessors[accessor_index].bufferView);
        debug->Trace("Model.accessors[%i].byteOffset    : %i\n",accessor_index, model.accessors[accessor_index].byteOffset);
        debug->Trace("Model.accessors[%i].componentType : %i\n",accessor_index, model.accessors[accessor_index].componentType);
        debug->Trace("Model.accessors[%i].count         : %i\n",accessor_index, model.accessors[accessor_index].count);
    }

    debug->Info("Model has %i animations\n",model.animations.size());
    for (int animation_index=0;animation_index<model.animations.size();animation_index++){
        debug->Info("Model.animations[%i].name         : %s\n",animation_index, model.animations[animation_index].name.c_str());
        debug->Info("Model.channels[%i].channels.size  : %i\n",animation_index, model.animations[animation_index].channels.size());
        debug->Info("Model.samplers[%i].samplers.size  : %i\n",animation_index, model.animations[animation_index].samplers.size());
    }

    debug->Trace("More info!!!\n");
}

void GLTFLoader::ListNodes(){
    debug->Info("Model has %i nodes\n",model.nodes.size());

    for (int node_index=0;node_index<model.nodes.size();node_index++){
        debug->Info("Model.nodes[%i].name     : %s\n",node_index, model.nodes[node_index].name.c_str());
        debug->Info("Model.nodes[%i].mesh     : %i\n",node_index, model.nodes[node_index].mesh);
        debug->Info("Model.nodes[%i].children : %i\n",node_index, model.nodes[node_index].children.size());

        for (int i=0;i<model.nodes[node_index].children.size();i++){
            int child_index = model.nodes[node_index].children.at(i);
            tinygltf::Node& child = model.nodes.at(child_index);
            debug->Info(" - Child[%i] -> model.nodes[%i].name : %s\n",i,child_index, child.name.c_str());
        }
    }
}

//Returns a pointer to node if found, or NULL
tinygltf::Node*  GLTFLoader::FindNode(std::string node_name){
    for (int node_index=0;node_index<model.nodes.size();node_index++){
        if (node_name.compare(model.nodes[node_index].name) == 0){
            return &model.nodes[node_index];
        }
    }
    return NULL;
}

//Returns a pointer to node if found, or NULL
tinygltf::Skin*  GLTFLoader::FindSkin(std::string skin_name){
    for (int skin_index=0;skin_index<model.skins.size();skin_index++){
        if (skin_name.compare(model.skins[skin_index].name) == 0){
            return &model.skins[skin_index];
        }
    }
    return NULL;
}

//Returns a pointer to node if found, or NULL
tinygltf::Animation*  GLTFLoader::FindAnimation(std::string animation_name){
    for (int animation_index=0;animation_index<model.animations.size();animation_index++){
        if (animation_name.compare(model.animations[animation_index].name) == 0){
            return &model.animations[animation_index];
        }
    }
    return NULL;
}

//Based on the component type, return the bytesize of the data we are trying to access.
int GLTFLoader::GetAccesorComponentTypeSize(const tinygltf::Accessor& accessor){
    int size = 1;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        size = 2;
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        size = 4;
    } else {
        debug->Fatal("Invalid accessor.componentType: %i\n",accessor.componentType);
    }
    return size;
}

int GLTFLoader::GetAccesorTypeSize(const tinygltf::Accessor& accessor){
    int size = 1;
    if (accessor.type == TINYGLTF_TYPE_SCALAR) {
        size = 1;
    } else if (accessor.type == TINYGLTF_TYPE_VEC2) {
        size = 2;
    } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
        size = 3;
    } else if (accessor.type == TINYGLTF_TYPE_VEC4) {
        size = 4;
    } else {
        debug->Fatal("Invalid accessor.type: %i\n",accessor.type);
    }
    return size;
}

//Returns the index located at position offset
int GLTFLoader::GetIndex(const tinygltf::Accessor& accessor, int offset){
    int index = 0;

    int size = GetAccesorComponentTypeSize(accessor);

    tinygltf::BufferView& bufferview = model.bufferViews[accessor.bufferView];
    tinygltf::Buffer& buffer = model.buffers[bufferview.buffer];

    int byte_offset = bufferview.byteOffset + (offset * size);

    if (size == sizeof(uint16_t)){
        uint16_t v;
        memcpy(&v,&buffer.data.at(byte_offset),sizeof(uint16_t));
        index = v;
    }else if (size == sizeof(uint32_t)){
        uint32_t v;
        memcpy(&v,&buffer.data.at(byte_offset),sizeof(uint32_t));
        index = v;
    }else{
        debug->Fatal("Unable to read index of size %i\n",size);
    }

    return index;
}

float GLTFLoader::Getfloat(unsigned char* data, int byte_offset){
    float v;
    memcpy(&v,data+byte_offset,sizeof(float));
    return v;
}

vec2 GLTFLoader::Getvec2(unsigned char* data, int byte_offset){
    vec2 v;
    memcpy(&v,data+byte_offset,sizeof(vec2));
    return v;
}

vec3 GLTFLoader::Getvec3(unsigned char* data, int byte_offset){
    vec3 v;
    memcpy(&v,data+byte_offset,sizeof(vec3));
    return v;
}

vec4 GLTFLoader::Getvec4(unsigned char* data, int byte_offset){
    vec4 v;
    memcpy(&v,data+byte_offset,sizeof(vec4));
    return v;
}

fmat4 GLTFLoader::Getfmat4(unsigned char* data, int byte_offset){
    fmat4 v;
    memcpy(&v,data+byte_offset,sizeof(fmat4));
    return v;
}

//This gets 4 uint8_t's and stores the first 3 in an int3
int3 GLTFLoader::Getint3_uint8_4(unsigned char* data, int byte_offset){
    uint8_t n[4];
    memcpy(n,data+byte_offset,sizeof(uint8_t) * 4);
    int3 v = int3(n[0],n[1],n[2]);
    return v;
}

morph_vertex GLTFLoader::GetMorphVertex(tinygltf::BufferView* pb, tinygltf::BufferView* nb, int index){
    morph_vertex v = {};
    //Kind of going to assume the correct accessor will be used for this:
    //I.e. FLOAT data with either VEC2 or VEC3.
    if (!pb){
        debug->Err("No bufferview for position data\n");
        return v;
    }
    if (!nb){
        debug->Err("No bufferview for normal data\n");
        return v;
    }
    int byte_offset_position = pb->byteOffset + (index * 3 * sizeof(float)); //FLOAT * VEC3 * index
    int byte_offset_normal = nb->byteOffset + (index * 3 * sizeof(float)); //FLOAT * VEC3 * index

    //This is guaranteed to exist... when the file is ok.
    tinygltf::Buffer& position_buffer = model.buffers[pb->buffer];
    tinygltf::Buffer& normal_buffer = model.buffers[nb->buffer];

    v.pos = Getvec3(&position_buffer.data.at(0),byte_offset_position);
    v.normal = Getvec3(&normal_buffer.data.at(0),byte_offset_normal);
    return v;
}

vertex GLTFLoader::GetVertex(tinygltf::BufferView* pb, tinygltf::BufferView* nb, tinygltf::BufferView* ub, int index){
    vertex v = {};
    //Kind of going to assume the correct accessor will be used for this:
    //I.e. FLOAT data with either VEC2 or VEC3.
    if (!pb){
        debug->Err("No bufferview for position data\n");
        return v;
    }
    if (!nb){
        debug->Err("No bufferview for normal data\n");
        return v;
    }
    if (!ub){
        debug->Err("No bufferview for uv data\n");
        return v;
    }

    int byte_offset_position = pb->byteOffset + (index * 3 * sizeof(float)); //FLOAT * VEC3 * index
    int byte_offset_normal = nb->byteOffset + (index * 3 * sizeof(float)); //FLOAT * VEC3 * index
    int byte_offset_uv = ub->byteOffset + (index * 2 * sizeof(float)); //FLOAT * VEC2 * index

    //This is guaranteed to exist... when the file is ok.
    tinygltf::Buffer& position_buffer = model.buffers[pb->buffer];
    tinygltf::Buffer& normal_buffer = model.buffers[nb->buffer];
    tinygltf::Buffer& uv_buffer = model.buffers[ub->buffer];

    v.pos = Getvec3(&position_buffer.data.at(0),byte_offset_position);
    v.normal = Getvec3(&normal_buffer.data.at(0),byte_offset_normal);
    v.uv = Getvec2(&uv_buffer.data.at(0),byte_offset_uv);

    v.matid = 0;
    return v;
}

skinned_vertex GLTFLoader::GetSkinnedVertex(tinygltf::BufferView* pb, tinygltf::BufferView* nb, tinygltf::BufferView* ub, tinygltf::BufferView* bb, tinygltf::BufferView* wb,int index){
    skinned_vertex v = {};
    //Kind of going to assume the correct accessor will be used for this:
    //I.e. FLOAT data with either VEC2 or VEC3.
    if (!pb){
        debug->Err("No bufferview for position data\n");
        return v;
    }
    if (!nb){
        debug->Err("No bufferview for normal data\n");
        return v;
    }
    if (!ub){
        debug->Err("No bufferview for uv data\n");
        return v;
    }
    if (!bb){
        debug->Err("No bufferview for uv bone ids\n");
        return v;
    }
    if (!wb){
        debug->Err("No bufferview for uv weights\n");
        return v;
    }

    int byte_offset_position = pb->byteOffset + (index * 3 * sizeof(float)); //FLOAT * VEC3 * index
    int byte_offset_normal = nb->byteOffset + (index * 3 * sizeof(float)); //FLOAT * VEC3 * index
    int byte_offset_uv = ub->byteOffset + (index * 2 * sizeof(float)); //FLOAT * VEC2 * index
    int byte_offset_bones = bb->byteOffset + (index * 4 * sizeof(uint8_t)); //FLOAT * UINT8 * index
    int byte_offset_weights = wb->byteOffset + (index * 4 * sizeof(float)); //FLOAT * VEC4 * index

    //This is guaranteed to exist... when the file is ok.
    tinygltf::Buffer& position_buffer = model.buffers[pb->buffer];
    tinygltf::Buffer& normal_buffer = model.buffers[nb->buffer];
    tinygltf::Buffer& uv_buffer = model.buffers[ub->buffer];
    tinygltf::Buffer& bones_buffer = model.buffers[bb->buffer];
    tinygltf::Buffer& weights_buffer = model.buffers[wb->buffer];

    v.pos = Getvec3(&position_buffer.data.at(0),byte_offset_position);
    v.normal = Getvec3(&normal_buffer.data.at(0),byte_offset_normal);
    v.uv = Getvec2(&uv_buffer.data.at(0),byte_offset_uv);
    v.bones = Getint3_uint8_4(&bones_buffer.data.at(0),byte_offset_bones);
    vec4 v_bone_weights = Getvec4(&weights_buffer.data.at(0),byte_offset_weights);

    //We only store 3 bones because that how we roll.
    v.weights.x = v_bone_weights.x;
    v.weights.y = v_bone_weights.y;
    v.weights.z = v_bone_weights.z;

    v.matid = 0;
    return v;
}

vec3 GLTFLoader::GetNodePosition(const char* node_name){
    //First, we lookup the node.
    tinygltf::Node* node = FindNode(node_name);
    if (!node){
        debug->Warn("Unable to find Node %s for you.\n",node_name);
        return vec3(0,0,0);
    }

    //A node can contain a single mesh (or none)
    debug->Trace("Found Node %s for you.\n",node_name);

    if (node->translation.size() == 0){
        return vec3(0,0,0);
    }else if (node->translation.size() != 3){
        debug->Fatal("GLTF Node %s contains invalid translation\n");
    }

    vec3 pos;
    pos.x = node->translation.at(0);
    pos.y = node->translation.at(1);
    pos.z = node->translation.at(2);
    return pos;
}

quat GLTFLoader::GetNodeRotation(const char* node_name){
    //First, we lookup the node.
    tinygltf::Node* node = FindNode(node_name);
    if (!node){
        debug->Warn("Unable to find Node %s for you.\n",node_name);
        return quat().identity();
    }

    //A node can contain a single mesh (or none)
    debug->Trace("Found Node %s for you.\n",node_name);

    if (node->rotation.size() == 0){
         return quat().identity();
    }else if (node->rotation.size() != 4){
        debug->Fatal("GLTF Node %s contains invalid rotation\n");
    }

    quat q;
    q.x = node->rotation.at(0);
    q.y = node->rotation.at(1);
    q.z = node->rotation.at(2);
    q.w = node->rotation.at(3);
    return q;
}

Mesh* GLTFLoader::GetMeshFromNode(const char* node_name, std::vector<Material>*optional_mat_list_out){
    //First, we lookup the node.
    tinygltf::Node* node = FindNode(node_name);
    if (!node){
        debug->Warn("Unable to find Node %s for you.\n",node_name);
        return NULL;
    }

    //A node can contain a single mesh (or none)
    debug->Trace("Found Node %s for you.\n",node_name);

    if (node->skin > -1){
        debug->Trace("Node %s contains a skin!\n",node_name);
    }

    if (node->mesh < 0){
        debug->Warn("GetMeshFromNode: Node %s does not contain a mesh\n",node_name);
        return NULL;
    }

    tinygltf::Mesh& nodemesh = model.meshes.at(node->mesh);
    debug->Trace(" -> Mesh name : %s\n",nodemesh.name.c_str());

    //A Mesh can have multiple primitives, like points, lines and triangles ... but not quads
    //We'll be parsing it only when it has seperate triangles for now
    if (nodemesh.primitives.size() < 1){
        debug->Err("Mesh has no primitives.\n");
        return NULL;
    }

    if (nodemesh.primitives.at(0).mode != TINYGLTF_MODE_TRIANGLES){
        debug->Err("Unable to parse primitive[0].mode %i\n",nodemesh.primitives.at(0).mode);
        return NULL;
    }

    //Now we expect there to be a NORMAL, POSITION and maybe a TEXCOORD_0
    //TODO: Parse a mesh without UVs
    //Parse multiple primitives, one primitive may have one material

    //All vertices loaded from this node.
    std::vector<vertex>verts;
    std::vector<morph_vertex>morph_verts;
    materials.clear();

    debug->Info("Node %s has %i primitives\n",node_name, nodemesh.primitives.size());
    int primitive_count = -1;
    for (tinygltf::Primitive &primitive : nodemesh.primitives){
        //This should be such that at least the materials in this Mesh can be looked up later on.
        primitive_count++;
        int material_index = primitive.material;
        int diff_texture_index = -1;
        debug->Trace("Primitive %i material_index = %i\n",primitive_count, material_index);

        tinygltf::Material* gltfmaterial = NULL;
        int material_id = 0;
        if (material_index > -1){
            gltfmaterial = &model.materials.at(material_index);

            Material m;
            m.name = gltfmaterial->name;

            //If the material has a diffuse texture, we load that here
            //TODO: We might already have previously loaded the image.
            if (gltfmaterial->pbrMetallicRoughness.baseColorTexture.index != -1){
                //This material uses texture with index
                diff_texture_index = gltfmaterial->pbrMetallicRoughness.baseColorTexture.index;
                if (diff_texture_index == -1){
                    debug->Fatal("diff_texture_index == -1\n");
                }
                debug->Info("diff_texture_index = %i\n",diff_texture_index);

                Texture* diff_texture = new Texture();
                //Get the memory offset.
                tinygltf::Texture& texture = model.textures.at(diff_texture_index);

                int image_index = texture.source;
                tinygltf::Image& image = model.images.at(image_index);
                if (image.bufferView == -1){
                    debug->Fatal("Probably external image file needs to be loaded.\n");
                }

                tinygltf::BufferView& bufferview = model.bufferViews.at(image.bufferView);
                tinygltf::Buffer& buffer = model.buffers.at(bufferview.buffer);

                int offset = bufferview.byteOffset;

                uint8_t* image_data = &buffer.data.at(offset);
                size_t data_len = bufferview.byteLength;
                diff_texture->LoadFromMemory(image_data,data_len,GL_TEXTURE_2D,1);
                diff_texture->name = image.name;

                m.diff_texture = diff_texture;
                debug->Info("Loaded diffuse texture %s from GLTF File using Bufferview %i\n",diff_texture->name.c_str(),image.bufferView);
            }else{
                //We just load the base color
                m.glsl_material.color.r = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(0);
                m.glsl_material.color.g = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(1);
                m.glsl_material.color.b = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(2);
                m.glsl_material.color.a = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(3);
            }
            materials.push_back(m);
            //Vertex parameter
            material_id = materials.size() - 1;
        }else{
            //Primitive has no material defined. We use material slot 0 anyway, so we can load some kind of default material
            material_id = 0;
        }

        std::map<std::string, int>::const_iterator it(primitive.attributes.begin());
        std::map<std::string, int>::const_iterator itEnd(primitive.attributes.end());

        const tinygltf::Accessor &indexAccessor = model.accessors[primitive.indices];

        tinygltf::BufferView* normal_bufferview = NULL;
        tinygltf::BufferView* position_bufferview = NULL;
        tinygltf::BufferView* uv_bufferview = NULL;

        //Iterate over the accessors for each attribute.
        //Set the appropriate buffer views.
        //The we loop over the indices fetching the normals and postions for those

        for (; it != itEnd; it++) {
            //it->first is NORMAL, POSITION and maybe a TEXCOORD_0
            const tinygltf::Accessor &accessor = model.accessors[it->second];
            int size = 1;
            if (accessor.type == TINYGLTF_TYPE_SCALAR) {
                size = 1;
            } else if (accessor.type == TINYGLTF_TYPE_VEC2) {
                size = 2;
            } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
                size = 3;
            } else if (accessor.type == TINYGLTF_TYPE_VEC4) {
                size = 4;
            } else {
                debug->Fatal("Invalid accessor.type: %i\n",accessor.type);
            }

            debug->Info("Accessor Size = %i for %s\n",size,it->first.c_str());

            if (it->first.compare("NORMAL") == 0){
                normal_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("POSITION") == 0){
                position_bufferview = &model.bufferViews[accessor.bufferView];
                debug->Info("Position accessor.bufferView.count = %i\n",accessor.count);
            }else if (it->first.compare("TEXCOORD_0") == 0){
                uv_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("JOINTS_0") == 0){
                //Skinning
                debug->Warn("Loading a skinned mesh as normal mesh\n");
            }else if (it->first.compare("WEIGHTS_0") == 0){
                //Skinning
            }
        }

        if (uv_bufferview == NULL){
            debug->Fatal("No uv_bufferview for Mesh\n");
        }

        int vertex_count = indexAccessor.count;
        debug->Info("indexAccessor.count = %i\n",vertex_count);
        int triangle_count = vertex_count / 3;

        //Assemble the triangles:
        int vertex_index = 0;
        for (int t=0;t<triangle_count;t++){
            vertex vert1 = GetVertex(position_bufferview,normal_bufferview,uv_bufferview, GetIndex(indexAccessor,vertex_index + 0));
            vertex vert2 = GetVertex(position_bufferview,normal_bufferview,uv_bufferview, GetIndex(indexAccessor,vertex_index + 1));
            vertex vert3 = GetVertex(position_bufferview,normal_bufferview,uv_bufferview, GetIndex(indexAccessor, vertex_index + 2));

            //Tangent calculation
            vec3 edge1 = vert2.pos - vert1.pos;
            vec3 edge2 = vert3.pos - vert1.pos;
            vec2 deltaUV1 = vert2.uv - vert1.uv;
            vec2 deltaUV2 = vert3.uv - vert1.uv;

            float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
            vert1.tangent = (edge1 * deltaUV2.y   - edge2 * deltaUV1.y)*f;
            vert1.tangent.normalize();
            vert2.tangent = vert1.tangent;
            vert3.tangent = vert1.tangent;

            //We use the last material we loaded.
            vert1.matid = material_id;
            vert2.matid = material_id;
            vert3.matid = material_id;

            verts.push_back(vert1);
            verts.push_back(vert2);
            verts.push_back(vert3);
            vertex_index += 3;
        }

        //In addition to attibutes, it has 'targets' which are effectively the same, they form meshes.
        //This is done per primitive. I.e. a single mesh, with two materials can have 2 primitives. Each with an
        // asociated list of morph targets
        //For morph targets == shapekeys
        //Reset

        normal_bufferview = NULL;
        position_bufferview = NULL;
        uv_bufferview = NULL;

        //List morph targets
        for (size_t mt_i = 0; mt_i < primitive.targets.size(); mt_i++){
            std::map<std::string, int>& morph_target = primitive.targets.at(mt_i);
            debug->Info("targets[%i].size() = %i\n",mt_i,morph_target.size());

            std::map<std::string, int>::const_iterator it(morph_target.begin());
            std::map<std::string, int>::const_iterator itEnd(morph_target.end());

            int attrib_index = 0;
            for (; it != itEnd; it++) {
                debug->Info("targets[%i] : %s -> accessor: %i\n",mt_i, it->first.c_str(),it->second);
                attrib_index++;

                //We expect a morph target to consist of positions and normals, for a morph_vertex
                const tinygltf::Accessor &accessor = model.accessors[it->second];
                int size = 1;
                if (accessor.type == TINYGLTF_TYPE_SCALAR) {
                    size = 1;
                } else if (accessor.type == TINYGLTF_TYPE_VEC2) {
                    size = 2;
                } else if (accessor.type == TINYGLTF_TYPE_VEC3) {
                    size = 3;
                } else if (accessor.type == TINYGLTF_TYPE_VEC4) {
                    size = 4;
                } else {
                    debug->Fatal("Invalid accessor.type: %i\n",accessor.type);
                }

                if (it->first.compare("NORMAL") == 0){
                    normal_bufferview = &model.bufferViews[accessor.bufferView];
                }else if (it->first.compare("POSITION") == 0){
                    position_bufferview = &model.bufferViews[accessor.bufferView];
                }else{
                    debug->Err("Unknown Morph Target accessor %s\n",it->first);
                }
            }

            //We have to read these in using the same indices the base mesh was loaded with.
            //Assemble the triangles:
            int vertex_index = 0;
            for (int t=0;t<triangle_count;t++){
                morph_vertex vert1 = GetMorphVertex(position_bufferview,normal_bufferview, GetIndex(indexAccessor,vertex_index + 0));
                morph_vertex vert2 = GetMorphVertex(position_bufferview,normal_bufferview, GetIndex(indexAccessor,vertex_index + 1));
                morph_vertex vert3 = GetMorphVertex(position_bufferview,normal_bufferview, GetIndex(indexAccessor,vertex_index + 2));

                morph_verts.push_back(vert1);
                morph_verts.push_back(vert2);
                morph_verts.push_back(vert3);
                vertex_index += 3;
            }
        }
    }

    if (optional_mat_list_out){
        optional_mat_list_out->insert(optional_mat_list_out->end(),materials.begin(),materials.end());
    }

    debug->Info("Loaded %i vertices. Loaded %i materials\n",verts.size(),materials.size());
    debug->Info("Loaded %i associated morph_target vertices.\n",morph_verts.size());

    int num_morph_targets = morph_verts.size() / verts.size();
    //Check if they are whole multiples
    if (num_morph_targets * verts.size() != morph_verts.size()){
        debug->Fatal("Loaded a fractional numper of morph vertices.\n");
    }

    Mesh* mesh = new Mesh();
    mesh->SetMeshData(&verts.at(0),verts.size());
    if (morph_verts.size() > 0){
        mesh->SetMorphMeshData(&morph_verts.at(0),morph_verts.size());
    }
    mesh->num_materials = materials.size();
    return mesh;
}

//TODO: Merge with getmesh and switch by argument on returned mesh type.
// Basically the same as get mesh, only it returns a skinned mesh
Mesh* GLTFLoader::GetSkinnedMeshFromNode(const char* node_name, std::vector<Material>*optional_mat_list_out){
    //First, we lookup the node.
    tinygltf::Node* node = FindNode(node_name);
    if (!node){
        debug->Warn("Unable to find Node %s for you.\n",node_name);
        return NULL;
    }

    //A node can contain a single mesh (or none)
    debug->Trace("GetSkinnedMeshFromNode: Found Node %s for you.\n",node_name);

    if (node->skin == -1){
        debug->Err("GetSkinnedMeshFromNode: Node %s does not contain a skin!\n",node_name);
        return NULL;
    }

    if (node->mesh < 0){
        debug->Warn("GetSkinnedMeshFromNode: Node %s does not contain a mesh\n",node_name);
        return NULL;
    }

    tinygltf::Mesh& nodemesh = model.meshes.at(node->mesh);
    debug->Trace(" -> Mesh name : %s\n",nodemesh.name.c_str());

    //A Mesh can have multiple primitives, like points, lines and triangles ... but not quads
    //We'll be parsing it only when it has seperate triangles for now
    if (nodemesh.primitives.size() < 1){
        debug->Err("Mesh has no primitives.\n");
        return NULL;
    }

    if (nodemesh.primitives.at(0).mode != TINYGLTF_MODE_TRIANGLES){
        debug->Err("Unable to parse primitive[0].mode %i\n",nodemesh.primitives.at(0).mode);
        return NULL;
    }


    //Now we expect there to be a NORMAL, POSITION and maybe a TEXCOORD_0
    //TODO: Parse a mesh without UVs
    //Parse multiple primitives, one primitive may have one material

    //All vertices loaded from this node.
    std::vector<skinned_vertex>verts;
    materials.clear();

    debug->Trace("Node has %i primitives\n",nodemesh.primitives.size());

    for (tinygltf::Primitive &primitive : nodemesh.primitives){
        //This should be such that at least the materials in this Mesh can be looked up later on.
        int material_index = primitive.material;
        int diff_texture_index = -1;


        tinygltf::Material* gltfmaterial = NULL;
        if (material_index > -1){
            gltfmaterial = &model.materials.at(material_index);

            Material m;
            m.name = gltfmaterial->name;


            //If the material has a diffuse texture, we load that here
            if (gltfmaterial->pbrMetallicRoughness.baseColorTexture.index != -1){
                //This material uses texture with index
                diff_texture_index = gltfmaterial->pbrMetallicRoughness.baseColorTexture.index;

                //Get texture by name and store/lookup in textures vector... TODO

                Texture* diff_texture = new Texture();
                //Get the memory offset.
                tinygltf::Texture& texture = model.textures.at(diff_texture_index);

                int image_index = texture.source;
                tinygltf::Image& image = model.images.at(image_index);
                if (image.bufferView == -1){
                    debug->Fatal("Probably external image file needs to be loaded.\n");
                }

                tinygltf::BufferView& bufferview = model.bufferViews.at(image.bufferView);
                tinygltf::Buffer& buffer = model.buffers.at(bufferview.buffer);

                int offset = bufferview.byteOffset;

                uint8_t* image_data = &buffer.data.at(offset);
                size_t data_len = bufferview.byteLength;
                diff_texture->LoadFromMemory(image_data,data_len,GL_TEXTURE_2D,1);
                diff_texture->name = image.name;

                m.diff_texture = diff_texture;
                debug->Info("Loaded diffuse texture %s from GLTF File\n",diff_texture->name.c_str());
            }else{
                //We just load the base color
                m.glsl_material.color.r = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(0);
                m.glsl_material.color.g = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(1);
                m.glsl_material.color.b = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(2);
                m.glsl_material.color.a = gltfmaterial->pbrMetallicRoughness.baseColorFactor.at(3);
            }
            materials.push_back(m);
        }
        int material_id = materials.size() - 1;

        std::map<std::string, int>::const_iterator it(primitive.attributes.begin());
        std::map<std::string, int>::const_iterator itEnd(primitive.attributes.end());

        const tinygltf::Accessor &indexAccessor = model.accessors[primitive.indices];

        tinygltf::BufferView* normal_bufferview = NULL;
        tinygltf::BufferView* position_bufferview = NULL;
        tinygltf::BufferView* uv_bufferview = NULL;
        tinygltf::BufferView* bones_bufferview = NULL;
        tinygltf::BufferView* weights_bufferview = NULL;


        //Iterate over the accessors for each attribute.
        //Set the appropriate buffer views.
        //The we loop over the indices fetching the normals and postions for those

        for (; it != itEnd; it++) {
            //it->first is NORMAL, POSITION and maybe a TEXCOORD_0

            const tinygltf::Accessor &accessor = model.accessors[it->second];
            int size = GetAccesorTypeSize(accessor);

            debug->Trace("Accessor Size = %i for %s component_type = %i\n",size,it->first.c_str(),accessor.componentType);

            if (it->first.compare("NORMAL") == 0){
                normal_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("POSITION") == 0){
                position_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("TEXCOORD_0") == 0){
                uv_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("JOINTS_0") == 0){
                bones_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("WEIGHTS_0") == 0){
                weights_bufferview = &model.bufferViews[accessor.bufferView];
            }
        }

        if (uv_bufferview == NULL){
            debug->Fatal("No uv_bufferview for Mesh\n");
        }

        int vertex_count = indexAccessor.count;
        debug->Trace("indexAccessor.count = %i\n",vertex_count);
        int triangle_count = vertex_count / 3;

        //Assemble the triangles:
        int vertex_index = 0;
        for (int t=0;t<triangle_count;t++){
            skinned_vertex vert1 = GetSkinnedVertex(position_bufferview,normal_bufferview,uv_bufferview,bones_bufferview,weights_bufferview, GetIndex(indexAccessor,vertex_index + 0));
            skinned_vertex vert2 = GetSkinnedVertex(position_bufferview,normal_bufferview,uv_bufferview,bones_bufferview,weights_bufferview, GetIndex(indexAccessor,vertex_index + 1));
            skinned_vertex vert3 = GetSkinnedVertex(position_bufferview,normal_bufferview,uv_bufferview,bones_bufferview,weights_bufferview, GetIndex(indexAccessor, vertex_index + 2));

            //Tangent calculation
            vec3 edge1 = vert2.pos - vert1.pos;
            vec3 edge2 = vert3.pos - vert1.pos;
            vec2 deltaUV1 = vert2.uv - vert1.uv;
            vec2 deltaUV2 = vert3.uv - vert1.uv;

            float f = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y);
            vert1.tangent = (edge1 * deltaUV2.y   - edge2 * deltaUV1.y)*f;
            vert1.tangent.normalize();
            vert2.tangent = vert1.tangent;
            vert3.tangent = vert1.tangent;

            //We use the last material we loaded.
            vert1.matid = material_id;
            vert2.matid = material_id;
            vert3.matid = material_id;

            verts.push_back(vert1);
            verts.push_back(vert2);
            verts.push_back(vert3);
            vertex_index += 3;
        }
    }

    if (optional_mat_list_out){
        optional_mat_list_out->insert(optional_mat_list_out->end(),materials.begin(),materials.end());
    }

    debug->Trace("Generated %i skinned vertices. Loaded %i materials\n",verts.size(),materials.size());
    Mesh* mesh = new Mesh();
    mesh->SetSkinnedMeshData(&verts.at(0),verts.size());
    mesh->num_materials = materials.size();
    return mesh;
}

//Somehow load an animation somewhere
Animation* GLTFLoader::LoadAnimation(const char* animation_name){
    //First, we lookup the node.
    tinygltf::Animation* gltf_animation = FindAnimation(animation_name);
    if (!gltf_animation){
        debug->Warn("Unable to find Animation %s for you.\n",animation_name);
        return NULL;
    }

    //A node can contain a single mesh (or none)
    debug->Trace("LoadAnimation: Found Animation %s for you.\n",animation_name);

    if (gltf_animation->channels.size() == 0){
        debug->Err("LoadAnimation: Animations has no channels.\n",animation_name);
        return NULL;
    }

    if (gltf_animation->samplers.size() == 0){
        debug->Warn("LoadAnimation: Animation has no samplers\n",animation_name);
        return NULL;
    }

    Animation* animation = new Animation();
    animation->name = animation_name;

    float largest_frame_time = 0.0f;

    int channel_index = -1;
    for (tinygltf::AnimationChannel& channel: gltf_animation->channels){
        channel_index++;
        tinygltf::Node& target_node = model.nodes.at(channel.target_node);
        debug->Trace("Channel %2i : sampler = %i node = %i (%s) target_path=%s\n",channel_index,channel.sampler,channel.target_node,target_node.name.c_str()
                                                                                ,channel.target_path.c_str());

        int target_path = ANIM_TARGET_PATH_NONE;
        if (channel.target_path.compare("scale") == 0){
            target_path = ANIM_TARGET_PATH_SCALE;
        }else if (channel.target_path.compare("rotation") == 0){
            target_path = ANIM_TARGET_PATH_ROTATION;
        }else if (channel.target_path.compare("translation") == 0){
            target_path = ANIM_TARGET_PATH_TRANSLATION;
        }else if (channel.target_path.compare("weights") == 0){
            target_path = ANIM_TARGET_PATH_WEIGHTS;
        }else{
            debug->Fatal("Unknown animation target path %s\n",channel.target_path.c_str());
        }

        //Find the ObjectAnimation that may already contain this node_name
        ObjectAnimation* object_animation = animation->FindObjectAnimation(target_node.name);
        if (!object_animation){
            debug->Trace("Creating new ObjectAnimation\n");
            object_animation = new ObjectAnimation();
            object_animation->target_name = target_node.name;
            animation->AddObjectAnimation(object_animation);
        }else{
            debug->Trace("Updating Existing ObjectAnimation\n");
        }

        tinygltf::AnimationSampler& sampler = gltf_animation->samplers.at(channel.sampler);
        tinygltf::Accessor& input_accessor = model.accessors.at(sampler.input);
        tinygltf::Accessor& output_accessor = model.accessors.at(sampler.output);
        debug->Trace(" -> Sampler input (accessor Time) = %3i type_size = %i byte_stride = %i\n",sampler.input,GetAccesorTypeSize(input_accessor),input_accessor.ByteStride(model.bufferViews[input_accessor.bufferView]));
        debug->Trace(" -> Sampler output (accessor ?)   = %3i type_size = %i byte_stride = %i\n",sampler.output,GetAccesorTypeSize(output_accessor),output_accessor.ByteStride(model.bufferViews[output_accessor.bufferView]));
        debug->Trace(" -> Interpolation Type: %s\n",sampler.interpolation.c_str());

        //Display number of time slots for each input accessor
        debug->Trace(" -> Input  Accesor has %2i frames in BufferView %i\n",input_accessor.count,input_accessor.bufferView);
        debug->Trace(" -> Output Accesor has %2i frames in BufferView %i\n",output_accessor.count,output_accessor.bufferView);

        int interpolation_type = ANIMSAMPLER_INTERPOLATION_TYPE_NONE;
        if (sampler.interpolation.compare("LINEAR") == 0){
            interpolation_type = ANIMSAMPLER_INTERPOLATION_TYPE_LINEAR;
        }else if (sampler.interpolation.compare("STEP") == 0){
            interpolation_type = ANIMSAMPLER_INTERPOLATION_TYPE_STEP;
        }else if (sampler.interpolation.compare("CUBICSPLINE") == 0){
            interpolation_type = ANIMSAMPLER_INTERPOLATION_TYPE_CUBICSPLINE;
        }else{
            debug->Fatal("Unknown animationsampler interpolation type %s\n",sampler.interpolation.c_str());
        }

        tinygltf::BufferView &input_buffer_view = model.bufferViews[input_accessor.bufferView];
        tinygltf::Buffer &input_buffer = model.buffers[input_buffer_view.buffer];

        tinygltf::BufferView &output_buffer_view = model.bufferViews[output_accessor.bufferView];
        tinygltf::Buffer &output_buffer = model.buffers[output_buffer_view.buffer];

        for (int index = 0;index<input_accessor.count;index++){
            int input_byte_offset = input_accessor.byteOffset + input_buffer_view.byteOffset + (index * sizeof(float));
            float frame_time = Getfloat(&input_buffer.data.at(0),input_byte_offset);
            debug->Trace("    -> Frame Time [%i] = %.4f\n",index, frame_time);

            //TODO: The keyframes need to be inserted in a list
            ObjectAnimationKeyFrame* keyframe = object_animation->FindKeyframeAtTime(frame_time);
            if (!keyframe){
                debug->Trace("Creating new keyframe at time index %.4f\n",frame_time);
                keyframe = new ObjectAnimationKeyFrame();
                keyframe->time = frame_time;
                object_animation->AddKeyframe(keyframe);
                if (frame_time > largest_frame_time){
                    largest_frame_time = frame_time;
                }
            }else{
                debug->Trace("Updating existing keyframe at time index %.4f\n",frame_time);
            }


            if (target_path == ANIM_TARGET_PATH_ROTATION){
                //In CUBICSPLINE there seems to be 3 values, the middle one the actual value.
                int output_byte_offset = 0;
                if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_LINEAR){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + (index * sizeof(vec4));
                }else if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_STEP){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + (index * sizeof(vec4));
                }else if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_CUBICSPLINE){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + sizeof(vec4) + (index * sizeof(vec4) * 3);
                }else{
                    debug->Fatal("Invalid interpolation type %i\n",interpolation_type);
                }
                vec4 r = Getvec4(&output_buffer.data.at(0),output_byte_offset);
                debug->Trace("    -> Target Rotation [%i] = %.2f %.2f %.2f %.2f\n",index, r.x,r.y,r.z,r.w);
                if(keyframe->f_rotation == true){
                    debug->Err("Rotation for this keyframe is already set.\n");
                }
                keyframe->f_rotation = true;
                keyframe->rotation = quat(r.x,r.y,r.z,r.w);
            }else if (target_path == ANIM_TARGET_PATH_TRANSLATION){
                int output_byte_offset = 0;
                if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_LINEAR){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + (index * sizeof(vec3));
                }else if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_STEP){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + (index * sizeof(vec3));
                }else if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_CUBICSPLINE){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + sizeof(vec3) + (index * sizeof(vec3) * 3);
                }else{
                    debug->Fatal("Invalid interpolation type %i\n",interpolation_type);
                }
                vec3 t = Getvec3(&output_buffer.data.at(0),output_byte_offset);
                debug->Trace("    -> Target Translation [%i] = %.2f %.2f %.2ff\n",index, t.x,t.y,t.z);
                if(keyframe->f_position == true){
                    debug->Err("Translation for this keyframe is already set.\n");
                }
                keyframe->f_position = true;
                keyframe->position = t;
            }else if (target_path == ANIM_TARGET_PATH_WEIGHTS){
                int num_weights = output_accessor.count / input_accessor.count;
                int sizeof_frame = sizeof(float) * num_weights;
                int output_byte_offset = 0;
                if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_LINEAR){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + (index * sizeof_frame);
                }else if (interpolation_type == ANIMSAMPLER_INTERPOLATION_TYPE_STEP){
                    output_byte_offset = output_accessor.byteOffset +  output_buffer_view.byteOffset + (index * sizeof_frame);
                }else{
                    debug->Fatal("Invalid interpolation type %i\n",interpolation_type);
                }
                keyframe->num_shapekeys = num_weights;
                for (int i =0;i<num_weights;i++){
                    float t = Getfloat(&output_buffer.data.at(0),output_byte_offset);
                    output_byte_offset += sizeof(float);
                    keyframe->shapekey_weights.push_back(t);
                }
                if(keyframe->f_shapekeys == true){
                    debug->Err("Translation for this keyframe is already set.\n");
                }
                keyframe->f_shapekeys = true;
            }
        }
    }

    debug->Trace("Done loading animation %s. References %i different objects. Frame time %.4f \n",animation->name.c_str(),animation->object_animations.size(),largest_frame_time);

    for (ObjectAnimation* object_animation:animation->object_animations){
        debug->Trace(" -> target_name : %s\n",object_animation->target_name.c_str());
    }

    animation->duration = largest_frame_time;
    return animation;
}

Bone* GLTFLoader::GetBone(int node_index, int& bone_count, std::vector<fmat4>&invbinmatrices, AssetManager* assetmanager){
    tinygltf::Node& node = model.nodes.at(node_index);
    Bone* bone = new Bone();
    bone->name = node.name;
    bone->bone_index = bone_count;
    bone->node_index = node_index;
    bone->material_names[0] = "bone_mat";
    bone->f_update_materials = true;
    bone->inverse_bind_matrix = invbinmatrices.at(bone->bone_index);
    bone_count++;

    if (node.translation.size() == 3){
        vec3 translation = vec3(node.translation[0],node.translation[1],node.translation[2]);
        bone->SetPosition(translation);
    }
    if (node.rotation.size() == 4){
        quat rotation = quat(node.rotation[0],node.rotation[1],node.rotation[2],node.rotation[3]);
        bone->SetRotation(rotation);
    }
    if (node.scale.size() == 3){
        vec3 scale = vec3(node.scale[0],node.scale[1],node.scale[2]);
        bone->SetScale(scale);
        debug->Trace(" Bone scale %.2f %.2f %.2f\n",scale.x,scale.y,scale.z);
    }

    //For debugging, we make the bone object have a mesh that we can see.
    /*if (assetmanager){
        assetmanager->GetObjectFromAsset("bone_mesh",bone);
    }*/

    //Traverse nodes until no more nodes have children.
    for (int node_index : node.children){
        Bone* child_bone = GetBone(node_index,bone_count,invbinmatrices,assetmanager);
        bone->AttachChild(child_bone);
        child_bone->SetReferences();
        bone->SetReferences();
        debug->Trace("Attaching Bone %s onto %s\n",child_bone->name.c_str(),bone->name.c_str());
    }
    bone->SetReferences();
    return bone;
}

//In blender, this would be an armature in here we look up a skin.
//Asset manager for getting a bone mesh.
Skeleton*  GLTFLoader::GetSkeleton(const char* skeleton_name, AssetManager* assetmanager,Skeleton* optional_target){
    Skeleton* skeleton;
    if (optional_target){
        skeleton = optional_target;
    }else{
        skeleton = new Skeleton();
    }
    skeleton->name = skeleton_name;

    tinygltf::Skin* skin = FindSkin(skeleton_name);
    if (!skin){
        debug->Err("Unable to find skeleton (skin) %s for you\n",skeleton_name);
        return NULL;
    }

    // A skin contains a root node (which won't have a parent) and a list of all the skeleton nodes.
    debug->Trace("Found skeleton %s for you.\n",skeleton_name);

    int accessor_invbindmatrices = skin->inverseBindMatrices;
    if (accessor_invbindmatrices == -1){
        debug->Fatal("No accessor to get inverseBindMatrices from skin\n");
    }

    //Apparently, we dont have a skeleton as root node.

    //We expect the same number of matrices as bones.
    std::vector<fmat4>inv_binds;
    LoadInverseBindMatrices(inv_binds,accessor_invbindmatrices);

    //The first one should be a root bone, and reference all the subsequent bones in some way.
    if (skin->joints.size() < 1){
        debug->Err("No bones in skeleton.\n");
        return NULL;
    }

    //Let's just list all the nodes
    for (int node_index : skin->joints){
        tinygltf::Node& node = model.nodes.at(node_index);
        debug->Trace("Loading Node[%i] - %s as bone\n",node_index,node.name.c_str());
        debug->Trace(" -> Node mesh, skin       : %i, %i\n",node.mesh,node.skin);
        debug->Trace(" -> Node translation.size : %i\n",node.translation.size());
        debug->Trace(" -> Node rotation.size    : %i\n",node.rotation.size());
        debug->Trace(" -> Node scale.size       : %i\n",node.scale.size());
        debug->Trace(" -> Node matrix.size      : %i\n",node.matrix.size());
        debug->Trace(" -> Node children.size    : %i\n",node.children.size());
    }

    //Recursively get everything
    int bone_count = 0;
    Bone* root_bone = GetBone(skin->joints.at(0),bone_count,inv_binds,assetmanager);
    skeleton->AttachChild(root_bone);
    skeleton->num_bones = bone_count;
    debug->Trace("Loaded %i bones into skeleton\n",bone_count);
    return skeleton;
}

void GLTFLoader::LoadInverseBindMatrices(std::vector<fmat4>& matrices, int accesor_index){
    const tinygltf::Accessor &accessor = model.accessors[accesor_index];
    if (accessor.type != TINYGLTF_TYPE_MAT4){
        debug->Fatal("Expected accessor.type TINYGLTF_TYPE_MAT4 but got %i\n",accessor.type);
    }
    debug->Trace("Loading %i inverse Bind Matrices from Bufferview %i\n",accessor.count,accessor.bufferView);

    tinygltf::BufferView &buffer_view = model.bufferViews[accessor.bufferView];
    tinygltf::Buffer &buffer = model.buffers[buffer_view.buffer];

    for (int index = 0;index<accessor.count;index++){
        int byte_offset = buffer_view.byteOffset + (index * 4 * sizeof(vec4)); //VEC4 * 4 * index
        fmat4 matrix = Getfmat4(&buffer.data.at(0),byte_offset);
        matrices.push_back(matrix);
    }
}