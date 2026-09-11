#include "Renderer.h"

#include "Debug.h"
#include "stb_image_write.h"
#include <chrono>
#include <cstring>

#define DEFAULT_FRAMEBUFFER_ID  0

static Debugger* debug = new Debugger("Renderer",DEBUG_INFO);

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

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
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
void Renderer::GetAllVisibleSubLights(Object* object,std::vector<Light*>&lights){
    if (!object){
        return;
    }

    //Check if any of the direct children are lights
    for (int i=0;i<object->children.size();i++){
        Object* child = object->GetChild(i);
        Light* light = dynamic_cast<Light*>(child);
        if (light && light->IsVisible()){
            lights.push_back(light);
        }
    }
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
        object->ResolveMaterialNames(materials);
    }
}

//For now, we simple render all objects.
void Renderer::CullLights(){
    visible_lights.clear();
    for (Object* object:objects){
        Light* light = dynamic_cast<Light*>(object);
        if (light && light->IsVisible()){
            visible_lights.push_back(light);
            continue;
        }
        GetAllVisibleSubLights(object,visible_lights);
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
void Renderer::RenderUniqueMeshes(int rendering_mode, int custom_shader_index){
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
        if ((rendering_mode == MESH_MODE_SHADER) && (mesh->mesh_mode != MESH_MODE_SHADER)){
            continue;
        }
        //One sub-pass per custom shader, so a mesh is drawn only while ITS shader is bound.
        if ((rendering_mode == MESH_MODE_SHADER) && (custom_shader_index >= 0)
            && (mesh->custom_shader_index != custom_shader_index)){
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
            const int* object_slots = object->GetMaterialSlots();
            for (int i=0;i<NUM_MATERIAL_SLOTS;i++){
                data.material_slot[i] = object_slots[i];
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
                bonedata_t bonedata = {};
                bonedata.mat_transformscale = fmat4().identity();

                //It has been previously established we are skinned mesh
                Skeleton* skeleton = dynamic_cast<Skeleton*>(object);
                if (!skeleton){
                    debug->Trace("Skinned itself does not appear to be a skeleton...\n");
                    skeleton = dynamic_cast<Skeleton*>(object->GetParent());
                    if (!skeleton){
                        instancedata.push_back(data);
                        debug->Warn("Skinned mesh itself does not appear to be a skeleton nor it's parent.\n");
                        continue;
                    }
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

            if (boneinstancedata.size() > 0){
                glNamedBufferData(boneinstdata_ssbo,boneinstancedata.size()*sizeof(bonedata_t) , &boneinstancedata.at(0),GL_STREAM_DRAW);
            }else{
                debug->Warn("No boneinstancedata for mesh ID: %lu\n",mesh->GetID());
                //glInvalidateBufferData(boneinstdata_ssbo);
            }
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
    //No per-object state copy here any more: an object carries a single ObjectState, written by
    //the physics thread under physics_mutex - which DrawFrame holds across this whole call.
    CullObjects();
    CullLights();
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

    //Viewport and clear - see the main color pass in DrawFrame for why this uses
    //GetViewportWidth/Height() (and the offset) instead of the raw width/height.
    glViewport(viewport_x, viewport_y, GetViewportWidth(), GetViewportHeight());
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

    //MESH_MODE_SHADER meshes are deliberately NOT drawn here. They used to be, with
    //deferred_shader still bound - so a custom material wrote position/normal/objectid/depth as
    //if it were solid geometry. For a volume that is actively wrong twice over: it made the box
    //swallow every hover pick made through it, and now that CustomShaderPass reads this same
    //G-buffer to find the geometry in front of it, the box would have occluded itself.
    //A custom material that genuinely wants to be in the G-buffer needs its own deferred variant.

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

int Renderer::AddCustomShader(Shader* shader){
    if (!shader){
        debug->Err("AddCustomShader called with no shader\n");
        return -1;
    }
    custom_shaders.push_back(shader);
    int index = (int)custom_shaders.size() - 1;
    debug->Info("Registered custom shader %s as index %i\n",shader->fname.c_str(),index);
    return index;
}

Shader* Renderer::GetCustomShader(int index){
    if ((index < 0) || (index >= (int)custom_shaders.size())){
        return NULL;
    }
    return custom_shaders.at(index);
}

/*
    The custom-material pass: everything tagged MESH_MODE_SHADER, one sub-pass per registered
    shader. Called last of the geometry passes in DrawFrame, after the skinned meshes, because a
    custom material is typically translucent and has to blend over everything solid - a volume
    with a character standing in it must be drawn after that character, not before.

    Every sub-pass gets the deferred G-buffer bound as texture input (see TEXUNIT_GBUFFER_*),
    which is why DeferredPass now runs BEFORE the main color pass. That is the only way a shader
    here can know how far away the scene is: msaa_fbo's depth is a multisampled renderbuffer and
    cannot be sampled at all, and the position buffer is what a raymarcher actually wants anyway
    (a world-space point, no inverse projection needed).

    A shader's uniform_callback may change cull face, depth mask and the depth test - a volume
    wants inside faces, no depth write and no depth test (it resolves occlusion itself, against
    the G-buffer below; see ApplicationShip::SetVolumeUniforms for why the test has to go), a
    ground decal wants the defaults - and all three are restored after each sub-pass, so nothing
    leaks into the next sub-pass or the next frame's depth passes.
*/
void Renderer::CustomShaderPass(Camera* camera){
    if (custom_shaders.empty() || !camera){
        return;
    }
    vec3 eye = camera->GetPosition();
    for (int i = 0;i < (int)custom_shaders.size();i++){
        Shader* shader = custom_shaders.at(i);
        if (!shader){
            continue;
        }
        shader->Use();
        shader->Setmat4("mat_worldcam",camera->mat_cam);
        //A custom shader that reconstructs a ray - anything raymarched - needs the ray origin,
        //which the default shaders get under this same name.
        shader->Setvec3("eye_position",eye);

        glBindTextureUnit(TEXUNIT_GBUFFER_DEPTH,deferred_depth_tex_id);
        glBindTextureUnit(TEXUNIT_GBUFFER_POSITION,deferred_position_tex_id);
        glBindTextureUnit(TEXUNIT_GBUFFER_NORMAL,deferred_normal_tex_id);

        //We'd like a callback so the custom shader can set its own uniforms and such.
        if (shader->uniform_callback){
            shader->uniform_callback();
        }
        RenderUniqueMeshes(MESH_MODE_SHADER,i);
        glCullFace(GL_BACK);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }
}

/*
    Hands one shader the cloud shadow map. Called for every shader that lights with the sun,
    which is the default and skinned ones - they share default.frag.

    Setmat4 goes through debug->Fatal if the uniform is missing, so it is only called when there
    is a map to point at. f_cloud_shadows is always set, so a shader cannot be left sampling a
    stale map from a previous scene.
*/
void Renderer::UploadCloudShadow(Shader* s){
    if (!s){
        return;
    }
    if (cloud_shadow_tex_id == (GLuint)-1){
        s->Setint("f_cloud_shadows",0);
        return;
    }
    glBindTextureUnit(TEXUNIT_CLOUD_SHADOW,cloud_shadow_tex_id);
    s->Setmat4("mat_cloud_shadow",mat_cloud_shadow);
    s->Setint("f_cloud_shadows",1);
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

    PrepareObjects();

    camera->viewport.width = GetViewportWidth();
    camera->viewport.height = GetViewportHeight();
    camera->CalculateLookatMatrix();

    ClearDepthPasses();
    if (skinned_shader){
        skinned_shader->Use();
        vec3 p = camera->GetPosition();
        skinned_shader->Setvec3("eye_position",p);
        if (!skinned_shader->Setint("f_normal_mapping",(int)f_normal_mapping)){
            debug->Fatal("Could not set f_normal_mapping in skinned shader\n");
        }
        skinned_shader->Setfloat("alpha_clip",alpha_clip);
        skinned_shader->Setint("f_materialindex_is_color",1); //Abusing this to bypass everything
        //skinned_shader, not shader: RenderSingleDepthPass sets mat_worldcam/mat_shadow on
        //whatever it is handed, and the skinned meshes below are drawn with skinned_shader
        //bound. This used to pass `shader` and worked anyway, because glUniform wrote into the
        //bound program regardless of which Shader object was asked - now that Shader uses
        //glProgramUniform the two have to actually agree.
        RenderDepthPasses(skinned_shader,MESH_MODE_SKINNED);
    }

    {//Depth pass with default shader.
        shader->Use();
        vec3 p = camera->GetPosition();
        shader->Setvec3("eye_position",p);
        if (!shader->Setint("f_normal_mapping",(int)f_normal_mapping)){
            debug->Fatal("Could not set f_normal_mapping in default shader\n");
        }
        shader->Setfloat("alpha_clip",alpha_clip);
        shader->Setint("f_materialindex_is_color",1); //Abusing this to bypass everything
        RenderDepthPasses(shader,MESH_MODE_NORMAL);
        //RenderDepthPasses(shader,MESH_MODE_SHADER);
        FinishDepthPasses();
    }

    //The deferred G-buffer, filled BEFORE the color pass rather than after it.
    //
    //It used to run at the end of the frame, purely so the mouse-over readback below had an
    //object-id buffer to read - hence the "do we even want these passes?" note that used to sit
    //on it. Where it sits now it does the same job (nothing between here and the readback
    //touches its attachments) and additionally gives CustomShaderPass a depth/position/normal
    //texture of the solid scene, which the color pass itself cannot offer: msaa_fbo's depth is a
    //multisampled renderbuffer and is not samplable.
    //
    //It is a second full geometry pass either way. If that ever needs to go, the fix is to make
    //the color pass write the G-buffer as extra render targets - not to move this back.
    if (pipeline == PIPELINE_DEFERRED){
        DeferredPass(camera);
    }

    glBindTextureUnit(0, shadow_tex_id);
    //Lands in `shader` even though DeferredPass above left deferred_shader bound - Shader's
    //setters name their own program (glProgramUniform), so uniform sets no longer depend on the
    //bind order. They did until 2026-09-10, and moving DeferredPass here quietly redirected this
    //matrix into deferred_shader, leaving the whole colour pass drawn from the sun's viewpoint.
    shader->Setmat4("mat_worldcam",camera->mat_cam);

    //Select the mutisampled framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    //Clears the WHOLE fbo (unaffected by glViewport, and scissor test is never enabled) -
    //still full window size, so a restricted viewport_width/height below still leaves the
    //rest of the frame cleared to transparent black for ImGui to draw over.
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(msaa_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    glClearNamedFramebufferfv(msaa_fbo_id,GL_DEPTH,0,&depth);

    //Viewport - confines the actual 3D draw calls below to the (optionally smaller,
    //optionally offset) sub-rectangle set via viewport_x/viewport_width/viewport_height;
    //see Renderer.h's comment on those fields.
    glViewport(viewport_x, viewport_y, GetViewportWidth(), GetViewportHeight());

    { //We draw skybox before other stuff
        DrawSkyBox(camera);
    }
    shader->Use();

    UploadMaterials();
    UploadLights();

    shader->Setint("f_environment_reflections",f_use_reflections);
    shader->Setfloat("cone_softness",cone_softness);
    shader->Setint("f_materialindex_is_color",0);
    UploadCloudShadow(shader);
    RenderUniqueMeshes(MESH_MODE_NORMAL);
    shader->Setint("f_materialindex_is_color",1);
    RenderUniqueMeshes(MESH_MODE_LINE);
    shader->Setint("f_materialindex_is_color",0);



    if (skinned_shader && camera){
        skinned_shader->Use();
        vec3 p = camera->GetPosition();
        skinned_shader->Setvec3("eye_position",p);
        skinned_shader->Setmat4("mat_worldcam",camera->mat_cam);
        if (!skinned_shader->Setint("f_normal_mapping",(int)f_normal_mapping)){
            debug->Fatal("Could not set f_normal_mapping in skinned shader\n");
        }
        skinned_shader->Setint("f_environment_reflections",f_use_reflections);
        skinned_shader->Setfloat("cone_softness",cone_softness);
        skinned_shader->Setfloat("alpha_clip",alpha_clip);
        skinned_shader->Setint("f_materialindex_is_color",0);
        UploadCloudShadow(skinned_shader);
        RenderUniqueMeshes(MESH_MODE_SKINNED);
    }

    //Custom materials go last of the geometry passes, AFTER the skinned meshes: they are
    //typically translucent and do not write depth, so anything solid has to already be in the
    //buffer for them to blend over. A character standing inside a volume was previously drawn
    //on top of it at full strength.
    CustomShaderPass(camera);

    ResolveAA();

    //Now we can read the normal and object ID:
    if ((pipeline == PIPELINE_DEFERRED && input)){
        //DeferredPass ran near the top of the frame and left its own framebuffer bound; it no
        //longer does, and ResolveAA has since pointed the read framebuffer at msaa_fbo. So say
        //explicitly which framebuffer these glReadPixels come from.
        glBindFramebuffer(GL_READ_FRAMEBUFFER, deferred_fbo_id);
        glReadBuffer(GL_COLOR_ATTACHMENT3);
        int32_t id_pixeldata[4] = {-1,-1,-1,-1};
        float  normal_pixeldata[4] = {0,0,0,0};
        float  position_pixeldata[4] = {0,0,0,0};
        int2 mouse = {-1,-1};
        if (input){
            mouse = input->GetRelativeMousePosition();
        }
        glReadPixels(mouse.x,  height - mouse.y, 1, 1, GL_RED_INTEGER, GL_INT, id_pixeldata);
        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glReadPixels(mouse.x,  height - mouse.y, 1, 1, GL_RGB, GL_FLOAT, normal_pixeldata);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glReadPixels(mouse.x,  height - mouse.y, 1, 1, GL_RGB, GL_FLOAT, position_pixeldata);

        //Somehow, -1 reads back as 3F800000
        if ((id_pixeldata[0] != 0x3F800000) && (id_pixeldata[0] != -1)){
            int index = id_pixeldata[0];
            if (index > renderable_objects.size()){
                debug->Err("Read back object index %i is out of bounds (max %i)\n",index,renderable_objects.size());
                input->SetHoveredObjectID(OBJECTID_INVALID);
                input->SetHoveredNormal(vec3());
                input->SetHoveredPosition(vec3());
            }else{
                input->SetHoveredObjectID(renderable_objects.at(index)->GetID());
                vec3 n = vec3(normal_pixeldata[0],normal_pixeldata[1],normal_pixeldata[2]);
                input->SetHoveredNormal(n.normalize());
                input->SetHoveredPosition(vec3(position_pixeldata[0],position_pixeldata[1],position_pixeldata[2]));
            }
        }else{
            input->SetHoveredObjectID(OBJECTID_INVALID);
            input->SetHoveredNormal(vec3());
            input->SetHoveredPosition(vec3());
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

    //resolve_fbo_id's GL_COLOR_ATTACHMENT0 now holds this frame's fully-resolved output
    //(see ResolveAA/BlitBufferTarget, which always blit into it) - the right place to grab
    //a screenshot from, before anything else gets a chance to rebind the framebuffer.
    CaptureScreenshotIfRequested();

    ClearObjectBatches();

    if (tmr_frame){
        tmr_frame->Stop();
    }
}

std::vector<uint8_t> Renderer::RequestScreenshot(int timeout_ms){
    std::unique_lock<std::mutex> lock(screenshot_mutex);
    screenshot_requested = true;
    screenshot_ready = false;
    bool got = screenshot_cv.wait_for(lock,std::chrono::milliseconds(timeout_ms),[this]{ return screenshot_ready; });
    if (!got){
        debug->Warn("RequestScreenshot: timed out after %dms waiting for the render thread\n",timeout_ms);
        return {};
    }
    return screenshot_png; //copy out while still holding the lock
}

void Renderer::CaptureScreenshotIfRequested(){
    std::unique_lock<std::mutex> lock(screenshot_mutex);
    if (!screenshot_requested){
        return;
    }
    screenshot_requested = false;
    lock.unlock(); //GL work + PNG encoding can take a while - don't hold the mutex for it

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    int row_bytes = width * 3;
    std::vector<uint8_t> pixels((size_t)row_bytes * height);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data());

    //glReadPixels' origin is bottom-left; flip rows so the PNG reads top-down like a normal image.
    std::vector<uint8_t> flipped(pixels.size());
    for (int y = 0; y < height; y++){
        memcpy(flipped.data() + (size_t)y*row_bytes,pixels.data() + (size_t)(height-1-y)*row_bytes,row_bytes);
    }

    std::vector<uint8_t> png_bytes;
    auto write_cb = [](void* context, void* data, int size){
        std::vector<uint8_t>* out = (std::vector<uint8_t>*)context;
        out->insert(out->end(),(uint8_t*)data,(uint8_t*)data + size);
    };
    stbi_write_png_to_func(write_cb,&png_bytes,width,height,3,flipped.data(),row_bytes);

    lock.lock();
    screenshot_png = std::move(png_bytes);
    screenshot_ready = true;
    lock.unlock();
    screenshot_cv.notify_all();
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
    //Default wrap is GL_REPEAT, which tiles the depth map over everything outside the light's
    //ortho box and fakes shadows there. CalcShadow now rejects out-of-range lookups outright, so
    //this is only a safety net - clamping is still a great deal saner than tiling.
    glTextureParameteri(shadow_tex_id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(shadow_tex_id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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

//Add's materials to global list, omitting duplicates by name. Returns the index of the material
//with that name - the one just appended, or the EXISTING one when the name was already taken.
//
//It used to return materials.size()-1 either way, i.e. the index of the LAST material rather than
//of the matching one, so a caller adding a material under a name that already existed silently got
//a handle on somebody else's material. Fixed rather than worked around because a wrong index is
//indistinguishable from a right one at the call site, and the failure only ever shows up as an
//object rendering in another object's colour. No existing caller changes behaviour: AddMaterials
//ignores the return value, and every other call site adds a name that is new.
int Renderer::AddMaterial(Material& newmat){
    for (int index = 0; index < (int)materials.size(); index++){
        if (newmat.name.compare(materials[index].name) == 0){
            debug->Info("Already have material %s\n",newmat.name.c_str());
            return index;
        }
    }
    materials.push_back(newmat);
    return (int)materials.size() - 1;
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
            debug->Trace("Material has diffuse Texture: Binding to Unit %i\n",last_texture_unit);
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

    //Material textures are handed units from 4 upwards with no upper bound, so a scene with
    //enough of them will walk into the units reserved above - the skybox cubemap at 24 and
    //whatever an app bound at TEXUNIT_APP_RESERVED. Silently overwriting one of those is very
    //hard to recognise from the resulting image, so say it out loud.
    if (last_texture_unit > cubemap_texture_unit){
        debug->Warn("Material textures reached unit %i, past the reserved units at %i and %i - "
                    "they are now overwriting each other\n",
                    last_texture_unit,cubemap_texture_unit,TEXUNIT_APP_RESERVED);
    }

    if (glsl_materials.size() > 0){
        //glInvalidateBufferData(materialdata_ssbo);
        glNamedBufferData(materialdata_ssbo,glsl_materials.size()*sizeof(material_t) , &glsl_materials.at(0),GL_STREAM_DRAW);
    }
}

void Renderer::SetSkyboxCubemap(CubeMap* cubemap){
    skybox = cubemap;
}

void Renderer::UploadCubeMap(CubeMap* cubemap){
    if (cubemap->cubemap_id){
        // Built via LoadFromEquirectangular — one GL object, bind once.
        debug->Info("UploadCubeMap: binding equirectangular cubemap to unit %i\n", cubemap_texture_unit);
        glBindTextureUnit(cubemap_texture_unit, cubemap->cubemap_id);
        return;
    }
    // Legacy path: 6 separate Texture objects sharing a texture_id.
    for (int i = 0; i < 6; i++){
        if (cubemap->texture[i]){
            debug->Info("Loading CubeMap %i/6 : %s to texture_unit %i\n",i,cubemap->texture[0]->name.c_str(),cubemap_texture_unit+i);
            glBindTextureUnit(cubemap_texture_unit+i, cubemap->texture[i]->texture_id);
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
            //World, for the same reason as the cone light below - a directional light
            //parented to something would otherwise ignore the parent. Currently the sun
            //is a root object, so this changes nothing for it today.
            light.direction = directional_light->GetWorldForward();
            light.position = directional_light->GetWorldPosition();
            light.color = directional_light->color;
            light.brightness = directional_light->brightness;
            glsl_lights.push_back(light);
            continue;
        }
        //ConeLight has existed in Light.h all along but had no case here, so one in a scene was
        //silently dropped from the SSBO. Tried before PointLight because they are siblings - the
        //casts do not cross - and after DirectionalLight, which is the one that is also a Camera.
        ConeLight* cone_light = dynamic_cast<ConeLight*>(l);
        if (cone_light){
            //GetWorldForward, not GetForward: the latter uses only the object's OWN
            //rotation, so a cone light parented to something - the ship's headlight is a
            //child of the hull - would keep pointing wherever its local rotation says and
            //never follow its parent. The position below is already a world one, so
            //taking a local direction here also mixed the two spaces.
            light.direction = cone_light->GetWorldForward();
            light.position = cone_light->GetWorldPosition();
            light.color = cone_light->color;
            light.brightness = cone_light->brightness;
            //cone_angle is the FULL opening angle in degrees; cos_angle is the cosine of the
            //HALF angle, because that is what a dot product against the cone axis compares
            //against. Clamped below 180 because cos(90) is 0, and 0 is how the shaders
            //recognise a plain point light (see light_t) - a 180 degree cone would turn into one.
            float half_angle = fminf(cone_light->cone_angle,179.0f) * 0.5f;
            light.cos_angle = cosf(half_angle * (TYPE_PI / 180.0f));
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