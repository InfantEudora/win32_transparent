#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"
#include "TankCharacter.h"
#include "Heightmap.h"
#include "ParticleEmitter.h"
#include "tinygltf/json.hpp"

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
private:
    vec3 camera_target = {};
};

#endif
