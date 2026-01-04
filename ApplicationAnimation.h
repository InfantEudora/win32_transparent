#ifndef _APPLICATION_ANIMATION_H_
#define _APPLICATION_ANIMATION_H_

#include "Application.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationAnimation : public Application{
public:
    ApplicationAnimation();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;

    Scene* CreateEmptyScene();

    DirectionalLight* sun = NULL;
    PlayerCharacter* character = NULL;

private:
    vec3 camera_target = {};
    bool f_filemodal = false;
    std::string filemodal_filename;
    bool f_import_file = false;
};

#endif
