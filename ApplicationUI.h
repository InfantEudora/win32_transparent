#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"

/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationUI : public Application{
public:
    ApplicationUI();

    void Run(void) override;
    void RunLogic() override;

private:
    void UpdateUI();
};

#endif
