#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"
#include "HTTPServer.h"
#include "OCPPClient.h"
#include <deque>
#include <windows.h>

enum class VehicleState {
    Unplugged,
    PluggedIn,
    Charging,
    Finished
};

struct VehicleSimulation {
    VehicleState state = VehicleState::Unplugged;
    double meterKwh = 0.0;       // Total session energy (kWh)
    double powerKw = 7.4;        // Simulated charge power (kW)
    DWORD lastTickMs = 0;        // Last time meter was updated
    DWORD lastMeterSendMs = 0;   // Last time MeterValues was sent to server
    DWORD meterSendIntervalMs = 30000; // How often to send MeterValues (ms)
    int transactionId = 1;
    char idTag[64] = "SimTag";   // RFID tag for this session
};
/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationOCPP : public Application{
public:
    ApplicationOCPP();

    void Init(void) override;
    void RunLogic() override;
    void DrawImGuiUI(void) override;

    void RenderOCPPServerUI();
    void RenderOCPPClientsUI();
    void RenderTCPClientsUI();

    HTTPServer* http_server = NULL;

    TCPClient* tcp_client = NULL;

    std::deque<OCPPClient*> ocpp_clients;
    std::deque<VehicleSimulation> vehicle_sims;
};

#endif
