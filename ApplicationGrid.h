#ifndef _APPLICATION_GRID_H_
#define _APPLICATION_GRID_H_

#include "Application.h"

/*
    An attempt at an application that overrides the default, and shows a grid.
*/
class ApplicationGrid : public Application{
public:
    ApplicationGrid();

    void Run(void) override;
    void RunLogic() override;

    Object* arrows = NULL;

private:
    static DWORD WINAPI GridFrameThreadFunction(LPVOID lpParameter);
    void UpdateUI();
};

#endif
