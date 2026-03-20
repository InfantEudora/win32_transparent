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

    default_shader = new Shader("shaders/default.vert","shaders/default.frag");

    main_window->Resize(1024,768);

    main_scene = CreateNewScene("Main Scene");
    main_scene->UpdatePhysics(1.0f / physics_tps * physics_time_factor);

    //Create an HTTP server to listen for connections
    http_server = new HTTPServer(9090);
    if (!http_server->Start()){
        debug->Fatal("Failed to start HTTP server\n");
    }

    debug->Info("HTTP Server started on port 9090\n"); 

}

//Called before update physics
void ApplicationOCPP::RunLogic(){
    // Check if any OCPP clients need charging profile updates
    if (http_server) {
        DWORD now_ms = GetTickCount();
        for (SOCKET client_socket : http_server->m_ocppClients) {
            OCPPClientData* data = http_server->GetOCPPClientData(client_socket);
            if (data && data->server_current_timit_updatereq) {
                // Rate limit: only send updates once per second (1000ms)
                DWORD time_since_last_update_ms = now_ms - data->last_profile_update_time_ms;
                if (time_since_last_update_ms >= 1000) {
                    // Send SetChargingProfile request
                    int connectorId = (data->connectorId > 0) ? data->connectorId : 1;
                    http_server->SendSetChargingProfile(client_socket, connectorId, data->server_current_limit);

                    // Update the last update time
                    data->last_profile_update_time_ms = now_ms;

                    // Clear the update flag
                    data->server_current_timit_updatereq = false;
                }
            }
        }
    }

    // Tick vehicle simulations
    DWORD now_ms = GetTickCount();
    for (int i = 0; i < (int)ocpp_clients.size(); i++) {
        OCPPClient* client = ocpp_clients[i];
        VehicleSimulation& sim = vehicle_sims[i];

        if (sim.state != VehicleState::Charging || !client->IsConnected())
            continue;

        if (sim.lastTickMs == 0) {
            sim.lastTickMs = now_ms;
            continue;
        }

        DWORD elapsed_ms = now_ms - sim.lastTickMs;
        if (elapsed_ms >= 100) {
            double elapsed_hours = elapsed_ms / 3600000.0;
            sim.meterKwh += sim.powerKw * elapsed_hours;
            sim.lastTickMs = now_ms;
        }

        if (now_ms - sim.lastMeterSendMs >= sim.meterSendIntervalMs) {
            client->SendMeterValues(1, sim.powerKw * 1000.0, 0.0);
            sim.lastMeterSendMs = now_ms;
        }
    }
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
            ImGui::Text("Chargebox Path %s", data->path.c_str());

            // Access any OCPP data for this client
            std::string vendor = data->chargePointVendor;
            std::string status = data->connectorStatus;
            std::string lastTag = data->lastAuthorizedIdTag;
            // etc.
            ImGui::Text("Status     : %s",status.c_str());

            double power = data->powerActiveImport;  // in Watts
            double ac_voltage = data->powerActiveImport;
            double soc = data->soc;                  // in Percent
            std::string timestamp = data->meterValuesTimestamp;
            ImGui::Text("SOC        : %.1f%% ",soc);
            ImGui::Text("AC Voltage : %.1f Watt",power);
            ImGui::Text("Power      : %.1f Watt",power);
            ImGui::Text("Time       : %s",timestamp.c_str());

            static float current_limit = 16.0f;
            if (ImGui::SliderFloat("Set Current Limit for Session",&current_limit,5,32)){
                data->server_current_limit = current_limit;
                data->server_current_timit_updatereq = true;
            }

            // Transaction history table for this chargepoint
            std::vector<OCPPTransaction> history = http_server->GetTransactionHistory(client_socket);
            if (!history.empty()) {
                ImGui::Separator();
                ImGui::Text("Transaction History (%d)", (int)history.size());
                if (ImGui::BeginTable("txhistory", 8,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit,
                    ImVec2(0, 120))) {
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableSetupColumn("TxID");
                    ImGui::TableSetupColumn("Conn");
                    ImGui::TableSetupColumn("IdTag");
                    ImGui::TableSetupColumn("Start (Wh)");
                    ImGui::TableSetupColumn("Stop (Wh)");
                    ImGui::TableSetupColumn("Energy (Wh)");
                    ImGui::TableSetupColumn("Start Time");
                    ImGui::TableSetupColumn("Stop / Reason");
                    ImGui::TableHeadersRow();
                    for (const OCPPTransaction& tx : history) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%d", tx.transactionId);
                        ImGui::TableNextColumn(); ImGui::Text("%d", tx.connectorId);
                        ImGui::TableNextColumn(); ImGui::Text("%s", tx.idTag.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%d", tx.meterStart);
                        if (tx.completed) {
                            ImGui::TableNextColumn(); ImGui::Text("%d", tx.meterStop);
                            ImGui::TableNextColumn(); ImGui::Text("%d", tx.meterStop - tx.meterStart);
                            ImGui::TableNextColumn(); ImGui::Text("%s", tx.startTimestamp.c_str());
                            ImGui::TableNextColumn();
                            if (!tx.stopReason.empty())
                                ImGui::Text("%s (%s)", tx.stopTimestamp.c_str(), tx.stopReason.c_str());
                            else
                                ImGui::Text("%s", tx.stopTimestamp.c_str());
                        } else {
                            ImGui::TableNextColumn(); ImGui::TextDisabled("--");
                            ImGui::TableNextColumn(); ImGui::TextDisabled("--");
                            ImGui::TableNextColumn(); ImGui::Text("%s", tx.startTimestamp.c_str());
                            ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0,1,0,1), "Active");
                        }
                    }
                    ImGui::EndTable();
                }
            }
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
        ImGui::TextColored(ImVec4(0,1,0,1),"Connected");
        if (ImGui::Button("Send some garbage")){
            tcp_client->Send("Garbage\n");
        }
    }
    ImGui::End();
}

