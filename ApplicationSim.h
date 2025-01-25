#ifndef _APPLICATION_SIM_H_
#define _APPLICATION_SIM_H_

#include "Application.h"
#include "RRandom.h"
#include "PerlinNoise.h"
#include "WorleyNoise.h"
#include "StellarBody.h"
#include <vector>
/*
    An attempt at an application that overrides the default, and shows a UI only.
*/



class ApplicationSim : public Application{
public:
    ApplicationSim();

    void Run(void) override;
    void RunLogic() override;

    RRandom rrand;
    PerlinNoise pnoise;
    WorleyNoise wnoise;
    Texture* noise_texture = NULL;

    std::vector<StellarBody*>stellarbodies;
    std::vector<StellarObject*>stellarobjects;
    std::vector<RouteObject*>routeobjects;

    bool create_stellarbodies = false;
    int simulation_interval = 30;

    vec3 target_position = vec3();

    StellarObject* controlling_ship = NULL;     // The ship that we control.
    StellarObject* target_beacon = NULL;        // The location we've selected.

    void StoreStellarObject(StellarObject* object);
    void SetControllingShip(StellarBody* body);

    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);
    void RenderPopulationOverview();
    void RenderRandTestWindow();
    void RenderNoiseTestWindow();
    void RenderSuperCustomUI();
    void UpdateUI();
};

#endif
