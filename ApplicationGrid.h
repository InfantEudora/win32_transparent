#ifndef _APPLICATION_GRID_H_
#define _APPLICATION_GRID_H_

#include "Application.h"
#include "IsoTerrain.h"
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
    Object* selected_object = NULL;



private:
    static DWORD WINAPI GridFrameThreadFunction(LPVOID lpParameter);
    void UpdateUI();
};

#endif
