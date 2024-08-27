#ifndef _APPLICATION_CANUI_H_
#define _APPLICATION_CANUI_H_

#include "Application.h"
#include "caninterfaces/PCANReader.h"
#include "caninterfaces/controllers/InfyPower.h"
#include "caninterfaces/controllers/DeltaSM15K.h"

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
    DeltaSM15K* delta = NULL;

    ThreadSafeQueue<CANMessage> infy_queue;
    ThreadSafeQueue<CANMessage> vector_queue;
    ThreadSafeQueue<CANMessage> cab500_queue;

private:
    static DWORD WINAPI CANReadThreadFunction(LPVOID lpParameter);
    void UpdateUI();
};

#endif
