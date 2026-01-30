#ifndef _APPLICATION_ANIMATION_H_
#define _APPLICATION_ANIMATION_H_

#include "Application.h"
#include "ship/Asteroid.h"
#include "ship/ShipCharacter.h"
#include "ship/ShipCollisionMasks.h"
#include "GamePadController.h"
#include "AsteroidExplosion.h"

class ApplicationShip : public Application, public rp3d::EventListener{
public:
    ApplicationShip();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;

    DirectionalLight* sun = NULL;
    ShipCharacter* ship_character = NULL;
    std::vector<Asteroid*> asteroids;

    Scene* CreateEmptyScene();
    GamePadController* gamepad_controller = NULL;

    std::vector<AsteroidExplosion*>active_asteroid_explosions;
    std::vector<AsteroidExplosion*>new_asteroid_explosions;

private:
    vec3 camera_target = {};
    float zoom_target = 20.0f;
    bool f_filemodal = false;
    std::string filemodal_filename;
    bool f_import_file = false;
    bool f_mode_grab = false;
    bool f_mode_camera_track = true;
    bool f_lock_ship_axis = true;

    void onContact(const rp3d::CollisionCallback::CallbackData& callbackData) override;
};

#endif
