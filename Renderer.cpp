
#include "Renderer.h"
#include "OBJLoader.h"


#include "Debug.h"
static Debugger* debug = new Debugger("Renderer",DEBUG_INFO);

Renderer::Renderer(int w, int h){
    width = w;
    height = h;
}

bool Renderer::Init(){
    if (!InitFBO()){
        return false;
    }

    if (!InitSSBO()){
        return false;
    }

    //A single mesh
    Mesh* cube_mesh = new Mesh();
    cube_mesh->LoadUnitCube();

    Mesh* sphere_mesh = OBJLoader::ParseOBJFile("sphere.obj");
    Mesh* char_mesh = OBJLoader::ParseOBJFile("chara.obj");

    //Make a bunch of objects
    for (int i = 0;i<5;i++){
        cube = new Object();
        cube->SetMesh(cube_mesh);
        objects.push_back(cube);
        cube->rotation = (rand()%100) / 10.0f;
        cube->rot_speed = rand()%10 *0.001f;
        cube->SetPosition(vec3(0.5,0.5,0.0));

        cube->material_slot[0] = i%2;
    }

    //Make a bunch of spheres
    for (int i = 0;i<5;i++){
        cube = new Object();
        cube->SetMesh(sphere_mesh);
        objects.push_back(cube);
        cube->rotation = (rand()%100) / 10.0f;
        cube->rot_speed = rand()%10 *0.001f;
        cube->SetPosition(vec3(-0.5,0.5,0.0));
        cube->material_slot[0] = i%2;
    }

    camera = new Camera();
    camera->SetPosition(vec3(0,0.5,8));
    camera->SetLookat(vec3());
    camera->SetupPerspective(width,height,45,0.1,100);

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    // Create the texture
    texture = new Texture();
    texture->Create2D(128,128,GL_RGBA8);

    //Create a compute shader that will massage the texture.
    Shader* comp_shader = new Shader();
    comp_shader->CreateComputeShader("texture.comp");
    comp_shader->Use();
    //glBindImageTexture(0, texture->texture_id, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindImageTexture(1, texture->texture_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
    glDispatchCompute(texture->width, texture->height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);


    //A second texture
    Texture* texture_2 = new Texture();
    texture_2->Create2D(128,128,GL_RGBA8);
    comp_shader->Setint("pattern",1);
    //glBindImageTexture(0, texture_2->texture_id, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindImageTexture(1, texture_2->texture_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
    glDispatchCompute(texture_2->width, texture_2->height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    glBindTextureUnit(0, texture->texture_id);
    glBindTextureUnit(1, texture_2->texture_id);



    //Create a material
    material_t m;
    m.color = vec4(0,1,0,1);
    m.texture_unit = 0;
    materials.push_back(m);
    m.color = vec4(1,0.5,0,1);
    m.texture_unit = 1;
    materials.push_back(m);
    debug->Info("We have %i materials\n",materials.size());


    return true;
}

//For now, we simple render all objects.
void Renderer::CullObjects(){
    renderable_objects.clear();
    for (Object* object:objects){
        renderable_objects.push_back(object);
    }
}

void Renderer::RebuildUniqueMeshList(){
    unique_meshes.clear();

    debug->Trace("Rebuilding unique list. batch_ids.size() = %i unique_mesh.size()=%i\n",batch_ids.size(),unique_meshes.size());
    for (Object* object:renderable_objects){
        bool new_mesh = true;
        for (Mesh* mesh:unique_meshes){
            if (mesh->GetID() == object->GetMeshID()){
                //We already have this same mesh.
                object->SetMeshBatchIndex(mesh->batch_index); //Copy the index from the already batched mesh.
                new_mesh = false;
                break;
            }
        }
        //Add this new mesh to our unique list.
        if (new_mesh){
            object->SetMeshBatchIndex(unique_meshes.size()); //Store the index in this array
            unique_meshes.push_back(object->GetMesh());
            if (batch_ids.size() < (object->GetMeshBatchIndex()+1)){
                batch_ids.push_back(new std::vector<objectid_t>());
            }
        }
    }

}

//Clear all previous batches
void Renderer::ClearBatches(){
    for (int i = 0;i<unique_meshes.size();i++){
        batch_ids.at(i)->clear();
    }
}

void Renderer::FillBactches(){
    //This should mark all meshes that need to for render, and have uploaded their data.
    debug->Trace("objects.size() = %i\n",objects.size());
    debug->Trace("batch_ids.size() = %i\n",batch_ids.size());

    //Reset stats
    int num_rendered_objects = 0;
    int num_rendered_triangles = 0;

    for (int32_t object_index=0;object_index<renderable_objects.size();object_index++){
        Object* object = renderable_objects.at(object_index);
        if (object->WouldRender()){
            object->MarkForRender();
            num_rendered_objects++;
            //It can only be rendered if it has a mesh
            int32_t mesh_index = object->GetMeshBatchIndex();
            debug->Trace("batch_ids.at(mesh_index=%i) mesh_id = %lu\n",mesh_index,object->GetMeshID());
            int32_t id = object_index;
            batch_ids.at(mesh_index)->push_back(id);
        }else{
            debug->Err("Object '%s' did not render while it should have.\n",object->name.c_str());
        }
    }
    debug->Trace("num_rendered_objects = %i\n",num_rendered_objects);
    if (num_rendered_objects == 0){
        debug->Info("Nothing to be rendered\n");
        return;
    }
}

//Each unique mesh gets a single drawcall with an associated SSBO with all object parameters per instance.
void Renderer::RenderUniqueMeshes(){
    for (int i = 0;i<unique_meshes.size();i++){
        instancedata.clear();
        Mesh* mesh = unique_meshes.at(i);
        int batch_index = unique_meshes.at(i)->batch_index;
        if (batch_ids.at(batch_index)->size() == 0){
            debug->Fatal("No batches for meshindex %i\n",batch_index);
        }
        for (uint32_t object_index : *batch_ids.at(batch_index)){
            Object* object = renderable_objects.at(object_index);
            debug->Trace("Object (mesh_index %i) obj_index: %lu object->GetID() %lu\n",batch_index,object_index,object->GetID());
            object->Rotate();
            instancedata_t data;
            data.mat_transformscale = object->GetWorldTransformScaleMatrix();
            for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
                data.material_slot[i] = object->material_slot[i];
            }
            //object->mat_rotation.print();
            //data.mat_transformscale.print();
            instancedata.push_back(data);
        }
        glInvalidateBufferData(instdata_ssbo);
        glNamedBufferData(instdata_ssbo,instancedata.size()*sizeof(instancedata_t) , &instancedata.at(0),GL_DYNAMIC_DRAW);

        debug->Trace("Rendering %i instances of mesh->id %i\n",mesh->batch_num_instances,mesh->GetID());
        mesh->RenderInstances(mesh->batch_num_instances);
        mesh->batch_num_instances = 0;
    }
}

