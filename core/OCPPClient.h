#pragma once

#include "TCPClient.h"
#include "tinygltf/json.hpp"
#include <string>
#include <map>
#include <functional>
#include <vector>

using json = nlohmann::json;

// OCPP Client data structure
struct OCPPClientInfo {
	// Connection info
	std::string serverUrl;
	std::string chargeBoxIdentity;
	bool connected = false;
	bool websocketHandshakeComplete = false;

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
	bool bootAccepted = false;
	std::string bootTimestamp;

	// Status
	std::string connectorStatus = "Available";
	std::string errorCode = "NoError";
	int connectorId = 0;

	// Meter values
	double powerActiveImport = 0.0;  // Watts
	double soc = 0.0;  // State of Charge %

	// Authorization
	std::string lastAuthorizedIdTag;
};

class OCPPClient : public TCPClient
{
public:
	OCPPClient();
	~OCPPClient();

	// Connect to OCPP server with WebSocket upgrade
	bool ConnectOCPP(const std::string& host, int port, const std::string& chargeBoxIdentity);

	// Send OCPP messages
	bool SendBootNotification(const std::string& vendor, const std::string& model);
	bool SendStatusNotification(int connectorId, const std::string& status, const std::string& errorCode = "NoError");
	bool SendHeartbeat();
	bool SendAuthorize(const std::string& idTag);
	bool SendMeterValues(int connectorId, double powerWatts, double socPercent);
	bool SendStartTransaction(int connectorId, const std::string& idTag, int meterStart);
	bool SendStopTransaction(int transactionId, int meterStop, const std::string& reason = "Local");

	// Get client info
	OCPPClientInfo& GetInfo() { return m_info; }
	const OCPPClientInfo& GetInfo() const { return m_info; }

	// Set callback for OCPP responses
	void SetOnOCPPResponse(std::function<void(const std::string& messageType, const json& response)> callback);

	// Check if WebSocket handshake is complete
	bool IsWebSocketReady() const { return m_info.websocketHandshakeComplete; }

private:
	OCPPClientInfo m_info;
	std::string m_receiveBuffer;
	int m_messageIdCounter = 0;
	std::function<void(const std::string& messageType, const json& response)> m_onOCPPResponse;

	// WebSocket handshake
	bool PerformWebSocketHandshake(const std::string& chargeBoxIdentity);
	std::string GenerateWebSocketKey();

	// WebSocket frame handling
	bool SendWebSocketFrame(const std::string& message);
	void HandleWebSocketFrame(const char* data, int length);
	std::string DecodeWebSocketFrame(const char* data, int length, int& bytesConsumed);

	// OCPP message handling
	bool SendOCPPMessage(int messageType, const std::string& messageId, const std::string& action, const json& payload);
	void HandleOCPPMessage(const std::string& message);

	// Generate unique message ID
	std::string GenerateMessageId();

	// Internal data received handler
	void OnDataReceivedInternal(const char* data, int length);
};
