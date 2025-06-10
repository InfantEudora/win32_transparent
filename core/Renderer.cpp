#include "Renderer.h"

#include "Debug.h"
static Debugger* debug = new Debugger("Renderer",DEBUG_INFO);

Renderer::Renderer(int w, int h){
    width = w;
    height = h;
}

void Renderer::SetState(){
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
}

bool Renderer::Init(){
    //Get some info
    int r = 0;

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &r);
    debug->Info("GL_MAX_TEXTURE_SIZE = %i\n",r);

    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &r);
    debug->Info("GL_MAX_TEXTURE_IMAGE_UNITS = %i\n",r);

    if (!SetNumAASamples(16)){
        return false;
    }

    if ((pipeline == PIPELINE_DEFERRED) && !InitDeferredFBO()){
        return false;
    }

    if (!InitSSBO()){
        return false;
    }

    if (pipeline == PIPELINE_DEFERRED){
        deferred_shader = new Shader("shaders/default.vert","shaders/deferred.frag");
        ssao_compute_shader = new Shader();
        ssao_compute_shader->CreateComputeShader("shaders/ssao_compute.comp");
    }

    SetState();

    //We make intel happy with an empty VAO
    GLuint empty_vao = -1;
    glGenVertexArrays(1,&empty_vao);
    glBindVertexArray(empty_vao);

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glDebugMessageCallback(opengl_message_callback, nullptr);

    tmr_frame = new PerfTimer("Frame Time");
    return true;
}

//Called when a window is resized
bool Renderer::Resize(int new_width, int new_height){
    width = new_width;
    height = new_height;
    if (!RebuildMSAAFBO()){
        return false;
    }
    return true;
}

//TODO: This should iterate over sub objects as well.
void Renderer::GetAllVisibleSubLights(Light* light,std::vector<Light*>&lights){
    if (!light){
        return;
    }
    if (light->IsDestroyed()){
        return;
    }

    if (!light->IsVisible()){
        return;
    }

    lights.push_back(light);
    //Lights don't get to have lights as children...
}

//Put's all children and it's childrens children etc into a list
void Renderer::GetAllRenderableVisableSubObjects(Object* object,std::vector<Object*>&objects){
    if (!object){
        return;
    }

    if (object->IsDestroyed()){
        return;
    }

    if (!object->IsVisible()){
        return;
    }

    //This object is renderable
    if (object->GetMesh() != NULL){
        objects.push_back(object);
    }
    if (object->GetSkinnedMesh() != NULL){
        objects.push_back(object);
    }

    //We check all the children
    for (Object* child:object->children){
        GetAllRenderableVisableSubObjects(child,objects);
    }
}

//For now all objects are rendered when visible
void Renderer::CullObjects(){
    renderable_objects.clear();
    for (Object* object:objects){
        if (object->IsVisible()){
            GetAllRenderableVisableSubObjects(object,renderable_objects);
        }
    }
}

//Updates all the materials that need to be picked from the objects that need to be rendered.
void Renderer::UpdateObjectMaterials(){
    for (Object* object:renderable_objects){
        object->UpdateMaterials(materials);
    }
}

//For now, we simple render all objects.
void Renderer::CullLights(){
    visible_lights.clear();
    for (Object* object:objects){
        Light* light = dynamic_cast<Light*>(object);
        if (light){
            GetAllVisibleSubLights(light,visible_lights);
        }
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
    unique_skinned_meshes.clear();

    debug->Trace("Rebuilding unique list. unique_mesh_batches.size() = %i unique_mesh.size()=%i\n",unique_mesh_batches.size(),unique_meshes.size());
    for (Object* object:renderable_objects){
        if (object->GetMesh()){
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
                Mesh* mesh = object->GetMesh();
                if (mesh){
                    unique_meshes.push_back(mesh);
                }
                if (unique_mesh_batches.size() < (object->GetMeshBatchIndex()+1)){
                    unique_mesh_batches.push_back(new std::vector<objectid_t>());
                }
            }
        }
        if (object->GetSkinnedMesh()){

        }

    }

}

