#include "OCPPServerHandler.h"
#include "Debug.h"
#include <ctime>

static Debugger* ocpp_debug = new Debugger("OCPPServerHandler", DEBUG_TRACE);

using json = nlohmann::json;

namespace {
	std::string CurrentUtcTimestamp() {
		time_t now = time(nullptr);
		struct tm gm;
		gmtime_s(&gm, &now);
		char buf[64];
		strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
		return std::string(buf);
	}
}

OCPPServerHandler::OCPPServerHandler(SendMessageFn sendMessage, SetVariableFn setVariable)
	: m_sendMessage(sendMessage), m_setVariable(setVariable)
{
	InitializeCriticalSection(&m_lock);
}

OCPPServerHandler::~OCPPServerHandler()
{
	DeleteCriticalSection(&m_lock);
}

void OCPPServerHandler::RegisterClient(SOCKET clientSocket, const std::string& path, const std::string& protocol)
{
	EnterCriticalSection(&m_lock);
	clients.push_back(clientSocket);

	OCPPClientData& clientData = m_clientData[clientSocket];
	clientData.socket = clientSocket;
	clientData.path = path;
	clientData.protocol = protocol;
	clientData.connectTimestamp = CurrentUtcTimestamp();
	LeaveCriticalSection(&m_lock);

	ocpp_debug->Info("OCPP client connected (protocol=%s)\n", protocol.c_str());
}

void OCPPServerHandler::UnregisterClient(SOCKET clientSocket)
{
	EnterCriticalSection(&m_lock);
	for (size_t i = 0; i < clients.size(); ++i) {
		if (clients[i] == clientSocket) { clients.erase(clients.begin() + i); break; }
	}
	m_clientData.erase(clientSocket);
	LeaveCriticalSection(&m_lock);
}

OCPPClientData* OCPPServerHandler::GetClientData(SOCKET clientSocket)
{
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	OCPPClientData* result = (it != m_clientData.end()) ? &it->second : nullptr;
	LeaveCriticalSection(&m_lock);
	return result;
}

std::vector<OCPPTransaction> OCPPServerHandler::GetTransactionHistory(SOCKET clientSocket)
{
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	std::vector<OCPPTransaction> history;
	if (it != m_clientData.end()) {
		history = it->second.transactionHistory;
	}
	LeaveCriticalSection(&m_lock);
	return history;
}

// Very small/simple parser to detect OCPP CALL messages and dispatch to the right handler
bool OCPPServerHandler::HandleMessage(SOCKET clientSocket, const std::string &path, const std::string &msg)
{
	ocpp_debug->Ok("OCPP Message: %s\n", msg.c_str());
	auto j = json::parse(msg);
	if (!j.is_array() || j.size() < 3) return false;
	int msgType = j[0].get<int>();
	if (msgType != 2) return false; // not a CALL
	std::string msgId = j[1].is_string() ? j[1].get<std::string>() : j[1].dump();
	std::string action = j[2].get<std::string>();
	if (action == "BootNotification") {
		return HandleBootNotification(clientSocket, path, msgId, j);
	}
	if (action == "StatusNotification") {
		return HandleStatusNotification(clientSocket, path, msgId, j);
	}
	if (action == "Heartbeat") {
		return HandleHeartbeat(clientSocket, path, msgId, j);
	}
	if (action == "Authorize") {
		return HandleAuthorize(clientSocket, path, msgId, j);
	}
	if (action == "MeterValues") {
		return HandleMeterValues(clientSocket, path, msgId, j);
	}
	if (action == "StartTransaction") {
		return HandleStartTransaction(clientSocket, path, msgId, j);
	}
	if (action == "StopTransaction") {
		return HandleStopTransaction(clientSocket, path, msgId, j);
	}
	if (action == "DataTransfer") {
		return HandleDataTransfer(clientSocket, path, msgId, j);
	}

	ocpp_debug->Warn("Unhandled OCPP action: %s\n", action.c_str());
	return false;
}