//This for now just uploads all the known materials to a SSBO... each frame.
//Might only need to do this once.
void Renderer::UploadMaterials(){

    glInvalidateBufferData(materialdata_ssbo);
    glNamedBufferData(materialdata_ssbo,materials.size()*sizeof(material_t) , &materials.at(0),GL_DYNAMIC_DRAW);
}

void Renderer::DrawObjects(){
    //First, we cull all objects we are sure of are not visible.
    //Then we make a list of all objects that need to be rendered.
    //Of those objects, we make a list for each unique mesh with object attributes and object ids.
    CullObjects();
    RebuildUniqueMeshList();
    ClearBatches();
    FillBactches();
    UploadMaterials();
    RenderUniqueMeshes();
}

void Renderer::DrawFrame(Shader* shader){
    //Select the mutisampled frambuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glViewport(0, 0, width, height);
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(msaa_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    glClearNamedFramebufferfv(msaa_fbo_id,GL_DEPTH,0,&depth);

    shader->Use();
    shader->Setmat4("mat_worldcam",camera->mat_cam);

    DrawObjects();

    ResolveAA();
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);
}

//Create the required Shader Storage Buffer
bool Renderer::InitSSBO(){
    glCreateBuffers(1, (GLuint*)&instdata_ssbo);
    //glNamedBufferStorage(instdata_ssbo, 0 , NULL, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferData(instdata_ssbo, 0 , NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instdata_ssbo);

    //A buffer for the materials
    glCreateBuffers(1, (GLuint*)&materialdata_ssbo);
    glNamedBufferData(materialdata_ssbo, 0 , NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, materialdata_ssbo);
    return true;
}

//Create all the frame and renderbuffers
bool Renderer::InitFBO(){
    glCreateFramebuffers(1, &msaa_fbo_id);
    glCreateRenderbuffers(1, &color_rbo_id);
    glCreateRenderbuffers(1, &depth_rbo_id);

    glCreateFramebuffers(1, &resolve_fbo_id);
    glCreateRenderbuffers(1, &resolve_rbo_id);

    if (msaa_fbo_id == -1){
        return false;
    }

    int aa_samples = 16;

    //Setup buffers:
    //Mutisampled color 16bit float
    glNamedRenderbufferStorageMultisample(color_rbo_id, aa_samples, GL_RGBA16F, width, height);
    glNamedFramebufferRenderbuffer(msaa_fbo_id, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rbo_id);
    CheckFrameBuffer();

    //32-bit depth
    glNamedRenderbufferStorageMultisample(depth_rbo_id, aa_samples, GL_DEPTH_COMPONENT32F, width, height);
    glNamedFramebufferRenderbuffer(msaa_fbo_id, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_id);
    CheckFrameBuffer();

    //The resolve buffer
    glNamedRenderbufferStorage(resolve_rbo_id, GL_RGBA16F, width, height);
    glNamedFramebufferRenderbuffer(resolve_fbo_id, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve_rbo_id);
    CheckFrameBuffer();
    return true;
}

//Blit all multisampled buffer back to main/resolve buffers
void Renderer::ResolveAA(){
    //Blit from multisampled buffer to main backbuffer = GL_COLOR_ATTACHMENT0
    glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa_fbo_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_id);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

//Returns true if the framebuffer checks OK.
bool Renderer::CheckFrameBuffer(){
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE){
        debug->Ok("GL_FRAMEBUFFER_COMPLETE\n");
        return true;
    }
    if (status == GL_FRAMEBUFFER_UNDEFINED)
        debug->Err("GL_FRAMEBUFFER_UNDEFINED\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER\n");
    else if (status == GL_FRAMEBUFFER_UNSUPPORTED)
        debug->Err("GL_FRAMEBUFFER_UNSUPPORTED\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE\n");
    else if (status == GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS)
        debug->Err("GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS\n");
    else
        debug->Err("glCheckFramebufferStatus UNKNOWN\n");
    return false;
}
