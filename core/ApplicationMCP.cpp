/*
    The core MCP tools, and the screenshot attachment they share - the half of Application that
    exists to let an agent drive the app.

    THIS FILE IS ONE OF A SWAPPABLE PAIR. core/ApplicationMCP_none.cpp defines the same three
    symbols as no-ops, and engine.mk compiles exactly one of them: this one when USE_MCP=1,
    the other when it is 0. Nothing else in core changes either way.

    THAT IS WHY IT IS A SEPARATE FILE RATHER THAN AN #ifdef IN Application.cpp. The core objects
    under build/core are compiled ONCE and shared by every app, and make compares them by
    timestamp and not by the flags they were built with - so a flag that CHANGES a core
    translation unit would let whichever app built first decide what every other app links,
    silently. A flag that swaps one whole unit for another cannot do that. engine.mk's
    CORE_CFLAGS block is the long version of this, and USE_SOUND is the other example: it adds
    two core objects rather than altering any.

    It also takes 709 lines of tool registration out of Application.cpp, which was 3327 lines
    and is now closer to being about running an application.
*/
#include "Application.h"
#include "MCPServer.h"
#include "Scene.h"
#include "Renderer.h"
#include "Camera.h"
#include "AssetManager.h"
#include "Object.h"
#include "Debug.h"

static Debugger* debug = new Debugger("ApplicationMCP", DEBUG_ALL);

/*
    Starts both MCP transports, called from FrameThreadFunction once the app's own Init() has
    returned and registered its tools.

    This wrapper exists so Application.cpp does not have to name MCPServer at all: with USE_MCP=0
    that class is not compiled, so a direct MCPServer::Get()->Start() there would be an undefined
    reference rather than a disabled feature. The no-op twin makes the call site unconditional.

    Both transports start for every app - stdio for clients that spawn and own the process, HTTP
    for one attaching to an already-running instance. If the HTTP port is taken (another instance
    of this app is already up) that transport logs and stays off; stdio still works.
*/
static json Vec3ToJson(const vec3& v){
    return json::array({v.x,v.y,v.z});
}

static json QuatToJson(const quat& q){
    return json::array({q.x,q.y,q.z,q.w});
}

/*
    The JSON <-> engine conversions the tools below are built from, and the four
    Application members that exist only to serve them. All nine moved out of
    Application.cpp with the tools on 2026-09-14: none has a caller anywhere else in
    the tree, which is what made the split clean rather than a judgement call.
*/

static bool JsonToVec3(const json& arr,vec3& out){
    if (!arr.is_array() || arr.size() != 3 || !arr[0].is_number() || !arr[1].is_number() || !arr[2].is_number()){
        return false;
    }
    out = vec3(arr[0].get<float>(),arr[1].get<float>(),arr[2].get<float>());
    return true;
}

//"id" (number) or "name" (string) - id wins if both are given. Fills error and returns NULL if
//neither resolves.
static Object* ResolveObjectArg(Scene* scene,const json& args,std::string& error){
    if (!scene){
        error = "no scene";
        return NULL;
    }
    if (args.contains("id") && args.at("id").is_number()){
        objectid_t id = (objectid_t)args.at("id").get<uint32_t>();
        Object* object = scene->FindObjectByID(id);
        if (!object){
            error = "no object with id " + std::to_string(id);
        }
        return object;
    }
    if (args.contains("name") && args.at("name").is_string()){
        std::string name = args.at("name").get<std::string>();
        Object* object = scene->FindObject(name);
        if (!object){
            error = "no object named '" + name + "' (names aren't unique - prefer id from object_list)";
        }
        return object;
    }
    error = "supply either id (number) or name (string)";
    return NULL;
}

//Accepts any one of: "rotation" [x,y,z,w] quaternion, "axis_degrees" [x,y,z] (applied in X, Y, Z
//order, exactly like the Generic Object UI's "Axis Degrees" mode), or "yaw_degrees" (rotation
//about world up only). Returns false with error left empty if none is present, false with error
//set if one is present but malformed.
static bool RotationFromArgs(const json& args,quat& out,std::string& error){
    if (args.contains("rotation")){
        const json& arr = args.at("rotation");
        if (!arr.is_array() || arr.size() != 4){
            error = "rotation must be a [x,y,z,w] quaternion";
            return false;
        }
        out.x = arr[0].get<float>(); out.y = arr[1].get<float>(); out.z = arr[2].get<float>(); out.w = arr[3].get<float>();
        out.normalize();
        return true;
    }
    if (args.contains("axis_degrees")){
        vec3 deg;
        if (!JsonToVec3(args.at("axis_degrees"),deg)){
            error = "axis_degrees must be [x,y,z] in degrees";
            return false;
        }
        quat qx(vec3(1,0,0),toradians(deg.x));
        quat qy(vec3(0,1,0),toradians(deg.y));
        quat qz(vec3(0,0,1),toradians(deg.z));
        out = qx * qy * qz;
        return true;
    }
    if (args.contains("yaw_degrees") && args.at("yaw_degrees").is_number()){
        out = quat(vec3(0,1,0),toradians(args.at("yaw_degrees").get<float>()));
        return true;
    }
    return false;
}

//Shared by camera_get and camera_set so the two cannot drift apart. camera_target is the app's
//orbit pivot (see Application::GetCameraTargetPtr) and may be NULL for an app that has none.
static json CameraToJson(Camera* camera,vec3* camera_target){
    //GetPosition, not GetWorldPosition: this is exactly what the middle-mouse orbit reads and
    //writes, so what these tools report is what that code sees. The camera is a root object, so
    //local and world are the same thing anyway. (This used to matter for a second reason -
    //GetWorldPosition read a once-per-frame copy that was stale on this MCP thread straight
    //after a camera_set. There is one ObjectState now, so both are equally fresh.)
    vec3 pos = camera->GetPosition();
    vec3 forward = camera->GetForward();
    json result = {
        {"position", Vec3ToJson(pos)},
        //Where it points, one unit out - the argument camera_set's look_at takes back.
        {"look_at", Vec3ToJson(pos + forward)},
        {"forward", Vec3ToJson(forward)},
        {"up", Vec3ToJson(camera->GetUp())},
        {"left", Vec3ToJson(camera->GetLeft())},
        {"rotation", QuatToJson(camera->GetRotation())},
        {"fov", camera->viewport.fov},
        {"znear", camera->viewport.znear},
        {"zfar", camera->viewport.zfar},
        {"aspect", camera->viewport.aspect},
    };
    if (camera_target){
        result["camera_target"] = Vec3ToJson(*camera_target);
        result["target_distance"] = (pos - *camera_target).length();
    }
    return result;
}