void ApplicationOCPP::RenderOCPPClientsUI(){
    ImGui::Begin("OCPP Clients");
    if (ocpp_clients.size() == 0){
        ImGui::Text("No OCPP Clients");        
        if (ImGui::Button("Create New OCPP Client")){
            OCPPClient* new_ocpp_client = new OCPPClient();
            ocpp_clients.push_back(new_ocpp_client);
            vehicle_sims.push_back(VehicleSimulation());
        }
        ImGui::End();
        return;
    }

    int index = -1;
    for (OCPPClient* ocpp_client:ocpp_clients){
        index++;        
        std::string header = "OCPP Client " + std::to_string(index);
        if (ImGui::CollapsingHeader(header.c_str(),ImGuiTreeNodeFlags_DefaultOpen)){    
            ImGui::PushID(index);

            ImGuiTabBarFlags tab_bar_flags = ImGuiTabBarFlags_None;
            if (ImGui::BeginTabBar("OCPP Client Tabbar", tab_bar_flags))
            {
                if (ImGui::BeginTabItem("Connection")){
                    char server_address[128] = {};
                    char ocpp_id[128] = {};

                    if (ocpp_client->GetInfo().serverUrl.length() == 0){
                        debug->Warn("Client Len = 0\n");
                        //Load default addres and ID:
                        //sprintf_s(server_address,"ws://127.0.0.1:9090");
                        sprintf_s(server_address,"ws://10.239.1.42:8081");
                        sprintf_s(ocpp_id,"OCPP/Tester");
                        ocpp_client->GetInfo().serverUrl = server_address;
                        ocpp_client->GetInfo().chargeBoxIdentity = ocpp_id;                          
                    }
                    sprintf_s(server_address,ocpp_client->GetInfo().serverUrl.c_str());
                    sprintf_s(ocpp_id,ocpp_client->GetInfo().chargeBoxIdentity.c_str());
                    

                    if (ImGui::InputText("Server Address", server_address, 128)){
                        ocpp_client->GetInfo().serverUrl = server_address;
                    }
                    if (ImGui::InputText("OCPP ID", ocpp_id, 128)){
                        ocpp_client->GetInfo().chargeBoxIdentity = ocpp_id;
                    }
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
                    }else if (ocpp_client->IsConnected()){   
                        ImGui::TextColored(ImVec4(0,1,0,1),"Connected");
                        ImGui::SameLine();
                        if (ImGui::Button("Disconnect")){
                            ocpp_client->Disconnect();
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("OCPP Messages")){
                    if (ocpp_client->IsConnected()){
                        if (ImGui::Button("Send Boot Notification")){
                            ocpp_client->SendBootNotification("MyVendor", "ChargePoint-v1");
                        }

                        static const char* cpStatuses[] = {
                            "Available", "Preparing", "Charging", "SuspendedEVSE",
                            "SuspendedEV", "Finishing", "Reserved", "Unavailable", "Faulted"
                        };
                        static int selectedStatus = 0;
                        ImGui::Combo("Chargepoint State", &selectedStatus, cpStatuses, IM_ARRAYSIZE(cpStatuses));
                        ImGui::SameLine();
                        if (ImGui::Button("Send Status Notification")){
                            ocpp_client->SendStatusNotification(0, cpStatuses[selectedStatus]);
                        }
                        if (ImGui::Button("Send Heartbeat")){
                            ocpp_client->SendHeartbeat();
                        }

                        static char authorizeIdTag[64] = "TagNoUnderscore";
                        ImGui::InputText("IdTag", authorizeIdTag, sizeof(authorizeIdTag));
                        ImGui::SameLine();
                        if (ImGui::Button("Send Authorize")){
                            ocpp_client->SendAuthorize(authorizeIdTag);
                        }
                        if (ImGui::Button("Send Metervalues")){
                            ocpp_client->SendMeterValues(1, 7400.0, 85.5);
                        }
                        if (ImGui::Button("Send StartTransaction")){                    
                            ocpp_client->SendStartTransaction(1, "TagNoUnderscore", 100.0);
                        }
                        if (ImGui::Button("Send StopTransaction")){            
                            ocpp_client->SendStopTransaction(1, 110.0,"Local");
                        }                        
                    }else{
                        ImGui::Text("Client is not connected\n");
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Meter / Session Data")){
                    VehicleSimulation& sim = vehicle_sims[index];

                    // State label
                    const char* stateNames[] = { "Unplugged", "Plugged In", "Charging", "Finished" };
                    int stateIdx = (int)sim.state;
                    ImVec4 stateColor = stateIdx == 2 ? ImVec4(0,1,0,1) : stateIdx == 1 ? ImVec4(1,1,0,1) : ImVec4(1,1,1,1);
                    ImGui::TextColored(stateColor, "Vehicle State: %s", stateNames[stateIdx]);

                    ImGui::Separator();

                    // State transition buttons
                    bool connected = ocpp_client->IsConnected();
                    if (sim.state == VehicleState::Unplugged || sim.state == VehicleState::PluggedIn) {
                        ImGui::InputText("IdTag", sim.idTag, sizeof(sim.idTag));
                        ImGui::SameLine();
                        if (ImGui::Button("Present Tag") && connected)
                            ocpp_client->SendAuthorize(sim.idTag);
                        ImGui::Spacing();
                    }
                    if (sim.state == VehicleState::Unplugged) {
                        if (ImGui::Button("Plug In") && connected) {
                            sim.state = VehicleState::PluggedIn;
                            sim.meterKwh = 0.0;
                            ocpp_client->SendStatusNotification(1, "Preparing");
                        }
                    }
                    if (sim.state == VehicleState::PluggedIn) {
                        if (ImGui::Button("Start Charging") && connected) {
                            sim.state = VehicleState::Charging;
                            sim.lastTickMs = 0;
                            ocpp_client->SendStartTransaction(1, sim.idTag, (int)(sim.meterKwh * 1000));
                            ocpp_client->SendStatusNotification(1, "Charging");
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Unplug") && connected) {
                            sim.state = VehicleState::Unplugged;
                            ocpp_client->SendStatusNotification(1, "Available");
                        }
                    }
                    if (sim.state == VehicleState::Charging) {
                        if (ImGui::Button("Stop Charging") && connected) {
                            sim.state = VehicleState::Finished;
                            ocpp_client->SendStopTransaction(sim.transactionId, (int)(sim.meterKwh * 1000), "Local");
                            ocpp_client->SendStatusNotification(1, "Finishing");
                        }
                    }
                    if (sim.state == VehicleState::Finished) {
                        if (ImGui::Button("Unplug") && connected) {
                            sim.state = VehicleState::Unplugged;
                            ocpp_client->SendStatusNotification(1, "Available");
                        }
                    }

                    ImGui::Separator();

                    // Meter data
                    ImGui::Text("Session Energy : %.4f kWh", sim.meterKwh);
                    ImGui::Text("Session Energy : %.1f Wh",  sim.meterKwh * 1000.0);

                    const OCPPClientInfo& info = ocpp_client->GetInfo();
                    if (!info.chargingSchedulePeriods.empty()) {
                        const ChargingSchedulePeriod& period = info.chargingSchedulePeriods[0];
                        ImGui::Text("Current Limit  : %.1f %s", period.limit, info.chargingRateUnit.c_str());
                    } else {
                        ImGui::TextDisabled("Current Limit  : (no profile set)");
                    }

                    float powerKw = (float)sim.powerKw;
                    if (ImGui::SliderFloat("Charge Power (kW)", &powerKw, 1.0f, 22.0f))
                        sim.powerKw = powerKw;

                    int sendIntervalSec = (int)(sim.meterSendIntervalMs / 1000);
                    if (ImGui::SliderInt("MeterValues Interval (s)", &sendIntervalSec, 5, 300))
                        sim.meterSendIntervalMs = (DWORD)(sendIntervalSec * 1000);

                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::Separator();           
            
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    if (ImGui::Button("Create New OCPP Client")){
        OCPPClient* new_ocpp_client = new OCPPClient();
        ocpp_clients.push_back(new_ocpp_client);
        vehicle_sims.push_back(VehicleSimulation());
    }
    ImGui::End();
}