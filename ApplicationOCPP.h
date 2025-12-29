#ifndef _APPLICATION_UI_H_
#define _APPLICATION_UI_H_

#include "Application.h"
#include "HTTPServer.h"
#include "OCPPClient.h"
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
    OCPPClient* ocpp_client = NULL;
};

#endif
