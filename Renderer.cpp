
#include "Renderer.h"

#include "Debug.h"
static Debugger* debug = new Debugger("Renderer",DEBUG_ALL);

#define TEXTURE_WIDTH   128
#define TEXTURE_HEIGHT  128


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

    //Make an object
    cube = new Object();
    cube->SetMesh(new Mesh());
    objects.push_back(cube);

    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glEnable(GL_TEXTURE_2D);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);

    // Create and bind the texture to the pbuffer's rendering context.
    InitCheckerPatternTexture();
    glBindTextureUnit(0, texture_id);
    return true;
}

void Renderer::InitCheckerPatternTexture(){
    // Procedurally create a black and white checker pattern texture.
    // Adapted from the "OpenGL Programming Guide" (aka the red book).

    BYTE *pPixels = 0;
    BYTE c = 0;
    UINT pitch = TEXTURE_WIDTH * 4;
    std::vector<BYTE> checkerImage(pitch * TEXTURE_HEIGHT);

    for (UINT i = 0; i < TEXTURE_HEIGHT; ++i){
        for (UINT j = 0; j < TEXTURE_WIDTH; ++j){
            c = (BYTE)((((i & 0x10) == 0) ^ ((j & 0x10) == 0)) * 255);
            pPixels = &checkerImage[i * pitch + (j * 4)];
            pPixels[0] = pPixels[1] = pPixels[2] = c;
            if (c == 0){
                pPixels[3] = 255;
            }else{
                pPixels[3] = 200;
            }
        }
    }

    glCreateTextures(GL_TEXTURE_2D, 1, &texture_id);

    glTextureParameteri(texture_id, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTextureParameteri(texture_id, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);

    glTextureStorage2D(texture_id, 1, GL_RGBA8, TEXTURE_WIDTH, TEXTURE_WIDTH);
    glTextureSubImage2D(texture_id, 0, 0, 0, TEXTURE_WIDTH, TEXTURE_WIDTH, GL_RGBA, GL_UNSIGNED_BYTE, &checkerImage[0]);
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

    debug->Info("Rebuilding unique list. batch_ids.size() = %i unique_mesh.size()=%i\n",batch_ids.size(),unique_meshes.size());
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
    debug->Info("objects.size() = %i\n",objects.size());
    debug->Info("batch_ids.size() = %i\n",batch_ids.size());

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
            debug->Info("batch_ids.at(mesh_index=%i) mesh_id = %lu\n",mesh_index,object->GetMeshID());
            int32_t id = object_index;
            batch_ids.at(mesh_index)->push_back(id);
        }else{
            debug->Err("Object '%s' did not render while it should have.\n",object->name.c_str());
        }
    }
    debug->Info("num_rendered_objects = %i\n",num_rendered_objects);
    if (num_rendered_objects == 0){
        debug->Info("Nothing to be rendered\n");
        return;
    }
}

//Each unique mesh gets a single drawcall with an associated SSBO with all object parameters per instance.
void Renderer::RenderUniqueMeshes(){
    instancedata.clear();

    for (int i = 0;i<unique_meshes.size();i++){
        Mesh* mesh = unique_meshes.at(i);
        int batch_index = unique_meshes.at(i)->batch_index;
        if (batch_ids.at(batch_index)->size() == 0){
            debug->Fatal("No batches for meshindex %i\n",batch_index);
        }
        for (uint32_t object_index : *batch_ids.at(batch_index)){
            Object* object = renderable_objects.at(object_index);
            debug->Info("Object (mesh_index %i) obj_index: %lu object->GetID() %lu\n",batch_index,object_index,object->GetID());

            InstanceData data;
            data.mat_transformscale = object->GetWorldTransformScaleMatrix();
            instancedata.push_back(data);
        }

        debug->Info("Rendering instances\n");
        mesh->RenderInstances(mesh->batch_num_instances);
        mesh->batch_num_instances = 0;
    }
}


void Renderer::DrawObjects(){
    //First, we cull all objects we are sure of are not visible.
    //Then we make a list of all objects that need to be rendered.
    //Of those objects, we make a list for each unique mesh with object attributes and object ids.
    CullObjects();
    RebuildUniqueMeshList();
    ClearBatches();
    FillBactches();

    RenderUniqueMeshes();
}


void Renderer::DrawFrame(Shader* shader){
    //Select the mutisampled frambuffer
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);

    glViewport(0, 0, width, height);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    DrawObjects();

    cube->Rotate();
    shader->Setmat3("obj_rotate",cube->mat_rotation);

    //Mesh* mesh = cube->GetMesh();
    //mesh->RenderInstances();

    ResolveAA();
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);
}

//Create the required Shader Storage Buffer
bool Renderer::InitSSBO(){
    glCreateBuffers(1, (GLuint*)&instdata_ssbo);
    glNamedBufferStorage(instdata_ssbo, 0 , NULL, GL_DYNAMIC_STORAGE_BIT);
    //glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, instdata_ssbo);
    return true;
}

//Create all the frame and renderbuffers
bool Renderer::InitFBO(){
    glGenFramebuffers(1, &msaa_fbo_id);
    glGenRenderbuffers(1, &color_rbo_id);
    glGenRenderbuffers(1, &depth_rbo_id);

    glGenFramebuffers(1, &resolve_fbo_id);
    glGenRenderbuffers(1, &resolve_rbo_id);

    if (msaa_fbo_id == -1){
        return false;
    }

    int aa_samples = 16;

    //Setup buffers:
    //Mutisampled color 16bit float
    glBindFramebuffer(GL_FRAMEBUFFER, msaa_fbo_id);
    glBindRenderbuffer(GL_RENDERBUFFER, color_rbo_id);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, aa_samples, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color_rbo_id);
    CheckFrameBuffer();

    //32-bit depth
    glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_id);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, aa_samples, GL_DEPTH_COMPONENT32F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_id);
    CheckFrameBuffer();

    //The resolve buffer
    glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo_id);
    glBindRenderbuffer(GL_RENDERBUFFER, resolve_rbo_id);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA16F, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolve_rbo_id);
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
