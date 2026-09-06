#ifndef _SCENE_H_
#define _SCENE_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <functional>

#include "type_fmat3.h"
#include "type_fmat4.h"

#include "InputController.h"
#include "Renderer.h"
#include "Shader.h"

#include "PhysicsWorld.h"

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
};

#endif