static json ObjectToJson(Object* object,bool verbose){
    json result = {
        {"id", object->GetID()},
        {"name", object->name},
        {"parent_id", object->GetParent() ? json(object->GetParent()->GetID()) : json(nullptr)},
        {"position", Vec3ToJson(object->GetPosition())},
        //Reported in both builds, and always false without physics - an agent asking "does this
        //object have a body" gets a true answer either way rather than a missing field.
        {"has_physics", object->HasPhysics()},
    };
#ifdef USE_PHYSICS
    if (Physics* physics = object->GetPhysics()){
        result["static"] = physics->IsStatic();
    }
#endif
    if (!verbose){
        return result;
    }
    result["world_position"] = Vec3ToJson(object->GetWorldPosition());
    result["rotation"] = QuatToJson(object->GetRotation());
    //`forward`/`up` come from the object's OWN rotation, so for a child they are relative to the
    //parent - next to a `world_position` that is absolute, which is a genuinely confusing pair.
    //The world versions are reported alongside rather than replacing them, because "the axis in
    //my parent's frame" is what a caller manipulating a child wants. For a root object the two
    //are identical.
    result["forward"] = Vec3ToJson(object->GetForward());
    result["world_forward"] = Vec3ToJson(object->GetWorldForward());
    result["up"] = Vec3ToJson(object->GetUp());
    result["world_up"] = Vec3ToJson(object->GetWorldUp());
    result["scale"] = Vec3ToJson(object->GetScale());
    result["visible"] = object->IsVisible();
    json children = json::array();
    for (Object* child:object->children){
        children.push_back({{"id",child->GetID()},{"name",child->name}});
    }
    result["children"] = children;
#ifdef USE_PHYSICS
    if (Physics* physics = object->GetPhysics()){
        result["physics"] = {
            {"static", physics->IsStatic()},
            {"gravity", physics->IsGravityEnabled()},
            {"sleeping", physics->IsSleeping()},
            {"mass_kg", physics->GetMass()},
            {"velocity", Vec3ToJson(physics->GetVelocity())},
            {"angular_velocity", Vec3ToJson(physics->GetAngularVelocity())},
        };
    }
#endif
    return result;
}

bool Application::ResolveObjectIdArg(const json& args, objectid_t& id_out, std::string& error){
    if (!main_scene){
        error = "no scene";
        return false;
    }
    bool f_found = false;
    main_scene->AtTickBoundary([&]{
        Object* object = ResolveObjectArg(main_scene,args,error);
        if (object){
            id_out = object->GetID();
            f_found = true;
        }
    });
    if (!f_found && error.empty()){
        error = "no scene";
    }
    return f_found;
}

json Application::ObjectJsonAtTickBoundary(objectid_t id, bool full){
    json result;
    bool f_found = false;
    if (main_scene){
        main_scene->AtTickBoundary([&]{
            Object* object = main_scene->FindObjectByID(id);
            if (object){
                result = ObjectToJson(object,full);
                f_found = true;
            }
        });
    }
    if (!f_found){
        return json{ {"error","object " + std::to_string(id) + " no longer exists"} };
    }
    return result;
}

json Application::SimClockJson(){
    if (!main_scene){
        return json{ {"error","no scene"} };
    }
    //input_focused is whether the app believes keystrokes are its own. Reported because it was
    //once wrong in a way nothing showed: a window started --minimized thought it had focus, and
    //acted on keys and wheel notches typed into other programs. If scripted input or a stray key
    //does something surprising, this is the first number to read.
    InputController* input = main_scene->inputcontroller;
    return json{
        {"tick", main_scene->GetPhysicsTick()},
        {"paused", main_scene->IsPhysicsPaused()},
        {"pending_steps", main_scene->GetPendingPhysicsSteps()},
        {"timestep", main_scene->GetPhysicsTimestep()},
        {"input_focused", input ? input->HasFocus() : false}
    };
}

json Application::ReloadShadersAndWait(const std::string& name_filter, int timeout_ms){
    {
        std::lock_guard<std::mutex> lock(shader_reload_mutex);
        if (f_shader_reload_pending){
            return json{ {"error","another shader reload is already waiting to be serviced"} };
        }
        shader_reload_filter = name_filter;
        shader_reload_result = json::object();
        f_shader_reload_pending = true;
    }

    //Polled rather than signalled, the same way StepPhysicsAndWait waits on the physics thread.
    //A frame at any sane rate is well inside this; a timeout means the render thread is not
    //running, and the request is then dropped rather than left to fire at some later moment the
    //caller has stopped expecting.
    for (int waited_ms = 0; waited_ms < timeout_ms; waited_ms += 5){
        {
            std::lock_guard<std::mutex> lock(shader_reload_mutex);
            if (!f_shader_reload_pending){
                return shader_reload_result;
            }
        }
        Sleep(5);
    }

    std::lock_guard<std::mutex> lock(shader_reload_mutex);
    f_shader_reload_pending = false;
    shader_reload_filter.clear();
    return json{ {"error","the render thread did not service the reload in time"} };
}

void Application::StartMCPServer(){
    MCPServer::Get()->Start();
    MCPServer::Get()->StartHttp(8765);
}

json Application::MaybeAttachScreenshot(json result, bool include_screenshot, bool include_ui){
    if (!include_screenshot){
        return result;
    }
    if (!renderer){
        result["screenshot_error"] = "no renderer";
        return result;
    }
    std::vector<uint8_t> png = renderer->RequestScreenshot(include_ui);
    if (png.empty()){
        result["screenshot_error"] = "timed out waiting for the render thread to capture a frame";
        return result;
    }
    return MCPServer::AttachImagePNG(result,png);
}