//Message from Heliox/Fermata Energy
//[2,"4928f01b-6dcb-4549-b467-34f3061e9b26","BootNotification",{"chargePointVendor":"Heliox","chargePointModel":"FE20","chargeBoxSerialNumber":"620722003700_2241001115","chargePointSerialNumber":"243401022","firmwareVersion":"1.0.0","iccid":"","imsi":""}]
//Simulator
//[2,"b16dd18e-a33a-41dc-9f0f-e1a1c0b30279","BootNotification",{"chargeBoxSerialNumber":"123456","chargePointModel":"Model","chargePointSerialNumber":"123456","chargePointVendor":"Vendor","firmwareVersion":"1.0","iccid":"","imsi":"","meterSerialNumber":"123456","meterType":""}]
//Authorise
//[2,"f0922aea-da99-4c71-9084-98d43af98a64","Authorize",{"idTag":"6BABE9D2"}]
//
//From Fermata Heliox/Fermata Energy
//[2,"2bf90fe7-298e-488c-818f-2c109e3ac002","DataTransfer",{"vendorId":"nu.ame","messageId":"customMeterValues","data":"{\"local_mode\":false,\"p_baseline\":7000,\"q_baseline\":0,\"p_max\":18326,\"p_min\":-19998,\"min_soc\":0,\"max_soc\":100,\"ev_min_soc\":10,\"ev_energy_capacity\":33,\"session_active\":true,\"output_power\":6678}"}]
//[2,"d4187d9e-3fd3-4c49-84b5-bcb6b4053d44","MeterValues",{"connectorId":1,"transactionId":0,"meterValue":[{"timestamp":"2025-12-29T14:37:27.444Z","sampledValue":[{"value":"68","measurand":"SoC","unit":"Percent"},{"value":"6807.0","measurand":"Power.Active.Import","unit":"W"},{"value":"0","measurand":"Power.Active.Export","unit":"W"},{"value":"72.0","measurand":"Power.Reactive.Import","unit":"var"},{"value":"0","measurand":"Power.Reactive.Export","unit":"var"},{"value":"28.300001","measurand":"Temperature","unit":"Celsius"},{"value":"49.965004","measurand":"Frequency","unit":"W"}]}]}]

//From Eaton
//[2, "c0c9197c-62e4-465d-947b-552f5014294b", "DataTransfer", {"vendorId": "GreenMotion", "messageId": "PlugType", "data": "PlugType.Mode3_Type2_Socket"}]

bool OCPPServerHandler::HandleBootNotification(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	std::string identity;
	if (j.size() >= 4 && j[3].is_object()){
		if (j[3].contains("chargeBoxIdentity")) identity = j[3]["chargeBoxIdentity"].get<std::string>();
		else if (j[3].contains("chargePointIdentity")) identity = j[3]["chargePointIdentity"].get<std::string>();
	}

	std::string expectedId = path;
	if (!expectedId.empty() && expectedId[0] == '/') expectedId = expectedId.substr(1);

	bool accept = false;
	if (identity.empty() || identity == expectedId){
		accept = true;
	}

	accept = true;

	std::string ts = CurrentUtcTimestamp();

	// Update OCPP client data
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		OCPPClientData& clientData = it->second;
		const json& payload = j[3];
		if (payload.contains("chargePointVendor")) clientData.chargePointVendor = payload["chargePointVendor"].get<std::string>();
		if (payload.contains("chargePointModel")) clientData.chargePointModel = payload["chargePointModel"].get<std::string>();
		if (payload.contains("chargeBoxSerialNumber")) clientData.chargeBoxSerialNumber = payload["chargeBoxSerialNumber"].get<std::string>();
		if (payload.contains("chargePointSerialNumber")) clientData.chargePointSerialNumber = payload["chargePointSerialNumber"].get<std::string>();
		if (payload.contains("firmwareVersion")) clientData.firmwareVersion = payload["firmwareVersion"].get<std::string>();
		if (payload.contains("iccid")) clientData.iccid = payload["iccid"].get<std::string>();
		if (payload.contains("imsi")) clientData.imsi = payload["imsi"].get<std::string>();
		if (payload.contains("meterSerialNumber")) clientData.meterSerialNumber = payload["meterSerialNumber"].get<std::string>();
		if (payload.contains("meterType")) clientData.meterType = payload["meterType"].get<std::string>();
		clientData.chargeBoxIdentity = identity;
		clientData.bootAccepted = accept;
		clientData.bootTimestamp = ts;
	}
	LeaveCriticalSection(&m_lock);

	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["status"] = accept ? "Accepted" : "Rejected";
	result["interval"] = accept ? 15 : 0;
	result["currentTime"] = ts;
	resp.push_back(result);

	m_sendMessage(clientSocket, resp.dump());
	ocpp_debug->Ok("Sending Back: %s\n",resp.dump().c_str());
	m_setVariable(std::string("ocpp_last_boot_") + path, accept ? "Accepted" : "Rejected");
	ocpp_debug->Info("BootNotification %s for %s (id=%s)\n", accept ? "Accepted" : "Rejected", path.c_str(), identity.c_str());
	return true;
}

