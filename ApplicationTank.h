#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"
#include "TankCharacter.h"
#include "BuggyCharacter.h"
#include "Heightmap.h"
#include "ParticleEmitter.h"
#include "tinygltf/json.hpp"
#include <vector>

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

    void DumpTerrainVertices();
    void TestHeightmapRoundTrip();
    void TestHeightmapMesh();
    void RegisterMCPTools();
    json GetTankTelemetry();
    json GetBridgeTelemetry();
    json MaybeAttachScreenshot(json result, bool include_screenshot);

    //Shared by Init() (the recorded, permanent placement) and the bridge_spawn MCP tool
    //(an ad hoc one for scouting a new crossing). Fails (returns false, bridge left NULL) if
    //a bridge already exists or the asset/asset manager isn't available.
    bool SpawnBridge(const vec3& pos, float yaw_degrees);

    TankCharacter* controlled_tank = NULL;
    BuggyCharacter* controlled_buggy = NULL;

    //Whichever of controlled_tank/controlled_buggy currently receives keyboard/RunLogic input -
    //toggled by the "Controlling" selector in RenderTankWheelDebugUI. Both vehicles exist and
    //simulate simultaneously; this only decides where the arrow keys/fire key go. NULL until
    //Init() has spawned at least one of them.
    Vehicle* controlled_vehicle = NULL;
    void SetControlledVehicle(Vehicle* vehicle);

    //Moves camera_target (the point the middle-mouse orbit/zoom pivots around) onto the
    //controlled vehicle, carrying the camera along by the same delta so the current viewing
    //angle and distance are preserved - a snap of the PIVOT, not a jump to a fixed chase pose.
    //No-op with no controlled vehicle. Called by the "Snap To Vehicle" button, and every frame
    //while f_camera_follow_vehicle is set.
    void SnapCameraToControlledVehicle();
    //While set, SnapCameraToControlledVehicle runs every frame, so the orbit pivot rides along
    //with the vehicle and the camera keeps whatever angle/distance the mouse last set. Orbiting
    //and zooming stay fully usable while following, since both are relative to the pivot.
    bool f_camera_follow_vehicle = false;
private:
    vec3 camera_target = {};
};

#endif
