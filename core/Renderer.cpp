#include "Renderer.h"

#include "Debug.h"

#define DEFAULT_FRAMEBUFFER_ID  0

static Debugger* debug = new Debugger("Renderer",DEBUG_ERROR);

Renderer::Renderer(int w, int h){
    width = w;
    height = h;
}

void Renderer::SetOpenGLState(){
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
}

bool Renderer::Init(int _pipeline){
    pipeline = _pipeline;
    //Get some info
    int r = 0;
    int x,y,z;


    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &r);
    debug->Info("GL_MAX_VERTEX_ATTRIBS = %i\n",r);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &r);
    debug->Info("GL_MAX_TEXTURE_SIZE = %i\n",r);

    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &r);
    debug->Info("GL_MAX_TEXTURE_IMAGE_UNITS = %i\n",r);

    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &r);
    debug->Info("GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS = %i\n",r);

    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,0, &x);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,1, &y);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT,2, &z);
    debug->Info("GL_MAX_COMPUTE_WORK_GROUP_COUNT = x=%i y=%i z=%i\n",x,y,z);

    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,0, &x);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,1, &y);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE,2, &z);
    debug->Info("GL_MAX_COMPUTE_WORK_GROUP_SIZE = x=%i y=%i z=%i\n",x,y,z);

    if (!SetNumAASamples(16)){
        return false;
    }

    if ((pipeline == PIPELINE_DEFERRED) && !RebuildDeferredFBO()){
        return false;
    }

    if (!InitSSBO()){
        return false;
    }

    //If we do shadows
    if (!RebuildShadowFBO(shadow_texture_size,shadow_texture_size)){
        return false;
    }

    if (pipeline == PIPELINE_DEFERRED){
        deferred_shader = new Shader("shaders/default.vert","shaders/deferred.frag");
        deferred_shader_skinned = new Shader("shaders/default_skinned.vert","shaders/deferred.frag");
        ssao_compute_shader = new Shader();
        ssao_compute_shader->CreateComputeShader("shaders/ssao_compute.comp");
    }

    SetOpenGLState();

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
    if ((pipeline == PIPELINE_DEFERRED) && !RebuildDeferredFBO()){
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
        //return;
    }

    //We take the completed state, copy it over and mark it as incomplete.
    //Physics is allowed to swap when a state is incomplete.
    for (Object* object:objects){
        //This was previously tested, and it's now broken....?
        if (!object->PhysicsCompleted()){
            //debug->Fatal("Previously set complete state now incomplete.\n");
        }
        //Copies object state and invalidates physics state
        object->UpdateState();

    }
}

void Renderer::RebuildUniqueMeshList(){
    unique_meshes.clear();

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
    }
}

//Clear all previous batches
void Renderer::ClearBatches(){
    for (int i = 0;i<unique_meshes.size();i++){
        unique_mesh_batches.at(i)->clear();
    }
}

