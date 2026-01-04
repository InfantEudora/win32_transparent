#ifndef _APPLICATION_GRID_H_
#define _APPLICATION_GRID_H_

#include "Application.h"
#include "IsoTerrain.h"
#include "type_plane.h"
#include "type_ray.h"
#include "PlayerCharacter.h"
#include "IsoRoom.h"
/*
    An attempt at an application that overrides the default, and shows a grid.
*/
#include "imgui.h"
#include "imgui_internal.h"


Skeleton* FindSkeletonInScene(Scene* scene, const std::string& name);

class ApplicationGrid : public Application{
public:
    ApplicationGrid();

    void Init(void) override;

    void DrawImGuiUI(void) override;
    void RunLogic() override;

    vec3 camera_target = {};

    IsoTerrain* terrain = NULL;
    std::vector<IsoRoom*>rooms;

    Object* selection_tile = NULL;

    bool f_show_rightclick_menu = false;
    int2 rightclick_menu_coord;
    vec3 rightclick_menu_normal;    //Normal under cursor at the time of clicking
    Object* rightclick_menu_object = NULL;

    struct{
        int grid_level = 0;
        int tile_number = 1;
        bool f_place = true;              // Place new tile on left click
        bool f_selection = false;         // Show and move selection tile
        bool f_delete = true;             // Delete on right click
        bool f_place_prop = true;         // Place a prop on tile
        bool f_camera_control = false;
    }grid_settings;


    bool f_track_cursor = false;
    Animation* selected_animation = NULL;

    PlayerCharacter* character = NULL;


private:
    //Test scene, so we can load stuff and not see it.
    Scene* test_scene = NULL;
    Scene* CreateEmptyScene();
    Scene* CreateTestScene();
    Scene* CreateBoneTestScene();
    Scene* CreateHandTestScene();

    //Rooms
    IsoRoom* FindRoomByCell(IsoCell* cell); // Returns if supplied cell belongs to a room.
    void CreateRoom(IsoCell* center, int size_x, int size_y);

    static DWORD WINAPI GridFrameThreadFunction(LPVOID lpParameter);

    void RenderRightClickMenu_IsoCell(IsoCell* cell);
    void RenderRightClickMenu_IsoWall(IsoWall* wall);
    void RenderRightClickMenu();
    int  MenuQueryDirection();

    void RenderBoneModifierHeader(Bone* bone, int id);
    void RenderGridUI();
    void RenderSkeletonUI();
    void RenderAnimationUI();

};

#endif
