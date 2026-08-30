#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"
#include "TankCharacter.h"
#include "Heightmap.h"
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

    void DumpTerrainVertices();
    void TestHeightmapRoundTrip();
    void TestHeightmapMesh();
    void RegisterMCPTools();
    json GetTankTelemetry();
    json MaybeAttachScreenshot(json result, bool include_screenshot);

    TankCharacter* controlled_tank = NULL;
private:
    vec3 camera_target = {};
};

#endif
