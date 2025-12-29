#include "ApplicationOCPP.h"
#include "Debug.h"

static Debugger *debug = new Debugger("ApplicationOCPP", DEBUG_ALL);

ApplicationOCPP::ApplicationOCPP():Application(){
    debug->Info("Created new application.\n");
};

void ApplicationOCPP::Init(void){
    int2 dimensions = GetDisplaySettings();
    renderer = new Renderer(main_window->width,main_window->height);
    if (!renderer->Init(PIPELINE_MSAA)){
        debug->Fatal("Failed to Initilise Rendering Pipeline\n");
    }

    //Create a renderer for this window
    renderer = new Renderer(main_window->width,main_window->height);
    renderer->Init();


    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1024,768);

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics();

    //Create an HTTP server to listen for connections
    http_server = new HTTPServer(9090);
    if (!http_server->Start()){
        debug->Fatal("Failed to start HTTP server\n");
    }

    debug->Info("HTTP Server started on port 9090\n");


}

//Called before update physics
void ApplicationOCPP::RunLogic(){

}

void ApplicationOCPP::DrawImGuiUI(){
    RenderOCPPClientsUI();
    RenderDebugMenuBar();
}

void ApplicationOCPP::RenderOCPPClientsUI(){
    if (!http_server) return;

    ImGui::Begin("OCPP Clients");

    ImGui::Text("Connected OCPP Clients: %d", (int)http_server->m_ocppClients.size());
    int client_idx = 0;
    for (SOCKET client_socket : http_server->m_ocppClients){
        ImGui::PushID(client_idx);
        ImGui::Text("Client %d - Socket %llu", client_idx, (unsigned long long)client_socket);
        client_idx++;

        OCPPClientData* data = http_server->GetOCPPClientData(client_socket);
        if (data) {
            // Access any OCPP data for this client
            std::string vendor = data->chargePointVendor;
            std::string status = data->connectorStatus;
            std::string lastTag = data->lastAuthorizedIdTag;
            // etc.
            ImGui::Text("Status: %s",status.c_str());

            double power = data->powerActiveImport;  // in Watts
            double soc = data->soc;                   // in Percent
            std::string timestamp = data->meterValuesTimestamp;
            ImGui::Text("SOC   : %.1f%% ",soc);
            ImGui::Text("Power : %.1f Watt",power);
            ImGui::Text("Time  : %s",timestamp.c_str());

            if (ImGui::Button("Get Metervalues")){

            }


        }

        ImGui::PopID();
    }

    ImGui::End();
}