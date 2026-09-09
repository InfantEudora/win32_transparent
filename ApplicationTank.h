#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"
#include "SimCommand.h"
#include "TankCharacter.h"
#include "BuggyCharacter.h"
#include "CraneCharacter.h"
#include "Heightmap.h"
#include "ParticleEmitter.h"
#include "tinygltf/json.hpp"
#include <vector>

using json = nlohmann::json;

//SimCommand types this app adds to core's set, numbered from SIM_CMD_LAST up - the same
//convention the input keycodes use with INPUT_LAST. Core carries and orders these like any other
//command without knowing what they mean; the meaning lives entirely in the handler registered in
//RegisterCommandHandlers, which is free to close over TankCharacter/BuggyCharacter. That is the
//whole point of the command-is-data/handler-is-code split - see core/SimCommand.h.
//
//Teleport a vehicle back to rest at the pose in the command's position/rotation. `target` is the
//vehicle's object id.
#define TANK_CMD_VEHICLE_RESET  (SIM_CMD_LAST + 0)
//Switch the crane's electromagnet on (value[0] != 0) or off. Off drops whatever is held.
//A command rather than a plain setter because turning the magnet off DESTROYS a joint, and the
//callers - the debug UI's checkbox, a key, an MCP tool - are all on the wrong thread for that;
//this lands it on the physics thread at the top of a tick. See core/SimCommand.h.
#define TANK_CMD_CRANE_MAGNET   (SIM_CMD_LAST + 1)

/*
    An attempt at an application that overrides the default, and shows a compass.
*/
//rp3d::EventListener is inherited for onTrigger alone: it is how the crane's magnet learns what
//is inside its field (see CraneCharacter's header comment). A world has exactly one listener, so
//it has to live here on the app rather than on the crane - the same arrangement ApplicationDozer
//and ApplicationTileset already use for their own triggers.
class ApplicationTank : public Application, public rp3d::EventListener{
public:
    ApplicationTank();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;
    void RenderTankWheelDebugUI(void);
    //Shared by the tank and buggy sections of RenderTankWheelDebugUI - the per-wheel table only
    //ever reads/writes Wheel fields and Vehicle::WheelRadius/WheelRestLength/WheelTravel, none
    //of which are vehicle-specific, so one function renders it for whichever Vehicle is passed.
    void RenderVehicleWheelTable(Vehicle* vehicle);

    //The crane's own section of the Vehicle Debug window: the velocity commands, the magnet,
    //what each joint currently reads back, and the key legend for driving it from the keyboard.
    void RenderCraneDebugUI(void);

