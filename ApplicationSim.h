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

    std::vector<StellarBody>stellarbodies;
    bool create_stellarbodies = false;

    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);
    void RenderPopulationOverview();
    void RenderRandTestWindow();
    void RenderNoiseTestWindow();
    void UpdateUI();
};

#endif
