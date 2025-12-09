#ifndef _APPLICATION_TILESET_H_
#define _APPLICATION_TILESET_H_

#include "Application.h"
#include "type_plane.h"
#include "type_ray.h"
#include "HTTPServer.h"
/*
    An attempt at an application that overrides the default, and shows a grid.
*/
class ApplicationTileset : public Application{
public:
    ApplicationTileset();
    ~ApplicationTileset();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;

    vec3 camera_target = {};
private:
	HTTPServer* http_server = nullptr;

    // UI variables
    int ui_playerHealth = 100;
    int ui_score = 0;

    void UpdateUI();
};

#endif
