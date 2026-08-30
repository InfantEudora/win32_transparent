#ifndef _APPLICATION_TANK_H_
#define _APPLICATION_TANK_H_

#include "Application.h"

/*
    An attempt at an application that overrides the default, and shows a compass.
*/
class ApplicationTank : public Application{
public:
    ApplicationTank();

    void Init(void) override;
    void RunLogic() override;

    void DrawImGuiUI(void) override;

    Object* compass = NULL;
private:
    vec3 camera_target = {};
};

#endif