bool OCPPServerHandler::HandleStatusNotification(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		ocpp_debug->Warn("StatusNotification missing payload from %s\n", path.c_str());
		return false;
	}
	const json &p = j[3];
	std::string status;
	std::string errorCode;
	std::string timestamp;
	std::string vendorId;
	std::string vendorErrorCode;

	if (p.contains("status")) status = p["status"].get<std::string>();
	if (p.contains("errorCode")) errorCode = p["errorCode"].get<std::string>();
	if (p.contains("timestamp")) timestamp = p["timestamp"].get<std::string>();
	if (p.contains("vendorId")) vendorId = p["vendorId"].get<std::string>();
	if (p.contains("vendorErrorCode")) vendorErrorCode = p["vendorErrorCode"].get<std::string>();
	int connectorId = 0; // 0 = the charge point itself, per OCPP convention, when omitted
	if (p.contains("connectorId") && p["connectorId"].is_number()) {
		connectorId = p["connectorId"].get<int>();
	}

	// Update OCPP client data
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		OCPPConnectorState& conn = it->second.connectors[connectorId];
		if (!status.empty()) conn.status = status;
		if (!errorCode.empty()) conn.errorCode = errorCode;
		if (!timestamp.empty()) conn.statusTimestamp = timestamp;
		if (!vendorId.empty()) conn.vendorId = vendorId;
		if (!vendorErrorCode.empty()) conn.vendorErrorCode = vendorErrorCode;
	}
	LeaveCriticalSection(&m_lock);

	// Set variables for visibility via /status, scoped per connector
	std::string connBase = path + "/" + std::to_string(connectorId);
	std::string base = std::string("ocpp_status_") + connBase;
	if (!status.empty()) m_setVariable(base, status);
	if (!errorCode.empty()) m_setVariable(base + std::string("_error"), errorCode);
	if (!timestamp.empty()) m_setVariable(base + std::string("_ts"), timestamp);
	if (!vendorId.empty()) m_setVariable(base + std::string("_vendor"), vendorId);
	if (!vendorErrorCode.empty()) m_setVariable(base + std::string("_vendor_err"), vendorErrorCode);

	// Store full JSON payload for debugging
	m_setVariable(std::string("ocpp_last_status_") + connBase, p.dump());

	ocpp_debug->Info("Received OCPP StatusNotification from %s connector %d: status=%s error=%s\n", path.c_str(), connectorId, status.c_str(), errorCode.c_str());

	// Reply with CALLRESULT (empty object) per OCPP convention
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	resp.push_back(json::object());
	m_sendMessage(clientSocket, resp.dump());
	return true;
}

bool OCPPServerHandler::HandleHeartbeat(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	std::string ts = CurrentUtcTimestamp();

	// Update OCPP client data
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		it->second.lastHeartbeatTimestamp = ts;
	}
	LeaveCriticalSection(&m_lock);

	// Expose via /status and log
	m_setVariable(std::string("ocpp_last_heartbeat_") + path, ts);
	ocpp_debug->Info("Heartbeat from %s at %s\n", path.c_str(), ts.c_str());

	// Send CALLRESULT [3, msgId, { currentTime: ts }]
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["currentTime"] = ts;
	resp.push_back(result);
	m_sendMessage(clientSocket, resp.dump());

	return true;
}

