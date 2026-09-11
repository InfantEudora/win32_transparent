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
    inputcontroller->UpdateKeyState(physics_tick);
};

void Scene::UpdateAnimations(float delta_time){
    if (!renderer){
        return;
    }

    //Animation timing is simulation state: since the root-motion rewrite the extracted deltas
    //drive the character's motion, so a clip advancing by a hardcoded 20ms while the sim ticks at
    //a different rate would desync the two. Each object's delta is refreshed from the simulation
    //timestep every tick, unless it was deliberately overridden (the debug UI slider).
    for (Object* object:renderer->objects){
        if (!object->f_animation_time_delta_override){
            object->animation_time_delta = delta_time;
        }
        object->ApplyAnimation(object->animation_time_delta); //Each object updates its own animation time
    }
};

//--- Simulation commands ------------------------------------------------------------------------

uint32_t Scene::SubmitCommand(const SimCommand& cmd){
    QueuedCommand queued;
    queued.cmd = cmd;
    std::lock_guard<std::mutex> lock(commands_mutex);
    //Assigned under the lock, not with a bare atomic increment: the sequence has to match the
    //order commands sit in the queue, and two threads incrementing then pushing could interleave
    //those two steps and produce a queue that is out of order with respect to its own sequences.
    queued.sequence = ++command_sequence;
    pending_commands.push_back(queued);
    return queued.sequence;
}

void Scene::RegisterCommandHandler(uint16_t type, std::function<objectid_t(const SimCommand&)> handler){
    std::lock_guard<std::mutex> lock(commands_mutex);
    command_handlers[type] = handler;
}

int Scene::GetPendingCommands(){
    std::lock_guard<std::mutex> lock(commands_mutex);
    return (int)pending_commands.size();
}

objectid_t Scene::GetCommandResult(uint32_t sequence){
    if (sequence == 0){
        return OBJECTID_INVALID;
    }
    std::lock_guard<std::mutex> lock(commands_mutex);
    const CommandResult& result = command_results[sequence % COMMAND_RESULT_RING];
    //The sequence check is what makes the ring safe: the slot may since have been reused by a
    //newer command, in which case this one's result is simply gone rather than wrong.
    return (result.sequence == sequence) ? result.object : OBJECTID_INVALID;
}

void Scene::DrainCommands(){
    //Swap the whole queue out under the lock and run the handlers unlocked. A handler runs
    //arbitrary app code which may itself submit commands (and will take the same mutex), so
    //holding it across the dispatch would deadlock.
    std::vector<QueuedCommand> commands;
    {
        std::lock_guard<std::mutex> lock(commands_mutex);
        if (pending_commands.empty()){
            return;
        }
        commands.swap(pending_commands);
    }

    for (const QueuedCommand& queued:commands){
        //A version mismatch means the struct this command was built against is not the struct
        //being read - refuse it rather than acting on fields at the wrong offsets. Today this can
        //only fire across a stale recording; it is here from the start so a replay reader has one
        //defined behaviour to rely on.
        if (queued.cmd.version != SIM_COMMAND_VERSION){
            debug->Err("Dropping SimCommand type %u: version %u, expected %u\n",
                       queued.cmd.type,queued.cmd.version,SIM_COMMAND_VERSION);
        }else{
            std::function<objectid_t(const SimCommand&)> handler;
            {
                std::lock_guard<std::mutex> lock(commands_mutex);
                auto it = command_handlers.find(queued.cmd.type);
                if (it != command_handlers.end()){
                    handler = it->second; //copied out, then called unlocked
                }
            }
            objectid_t result = OBJECTID_INVALID;
            if (handler){
                result = handler(queued.cmd);
            }else{
                debug->Err("No handler registered for SimCommand type %u\n",queued.cmd.type);
            }
            std::lock_guard<std::mutex> lock(commands_mutex);
            CommandResult& slot = command_results[queued.sequence % COMMAND_RESULT_RING];
            slot.sequence = queued.sequence;
            slot.object = result;
        }
        //Advanced per command and only after its result is stored, so a submitter that sees its
        //own sequence here can immediately read the result back.
        applied_command_sequence = queued.sequence;
    }
}

