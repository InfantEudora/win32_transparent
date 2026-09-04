#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"
#include "TankCharacter.h"
#include "BuggyCharacter.h"
#include "CraneCharacter.h"
#include "Heightmap.h"
#include "ParticleEmitter.h"
#include "tinygltf/json.hpp"
#include <vector>
#include "GamePadController.h"

using json = nlohmann::json;

/*
    An attempt at an application that overrides the default, and shows a compass.
*/
class ApplicationTank : public Application{
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
    //outside the controlled_tank/controlled_buggy/controlled_vehicle machinery entirely; driven
    //directly by the "Crane" panel in RenderTankWheelDebugUI via crane_piston_speed.
    CraneCharacter* crane = NULL;
    float crane_piston_speed = 0.0f;
    float crane_extension_speed = 0.0f; //same, for the telescoping extension's slider motor
    float crane_hook_speed = 0.0f;      //same, for the hook's winch (positive lowers)

    void DumpTerrainVertices();
    void TestHeightmapRoundTrip();
    void TestHeightmapMesh();
    void AddTestSceneObjects();
    void RegisterMCPTools();
    json GetTankTelemetry();
    json GetCraneTelemetry();
    json GetBridgeTelemetry();
    json MaybeAttachScreenshot(json result, bool include_screenshot);

    //Shared by Init() (the recorded, permanent placement) and the bridge_spawn MCP tool
    //(an ad hoc one for scouting a new crossing). Fails (returns false, bridge left NULL) if
    //a bridge already exists or the asset/asset manager isn't available.
    bool SpawnBridge(const vec3& pos, float yaw_degrees);

    GamePadController* gamepad_controller = NULL;

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
};

#endif
