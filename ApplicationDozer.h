#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationDozer : public Application{
public:
    ApplicationDozer();

    void Run(void) override;
    void RunLogic() override;

    Scene* CreateMainScene();

    static DWORD WINAPI FrameThreadFunction(LPVOID lpParameter);

private:
    void UpdateUI();
};

#endif
