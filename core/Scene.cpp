#include "glad.h"

#include "Scene.h"
#include "OBJLoader.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Scene", DEBUG_ALL);

Scene::Scene(){

};

void Scene::SetupExample(){
    //TODO Fix clean
    //A single mesh
    //Mesh* cube_mesh = new Mesh();
    //cube_mesh->LoadUnitCube();

    Mesh* sphere_mesh = OBJLoader::ParseOBJFile("sphere.obj");
    //Mesh* char_mesh = OBJLoader::ParseOBJFile("chara.obj");

    //Make a bunch of objects
    for (int i = 0;i<5;i++){
        cube = new Object();
        //cube->SetMesh(cube_mesh);
        renderer->objects.push_back(cube);
        cube->SetPosition(vec3(0.5,0.5,0.0));
        cube->material_slot[0] = i%2;
        cube->material_slot[1] = i%2;
    }

    //Make a bunch of spheres
    for (int i = 0;i<5;i++){
        cube = new Object();
        cube->SetMesh(sphere_mesh);
        renderer->objects.push_back(cube);
        cube->SetPosition(vec3(-0.5,0.5,0.0));
        cube->material_slot[0] = i%2;
        cube->material_slot[1] = i%2;
    }

    camera = new Camera();
    camera->SetPosition(vec3(0,0.5,8));
    camera->SetLookAt(vec3());
    camera->SetupPerspective(renderer->width,renderer->height,45,0.1,100);
    renderer->objects.push_back(camera);

    // Create a texture
    tex_1 = new Texture();
    //tex_1->Create2D(128,128,GL_RGBA8,GL_TEXTURE_2D,1);
    //TODO: Fix this thing in its entirity.

    //Create a compute shader that will massage the texture.
    Shader* comp_shader = new Shader();
    comp_shader->CreateComputeShader("texture.comp");
    comp_shader->Use();
    //glBindImageTexture(0, texture->texture_id, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindImageTexture(1, tex_1->texture_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
    glDispatchCompute(tex_1->width, tex_1->height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);


    //A second texture
    tex_2 = new Texture();
    //tex_2->Create2D(128,128,GL_RGBA8,GL_TEXTURE_2D,1);
    //TODO: Fix this thing in its entirity.


    comp_shader->Setint("pattern",1);
    //glBindImageTexture(0, texture_2->texture_id, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA8);
    glBindImageTexture(1, tex_2->texture_id, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
    glDispatchCompute(tex_2->width, tex_2->height, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    glBindTextureUnit(0, tex_1->texture_id);
    glBindTextureUnit(1, tex_2->texture_id);

    //Create a material
    /* TODO FIX or remove
    material_t m;
    m.color = vec4(0,1,0,1);
    m.texture_unit = -1;
    renderer->materials.push_back(m);

    m.color = vec4(0.5,0.5,0.50,1);
    m.texture_unit = -1;
    renderer->materials.push_back(m);

    m.color = vec4(0,0.5,1,1);
    m.texture_unit = 1;
    renderer->materials.push_back(m);
    */

    debug->Info("We have %i materials\n",renderer->materials.size());

};

void Scene::HandleInput(){
    if (!inputcontroller){
        return;
    }
    inputcontroller->UpdateKeyState();
};

void Scene::UpdatePhysics(){
    //We need a renderer because that's were we store our objects that need to be rendered.
    if (!renderer){
        return;
    }
    if (inputcontroller && inputcontroller->WasKeyReleased(INPUT_PAUSE)){
        f_paused = !f_paused;
    }
    if (f_paused){
        return;
    }

    if (physics_world){
        physics_world->Update(1.0/50.0);
    }

    //debug->Info("Updating object phyics for tick %llu\n",physics_ticks++);
    /*
        This would copy over all physics_states for a current render frame.
    */
    for (Object* object:renderer->objects){
        //debug->Info("Updating physics for obj->id %i\n",object->GetID());
        if (object == camera){
            object->UpdatePhysicsState();
            continue;
        }

        //Copies object state and invalidates physics state
        object->UpdatePhysicsState();
    }
};

void Scene::DrawFrame(){
    if (camera){
        camera->viewport.width = renderer->width;
        camera->viewport.height = renderer->height;
        camera->CalculateLookatMatrix();
    }

    int2 m = inputcontroller->GetRelativeMousePosition();

    //if (tex_1)
    //    glBindTextureUnit(0, tex_1->texture_id);
    //if (tex_2)
    //glBindTextureUnit(1, tex_2->texture_id);
    renderer->DrawFrame(camera, shader,inputcontroller);
};

void Scene::AddObject(Object* object){
    if (object && renderer){
        renderer->objects.push_back(object);
    }
}

Object* Scene::FindObject(const std::string& name){
    if (!renderer){
        return NULL;
    }

    for (Object* object:renderer->objects){
        if(object->name.compare(name) == 0){
            return object;
        }
    }
    return NULL;
}