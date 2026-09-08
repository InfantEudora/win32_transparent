#ifndef _SCENE_H_
#define _SCENE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <functional>
#include <unordered_map>

#include "type_fmat3.h"
#include "type_fmat4.h"

#include "InputController.h"
#include "Renderer.h"
#include "Shader.h"

#include "PhysicsWorld.h"
#include "SimCommand.h"

#include "imgui.h"

class Scene{
public:
    Scene();
    std::string name;
    InputController* inputcontroller = NULL;
    Renderer* renderer = NULL;
    Shader* shader = NULL;

    PhysicsWorld* physics_world = NULL;


    void UpdateInput();
    //delta_time is the fixed simulation timestep - it becomes each object's animation_time_delta
    //(unless that object opted out, see Object::f_animation_time_delta_override), so animation advances
    //on simulated ticks rather than on a hardcoded 20ms.
    void UpdateAnimations(float delta_time);
    void UpdatePhysics(float delta_time);
    void DrawFrame();

    void AddObject(Object* object);

    //All three walk the whole tree (children included), depth-first, in renderer->objects order.
    Object* FindObject(const std::string& name);
    Object* FindObjectByID(objectid_t id);
    void ForEachObject(const std::function<void(Object*)>& fn);

    //Moves object from wherever it is when the first tick runs to the given target(s) over
    //exactly `ticks` physics ticks, interpolated (lerp/slerp). NULL leaves that component alone.
    //
    //An object WITH a physics body is switched to KINEMATIC for the duration and driven by
    //velocity - each tick gets the linear/angular velocity that carries it to the next
    //interpolated pose, and on the tick after the last it's snapped to the exact target, its
    //velocities zeroed and its previous body type restored. A collider on it shoves dynamic
    //bodies out of the way, and joints attached to it follow properly. The obvious alternative
    //- SetPosition (setTransform) every tick, which is what dragging the Generic Object UI's
    //slider does - teleports the body with zero velocity: joints on it only see a position
    //error after the fact, and the per-tick corrections leak into any free joint DOF (a hinged
    //boom on a yawing base flaps around and drifts straight through its angle limits).
    //
    //An object without physics just gets SetPosition/SetRotation each tick. Requesting a motion
    //for an object that already has one replaces it. Callable from any thread (e.g. an MCP tool
    //handler); consumed on the physics thread.
    void MoveObjectOverTicks(Object* object,const vec3* target_position,const quat* target_rotation,int ticks);
    int GetPendingObjectMotions();

    //--- Simulation commands ------------------------------------------------------------------
    //Changes to the simulation that are NOT input (create/teleport/destroy an object, reset a
    //vehicle), coming from the render thread (debug UI) or the MCP thread. See SimCommand.h for
    //why they are data rather than virtual objects, and for the command/handler split that lets
    //an app add its own types without core knowing them.
    //
    //Callable from ANY thread. Returns the command's sequence number: 1 for the first command
    //ever submitted, and up from there. The command has NOT run yet when this returns.
    uint32_t SubmitCommand(const SimCommand& cmd);

    //The sequence number of the last command that has been APPLIED on the physics thread. A
    //submitter that needs to report the result waits for this to reach its own sequence:
    //   uint32_t seq = scene->SubmitCommand(cmd);
    //   while (scene->GetAppliedCommandSequence() < seq && !timed_out){ Sleep(5); }
    //Commands are applied in submission order, so this single number covers every earlier one
    //too. It advances while PAUSED as well (commands drain before the pause check), so a caller
    //waiting on it does not hang against a paused editor.
    uint32_t GetAppliedCommandSequence(){ return applied_command_sequence; }

    //The object a given applied command created or acted on, or OBJECTID_INVALID. This is how a
    //spawn reports back WHAT it spawned - the submitter cannot know the id in advance, because
    //ids are handed out on the physics thread in tick order (which is what makes them
    //reproducible). Only the most recent handful of results are kept; a sequence older than that
    //returns OBJECTID_INVALID, so read it promptly after the wait above.
    objectid_t GetCommandResult(uint32_t sequence);

    //Installs the code for one command type. The handler runs on the PHYSICS THREAD, inside the
    //tick, with the renderer's physics_mutex already held - so it may touch objects and physics
    //bodies freely, and must not take that lock itself or block. It returns the object it created
    //or acted on (OBJECTID_INVALID if none), which is what GetCommandResult reports.
    //
    //Registering a type twice replaces the handler. Core's own types are registered by
    //Application::RegisterCoreCommandHandlers (not here, because spawning needs the
    //AssetManager, which Scene has no business knowing about); an app registers its own from
    //Init() alongside its MCP tools.
    void RegisterCommandHandler(uint16_t type, std::function<objectid_t(const SimCommand&)> handler);

