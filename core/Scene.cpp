#include "glad.h"

#include "Scene.h"
#include "OBJLoader.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Scene", DEBUG_ALL);

Scene::Scene(){

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

    //Update mesh for physics debugging
    if (physics_world->IsDebugRenderingEnabled()){
        renderer->state_mutex.lock();
        reactphysics3d::DebugRenderer* dbr = physics_world->debug_renderer;
        uint32_t num_tris = dbr->getNbTriangles();
        //debug->Info("Number of debug lines: %lu\n",dbr->getNbLines());
        //debug->Info("Number of debug triangles: %lu\n",dbr->getNbTriangles());
        if (num_tris > 0){
            const reactphysics3d::DebugRenderer::DebugTriangle* trilist = dbr->getTrianglesArray();
            std::vector<line_vertex>line_vertices;
            for (int i=0;i<num_tris;i++){
                const reactphysics3d::DebugRenderer::DebugTriangle* tri = &trilist[i];
                line_vertex v;
                //1-2 2-3 3-1
                v.pos = (vec3&)tri->point1;
                v.color = tri->color1;
                line_vertices.push_back(v);
                v.pos = (vec3&)tri->point2;
                v.color = tri->color2;
                line_vertices.push_back(v);

                v.pos = (vec3&)tri->point2;
                v.color = tri->color2;
                line_vertices.push_back(v);
                v.pos = (vec3&)tri->point3;
                v.color = tri->color3;
                line_vertices.push_back(v);

                v.pos = (vec3&)tri->point3;
                v.color = tri->color3;
                line_vertices.push_back(v);
                v.pos = (vec3&)tri->point1;
                v.color = tri->color1;
                line_vertices.push_back(v);
            }

            Object* debugobject = FindObject("PhysicsDebugObject");
            if (!debugobject){
                debugobject = new Object();
                debugobject->name = "PhysicsDebugObject";
                AddObject(debugobject);
                debugobject->UpdatePhysicsState();
                debug->Info("Created new PhysicsDebugObject\n");
            }
            Mesh* debugmesh = debugobject->GetMesh();
            if (!debugmesh){
                debug->Info("Created new PhysicsDebugObject Mesh\n");
                debugmesh = new Mesh();
                debugobject->SetMesh(debugmesh);
            }
            debugmesh->SetLineMeshData(&line_vertices.at(0),line_vertices.size());
            debugobject->SetPickability(false);
        }
        renderer->state_mutex.unlock();
    }

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