//Clear all previous batches
void Renderer::ClearBatches(){
    for (int i = 0;i<unique_meshes.size();i++){
        unique_mesh_batches.at(i)->clear();
    }
}

void Renderer::FillBactches(){
    //This should mark all meshes that need to for render, and have uploaded their data.
    debug->Trace("objects.size() = %i\n",objects.size());
    debug->Trace("unique_mesh_batches.size() = %i\n",unique_mesh_batches.size());

    //Reset stats
    int num_rendered_objects = 0;
    int num_rendered_triangles = 0;

    for (int32_t object_index=0;object_index<renderable_objects.size();object_index++){
        Object* object = renderable_objects.at(object_index);
        if (object->GetMesh()){
            object->MarkForRender();
            num_rendered_objects++;
            //It can only be rendered if it has a mesh
            int32_t mesh_index = object->GetMeshBatchIndex();
            debug->Trace("unique_mesh_batches.at(mesh_index=%i) mesh_id = %lu\n",mesh_index,object->GetMeshID());
            int32_t id = object_index;
            unique_mesh_batches.at(mesh_index)->push_back(id);
        }/*else{
            debug->Err("Object '%s' did not render while it should have.\n",object->name.c_str());
        }*/
    }
    debug->Trace("num_rendered_objects = %i\n",num_rendered_objects);
    if (num_rendered_objects == 0){
        //debug->Info("Nothing to be rendered\n");
        return;
    }
}

