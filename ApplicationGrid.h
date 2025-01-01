#ifndef _APPLICATION_GRID_H_
#define _APPLICATION_GRID_H_

#include "Application.h"
#include "IsoTerrain.h"
#include "type_plane.h"
#include "type_ray.h"

/*
    An attempt at an application that overrides the default, and shows a grid.
*/
class ApplicationGrid : public Application{
public:
    ApplicationGrid();

    void Run(void) override;
    void RunLogic() override;

    vec3 camera_target = {};

    IsoTerrain* terrain = NULL;

    Object* selection_tile = NULL;


    struct{
        int grid_level = 0;
        int tile_number = 1;
        bool f_place = true;    // Place on left click
        bool f_delete = true;   // Delete on right click
    }grid_settings;

private:
    static DWORD WINAPI GridFrameThreadFunction(LPVOID lpParameter);
    void UpdateUI();
};

#endif
