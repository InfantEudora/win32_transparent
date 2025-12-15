#ifndef _APPLICATION_TILESET_H_
#define _APPLICATION_TILESET_H_

#include "Application.h"
#include "type_plane.h"
#include "type_ray.h"
#include "HTTPServer.h"
#include "Isoterrain.h"
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

    IsoTerrain* terrain = NULL;
private:
	HTTPServer* http_server = nullptr;

    // UI variables
    int ui_playerHealth = 100;
    int ui_score = 0;
    // Per-string cell voltages in mV and metadata
    int ui_string0_mv = 3300;
    int ui_string0_min_mv = 3290;
    int ui_string0_max_mv = 3310;
    int ui_string0_temp_c = 25;

    int ui_string1_mv = 3300;
    int ui_string1_min_mv = 3280;
    int ui_string1_max_mv = 3320;
    int ui_string1_temp_c = 24;

    int ui_string2_mv = 3300;
    int ui_string2_min_mv = 3270;
    int ui_string2_max_mv = 3330;
    int ui_string2_temp_c = 23;
    // Per-string state-of-charge (percent)
    int ui_string0_soc = 100;
    int ui_string1_soc = 80;
    int ui_string2_soc = 60;
    // Mode availability toggles
    bool ui_mode_netzero_enabled = true;
    bool ui_mode_charge_enabled = true;
    bool ui_mode_discharge_enabled = true;

    void UpdateUI();
};

#endif