bool OCPPServerHandler::HandleAuthorize(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Extract idTag from payload
	std::string idTag;
	if (j.size() >= 4 && j[3].is_object()){
		if (j[3].contains("idTag")) {
			idTag = j[3]["idTag"].get<std::string>();
		}
	}

	std::string ts = CurrentUtcTimestamp();

	// Update OCPP client data
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		it->second.lastAuthorizedIdTag = idTag;
		it->second.lastAuthorizeTimestamp = ts;
	}
	LeaveCriticalSection(&m_lock);

	// Expose via /status and log
	m_setVariable(std::string("ocpp_last_authorize_") + path, idTag);
	ocpp_debug->Info("Authorize request from %s for idTag: %s\n", path.c_str(), idTag.c_str());

	// Accept all ID tags - send CALLRESULT [3, msgId, { idTagInfo: { status: "Accepted" } }]
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	json idTagInfo;
	idTagInfo["status"] = "Accepted";
	result["idTagInfo"] = idTagInfo;
	resp.push_back(result);

	m_sendMessage(clientSocket, resp.dump());
	ocpp_debug->Ok("Authorize Accepted for idTag: %s\n", idTag.c_str());

	return true;
}

bool OCPPServerHandler::HandleMeterValues(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		ocpp_debug->Warn("MeterValues missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract meterValue array
	if (!payload.contains("meterValue") || !payload["meterValue"].is_array()) {
		ocpp_debug->Warn("MeterValues missing meterValue array from %s\n", path.c_str());
		return false;
	}

	int connectorId = 0; // 0 = the charge point itself, per OCPP convention, when omitted
	if (payload.contains("connectorId") && payload["connectorId"].is_number()) {
		connectorId = payload["connectorId"].get<int>();
	}

	double powerActiveImport = 0.0;
	double voltage = 0.0;
	double soc = 0.0;
	bool foundPower = false;
	bool foundVoltage = false;
	bool foundSoC = false;
	std::string timestamp;

	// Iterate through meterValue array (usually contains one entry with timestamp and sampledValues)
	const json &meterValueArray = payload["meterValue"];
	for (const auto &meterValue : meterValueArray) {
		if (!meterValue.is_object()) continue;

		// Extract timestamp if available
		if (meterValue.contains("timestamp") && timestamp.empty()) {
			timestamp = meterValue["timestamp"].get<std::string>();
		}

		// Extract sampledValue array
		if (!meterValue.contains("sampledValue") || !meterValue["sampledValue"].is_array()) continue;

		const json &sampledValueArray = meterValue["sampledValue"];
		for (const auto &sample : sampledValueArray) {
			if (!sample.is_object()) continue;

			std::string measurand;
			if (sample.contains("measurand")) {
				measurand = sample["measurand"].get<std::string>();
			}

			// Look for Power.Active.Import
			if (measurand == "Power.Active.Import" && sample.contains("value")) {
				if (sample["value"].is_number()) {
					powerActiveImport = sample["value"].get<double>();
				} else if (sample["value"].is_string()) {
					powerActiveImport = std::stod(sample["value"].get<std::string>());
				}
				foundPower = true;
			}
			// Look for Voltage
			if (measurand == "Voltage" && sample.contains("value")) {
				if (sample["value"].is_number()) {
					voltage = sample["value"].get<double>();
				} else if (sample["value"].is_string()) {
					voltage = std::stod(sample["value"].get<std::string>());
				}
				foundVoltage = true;
			}

			// Look for SoC
			if (measurand == "SoC" && sample.contains("value")) {
				if (sample["value"].is_number()) {
					soc = sample["value"].get<double>();
				} else if (sample["value"].is_string()) {
					soc = std::stod(sample["value"].get<std::string>());
				}
				foundSoC = true;
			}
		}
	}

	// Update OCPP client data
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		OCPPConnectorState& conn = it->second.connectors[connectorId];
		if (foundPower) conn.powerActiveImport = powerActiveImport;
		if (foundVoltage) conn.ACVoltage = voltage;
		if (foundSoC) conn.soc = soc;
		if (!timestamp.empty()) conn.meterValuesTimestamp = timestamp;
	}
	LeaveCriticalSection(&m_lock);

	// Log the values
	if (foundPower || foundSoC) {
		ocpp_debug->Info("MeterValues from %s connector %d: Power=%.1fW, Voltage:%.1fV SoC=%.1f%%\n",
			path.c_str(), connectorId, powerActiveImport, voltage, soc);
	}

	// Set variables for visibility via /status, scoped per connector
	std::string connBase = path + "/" + std::to_string(connectorId);
	if (foundPower) {
		m_setVariable(std::string("ocpp_power_") + connBase, std::to_string(powerActiveImport));
	}
	if (foundVoltage) {
		m_setVariable(std::string("ocpp_voltage_") + connBase, std::to_string(voltage));
	}
	if (foundSoC) {
		m_setVariable(std::string("ocpp_soc_") + connBase, std::to_string(soc));
	}

	// Reply with CALLRESULT (empty object) per OCPP convention
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	resp.push_back(json::object());
	m_sendMessage(clientSocket, resp.dump());

	return true;
}

