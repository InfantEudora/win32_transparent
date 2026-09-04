#include "glad.h"

#include "Scene.h"
#include "OBJLoader.h"
#include "Debug.h"

static Debugger *debug = new Debugger("Scene", DEBUG_ALL);

Scene::Scene(){

};

void Scene::UpdateInput(){
    if (!inputcontroller){
        return;
    }
    inputcontroller->UpdateKeyState();
};

void Scene::UpdateAnimations(){
    if (!renderer){
        return;
    }

    //Update all objects stored in the renderer.
    for (Object* object:renderer->objects){
        object->ApplyAnimation(object->animation_time_delta); //Each object updates its own animation time
    }
};

void Scene::UpdatePhysics(float delta_time){
    //We need a renderer because that's were we store our objects that need to be rendered.
    if (!renderer){
        return;
    }
    if (inputcontroller && inputcontroller->WasKeyReleased(INPUT_PAUSE)){
        PausePhysics(!f_paused);
    }

    if (f_paused){
        //StepPhysics() queues these up from any thread (e.g. an MCP tool handler) - run
        //exactly one queued tick per call here, same as this function would do unpaused,
        //so the caller can single-step the simulation deterministically.
        if (pending_physics_steps <= 0){
            return;
        }
        pending_physics_steps--;
    }

    //Before the physics step, so this tick's simulation reacts to the new pose/velocity.
    AdvanceObjectMotions(delta_time);

    if (physics_world){
        physics_world->Update(delta_time);
    }

    //Update all objects stored in the renderer.
    for (Object* object:renderer->objects){
        //debug->Info("Updating physics for obj->id %i\n",object->GetID());

        //Copies object state and invalidates physics state
        object->UpdatePhysicsState();
    }
};

void Scene::DrawFrame(){
    /*if (camera){
        camera->viewport.width = renderer->width;
        camera->viewport.height = renderer->height;
        camera->CalculateLookatMatrix();
    }*/

    int2 m = inputcontroller->GetRelativeMousePosition();

    //Update mesh for physics debugging
    if (physics_world && physics_world->IsDebugRenderingEnabled()){
        renderer->physics_mutex.lock();
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
            debugobject->SetVisibility(true);
            Mesh* debugmesh = debugobject->GetMesh();
            if (!debugmesh){
                debug->Info("Created new PhysicsDebugObject Mesh\n");
                debugmesh = new Mesh();
                debugobject->SetMesh(debugmesh);
            }
            debugmesh->SetLineMeshData(&line_vertices.at(0),line_vertices.size());
            debugobject->SetPickability(false);
        }
        renderer->physics_mutex.unlock();
    }else{
        Object* debugobject = FindObject("PhysicsDebugObject");
        if (debugobject){
            debugobject->SetVisibility(false);
        }
    }
    renderer->physics_mutex.lock();
    renderer->DrawFrame(camera, shader,inputcontroller);
    renderer->physics_mutex.unlock();
};

void Scene::AddObject(Object* object){
    if (object && renderer){
        renderer->objects.push_back(object);
    }
}

static void ForEachObjectRecursive(Object* object,const std::function<void(Object*)>& fn){
    fn(object);
    for (Object* child:object->children){
        ForEachObjectRecursive(child,fn);
    }
}

void Scene::ForEachObject(const std::function<void(Object*)>& fn){
    if (!renderer){
        return;
    }
    for (Object* object:renderer->objects){
        ForEachObjectRecursive(object,fn);
    }
}

Object* Scene::FindObject(const std::string& name){
    Object* found = NULL;
    ForEachObject([&](Object* object){
        if (!found && object->name.compare(name) == 0){
            found = object;
        }
    });
    return found;
}

Object* Scene::FindObjectByID(objectid_t id){
    Object* found = NULL;
    ForEachObject([&](Object* object){
        if (!found && object->GetID() == id){
            found = object;
        }
    });
    return found;
}

