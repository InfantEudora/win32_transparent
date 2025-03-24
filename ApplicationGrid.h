#ifndef _APPLICATION_GRID_H_
#define _APPLICATION_GRID_H_

#include "Application.h"
#include "IsoTerrain.h"
#include "type_plane.h"
#include "type_ray.h"

#include "GLTFLoader.h"
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

    GLTFLoader gltfloader;


    struct{
        int grid_level = 0;
        int tile_number = 1;
        bool f_place = true;        // Place new tile on left click
        bool f_selection = false;   // Show and move selection tile
        bool f_delete = true;       // Delete on right click
        bool f_place_prop = true;   // Place a prop on tile
    }grid_settings;

private:
    static DWORD WINAPI GridFrameThreadFunction(LPVOID lpParameter);
    void UpdateUI();
};

#endif
