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
    RenderOCPPServerUI();
    RenderOCPPClientsUI();
    RenderDebugMenuBar();
}

void ApplicationOCPP::RenderOCPPServerUI(){
    ImGui::Begin("OCPP Server");
    if (!http_server){
        ImGui::Text("No HTTP server running.");
        ImGui::End();
        return;
    }

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
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void ApplicationOCPP::RenderTCPClientsUI(){
    ImGui::Begin("TCP Client");
    if (!tcp_client){

        ImGui::Text("No TCP Client");
        if (ImGui::Button("Create")){
            tcp_client = new TCPClient();
        }
        ImGui::End();
        return;
    }

    static char server_address[128] = "127.0.0.1:9090";
    ImGui::InputText("Server Address", server_address, 128);

    if (!tcp_client->IsConnected()){
        if (ImGui::Button("Connect")){
            // Parse server address into host and port
            std::string addr_str(server_address);
            size_t colon_pos = addr_str.find(':');

            if (colon_pos != std::string::npos){
                std::string host = addr_str.substr(0, colon_pos);
                std::string port_str = addr_str.substr(colon_pos + 1);

                int port = std::stoi(port_str);
                debug->Info("Connecting to %s:%d\n", host.c_str(), port);

                if (tcp_client->Connect(host, port)){
                    debug->Info("Successfully connected to server\n");
                } else {
                    debug->Err("Failed to connect to server\n");
                }
            } else {
                debug->Err("Invalid server address format. Use host:port\n");
            }
        }
    }

    if (tcp_client->IsConnected()){
        ImGui::Text("Connected");
        if (ImGui::Button("Send some garbage")){
            tcp_client->Send("Garbage\n");
        }
    }
    ImGui::End();
}

void ApplicationOCPP::RenderOCPPClientsUI(){
    ImGui::Begin("OCPP Client");
    if (!ocpp_client){

        ImGui::Text("No OCPP Client");
        if (ImGui::Button("Create")){
            ocpp_client = new OCPPClient();
        }
        ImGui::End();
        return;
    }

    static char server_address[128] = "ws://127.0.0.1:9090";
    static char ocpp_id[128] = "CP_1";
    ImGui::InputText("Server Address", server_address, 128);
    ImGui::InputText("OCPP ID", ocpp_id, 128);

    if (!ocpp_client->IsConnected()){
        if (ImGui::Button("Connect")){
            // Parse server address into host and port
            std::string addr_str(server_address);

            // Strip protocol prefix (ws://, wss://, http://, https://)
            size_t protocol_end = addr_str.find("://");
            if (protocol_end != std::string::npos){
                addr_str = addr_str.substr(protocol_end + 3);
            }

            size_t colon_pos = addr_str.find(':');

            if (colon_pos != std::string::npos){
                std::string host = addr_str.substr(0, colon_pos);
                std::string port_str = addr_str.substr(colon_pos + 1);

                int port = std::stoi(port_str);
                debug->Info("Connecting to %s:%d\n", host.c_str(), port);

                if (ocpp_client->ConnectOCPP(host, port, std::string(ocpp_id))){
                    debug->Info("Successfully connected to OCPP server\n");
                } else {
                    debug->Err("Failed to connect to OCPP server\n");
                }
            } else {
                debug->Err("Invalid server address format. Use host:port\n");
            }
        }
    }

    if (ocpp_client->IsConnected()){
        ImGui::Text("Connected");
        if (ImGui::Button("Send Boot Notification")){
            ocpp_client->SendBootNotification("MyVendor", "ChargePoint-v1");
        }
        if (ImGui::Button("Send Status Notification")){
            ocpp_client->SendStatusNotification(0, "Available");
        }
        if (ImGui::Button("Send Heartbeat")){
            ocpp_client->SendHeartbeat();
        }
        if (ImGui::Button("Send Metervalues")){
            ocpp_client->SendMeterValues(1, 7400.0, 85.5);
        }


    }
    ImGui::End();
}