void Scene::MoveObjectOverTicks(Object* object,const vec3* target_position,const quat* target_rotation,int ticks){
    if (!object || (!target_position && !target_rotation)){
        return;
    }
    ObjectMotion motion;
    motion.object = object;
    motion.ticks_total = max(ticks,1);
    if (target_position){
        motion.f_position = true;
        motion.target_position = *target_position;
    }
    if (target_rotation){
        motion.f_rotation = true;
        motion.target_rotation = *target_rotation;
    }
    std::lock_guard<std::mutex> lock(object_motions_mutex);
    for (ObjectMotion& existing:object_motions){
        if (existing.object == object){
            existing = motion; //a new request supersedes whatever was in flight
            return;
        }
    }
    object_motions.push_back(motion);
}

int Scene::GetPendingObjectMotions(){
    std::lock_guard<std::mutex> lock(object_motions_mutex);
    return (int)object_motions.size();
}

//Angular velocity (world space) that rotates a body from `from` to `to` in delta_time.
static vec3 AngularVelocityBetween(const quat& from,const quat& to,float delta_time){
    quat from_inv = from;
    from_inv.inverse();
    quat delta = to * from_inv;
    delta.normalize();
    if (delta.w < 0.0f){ //shortest way round
        delta = -delta;
    }
    vec3 axis(delta.x,delta.y,delta.z);
    float sin_half = axis.length();
    if (sin_half < 1e-6f || delta_time <= 0.0f){
        return vec3(0,0,0);
    }
    float angle = 2.0f * atan2f(sin_half,delta.w);
    return axis * (angle / (sin_half * delta_time));
}

void Scene::AdvanceObjectMotions(float delta_time){
    std::lock_guard<std::mutex> lock(object_motions_mutex);
    for (size_t i = 0; i < object_motions.size();){
        ObjectMotion& motion = object_motions[i];
        Object* object = motion.object;
        Physics* physics = object->GetPhysics();

        if (motion.ticks_done == 0){
            motion.start_position = object->GetPosition();
            motion.start_rotation = object->GetRotation();
            if (physics){
                motion.f_kinematic = true;
                motion.previous_body_type = physics->GetBodyType();
                physics->SetBodyType(rp3d::BodyType::KINEMATIC);
                physics->WakeUp();
            }
        }

        if (motion.ticks_done >= motion.ticks_total){
            //The extra tick after the last velocity has played out: land exactly on the target
            //(integration won't be bit-exact), stop, and hand the body back to whatever it was.
            if (physics){
                physics->SetVelocity(vec3(0,0,0));
                physics->SetAngularVelocity(vec3(0,0,0));
            }
            if (motion.f_position){
                object->SetPosition(motion.target_position);
            }
            if (motion.f_rotation){
                object->SetRotation(motion.target_rotation);
            }
            if (physics && motion.f_kinematic){
                physics->SetBodyType(motion.previous_body_type);
            }
            object_motions.erase(object_motions.begin() + i);
            continue;
        }

        motion.ticks_done++;
        float factor = (float)motion.ticks_done / (float)motion.ticks_total;
        vec3 next_position = motion.f_position ? motion.start_position.lerp(motion.target_position,factor) : object->GetPosition();
        quat next_rotation = motion.f_rotation ? quat::slerp(motion.start_rotation,motion.target_rotation,factor) : object->GetRotation();

        if (physics && delta_time > 0.0f){
            //Velocity that arrives at the next pose by the end of this tick, measured from where
            //the body actually is now (not where the ideal curve says it should be) so any
            //integration error is corrected next tick rather than accumulating.
            vec3 current_position = physics->GetBodyWorldPosition();
            quat current_rotation = physics->GetBodyWorldOrientation();
            physics->SetVelocity(motion.f_position ? (next_position - current_position) * (1.0f / delta_time) : vec3(0,0,0));
            physics->SetAngularVelocity(motion.f_rotation ? AngularVelocityBetween(current_rotation,next_rotation,delta_time) : vec3(0,0,0));
        }else{
            if (motion.f_position){
                object->SetPosition(next_position);
            }
            if (motion.f_rotation){
                object->SetRotation(next_rotation);
            }
        }
        i++;
    }
}