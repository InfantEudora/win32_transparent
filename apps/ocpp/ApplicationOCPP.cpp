#include "ApplicationOCPP.h"
#include "Debug.h"
#include <cstdlib>

static Debugger *debug = new Debugger("ApplicationOCPP", DEBUG_ALL);

// Split "[ws://]host:port[/path]" into host and port.
//
// Deliberately avoids std::stoi: this build is -fno-exceptions, so std::stoi on
// a non-numeric port calls std::terminate() instead of throwing - a typo in the
// address box would take the whole application down. strtol reports failure
// through its arguments instead.
//
// Returns false with a human-readable reason in `error`.
static bool ParseHostPort(const std::string& input, std::string& host, int& port, std::string& error)
{
    std::string addr = input;

    // Trim - addresses get pasted, and a stray space is not a syntax error
    // worth refusing.
    size_t first = addr.find_first_not_of(" \t");
    if (first == std::string::npos){
        error = "address is empty";
        return false;
    }
    size_t last = addr.find_last_not_of(" \t");
    addr = addr.substr(first, last - first + 1);

    // Strip protocol prefix (ws://, wss://, http://, https://)
    size_t protocol_end = addr.find("://");
    if (protocol_end != std::string::npos)
        addr = addr.substr(protocol_end + 3);

    // Drop any path. "ws://host:9000/OCPP/Tester" is a natural thing to paste
    // from a backend's docs, and the path belongs in the OCPP ID field - without
    // this the path would end up inside the port and fail to parse.
    size_t slash = addr.find('/');
    if (slash != std::string::npos)
        addr = addr.substr(0, slash);

    size_t colon = addr.rfind(':');
    if (colon == std::string::npos){
        error = "no port given - use host:port";
        return false;
    }

    host = addr.substr(0, colon);
    std::string port_str = addr.substr(colon + 1);

    if (host.empty()){
        error = "no host given - use host:port";
        return false;
    }
    if (port_str.empty()){
        error = "no port given - use host:port";
        return false;
    }

    const char* begin = port_str.c_str();
    char* end = nullptr;
    long parsed = strtol(begin, &end, 10);
    if (end == begin || *end != 0){
        error = "port \"" + port_str + "\" is not a number";
        return false;
    }
    if (parsed < 1 || parsed > 65535){
        error = "port " + port_str + " is out of range (1-65535)";
        return false;
    }

    port = (int)parsed;
    error.clear();
    return true;
}

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
    main_scene->UpdatePhysics(GetPhysicsTimestep());

    //Create an HTTP server to listen for connections
    http_server = new HTTPServer(9090);
    if (!http_server->Start()){
        debug->Fatal("Failed to start HTTP server\n");
    }

    debug->Info("HTTP Server started on port 9090\n"); 

}

