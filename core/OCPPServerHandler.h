#pragma once

#include <winsock2.h>
#include <windows.h>
#include <string>
#include <map>
#include <vector>
#include <functional>
#include "tinygltf/json.hpp"
#include "OCPPClient.h" // for OCPPTransaction

// Per-connector state within a charge point. OCPP connectorId 0 refers to the charge
// point as a whole; 1..N refer to its physical connectors/sockets. A charge point with
// multiple connectors reports status/meter values/transactions independently per connector,
// so this is keyed separately rather than flattened onto the charge point.
struct OCPPConnectorState {
	std::string status = "Unknown";
	std::string errorCode;
	std::string statusTimestamp;
	std::string vendorId;
	std::string vendorErrorCode;

	// Meter values data
	double powerActiveImport = 0.0;  // in Watts
	double ACVoltage = 0.0;  // In volts
	double soc = 0.0;  // State of Charge in Percent
	std::string meterValuesTimestamp;

	// Active transaction data
	int transactionId = -1;
	std::string transactionIdTag;
	int transactionMeterStart = 0;
	std::string transactionTimestamp;
	std::string transactionReservationId;

	// Server-side charging profile control
	float server_current_limit = 32.0f;
	bool server_current_timit_updatereq = false;
	DWORD last_profile_update_time_ms = 0;
};

// Per-connection state for a charge point speaking OCPP over the HTTP server's websocket transport.
struct OCPPClientData {
	SOCKET socket;
	std::string path;
	std::string protocol;  // e.g., "ocpp1.6" or "ocpp2.0"

	// Boot notification data
	std::string chargePointVendor;
	std::string chargePointModel;
	std::string chargeBoxSerialNumber;
	std::string chargePointSerialNumber;
	std::string firmwareVersion;
	std::string iccid;
	std::string imsi;
	std::string meterSerialNumber;
	std::string meterType;
	std::string chargeBoxIdentity;
	bool bootAccepted;
	std::string bootTimestamp;

	// Authorization data (chargepoint-level; OCPP Authorize.req carries no connectorId)
	std::string lastAuthorizedIdTag;
	std::string lastAuthorizeTimestamp;

	// Heartbeat data
	std::string lastHeartbeatTimestamp;

	// Per-connector state, keyed by OCPP connectorId
	std::map<int, OCPPConnectorState> connectors;

	// Historical record of all transactions for this chargepoint, across all connectors
	std::vector<OCPPTransaction> transactionHistory;

	// Connection info
	std::string connectTimestamp;

	OCPPClientData()
		: socket(INVALID_SOCKET),
		  bootAccepted(false)
	{}
};

// Owns OCPP protocol state/logic for charge points connected to the HTTP server's
// websocket transport. The transport (accepting sockets, framing, HTTP upgrade)
// stays in HTTPServer; this class only knows how to interpret/produce OCPP
// messages once it is handed a socket to write to and text it has received.
class OCPPServerHandler
{
public:
	using SendMessageFn = std::function<bool(SOCKET, const std::string&)>;
	using SetVariableFn = std::function<void(const std::string&, const std::string&)>;

	// sendMessage: used to write an OCPP frame back to a client socket.
	// setVariable: used to publish debug/status values (e.g. for the /status page).
	OCPPServerHandler(SendMessageFn sendMessage, SetVariableFn setVariable);
	~OCPPServerHandler();

	// Sockets currently registered as OCPP clients (subset of the HTTP server's websocket clients).
	std::vector<SOCKET> clients;

	// Called once a websocket client's HTTP upgrade selects an OCPP subprotocol.
	void RegisterClient(SOCKET clientSocket, const std::string& path, const std::string& protocol);

	// Called when an OCPP client's connection is torn down.
	void UnregisterClient(SOCKET clientSocket);

	// Handle a text websocket message that may be an OCPP JSON array. Returns true if handled.
	bool HandleMessage(SOCKET clientSocket, const std::string& path, const std::string& msg);

	// Get OCPP client data by socket (returns nullptr if not found)
	OCPPClientData* GetClientData(SOCKET clientSocket);

	// Returns a snapshot copy of the transaction history for a client (thread-safe)
	std::vector<OCPPTransaction> GetTransactionHistory(SOCKET clientSocket);

	// Send SetChargingProfile request to a client
	bool SendSetChargingProfile(SOCKET clientSocket, int connectorId, float currentLimit);

private:
	std::map<SOCKET, OCPPClientData> m_clientData;
	CRITICAL_SECTION m_lock;

	SendMessageFn m_sendMessage;
	SetVariableFn m_setVariable;

	bool HandleBootNotification(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleStatusNotification(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleHeartbeat(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleAuthorize(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleMeterValues(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleStartTransaction(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleStopTransaction(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
	bool HandleDataTransfer(SOCKET clientSocket, const std::string& path, const std::string& msgId, const nlohmann::json& j);
};