void Scene::UpdatePhysics(float delta_time){
    //We need a renderer because that's were we store our objects that need to be rendered.
    if (!renderer){
        return;
    }
    physics_timestep = delta_time;

    //Commands drain BEFORE the pause check below, deliberately. A paused simulation is exactly
    //when the debug UI and a scripted MCP session do most of their creating and teleporting, and
    //if commands only ran on a tick that actually stepped, a paused editor would freeze solid and
    //every caller waiting on GetAppliedCommandSequence() would time out. The cost is that a
    //command applied while paused lands between ticks rather than on one - fine, because
    //GetPhysicsTick() is unchanged by it, so it is unambiguous which tick it precedes.
    DrainCommands();
    if (inputcontroller && inputcontroller->WasKeyReleased(INPUT_PAUSE)){
        PausePhysics(!f_paused);
    }

    //StepPhysics() queues these up from any thread (e.g. an MCP tool handler) - run exactly
    //one queued tick per call here, same as this function would do unpaused, so the caller
    //can single-step the simulation deterministically. The counter is decremented at the END
    //of the tick, not here: a caller polling GetPendingPhysicsSteps() == 0 (tank_step) takes
    //that as "the steps have happened" and immediately touches the simulation (a reset, a
    //teleport) from its own thread - decrementing first let that land in the middle of the
    //last step still running here, corrupting the state it was racing with.
    bool consumed_pending_step = false;
    if (f_paused){
        if (pending_physics_steps <= 0){
            return;
        }
        consumed_pending_step = true;
    }

    //Before the physics step, so this tick's simulation reacts to the new pose/velocity.
    AdvanceObjectMotions(delta_time);

    if (physics_world){
        physics_world->Update(delta_time);
    }

    //Update all objects stored in the renderer.
    for (Object* object:renderer->objects){
        //debug->Info("Updating physics for obj->id %i\n",object->GetID());

        //Hand the object this tick's real timestep, so per-tick logic doesn't have to assume one
        //- see TankCharacter/BuggyCharacter::UpdatePhysicsState, which use it for the wheel
        //readback, the drive-force governor and the turret slew.
        object->physics_timestep = delta_time;

        //Copies object state and invalidates physics state
        object->UpdatePhysicsState();
    }

    //A tick has now actually run, so advance the simulation clock. Deliberately last, for the
    //same reason the pending-step decrement below is last: another thread watching this counter
    //treats a change as "that tick is finished and the state is mine to read".
    physics_tick++;

    if (consumed_pending_step){
        pending_physics_steps--;
    }
};

void Scene::DrawFrame(){

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
    //A new request supersedes whatever was in flight. Every queued leg for this object goes too,
    //not just the first one found - once QueueObjectMotion can put several in here, leaving the
    //rest would have the "replaced" motion resume as soon as the new one finished.
    //
    //An erased leg that had already started leaves the body KINEMATIC. That is deliberate and
    //harmless: the replacement motion runs on the same object, sees ticks_done == 0, and records
    //previous_body_type from the body as it is now - so the type that gets restored at the end is
    //the one the erased motion would have restored, not KINEMATIC.
    for (size_t i = 0; i < object_motions.size();){
        if (object_motions[i].object == object){
            object_motions.erase(object_motions.begin() + i);
        }else{
            i++;
        }
    }
    object_motions.push_back(motion);
}

//See the header. Identical to MoveObjectOverTicks except that it appends rather than replacing;
//AdvanceObjectMotions is what keeps one object's legs in order.
void Scene::QueueObjectMotion(Object* object,const vec3* target_position,const quat* target_rotation,int ticks){
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

    //The last act of a motion: land exactly on the target (a lerp/slerp at factor 1.0 is not
    //bit-exact, and an integrated body is further off than that), stop the body, and hand it back
    //to whatever type it was before the motion took it over. Shared by the two places a motion can
    //retire - see the comment on that split below.
    auto FinishMotion = [](ObjectMotion& motion,Object* object,Physics* physics){
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
    };

    for (size_t i = 0; i < object_motions.size();){
        ObjectMotion& motion = object_motions[i];
        Object* object = motion.object;

        //Only the FIRST leg queued for an object is live; anything behind it waits its turn. The
        //vector is in submission order and a finished leg is erased, so "first" is simply "no
        //earlier entry names this object". Linear, but the list holds a handful of entries at
        //most and this runs once per tick.
        bool f_waiting_behind_another_leg = false;
        for (size_t j = 0; j < i; j++){
            if (object_motions[j].object == object){
                f_waiting_behind_another_leg = true;
                break;
            }
        }
        if (f_waiting_behind_another_leg){
            i++;
            continue;
        }

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

        /*
            A motion retires in one of two places, and WHICH one depends on whether it drives a
            physics body. This is the whole of the fix for the ticks+1 bug (engine backlog item 32,
            docs/tetris_findings.md 4.11), so it is worth being explicit:

            - A KINEMATIC motion needs one call BEYOND its last interpolating tick. That tick set a
              velocity; the solver has not run yet, so the body is not where it should be until it
              has. Retiring here would restore the body type with the last step still unintegrated.

            - A PLAIN object does not. Its last interpolating tick assigned the exact pose directly,
              so there is nothing left to play out, and the extra call only made the motion outlive
              the length it advertised - writing its target back over whatever the app did on that
              tick. That is not theoretical: a line-collapse animation of N ticks, in a game phase
              that also lasted N ticks, had its cubes shoved back down again after the phase had
              restored them, and they stayed a row low for the rest of the run.

            So the kinematic case still finishes here, at the top of the call after the last one;
            the plain case finishes at the BOTTOM of the call that did its last interpolation.
        */
        if (motion.f_kinematic && (motion.ticks_done >= motion.ticks_total)){
            FinishMotion(motion,object,physics);
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
            //Non-physics: that was the last interpolation step, so the motion is done NOW. See the
            //block above. FinishMotion only snaps to the exact target here - factor just reached
            //1.0, so the pose is already right to within a float - and there is no body to restore.
            //
            //The !f_kinematic test makes this and the top-of-loop retire mutually exclusive rather
            //than merely unlikely to overlap: a motion that DOES drive a body reaches this branch
            //whenever delta_time is not positive, and it must still take its extra call.
            if (!motion.f_kinematic && (motion.ticks_done >= motion.ticks_total)){
                FinishMotion(motion,object,physics);
                object_motions.erase(object_motions.begin() + i);
                continue;
            }
        }
        i++;
    }
}