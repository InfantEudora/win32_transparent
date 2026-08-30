#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"
#include "TankCharacter.h"
#include "Heightmap.h"

/*
    An attempt at an application that overrides the default, and shows a compass.
*/
class ApplicationTank : public Application{
public:
    ApplicationTank();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;

    Object* compass = NULL;
    Object* target = NULL;
    Object* terrain = NULL;
    Object* heightmap_mesh_test = NULL;

    void DumpTerrainVertices();
    void TestHeightmapRoundTrip();
    void TestHeightmapMesh();

    TankCharacter* controlled_tank = NULL;
private:
    vec3 camera_target = {};
};

#endif
