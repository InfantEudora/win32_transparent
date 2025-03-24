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
    debug->Info("Model has %i scenes\n",model.scenes.size());
    for (int scene_index=0;scene_index<model.scenes.size();scene_index++){
        debug->Info("Model.scenes[%i].name : %s\n",scene_index, model.scenes[scene_index].name.c_str());
    }
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
    debug->Info("Model has %i meshes\n",model.meshes.size());
    for (int mesh_index=0;mesh_index<model.meshes.size();mesh_index++){
        debug->Info("Model.meshes[%i].name       : %s\n",mesh_index, model.meshes[mesh_index].name.c_str());
        debug->Info("Model.meshes[%i].primitives : %i\n",mesh_index, model.meshes[mesh_index].primitives.size());

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
        }

    }
    debug->Info("Model has %i textures\n",model.textures.size());
    for (int texture_index=0;texture_index<model.textures.size();texture_index++){
        debug->Info("Model.textures[%i].name : %s\n",texture_index, model.textures[texture_index].name.c_str());
    }
    debug->Info("Model has %i images\n",model.textures.size());
    for (int image_index=0;image_index<model.images.size();image_index++){
        debug->Info("Model.images[%i].name       : %s\n",image_index, model.images[image_index].name.c_str());
        debug->Info("Model.images[%i].mime_type  : %s\n",image_index, model.images[image_index].mimeType.c_str());
        debug->Info("Model.images[%i].bufferView : %i\n",image_index, model.images[image_index].bufferView);
        debug->Info("Model.images[%i].uri        : %s\n",image_index, model.images[image_index].uri.c_str());
    }
    debug->Info("Model has %i materials\n",model.materials.size());
    for (int material_index=0;material_index<model.materials.size();material_index++){
        debug->Info("Model.materials[%i].name                       : %s\n",material_index, model.materials[material_index].name.c_str());
        debug->Info("Model.materials[%i].pbr.baseColorTexture.index : %i\n",material_index, model.materials[material_index].pbrMetallicRoughness.baseColorTexture.index);
        debug->Info("Model.materials[%i].pbr.baseColorFactor        : %.1f %.1f %.1f %.1f\n",material_index
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(0)
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(1)
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(2)
        , model.materials[material_index].pbrMetallicRoughness.baseColorFactor.at(3));

    }
    debug->Info("Model has %i buffers\n",model.buffers.size());
    for (int buffer_index=0;buffer_index<model.buffers.size();buffer_index++){
        debug->Info("Model.buffers[%i].name : %s\n",buffer_index, model.buffers[buffer_index].name.c_str());
    }
    debug->Info("Model has %i bufferViews\n",model.bufferViews.size());
    for (int bufferview_index=0;bufferview_index<model.bufferViews.size();bufferview_index++){
        debug->Info("Model.bufferViews[%i].name       : %s\n",bufferview_index, model.bufferViews[bufferview_index].name.c_str());
        debug->Info("Model.bufferViews[%i].buffer     : %i\n",bufferview_index, model.bufferViews[bufferview_index].buffer);
        debug->Info("Model.bufferViews[%i].byteOffset : %i\n",bufferview_index, model.bufferViews[bufferview_index].byteOffset);
        debug->Info("Model.bufferViews[%i].byteLength : %i\n",bufferview_index, model.bufferViews[bufferview_index].byteLength);
        debug->Info("Model.bufferViews[%i].byteStride : %i\n",bufferview_index, model.bufferViews[bufferview_index].byteStride);
        debug->Info("Model.bufferViews[%i].target     : %i\n",bufferview_index, model.bufferViews[bufferview_index].target);

        //Now list all the accessors the use this bufferView
        bool accessor_found = false;
        int sparse_accessor = -1;
        for (size_t a_i = 0; a_i < model.accessors.size(); ++a_i){
            tinygltf::Accessor& accessor = model.accessors[a_i];
            if (accessor.bufferView == bufferview_index){
                debug->Info("Model.accessors[%i] uses this bufferView\n",a_i);
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
            debug->Info("No accessors use this bufferView\n");
        }

    }
    debug->Info("Model has %i accessors\n",model.accessors.size());
    for (int accessor_index=0;accessor_index<model.accessors.size();accessor_index++){
        debug->Info("Model.accessors[%i].name          : %s\n",accessor_index, model.accessors[accessor_index].name.c_str());
        debug->Info("Model.accessors[%i].bufferView    : %i\n",accessor_index, model.accessors[accessor_index].bufferView);
        debug->Info("Model.accessors[%i].byteOffset    : %i\n",accessor_index, model.accessors[accessor_index].byteOffset);
        debug->Info("Model.accessors[%i].componentType : %i\n",accessor_index, model.accessors[accessor_index].componentType);
        debug->Info("Model.accessors[%i].count         : %i\n",accessor_index, model.accessors[accessor_index].count);
    }

    debug->Info("More info!!!\n");
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

//Returns the index located at position offset
int GLTFLoader::GetIndex(const tinygltf::Accessor& accessor, int offset){
    int index = 0;

    int size = 1;
    if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
        size = 2;
    } else if (accessor.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
        size = 4;
    } else {
        debug->Fatal("Invalid accessor.componentType: %i\n",accessor.componentType);
    }

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

Mesh* GLTFLoader::GetMeshFromNode(const char* node_name, std::vector<Material>*optional_mat_list_out){
    //First, we lookup the node.
    tinygltf::Node* node = FindNode(node_name);
    if (!node){
        debug->Warn("Unable to find Node %s for you.\n",node_name);
        return NULL;
    }

    //A node can contain a single mesh (or none)
    debug->Info("Found Node %s for you.\n",node_name);

    if (node->mesh < 0){
        debug->Info("Node %s does not contain a mesh\n",node_name);
        return NULL;
    }

    tinygltf::Mesh& nodemesh = model.meshes.at(node->mesh);
    debug->Info(" -> Mesh name : %s\n",nodemesh.name.c_str());

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
    materials.clear();

    debug->Info("Node has %i primitives\n",nodemesh.primitives.size());

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

                Texture* diff_texture = new Texture();
                //Get the memory offset.

                tinygltf::Image& image = model.images.at(diff_texture_index);
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

            debug->Info("Accessor Size = %i\n",size);

            if (it->first.compare("NORMAL") == 0){
                normal_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("POSITION") == 0){
                position_bufferview = &model.bufferViews[accessor.bufferView];
            }else if (it->first.compare("TEXCOORD_0") == 0){
                uv_bufferview = &model.bufferViews[accessor.bufferView];
            }

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
    }

    if (optional_mat_list_out){
        optional_mat_list_out->insert(optional_mat_list_out->end(),materials.begin(),materials.end());
    }

    debug->Info("Generated %i vertices. Loaded %i materials\n",verts.size(),materials.size());
    Mesh* mesh = new Mesh();
    mesh->SetMeshData(&verts.at(0),verts.size());
    mesh->num_materials = materials.size();
    return mesh;
}