#ifndef _APPLICATION_TILESET_H_
#define _APPLICATION_TILESET_H_

#include "Application.h"
#include "type_plane.h"
#include "type_ray.h"
#include "HTTPServer.h"
#include "Isoterrain.h"
#include "IsoCar.h"
#include "IsoHouse.h"
#include "SpriteSheet.h"
#include "RouteObject.h"

enum IsoTool{
    ISO_TOOL_NONE       = 0,
    ISO_TOOL_ROAD       = 1,
    ISO_TOOL_CAR        = 2,
    ISO_TOOL_TREE       = 3,
    ISO_TOOL_TERRAIN    = 4,
    ISO_TOOL_HOUSE      = 5,
};

enum collision_category_bits{
    COLLISION_CATEGORY_CAR = 0x0001,
    COLLISION_CATEGORY_CAR_PROXIMITY = 0x0002,
    COLLISION_CATEGORY_SCENERY = 0x0004
};

/*
    An attempt at an application that overrides the default, and shows a grid.
*/
class ApplicationTileset : public Application, public rp3d::EventListener{
public:
    ApplicationTileset();
    ~ApplicationTileset();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;
    void RenderHTTPTestUI();
    void RenderOCPPClientsUI();
    void RenderToolsUI();
    void RenderTerrainUI();
    void RenderSelectedRoadUI();
    void RenderSelectedCarUI();

    SoundSystem* soundsystem = NULL;
    SpriteSheet* icon_sprites = NULL;

    vec3 camera_target = {};

    IsoTerrain* terrain = NULL;
    IsoTool current_tool = ISO_TOOL_NONE;
    void StartDrag(IsoCell* cell);

    IsoCar* selected_car = NULL;
    IsoCar* controlled_car = NULL;
    IsoRoad* selected_road = NULL;
    RouteObject* route_object = NULL;
    std::vector<IsoCar*> cars;
    std::vector<IsoHouse*> houses;
    void PlaceCar(IsoCell* target_cell);
    void PlaceHouse(IsoCell* target_cell);

    bool f_place_road_marker = false;
    bool f_mouse_over_terrain = false;
    vec3 mouse_terrain_coord = vec3();
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


    void onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData) override;
    //void notifyContact(rp3d::OverlappingPair* overlappingPair,const rp3d::ContactPointInfo& contactInfo);
};

#endif
