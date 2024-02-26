#include "Renderer.h"

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

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);



    return true;
}

//For now, we simple render all objects.
void Renderer::CullObjects(){
    renderable_objects.clear();
    for (Object* object:objects){
        if (object->GetMesh() == NULL){
            continue;
        }

        renderable_objects.push_back(object);
    }
}

//This checks if all objects we are interested in have completed.
void Renderer::UpdateState(){
    bool all_completed = true;
    for (Object* object:objects){
        if (!object->PhysicsCompleted()){
            all_completed = false;
            break;
        }
    }

    if (!all_completed){
        //debug->Warn("Not all objects have complete state_physics_prev\n");
        //Physics is still modifying the current and/or previous state.
        //We are rendering faster than physics.
        //Draw the current state.
        return;
    }

    //We take the completed state, copy it over and mark it as incomplete.
    //Physics is allowed to swap when a state is incomplete.
    for (Object* object:objects){
        //This was previously tested, and it's now broken....?
        if (!object->PhysicsCompleted()){
            debug->Fatal("Previously set complete state now incomplete.\n");
        }
        //Copies object state and invalidates physics state
        object->UpdateState();

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

            instancedata_t data;
            data.mat_transformscale = object->GetWorldTransformScaleMatrix();
            for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
                data.material_slot[i] = object->material_slot[i];
            }
            data.objectindex = object_index;
            //This is not really the place to do this... TODO
            if (object->IsHovered()){
                data.material_slot[1] = data.material_slot[0];
                data.material_slot[0] = 2;
            }else{
                data.material_slot[0] = data.material_slot[1];
            }
            object->SetMouseOver(false);
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

//Set's the SSBO that will be used for reading back data
void Renderer::UpdateReadbackBuffer(){
    readbackbuffer.data_out[0] = -1;
    glNamedBufferSubData(readback_ssbo,0,sizeof(readback_buffer_t), &readbackbuffer);
}

void Renderer::DrawObjects(){
    //First, we cull all objects we are sure of are not visible.
    //Then we make a list of all objects that need to be rendered.
    //Of those objects, we make a list for each unique mesh with object attributes and object ids.
    CullObjects();
    UpdateState();

    RebuildUniqueMeshList();
    ClearBatches();
    FillBactches();
    UploadMaterials();
    RenderUniqueMeshes();
}

void Renderer::DrawFrame(Camera* camera, Shader* shader, int mousex, int mousey){
    //Select the mutisampled frambuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glViewport(0, 0, width, height);
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(msaa_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    glClearNamedFramebufferfv(msaa_fbo_id,GL_DEPTH,0,&depth);

    shader->Use();
    shader->Setvec3("eye_position",camera->GetPosition());
    shader->Setmat4("mat_worldcam",camera->mat_cam);

    readbackbuffer.data_in[0] = mousex;
    readbackbuffer.data_in[1] = height - mousey;
    readbackbuffer.fdata_out[0] = 1.0f;
    UpdateReadbackBuffer();

    DrawObjects();
    ResolveAA();

    //Read back buffer contents
    glGetNamedBufferSubData(readback_ssbo, 0, sizeof(readback_buffer_t), &readbackbuffer);
    //debug->Info("Read back %i x %i = %i, %i\n",readbackbuffer.data_in[0],readbackbuffer.data_in[1],readbackbuffer.data_out[0],readbackbuffer.data_out[1]);
    if(readbackbuffer.data_out[0] != -1){
        debug->Info("Read back %i x %i = %i, %i Depth=%.7f\n",readbackbuffer.data_in[0],readbackbuffer.data_in[1],readbackbuffer.data_out[0],readbackbuffer.data_out[1],
        readbackbuffer.fdata_out[0]);
        int index = readbackbuffer.data_out[0];
        renderable_objects.at(index)->SetMouseOver(true);
    }

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

    //A buffer where we read back data from, mainly the object id at mouse coordinate.
    glCreateBuffers(1, (GLuint*)&readback_ssbo);
    glNamedBufferStorage(readback_ssbo, sizeof(readback_buffer_t), &readbackbuffer, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, readback_ssbo);

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
