#pragma once

#include "TCPClient.h"
#include "tinygltf/json.hpp"
#include <string>
#include <map>
#include <functional>
#include <vector>
using json = nlohmann::json;

struct MeterData{
	double powerActiveImport = 0.0;  // Watts
	double soc = 0.0;  // State of Charge %
	float current = 0;
	float voltage = 0;
};

// A single period in a charging schedule
struct ChargingSchedulePeriod {
	int startPeriod;   // Seconds from schedule start
	double limit;      // Power limit (W or A depending on chargingRateUnit)
	int numberPhases;  // Optional, -1 if not set

	ChargingSchedulePeriod()
		: startPeriod(0), limit(0.0), numberPhases(-1)
	{}
};

// Record of a single charging transaction (start through stop)
struct OCPPTransaction {
	int transactionId;
	int connectorId;
	std::string idTag;
	int meterStart;       // Wh at start
	std::string startTimestamp;
	int meterStop;        // Wh at stop (-1 while active)
	std::string stopTimestamp;
	std::string stopReason;
	bool completed;

	OCPPTransaction()
		: transactionId(-1), connectorId(-1), meterStart(0),
		  meterStop(-1), completed(false)
	{}
};

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
	MeterData m_meterdata;

	// Authorization
	std::string lastAuthorizedIdTag;

	// Charging profile
	std::string chargingRateUnit;
	std::vector<ChargingSchedulePeriod> chargingSchedulePeriods;
};

class OCPPClient : public TCPClient
{
public:
	OCPPClient();
	~OCPPClient();

	// Connect to OCPP server with WebSocket upgrade
	bool ConnectOCPP(const std::string& host, int port, const std::string& chargeBoxIdentity);

	// Disconnect and reset WebSocket state
	void Disconnect() override;

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

	// Transaction
	OCPPTransaction m_current_transaction;

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
	void HandleSetChargingProfile(const std::string& messageId, const json& payload);

	// Generate unique message ID
	std::string GenerateMessageId();

	// Get current UTC time as ISO 8601 string
	std::string CurrentTimestamp();

	// Internal data received handler
	void OnDataReceivedInternal(const char* data, int length);
};
