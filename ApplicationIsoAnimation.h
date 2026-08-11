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
    void RunLogic() override;

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
    IsoTerrain* terrain = NULL;
    void BuildTestEnvironment();

    DirectionalLight* sun = NULL;
    PlayerCharacter* character = NULL;

    bool f_ik_arm = false;
    Skeleton* selected_skeleton = NULL;

    Object* target_indicator = NULL;
private:
    vec3 camera_target = {};
    bool f_filemodal = false;
    std::string filemodal_filename;
    bool f_import_file = false;
    bool f_mode_grab = false;
    bool f_mode_camera_track = false;
};

#endif
