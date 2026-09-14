#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"
#include "HTTPServer.h"
#include "OCPPClient.h"
#include "OCPPServerHandler.h"
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

    // Fermata emulation only: the charger pushes its customMeterValues
    // DataTransfer on its own cadence, independent of MeterValues.
    DWORD lastDataTransferMs = 0;
    DWORD dataTransferIntervalMs = 5000;
    DWORD lastV2GTickMs = 0;
    bool  autoSendDataTransfer = true;
};
/*
    An attempt at an application that overrides the default, and shows a UI only.
*/
class ApplicationOCPP : public Application{
public:
    ApplicationOCPP();

    void Init(void) override;
    void UpdateView() override;
    void DrawImGuiUI(void) override;

    void RenderOCPPServerUI();
    void RenderOCPPClientsUI();
    void RenderTCPClientsUI();

    HTTPServer* http_server = NULL;

    /*
        The OCPP protocol, which THIS APP owns. Until 2026-09-14 it was a member of HTTPServer
        and reached as http_server->ocpp, which made a charge-point protocol part of the engine's
        generic web server and so of every app that links core - see the note in apps/ocpp/makefile.

        A pointer, not a value, because it has to be constructed with two callbacks bound to
        http_server, and http_server is not built until Init(). Created there and installed into
        the server's WebSocketApp hooks in the same breath.
    */
    OCPPServerHandler* ocpp = NULL;

    TCPClient* tcp_client = NULL;

    std::deque<OCPPClient*> ocpp_clients;
    std::deque<VehicleSimulation> vehicle_sims;
};

#endif
