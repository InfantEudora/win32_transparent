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

//Optional, the same way it is in Object.h: with USE_PHYSICS off this header is not included and
//no rp3d symbol is named, so the library is neither built nor linked. physics_world is declared
//either way and is simply always NULL.
#ifdef USE_PHYSICS
#include "PhysicsWorld.h"
#endif
#include "SimCommand.h"

/*
    NO imgui.h here either, and this one was doing real damage rather than merely being untidy.

    Scene uses nothing from ImGui - not a type, not a call, checked - but this header is reached
    by every app through Application.h, and it included imgui.h WITHOUT first defining
    IMGUI_DEFINE_MATH_OPERATORS. So ImGui was always pulled in early and without that define, and
    any translation unit that later included imgui_internal.h properly - apps/grid and apps/sim do -
    hit imgui_internal.h's "#error Please '#define IMGUI_DEFINE_MATH_OPERATORS' _BEFORE_ including
    imgui.h!". That was invisible only because core/Window.h happened to define it even earlier;
    taking ImGui out of Window.h on 2026-09-14 is what exposed it.

    See the note at the top of core/Window.h for the general rule: a header includes what it uses.
*/

class Scene{
public:
    Scene();
    std::string name;
    InputController* inputcontroller = NULL;
    Renderer* renderer = NULL;
    Shader* shader = NULL;

    //Always declared so Scene's shape and every `if (physics_world)` test stay the same in both
    //builds; without USE_PHYSICS nothing can assign it. Same treatment as Object::physics.
#ifdef USE_PHYSICS
    PhysicsWorld* physics_world = NULL;
#else
    void*         physics_world = NULL;
#endif

    /*
        Everything in this scene, top level only - children hang off their parents and are reached
        by recursing, which is what CullObjects, ForEachObject and FindObject all do.

        THIS IS WHAT MAKES A SCENE A SCENE. It used to live on Renderer, which meant every Scene
        sharing a Renderer shared one world: switching scenes swapped the camera, the physics
        world, the tick clock and the command handlers, and left the same objects on screen. The
        Renderer now gets this list handed to it per frame and keeps only what it derives from it.

        Owned here, and freed by DeleteDestroyedObjects below.
    */
    std::vector<Object*> objects;


    void UpdateInput();
    //Only on a pass that ticks, and before anything reads an edge - see
    //InputController::ApplyTickInput, which is the whole of it and says why it is its own call.
    void UpdateTickInput();

    /*
        Called once per pass of the physics loop, straight after UpdateInput and before anything
        else in the pass. It drains the command queue, services the pause key, and decides whether
        this pass runs a simulated tick - returning that decision, which stays readable for the
        rest of the pass through IsTickingThisPass().

        The decision is made HERE, once, rather than by each stage re-deriving it from f_paused and
        pending_physics_steps. While UpdateAnimations and UpdatePhysics each tested for themselves,
        a StepPhysics() call from another thread landing between the two tests made them disagree,
        and that step ran its physics without its animation frame. One answer per pass cannot
        disagree with itself.

        Command draining and the pause key live here for a different reason: both have to happen on
        EVERY pass, including the ones that do not tick. A paused editor still has to accept
        commands and still has to see the key that unpauses it.
    */
    bool BeginPass();

    //The answer BeginPass reached for the pass now running. False on a pass that is only spinning
    //because the simulation is paused: view work still runs on those, simulation work must not.
    bool IsTickingThisPass(){ return f_tick_this_pass; }

    //delta_time is the fixed simulation timestep - it becomes each object's animation_time_delta
    //(unless that object opted out, see Object::f_animation_time_delta_override), so animation advances
    //on simulated ticks rather than on a hardcoded 20ms.
    void UpdateAnimations(float delta_time);
    void UpdatePhysics(float delta_time);
    void DrawFrame();

    void AddObject(Object* object);

    /*
        Deletes every object marked by Object::Destroy, and their destroyed children. This is the
        only thing that actually frees them and takes their rigid bodies out of the physics world;
        Destroy() alone just stops them being drawn.

        CALL IT FROM THE SIMULATION TICK - RunSimulationTick, or anything else the physics loop
        runs while it holds physics_mutex. That is the whole constraint: this erases from the same
        object list the render thread walks in Renderer::CullObjects and DrawFrame, so it must not
        run while that thread is in there. The physics loop holds the mutex across the tick, so a
        call made from inside the tick is mutually exclusive with rendering for free. A call from
        an MCP handler or any render-thread code is NOT - use a SimCommand to get onto the tick.

        IT IS DELIBERATELY NOT AUTOMATIC. The engine could call this at the end of every pass and
        for a while it looked like it should - four apps had each worked out the same answer
        independently (Dozer, Ship, Tetris, Breakout). It stays opt-in because WHEN an object
        stops existing is a gameplay decision: a tick that destroys something and then looks at it
        again is doing something ordinary, and an app that wants everything gone before it rebuilds
        a level - ApplicationTetris::NewGame - wants to say exactly where that happens. What was
        actually missing was this paragraph, not a call.

        An app that creates objects at run time and never calls this leaks them and, worse, keeps
        their colliders live in the physics world for the rest of the run.

        It was Renderer::DeleteDestroyedObjects until the object list moved here; a call on the
        renderer is now a compile error rather than a silent no-op, which is the point.
    */
    void DeleteDestroyedObjects();

