#ifndef _APPLICATION_CANUI_H_
#define _APPLICATION_CANUI_H_

#include "Application.h"
#include "caninterfaces/PCANReader.h"
#include "caninterfaces/controllers/InfyPower.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationCANUI : public Application{
public:
    ApplicationCANUI();

    void Run(void) override;
    void RunLogic() override;

    PCANReader* pcan_reader = NULL;
    InfyPower* infy = NULL;

private:
    void UpdateUI();
};

#endif