void Application::RegisterCoreMCPTools(){
    const json object_selector_properties = {
        {"id", {{"type","number"},{"description","object id, as reported by object_list (preferred - unique)"}}},
        {"name", {{"type","string"},{"description","object name, first match wins - names are not unique"}}}
    };

    MCPServer::Get()->RegisterTool("object_list",
        "List the objects in the active scene (children included, depth-first) with id, name, "
        "parent id, position and whether they carry a physics body. Use name_filter (case-sensitive "
        "substring) to narrow it down; the list is capped at `limit` entries (default 200).",
        json{
            {"type","object"},
            {"properties", {
                {"name_filter", {{"type","string"},{"description","only objects whose name contains this substring"}}},
                {"limit", {{"type","number"},{"description","max entries returned, default 200"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            std::string filter = args.value("name_filter","");
            int limit = max((int)args.value("limit",200.0f),1);
            json objects = json::array();
            int total = 0;
            //The whole walk inside one tick boundary, not one object at a time: the physics thread
            //adds and destroys objects, so a walk that released the lock between entries could
            //follow a pointer that had already been reaped, and would report a list that never
            //existed at any single moment.
            main_scene->AtTickBoundary([&]{
                main_scene->ForEachObject([&](Object* object){
                    if (!filter.empty() && object->name.find(filter) == std::string::npos){
                        return;
                    }
                    total++;
                    if ((int)objects.size() < limit){
                        objects.push_back(ObjectToJson(object,false));
                    }
                });
            });
            return json{ {"objects",objects}, {"matched",total}, {"returned",(int)objects.size()} };
        });

    MCPServer::Get()->RegisterTool("object_get",
        "Full state of one object: local and world position, rotation (quaternion [x,y,z,w]), "
        "forward/up vectors, scale, children, and its physics body's static/gravity/sleeping flags, "
        "mass and velocities if it has one.",
        json{ {"type","object"}, {"properties", object_selector_properties} },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            //Resolved and serialised in the SAME tick boundary, so the object cannot move or be
            //destroyed between being found and being read.
            std::string error;
            json result;
            bool f_found = false;
            main_scene->AtTickBoundary([&]{
                Object* object = ResolveObjectArg(main_scene,args,error);
                if (object){
                    result = ObjectToJson(object,true);
                    f_found = true;
                }
            });
            if (!f_found){
                return json{ {"error",error.empty() ? "no scene" : error} };
            }
            return result;
        });

    MCPServer::Get()->RegisterTool("object_set_transform",
        "Instantly set an object's position and/or rotation and/or scale - the same as typing a "
        "value into the Generic Object UI. Only the fields given are changed. Rotation can be given "
        "as `rotation` [x,y,z,w], `axis_degrees` [x,y,z] (applied X then Y then Z, like the UI's "
        "'Axis Degrees' mode) or `yaw_degrees`. A physics body is teleported along with it: use "
        "object_move instead if it should push things out of the way on its way there. Applied on "
        "the physics thread at the start of a tick (a SimCommand), so it cannot land in the middle "
        "of a physics step - it takes effect within a tick, or on the next step if paused.",
        json{
            {"type","object"},
            {"properties", {
                {"id", object_selector_properties.at("id")},
                {"name", object_selector_properties.at("name")},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] local position (world, for a root object)"}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"},{"description","rotation about world up, degrees"}}},
                {"scale", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] scale - box/sphere/capsule colliders and their offsets follow; mass is kept, inertia recomputed"}}}
            }}
        },
        [this](const json &args) -> json {
            std::string error;
            //Resolved here only to turn a name into an id and to report a bad selector straight
            //back to the caller. The command itself carries the ID, never a pointer - see
            //SimCommand::target - which is also why only the id survives the lock.
            objectid_t target_id = OBJECTID_INVALID;
            if (!ResolveObjectIdArg(args,target_id,error)){
                return json{ {"error",error} };
            }
            SimCommand cmd;
            cmd.type = SIM_CMD_OBJECT_SET_TRANSFORM;
            cmd.target = target_id;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),cmd.position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_POSITION;
            }
            if (RotationFromArgs(args,cmd.rotation,error)){
                cmd.flags |= SIM_CMD_FLAG_ROTATION;
            }else if (!error.empty()){
                return json{ {"error",error} };
            }
            if (args.contains("scale")){
                if (!JsonToVec3(args.at("scale"),cmd.scale)){
                    return json{ {"error","scale must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_SCALE;
            }
            if (cmd.flags == 0){
                return json{ {"error","give a position, rotation and/or scale"} };
            }
            //This handler runs on an MCP thread, which holds no locks, so it can wait for the
            //physics thread to apply the command and then report the real resulting state.
            if (SubmitCommandAndWait(cmd) == OBJECTID_INVALID){
                return json{ {"error","the transform command was not applied - see the log"} };
            }
            //Read back AFTER the wait and at its own tick boundary. Waiting inside one would
            //deadlock: the physics thread applies the command, and it cannot run while this
            //handler holds the lock it needs.
            return ObjectJsonAtTickBoundary(target_id,true);
        });

    MCPServer::Get()->RegisterTool("object_move",
        "Move and/or rotate an object to a target over `ticks` physics ticks (default 50, i.e. one "
        "second at 50 tps), interpolating its transform one step per tick - what dragging the "
        "Generic Object UI's position slider does frame by frame. The body's transform is set "
        "directly each tick, so a collider on it pushes dynamic bodies out of the way instead of "
        "teleporting through them. Rotation is given like object_set_transform. Blocks until the "
        "motion has finished (or times out) and returns the object's resulting state; if physics "
        "is paused it returns immediately and the motion plays out as ticks are stepped.",
        json{
            {"type","object"},
            {"properties", {
                {"id", object_selector_properties.at("id")},
                {"name", object_selector_properties.at("name")},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] target local position"}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] target quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] target rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"},{"description","target rotation about world up, degrees"}}},
                {"ticks", {{"type","number"},{"description","physics ticks to spread the motion over, default 50, min 1"}}}
            }}
        },
        [this](const json &args) -> json {
            std::string error;
            objectid_t target_id = OBJECTID_INVALID;
            if (!ResolveObjectIdArg(args,target_id,error)){
                return json{ {"error",error} };
            }
            vec3 target_position;
            bool f_position = false;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),target_position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                f_position = true;
            }
            quat target_rotation;
            bool f_rotation = RotationFromArgs(args,target_rotation,error);
            if (!f_rotation && !error.empty()){
                return json{ {"error",error} };
            }
            if (!f_position && !f_rotation){
                return json{ {"error","give a target position and/or rotation"} };
            }
            int ticks = max((int)args.value("ticks",50.0f),1);
            //A direct WRITE from this thread, and the reason it needs a tick boundary rather than
            //just a careful read: MoveObjectOverTicks edits the object_motions vector that
            //AdvanceObjectMotions is iterating on the physics thread. It has no SimCommand form,
            //so the lock is the whole mechanism. Re-resolved inside, because the id was looked up
            //under a different lock and the object could have been destroyed since.
            bool f_moved = false;
            main_scene->AtTickBoundary([&]{
                Object* object = main_scene->FindObjectByID(target_id);
                if (object){
                    main_scene->MoveObjectOverTicks(object,f_position ? &target_position : NULL,f_rotation ? &target_rotation : NULL,ticks);
                    f_moved = true;
                }
            });
            if (!f_moved){
                return json{ {"error","object " + std::to_string(target_id) + " no longer exists"} };
            }

            json result;
            if (main_scene->IsPhysicsPaused()){
                result["note"] = "physics is paused - the motion runs as ticks are stepped";
            }else{
                //Physics ticks run on their own thread - poll for the queue to drain rather than
                //guessing a sleep. Generous timeout: the tick rate may be lower than nominal.
                int timeout_ms = (int)(ticks * (1000.0f / physics_tps) * 3.0f) + 2000;
                int waited_ms = 0;
                for (; waited_ms < timeout_ms && main_scene->GetPendingObjectMotions() > 0; waited_ms += 5){
                    Sleep(5);
                }
                if (main_scene->GetPendingObjectMotions() > 0){
                    result["note"] = "timed out waiting for the motion to finish - it is still in progress";
                }
                result["waited_ms"] = waited_ms;
            }
            //Outside the wait above, which polls the physics thread and so must not hold the lock.
            result["object"] = ObjectJsonAtTickBoundary(target_id,true);
            return result;
        });

    /*
        Pause and single-step, for every app rather than for the two that happened to need it.

        These are the read-side counterpart to sim_command: a tool handler holds no lock, so
        anything it reads from a free-running simulation is a race it is going to lose eventually.
        Pausing first turns "read the scene and hope" into "read the scene", and stepping turns
        "sleep 200ms and guess how far it got" into an exact number of ticks. Every duration in this
        engine is denominated in ticks (see Scene::GetPhysicsTick), so stepping is the unit the rest
        of the simulation is already written in.

        ApplicationTank and ApplicationTetris keep their own tank_pause/tank_step and
        tetris_pause/tetris_step: those return app telemetry with the step, which is genuinely more
        useful there than a bare clock. They now share the waiting logic below rather than each
        carrying a copy of it.
    */
    MCPServer::Get()->RegisterTool("sim_pause",
        "Pause or resume the simulation. While paused the render loop keeps running - the window "
        "stays responsive, the camera still works and screenshots still work - but no physics tick "
        "runs, no animation advances and no gameplay logic runs until sim_step advances it or this "
        "is called again with paused=false. Pause before reading scene state you care about: an MCP "
        "handler holds no lock, so reading a free-running simulation races the physics thread.",
        json{
            {"type","object"},
            {"properties", {
                {"paused", {{"type","boolean"},{"description","true to pause, false to resume free-running physics"}}}
            }},
            {"required", json::array({"paused"})}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            main_scene->PausePhysics(args.value("paused",true));
            return SimClockJson();
        });

    MCPServer::Get()->RegisterTool("sim_step",
        "Advance a paused simulation by exactly num_ticks ticks and return the resulting clock. "
        "Each tick is the same fixed timestep a free-running one uses, and a tick here is a whole "
        "tick - input, animation, the app's own per-tick logic and the physics step - so stepping "
        "advances the entire simulation, not just the physics. Requires sim_pause first. Blocks "
        "until the physics thread has consumed the steps, and reports ticks_advanced, which is the "
        "value to trust: if it is short of num_ticks the call timed out and the rest is still queued.",
        json{
            {"type","object"},
            {"properties", {
                {"num_ticks", {{"type","number"},{"description","how many ticks to advance, default 1"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the resulting frame, default false"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            if (!main_scene->IsPhysicsPaused()){
                return json{ {"error","simulation is not paused - call sim_pause with paused=true first"} };
            }
            int num_ticks = max((int)args.value("num_ticks",1.0f),0);
            uint64_t advanced = StepPhysicsAndWait(num_ticks);
            json result = SimClockJson();
            result["ticks_advanced"] = advanced;
            result["requested_ticks"] = num_ticks;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });

    /*
        Which scene is live, and switching it - so a change can be tested in the scene it is for.

        scene_set goes through RequestActiveScene like every other switch, so the swap still lands
        at the top of a physics pass and nowhere else; this only waits for it. That happens on every
        pass whether or not the simulation is paused, so it completes against a paused scene too.

        THE PAUSE STATE IS CARRIED ACROSS, because pause is per Scene. Without this, pausing one
        scene and switching to another drops you into a free-running one, and every measurement
        taken straight after the switch is a race - the exact thing sim_pause exists to prevent.
        Set on the target before the request, so it is already true on the first pass the target
        is live. Pass keep_paused=false to switch into whatever state the target was left in.
    */
    MCPServer::Get()->RegisterTool("scene_list",
        "List the scenes this app has, by name, with which one is active and whether each is "
        "paused. The name is what scene_set takes.",
        json{ {"type","object"}, {"properties",json::object()} },
        [this](const json &args) -> json {
            (void)args;
            json list = json::array();
            Scene* active = main_scene;
            for (Scene* scene:scenes){
                if (!scene){
                    continue;
                }
                list.push_back(json{
                    {"name",scene->name},
                    {"active",scene == active},
                    {"paused",scene->IsPhysicsPaused()},
                    {"tick",scene->GetPhysicsTick()},
                    {"objects",scene->objects.size()}
                });
            }
            return json{ {"scenes",list}, {"active",active ? active->name : std::string()} };
        });

    MCPServer::Get()->RegisterTool("scene_set",
        "Make the named scene the active one - the one simulated, drawn and addressed by every "
        "other tool - and wait for the switch to land. A paused scene switches into a paused one "
        "unless keep_paused is false. Returns the new active scene's clock.",
        json{
            {"type","object"},
            {"properties", {
                {"name", {{"type","string"},{"description","scene name, as scene_list reports it"}}},
                {"keep_paused", {{"type","boolean"},{"description","carry the current pause state into the target scene, default true"}}},
                {"include_screenshot", {{"type","boolean"},{"description","also return a PNG of the first frame of the new scene, default false"}}}
            }},
            {"required", json::array({"name"})}
        },
        [this](const json &args) -> json {
            std::string name = args.value("name",std::string());
            Scene* target = FindScene(name);
            if (!target){
                json names = json::array();
                for (Scene* scene:scenes){
                    if (scene){ names.push_back(scene->name); }
                }
                return json{ {"error","no scene called '" + name + "'"}, {"scenes",names} };
            }
            if (main_scene && args.value("keep_paused",true)){
                target->PausePhysics(main_scene->IsPhysicsPaused());
            }
            RequestActiveScene(target);
            //A pass is one tick at most, so a second is dozens of chances; the loop exits on the
            //first pass that applies it.
            for (int waited_ms = 0; waited_ms < 1000 && main_scene != target; waited_ms += 5){
                Sleep(5);
            }
            if (main_scene != target){
                return json{ {"error","the switch was requested but had not landed after 1s"} };
            }
            json result = SimClockJson();
            result["scene"] = target->name;
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });

    //The raw queue, exposed. Every OTHER command-shaped tool here (object_set_transform,
    //object_spawn, tank_reset) is a friendly wrapper that builds one specific SimCommand; this is
    //the generic one, and it exists for two reasons. It is how the command handlers get TESTED -
    //most of them are otherwise only reachable by clicking a button in the Inspector, so they
    //would ship unexercised - and it is what a future record/replay harness needs, since a
    //recording is a stream of exactly these.
    MCPServer::Get()->RegisterTool("sim_command",
        "Submit a raw SimCommand to the simulation's command queue and wait for the physics "
        "thread to apply it. `type` is a command name (object_set_transform, object_set_physics, "
        "object_spawn_asset, object_spawn_primitive, object_duplicate, object_destroy, "
        "world_set_gravity, object_spawn_collider_gizmo) or a raw number for an app's own type. "
        "Any other field given is carried in the command and its presence flag set; fields left "
        "out are not touched by the command. Returns the applied sequence number and the id of "
        "the object the handler created or acted on.",
        json{
            {"type","object"},
            {"properties", {
                {"type", {{"description","command name, or a raw type number for an app-specific command"}}},
                {"subtype", {{"type","number"},{"description","variant within the type: primitive kind for object_spawn_primitive (0 empty, 1 camera, 2 directional light, 3 point light), collider index for object_spawn_collider_gizmo"}}},
                {"target", {{"type","number"},{"description","object id the command is about"}}},
                {"asset", {{"type","string"},{"description","asset name - hashed to its id here, see asset_list"}}},
                {"asset_id", {{"type","number"},{"description","asset id, wins over `asset`"}}},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"}}},
                {"scale", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}}},
                {"velocity", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","linear velocity, and the gravity vector for world_set_gravity"}}},
                {"angular_velocity", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3}}},
                {"static", {{"type","boolean"}}},
                {"gravity", {{"type","boolean"},{"description","whether the body reacts to gravity"}}},
                {"active", {{"type","boolean"}}},
                {"wake_up", {{"type","boolean"},{"description","true to wake the body - a trigger, not a state"}}},
                {"category_bits", {{"type","number"},{"description","collision category mask"}}},
                {"collide_with_bits", {{"type","number"},{"description","collide-with mask"}}},
                {"friction", {{"type","number"}}},
                {"bounciness", {{"type","number"}}}
            }},
            {"required", json::array({"type"})}
        },
        [this](const json &args) -> json {
            if (!main_scene){
                return json{ {"error","no scene"} };
            }
            SimCommand cmd;

            //--- type ---
            if (!args.contains("type")){
                return json{ {"error","give a command type"} };
            }
            const json& type_arg = args.at("type");
            if (type_arg.is_number()){
                cmd.type = (uint16_t)type_arg.get<uint32_t>();
            }else if (type_arg.is_string()){
                //Names rather than numbers at the boundary, for the same reason durations are
                //milliseconds here and ticks inside: a caller should not have to know the enum.
                const std::string name = type_arg.get<std::string>();
                if (name == "object_set_transform")            cmd.type = SIM_CMD_OBJECT_SET_TRANSFORM;
                else if (name == "object_set_physics")          cmd.type = SIM_CMD_OBJECT_SET_PHYSICS;
                else if (name == "object_spawn_asset")          cmd.type = SIM_CMD_OBJECT_SPAWN_ASSET;
                else if (name == "object_spawn_primitive")      cmd.type = SIM_CMD_OBJECT_SPAWN_PRIMITIVE;
                else if (name == "object_duplicate")            cmd.type = SIM_CMD_OBJECT_DUPLICATE;
                else if (name == "object_destroy")              cmd.type = SIM_CMD_OBJECT_DESTROY;
                else if (name == "world_set_gravity")           cmd.type = SIM_CMD_WORLD_SET_GRAVITY;
                else if (name == "object_spawn_collider_gizmo") cmd.type = SIM_CMD_OBJECT_SPAWN_COLLIDER_GIZMO;
                else return json{ {"error","unknown command type '" + name + "'"} };
            }else{
                return json{ {"error","type must be a command name or a number"} };
            }

            //--- payload. Presence in the JSON is what sets the flag, which is exactly the
            //contract the struct itself has: a field nobody mentioned is not written. ---
            std::string error;
            vec3 v;
            if (args.contains("subtype")){
                cmd.subtype = (uint32_t)args.at("subtype").get<uint32_t>();
            }
            if (args.contains("target")){
                cmd.target = (objectid_t)args.at("target").get<uint32_t>();
            }
            if (args.contains("asset_id")){
                cmd.asset = (assetid_t)args.at("asset_id").get<uint32_t>();
            }else if (args.contains("asset")){
                cmd.asset = AssetIDFromName(args.at("asset").get<std::string>().c_str());
            }
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),cmd.position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_POSITION;
            }
            if (RotationFromArgs(args,cmd.rotation,error)){
                cmd.flags |= SIM_CMD_FLAG_ROTATION;
            }else if (!error.empty()){
                return json{ {"error",error} };
            }
            if (args.contains("scale")){
                if (!JsonToVec3(args.at("scale"),cmd.scale)){
                    return json{ {"error","scale must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_SCALE;
            }
            if (args.contains("velocity")){
                if (!JsonToVec3(args.at("velocity"),cmd.velocity)){
                    return json{ {"error","velocity must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_VELOCITY;
            }
            if (args.contains("angular_velocity")){
                if (!JsonToVec3(args.at("angular_velocity"),cmd.angular_velocity)){
                    return json{ {"error","angular_velocity must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_ANGULAR_VELOCITY;
            }
            //The booleans, whose value lives at the same bit position as their own flag.
            struct BoolArg{ const char* name; uint32_t flag; };
            const BoolArg bool_args[] = {
                {"static",  SIM_CMD_FLAG_STATIC},
                {"gravity", SIM_CMD_FLAG_GRAVITY},
                {"active",  SIM_CMD_FLAG_ACTIVE},
            };
            for (const BoolArg& b:bool_args){
                if (args.contains(b.name)){
                    cmd.flags |= b.flag;
                    if (args.at(b.name).get<bool>()){
                        cmd.bool_values |= b.flag;
                    }
                }
            }
            if (args.value("wake_up",false)){
                cmd.flags |= SIM_CMD_FLAG_WAKE_UP; //a trigger, so nothing in bool_values
            }
            if (args.contains("category_bits")){
                cmd.collision_category_bits = (uint32_t)args.at("category_bits").get<uint32_t>();
                cmd.flags |= SIM_CMD_FLAG_CATEGORY_BITS;
            }
            if (args.contains("collide_with_bits")){
                cmd.collide_with_bits = (uint32_t)args.at("collide_with_bits").get<uint32_t>();
                cmd.flags |= SIM_CMD_FLAG_COLLIDE_BITS;
            }
            if (args.contains("friction")){
                cmd.value[0] = args.at("friction").get<float>();
                cmd.flags |= SIM_CMD_FLAG_FRICTION;
            }
            if (args.contains("bounciness")){
                cmd.value[1] = args.at("bounciness").get<float>();
                cmd.flags |= SIM_CMD_FLAG_BOUNCINESS;
            }

            //Submitted and waited on, so the reply can describe what actually happened. Safe
            //here: an MCP handler holds no locks - see SubmitCommandAndWait.
            uint32_t sequence = main_scene->SubmitCommand(cmd);
            objectid_t result = OBJECTID_INVALID;
            bool applied = false;
            for (int waited_ms = 0; waited_ms < 2000; waited_ms += 5){
                if (main_scene->GetAppliedCommandSequence() >= sequence){
                    result = main_scene->GetCommandResult(sequence);
                    applied = true;
                    break;
                }
                Sleep(5);
            }
            json out;
            out["sequence"] = sequence;
            out["applied"] = applied;
            out["version"] = cmd.version;
            out["flags"] = cmd.flags;
            if (!applied){
                out["error"] = "the command was not applied within 2000 ms";
                return out;
            }
            out["object_id"] = result;
            if (result != OBJECTID_INVALID){
                json object_json = ObjectJsonAtTickBoundary(result,true);
                if (!object_json.contains("error")){
                    out["object"] = object_json;
                }else{
                    //A destroy reports the id it acted on, and that object is gone by now.
                    out["note"] = "the object is no longer in the scene";
                }
            }else if (cmd.type != SIM_CMD_WORLD_SET_GRAVITY){
                //"applied" only means the queue drained it. A handler that found no such object,
                //or no handler at all, still counts as applied - so say so, or a caller reads a
                //declined command as a successful one. world_set_gravity is the legitimate case
                //of a command that concerns no object.
                out["note"] = "the command was drained but its handler acted on nothing - "
                              "bad target, unknown subtype, or no handler for this type. "
                              "See the application log.";
            }
            return out;
        });

    MCPServer::Get()->RegisterTool("asset_list",
        "List the loaded assets - the things object_spawn can build an object from - with the "
        "stable id each one is addressed by. The id is a hash of the asset's name (not a load "
        "order index), so it stays the same across runs and across changes to what else is "
        "loaded.",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json &args) -> json {
            if (!assetmanager){
                return json{ {"error","no asset manager"} };
            }
            json assets = json::array();
            for (Asset* asset:assetmanager->assets){
                assets.push_back(json{
                    {"name",asset->name},
                    {"id",asset->id},
                    {"has_mesh",asset->mesh != NULL}
                });
            }
            return json{ {"assets",assets}, {"count",(int)assets.size()} };
        });

    MCPServer::Get()->RegisterTool("object_spawn",
        "Create an object from a loaded asset and add it to the scene - the same as the debug UI's "
        "Add > Asset menu. Give `asset` (the name) or `asset_id` (from asset_list), and optionally "
        "a position/rotation/scale; rotation is given as in object_set_transform. The object is "
        "created on the physics thread at the start of a tick, so the id it gets is reproducible; "
        "that id is returned along with the object's full state. No physics body is added - use "
        "an app-specific tool if the thing needs a collider.",
        json{
            {"type","object"},
            {"properties", {
                {"asset", {{"type","string"},{"description","asset name, as listed by asset_list"}}},
                {"asset_id", {{"type","number"},{"description","asset id, as listed by asset_list - wins over `asset`"}}},
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world position"}}},
                {"rotation", {{"type","array"},{"items",{{"type","number"}}},{"minItems",4},{"maxItems",4},{"description","[x,y,z,w] quaternion"}}},
                {"axis_degrees", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] rotation in degrees about each axis, applied X, Y, Z"}}},
                {"yaw_degrees", {{"type","number"},{"description","rotation about world up, degrees"}}},
                {"scale", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] scale"}}}
            }}
        },
        [this](const json &args) -> json {
            SimCommand cmd;
            cmd.type = SIM_CMD_OBJECT_SPAWN_ASSET;
            //A name is hashed HERE, at the boundary, so the command itself only ever carries the
            //id - the same rule the MCP tools follow for durations (milliseconds in, ticks
            //onwards). Nothing deeper in the simulation sees the string.
            if (args.contains("asset_id") && args.at("asset_id").is_number()){
                cmd.asset = (assetid_t)args.at("asset_id").get<uint32_t>();
            }else if (args.contains("asset") && args.at("asset").is_string()){
                cmd.asset = AssetIDFromName(args.at("asset").get<std::string>().c_str());
            }else{
                return json{ {"error","give an asset name or asset_id - see asset_list"} };
            }
            std::string error;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),cmd.position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_POSITION;
            }
            if (RotationFromArgs(args,cmd.rotation,error)){
                cmd.flags |= SIM_CMD_FLAG_ROTATION;
            }else if (!error.empty()){
                return json{ {"error",error} };
            }
            if (args.contains("scale")){
                if (!JsonToVec3(args.at("scale"),cmd.scale)){
                    return json{ {"error","scale must be [x,y,z]"} };
                }
                cmd.flags |= SIM_CMD_FLAG_SCALE;
            }
            //The whole reason a command reports a result: the caller cannot know the new object's
            //id in advance, because ids are handed out on the physics thread in tick order, which
            //is what makes them reproducible in the first place.
            objectid_t created = SubmitCommandAndWait(cmd);
            if (created == OBJECTID_INVALID){
                return json{ {"error","nothing was spawned - no such asset, or the command was not applied (see the log)"} };
            }
            return json{ {"id",created}, {"object",ObjectJsonAtTickBoundary(created,true)} };
        });

    //--- Camera -------------------------------------------------------------------------------
    //The camera is an Object, so object_get/object_set_transform can already reach it by name -
    //but only in terms of position and a quaternion. What the orbit controls actually work in is
    //a position, a point being looked AT, and the pivot they turn around, so these report and
    //accept exactly that. Written for debugging the middle-mouse orbit: camera_get before and
    //after a drag says whether a runaway came from the input delta or from the orbit maths.
    /*
        The renderer's own numbers, so they can be read without the ImGui panel.

        The panel (Engine -> Performance) is the same data, but it is behind a collapsing header
        and a screenshot, which makes an A/B measurement a matter of clicking accurately rather
        than of measuring. This repo's measurements are made by agents driving MCP, so the
        timings belong here too.

        `gpu_passes` are GL_TIME_ELAPSED queries - actual GPU execution. `cpu` are wall-clock
        PerfTimers. The two are different machines and do not sum to each other; see the block
        comment on Renderer::gpu_pass_t.
    */
    MCPServer::Get()->RegisterTool("renderer_timings",
        "Per-pass GPU cost and the renderer's CPU timers, in microseconds, as the Engine panel's "
        "Performance section shows them. `gpu_passes` are GL_TIME_ELAPSED queries measuring real "
        "GPU execution; a pass that did not run this frame reads 0. `cpu` are wall-clock timers - "
        "`pick_readback` is the mouse-over readback, which is a CPU stall rather than GPU work. "
        "Each value is a rolling average over the last 60 frames, with the peak alongside.",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json &args) -> json {
            if (!renderer){
                return json{ {"error","no renderer"} };
            }
            json passes = json::array();
            double total_us = 0;
            for (int i=0;i<Renderer::GPU_PASS_COUNT;i++){
                const Renderer::GPUPassTimer* pass = renderer->GetGPUPassTimer(i);
                if (!pass || !pass->timer){
                    continue;
                }
                total_us += pass->timer->avg;
                passes.push_back(json{
                    {"pass",   Renderer::GetGPUPassName(i)},
                    {"avg_us", pass->timer->avg},
                    {"max_us", pass->timer->max},
                });
            }
            json cpu = json::object();
            if (renderer->tmr_frame){
                cpu["renderer_us"] = renderer->tmr_frame->avg;
            }
            if (renderer->tmr_pick_readback){
                cpu["pick_readback_us"] = renderer->tmr_pick_readback->avg;
            }
            if (tmr_render_loop){
                cpu["frame_us"] = tmr_render_loop->avg;
            }
            return json{
                {"gpu_timers_supported", renderer->GPUTimersSupported()},
                {"gpu_passes", passes},
                {"gpu_total_us", total_us},
                {"cpu", cpu},
            };
        });

    MCPServer::Get()->RegisterTool("camera_get",
        "Report the active scene camera: world position, the point it is looking at, its "
        "forward/up/left vectors, its rotation quaternion, the orbit pivot (camera_target) the "
        "middle-mouse orbit and zoom turn around, the distance from the camera to that pivot, and "
        "the perspective viewport settings.",
        json{ {"type","object"}, {"properties", json::object()} },
        [this](const json &args) -> json {
            if (!main_scene || !main_scene->camera){
                return json{ {"error","no camera"} };
            }
            //A camera is written every pass now - UpdateView is where the orbit, the chase and the
            //overhead easing all live - so reading one off-tick returns a pose from part-way
            //through whatever it was doing.
            json result;
            main_scene->AtTickBoundary([&]{
                result = CameraToJson(main_scene->camera,GetCameraTargetPtr());
            });
            return result;
        });

    MCPServer::Get()->RegisterTool("camera_set",
        "Place the active scene camera. Only the fields given are changed. `position` moves it, "
        "`look_at` aims it at a world point (applied after position, so giving both aims from the "
        "new place), `up` is an optional up hint for that aim, and `camera_target` moves the orbit "
        "pivot the middle-mouse orbit and zoom turn around. Setting position and camera_target to "
        "a known pair is how you put the orbit into a repeatable state before testing it. Returns "
        "the resulting camera, same shape as camera_get.",
        json{
            {"type","object"},
            {"properties", {
                {"position", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world position"}}},
                {"look_at", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] world point to aim at"}}},
                {"up", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] up hint used with look_at"}}},
                {"camera_target", {{"type","array"},{"items",{{"type","number"}}},{"minItems",3},{"maxItems",3},{"description","[x,y,z] orbit/zoom pivot"}}}
            }}
        },
        [this](const json &args) -> json {
            if (!main_scene || !main_scene->camera){
                return json{ {"error","no camera"} };
            }
            /*
                Arguments are parsed BEFORE the tick boundary and applied inside it.

                Parsing needs nothing from the scene, so doing it first means a malformed request
                is rejected without ever stopping the simulation. It also makes the write
                all-or-nothing: the old order set the position, then discovered look_at was
                malformed and returned an error, leaving the camera half-moved by a call that
                reported failure.
            */
            vec3 position, look_at, up, camera_target;
            bool f_position = false, f_look_at = false, f_up = false, f_camera_target = false;
            if (args.contains("position")){
                if (!JsonToVec3(args.at("position"),position)){
                    return json{ {"error","position must be [x,y,z]"} };
                }
                f_position = true;
            }
            if (args.contains("look_at")){
                if (!JsonToVec3(args.at("look_at"),look_at)){
                    return json{ {"error","look_at must be [x,y,z]"} };
                }
                f_look_at = true;
            }
            if (args.contains("up")){
                if (!JsonToVec3(args.at("up"),up)){
                    return json{ {"error","up must be [x,y,z]"} };
                }
                f_up = true;
            }
            if (args.contains("camera_target")){
                if (!GetCameraTargetPtr()){
                    return json{ {"error","this application has no camera_target"} };
                }
                if (!JsonToVec3(args.at("camera_target"),camera_target)){
                    return json{ {"error","camera_target must be [x,y,z]"} };
                }
                f_camera_target = true;
            }

            //A camera is read by the render thread every frame and written by UpdateView every
            //pass, so these writes land at a tick boundary rather than whenever this thread
            //happens to be scheduled. The resulting state is read back inside the same boundary,
            //so what comes back is what was actually set.
            json result;
            main_scene->AtTickBoundary([&]{
                Camera* camera = main_scene->camera;
                if (f_position){
                    camera->SetPosition(position);
                }
                if (f_look_at){
                    camera->SetLookAt(look_at,f_up ? &up : NULL);
                }
                if (f_camera_target){
                    *GetCameraTargetPtr() = camera_target;
                }
                camera->CalculateLookatMatrix();
                result = CameraToJson(camera,GetCameraTargetPtr());
            });
            return result;
        });

    MCPServer::Get()->RegisterTool("screenshot",
        "Capture the app's current frame as a PNG. Blocks until the render thread has drawn and "
        "encoded a frame, so what comes back is the window as it is now - the only way to check "
        "anything about how an app LOOKS rather than what its numbers say. The ImGui debug panels "
        "are included by default - telemetry readouts, the object inspector, buttons and sliders "
        "exist only there, so a capture without them silently drops all of it. Pass "
        "include_ui=false for the clean 3D scene when the panels would be in the way. Available in "
        "every app; an app's own tools may also take an include_screenshot argument to return one "
        "alongside their telemetry, which saves a second round trip.",
        json{
            {"type","object"},
            {"properties", {
                {"include_ui", {{"type","boolean"},{"description","include the ImGui panels, default true; false captures the 3D scene alone"}}}
            }}
        },
        [this](const json &args) -> json {
            //MaybeAttachScreenshot reports a failure in a field, which is right when there is a
            //telemetry payload to keep - but here the picture IS the result, so an empty one is
            //an error and worth saying so plainly.
            json result = MaybeAttachScreenshot(json::object(),true,args.value("include_ui",true));
            if (result.contains("screenshot_error")){
                return json{ {"error",result["screenshot_error"]} };
            }
            return result;
        });

    MCPServer::Get()->RegisterTool("shader_reload",
        "Recompile a shader from disk and swap it in, without restarting the app - backlog item "
        "61. `name` is a substring matched against every live shader's vertex and fragment file "
        "name, and every match is rebuilt; call it with no `name` to list what there is. The whole "
        "source set is re-read, #included files included, so editing a shared .glsl reaches every "
        "program that includes it. A shader that fails to compile is reported with its GLSL log "
        "and the last working program stays on screen - it no longer takes the process down. "
        "Assets baked into the binary report that they are not reloadable in this build rather "
        "than recompiling identical bytes and claiming success. The rebuild happens on the render "
        "thread and this call waits for it, so the log you get back is the real one.",
        json{
            {"type","object"},
            {"properties", {
                {"name", {{"type","string"},{"description","substring of the shader's file name, e.g. \"raymarch_volume\"; omitted lists the shaders instead"}}},
                {"include_screenshot", {{"type","boolean"},{"description","return a screenshot of the frame after the reload"}}}
            }}
        },
        [this](const json &args) -> json {
            json result = ReloadShadersAndWait(args.value("name",std::string()));
            //Taken AFTER the reload has been serviced, so the picture is the new shader's - which
            //is the point of asking for one here at all.
            return MaybeAttachScreenshot(result,args.value("include_screenshot",false));
        });
}
