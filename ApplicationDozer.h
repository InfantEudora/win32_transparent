#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"
#include "DozerCharacter.h"
#include "SoundSystem.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationDozer : public Application, public rp3d::EventListener{
public:
    ApplicationDozer();


    void Init(void) override;

    void RunLogic(void) override;
    void DrawImGuiUI(void) override;

    vec3 camera_target = {};
    Scene* CreateMainScene();
    DozerCharacter* dozer = NULL;
    void ResetDozer();
    bool dozer_camera_tracking = true;
    void SpawnAssetAt(const std::string& name, const vec3& wpos);
    SoundSystem* soundsystem = NULL;

private:
    //reactphysics3d::EventListener
    void onContact(const rp3d::CollisionCallback::CallbackData& callbackData) override;
    void onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData) override;

};

#endif