/*
    This app has no simulation, so it has no RunSimulationTick - everything below is websocket
    traffic and the rate limits around it, paced by GetTickCount() against real charger backends.
    None of it is the engine's model: pausing the physics must not stop a charging session or stall
    a handshake, and single-stepping must not send a meter value. So it all lives on the every-pass
    hook, wall-clock timing and all - the one case in this codebase where a duration in real
    milliseconds is the correct unit rather than a tick count, because the thing being timed is
    outside the simulation entirely.
*/
void ApplicationOCPP::UpdateView(){
    // Check if any OCPP clients need charging profile updates
    if (http_server) {
        DWORD now_ms = GetTickCount();
        for (SOCKET client_socket : http_server->ocpp.clients) {
            OCPPClientData* data = http_server->ocpp.GetClientData(client_socket);
            if (!data) continue;
            for (auto& kv : data->connectors) {
                int connectorId = kv.first;
                OCPPConnectorState& conn = kv.second;
                if (!conn.server_current_timit_updatereq) continue;
                // Rate limit: only send updates once per second (1000ms)
                DWORD time_since_last_update_ms = now_ms - conn.last_profile_update_time_ms;
                if (time_since_last_update_ms >= 1000) {
                    // Send SetChargingProfile request
                    http_server->ocpp.SendSetChargingProfile(client_socket, connectorId, conn.server_current_limit);

                    // Update the last update time
                    conn.last_profile_update_time_ms = now_ms;

                    // Clear the update flag
                    conn.server_current_timit_updatereq = false;
                }
            }
        }
    }

    // Tick vehicle simulations
    DWORD now_ms = GetTickCount();
    for (int i = 0; i < (int)ocpp_clients.size(); i++) {
        OCPPClient* client = ocpp_clients[i];
        VehicleSimulation& sim = vehicle_sims[i];

        // A backend can accept the connection and then never answer the
        // upgrade; this trips the same failure state a rejection does, so the
        // teardown below handles both.
        client->CheckHandshakeTimeout();

        // A rejected upgrade never recovers, so drop the socket rather than
        // leaving it open forever. This has to happen here and not in the
        // receive thread's handler: Disconnect() joins that thread. The error
        // text survives the disconnect, so the UI still shows what went wrong.
        if (client->HasHandshakeFailed() && client->IsConnected()) {
            debug->Err("Handshake rejected (%s), disconnecting\n", client->GetHandshakeError().c_str());
            client->Disconnect();
        }

        // The Fermata charger runs its own power-tracking loop and pushes
        // telemetry regardless of the transaction state machine above, so this
        // block sits before the Charging check.
        if (client->GetInfo().chargerType == ChargerType::FermataV2G) {
            if (sim.lastV2GTickMs != 0) {
                DWORD v2g_elapsed_ms = now_ms - sim.lastV2GTickMs;
                client->TickV2G(v2g_elapsed_ms / 1000.0);
            }
            sim.lastV2GTickMs = now_ms;

            // IsWebSocketReady(), not IsConnected(): the latter is true as soon
            // as TCP connects, and with lastDataTransferMs starting at 0 the
            // interval test passes immediately, so this fired a masked frame
            // before the handshake had even been sent.
            if (sim.autoSendDataTransfer && client->IsWebSocketReady() &&
                now_ms - sim.lastDataTransferMs >= sim.dataTransferIntervalMs) {
                client->SendCustomMeterValues();
                sim.lastDataTransferMs = now_ms;
            }
        }

        if (sim.state != VehicleState::Charging || !client->IsWebSocketReady())
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
            if (client->GetInfo().chargerType == ChargerType::FermataV2G)
                client->SendV2GMeterValues(1, client->GetInfo().m_meterdata.soc);
            else
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

    ImGui::Text("Connected OCPP Clients: %d", (int)http_server->ocpp.clients.size());
    int client_idx = 0;
    for (SOCKET client_socket : http_server->ocpp.clients){
        ImGui::PushID(client_idx);
        ImGui::Text("Client %d - Socket %llu", client_idx, (unsigned long long)client_socket);
        client_idx++;

        OCPPClientData* data = http_server->ocpp.GetClientData(client_socket);
        if (data) {
            ImGui::Text("Chargebox Path %s", data->path.c_str());

            // Access any OCPP data for this client
            std::string vendor = data->chargePointVendor;
            std::string lastTag = data->lastAuthorizedIdTag;
            // etc.

            if (data->connectors.empty()) {
                ImGui::TextDisabled("No connector data yet");
            }
            for (auto& kv : data->connectors) {
                int connectorId = kv.first;
                OCPPConnectorState& conn = kv.second;
                ImGui::PushID(connectorId);
                ImGui::Separator();
                ImGui::Text("Connector %d", connectorId);
                ImGui::Text("Status     : %s", conn.status.c_str());
                ImGui::Text("SOC        : %.1f%% ", conn.soc);
                ImGui::Text("AC Voltage : %.1f V", conn.ACVoltage);
                ImGui::Text("Power      : %.1f Watt", conn.powerActiveImport);
                ImGui::Text("Time       : %s", conn.meterValuesTimestamp.c_str());
                if (!conn.info.empty()) ImGui::TextColored(ImVec4(1,0.6f,0,1), "Info: %s", conn.info.c_str());

                if (ImGui::SliderFloat("Set Current Limit for Session", &conn.server_current_limit, 5, 32)){
                    conn.server_current_timit_updatereq = true;
                }

                if (conn.transactionId != -1) {
                    ImGui::Text("Active Transaction: %d (idTag: %s)", conn.transactionId, conn.transactionIdTag.c_str());
                }

                char idTagBuf[64];
                strncpy_s(idTagBuf, conn.remote_start_id_tag.c_str(), sizeof(idTagBuf) - 1);
                ImGui::SetNextItemWidth(150);
                if (ImGui::InputText("##RemoteStartIdTag", idTagBuf, sizeof(idTagBuf))){
                    conn.remote_start_id_tag = idTagBuf;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remote Start")){
                    http_server->ocpp.SendRemoteStartTransaction(client_socket, connectorId, conn.remote_start_id_tag);
                }
                ImGui::SameLine();
                if (ImGui::Button("Remote Stop")){
                    http_server->ocpp.SendRemoteStopTransaction(client_socket, conn.transactionId);
                }

                // ChangeAvailability is genuinely per connector, so unlike the
                // Reset control below the list these belong inside the loop.
                // The connector 0 row addresses the whole charge point.
                if (ImGui::Button("Operative")){
                    http_server->ocpp.SendChangeAvailability(client_socket, connectorId, "Operative");
                }
                ImGui::SameLine();
                if (ImGui::Button("Inoperative")){
                    http_server->ocpp.SendChangeAvailability(client_socket, connectorId, "Inoperative");
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()){
                    ImGui::SetTooltip(
                        connectorId == 0
                            ? "ChangeAvailability for connectorId 0 = the whole charge point.\n"
                              "Operative is the cheap thing to try on a Faulted unit before\n"
                              "resorting to a Hard Reset. Reply may be Accepted, Rejected or\n"
                              "Scheduled (applies after the running transaction ends) - watch\n"
                              "the log for the CALLRESULT."
                            : "ChangeAvailability for this connector only.\n"
                              "Reply may be Accepted, Rejected or Scheduled (applies after\n"
                              "the running transaction ends) - watch the log for the CALLRESULT.");
                }

                ImGui::SetNextItemWidth(120);
                ImGui::InputInt("PBaseline (W)", &conn.pbaseline_watts);
                ImGui::SameLine();
                if (ImGui::Button("Set Power")){
                    http_server->ocpp.SendChangeConfiguration(client_socket, "PBaseline", std::to_string(conn.pbaseline_watts));
                }

                if (data->hasV2GTelemetry) {
                    ImGui::TextDisabled("Charger Telemetry (DataTransfer)");
                    ImGui::Text("Vehicle SOC        : %.1f%%", conn.soc);
                    ImGui::Text("Applied PBaseline  : %.0f W", data->v2gPBaselineWatts);
                    ImGui::Text("Actual Output      : %.0f W", data->v2gOutputPowerWatts);
                    ImGui::Text("Session Active     : %s", data->v2gSessionActive ? "true" : "false");
                    ImGui::Text("Power Range        : %.0f W .. %.0f W", data->v2gPMinWatts, data->v2gPMaxWatts);
                    ImGui::Text("SOC Range          : %.0f%% .. %.0f%% (EV min %.0f%%, capacity %.1f kWh)",
                        data->v2gMinSoc, data->v2gMaxSoc, data->v2gEvMinSoc, data->v2gEvEnergyCapacityKwh);
                }
                ImGui::PopID();
            }

            // Reset is chargepoint-wide (Reset.req carries no connectorId), so it
            // sits below the connector list rather than inside it - this charger
            // reports on both connector 0 and 1, and a per-connector placement
            // would draw two buttons for one physical reset.
            ImGui::Separator();
            bool anyFaulted = false;
            for (auto& kv : data->connectors) {
                if (kv.second.status == "Faulted") { anyFaulted = true; break; }
            }
            if (anyFaulted) {
                ImGui::TextColored(ImVec4(1,0.4f,0.4f,1), "Chargepoint reports Faulted");
                ImGui::SameLine();
            }

            const char* resetTypes[] = { "Soft", "Hard" };
            ImGui::SetNextItemWidth(80);
            ImGui::Combo("##ResetType", &data->resetTypeIdx, resetTypes, IM_ARRAYSIZE(resetTypes));
            ImGui::SameLine();
            bool hardReset = (data->resetTypeIdx == 1);
            // Hard reboots the physical unit, so colour it as the dangerous one.
            if (hardReset) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f,0.15f,0.15f,1.0f));
            if (ImGui::Button(hardReset ? "Hard Reset" : "Soft Reset")){
                http_server->ocpp.SendReset(client_socket, resetTypes[data->resetTypeIdx]);
            }
            if (hardReset) ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()){
                ImGui::SetTooltip(
                    "Soft: restart the charger's OCPP application.\n"
                    "Hard: full reboot of the unit.\n"
                    "Either way the charger drops the connection and comes back\n"
                    "with a fresh BootNotification - watch the log for that.");
            }

            // Transaction history table for this chargepoint (spans all connectors)
            std::vector<OCPPTransaction> history = http_server->ocpp.GetTransactionHistory(client_socket);
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

    // Single tcp_client, so a function-local static is enough to hold the
    // last error for display.
    static std::string tcp_connect_error;

    if (!tcp_client->IsConnected()){
        if (ImGui::Button("Connect")){
            std::string host, parseError;
            int port = 0;
            if (!ParseHostPort(server_address, host, port, parseError)){
                tcp_connect_error = parseError;
                debug->Err("Invalid server address \"%s\": %s\n", server_address, parseError.c_str());
            } else {
                tcp_connect_error.clear();
                debug->Info("Connecting to %s:%d\n", host.c_str(), port);

                if (tcp_client->Connect(host, port)){
                    debug->Info("Connecting to server\n");
                }else{
                    tcp_connect_error = "could not open a TCP connection to " + host + ":" + std::to_string(port);
                    debug->Err("Failed to connect to server\n");
                }
            }
        }
        if (!tcp_connect_error.empty())
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Cannot connect: %s", tcp_connect_error.c_str());
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
                        //sprintf_s(server_address,"ws://10.239.1.42:8081");
                        sprintf_s(server_address,"ws://192.168.140.10:9000");
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

                    // Which charge point this client pretends to be. Picking a
                    // type rewrites the BootNotification identity to match the
                    // real hardware, and gates the Fermata V2G tab below.
                    const char* chargerTypes[] = {
                        "Generic AC charger",
                        "Fermata / Heliox FE20 (V2G)"
                    };
                    int chargerTypeIdx = (int)ocpp_client->GetInfo().chargerType;
                    if (ImGui::Combo("Charger Type", &chargerTypeIdx, chargerTypes, IM_ARRAYSIZE(chargerTypes))){
                        ocpp_client->SetChargerType((ChargerType)chargerTypeIdx);
                    }
                    ImGui::TextDisabled("Boots as: %s / %s (fw %s)",
                        ocpp_client->GetInfo().chargePointVendor.c_str(),
                        ocpp_client->GetInfo().chargePointModel.c_str(),
                        ocpp_client->GetInfo().firmwareVersion.c_str());
                    // A malformed address or a refused connection never reaches
                    // the handshake, so it reports separately from the block below.
                    if (!ocpp_client->GetInfo().connectError.empty()){
                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Cannot connect: %s",
                            ocpp_client->GetInfo().connectError.c_str());
                    }

                    // The last attempt's failure stays visible after the socket
                    // is gone, so a mistyped address explains itself instead of
                    // just looking disconnected. Cleared by the next connect.
                    if (!ocpp_client->IsConnected() && ocpp_client->HasHandshakeFailed()){
                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Last attempt rejected: %s",
                            ocpp_client->GetHandshakeError().c_str());
                        const std::string& body = ocpp_client->GetInfo().handshakeBody;
                        if (!body.empty())
                            ImGui::TextWrapped("Server said: %s", body.c_str());
                        if (ocpp_client->GetInfo().handshakeStatusCode == 404)
                            ImGui::TextDisabled("404 usually means the OCPP ID path is wrong for this backend.");
                    }

                    // Some backends are slow to upgrade; 0 waits forever.
                    int timeoutSec = (int)(ocpp_client->GetInfo().handshakeTimeoutMs / 1000);
                    ImGui::SetNextItemWidth(100);
                    if (ImGui::InputInt("Handshake timeout (s, 0 = none)", &timeoutSec)){
                        if (timeoutSec < 0) timeoutSec = 0;
                        ocpp_client->GetInfo().handshakeTimeoutMs = (DWORD)(timeoutSec * 1000);
                    }

                    if (!ocpp_client->IsConnected()){
                        if (ImGui::Button("Connect")){
                            std::string host, parseError;
                            int port = 0;
                            if (!ParseHostPort(server_address, host, port, parseError)){
                                ocpp_client->GetInfo().connectError = parseError;
                                debug->Err("Invalid server address \"%s\": %s\n", server_address, parseError.c_str());
                            } else {
                                ocpp_client->GetInfo().connectError.clear();
                                debug->Info("Connecting to %s:%d\n", host.c_str(), port);

                                if (ocpp_client->ConnectOCPP(host, port, std::string(ocpp_id))){
                                    debug->Info("Connecting to OCPP server\n");
                                } else {
                                    ocpp_client->GetInfo().connectError =
                                        "could not open a TCP connection to " + host + ":" + std::to_string(port);
                                    debug->Err("Failed to connect to OCPP server\n");
                                }
                            }
                        }
                    }else if (ocpp_client->IsConnected()){   
                        // Three distinct states, not two: green is usable, red
                        // is a definitive rejection (the server answered the
                        // upgrade with something other than 101), yellow is
                        // genuinely still waiting.
                        if (ocpp_client->IsWebSocketReady())
                            ImGui::TextColored(ImVec4(0,1,0,1),"Connected (ocpp1.6)");
                        else if (ocpp_client->HasHandshakeFailed())
                            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1),"Handshake rejected: %s",
                                ocpp_client->GetHandshakeError().c_str());
                        else {
                            // Count up so a stalled backend visibly approaches
                            // the deadline instead of just sitting there.
                            DWORD waitedMs = GetTickCount() - ocpp_client->GetInfo().handshakeSentTickMs;
                            ImGui::TextColored(ImVec4(1,1,0,1),"TCP up, awaiting handshake... (%.1fs / %.0fs)",
                                waitedMs / 1000.0f,
                                ocpp_client->GetInfo().handshakeTimeoutMs / 1000.0f);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Disconnect")){
                            ocpp_client->Disconnect();
                        }
                    }
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("OCPP Messages")){
                    if (ocpp_client->IsWebSocketReady()){
                        // ---- Charge-point-scoped messages ----
                        // These three carry no connectorId: BootNotification and
                        // Heartbeat are about the unit, and Authorize identifies a
                        // token rather than a socket. Everything else this client
                        // sends is per connector and lives in the groups below.
                        ImGui::TextDisabled("Charge point");
                        if (ImGui::Button("Send Boot Notification")){
                            // Vendor/model come from the selected charger type
                            // so the server sees the real hardware's identity.
                            ocpp_client->SendBootNotification(
                                ocpp_client->GetInfo().chargePointVendor,
                                ocpp_client->GetInfo().chargePointModel);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Send Heartbeat")){
                            ocpp_client->SendHeartbeat();
                        }

                        static char authorizeIdTag[64] = "TagNoUnderscore";
                        ImGui::SetNextItemWidth(200);
                        ImGui::InputText("IdTag", authorizeIdTag, sizeof(authorizeIdTag));
                        ImGui::SameLine();
                        if (ImGui::Button("Send Authorize")){
                            ocpp_client->SendAuthorize(authorizeIdTag);
                        }

                        // Per-connector status. connectorId 0 is the charge point
                        // itself, 1..N the physical sockets; a new client starts
                        // with 0 and 1, both Available.
                        static const char* cpStatuses[] = {
                            "Available", "Preparing", "Charging", "SuspendedEVSE",
                            "SuspendedEV", "Finishing", "Reserved", "Unavailable", "Faulted"
                        };
                        static const char* cpErrorCodes[] = {
                            "NoError", "ConnectorLockFailure", "EVCommunicationError",
                            "GroundFailure", "HighTemperature", "InternalError",
                            "LocalListConflict", "OtherError", "OverCurrentFailure",
                            "OverVoltage", "PowerMeterFailure", "PowerSwitchFailure",
                            "ReaderFailure", "ResetFailure", "UnderVoltage", "WeakSignal"
                        };

                        ImGui::Separator();
                        ImGui::TextDisabled("Connectors");
                        for (auto& ckv : ocpp_client->GetInfo().connectors){
                            int cid = ckv.first;
                            ClientConnectorState& cstate = ckv.second;
                            ImGui::PushID(cid);

                            // Derive the combo index from the stored string, so the
                            // widget and the state cannot drift apart.
                            int statusIdx = 0;
                            for (int s = 0; s < IM_ARRAYSIZE(cpStatuses); s++)
                                if (cstate.status == cpStatuses[s]) { statusIdx = s; break; }
                            int errIdx = 0;
                            for (int e = 0; e < IM_ARRAYSIZE(cpErrorCodes); e++)
                                if (cstate.errorCode == cpErrorCodes[e]) { errIdx = e; break; }

                            char header[64];
                            if (cid == 0)
                                sprintf_s(header, "Connector 0 (charge point) - %s", cstate.status.c_str());
                            else
                                sprintf_s(header, "Connector %d - %s", cid, cstate.status.c_str());

                            if (ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)){
                                // StatusNotification
                                ImGui::SetNextItemWidth(130);
                                if (ImGui::Combo("##status", &statusIdx, cpStatuses, IM_ARRAYSIZE(cpStatuses)))
                                    cstate.status = cpStatuses[statusIdx];
                                ImGui::SameLine();
                                ImGui::SetNextItemWidth(160);
                                if (ImGui::Combo("##errcode", &errIdx, cpErrorCodes, IM_ARRAYSIZE(cpErrorCodes)))
                                    cstate.errorCode = cpErrorCodes[errIdx];
                                ImGui::SameLine();
                                if (ImGui::Button("Send Status"))
                                    ocpp_client->SendConnectorStatus(cid);

                                // Free-text info - the real charger carries its raw
                                // "Stop reason: 0x.., Error: 0x.." codes here.
                                char infoBuf[128];
                                strncpy_s(infoBuf, cstate.info.c_str(), sizeof(infoBuf) - 1);
                                ImGui::SetNextItemWidth(300);
                                if (ImGui::InputTextWithHint("##info", "info (optional)", infoBuf, sizeof(infoBuf)))
                                    cstate.info = infoBuf;

                                // Connector 0 is the charge point itself, not a plug,
                                // so it gets no meter or transaction controls.
                                if (cid != 0){
                                    ImGui::Spacing();

                                    // MeterValues. SoC is a sampledValue in here, not
                                    // a message of its own - same as the real charger.
                                    float powerW = (float)cstate.meterPowerWatts;
                                    float socPct = (float)cstate.soc;
                                    ImGui::SetNextItemWidth(110);
                                    if (ImGui::InputFloat("Power (W)", &powerW, 0, 0, "%.0f"))
                                        cstate.meterPowerWatts = powerW;
                                    ImGui::SameLine();
                                    ImGui::SetNextItemWidth(110);
                                    if (ImGui::SliderFloat("SoC (%)", &socPct, 0.0f, 100.0f, "%.1f"))
                                        cstate.soc = socPct;
                                    ImGui::SameLine();
                                    if (ImGui::Button("Send MeterValues")){
                                        if (ocpp_client->GetInfo().chargerType == ChargerType::FermataV2G)
                                            ocpp_client->SendV2GMeterValues(cid, cstate.soc);
                                        else
                                            ocpp_client->SendMeterValues(cid, cstate.meterPowerWatts, cstate.soc);
                                    }

                                    ImGui::Spacing();

                                    // StartTransaction / StopTransaction. StopTransaction
                                    // keys off transactionId rather than connectorId, but
                                    // the transaction belongs to this plug, so it lives here.
                                    char tagBuf[64];
                                    strncpy_s(tagBuf, cstate.idTag.c_str(), sizeof(tagBuf) - 1);
                                    ImGui::SetNextItemWidth(140);
                                    if (ImGui::InputText("IdTag##tx", tagBuf, sizeof(tagBuf)))
                                        cstate.idTag = tagBuf;
                                    ImGui::SameLine();
                                    ImGui::SetNextItemWidth(90);
                                    ImGui::InputInt("Meter (Wh)", &cstate.meterWh, 0, 0);
                                    ImGui::SameLine();
                                    if (ImGui::Button("Start Transaction"))
                                        ocpp_client->SendStartTransaction(cid, cstate.idTag, cstate.meterWh);

                                    static const char* stopReasons[] = {
                                        "Local", "Remote", "EVDisconnected", "EmergencyStop",
                                        "HardReset", "SoftReset", "PowerLoss", "Reboot",
                                        "UnlockCommand", "DeAuthorized", "Other"
                                    };
                                    int reasonIdx = 0;
                                    for (int r = 0; r < IM_ARRAYSIZE(stopReasons); r++)
                                        if (cstate.stopReason == stopReasons[r]) { reasonIdx = r; break; }

                                    ImGui::SetNextItemWidth(140);
                                    ImGui::InputInt("TxId", &cstate.transactionId, 0, 0);
                                    ImGui::SameLine();
                                    ImGui::SetNextItemWidth(90);
                                    if (ImGui::Combo("##reason", &reasonIdx, stopReasons, IM_ARRAYSIZE(stopReasons)))
                                        cstate.stopReason = stopReasons[reasonIdx];
                                    ImGui::SameLine();
                                    if (ImGui::Button("Stop Transaction"))
                                        ocpp_client->SendStopTransaction(cstate.transactionId, cstate.meterWh, cstate.stopReason);
                                }
                            }

                            ImGui::PopID();
                        }
                        if (ImGui::Button("Send All Connector Status"))
                            ocpp_client->SendAllConnectorStatus();
                        ImGui::SameLine();
                        if (ImGui::Button("Add Connector")){
                            // Next free id after the highest existing one.
                            int nextId = ocpp_client->GetInfo().connectors.empty()
                                ? 1 : ocpp_client->GetInfo().connectors.rbegin()->first + 1;
                            ocpp_client->GetInfo().connectors[nextId] = ClientConnectorState();
                        }
                    }else if (ocpp_client->HasHandshakeFailed()){
                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Handshake rejected: %s",
                            ocpp_client->GetHandshakeError().c_str());
                    }else if (ocpp_client->IsConnected()){
                        ImGui::TextColored(ImVec4(1,1,0,1), "TCP connected, WebSocket handshake not complete");
                    }else{
                        ImGui::Text("Client is not connected\n");
                    }
                    ImGui::EndTabItem();
                }
                if (ocpp_client->GetInfo().chargerType == ChargerType::FermataV2G &&
                    ImGui::BeginTabItem("Fermata V2G")){
                    V2GState& v = ocpp_client->GetInfo().v2g;
                    VehicleSimulation& sim = vehicle_sims[index];

                    ImGui::TextDisabled("customMeterValues DataTransfer (vendorId \"nu.ame\")");
                    ImGui::Separator();

                    ImGui::Checkbox("session_active", &v.session_active);
                    ImGui::SameLine();
                    ImGui::Checkbox("local_mode", &v.local_mode);

                    // Signed: positive charges the vehicle, negative discharges
                    // it back to the grid. Range matches the vendor tool's slider.
                    float pBaseline = (float)v.p_baseline;
                    if (ImGui::SliderFloat("p_baseline (W)", &pBaseline, -20000.0f, 20000.0f, "%.0f"))
                        v.p_baseline = pBaseline;
                    ImGui::SameLine();
                    if (ImGui::Button("Zero")) v.p_baseline = 0.0;

                    float qBaseline = (float)v.q_baseline;
                    if (ImGui::SliderFloat("q_baseline (var)", &qBaseline, -10000.0f, 10000.0f, "%.0f"))
                        v.q_baseline = qBaseline;

                    // output_power is driven by TickV2G, not editable here.
                    ImGui::Separator();
                    ImVec4 powerColor = v.output_power > 0 ? ImVec4(0,1,0,1)
                                      : v.output_power < 0 ? ImVec4(1,0.6f,0,1)
                                                           : ImVec4(1,1,1,1);
                    ImGui::TextColored(powerColor, "output_power : %.0f W  (%s)",
                        v.output_power,
                        v.output_power > 0 ? "importing" : v.output_power < 0 ? "exporting (V2G)" : "idle");
                    ImGui::Separator();

                    if (ImGui::TreeNode("Envelope / SOC limits")){
                        float pMax = (float)v.p_max, pMin = (float)v.p_min, pLimit = (float)v.power_limit;
                        if (ImGui::InputFloat("p_max (W)", &pMax, 0, 0, "%.0f")) v.p_max = pMax;
                        if (ImGui::InputFloat("p_min (W)", &pMin, 0, 0, "%.0f")) v.p_min = pMin;
                        if (ImGui::InputFloat("PowerLimit (W)", &pLimit, 0, 0, "%.0f")) v.power_limit = pLimit;

                        float minSoc = (float)v.min_soc, maxSoc = (float)v.max_soc;
                        float evMinSoc = (float)v.ev_min_soc, evCap = (float)v.ev_energy_capacity;
                        if (ImGui::SliderFloat("min_soc (%)", &minSoc, 0.0f, 100.0f, "%.0f")) v.min_soc = minSoc;
                        if (ImGui::SliderFloat("max_soc (%)", &maxSoc, 0.0f, 100.0f, "%.0f")) v.max_soc = maxSoc;
                        if (ImGui::SliderFloat("ev_min_soc (%)", &evMinSoc, 0.0f, 100.0f, "%.0f")) v.ev_min_soc = evMinSoc;
                        if (ImGui::InputFloat("ev_energy_capacity (kWh)", &evCap, 0, 0, "%.0f")) v.ev_energy_capacity = evCap;
                        ImGui::TreePop();
                    }

                    ImGui::Separator();
                    ImGui::Checkbox("Auto-send DataTransfer", &sim.autoSendDataTransfer);
                    int dtIntervalSec = (int)(sim.dataTransferIntervalMs / 1000);
                    if (ImGui::SliderInt("DataTransfer Interval (s)", &dtIntervalSec, 1, 60))
                        sim.dataTransferIntervalMs = (DWORD)(dtIntervalSec * 1000);
                    if (ImGui::Button("Send DataTransfer Now") && ocpp_client->IsWebSocketReady())
                        ocpp_client->SendCustomMeterValues();

                    // Exactly what goes on the wire, so it can be compared
                    // against a capture from the real charger.
                    ImGui::Separator();
                    ImGui::TextDisabled("Wire payload (data field):");
                    std::string preview = ocpp_client->BuildCustomMeterValuesData();
                    ImGui::TextWrapped("%s", preview.c_str());

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
                    bool connected = ocpp_client->IsWebSocketReady();
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