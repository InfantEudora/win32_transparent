#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"
#include "DozerCharacter.h"
#include "DozerButton.h"
#include "SoundSystem.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationDozer : public Application, public rp3d::EventListener{
public:
    ApplicationDozer();


    void Init(void) override;

    void UpdateView(void) override;
    void RunSimulationTick(void) override;
    void DrawImGuiUI(void) override;

    vec3 camera_target = {};
    Scene* CreateMainScene();
    DozerCharacter* dozer = NULL;
    int dozer_floor_contact_points = 0;
    void ResetDozer();
    bool dozer_camera_tracking = true;
    void SpawnAssetAt(const std::string& name, const vec3& wpos);
    SoundSystem* soundsystem = NULL;

    //The steel beam's own voice. Kept only so the impact sound can ask whether the LAST one is
    //still going before starting another - a beam landing every frame would otherwise stack a
    //dozen copies of itself now that a sound can overlap. An ordinary one-shot otherwise.
    soundhandle_t snd_steelbeam = SOUND_INVALID_HANDLE;

    DozerButton* CreateDozerButton(Scene* scene);

    void ExportSceneString();

protected:
    void RenderDebugMenuBarClass(void) override;

private:
    //reactphysics3d::EventListener
    void onContact(const rp3d::CollisionCallback::CallbackData& callbackData) override;
    void onTrigger(const rp3d::OverlapCallback::CallbackData& callbackData) override;
};

#endif