//Called when all rendering has finished and a new batch should be started
void Renderer::ClearObjectBatches(){
    for (Object* object:renderable_objects){
        object->ClearRenderBatch();
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
            object->MarkForRenderBatch();
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
void Renderer::RenderUniqueMeshes(int rendering_mode){
    debug->Trace("Rendering Meshes rendering_mode = %i\n",rendering_mode);
    for (int i = 0;i<unique_meshes.size();i++){
        instancedata.clear();
        boneinstancedata.clear();

        Mesh* mesh = unique_meshes.at(i);
        if (!mesh){
            debug->Fatal("Attempting to render a mesh that's NULL\n");
        }
        if ((rendering_mode == MESH_MODE_NORMAL) && (!mesh->IsNormalMesh())){
            continue;
        }
        if ((rendering_mode == MESH_MODE_SKINNED) && (!mesh->IsSkinnedMesh())){
            continue;
        }
        if ((rendering_mode == MESH_MODE_LINE) && (!mesh->IsLineMesh())){
            continue;
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

            data.num_vertices = mesh->num_vertices;
            data.num_morph_targets = mesh->num_morph_targets;
            for (int i=0;i<NUM_MORPH_FACTOR_SLOTS;i++){
                data.morph_factors[i] = object->morph_factors[i];
            }

            //object->mat_rotation.print();
            //data.mat_transformscale.print();

            if (rendering_mode == MESH_MODE_SKINNED){
                bonedata_t bonedata;
                bonedata.mat_transformscale = fmat4().identity();

                //It has been previously established we are skinned mesh
                Skeleton* skeleton = dynamic_cast<Skeleton*>(object);
                if (!skeleton){
                    debug->Warn("Skinned mesh does not appear to be a skeleton...\n");
                    continue;
                }

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
                data.num_bones = num_bones;
            }

            instancedata.push_back(data);

        }
        //glInvalidateBufferData(instdata_ssbo);
        glNamedBufferData(instdata_ssbo,instancedata.size()*sizeof(instancedata_t) , &instancedata.at(0),GL_STREAM_DRAW);

        if (rendering_mode == MESH_MODE_SKINNED){
            //glInvalidateBufferData(boneinstdata_ssbo);
            glNamedBufferData(boneinstdata_ssbo,boneinstancedata.size()*sizeof(bonedata_t) , &boneinstancedata.at(0),GL_STREAM_DRAW);
        }

        debug->Trace("Rendering %i instances of mesh->id %i\n",mesh->batch_num_instances,mesh->GetID());
        mesh->RenderInstances(mesh->batch_num_instances);
    }
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

void Renderer::PrepareObjects(){
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
}

void Renderer::DeferredPass(Camera* camera){
    if (pipeline != PIPELINE_DEFERRED){
        debug->Fatal("Called Deffered pass. Pipeline must be PIPELINE_DEFERRED\n");
    }
    //Select the deferred framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, deferred_fbo_id);

    deferred_shader->Use();
    deferred_shader->Setmat4("mat_worldcam",camera->mat_cam);

    unsigned int attachments[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT3};
    glNamedFramebufferDrawBuffers(deferred_fbo_id,3, attachments);

    //Viewport and clear
    glViewport(0, 0, width, height);
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(deferred_fbo_id,GL_DEPTH,0,&depth);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    clr_clear = vec4(1,0,0,0);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,1,(float*)&clr_clear);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,2,(float*)&clr_clear);
    GLint int_clear[4] = {-1,-1,-1,-1};
    glClearNamedFramebufferiv(deferred_fbo_id,GL_COLOR,3,(GLint*)&int_clear);


    //UploadMaterials();
    //UploadLights();
    RenderUniqueMeshes(MESH_MODE_NORMAL);

    if (deferred_shader_skinned && camera){
        deferred_shader_skinned->Use();
        //vec3 p = camera->GetPosition();
        //deferred_shader_skinned->Setvec3("eye_position",p);
        deferred_shader_skinned->Setmat4("mat_worldcam",camera->mat_cam);
        if (!deferred_shader_skinned->Setint("f_normal_mapping",(int)f_normal_mapping)){
            debug->Fatal("Could not set normal mapping int in deferred pass\n");
        };
        //deferred_shader_skinned->Setfloat("alpha_clip",alpha_clip);
        RenderUniqueMeshes(MESH_MODE_SKINNED);
    }
}

//Uses a compute shader and uses the textures from deferred pass.
void Renderer::SSAOPass(Camera* camera){
    ssao_compute_shader->Use();
    glBindImageTexture(0, ssao_tex_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindTextureUnit(0, deferred_position_tex_id);
    glBindTextureUnit(1, deferred_normal_tex_id);
    glBindTextureUnit(2, resolve_tex_id);

    ssao_compute_shader->Setmat4("mat_worldcam",camera->mat_cam);

    glDispatchCompute(width/32, height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void Renderer::RenderSingleDepthPass(Camera* camera,Shader* shader, int mesh_mode){
    if (!camera){
        debug->Err("Rendering Depth Pass with no camera\n");
        return;
    }
    //debug->Info("Rendering Depth Pass for Camera %s ID:%lu\n",camera->name.c_str(),camera->GetID());

    //TODO: We need to put this thing in its seperate tile in the texture.
    //For now, we use the entire texture

    // Setup view port.
    glViewport(0, 0, camera->viewport.width, camera->viewport.height);
    //It seems happy rendering with no color buffer attached

    camera->CalculateLookatMatrix();
    shader->Setmat4("mat_worldcam",camera->mat_cam);
    shader->Setmat4("mat_shadow",camera->mat_cam);

    RenderUniqueMeshes(mesh_mode);
}

void Renderer::ClearDepthPasses(){
    //We need to know for which light source we need to do the depth pass
    //Each shadow caster is a new pass
    //For now we're use the one sun.

    float depth = 1.0;
    glClearNamedFramebufferfv(shadow_fbo_id,GL_DEPTH,0,&depth);
    glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo_id);

    //We are only interested in back faces, so we cull front faces
    //glFrontFace(GL_CW);
    //glEnable(GL_CULL_FACE);
    //glCullFace(GL_FRONT);
}

void Renderer::RenderDepthPasses(Shader* shader, int mesh_mode){
    for (Light* l:visible_lights){
        light_t light;
        DirectionalLight* directional_light = dynamic_cast<DirectionalLight*>(l);
        if (directional_light){
            RenderSingleDepthPass(dynamic_cast<Camera*>(directional_light),shader,mesh_mode);
            return;
        }
    }
}

void Renderer::FinishDepthPasses(){
    //Reset
    //glCullFace(GL_BACK);
}

void Renderer::DrawFrame(Camera* camera, Shader* shader, InputController* input){
    if (!camera){
        debug->Fatal("DrawFrame called without camera.\n");
    }
    if (!shader){
        debug->Fatal("DrawFrame called without shader.\n");
    }
    if (tmr_frame){
        tmr_frame->Restart();
    }

    //TODO: Where/When to render Skybox. Re-Test
    //DrawSkyBox(camera);

    PrepareObjects();

    ClearDepthPasses();
    if (skinned_shader){
        skinned_shader->Use();
        vec3 p = camera->GetPosition(STATE_ACCESS_RENDERER);
        skinned_shader->Setvec3("eye_position",p);
        if (!skinned_shader->Setint("f_normal_mapping",(int)f_normal_mapping)){
            debug->Fatal("Could not set f_normal_mapping in skinned shader\n");
        }
        skinned_shader->Setfloat("alpha_clip",alpha_clip);
        skinned_shader->Setint("f_materialindex_is_color",1); //Abusing this to bypass everything
        RenderDepthPasses(shader,MESH_MODE_SKINNED);
    }

    //Depth pass with default shader.
    shader->Use();
    vec3 p = camera->GetPosition(STATE_ACCESS_RENDERER);
    shader->Setvec3("eye_position",p);
    if (!shader->Setint("f_normal_mapping",(int)f_normal_mapping)){
        debug->Fatal("Could not set f_normal_mapping in default shader\n");
    }
    shader->Setfloat("alpha_clip",alpha_clip);
    shader->Setint("f_materialindex_is_color",1); //Abusing this to bypass everything
    RenderDepthPasses(shader,MESH_MODE_NORMAL);

    FinishDepthPasses();

    glBindTextureUnit(0, shadow_tex_id);
    shader->Setmat4("mat_worldcam",camera->mat_cam);

    //Select the mutisampled framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    //Viewport and clear
    glViewport(0, 0, width, height);
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(msaa_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    glClearNamedFramebufferfv(msaa_fbo_id,GL_DEPTH,0,&depth);

    UploadMaterials();
    UploadLights();

    shader->Setint("f_materialindex_is_color",0);
    RenderUniqueMeshes(MESH_MODE_NORMAL);
    shader->Setint("f_materialindex_is_color",1);
    RenderUniqueMeshes(MESH_MODE_LINE);
    shader->Setint("f_materialindex_is_color",0);

    if (skinned_shader && camera){
        skinned_shader->Use();
        vec3 p = camera->GetPosition(STATE_ACCESS_RENDERER);
        skinned_shader->Setvec3("eye_position",p);
        skinned_shader->Setmat4("mat_worldcam",camera->mat_cam);
        if (!skinned_shader->Setint("f_normal_mapping",(int)f_normal_mapping)){
            debug->Fatal("Could not set f_normal_mapping in skinned shader\n");
        }
        skinned_shader->Setfloat("alpha_clip",alpha_clip);
        skinned_shader->Setint("f_materialindex_is_color",0);
        RenderUniqueMeshes(MESH_MODE_SKINNED);
    }

    ResolveAA();


    //We use a deferred pass for object ID, amongst many other things.
    //TODO: These passes need to be fixed... do we even want them?
    if (pipeline == PIPELINE_DEFERRED){
        DeferredPass(camera);
    }

    //Now we can read the normal and object ID:
    if ((pipeline == PIPELINE_DEFERRED && input)){
        glReadBuffer(GL_COLOR_ATTACHMENT3);
        int32_t id_pixeldata[4] = {-1,-1,-1,-1};
        float  normal_pixeldata[4] = {0,0,0,0};
        int2 mouse = {-1,-1};
        if (input){
            mouse = input->GetRelativeMousePosition();
        }
        glReadPixels(mouse.x,  height - mouse.y, 1, 1, GL_RED_INTEGER, GL_INT, id_pixeldata);
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(mouse.x,  height - mouse.y, 1, 1, GL_RGB, GL_FLOAT, normal_pixeldata);

        //Somehow, -1 reads back as 3F800000
        if ((id_pixeldata[0] != 0x3F800000) && (id_pixeldata[0] != -1)){
            int index = id_pixeldata[0];
            input->SetHoveredObjectID(renderable_objects.at(index)->GetID());
            vec3 n = vec3(normal_pixeldata[0],normal_pixeldata[1],normal_pixeldata[2]);
            input->SetHoveredNormal(n.normalize());
        }else{
            input->SetHoveredObjectID(OBJECTID_INVALID);
            input->SetHoveredNormal(vec3());
        }

        //debug->Info("Pixel data: %08X %08X %08X %08X\n",id_pixeldata[0],id_pixeldata[1],id_pixeldata[2],id_pixeldata[3]);
        //debug->Info("Normal data: %.3f %.3f %.3f\n",normal_pixeldata[0],normal_pixeldata[1],normal_pixeldata[2]);
        //glReadPixels(x,  window->height - y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixelData);
    }

    if (f_ssao){
        SSAOPass(camera);
    }

    //Look at one of the intermediate buffers
    if (view_buffer == 1){
        //Object position
        BlitBufferTarget(deferred_fbo_id,GL_COLOR_ATTACHMENT0);
    }else if (view_buffer == 2){
        //Normals
        BlitBufferTarget(deferred_fbo_id,GL_COLOR_ATTACHMENT1);
    }else if (view_buffer == 3){
        //SSAO output
        BlitBufferTarget(deferred_fbo_id,GL_COLOR_ATTACHMENT2);
    }else{

    }
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);

    ClearObjectBatches();

    if (tmr_frame){
        tmr_frame->Stop();
    }
}

//Create the required Shader Storage Buffer
bool Renderer::InitSSBO(){
    glCreateBuffers(1, (GLuint*)&instdata_ssbo);
    //glNamedBufferStorage(instdata_ssbo, 0 , NULL, GL_DYNAMIC_STORAGE_BIT);
    glNamedBufferData(instdata_ssbo, 0 , NULL, GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instdata_ssbo);

    //A buffer for the materials
    glCreateBuffers(1, (GLuint*)&materialdata_ssbo);
    glNamedBufferData(materialdata_ssbo, 0 , NULL, GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, materialdata_ssbo);

    //A buffer for all the lights
    glCreateBuffers(1, (GLuint*)&lights_ssbo);
    glNamedBufferData(lights_ssbo, 0 , NULL, GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, lights_ssbo);

    //A buffer where we read back data from, mainly the object id at mouse coordinate.
    //glCreateBuffers(1, (GLuint*)&readback_ssbo);
    //glNamedBufferData(readback_ssbo, 0 , NULL, GL_STREAM_DRAW);
    //glNamedBufferStorage(readback_ssbo, sizeof(readback_buffer_t), &readbackbuffer, GL_DYNAMIC_STORAGE_BIT);
    //glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, readback_ssbo);

    //A buffer for storing all the bone data for skinned meshes
    glCreateBuffers(1, (GLuint*)&boneinstdata_ssbo);
    glNamedBufferData(boneinstdata_ssbo, 0 , NULL, GL_STREAM_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, boneinstdata_ssbo);

    //Each mesh can use buffer base 5 for morph targets

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

bool Renderer::RebuildShadowFBO(int shadow_width, int shadow_height){
    debug->Info("(Re)Building buffers for Shadow mapping: %i x %i\n",shadow_width,shadow_height);
    if (shadow_fbo_id == -1){
        glCreateFramebuffers(1, &shadow_fbo_id);
    }

    //32-bit depth
    if (shadow_tex_id != -1){
        glDeleteTextures(1, &shadow_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &shadow_tex_id);

    glTextureStorage2D(shadow_tex_id, 1, GL_DEPTH_COMPONENT32F, shadow_width, shadow_height);
    glTextureParameteri(shadow_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(shadow_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(shadow_fbo_id, GL_DEPTH_ATTACHMENT, shadow_tex_id, 0);
    return CheckFrameBuffer();
}

bool Renderer::RebuildDeferredFBO(){
    debug->Info("(Re)Building buffers for deferred stage\n");
    if (deferred_fbo_id == -1){
        glCreateFramebuffers(1, &deferred_fbo_id);
    }

    if (deferred_fbo_id == -1){
        return false;
    }

    //Color buffer for object position 32-bit... 16?
    if (deferred_position_tex_id != -1){
        glDeleteTextures(1, &deferred_position_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_position_tex_id);

    glTextureStorage2D(deferred_position_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(deferred_position_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_position_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT0, deferred_position_tex_id, 0);
    CheckFrameBuffer();

    //Normals for objects.
    if (deferred_normal_tex_id != -1){
        glDeleteTextures(1, &deferred_normal_tex_id);
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_normal_tex_id);

    glTextureStorage2D(deferred_normal_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(deferred_normal_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_normal_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT1, deferred_normal_tex_id, 0);
    CheckFrameBuffer();

    //32-bit depth
    if (deferred_depth_tex_id != -1){
        glDeleteTextures(1, &deferred_depth_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_depth_tex_id);

    glTextureStorage2D(deferred_depth_tex_id, 1, GL_DEPTH_COMPONENT32F, width, height);
    glTextureParameteri(deferred_depth_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_depth_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_DEPTH_ATTACHMENT, deferred_depth_tex_id, 0);
    CheckFrameBuffer();

    //We also generate a texture for the SSAO output, and attach it to the deferred FBO.
    if (ssao_tex_id != -1){
        glDeleteTextures(1, &ssao_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &ssao_tex_id);

    glTextureStorage2D(ssao_tex_id, 1, GL_RGBA16F, width, height);
    glTextureParameteri(ssao_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(ssao_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT2, ssao_tex_id, 0);
    CheckFrameBuffer();

    //ObjectID buffer
    if (deferred_objectid_tex_id != -1){
        glDeleteTextures(1, &deferred_objectid_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &deferred_objectid_tex_id);

    glTextureStorage2D(deferred_objectid_tex_id, 1, GL_R32I, width, height);
    glTextureParameteri(deferred_objectid_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(deferred_objectid_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glNamedFramebufferTexture(deferred_fbo_id, GL_COLOR_ATTACHMENT3, deferred_objectid_tex_id, 0);
    CheckFrameBuffer();

    return true;
}


//Create all the frame and renderbuffers for mulisampling
// A multisampled color and depth buffer, and a resolve buffer.
bool Renderer::RebuildMSAAFBO(){
    debug->Info("(Re)Building buffers for MSAA\n");
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
    if (resolve_tex_id != -1){
        glDeleteTextures(1, &resolve_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &resolve_tex_id);
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

//Copy a renderbuffer target to main buffer
void Renderer::BlitBufferTarget(GLuint framebuffer_id, GLenum attachment){
    //Blit from multisampled buffer to main backbuffer = GL_COLOR_ATTACHMENT0
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer_id);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo_id);
    glReadBuffer(attachment);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    //glBlitNamedFramebuffer exists, but you still have to bint the correct attachments...?
    //There is also glNamedFramebufferDrawBuffer
    glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void Renderer::SelectViewBuffer(int view_id){
    view_buffer = view_id;
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
            f_vsync = true;
        }else{
            debug->Ok("VSync: Disabled\n");
            f_vsync = false;
        }
    }
}

bool Renderer::GetVSync(){
    return f_vsync;
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
    debug->Info("Adding %i materials from list\n",list.size());
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
    debug->Trace("Uploading materials\n");
    last_texture_unit = 4;

    glsl_materials.clear();
    for (Material& mat:materials){
        if (mat.diff_texture){;
            debug->Info("Material has diffuse Texture: Binding to Unit %i\n",last_texture_unit);
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
        //glInvalidateBufferData(materialdata_ssbo);
        glNamedBufferData(materialdata_ssbo,glsl_materials.size()*sizeof(material_t) , &glsl_materials.at(0),GL_STREAM_DRAW);
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
    debug->Trace("Uploading Lights\n");
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
        //glInvalidateBufferData(lights_ssbo);
        glNamedBufferData(lights_ssbo,glsl_lights.size()*sizeof(light_t) , &glsl_lights.at(0),GL_STREAM_DRAW);
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

//Should be called when physics is done, before rendering.
//It deletes them from the list, and actually deletes them.
//This is now responsible for destroying objects... until something better comes to mind.
void Renderer::DeleteDestroyedObjects(){
    std::vector<Object*>::iterator it = objects.begin();
    for ( ; it != objects.end(); ) {
        Object* object = *it;
        if (object->IsDestroyed()){
            //We should destroy it.
            it = objects.erase(it);
            //Destroy object
            //debug->Info("Object %lu is about to be destroyed\n",object->GetID());
            delete object;
        }else{
            object->DeleteDestroyedChildren();
            ++it;
        }
    }
}