bool OCPPServerHandler::HandleStartTransaction(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		ocpp_debug->Warn("StartTransaction missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract required fields from StartTransaction request
	// Required: connectorId, idTag, meterStart, timestamp
	int connectorId = 0; // 0 = the charge point itself, per OCPP convention, when omitted
	std::string idTag;
	int meterStart = 0;
	std::string timestamp;
	std::string reservationId;

	if (payload.contains("connectorId")) {
		if (payload["connectorId"].is_number()) {
			connectorId = payload["connectorId"].get<int>();
		}
	}

	if (payload.contains("idTag") && payload["idTag"].is_string()) {
		idTag = payload["idTag"].get<std::string>();
	}

	if (payload.contains("meterStart")) {
		if (payload["meterStart"].is_number()) {
			meterStart = payload["meterStart"].get<int>();
		}
	}

	if (payload.contains("timestamp") && payload["timestamp"].is_string()) {
		timestamp = payload["timestamp"].get<std::string>();
	}

	if (payload.contains("reservationId")) {
		if (payload["reservationId"].is_number()) {
			reservationId = std::to_string(payload["reservationId"].get<int>());
		} else if (payload["reservationId"].is_string()) {
			reservationId = payload["reservationId"].get<std::string>();
		}
	}

	// Generate a unique transaction ID (simple incrementing counter)
	static int nextTransactionId = 1;
	int transactionId = nextTransactionId++;

	std::string currentTime = CurrentUtcTimestamp();

	// Update OCPP client data
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		OCPPConnectorState& conn = it->second.connectors[connectorId];
		conn.transactionId = transactionId;
		conn.transactionIdTag = idTag;
		conn.transactionMeterStart = meterStart;
		conn.transactionTimestamp = timestamp;
		conn.transactionReservationId = reservationId;

		// Record in transaction history
		OCPPTransaction tx;
		tx.transactionId = transactionId;
		tx.connectorId   = connectorId;
		tx.idTag         = idTag;
		tx.meterStart    = meterStart;
		tx.startTimestamp = timestamp;
		tx.completed     = false;
		it->second.transactionHistory.push_back(tx);
	}
	LeaveCriticalSection(&m_lock);

	// Set variables for visibility via /status, scoped per connector
	std::string connBase = path + "/" + std::to_string(connectorId);
	m_setVariable(std::string("ocpp_transaction_id_") + connBase, std::to_string(transactionId));
	m_setVariable(std::string("ocpp_transaction_idtag_") + connBase, idTag);
	m_setVariable(std::string("ocpp_transaction_meter_start_") + connBase, std::to_string(meterStart));

	ocpp_debug->Info("StartTransaction from %s: connectorId=%d, idTag=%s, meterStart=%d, transactionId=%d\n",
		path.c_str(), connectorId, idTag.c_str(), meterStart, transactionId);

	// Send CALLRESULT [3, msgId, { transactionId: <id>, idTagInfo: { status: "Accepted" } }]
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["transactionId"] = transactionId;
	json idTagInfo;
	idTagInfo["status"] = "Accepted";
	result["idTagInfo"] = idTagInfo;
	resp.push_back(result);

	m_sendMessage(clientSocket, resp.dump());
	ocpp_debug->Ok("StartTransaction Accepted: transactionId=%d for idTag=%s\n", transactionId, idTag.c_str());

	return true;
}