    //reactphysics3d::EventListener. Called from INSIDE PhysicsWorld::Update, on the physics
    //thread: forwards each overlap involving the crane's magnet field collider to the crane,
    //which records it and defers every decision to after the step.
    void onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData) override;

    //A separate, focused control panel - only visible while the buggy is the actively controlled
    //vehicle (see SetControlledVehicle/controlled_vehicle) - as opposed to RenderTankWheelDebugUI's
    //"Buggy" section above, which stays visible any time the buggy exists at all.
    void RenderBuggyControlDebugUI(void);

    Object* compass = NULL;
    Object* target = NULL;
    Object* terrain = NULL;
    Object* heightmap_mesh_test = NULL;
    Object* tank_tracks = NULL;

    //Toggle in RenderTankWheelDebugUI between the pink per-wheel debug visuals (default -
    //shows the raycast-sampled contact points the physics actually uses) and the tank_tracks
    //mesh (the fixed visual band, hidden by default since it doesn't follow wheel.compression).
    bool f_show_wheel_debug_visuals = true;

    //Bridge: a static prop dropped in over MCP, then nudged into position/rotation over MCP
    //too, so a crossing can be placed by eye in the running game rather than guessed at in code.
    //bridge_yaw_degrees is tracked here rather than decomposed back out of bridge's quaternion
    //on read - this is the only thing that ever sets that rotation, so it's the source of truth.
    Object* bridge = NULL;
    float bridge_yaw_degrees = 0.0f;

    //Fired on INPUT_FIRE (see RunLogic) - bursts a handful of copies of the target marker
    //itself at the target's current position, as a placeholder impact effect until a real
    //explosion/muzzle-flash asset exists.
    ParticleEmitter* fire_impact_emitter = NULL;

    //One dust-kicking emitter per controlled_buggy->wheels entry (same order/index), parented to
    //the BUGGY BODY, not the individual wheel Object - a wheel's own visual carries steer/roll
    //rotation, which would fling emitted particles in whatever direction the tire currently
    //happens to be pointing rather than a consistent "up off the ground" spray. Repositioned
    //every frame in UpdateBuggyWheelSpinParticles to track each wheel's own current
    //(compression-adjusted) position anyway, so being a sibling rather than a child of the wheel
    //costs nothing.
    std::vector<ParticleEmitter*> buggy_wheel_spin_emitters;
    //Repositions each buggy_wheel_spin_emitters entry onto its wheel's current position and
    //bursts a couple of tiny cube particles from any wheel that's actively spinning out this
    //tick (driven AND friction-saturated - see BuggyCharacter::UpdatePhysicsState's own
    //free-spin blend, the same condition). Called every frame regardless of window focus, same
    //reasoning as SnapCameraToControlledVehicle - the vehicle keeps simulating either way.
    void UpdateBuggyWheelSpinParticles();

    //Captured once in Init(), right after controlled_tank's spawn position/rotation are set -
    //the "Reset Tank To Start" button in RenderTankWheelDebugUI feeds these straight into
    //TankCharacter::ResetState.
    vec3 tank_start_position = {};
    quat tank_start_rotation = {};

    //Same idea, captured once the buggy is spawned.
    vec3 buggy_start_position = {};
    quat buggy_start_rotation = {};

    //Suspension test bed: one static box-collider prop per buggy wheel (same order as
    //controlled_buggy->wheels), spawned directly below each wheel's reach. The buggy's own body
    //is held STATIC and suspended in mid-air while this is in use (see Init()), so dragging one
    //of these up into a wheel's raycast is what compresses that wheel's suspension - the "Buggy
    //Suspension Test Bed" panel in RenderTankWheelDebugUI is what does the dragging.
    std::vector<Object*> buggy_test_cubes;

    //A joint-based test rig, unrelated to the tank/buggy - see CraneCharacter's own header
    //comment for what it's testing. Not a Vehicle (no wheels/pedals/steering), so it stays
    //outside the controlled_tank/controlled_buggy/controlled_vehicle machinery - but it IS one of
    //the three rigs the "Controlling" selector in RenderTankWheelDebugUI can hand the keyboard
    //to; see f_crane_controlled below.
    //
    //These four floats are the crane's whole command state, and the ONE source of truth for it:
    //the debug panel's sliders, the crane_speed MCP tool and the keyboard/gamepad all write
    //them, and RunLogic pushes them into the crane every tick. Nothing calls CraneCharacter's
    //Set*Speed with a value that isn't one of these.
    CraneCharacter* crane = NULL;
    float crane_piston_speed = 0.0f;
    float crane_extension_speed = 0.0f; //same, for the telescoping extension's slider motor
    float crane_hook_speed = 0.0f;      //same, for the hook's winch (positive lowers)
    float crane_slew_speed = 0.0f;      //same, for the base turning on the spot (positive = CCW from above)

    //The third state of the "Controlling" selector. The crane isn't a Vehicle so it cannot live
    //in controlled_vehicle, and exactly one rig has the keyboard at a time - so this flag and
    //controlled_vehicle are mutually exclusive, and SetControlledCrane/SetControlledVehicle are
    //what keep them that way. Never write either by hand: each releases the outgoing rig's
    //inputs, without which it would carry on running on whatever it was last commanded.
    bool f_crane_controlled = false;
    void SetControlledCrane();
    //Zeroes all four crane_*_speed commands (and the hardware latches below) and pushes them to
    //the crane - the crane's equivalent of Vehicle::ReleaseInputs, and used for the same reason:
    //switching away mid-slew must not leave the base turning forever.
    void ReleaseCraneInputs();
    //Whichever rig currently has the keyboard - the controlled vehicle, or the crane's base.
    //What the camera snap/follow pivots on, so "Follow" works while driving the crane too.
    Object* GetControlledObject();

    //The crane command each hardware axis (keyboard/gamepad) asked for on the PREVIOUS tick.
    //A hardware command overwrites its crane_*_speed only while it is nonzero, plus the single
    //tick on which it returns to zero - which is what releasing the key does. Sitting on the
    //value every tick instead would work, but it would also mean an idle keyboard permanently
    //pinned the debug panel's sliders (and any crane_speed MCP call) to 0 for as long as the
    //crane happened to be the selected rig. See ApplyHardwareCraneAxis in ApplicationTank.cpp.
    float crane_hw_slew = 0.0f;
    float crane_hw_boom = 0.0f;
    float crane_hw_extension = 0.0f;
    float crane_hw_hook = 0.0f;

    void DumpTerrainVertices();
    void TestHeightmapRoundTrip();
    void TestHeightmapMesh();
    void AddTestSceneObjects();
    void RegisterMCPTools();
    //Handlers for this app's own SimCommand types (TANK_CMD_*). Called from Init(), next to
    //RegisterMCPTools, since the tools submit the commands these handle.
    void RegisterCommandHandlers();
    //Builds the magnet command. Trivial, but it exists for the same reason
    //MakeVehicleResetCommand does: three callers (the UI checkbox, the G key, the MCP tool) must
    //not each hand-assemble the payload.
    SimCommand MakeCraneMagnetCommand(bool on) const;
    //Builds the reset command for a vehicle, picking the right recorded spawn pose. Shared by the
    //tank_reset MCP tool (which waits for it via SubmitCommandAndWait) and the debug UI's reset
    //buttons (which must NOT wait - see Application::SubmitCommandAndWait for why). Returns a
    //command with type SIM_CMD_NONE if the vehicle isn't one of ours.
    SimCommand MakeVehicleResetCommand(Vehicle* vehicle) const;
    //Which vehicle an MCP call is about: args["vehicle"] is "tank" (default, so every existing
    //caller keeps working unchanged) or "buggy". NULL if that vehicle doesn't exist.
    Vehicle* ResolveVehicleArg(const json& args);

    //The MCP tools speak milliseconds because that is the human-facing unit a caller reasons in;
    //the simulation speaks ticks. Convert here, at the boundary, and nowhere deeper - see the
    //Vehicle::HoldDrive comment for why a duration must never reach the sim as wall-clock time.
    //Rounded UP, so a duration shorter than one tick still yields one tick of input, never none.
    uint32_t DurationMsToTicks(float duration_ms) const {
        if (duration_ms <= 0.0f){
            return 0;
        }
        float ticks = ceilf(duration_ms / 1000.0f * physics_tps);
        return (uint32_t)max(ticks,1.0f);
    }
    //How much real time those ticks will take, for the blocking MCP calls to wait out. Ticks are
    //paced by physics_time_factor, so this is not simply duration_ms again.
    DWORD TicksToRealMs(uint32_t ticks) const {
        return (DWORD)((float)ticks * GetPhysicsTimestep() * 1000.0f / max(physics_time_factor,0.01f));
    }
    //Position/velocity/mass/centre of mass plus the per-wheel suspension and tire breakdown for
    //any Vehicle, and the vehicle-specific extras (turret, tuning) for the tank and buggy.
    json GetVehicleTelemetry(Vehicle* vehicle);
    json GetTankTelemetry();
    json GetCraneTelemetry();
    json GetBridgeTelemetry();
    json MaybeAttachScreenshot(json result, bool include_screenshot);

    //Shared by Init() (the recorded, permanent placement) and the bridge_spawn MCP tool
    //(an ad hoc one for scouting a new crossing). Fails (returns false, bridge left NULL) if
    //a bridge already exists or the asset/asset manager isn't available.
    bool SpawnBridge(const vec3& pos, float yaw_degrees);

    //The one InputController, reached under its old name so the gamepad call sites read the same.
    //It IS main_window->inputcontroller - the separate GamePadController class is gone.
    InputController* gamepad_controller = NULL;

    TankCharacter* controlled_tank = NULL;
    BuggyCharacter* controlled_buggy = NULL;

    //Whichever of controlled_tank/controlled_buggy currently receives keyboard/RunLogic input -
    //toggled by the "Controlling" selector in RenderTankWheelDebugUI. Both vehicles exist and
    //simulate simultaneously; this only decides where the arrow keys/fire key go. NULL until
    //Init() has spawned at least one of them.
    Vehicle* controlled_vehicle = NULL;
    void SetControlledVehicle(Vehicle* vehicle);

    //Moves camera_target (the point the middle-mouse orbit/zoom pivots around) onto the
    //controlled vehicle, carrying the camera along by the same delta so its distance and height
    //above the pivot are preserved, then gently steers its horizontal orbit angle back around
    //behind the vehicle's own current heading (see camera_behind_blend_rate) - so the camera
    //chases the vehicle's tail instead of just translating while keeping whatever angle the
    //mouse last set. No-op with no controlled vehicle. Called by the "Snap To Vehicle" button,
    //and every frame while f_camera_follow_vehicle is set.
    void SnapCameraToControlledVehicle();
    //While set, SnapCameraToControlledVehicle runs every frame, so the orbit pivot rides along
    //with the vehicle and the camera keeps chasing behind its heading. Orbiting and zooming stay
    //fully usable while following - both are relative to the pivot/distance this only ever
    //nudges the ANGLE of, so grabbing the view with the mouse still works, it just drifts back
    //toward directly-behind again over the next few frames.
    bool f_camera_follow_vehicle = false;
    //How much of the way from the camera's CURRENT horizontal orbit angle to directly-behind-
    //the-vehicle SnapCameraToControlledVehicle closes each call (0 = never turns, 1 = snaps
    //instantly every frame) - a partial step rather than an instant snap so a sharp turn doesn't
    //whip the view around in one frame.
    float camera_behind_blend_rate = 0.05f;
private:
    vec3 camera_target = {};
    //Lets the core camera_get/camera_set MCP tools see the orbit pivot - see Application.
    vec3* GetCameraTargetPtr() override{ return &camera_target; }
};

#endif
