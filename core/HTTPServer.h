#pragma once

#include "TCPServer.h"
#include "FileWatcher.h"
#include <string>
#include <map>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include "tinygltf/json.hpp"
using json = nlohmann::json;

// Structure to store OCPP client data
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

	// Status notification data
	std::string connectorStatus;
	std::string errorCode;
	std::string statusTimestamp;
	std::string vendorId;
	std::string vendorErrorCode;
	int connectorId;

	// Authorization data
	std::string lastAuthorizedIdTag;
	std::string lastAuthorizeTimestamp;

	// Heartbeat data
	std::string lastHeartbeatTimestamp;

	// Meter values data
	double powerActiveImport;  // in Watts
	double ACVoltage; //In volts
	double soc;  // State of Charge in Percent
	std::string meterValuesTimestamp;

	// Transaction data
	int transactionId;
	std::string transactionIdTag;
	int transactionMeterStart;
	std::string transactionTimestamp;
	std::string transactionReservationId;

	// Server-side charging profile control
	float server_current_limit;
	bool server_current_timit_updatereq;
	DWORD last_profile_update_time_ms;

	// Connection info
	std::string connectTimestamp;

	OCPPClientData()
		: socket(INVALID_SOCKET),
		  bootAccepted(false),
		  connectorId(-1),
		  powerActiveImport(0.0),
		  soc(0.0),
		  transactionId(-1),
		  transactionMeterStart(0),
		  server_current_limit(32.0f),
		  server_current_timit_updatereq(false),
		  last_profile_update_time_ms(0)
	{}
};

class HTTPServer : public TCPServer
{
public:
	HTTPServer(int port = 8080);
	~HTTPServer();

	// Set the HTML content to serve
	void SetHTMLContent(const std::string& html);

	// Load HTML content from a file using the project's File utilities
	// Returns true if file was found and loaded.
	bool LoadHTMLFromFile(const std::string& filename);

	// Set a variable that will be replaced in the HTML
	// Usage: SetVariable("playerHealth", "85")
	// In HTML: use {{playerHealth}} as a placeholder
	void SetVariable(const std::string& name, const std::string& value);

	// Start the HTTP server
	bool Start() override;

	// OCPP-specific websocket clients (subset of m_wsClients) - sockets speaking ocpp1.6/ocpp2.0
	std::vector<SOCKET> m_ocppClients;

	// Get OCPP client data by socket (returns nullptr if not found)
	OCPPClientData* GetOCPPClientData(SOCKET clientSocket);

	// Send SetChargingProfile request to a client
	bool SendSetChargingProfile(SOCKET clientSocket, int connectorId, float currentLimit);

private:
	std::string m_htmlContent;
	std::string m_htmlFilePath;
	std::map<std::string, std::string> m_variables;

	// WebSocket clients
	std::vector<SOCKET> m_wsClients;

	// OCPP client data map (socket -> client data)
	std::map<SOCKET, OCPPClientData> m_ocppClientData;

	CRITICAL_SECTION m_wsLock;

	// File watcher for HTML file changes
	FileWatcher* m_fileWatcher;

	// Send raw websocket text message to a client
	bool SendWebSocketMessage(SOCKET client, const std::string &message);

	// Broadcast JSON to all websocket clients
	void BroadcastVariables();

	// Handle HTTP requests from clients
	void HandleHTTPConnection(SOCKET clientSocket);

	// Per client reader that waits for HTTP trafic.
	void HandleHTTPClient(SOCKET clientSocket);

	// Per-WebSocket client reader that decodes client frames and handles simple opcodes
	void HandleWebSocketClient(SOCKET clientSocket, const std::string &path, const std::string &protocol);

	// Handle a text websocket message that may be an OCPP JSON array. Returns true if handled.
	bool HandleOCPPMessage(SOCKET clientSocket, const std::string &path, const std::string &msg);

	// Handle BootNotification OCPP message specifically
	bool HandleOCPPBootNotification(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPStatusNotification(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPHeartbeat(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPAuthorize(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPMeterValues(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPStartTransaction(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPStopTransaction(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);
	bool HandleOCPPDataTransfer(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j);

	// Parse HTTP request
	std::string ParseHTTPRequest(const std::string& request);

	// Replace all variables in HTML with their values
	std::string ReplaceVariables(const std::string& html);
};