    //How many submitted commands have not been applied yet. For a caller that wants to poll
    //rather than compare sequences.
    int GetPendingCommands();

    bool IsPhysicsPaused(){return f_paused;}
    void PausePhysics(bool paused){f_paused = paused;}

    //While paused, queues up num_steps physics ticks (each a full physics_world->Update() +
    //every object's UpdatePhysicsState(), same as a normal unpaused tick) for the physics
    //thread to run one at a time - lets a caller single-step the simulation deterministically
    //instead of guessing how long to sleep and hoping nothing else advanced in the meantime.
    //A no-op while not paused, since there's nothing to "step" - it's already running freely.
    void StepPhysics(int num_steps){ pending_physics_steps += max(num_steps,0); }
    int GetPendingPhysicsSteps(){ return pending_physics_steps; }

    //THE simulation clock: how many physics ticks have actually RUN. It advances only when a
    //tick really executes, so it measures simulated time, not wall time - it does not move while
    //paused, and single-stepping advances it by exactly num_steps. Anything in the simulation
    //that has a duration (input hold-latches, timers, cooldowns) must be denominated in these
    //ticks and never in GetTickCount64()/real milliseconds: real time is ~15.6ms-granular against
    //a 20ms tick, and it keeps running while the sim is paused or single-stepped, so a wall-clock
    //duration silently means something different every run. Written by the physics thread, read
    //from any, hence atomic.
    uint64_t GetPhysicsTick(){ return physics_tick; }

    //The fixed simulation timestep in seconds, as last handed to UpdatePhysics. Objects read this
    //instead of hardcoding 0.02f. It is a constant by design - see Application::GetPhysicsTimestep.
    float GetPhysicsTimestep(){ return physics_timestep; }

    //Do we always need a handle to a single camera?
    Camera* camera = NULL;
private:
    std::atomic<uint64_t> physics_tick{0};
    float physics_timestep = 0.02f;
    bool f_paused = false;
    std::atomic<int> pending_physics_steps{0}; //written from any thread, consumed by UpdatePhysics on the physics thread

    //See MoveObjectOverTicks. start_* are captured on the first tick the motion actually runs,
    //not when it was requested, so it always starts from the object's real current pose.
    // A stipped down version of ObjectAnimation
    struct ObjectMotion{
        Object* object = NULL;
        bool f_position = false;
        bool f_rotation = false;
        vec3 start_position = {};
        vec3 target_position = {};
        quat start_rotation;
        quat target_rotation;
        int ticks_total = 1;
        int ticks_done = 0;
        //Physics-driven motions only: the body type to put back when done. The motion lives one
        //tick past ticks_total for that (the velocity set on the last tick still has to play out).
        bool f_kinematic = false;
        rp3d::BodyType previous_body_type = rp3d::BodyType::STATIC;
    };
    std::vector<ObjectMotion> object_motions;
    std::mutex object_motions_mutex;
    void AdvanceObjectMotions(float delta_time); //one tick's worth, physics thread only

    //See SubmitCommand. The queue is swapped out wholesale under the lock and the handlers then
    //run unlocked, so a handler is free to submit further commands (they land on the next tick)
    //without deadlocking on this mutex.
    struct QueuedCommand{
        uint32_t sequence = 0;
        SimCommand cmd;
    };
    std::vector<QueuedCommand> pending_commands;
    std::mutex commands_mutex;
    std::atomic<uint32_t> command_sequence{0};          //last sequence HANDED OUT by SubmitCommand
    std::atomic<uint32_t> applied_command_sequence{0};  //last sequence actually applied
    std::unordered_map<uint16_t,std::function<objectid_t(const SimCommand&)>> command_handlers;
    //Ring of the last COMMAND_RESULT_RING results, indexed by sequence % size. A ring rather than
    //a growing map because this exists only to hand a spawn's object id back to a caller that is
    //actively waiting on it - nobody asks about a command from a thousand ticks ago, and an
    //unbounded map of every command ever submitted would just leak for the life of the run.
    static const int COMMAND_RESULT_RING = 64;
    struct CommandResult{
        uint32_t sequence = 0;
        objectid_t object = OBJECTID_INVALID;
    };
    CommandResult command_results[COMMAND_RESULT_RING];
    void DrainCommands(); //physics thread only, top of UpdatePhysics
};

#endif
