#ifndef _APPLICATION_ANIMATION_H_
#define _APPLICATION_ANIMATION_H_

#include "Application.h"
#include "IsoTerrain.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationIsoAnimation : public Application{
public:
    ApplicationIsoAnimation();

    void Init(void) override;
    void UpdateView() override;
    void RunSimulationTick() override;

    void DrawImGuiUI(void) override;
    void RenderDebugMenuBarClass(void) override;
    bool f_show_demo_window = false;
    bool f_show_shader_window = false;

    void RenderSkeletonUI();
    void RenderBoneModifierHeader(Bone* bone, int id);
    void SetCharacterUniforms(void);

    Scene* CreateEmptyScene();

    //The test environment will contain a IsoTerrain with some platforms, stairs, etc
    //To test all animations.
    IsoTerrain* test_terrain = NULL;

    void BuildTestEnvironment();

    DirectionalLight* sun = NULL;
    PlayerCharacter* character = NULL;
    PlayerCharacter* hands = NULL; //Preview of where the character's hands will be for a given animation.
    PlayerCharacter* feet = NULL;  //Preview of where the character's feet will be for a given animation.

    bool f_ik_arm = false;
    Skeleton* selected_skeleton = NULL;

    Object* target_indicator = NULL;

    //The targeting-arc decal's shader, registered with Renderer::AddCustomShader. Held here
    //rather than reached through the renderer: there is no single custom-shader slot any
    //more, and SetCharacterUniforms needs to push uniforms into THIS one.
    Shader* indicator_shader = NULL;

    //Hand/foot landing-spot debug tool: click a surface to place a target (hand target on a
    //wall-like surface, foot target on a floor-like one, classified by the hit normal), and compare
    //it against where the previewed animation's hands/feet actually end up.
    bool f_mode_place_target = false;
    Object* hand_target = NULL;   //Where you want the hands to land (set by clicking a wall-like surface)
    Object* foot_target = NULL;   //Where you want the feet to land (set by clicking a floor-like surface)
    Object* hand_landing = NULL;  //Where the previewed animation's hands actually are right now (midpoint of both hands)
    Object* foot_landing = NULL;  //Where the previewed animation's feet actually are right now (midpoint of both feet)
    void UpdateHandFootLandingMarkers();
private:
    vec3 camera_target = {};
    bool f_filemodal = false;
    std::string filemodal_filename;
    bool f_import_file = false;
    bool f_mode_grab = false;
    bool f_mode_camera_track = false;
};

#endif