bool OCPPServerHandler::HandleStopTransaction(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		ocpp_debug->Warn("StopTransaction missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract required fields from StopTransaction request
	// Required: transactionId, timestamp, meterStop
	// Optional: idTag, reason, transactionData
	int transactionId = -1;
	std::string timestamp;
	int meterStop = 0;
	std::string idTag;
	std::string reason;

	if (payload.contains("transactionId")) {
		if (payload["transactionId"].is_number()) {
			transactionId = payload["transactionId"].get<int>();
		}
	}

	if (payload.contains("timestamp") && payload["timestamp"].is_string()) {
		timestamp = payload["timestamp"].get<std::string>();
	}

	if (payload.contains("meterStop")) {
		if (payload["meterStop"].is_number()) {
			meterStop = payload["meterStop"].get<int>();
		}
	}

	if (payload.contains("idTag") && payload["idTag"].is_string()) {
		idTag = payload["idTag"].get<std::string>();
	}

	if (payload.contains("reason") && payload["reason"].is_string()) {
		reason = payload["reason"].get<std::string>();
	}

	// Update OCPP client data (clear transaction info). StopTransaction.req carries no
	// connectorId, so find whichever connector currently has this transaction active.
	int connectorId = 0;
	EnterCriticalSection(&m_lock);
	auto it = m_clientData.find(clientSocket);
	if (it != m_clientData.end()) {
		OCPPConnectorState* conn = nullptr;
		for (auto& kv : it->second.connectors) {
			if (kv.second.transactionId == transactionId) { conn = &kv.second; connectorId = kv.first; break; }
		}
		if (!conn && transactionId != -1) {
			ocpp_debug->Warn("StopTransaction for unknown/inactive transactionId=%d from %s\n", transactionId, path.c_str());
		}

		// Update the matching history entry with stop data
		for (OCPPTransaction &tx : it->second.transactionHistory) {
			if (tx.transactionId == transactionId && !tx.completed) {
				tx.meterStop      = meterStop;
				tx.stopTimestamp  = timestamp;
				tx.stopReason     = reason;
				tx.completed      = true;
				break;
			}
		}

		// Clear active transaction fields on the connector that had it
		if (conn) {
			conn->transactionId = -1;
			conn->transactionIdTag.clear();
			conn->transactionMeterStart = 0;
			conn->transactionTimestamp.clear();
			conn->transactionReservationId.clear();
		}
	}
	LeaveCriticalSection(&m_lock);

	// Set variables for visibility via /status, scoped per connector
	std::string connBase = path + "/" + std::to_string(connectorId);
	m_setVariable(std::string("ocpp_transaction_id_") + connBase, "-1");
	m_setVariable(std::string("ocpp_last_stop_meter_") + connBase, std::to_string(meterStop));
	m_setVariable(std::string("ocpp_last_stop_reason_") + connBase, reason.empty() ? "None" : reason);
	m_setVariable(std::string("ocpp_last_stop_timestamp_") + connBase, timestamp);

	ocpp_debug->Info("StopTransaction from %s: transactionId=%d, meterStop=%d, reason=%s\n",
		path.c_str(), transactionId, meterStop, reason.empty() ? "None" : reason.c_str());

	// Send CALLRESULT [3, msgId, { idTagInfo: { status: "Accepted" } }]
	// Note: idTagInfo is optional in StopTransaction response, but commonly included
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	if (!idTag.empty()) {
		json idTagInfo;
		idTagInfo["status"] = "Accepted";
		result["idTagInfo"] = idTagInfo;
	}
	resp.push_back(result);

	m_sendMessage(clientSocket, resp.dump());
	ocpp_debug->Ok("StopTransaction Accepted: transactionId=%d, meterStop=%d\n", transactionId, meterStop);

	return true;
}

bool OCPPServerHandler::HandleDataTransfer(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		ocpp_debug->Warn("DataTransfer missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract DataTransfer fields
	// Required: vendorId
	// Optional: messageId, data
	std::string vendorId;
	std::string messageId;
	std::string data;

	if (payload.contains("vendorId") && payload["vendorId"].is_string()) {
		vendorId = payload["vendorId"].get<std::string>();
	}

	if (payload.contains("messageId")) {
		if (payload["messageId"].is_string()) {
			messageId = payload["messageId"].get<std::string>();
		}
	}

	if (payload.contains("data")) {
		if (payload["data"].is_string()) {
			data = payload["data"].get<std::string>();
		} else {
			// If data is not a string, dump it as JSON
			data = payload["data"].dump();
		}
	}

	// Log the DataTransfer message
	ocpp_debug->Info("DataTransfer from %s: vendorId=%s, messageId=%s\n",
		path.c_str(), vendorId.c_str(), messageId.c_str());

	if (!data.empty()) {
		ocpp_debug->Trace("DataTransfer data: %s\n", data.c_str());
	}

	// Set variables for visibility via /status
	m_setVariable(std::string("ocpp_datatransfer_vendor_") + path, vendorId);
	m_setVariable(std::string("ocpp_datatransfer_msgid_") + path, messageId);
	m_setVariable(std::string("ocpp_datatransfer_data_") + path, data);

	// Parse custom data if it's JSON (like the customMeterValues example)
	if (!data.empty() && data[0] == '{') {
		json customData = json::parse(data);
		// Store interesting fields if they exist
		if (customData.contains("session_active")) {
			bool sessionActive = customData["session_active"].get<bool>();
			m_setVariable(std::string("ocpp_session_active_") + path, sessionActive ? "true" : "false");
		}
		if (customData.contains("output_power")) {
			int outputPower = customData["output_power"].get<int>();
			m_setVariable(std::string("ocpp_custom_power_") + path, std::to_string(outputPower));
		}
	}

	// Send CALLRESULT [3, msgId, { status: "Accepted" }]
	// Optional: can also return data back to the charge point
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["status"] = "Accepted";
	resp.push_back(result);

	m_sendMessage(clientSocket, resp.dump());
	ocpp_debug->Ok("DataTransfer Accepted from vendor=%s\n", vendorId.c_str());

	return true;
}

bool OCPPServerHandler::SendSetChargingProfile(SOCKET clientSocket, int connectorId, float currentLimit)
{
	// Generate a unique message ID for this request
	static int nextMsgId = 1000;
	std::string msgId = std::to_string(nextMsgId++);

	std::string currentTime = CurrentUtcTimestamp();

	// Create SetChargingProfile CALL message [2, msgId, "SetChargingProfile", payload]
	json call = json::array();
	call.push_back(2); // Message type: CALL
	call.push_back(msgId);
	call.push_back("SetChargingProfile");

	// Build the payload
	json payload;
	payload["connectorId"] = connectorId;

	// Create charging profile
	json chargingProfile;
	chargingProfile["chargingProfileId"] = nextMsgId;
	chargingProfile["stackLevel"] = 0;
	chargingProfile["chargingProfilePurpose"] = "TxProfile";
	chargingProfile["chargingProfileKind"] = "Absolute";

	// Create charging schedule
	json chargingSchedule;
	chargingSchedule["chargingRateUnit"] = "A"; // Amperes
	chargingSchedule["startSchedule"] = currentTime;

	// Create schedule period (single period with the current limit)
	json period;
	period["startPeriod"] = 0;
	period["limit"] = currentLimit;

	json periods = json::array();
	periods.push_back(period);
	chargingSchedule["chargingSchedulePeriod"] = periods;

	chargingProfile["chargingSchedule"] = chargingSchedule;
	payload["csChargingProfiles"] = chargingProfile;

	call.push_back(payload);

	// Send the message
	std::string message = call.dump();
	ocpp_debug->Info("Sending SetChargingProfile to connector %d: %.1fA\n", connectorId, currentLimit);
	ocpp_debug->Trace("SetChargingProfile message: %s\n", message.c_str());

	bool result = m_sendMessage(clientSocket, message);
	if (result) {
		ocpp_debug->Ok("SetChargingProfile sent successfully\n");
	} else {
		ocpp_debug->Err("Failed to send SetChargingProfile\n");
	}

	return result;
}