//Each unique mesh gets a single drawcall with an associated SSBO with all object parameters per instance.
void Renderer::RenderUniqueMeshes(){
    //debug->Info("Rendering Meshes\n");
    for (int i = 0;i<unique_meshes.size();i++){
        instancedata.clear();
        Mesh* mesh = unique_meshes.at(i);
        if (!mesh){
            debug->Fatal("Attempting to render a mesh that's NULL\n");
        }
        int batch_index = unique_meshes.at(i)->batch_index;
        if (unique_mesh_batches.at(batch_index)->size() == 0){
            debug->Fatal("No batches for meshindex %i\n",batch_index);
        }
        for (uint32_t object_index : *unique_mesh_batches.at(batch_index)){
            Object* object = renderable_objects.at(object_index);
            debug->Trace("Object (mesh_index %i) obj_index: %lu object->GetID() %lu\n",batch_index,object_index,object->GetID());

            instancedata_t data;
            data.mat_transformscale = object->GetWorldTransformScaleMatrix();
            for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
                data.material_slot[i] = object->material_slot[i];
            }
            if (object->IsPickable()){
                data.objectindex = object_index;
            }else{
                //TODO: This will overwrite in the ID buffer any objects below the non-pickable object.
                data.objectindex = OBJECTID_INVALID;
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


//We are lazy and for now we only render a single skinned mesh, which is the first object that has one.
void Renderer::RenderUniqueSkinnedMeshes(){

    debug->Info("Rendering %i Skinned Meshes\n",unique_skinned_meshes.size());

    uint32_t object_index = -1;
    for (Object* object:renderable_objects){
        //TODO: Have mulltiple skinned meshes render at different animation states.
        object_index++;
        SkinnedMesh* skinned_mesh = object->GetSkinnedMesh();
        if (!skinned_mesh){
            continue;
        }
        //We have one
        debug->Trace("Rendering the first skinned mesh we found in object: %s\n",object->name.c_str());
        skinned_mesh->batch_num_instances = 1; //We render just one.

        //We are probably a skeleton then
        Skeleton* skeleton = dynamic_cast<Skeleton*>(object);
        if (!skeleton){
            debug->Warn("Skinned mesh does not appear to be a skeleton...\n");
            continue;
        }
        debug->Trace("Skeleton contains %i bones\n",skeleton->num_bones);
        instancedata.clear();

        instancedata_t data;
        data.mat_transformscale = object->GetWorldTransformScaleMatrix();
        for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
            data.material_slot[i] = object->material_slot[i];
        }
        if (object->IsPickable()){
            data.objectindex = object_index; //Index in array renderable_objects
        }else{
            //TODO: This will overwrite any objects below the non-pickable object.
            data.objectindex = OBJECTID_INVALID;
        }
        instancedata.push_back(data);
        glInvalidateBufferData(instdata_ssbo);
        glNamedBufferData(instdata_ssbo,instancedata.size()*sizeof(instancedata_t) , &instancedata.at(0),GL_DYNAMIC_DRAW);

        //Now we build a buffer holding all the bone data for this mesh
        boneinstancedata.clear();
        bonedata_t bonedata;
        bonedata.mat_transformscale = fmat4().identity();

        std::vector<Bone*>bones;
        skeleton->GetAllBones(skeleton,bones);
        if (bones.size() != skeleton->num_bones){
            debug->Err("skeleton->GetAllBones() did not yield expected number of bones (%i vs %i)\n",bones.size(),skeleton->num_bones);
        }

        //We add however many bones we want / have
        int num_bones = skeleton->num_bones;
        for (int i=0;i<num_bones;i++){
            bonedata.mat_inversebind = bones.at(i)->inverse_bind_matrix;
            bonedata.mat_transformscale = bones.at(i)->GetWorldTransformScaleMatrix();

            bones.at(i)->bone_unpacked_index = i;
            boneinstancedata.push_back(bonedata);
        }
        glInvalidateBufferData(boneinstdata_ssbo);
        glNamedBufferData(boneinstdata_ssbo,boneinstancedata.size()*sizeof(bonedata_t) , &boneinstancedata.at(0),GL_DYNAMIC_DRAW);

        //And render all the shize
        debug->Trace("Rendering %i instances of skinned_mesh->id %i\n",skinned_mesh->batch_num_instances,skinned_mesh->GetID());
        skinned_mesh->RenderInstances(skinned_mesh->batch_num_instances);
        skinned_mesh->batch_num_instances = 0;

        break;
    }
}

void Renderer::RenderDebugLines(){

}

//Set's the SSBO that will be used for reading back data
void Renderer::UpdateReadbackBuffer(){
    readbackbuffer.data_out[0] = -1;
    glInvalidateBufferData(readback_ssbo);
    glNamedBufferData(readback_ssbo,sizeof(readback_buffer_t), &readbackbuffer,GL_DYNAMIC_DRAW);
}

//Requires a skybox shader and skybox to have been set.
void Renderer::DrawSkyBox(Camera* camera){
    if (f_render_skybox && skybox && skybox_shader && skybox_mesh){
        skybox_shader->Use();
        skybox_shader->Setmat4("mat_worldcam",camera->GetPositionlessMatrix());
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        skybox_mesh->RenderInstances(1);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
    }
}

void Renderer::DrawStaticObjects(){
    //First, we cull all objects we are sure of are not visible.
    //Then we make a list of all objects that need to be rendered.
    //Of those objects, we make a list for each unique mesh with object attributes and object ids.
    CullObjects();
    CullLights();
    UpdateState();
    UpdateObjectMaterials();

    RebuildUniqueMeshList();
    ClearBatches();
    FillBactches();
    UploadMaterials();
    UploadLights();
    RenderUniqueMeshes();
}

//We'll be using a seperate shader
void Renderer::DrawSkinnedObjects(){
    //Now we want to render all the objects that have skinned meshes
    RenderUniqueSkinnedMeshes();
}

void Renderer::DeferredPass(Camera* camera){
    //Select the deferred framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, deferred_fbo_id);

    deferred_shader->Use();
    deferred_shader->Setmat4("mat_worldcam",camera->mat_cam);

    unsigned int attachments[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glNamedFramebufferDrawBuffers(deferred_fbo_id,2, attachments);

    //Viewport and clear
    glViewport(0, 0, width, height);
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(deferred_fbo_id,GL_DEPTH,0,&depth);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    clr_clear = vec4(1,0,0,0);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,1,(float*)&clr_clear);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,2,(float*)&clr_clear);

    //UpdateReadbackBuffer();

    DrawStaticObjects();

    //glGetNamedBufferSubData(readback_ssbo, 0, sizeof(readback_buffer_t), &readbackbuffer);
    //debug->Info("Read back %i x %i = %i, %i Depth=%.7f\n",readbackbuffer.data_in[0],readbackbuffer.data_in[1],readbackbuffer.data_out[0],readbackbuffer.data_out[1],readbackbuffer.fdata_out[0]);

    glFinish();
    //We have deferred bound...
}

//Uses a compute shader and uses the textures from deferred pass.
void Renderer::SSAOPass(Camera* camera){
    ssao_compute_shader->Use();
    glBindImageTexture(0, ssao_tex_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindTextureUnit(0, deferred_position_tex_id);
    glBindTextureUnit(1, deferred_normal_tex_id);
    glBindTextureUnit(2, resolve_tex_id);

    ssao_compute_shader->Setmat4("mat_worldcam",camera->mat_cam);

    glDispatchCompute(width, height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //Set this as output buffer
    glBindFramebuffer(GL_FRAMEBUFFER, deferred_fbo_id);
    glReadBuffer(GL_COLOR_ATTACHMENT2);
}

void Renderer::DrawFrame(Camera* camera, Shader* shader, InputController* input){
    if (tmr_frame){
        tmr_frame->Stop();
        double dt = tmr_frame->GetdtUs();
        tmr_frame->Restart();
    }

    //Select the mutisampled framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    //Viewport and clear
    glViewport(0, 0, width, height);
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(msaa_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    glClearNamedFramebufferfv(msaa_fbo_id,GL_DEPTH,0,&depth);

    //Skybox
    DrawSkyBox(camera);

    if (shader && camera){
        shader->Use();
        vec3 p = camera->GetPosition();
        shader->Setvec3("eye_position",p);
        shader->Setmat4("mat_worldcam",camera->mat_cam);
        shader->Setint("f_normal_mapping",(int)f_normal_mapping);
        shader->Setfloat("alpha_clip",alpha_clip);
    }

    if (input){
        int2 mouse = {-1,-1};
        if (input){
            mouse = input->GetRelativeMousePosition();
        }
        readbackbuffer.data_in[0] = mouse.x;
        readbackbuffer.data_in[1] = height - mouse.y;
        readbackbuffer.fdata_out[0] = 1.0f;
        UpdateReadbackBuffer();
    }

    DrawStaticObjects();

    if (skinned_shader && camera){
        skinned_shader->Use();
        vec3 p = camera->GetPosition();
        skinned_shader->Setvec3("eye_position",p);
        skinned_shader->Setmat4("mat_worldcam",camera->mat_cam);
        skinned_shader->Setint("f_normal_mapping",(int)f_normal_mapping);
        shader->Setfloat("alpha_clip",alpha_clip);
        DrawSkinnedObjects();
    }



    ResolveAA();

    if (input){
        //Read back buffer contents
        glGetNamedBufferSubData(readback_ssbo, 0, sizeof(readback_buffer_t), &readbackbuffer);
        //debug->Info("Read back %i x %i = %i, %i\n",readbackbuffer.data_in[0],readbackbuffer.data_in[1],readbackbuffer.data_out[0],readbackbuffer.data_out[1]);
        if(readbackbuffer.data_out[0] != -1){
            //debug->Info("Read back %i x %i = %i, %i Depth=%.7f\n",readbackbuffer.data_in[0],readbackbuffer.data_in[1],readbackbuffer.data_out[0],readbackbuffer.data_out[1],readbackbuffer.fdata_out[0]);
            int index = readbackbuffer.data_out[0];
            input->SetHoveredObjectID(renderable_objects.at(index)->GetID());
            //debug->Info("Normal at mouse = %.3f, %.3f, %.3f\n",readbackbuffer.fdata_out[1],readbackbuffer.fdata_out[2],readbackbuffer.fdata_out[3]);
            input->SetHoveredNormal(vec3(readbackbuffer.fdata_out[1],readbackbuffer.fdata_out[2],readbackbuffer.fdata_out[3]));
        }else{
            input->SetHoveredObjectID(OBJECTID_INVALID);
            input->SetHoveredNormal(vec3());
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);
    glFinish();

    //DeferredPass(camera);
    //SSAOPass(camera);
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

    //A buffer for all the lights
    glCreateBuffers(1, (GLuint*)&lights_ssbo);
    glNamedBufferData(lights_ssbo, 0 , NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lights_ssbo);

    //A buffer where we read back data from, mainly the object id at mouse coordinate.
    glCreateBuffers(1, (GLuint*)&readback_ssbo);
    glNamedBufferData(readback_ssbo, 0 , NULL, GL_DYNAMIC_DRAW);
    //glNamedBufferStorage(readback_ssbo, sizeof(readback_buffer_t), &readbackbuffer, GL_DYNAMIC_STORAGE_BIT);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, readback_ssbo);

    //A buffer for storing all the bone data for skinned meshes
    glCreateBuffers(1, (GLuint*)&boneinstdata_ssbo);
    glNamedBufferData(boneinstdata_ssbo, 0 , NULL, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, boneinstdata_ssbo);

    return true;
}

bool Renderer::SetNumAASamples(int desired){
    int max_samples;
	glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
    debug->Info("GL_MAX_SAMPLES=%i\n",max_samples);

    aa_samples = desired;
    if (aa_samples > max_samples){
        aa_samples = max_samples;
        debug->Info("Limited desired number of AA Samples from %i to %i\n",desired,aa_samples);
    }else{
         debug->Info("Number of AA Samples set to %i\n",aa_samples);
    }

    //After this, we need some buffers rebuilt:
    if (!RebuildMSAAFBO()){
        return false;
    }
    return true;
}

bool Renderer::InitDeferredFBO(){
    debug->Info("Creating buffers for deferred stage\n");
    glCreateFramebuffers(1, &deferred_fbo_id);

    //Color buffer for object position 32-bit... 16?
    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_position_tex_id);
    glTextureStorage2D(deferred_position_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(deferred_position_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_position_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT0, deferred_position_tex_id, 0);
    CheckFrameBuffer();

    //Normals for objects.
    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_normal_tex_id);
    glTextureStorage2D(deferred_normal_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(deferred_normal_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_normal_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT1, deferred_normal_tex_id, 0);
    CheckFrameBuffer();

    //32-bit depth
    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_depth_tex_id);
    glTextureStorage2D(deferred_depth_tex_id, 1, GL_DEPTH_COMPONENT32F, width, height);
    glTextureParameteri(deferred_depth_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_depth_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_DEPTH_ATTACHMENT, deferred_depth_tex_id, 0);
    CheckFrameBuffer();

    //We also generate a texture for the SSAO output, and attach it to the deferred FBO.
    glCreateTextures(GL_TEXTURE_2D, 1, &ssao_tex_id);
    glTextureStorage2D(ssao_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(ssao_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(ssao_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT2, ssao_tex_id, 0);
    CheckFrameBuffer();

    return true;
}

//Create all the frame and renderbuffers for mulisampling
// A multisampled color and depth buffer, and a resolve buffer.
bool Renderer::RebuildMSAAFBO(){
    if (msaa_fbo_id == -1){
        glCreateFramebuffers(1, &msaa_fbo_id);
    }
    if (color_rbo_id == -1){
        glCreateRenderbuffers(1, &color_rbo_id);
    }
    if (depth_rbo_id == -1){
        glCreateRenderbuffers(1, &depth_rbo_id);
    }
    if (resolve_fbo_id == -1){
        glCreateFramebuffers(1, &resolve_fbo_id);
    }
    if (msaa_fbo_id == -1){
        return false;
    }

    //Setup buffers:
    //Mutisampled color 16bit float
    glNamedRenderbufferStorageMultisample(color_rbo_id, aa_samples, GL_RGBA16F, width, height);
    glNamedFramebufferRenderbuffer(msaa_fbo_id, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rbo_id);
    CheckFrameBuffer();

    //32-bit depth
    glNamedRenderbufferStorageMultisample(depth_rbo_id, aa_samples, GL_DEPTH_COMPONENT32F, width, height);
    glNamedFramebufferRenderbuffer(msaa_fbo_id, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_id);
    CheckFrameBuffer();

    //The resolve buffer is texture backed
    if (resolve_tex_id == -1){
        glCreateTextures(GL_TEXTURE_2D, 1, &resolve_tex_id);
    }else{
        glDeleteTextures(1, &resolve_tex_id);
        glCreateTextures(GL_TEXTURE_2D, 1, &resolve_tex_id);
    }
    debug->Info("Resolve Texture ID: %i\n",resolve_tex_id);
    glTextureStorage2D(resolve_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(resolve_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(resolve_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(resolve_fbo_id, GL_COLOR_ATTACHMENT0, resolve_tex_id, 0);
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

void opengl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, char const* message, void const* user_param){
    if (severity != GL_DEBUG_SEVERITY_HIGH){
        return;
    }
	const char* src_str = [source]() {
		switch (source)
		{
		case GL_DEBUG_SOURCE_API: return "API";
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM: return "WINDOW SYSTEM";
		case GL_DEBUG_SOURCE_SHADER_COMPILER: return "SHADER COMPILER";
		case GL_DEBUG_SOURCE_THIRD_PARTY: return "THIRD PARTY";
		case GL_DEBUG_SOURCE_APPLICATION: return "APPLICATION";
		case GL_DEBUG_SOURCE_OTHER: return "OTHER";
		}
        return "";
	}();

	const char* type_str = [type]() {
		switch (type)
		{
		case GL_DEBUG_TYPE_ERROR: return "ERROR";
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED_BEHAVIOR";
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: return "UNDEFINED_BEHAVIOR";
		case GL_DEBUG_TYPE_PORTABILITY: return "PORTABILITY";
		case GL_DEBUG_TYPE_PERFORMANCE: return "PERFORMANCE";
		case GL_DEBUG_TYPE_MARKER: return "MARKER";
		case GL_DEBUG_TYPE_OTHER: return "OTHER";
		}
        return "";
	}();

	const char* severity_str = [severity]() {
		switch (severity) {
		case GL_DEBUG_SEVERITY_NOTIFICATION:    return "NOTIFICATION";
		case GL_DEBUG_SEVERITY_LOW:             return "LOW";
		case GL_DEBUG_SEVERITY_MEDIUM:          return "MEDIUM";
		case GL_DEBUG_SEVERITY_HIGH:            return "HIGH";
		}
        return "";
	}();
    debug->Warn("GL_%s from %s: %s\n",severity_str,  src_str,message);
}

void Renderer::SetVSync(bool enable){
    if (wglSwapIntervalEXT){
        wglSwapIntervalEXT(enable);
        if (enable){
            debug->Ok("VSync: Enabled\n");
        }else{
            debug->Ok("VSync: Disabled\n");
        }
    }
}

//Add's materials to global list, omitting duplicates by name. Returns the index where the material was added.
int Renderer::AddMaterial(Material& newmat){
    bool isnew = true;
    for (Material& mat:materials){
        if (newmat.name.compare(mat.name) == 0){
            debug->Info("Already have material %s\n",mat.name.c_str());
            isnew = false;
            break;
        }
    }
    if (isnew){
        materials.push_back(newmat);
    }
    return materials.size() - 1;
}

//Add's materials to global list, omitting duplicates by name.
void Renderer::AddMaterials(std::vector<Material>& list){
    for (Material& newmat:list){
        int index = AddMaterial(newmat);
    }
}

int Renderer::GetNumMaterials(){
    return materials.size();
}

//This for now just uploads all the known materials to a SSBO... each frame.
//Might only need to do this once.
void Renderer::UploadMaterials(){
    last_texture_unit = 0;

    glsl_materials.clear();
    for (Material& mat:materials){
        if (mat.diff_texture){;
            //debug->Trace("Material has diffuse Texture: Binding to Unit %i\n",texture_unit);
            mat.glsl_material.diffuse_texture = last_texture_unit;
            glBindTextureUnit(last_texture_unit, mat.diff_texture->texture_id);
            last_texture_unit++;
        }
        if (mat.norm_texture){;
            //debug->Trace("Material has normal Texture: Binding to Unit %i\n",texture_unit);
            mat.glsl_material.normal_texture = last_texture_unit;
            glBindTextureUnit(last_texture_unit, mat.norm_texture->texture_id);
            last_texture_unit++;
        }
        glsl_materials.push_back(mat.glsl_material);
    }

    if (glsl_materials.size() > 0){
        glInvalidateBufferData(materialdata_ssbo);
        glNamedBufferData(materialdata_ssbo,glsl_materials.size()*sizeof(material_t) , &glsl_materials.at(0),GL_DYNAMIC_DRAW);
    }
}

void Renderer::UploadCubeMap(CubeMap* cubemap){
    //We upload each of the
    for (int i = 0;i<6;i++){
        if (cubemap->texture[i]){
            debug->Info("Loading CubeMap %i/6 : %s to texture_unit %i\n",i,cubemap->texture[0]->name.c_str(),last_texture_unit);
            glBindTextureUnit(last_texture_unit, cubemap->texture[i]->texture_id);
            last_texture_unit++;
        }
    }
}

//Convert all the active lights in the scene to a list
void Renderer::UploadLights(){
    glsl_lights.clear();
    for (Light* l:visible_lights){
        light_t light;
        DirectionalLight* directional_light = dynamic_cast<DirectionalLight*>(l);
        if (directional_light){
            light.direction = directional_light->GetForward();
            light.position = directional_light->GetWorldPosition();
            light.color = directional_light->color;
            light.brightness = directional_light->brightness;
            glsl_lights.push_back(light);
            continue;
        }
        PointLight* point_light = dynamic_cast<PointLight*>(l);
        if (point_light){
            light.direction = vec3(0,0,0);
            light.position = point_light->GetWorldPosition();
            light.color = point_light->color;
            light.brightness = point_light->brightness;
            glsl_lights.push_back(light);
            continue;
        }
    }

    if (glsl_lights.size() > 0){
        glInvalidateBufferData(lights_ssbo);
        glNamedBufferData(lights_ssbo,glsl_lights.size()*sizeof(light_t) , &glsl_lights.at(0),GL_DYNAMIC_DRAW);
    }
}

Material* Renderer::GetMaterial(int index){
    if ((index >= materials.size()) || (index < 0) || (materials.size() == 0)){
        //Return an invalid material
        return NULL;
    }
    return &materials.at(index);
}

//Returns the material index in material list based on supplied name
int Renderer::FindMaterialIndex(const std::string& name){
    for (int index=0;index<materials.size();index++){
        Material& mat = materials.at(index);
        if (mat.name.compare(name) == 0){
            return index;
        }
    }
    return -1;
}

//Load a texture from file, and returns the OpenGL handle/id-thing
Texture* Renderer::LoadTexture(const char* filename, int target, int depth){
    //A material with a texture.
    Texture* texture = new Texture();
    texture->LoadFromFile(filename,target,depth);
    //glBindTextureUnit(0, texture->texture_id);
    return texture;
}

//A Test for only rendering a texture to screen which we can upload first.
void Renderer::RenderResolveTextureOnly(){
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);
    glFinish();
}