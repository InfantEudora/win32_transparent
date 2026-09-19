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

Renderer::~Renderer(){
    DestroyGPUPassTimers();
    DestroyPickingPBOs();
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

bool Renderer::Init(const char* vert_filename, const char* frag_filename, int _pipeline,
                    const char* skinned_vert_filename){
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
        deferred_shader = new Shader(vert_filename,frag_filename);
        //The skinned twin differs in its vertex stage only, so it links against the SAME
        //fragment shader. An app with no skinned mesh can pass NULL and skip the link.
        if (skinned_vert_filename){
            deferred_shader_skinned = new Shader(skinned_vert_filename,frag_filename);
        }
        ssao_compute_shader = new Shader();
        ssao_compute_shader->CreateComputeShader("shaders/ssao_compute.comp");
    }

    //Line meshes have a program of their own - see SSBO_VERTEX_PULL in Mesh.h and line.vert: a
    //program's vertex inputs have to match every VAO it is drawn with, and the line VAO does not
    //look like a mesh VAO.
    line_shader = new Shader("shaders/line.vert","shaders/line.frag");

    SetOpenGLState();

    //We make intel happy with an empty VAO
    GLuint empty_vao = -1;
    glGenVertexArrays(1,&empty_vao);
    glBindVertexArray(empty_vao);

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(opengl_message_callback, nullptr);

    tmr_frame = new PerfTimer("Frame Time");
    tmr_pick_readback = new PerfTimer("Pick readback");
    InitGPUPassTimers();
    InitPickingPBOs();
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
    //Only if an app actually asked for one - RebuildLowResFBO allocates, and at scale 1 there is
    //nothing to resize. A failure here is not fatal to the resize: the target is dropped back to
    //full resolution and the frame still draws.
    if ((lowres_scale > 1) && !RebuildLowResFBO()){
        lowres_scale = 1;
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
void Renderer::CullObjects(const std::vector<Object*>& objects){
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
void Renderer::CullLights(const std::vector<Object*>& objects){
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
    debug->Trace("renderable_objects.size() = %i\n",renderable_objects.size());
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
void Renderer::RenderUniqueMeshes(int rendering_mode, int custom_shader_index, bool f_occluder_pass){
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
        //A MESH_MODE_SHADER mesh nobody tagged. Drawing it with whatever shader happens to be
        //bound is how this used to hide - see Mesh::custom_shader_index - so it draws nothing and
        //says so, once.
        if ((rendering_mode == MESH_MODE_SHADER) && (mesh->custom_shader_index < 0)){
            if (!mesh->f_warned_no_custom_shader){
                mesh->f_warned_no_custom_shader = true;
                debug->Err("Mesh id %u is MESH_MODE_SHADER but has no custom_shader_index - set it "
                           "to what Renderer::AddCustomShader returned, or it will not be drawn\n",
                           mesh->GetID());
            }
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
            //Not an occluder, and this is a shadow pass. Skipping it HERE rather than with a
            //per-object test at draw time is the whole reason this is cheap: depth passes batch
            //per mesh and draw every instance in one call, so the only way to leave one object
            //out is to leave it out of the instance list being built.
            if (f_occluder_pass && !object->CastsShadow()){
                continue;
            }
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

                /*
                    This instance's block of bone matrices, laid out BY JOINT INDEX.

                    The shader reads bone_data[instance * bone_count + bones.x], where bones.x is
                    the JOINTS_0 vertex attribute - an index into the skin's joint list. So the
                    block has to be in THAT order, and not in the order a tree walk happens to
                    visit bones in.

                    Those two agreed for as long as every rig was a single chain whose depth-first
                    order matched its joint list, which is why pushing them back in walk order
                    worked. It stops being true the moment a skin has more than one root bone: the
                    walk emits a whole subtree before it reaches the second root, while the joint
                    list interleaves them. The failure is silent and looks like a corrupt mesh.

                    Bone::bone_index is that joint index - see GLTFLoader::GetBone.
                */
                int num_bones = skeleton->num_bones;
                size_t bone_base = boneinstancedata.size();
                boneinstancedata.resize(bone_base + num_bones,bonedata);
                for (Bone* bone:bones){
                    int slot = bone->bone_index;
                    if (slot < 0 || slot >= num_bones){
                        debug->Err("Bone %s has joint index %i, outside this skin's %i joints\n",
                                   bone->name.c_str(),slot,num_bones);
                        continue;
                    }
                    bonedata.mat_inversebind = bone->inverse_bind_matrix;
                    bonedata.mat_transformscale = bone->GetWorldTransformScaleMatrix();
                    boneinstancedata.at(bone_base + slot) = bonedata;
                    bone->bone_unpacked_index = slot;
                }
                data.num_bones = num_bones;
            }

            instancedata.push_back(data);

        }
        //Every instance of this mesh was filtered out - a mesh used only by objects that cast no
        //shadow, during a shadow pass. Nothing to upload and nothing to draw, and .at(0) below
        //would be reading an empty vector.
        if (instancedata.empty()){
            continue;
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

        //instancedata.size(), not mesh->batch_num_instances. They are the same number in every
        //pass that draws the whole batch, but a shadow pass may have filtered some out, and the
        //draw count has to match what was actually uploaded above - otherwise the extra instances
        //read off the end of the buffer and draw whatever is there.
        debug->Trace("Rendering %zu instances of mesh->id %i\n",instancedata.size(),mesh->GetID());
        mesh->RenderInstances((int)instancedata.size());
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

void Renderer::PrepareObjects(const std::vector<Object*>& objects){
    //First, we cull all objects we are sure of are not visible.
    //Then we make a list of all objects that need to be rendered.
    //Of those objects, we make a list for each unique mesh with object attributes and object ids.
    //No per-object state copy here any more: an object carries a single ObjectState, written by
    //the physics thread under physics_mutex - which DrawFrame holds across this whole call.
    CullObjects(objects);
    CullLights(objects);
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
    /*
        Which attachment is which, because the numbers here are not self-describing and reading
        them off wrongly is easy (see SetupDeferredBuffers for where they are attached):

            DEPTH  depth              cleared to 1.0
            0      position   RGBA16F cleared to (0,0,0,0), w is the material's alpha
            1      normal     RGBA16F cleared to (1,0,0,0)
            2      SSAO       RGBA16F cleared to (1,0,0,0), i.e. unoccluded
            3      object id  int     cleared to -1, "no object"

        Of these, ONLY DEPTH AND OBJECT ID CARRY A CLEAR VALUE THAT MEANS "NOTHING WAS DRAWN
        HERE". 1.0 is outside the range a fragment can write and -1 is not an object; the colour
        buffers are cleared to values that are perfectly legal things for real geometry to have
        written, so they cannot be tested for emptiness. Anything sampling this G-buffer - a
        custom shader especially - asks depth first. See the TEXUNIT_GBUFFER_* block in Renderer.h.

        AND THE THIRD ARGUMENT BELOW IS A DRAW BUFFER INDEX, NOT AN ATTACHMENT NUMBER. It indexes
        the list handed to glNamedFramebufferDrawBuffers just above, so here:

            draw buffer 0 -> ATTACHMENT0 (position)
            draw buffer 1 -> ATTACHMENT1 (normal)
            draw buffer 2 -> ATTACHMENT3 (object id)   <- not 2, the list skips ATTACHMENT2
            draw buffer 3 -> nothing at all, the list has only three entries

        Reading it as an attachment number is what produced the mystery this code used to carry a
        workaround for: the object id texture was being cleared by the FLOAT call at index 2 with
        (1,0,0,0), so it came back holding 0x3F800000 - the bit pattern of 1.0f - and the integer
        clear meant for it, aimed at index 3, silently cleared nothing. See the readback below,
        which no longer has to know that number. The SSAO texture on ATTACHMENT2 is not a draw
        buffer in this pass and never was being cleared here.
    */
    vec4 clr_clear = vec4(0,0,0,0);
    float depth = 1.0;
    glClearNamedFramebufferfv(deferred_fbo_id,GL_DEPTH,0,&depth);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    clr_clear = vec4(1,0,0,0);
    glClearNamedFramebufferfv(deferred_fbo_id,GL_COLOR,1,(float*)&clr_clear);
    GLint int_clear[4] = {-1,-1,-1,-1};
    glClearNamedFramebufferiv(deferred_fbo_id,GL_COLOR,2,(GLint*)&int_clear);


    //UploadMaterials();
    //UploadLights();
    RenderUniqueMeshes(MESH_MODE_NORMAL);

    /*
        MESH_MODE_SHADER meshes are not drawn here BY DEFAULT. They all used to be, with
        deferred_shader still bound, so every custom material wrote position/normal/objectid/depth
        as if it were solid geometry. For a volume that is actively wrong twice over: it made the
        box swallow every hover pick made through it, and now that CustomShaderPass reads this same
        G-buffer to find the geometry in front of it, the box would have occluded itself.

        But "no custom material is ever solid" was too strong, and this is the opt-in that used to
        be the "needs its own deferred variant" note. A custom shader that draws an ordinary solid
        surface and merely computes its own COLOUR - apps/bomber's water tiles - sets
        Shader::f_writes_gbuffer, and its meshes are drawn here with the plain deferred shader.
        Which is exactly right: depth, position, normal and object id describe the SHAPE, and the
        shape was never the custom part.

        Drawn with `deferred_shader`, not with the custom program, and not by a second pass over
        every mesh: one RenderUniqueMeshes per opted-in shader index, which is how the mesh filter
        already works.
    */
    for (int i = 0;i < (int)custom_shaders.size();i++){
        if (custom_shaders.at(i) && custom_shaders.at(i)->f_writes_gbuffer){
            RenderUniqueMeshes(MESH_MODE_SHADER,i);
        }
    }

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
    Builds (or resizes) the reduced-resolution custom-shader target. See the block on
    lowres_fbo_id in Renderer.h for what it is and why it has no depth attachment.

    ROUNDED UP, not down. At an odd window width a truncating divide leaves the rightmost column
    of window pixels with no low-res block behind it, and the composite then samples the block
    next door - a one-pixel smear down the edge of the screen that only appears at some window
    sizes, which is the worst kind of bug to go looking for. One spare block costs nothing.
*/
bool Renderer::RebuildLowResFBO(void){


    int scale = (lowres_scale < 1) ? 1 : lowres_scale;
    int w = (width  + scale - 1) / scale;
    int h = (height + scale - 1) / scale;
    if (w < 1){
        w = 1;
    }
    if (h < 1){
        h = 1;
    }
    if ((lowres_fbo_id != (GLuint)-1) && (w == lowres_tex_width) && (h == lowres_tex_height)){
        return true;    //Already the right size - the common case, every frame.
    }
    debug->Info("(Re)Building the custom-shader target at 1/%i: %i x %i\n",scale,w,h);

    if (lowres_fbo_id == (GLuint)-1){
        glCreateFramebuffers(1, &lowres_fbo_id);
    }
    if (lowres_tex_id != (GLuint)-1){
        glDeleteTextures(1, &lowres_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &lowres_tex_id);
    //RGBA16F: the same format msaa_fbo's colour renderbuffer uses, for the same reason - a
    //volume's core is brighter than 1 on purpose.
    glTextureStorage2D(lowres_tex_id, 1, GL_RGBA16F, w, h);
    //NEAREST is the effect, not a compromise. The composite computes the exact texel it wants so
    //the filter never actually interpolates, but LINEAR here would quietly soften the blocks the
    //day somebody sampled it any other way.
    glTextureParameteri(lowres_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(lowres_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(lowres_tex_id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(lowres_tex_id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(lowres_fbo_id, GL_COLOR_ATTACHMENT0, lowres_tex_id, 0);
    GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(lowres_fbo_id, 1, &draw_buffer);

    //The named check rather than CheckFrameBuffer(), which reads whatever is bound - this runs
    //from PreRender with the frame's own framebuffer bound and must not disturb it.
    GLenum status = glCheckNamedFramebufferStatus(lowres_fbo_id, GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE){
        debug->Err("The custom-shader target is not complete (0x%04X)\n",status);
        return false;
    }
    lowres_tex_width = w;
    lowres_tex_height = h;

    if (lowres_vao == (GLuint)-1){
        //Holds nothing and is never filled. The composite's vertex stage builds its triangle out
        //of gl_VertexID, but core profile still refuses to draw with no VAO bound at all.
        glCreateVertexArrays(1, &lowres_vao);
    }
    if (!lowres_composite_shader){
        lowres_composite_shader = new Shader("shaders/lowres_composite.vert",
                                             "shaders/lowres_composite.frag");
    }
    return true;
}

bool Renderer::SetCustomShaderScale(int scale){
    #if defined(__ANDROID__)
    //Not supported on Android. TODO.
    scale = 1;
    lowres_scale = 1;
    return true;
    #else
    if (scale < 1){
        scale = 1;
    }
    if (scale > 8){
        scale = 8;
    }
    if (scale == lowres_scale){
        return true;
    }
    lowres_scale = scale;
    if (scale == 1){
        //Left allocated deliberately. An app driving this from a slider will be back in a moment,
        //and a texture the size of the window is not worth churning to save while somebody drags.
        return true;
    }
    if (!RebuildLowResFBO()){
        lowres_scale = 1;   //So the frame still draws, at full resolution, rather than not at all.
        return false;
    }
    return true;
    #endif
}

/*
    Scales the low-res target back over the frame, one NxN block per low-res pixel.

    PREMULTIPLIED, and that is the whole reason this is a shader rather than a blit. The low-res
    target was drawn into with a SEPARATE alpha blend (see CustomShaderPass) so that it holds
    colour already multiplied by coverage, which is the only form in which a stack of translucent
    volumes composites correctly into a buffer that started empty. glBlendFunc(GL_ONE,
    GL_ONE_MINUS_SRC_ALPHA) is the matching "over" for that form. A straight glBlitFramebuffer
    could not apply any blend at all, and would have stamped the volume over the scene.
*/
void Renderer::CompositeLowRes(void){
    if (!lowres_composite_shader || !lowres_composite_shader->f_compiled){
        return;
    }
    lowres_composite_shader->Use();
    //TEXUNIT_LOWRES_COMPOSITE, emphatically not unit 0 - see the note on that define for the
    //nearly-black scene that came of borrowing the shadow map's unit for this.
    glBindTextureUnit(TEXUNIT_LOWRES_COMPOSITE, lowres_tex_id);
    lowres_composite_shader->Setint("lowres_scale",lowres_scale);

    //The volume already resolved its own occlusion against the G-buffer; this is a flat overlay
    //and must not be depth-tested, depth-written or culled by whatever the last sub-pass left.
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(lowres_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    //Back to the state SetOpenGLState established once at start-up, which everything else in the
    //frame assumes is still in force.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/*
    The custom-material pass: everything tagged MESH_MODE_SHADER, one sub-pass per registered
    shader. What a custom shader is given and what is expected of it is stated once, on
    Renderer::AddCustomShader in Renderer.h - this is about how the pass is ordered and why.

    Called last of the geometry passes in DrawFrame, after the skinned meshes, because a custom
    material is typically translucent and has to blend over everything solid - a volume with a
    character standing in it must be drawn after that character, not before.

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
    /*
        Is there a reduced-resolution half this frame at all?

        SCALE 1 MEANS THE FEATURE IS OFF, NOT THAT THE TARGET IS THE SAME SIZE - so a shader that
        has opted in still has to be drawn, at full resolution, straight into the frame. Getting
        this wrong does not draw it small or blurry, it does not draw it AT ALL: it is in neither
        half, and the effect silently vanishes at exactly the setting a person reaches for to
        compare against. Which is what it did, until a fetch count came back as zero.

        Counted before anything is allocated or cleared for the same reason: an app that set a
        scale and then switched its volume off must not pay a target clear and a full-screen
        composite every frame to put nothing on the screen.
    */
    int num_lowres = 0;
    for (int i = 0;i < (int)custom_shaders.size();i++){
        if (custom_shaders.at(i) && custom_shaders.at(i)->f_lowres){
            num_lowres++;
        }
    }
    bool f_lowres_pass = (lowres_scale > 1) && (num_lowres > 0) && RebuildLowResFBO();

    //The full-resolution half first, straight into the frame - every custom shader in every app
    //that has not asked for anything else, plus any that has while the feature is switched off.
    CustomShaderSubPasses(camera,false,f_lowres_pass);
    if (!f_lowres_pass){
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, lowres_fbo_id);
    vec4 clr_clear = vec4(0,0,0,0);
    glClearNamedFramebufferfv(lowres_fbo_id,GL_COLOR,0,(float*)&clr_clear);
    /*
        The viewport, scaled. An app with an offset viewport (Renderer::viewport_x, which Tank
        uses) gets its offset divided too, so a viewport whose origin is not a multiple of the
        scale lands up to one block out. Nothing in the tree does both, and a block is the unit
        this whole feature deals in - noted rather than solved.
    */
    glViewport(viewport_x / lowres_scale,
               viewport_y / lowres_scale,
               (GetViewportWidth()  + lowres_scale - 1) / lowres_scale,
               (GetViewportHeight() + lowres_scale - 1) / lowres_scale);
    /*
        SEPARATE alpha blending, and this is the one line that has to be right.

        The frame's ordinary GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA applied to the ALPHA channel of a
        buffer cleared to zero computes a*a + 0*(1-a) - it squares the coverage. Over the opaque
        frame that never mattered, because nothing read the alpha back; here the composite does,
        and a squared alpha makes every volume render too transparent and a stack of them wrong in
        a way that looks like a density bug. GL_ONE/GL_ONE_MINUS_SRC_ALPHA on alpha is the
        standard "over" accumulation, and it leaves colour premultiplied - which is what
        CompositeLowRes then expects.
    */
    glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);

    CustomShaderSubPasses(camera,true,true);

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);
    glViewport(viewport_x, viewport_y, GetViewportWidth(), GetViewportHeight());
    CompositeLowRes();
}

/*
    One sub-pass per registered custom shader that belongs in this half of the pass. Returns how
    many ran. Everything about what a custom shader is handed is here; CustomShaderPass above is
    only about which target the two halves draw into.

    `f_lowres_pass` is whether there IS a reduced-resolution half this frame, and is what makes
    scale 1 mean "off" rather than "missing": with it false, an opted-in shader is drawn here at
    full resolution along with everything else.
*/
int Renderer::CustomShaderSubPasses(Camera* camera, bool f_lowres, bool f_lowres_pass){
    vec3 eye = camera->GetPosition();
    //What gl_FragCoord is measured against in this half of the pass. A shader sampling the
    //G-buffer must divide by THIS and not by textureSize(gbuffer_depth,0) - the two are the same
    //number at full resolution and are not at all in the low-res half, which is exactly the trap
    //Shader::f_lowres warns about.
    vec2 target_size = f_lowres ? vec2((float)lowres_tex_width,(float)lowres_tex_height)
                                : vec2((float)width,(float)height);
    int num_drawn = 0;
    for (int i = 0;i < (int)custom_shaders.size();i++){
        Shader* shader = custom_shaders.at(i);
        if (!shader){
            continue;
        }
        //Which half this shader is in. Only one that opted in AND has a low-res half to be drawn
        //into is in the low-res one; everything else is full resolution.
        if ((shader->f_lowres && f_lowres_pass) != f_lowres){
            continue;
        }
        shader->Use();
        shader->Setmat4("mat_worldcam",camera->mat_cam);
        //A custom shader that reconstructs a ray - anything raymarched - needs the ray origin,
        //which the default shaders get under this same name.
        shader->Setvec3("eye_position",eye);
        shader->Setvec2("render_target_size",target_size);

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
        num_drawn++;
    }
    return num_drawn;
}

/*
    Hands one shader the cloud shadow map. Called for every shader that lights with the sun,
    which is the default and skinned ones - they share default.frag.

    mat_cloud_shadow is only set when there is a map to point at - there is nothing meaningful to
    hand over otherwise. (That used to be load-bearing: Setmat4 on a missing uniform went through
    debug->Fatal. It warns and returns false now, so this is tidiness rather than survival.)
    f_cloud_shadows is always set, so a shader cannot be left sampling a stale map from a previous
    scene.
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

/*
    Hands one shader the occluder field. Same shape as UploadCloudShadow above, and the same
    reason for the f_ flag: a shader must never be left marching a map that no longer describes
    the scene in front of it.
*/
void Renderer::UploadFieldShadow(Shader* s){
    if (!s){
        return;
    }
    if (!f_field_shadows || (field_tex_id == (GLuint)-1)){
        s->Setint("f_field_shadows",0);
        return;
    }
    glBindTextureUnit(TEXUNIT_FIELD_SHADOW,field_tex_id);
    s->Setmat4("mat_field",mat_field);
    s->Setvec3("field_axis",field_axis);
    s->Setfloat("field_normal_bias",field_normal_bias);
    s->Setint("field_shadow_steps",field_shadow_steps);
    s->Setint("f_field_shadows",1);
}

//Uses a compute shader and uses the textures from deferred pass.
void Renderer::SSAOPass(Camera* camera){
    ssao_compute_shader->Use();
    glBindImageTexture(0, ssao_tex_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindTextureUnit(0, deferred_position_tex_id);
    glBindTextureUnit(1, deferred_normal_tex_id);
    glBindTextureUnit(2, resolve_tex_id);

    ssao_compute_shader->Setmat4("mat_worldcam",camera->mat_cam);
    /*
        The eye and the view axis, because the G-buffer this pass reads is in WORLD space and the
        occlusion test needs a distance from the camera. The shader takes it as
        dot(p - eye, forward), which for a lookat matrix is exactly -z_view - see the block on
        these two uniforms in ssao_compute.comp for why it is not simply a .z.
    */
    ssao_compute_shader->Setvec3("eye_position",camera->GetPosition());
    ssao_compute_shader->Setvec3("camera_forward",camera->GetForward());
    ssao_compute_shader->Setvec2("target_size",vec2((float)width,(float)height));

    //Round UP: `width/32` truncates, so on any width that is not a multiple of 32 the last few
    //columns were never dispatched at all and kept whatever the texture held before. The shader
    //drops the invocations that overshoot.
    glDispatchCompute((width + 31)/32, height, 1);
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

    RenderUniqueMeshes(mesh_mode,-1,true);      //true: occluders only (Object::f_casts_shadow)
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

/*
    Names, in gpu_pass_t order. Also the string pushed as the debug group (see BeginGPUPass), so
    a RenderDoc capture and the Engine panel call the same pass the same thing.
*/
static const char* gpu_pass_names[Renderer::GPU_PASS_COUNT] = {
    "Shadow map",
    "Occluder field",
    "Field jump flood",
    "Deferred G-buffer",
    "Skybox",
    "Color",
    "Skinned",
    "Custom shaders",
    "MSAA resolve",
    "SSAO",
    "Buffer blit",
    "UI overlay",
    "ImGui",
};

const char* Renderer::GetGPUPassName(int pass){
    if ((pass < 0) || (pass >= GPU_PASS_COUNT)){
        return "?";
    }
    return gpu_pass_names[pass];
}

const Renderer::GPUPassTimer* Renderer::GetGPUPassTimer(int pass){
    if ((pass < 0) || (pass >= GPU_PASS_COUNT)){
        return NULL;
    }
    return &gpu_pass_timers[pass];
}

/*
    Query objects for every pass. Called from Init, on the render thread, with a context current -
    query names are context state and this is the only thread that ever has one.

    No extension test: glGenQueries and GL_TIME_ELAPSED are core GL 3.3 and this renderer already
    requires 4.5 for direct state access, so the only way these are missing is a failed
    wglGetProcAddress, which is what the null check catches. The Android port has to do real
    feature detection here (GL_EXT_disjoint_timer_query) - that difference is the whole of what
    separates the two implementations.
*/
bool Renderer::InitGPUPassTimers(){
    f_gpu_timers_supported = glGenQueries && glDeleteQueries && glBeginQuery && glEndQuery
                          && glGetQueryObjectuiv && glGetQueryObjectui64v;
    if (!f_gpu_timers_supported){
        debug->Warn("Timer query entry points missing - per-pass GPU timings will read 0\n");
        return false;
    }
    for (int i=0;i<GPU_PASS_COUNT;i++){
        GPUPassTimer& t = gpu_pass_timers[i];
        glGenQueries(2,t.queries);
        //Registers with PerfTimer's static list and gets the same rolling window as tmr_frame.
        //Never Restart/Stop-ed: AddSample is the only thing that ever writes to it.
        t.timer = new PerfTimer(gpu_pass_names[i]);
    }
    debug->Info("GPU pass timers ready (GL_TIME_ELAPSED, %i passes)\n",(int)GPU_PASS_COUNT);
    return true;
}

void Renderer::DestroyGPUPassTimers(){
    if (!f_gpu_timers_supported){
        return;
    }
    for (int i=0;i<GPU_PASS_COUNT;i++){
        GPUPassTimer& t = gpu_pass_timers[i];
        glDeleteQueries(2,t.queries);
        t.queries[0] = 0;
        t.queries[1] = 0;
        t.f_has_run[0] = false;
        t.f_has_run[1] = false;
    }
    f_gpu_timers_supported = false;
}

void Renderer::BeginGPUPass(int pass){
    if ((pass < 0) || (pass >= GPU_PASS_COUNT)){
        return;
    }
    //The group is pushed even when the queries are unavailable - it costs nothing and a capture
    //is worth having either way.
    if (glPushDebugGroup){
        glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION,(GLuint)pass,-1,gpu_pass_names[pass]);
    }
    if (!f_gpu_timers_supported){
        return;
    }
    if (active_gpu_pass != -1){
        //Not Fatal: instrumentation should never be what takes the app down. But loud, because
        //the alternative is a GL_INVALID_OPERATION surfacing somewhere with no connection to the
        //nesting that caused it. See the block comment on gpu_pass_t.
        debug->Err("BeginGPUPass(%s) while %s is still open - GPU pass scopes cannot nest\n",
                   gpu_pass_names[pass],gpu_pass_names[active_gpu_pass]);
        return;
    }
    GPUPassTimer& t = gpu_pass_timers[pass];
    int idx = t.write_index;
    //Collect the result sitting in the slot about to be reused - it is two frames old, so this
    //does not wait. If it somehow is not ready, skip the sample rather than stall.
    if (t.f_has_run[idx]){
        GLuint available = 0;
        glGetQueryObjectuiv(t.queries[idx],GL_QUERY_RESULT_AVAILABLE,&available);
        if (available){
            GLuint64 ns = 0;
            glGetQueryObjectui64v(t.queries[idx],GL_QUERY_RESULT,&ns);
            if (t.timer){
                t.timer->AddSample((double)ns / 1000.0);    //ns -> us, the unit every timer uses
            }
        }
    }
    glBeginQuery(GL_TIME_ELAPSED,t.queries[idx]);
    active_gpu_pass = pass;
    t.f_begun_this_frame = true;
}

void Renderer::EndGPUPass(int pass){
    if ((pass < 0) || (pass >= GPU_PASS_COUNT)){
        return;
    }
    if (f_gpu_timers_supported && (active_gpu_pass == pass)){
        glEndQuery(GL_TIME_ELAPSED);
        GPUPassTimer& t = gpu_pass_timers[pass];
        t.f_has_run[t.write_index] = true;
        t.write_index = 1 - t.write_index;
        active_gpu_pass = -1;
    }
    if (glPopDebugGroup){
        glPopDebugGroup();
    }
}

void Renderer::EndGPUFrame(){
    if (!f_gpu_timers_supported){
        return;
    }
    if (active_gpu_pass != -1){
        debug->Err("EndGPUFrame with %s still open - a pass is missing its EndGPUPass\n",
                   gpu_pass_names[active_gpu_pass]);
    }
    for (int i=0;i<GPU_PASS_COUNT;i++){
        GPUPassTimer& t = gpu_pass_timers[i];
        //A pass that did not run this frame cost this frame nothing, and saying so is what makes
        //toggling one off visibly decay to zero instead of freezing at its last value.
        if (!t.f_begun_this_frame && t.timer){
            t.timer->AddSample(0.0);
        }
        t.f_begun_this_frame = false;
    }
}

/*
    Byte layout of one picking PBO.

    Three reads into ONE buffer at three offsets rather than three buffers: with a pack buffer
    bound, glReadPixels takes a byte offset, so a whole frame of picking data costs one buffer,
    one bind and one map. 16-byte spacing rather than tight packing because the slack costs
    nothing at 48 bytes and keeps every offset comfortably past GL_PACK_ALIGNMENT.
*/
#define PICK_OFFSET_POSITION    0       //GL_RGBA / GL_HALF_FLOAT  - 8 bytes
#define PICK_OFFSET_NORMAL      16      //GL_RGBA / GL_HALF_FLOAT  - 8 bytes
#define PICK_OFFSET_ID          32      //GL_RED_INTEGER / GL_INT  - 4 bytes
#define PICK_PBO_SIZE           48

/*
    THE FORMATS ABOVE MUST MATCH THE ATTACHMENTS EXACTLY, and that is not a tidiness point - it is
    the difference between this mechanism working and being slower than what it replaced.

    Position and normal are RGBA16F (RebuildDeferredFBO). Asking glReadPixels for GL_RGB/GL_FLOAT
    from them is a channel-count AND type conversion, and there is no GPU path for it: the driver
    does the conversion on the CPU, which means it must have the pixels NOW, which means it waits
    for the GPU to finish the frame. Measured: the first such read cost 4.8 ms while the exact
    format integer read beside it cost 31 us, and reordering them did not move the cost - it
    followed the mismatched read, not the position in the sequence.

    So both are read as GL_RGBA/GL_HALF_FLOAT, exactly what the texture holds, and unpacked here
    instead. Anyone changing a deferred attachment's internal format has to change its read too.
*/
static float HalfToFloat(uint16_t h){
    uint32_t sign = (uint32_t)(h >> 15) << 31;
    uint32_t exponent = (h >> 10) & 0x1F;
    uint32_t mantissa = h & 0x3FF;
    uint32_t bits;
    if (exponent == 0){
        if (mantissa == 0){
            bits = sign;                        //+-0
        }else{
            //Subnormal half. Renormalise into the float exponent range by shifting the mantissa
            //up until its implicit leading bit appears, paying one exponent step per shift.
            exponent = 127 - 15 + 1;
            while ((mantissa & 0x400) == 0){
                mantissa <<= 1;
                exponent--;
            }
            mantissa &= 0x3FF;
            bits = sign | (exponent << 23) | (mantissa << 13);
        }
    }else if (exponent == 31){
        bits = sign | 0x7F800000u | (mantissa << 13);    //inf / NaN
    }else{
        bits = sign | ((exponent - 15 + 127) << 23) | (mantissa << 13);
    }
    float f;
    memcpy(&f,&bits,sizeof(f));
    return f;
}

//One RGBA16F texel out of the mapped buffer, as three floats. The alpha channel carries nothing
//either of these two attachments needs.
static void UnpackHalf3(const void* src, float* out){
    uint16_t raw[4];
    memcpy(raw,src,sizeof(raw));
    out[0] = HalfToFloat(raw[0]);
    out[1] = HalfToFloat(raw[1]);
    out[2] = HalfToFloat(raw[2]);
}

bool Renderer::InitPickingPBOs(){
    if (!glCreateBuffers || !glNamedBufferData || !glBindBuffer || !glMapNamedBufferRange
        || !glUnmapNamedBuffer || !glFenceSync || !glDeleteSync || !glGetSynciv){
        debug->Warn("Pixel pack buffer entry points missing - picking stays synchronous\n");
        return false;
    }
    glCreateBuffers(PICK_PBO_SLOTS,picking_pbo);
    for (int i=0;i<PICK_PBO_SLOTS;i++){
        //GL_STREAM_READ: written by the GPU once, read by the CPU once, then thrown away. It is a
        //hint, but it is the honest description and the one drivers optimise this path for.
        glNamedBufferData(picking_pbo[i],PICK_PBO_SIZE,NULL,GL_STREAM_READ);
        f_picking_pbo_has_data[i] = false;
        picking_fence[i] = 0;
        picking_id_snapshot[i].clear();
    }
    debug->Info("Async picking readback ready (%i pixel pack buffers)\n",PICK_PBO_SLOTS);
    return true;
}

void Renderer::DestroyPickingPBOs(){
    if (picking_pbo[0] == 0){
        return;
    }
    glDeleteBuffers(PICK_PBO_SLOTS,picking_pbo);
    for (int i=0;i<PICK_PBO_SLOTS;i++){
        picking_pbo[i] = 0;
        f_picking_pbo_has_data[i] = false;
        if (picking_fence[i]){
            glDeleteSync(picking_fence[i]);
            picking_fence[i] = 0;
        }
    }
}

//See the block comment on this in Renderer.h for why it is asynchronous and what the one-frame
//lag costs. Collect first, then issue: the slot being read is the one NOT about to be written,
//so it has had a full frame to land and the map never waits.
void Renderer::ReadPickingAsync(InputController* input, int mouse_x, int mouse_y){
    if (!input || (picking_pbo[0] == 0) || (deferred_fbo_id == (GLuint)-1)){
        return;
    }
    //Same convention as the synchronous version this replaced: the mouse is measured from the top
    //of the window, GL reads from the bottom, and the deferred FBO is always full window size.
    int gl_y = height - mouse_y;
    //The slot about to be reused is the OLDEST - written PICK_PBO_SLOTS frames ago - so it is the
    //one with the best chance of having landed. Consuming it here, immediately before overwriting
    //it, is the same trick BeginGPUPass uses on its query objects.
    int slot = picking_pbo_write_index;

    //Poll, never wait. glMapNamedBufferRange on a buffer the GPU has not finished writing blocks
    //until it has, which is precisely the stall this whole mechanism exists to avoid - measured
    //at 6.65 ms, WORSE than the synchronous readback, before this check existed.
    bool f_ready = false;
    if (f_picking_pbo_has_data[slot] && picking_fence[slot]){
        GLint status = GL_UNSIGNALED;
        glGetSynciv(picking_fence[slot],GL_SYNC_STATUS,1,NULL,&status);
        f_ready = (status == GL_SIGNALED);
    }

    if (f_ready){
        void* mapped = glMapNamedBufferRange(picking_pbo[slot],0,PICK_PBO_SIZE,GL_MAP_READ_BIT);
        if (mapped){
            float position[3] = {0,0,0};
            float normal[3] = {0,0,0};
            int32_t object_index = -1;
            UnpackHalf3((const char*)mapped + PICK_OFFSET_POSITION,position);
            UnpackHalf3((const char*)mapped + PICK_OFFSET_NORMAL,normal);
            memcpy(&object_index,(const char*)mapped + PICK_OFFSET_ID,sizeof(object_index));
            glUnmapNamedBuffer(picking_pbo[slot]);

            //Resolved against the list this read was issued against, NOT the current one.
            const std::vector<objectid_t>& snapshot = picking_id_snapshot[slot];
            //-1 is DeferredPass's cleared value, i.e. the mouse is over nothing. The upper bound
            //is >= rather than >: an index equal to size() used to reach .at(size()), which with
            //-fno-exceptions aborts the process rather than throwing.
            if ((object_index < 0) || ((size_t)object_index >= snapshot.size())){
                input->SetHoveredObjectID(OBJECTID_INVALID);
                input->SetHoveredNormal(vec3());
                input->SetHoveredPosition(vec3());
            }else{
                input->SetHoveredObjectID(snapshot.at(object_index));
                vec3 n = vec3(normal[0],normal[1],normal[2]);
                input->SetHoveredNormal(n.normalize());
                input->SetHoveredPosition(vec3(position[0],position[1],position[2]));
            }
        }
    }

    //The mouse outside the window would make glReadPixels read outside the framebuffer, which is
    //undefined rather than an error. Issue nothing, and mark the slot empty so next frame does
    //not present whatever was in it as an answer.
    //A slot whose fence had not signalled is about to be overwritten, losing that sample. That is
    //deliberate: skipping the write instead would stop picking permanently if a fence ever stuck,
    //and one dropped hover update is invisible. The fence goes either way - it has been consumed
    //or it has been abandoned, and in both cases it must not outlive the data it described.
    if (picking_fence[slot]){
        glDeleteSync(picking_fence[slot]);
        picking_fence[slot] = 0;
    }

    //The mouse outside the window would make glReadPixels read outside the framebuffer, which is
    //undefined rather than an error. Issue nothing, and mark the slot empty so a later frame does
    //not present whatever was in it as an answer.
    if ((mouse_x < 0) || (mouse_x >= width) || (gl_y < 0) || (gl_y >= height)){
        f_picking_pbo_has_data[slot] = false;
        picking_pbo_write_index = (slot + 1) % PICK_PBO_SLOTS;
        return;
    }

    //clear() keeps the capacity, so this allocates once and then never again.
    std::vector<objectid_t>& snapshot = picking_id_snapshot[slot];
    snapshot.clear();
    snapshot.reserve(renderable_objects.size());
    for (Object* object:renderable_objects){
        snapshot.push_back(object->GetID());
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, deferred_fbo_id);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, picking_pbo[slot]);
    //The last argument is a byte OFFSET into the bound buffer, not a pointer. That is the whole
    //difference between this and the blocking version.
    glReadBuffer(GL_COLOR_ATTACHMENT3);
    glReadPixels(mouse_x,gl_y,1,1,GL_RED_INTEGER,GL_INT,(void*)(uintptr_t)PICK_OFFSET_ID);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(mouse_x,gl_y,1,1,GL_RGBA,GL_HALF_FLOAT,(void*)(uintptr_t)PICK_OFFSET_POSITION);
    glReadBuffer(GL_COLOR_ATTACHMENT1);
    glReadPixels(mouse_x,gl_y,1,1,GL_RGBA,GL_HALF_FLOAT,(void*)(uintptr_t)PICK_OFFSET_NORMAL);
    //NOT optional. A pack buffer left bound silently redirects the NEXT glReadPixels anywhere in
    //the engine into it - CaptureScreenshotIfRequested is the one that would hit, and it would
    //hand back an empty image with no error anywhere.
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    //Signalled once the GPU has actually executed the three reads above, which is what the poll
    //at the top of the next visit to this slot is asking about.
    picking_fence[slot] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    f_picking_pbo_has_data[slot] = true;
    picking_pbo_write_index = (slot + 1) % PICK_PBO_SLOTS;
}

void Renderer::DrawFrame(const std::vector<Object*>& objects, Camera* camera, Shader* shader, InputController* input){
    if (!camera){
        debug->Fatal("DrawFrame called without camera.\n");
    }
    if (!shader){
        debug->Fatal("DrawFrame called without shader.\n");
    }
    if (tmr_frame){
        tmr_frame->Restart();
    }

    PrepareObjects(objects);

    camera->viewport.width = GetViewportWidth();
    camera->viewport.height = GetViewportHeight();
    //...and where that viewport starts, so GetPixelRay can turn a window pixel into a ray. The
    //y flip is the whole point of these two fields: viewport_y above is measured from the BOTTOM
    //of the window because glViewport wants it that way, and the mouse is measured from the TOP,
    //so somebody has to convert and this is the one place that knows both. See the comment on
    //Camera::viewport.px_offset_x.
    camera->viewport.px_offset_x = (float)viewport_x;
    camera->viewport.px_offset_y = (float)(height - (viewport_y + GetViewportHeight()));
    camera->CalculateLookatMatrix();

    BeginGPUPass(GPU_PASS_SHADOW);
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
    EndGPUPass(GPU_PASS_SHADOW);

    //The occluder field, for apps that asked for one. Before the deferred pass rather than after
    //it only because both rebind the framebuffer and the viewport, and this keeps all of the
    //off-screen passes together ahead of the colour pass that consumes them.
    RenderFieldPass();

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
        BeginGPUPass(GPU_PASS_DEFERRED);
        DeferredPass(camera);
        EndGPUPass(GPU_PASS_DEFERRED);
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
        BeginGPUPass(GPU_PASS_SKYBOX);
        DrawSkyBox(camera);
        EndGPUPass(GPU_PASS_SKYBOX);
    }
    //Opens here rather than at RenderUniqueMeshes so the material and light SSBO uploads below
    //are counted with the pass that consumes them - they are GL work on the same timeline.
    BeginGPUPass(GPU_PASS_COLOR);
    shader->Use();

    UploadMaterials();
    UploadLights();

    shader->Setint("f_environment_reflections",f_use_reflections);
    shader->Setfloat("cone_softness",cone_softness);
    shader->Setint("f_materialindex_is_color",0);
    UploadCloudShadow(shader);
    UploadFieldShadow(shader);
    RenderUniqueMeshes(MESH_MODE_NORMAL);
    //Lines through their own program - see line.vert. f_materialindex_is_color, which used to
    //make default.frag paint them white, is no longer part of this pass.
    line_shader->Use();
    line_shader->Setmat4("mat_worldcam",camera->mat_cam);
    RenderUniqueMeshes(MESH_MODE_LINE);
    shader->Use();
    EndGPUPass(GPU_PASS_COLOR);



    if (skinned_shader && camera){
        BeginGPUPass(GPU_PASS_SKINNED);
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
        UploadFieldShadow(skinned_shader);
        RenderUniqueMeshes(MESH_MODE_SKINNED);
        EndGPUPass(GPU_PASS_SKINNED);
    }

    //Custom materials go last of the geometry passes, AFTER the skinned meshes: they are
    //typically translucent and do not write depth, so anything solid has to already be in the
    //buffer for them to blend over. A character standing inside a volume was previously drawn
    //on top of it at full strength.
    BeginGPUPass(GPU_PASS_CUSTOM);
    CustomShaderPass(camera);
    EndGPUPass(GPU_PASS_CUSTOM);

    BeginGPUPass(GPU_PASS_RESOLVE);
    ResolveAA();
    EndGPUPass(GPU_PASS_RESOLVE);

    //The mouse-over readback. Asynchronous since 2026-09-17 - it used to block here for most of
    //a frame; see Renderer.h's block comment on ReadPickingAsync for what it was costing and what
    //the one-frame lag it trades for costs instead. The timer stays so the panel keeps showing it.
    if ((pipeline == PIPELINE_DEFERRED) && input){
        if (tmr_pick_readback){
            tmr_pick_readback->Restart();
        }
        int2 mouse = input->GetRelativeMousePosition();
        ReadPickingAsync(input,mouse.x,mouse.y);
        if (tmr_pick_readback){
            tmr_pick_readback->Stop();
        }
    }

    if (f_ssao){
        BeginGPUPass(GPU_PASS_SSAO);
        SSAOPass(camera);
        EndGPUPass(GPU_PASS_SSAO);
    }

    //Look at one of the intermediate buffers
    const bool f_blit_view = (view_buffer >= 1) && (view_buffer <= 3);
    if (f_blit_view){
        BeginGPUPass(GPU_PASS_BLIT);
    }
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
    if (f_blit_view){
        EndGPUPass(GPU_PASS_BLIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);

    //resolve_fbo_id's GL_COLOR_ATTACHMENT0 now holds this frame's fully-resolved output
    //(see ResolveAA/BlitBufferTarget, which always blit into it) - the right place to grab
    //a screenshot from, before anything else gets a chance to rebind the framebuffer.
    //
    //This is the scene-only capture point. The UI-inclusive one is in Application::DrawFrame,
    //after ImGui has drawn into this same buffer; a request picks one or the other.
    CaptureScreenshotIfRequested(false);

    ClearObjectBatches();

    if (tmr_frame){
        tmr_frame->Stop();
    }
}

std::vector<uint8_t> Renderer::RequestScreenshot(bool f_include_ui, int timeout_ms){
    std::unique_lock<std::mutex> lock(screenshot_mutex);
    screenshot_requested = true;
    screenshot_include_ui = f_include_ui;
    screenshot_ready = false;
    bool got = screenshot_cv.wait_for(lock,std::chrono::milliseconds(timeout_ms),[this]{ return screenshot_ready; });
    if (!got){
        debug->Warn("RequestScreenshot: timed out after %dms waiting for the render thread\n",timeout_ms);
        return {};
    }
    return screenshot_png; //copy out while still holding the lock
}

void Renderer::CaptureScreenshotIfRequested(bool f_after_ui){
    std::unique_lock<std::mutex> lock(screenshot_mutex);
    //Both capture points call this every frame; only the one the request asked for services it.
    if (!screenshot_requested || (screenshot_include_ui != f_after_ui)){
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

bool Renderer::RebuildFieldFBO(int size){
    debug->Info("(Re)Building buffers for the occluder field: %i x %i\n",size,size);
    if (field_fbo_id == (GLuint)-1){
        glCreateFramebuffers(1, &field_fbo_id);
    }
    if (field_tex_id != (GLuint)-1){
        glDeleteTextures(1, &field_tex_id);
    }
    glCreateTextures(GL_TEXTURE_2D, 1, &field_tex_id);

    //RGBA16F, not a depth format: see shaders/field.frag for what the four channels mean. Half
    //floats are plenty - these are world heights in a scene a few tens of units across, and the
    //clear sentinels below sit nowhere near half's 65504 ceiling.
    glTextureStorage2D(field_tex_id, 1, GL_RGBA16F, size, size);
    //LINEAR would interpolate a height across the gap between an occluder and thin air, putting
    //a ramp of fictional geometry around every edge. The march wants the texel it landed in.
    glTextureParameteri(field_tex_id, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(field_tex_id, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTextureParameteri(field_tex_id, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTextureParameteri(field_tex_id, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glNamedFramebufferTexture(field_fbo_id, GL_COLOR_ATTACHMENT0, field_tex_id, 0);
    GLenum field_draw_buffer = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(field_fbo_id, 1, &field_draw_buffer);

    //The jump flood's ping-pong pair. Not attached to any framebuffer - they are only ever bound
    //as images to the compute shader, and they hold seed coordinates rather than anything that
    //could be displayed.
    for (int i = 0; i < 2; i++){
        if (field_seed_tex_id[i] != (GLuint)-1){
            glDeleteTextures(1, &field_seed_tex_id[i]);
        }
        glCreateTextures(GL_TEXTURE_2D, 1, &field_seed_tex_id[i]);
        glTextureStorage2D(field_seed_tex_id[i], 1, GL_RG16F, size, size);
        glTextureParameteri(field_seed_tex_id[i], GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTextureParameteri(field_seed_tex_id[i], GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }

    field_texture_size = size;
    return CheckFrameBuffer();
}

bool Renderer::EnableFieldShadows(Camera* camera, const vec3& axis, int size){
    if (!camera){
        debug->Err("EnableFieldShadows called without a camera\n");
        return false;
    }
    if (!field_shader){
        field_shader = new Shader("shaders/field.vert","shaders/field.frag");
    }
    if (!field_jfa_shader){
        field_jfa_shader = new Shader();
        field_jfa_shader->CreateComputeShader("shaders/field_jfa.comp");
    }
    if ((field_tex_id == (GLuint)-1) || (size != field_texture_size)){
        if (!RebuildFieldFBO(size)){
            return false;
        }
    }
    field_camera = camera;
    field_axis = axis;
    f_field_shadows = true;
    return true;
}

/*
    Fills the occluder field for this frame.

    One pass, no depth buffer, no culling, no sorting: MIN/MAX blending does the reduction, so
    every fragment of every surface can arrive in any order and the texel still ends up holding
    the highest and lowest of them. Back faces are wanted here, which is the other reason the
    depth pass could not have been reused - that one culls them on purpose.

    Rebuilt every frame. Nothing here caches against a dirty flag yet; the pass is one extra draw
    of the same geometry at a fixed resolution, and an app whose world only changes occasionally
    can skip frames by toggling f_field_shadows once that becomes worth measuring.
*/
void Renderer::RenderFieldPass(){
    if (!f_field_shadows || !field_camera || !field_shader){
        return;
    }
    //Square, so aspect is 1 and the camera's zoom is a half-extent in world units on both axes.
    //Set here rather than trusted from the app because CalculateLookatMatrix divides by these.
    field_camera->viewport.width  = (float)field_texture_size;
    field_camera->viewport.height = (float)field_texture_size;
    field_camera->CalculateLookatMatrix();
    mat_field = field_camera->mat_cam;

    BeginGPUPass(GPU_PASS_FIELD);
    glBindFramebuffer(GL_FRAMEBUFFER, field_fbo_id);
    //R very low and A very high, so an untouched column reads as a slab that contains nothing -
    //CalcFieldShadow needs no separate occupancy test. G starts at 0 for the jump flood.
    float clear[4] = {-1000.0f,0.0f,0.0f,1000.0f};
    glClearNamedFramebufferfv(field_fbo_id,GL_COLOR,0,clear);
    glViewport(0,0,field_texture_size,field_texture_size);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    //Blending is enabled globally (SetOpenGLState), so only the equation changes. MIN and MAX
    //ignore the blend factors entirely, which is why glBlendFunc is left as it is.
    glBlendEquationSeparate(GL_MAX,GL_MIN);

    field_shader->Use();
    field_shader->Setmat4("mat_field",mat_field);
    field_shader->Setvec3("field_axis",field_axis);
    //Skinned meshes are deliberately absent: they would need their own variant of field.vert to
    //apply the bone transforms, and nothing that uses this has any yet. A skinned character
    //currently receives field shadows but does not cast one.
    RenderUniqueMeshes(MESH_MODE_NORMAL,-1,true);   //true: occluders only (Object::f_casts_shadow)

    glBlendEquationSeparate(GL_FUNC_ADD,GL_FUNC_ADD);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    EndGPUPass(GPU_PASS_FIELD);

    //Timed separately rather than nested inside the scope above, because nesting is illegal -
    //see the block comment on gpu_pass_t. The dispatches are the half a CPU timer could never
    //see: for compute, submission and execution have nothing to do with each other.
    BeginGPUPass(GPU_PASS_FIELD_JFA);
    FieldDistancePass();
    EndGPUPass(GPU_PASS_FIELD_JFA);
}

/*
    Turns the heights just rasterised into a distance field, in the channel field.frag left at
    zero. See shaders/field_jfa.comp for the algorithm and for why it is a jump flood.

    log2(size) + 2 dispatches: seed, one flood per halving jump distance, resolve. The barrier
    between them is not optional - each pass reads every texel the previous one wrote, including
    texels owned by other workgroups, which is exactly the case glMemoryBarrier exists for.
*/
void Renderer::FieldDistancePass(){
    if (!field_jfa_shader || (field_tex_id == (GLuint)-1)){
        return;
    }
    const int groups = (field_texture_size + 7) / 8;    //local_size is 8x8
    //The ortho box is square and `zoom` is its half-extent, so this is the world size of one
    //texel on both axes. It is the only thing that turns the flood's texel counts into the world
    //units everything else in the field is measured in.
    float world_per_texel = (2.0f * field_camera->viewport.zoom) / (float)field_texture_size;

    field_jfa_shader->Use();
    glBindImageTexture(0, field_tex_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    field_jfa_shader->Setfloat("world_per_texel",world_per_texel);

    //Seed: occupied texels become their own seed, in [0].
    glBindImageTexture(2, field_seed_tex_id[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
    field_jfa_shader->Setint("stage",0);
    glDispatchCompute(groups,groups,1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    //Flood, halving the jump each round until neighbours are adjacent. Starting at half the map
    //rather than the whole of it is what makes this log2(size) passes and not size passes.
    int src = 0;
    for (int jump = field_texture_size / 2; jump >= 1; jump /= 2){
        glBindImageTexture(1, field_seed_tex_id[src],   0, GL_FALSE, 0, GL_READ_ONLY,  GL_RG16F);
        glBindImageTexture(2, field_seed_tex_id[1-src], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG16F);
        field_jfa_shader->Setint("stage",1);
        field_jfa_shader->Setint("jump",jump);
        glDispatchCompute(groups,groups,1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        src = 1 - src;
    }

    //Resolve: seed coordinates become a world-space distance, written back into the field's G.
    glBindImageTexture(1, field_seed_tex_id[src], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG16F);
    field_jfa_shader->Setint("stage",2);
    glDispatchCompute(groups,groups,1);
    //Against the TEXTURE fetch bit, not the image one: the next reader is default.frag sampling
    //this through a sampler2D, which is a different path to the one the dispatches above used.
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
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

/*
    Set true to let GL_DEBUG_SEVERITY_NOTIFICATION through as well. Off by default because some
    drivers narrate every buffer allocation at that level and bury everything else; on when you
    are asking a driver why it is doing something rather than whether it failed.
*/
bool f_gl_log_notifications = false;

void opengl_message_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, char const* message, void const* user_param){
    /*
        WHAT GETS THROUGH, AND WHY IT IS NOT JUST `HIGH` ANY MORE.

        This dropped everything below GL_DEBUG_SEVERITY_HIGH. HIGH is the severity a driver uses
        for "that call was an error". It is NOT the one it uses for "that call was legal and I am
        doing something other than what you expect with it" - undefined behaviour, an unsupported
        vertex format quietly substituted, a fallback to software. Those arrive as MEDIUM, LOW or
        NOTIFICATION.

        Which is the entire reason the Intel material-index bug was invisible: the picture was
        wrong, the log was empty, and this callback was the thing that should have said so. A
        driver difference that shows up as UNDEFINED_BEHAVIOR at MEDIUM is exactly the class of
        bug this engine hits when it moves between vendors, so that class must not be filtered
        out by default.
    */
    if ((severity == GL_DEBUG_SEVERITY_NOTIFICATION) && !f_gl_log_notifications){
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
    last_texture_unit = TEXUNIT_MATERIAL_FIRST;

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

    //Material textures are handed units from TEXUNIT_MATERIAL_FIRST upwards with no upper bound,
    //so a scene with enough of them runs off the top of the range - past the last entry the
    //shader's material_texture[] actually has, and past what the driver will accept. Under the
    //old layout an overflow silently landed on the reserved units above; now it lands nowhere,
    //which the shader draws as its missing-material magenta. Say it out loud either way, because
    //"some surfaces went magenta" is not a sentence that points here on its own.
    if (last_texture_unit > num_texture_units){
        debug->Warn("Material textures reached unit %i, past the end of the material range "
                    "(%i units from %i) - the surfaces above it will draw as missing\n",
                    last_texture_unit,NUM_MATERIAL_UNITS,TEXUNIT_MATERIAL_FIRST);
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
        // Built via LoadFromEquirectangular — one GL object, one unit, bind once.
        debug->Info("UploadCubeMap: binding equirectangular cubemap to unit %i\n",TEXUNIT_SKYBOX_CUBEMAP);
        glBindTextureUnit(TEXUNIT_SKYBOX_CUBEMAP, cubemap->cubemap_id);
        return;
    }

    /*
        THE SIX-FACE PATH IS GONE, and this is the error rather than an attempt at it.

        It bound the faces to six CONSECUTIVE units from the cubemap's own - which under the old
        layout ran 24..29 straight through TEXUNIT_APP_RESERVED, both shadow maps and the low-res
        composite, and under this one would run 7..12 through the same reserved run and into the
        materials. That was the "binds the wrong texture units" TODO.

        Renumbering it was never the fix, because it could not have worked at any numbering: the
        shaders declare `samplerCube environment_map` on ONE unit, and six sampler2Ds spread over
        six units is not something a samplerCube can read. Its only caller was already commented
        out in ApplicationGrid.cpp; every live cubemap comes from LoadFromEquirectangular above.

        Loading six faces into one GL_TEXTURE_CUBE_MAP is what this would have to do, and
        CubeMap::LoadFromEquirectangular already contains that code - see its glTextureSubImage3D
        loop. Say so here rather than in a commit message.
    */
    debug->Err("UploadCubeMap: this cubemap has no GL cubemap object. Six loose face textures are "
               "no longer bound - build it with LoadFromEquirectangular, or give CubeMap a "
               "six-face loader that fills one GL_TEXTURE_CUBE_MAP\n");
}

//Convert all the active lights in the scene to a list
void Renderer::UploadLights(){
    debug->Trace("Uploading Lights\n");
    glsl_lights.clear();
    for (Light* l:visible_lights){
        light_t light;
        //light_t has carried this field since it was written and nothing ever filled it, so
        //f_casts_shadow stopped at the CPU. Only the point light path reads it so far, to decide
        //whether to march the occluder field; the sun ignores it and still shadows through the
        //depth map, which is the behaviour every existing scene is tuned around.
        light.shadow = l->f_casts_shadow ? 1 : 0;
        //Resolved here rather than in the shader so the sentinel never leaves the CPU: what goes
        //into the SSBO is always a real radius. See Light::radius on why negative means "use the
        //scene-wide default" and why that is a safe thing to encode in the value.
        light.radius = (l->radius >= 0.0f) ? l->radius : field_light_radius;
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

//Depth-first over the object tree, re-uploading each distinct mesh once. `done` is a plain vector
//walked linearly: a scene has a handful of distinct meshes however many objects share them, so a
//set would cost more than it saved.
static void ReUploadMeshesRecursive(Object* object, std::vector<Mesh*>& done){
    Mesh* mesh = object->GetMesh();
    if (mesh){
        bool f_seen = false;
        for (Mesh* m:done){
            if (m == mesh){
                f_seen = true;
                break;
            }
        }
        if (!f_seen){
            done.push_back(mesh);
            //A no-op for a mesh with no CPU-side vertices, which is what makes this safe to call
            //blindly over the whole tree.
            mesh->ReUploadMeshData();
        }
    }
    for (Object* child:object->children){
        ReUploadMeshesRecursive(child,done);
    }
}

//See the header for why this exists when nothing on Windows can reach it.
void Renderer::ReUploadAllMeshes(const std::vector<Object*>& objects){
    std::vector<Mesh*> done;
    for (Object* object:objects){
        ReUploadMeshesRecursive(object,done);
    }
    debug->Info("Re-uploaded %d distinct meshes for the new GL context\n",(int)done.size());
}

//DeleteDestroyedObjects lives on Scene now - it erases from the object list, and Scene owns that.
//"until something better comes to mind" was the note here for a long time; this was it.