    /*
        Run fn with the simulation held still. Returns whether it ran.

        The read-side counterpart to SubmitCommand. A command is how another thread WRITES the
        simulation; this is how it READS one. A tool handler on the MCP server's thread holds no
        lock, so walking the object list or serialising a transform from there is a plain data race
        against the physics thread - and an object it is half-way through reading can be moved, or
        destroyed, underneath it.

        It is called AtTickBoundary rather than "Lock" because of what the lock happens to mean
        here. The physics thread holds renderer->physics_mutex across a whole pass - input, the
        tick, then the view work - and releases it only between passes. So taking it does not
        merely make the read atomic, it lands the read BETWEEN ticks: nothing fn sees is
        half-stepped, and GetPhysicsTick() inside fn names the tick that produced the state fn is
        looking at.

        Writing from inside is allowed and is sometimes the only option - the debug UI has always
        done exactly this, and Scene::MoveObjectOverTicks has no command form - but prefer a
        SimCommand where one exists. A command is tick-stamped, and so survives into a replay;
        a direct write is not and does not.

        Two rules for fn, both of which deadlock if broken:
          - It must not wait on the physics thread. No SubmitCommandAndWait, no StepPhysicsAndWait,
            no polling GetPendingPhysicsSteps or GetPendingObjectMotions - the physics thread
            cannot reach any of those while this call holds the lock it needs.
          - It must not wait on the render thread either, which rules out screenshots:
            Application::MaybeAttachScreenshot blocks on Renderer::RequestScreenshot, and the
            render thread takes this same mutex to draw. Capture before or after, never inside.
        Keep fn short for the same reason the debug UI keeps its work short: the simulation is
        stopped for exactly as long as it runs.
    */
    bool AtTickBoundary(const std::function<void()>& fn);

    //All three walk the whole tree (children included), depth-first, in `objects` order.
    Object* FindObject(const std::string& name);
    Object* FindObjectByID(objectid_t id);
    void ForEachObject(const std::function<void(Object*)>& fn);

    //Moves object from wherever it is when the first tick runs to the given target(s) over
    //exactly `ticks` physics ticks, interpolated (lerp/slerp). NULL leaves that component alone.
    //
    //"Exactly `ticks`" is now true for an object WITHOUT physics: the motion is retired on the
    //same tick as its last interpolation step. An object WITH a physics body still occupies one
    //tick more, and must - the last tick sets a velocity that the solver has not integrated yet,
    //so the body is not on target until the following tick. Budget ticks+1 for those.
    //(It used to be ticks+1 for everything, including plain objects, where the extra tick did
    //nothing but write the target back over whatever else had moved the object that tick. See
    //docs/engine_backlog.md item 32 for the bug that caused.)
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

    //Same, but APPENDS instead of replacing: this leg starts from wherever the previous one left
    //the object, on the tick after it finishes. Replacing is still the right default - a game
    //re-issuing "go here" every tick wants the latest target, not a thousand queued legs - but it
    //makes a there-and-back impossible to express, because only one leg of it survives. A camera
    //shake, a piece bouncing as it lands, a panel sliding out and back: each is Queue, Queue.
    //
    //Legs for one object run strictly in submission order; legs for DIFFERENT objects are
    //independent and run concurrently, exactly as separate motions always have.
    //MoveObjectOverTicks clears the whole queue for that object, so it remains the way to cancel.
    void QueueObjectMotion(Object* object,const vec3* target_position,const quat* target_rotation,int ticks);

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

    void PausePhysics(bool paused){
        f_paused = paused;
        if (!paused){
            //Resuming discards anything still queued. A step is a request to advance a STOPPED
            //simulation; once it is running again that request has been granted and then some.
            //Leaving them queued meant they fired as a burst of extra ticks at the START of the
            //next pause - a pause that advanced a tick before it froze.
            pending_physics_steps = 0;
        }
    }

    //While paused, queues up num_steps physics ticks (each a full physics_world->Update() +
    //every object's UpdatePhysicsState(), same as a normal unpaused tick) for the physics
    //thread to run one at a time - lets a caller single-step the simulation deterministically
    //instead of guessing how long to sleep and hoping nothing else advanced in the meantime.
    void StepPhysics(int num_steps){
        //Genuinely a no-op while running, which is what this always claimed to be and was not.
        //It used to add to the counter regardless, and UpdatePhysics only ever decrements it on a
        //paused tick, so a step queued against a running simulation sat there forever and then
        //spent itself the moment someone paused. Found by the sim_pause/sim_step tools reporting
        //pending_steps: 1 on a free-running scene.
        if (!f_paused){
            return;
        }
        pending_physics_steps += max(num_steps,0);
    }
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
    //Atomic for the same reason pending_physics_steps below is: an MCP tool handler runs on the
    //server's own thread and holds no lock, so sim_pause writes this while the physics thread is
    //reading it. A plain bool made that a data race - benign in practice on x86, but the fix is
    //one word and the tools that write it are now core rather than one app's debug aid.
    std::atomic<bool> f_paused{false};
    std::atomic<int> pending_physics_steps{0}; //written from any thread, consumed by UpdatePhysics on the physics thread

    //Set by BeginPass, read by everything downstream in the same pass. Physics-thread-only, so it
    //needs no synchronisation of its own - the whole pass runs under renderer->physics_mutex.
    bool f_tick_this_pass = false;

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
        //This flag is therefore also what AdvanceObjectMotions dispatches on to decide WHERE a
        //motion retires - a plain object finishes on its last interpolating tick instead, which is
        //what makes `ticks` mean ticks for it. The claim above that the extra tick was for physics
        //motions "only" was the intent but not the behaviour until that was fixed.
        bool f_kinematic = false;
        //int, not rp3d::BodyType, when there is no rp3d to name. Same size and same value
        //(BodyType is a plain enum class over int, STATIC == 0), so ObjectMotion's layout does
        //not move - and with physics off f_kinematic is never set, so it is never read either.
#ifdef USE_PHYSICS
        rp3d::BodyType previous_body_type = rp3d::BodyType::STATIC;
#else
        int            previous_body_type = 0;
#endif
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
