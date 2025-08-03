#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"
#include "DozerCharacter.h"
#include "SoundSystem.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationDozer : public Application{
public:
    ApplicationDozer();

    void Run(void) override;
    void RunLogic() override;

    vec3 camera_target = {};
    Scene* CreateMainScene();
    DozerCharacter* dozer = NULL;
    bool dozer_camera_tracking = true;
    void SpawnAssetAt(const std::string& name, const vec3& wpos);
    SoundSystem* soundsystem = NULL;

    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);

private:
    void UpdateUI();
};

#endif
