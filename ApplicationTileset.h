#ifndef _APPLICATION_TILESET_H_
#define _APPLICATION_TILESET_H_

#include "Application.h"
#include "type_plane.h"
#include "type_ray.h"
/*
    An attempt at an application that overrides the default, and shows a grid.
*/
class ApplicationTileset : public Application{
public:
    ApplicationTileset();

    void Run(void) override;
    void RunLogic() override;

    vec3 camera_target = {};
private:

    void UpdateUI();
};

#endif
