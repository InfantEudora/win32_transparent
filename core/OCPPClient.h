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

// Which charge point this client emulates.
//   GenericAC  - a plain OCPP 1.6 Core AC charge point, the original behaviour.
//   FermataV2G - a Heliox FE20 as deployed by Fermata Energy: same Core profile
//                plus the vendor "nu.ame" DataTransfer dialect, and PBaseline
//                power control via ChangeConfiguration rather than
//                SmartCharging. See docs/ocpp_pbaseline_setpoint.md.
enum class ChargerType {
	GenericAC  = 0,
	FermataV2G = 1
};

const char* ChargerTypeName(ChargerType type);

// The V2G telemetry the FE20 forwards in its "customMeterValues" DataTransfer.
// Field names match the charger's wire format one-for-one; defaults are the
// values from the captured real message in OCPPServerHandler.cpp.
struct V2GState {
	bool   local_mode = false;         // Standalone vs. backend-controlled
	double p_baseline = 0.0;           // Active power setpoint, W (+ charge / - discharge)
	double q_baseline = 0.0;           // Reactive power setpoint, var
	double p_max = 18326.0;            // Negotiated power envelope, W
	double p_min = -19998.0;
	double min_soc = 0.0;              // Backend SOC window for the session, %
	double max_soc = 100.0;
	double ev_min_soc = 10.0;          // Vehicle's own floor, %
	double ev_energy_capacity = 33.0;  // Pack size, kWh
	bool   session_active = false;
	double output_power = 0.0;         // Measured output, W - tracks p_baseline

	// Not in the DataTransfer, but settable over ChangeConfiguration on the
	// real unit and used here to clamp p_baseline (10000 on the tested charger).
	double power_limit = 10000.0;
};

// State of one connector ("socket") on the emulated charge point. Per OCPP,
// connectorId 0 is the charge point itself and 1..N are the physical sockets.
struct ClientConnectorState {
	std::string status = "Available";
	std::string errorCode = "NoError";
	std::string info;   // free text, e.g. the FE20's "Stop reason: 0x.., Error: 0x.."

	// Values for the connector-scoped messages. SoC lives here rather than in a
	// message of its own because it rides along as a sampledValue inside
	// MeterValues, the same way the real charger reports it.
	double meterPowerWatts = 7400.0;
	double soc = 50.0;
	int    meterWh = 0;                     // meter reading, used as meterStart/meterStop
	int    transactionId = 1;               // transaction currently running here
	std::string idTag = "TagNoUnderscore";
	std::string stopReason = "Local";
};

// OCPP Client data structure
struct OCPPClientInfo {
	// Connection info
	std::string serverUrl;
	std::string chargeBoxIdentity;
	std::string connectError;   // why the last connect attempt never got off the ground
	bool connected = false;
	bool websocketHandshakeComplete = false;

	// Handshake outcome. websocketHandshakeComplete says it succeeded; these say
	// it definitively failed, which is a different thing from still being in
	// flight - without them a rejected upgrade is indistinguishable from a slow
	// one and the UI waits forever.
	bool handshakeFailed = false;
	int  handshakeStatusCode = 0;   // HTTP status of the upgrade response, e.g. 404
	std::string handshakeError;     // "404 Not Found", or why the response made no sense
	std::string handshakeBody;      // response body, if the server explained itself

	// A backend can accept the TCP connection and then never answer the upgrade
	// at all, which no amount of response parsing will catch. Give up after
	// this long. 0 disables the timeout.
	DWORD handshakeTimeoutMs = 10000;
	DWORD handshakeSentTickMs = 0;  // GetTickCount() when the request went out

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

	// Per-connector status, keyed by connectorId. A newly created client starts
	// with the charge point (0) and one socket (1), both Available - the same
	// shape the real FE20 reports on.
	std::map<int, ClientConnectorState> connectors = {
		{0, ClientConnectorState()},
		{1, ClientConnectorState()}
	};

	// Meter values
	MeterData m_meterdata;

	// Authorization
	std::string lastAuthorizedIdTag;

	// Charging profile
	std::string chargingRateUnit;
	std::vector<ChargingSchedulePeriod> chargingSchedulePeriods;

	// Which charger is being emulated, and the extra state that only the
	// Fermata/Heliox emulation uses.
	ChargerType chargerType = ChargerType::GenericAC;
	V2GState v2g;
};

class OCPPClient : public TCPClient
{
public:
	// Result of kicking off the WebSocket handshake. The handshake response
	// arrives asynchronously via the nonblocking data-received handler, so
	// sending the request successfully does not mean it has completed yet.
	enum class HandshakeResult {
		Failed,   // Could not even send the handshake request
		Pending   // Request sent; completion/failure is reported later via IsWebSocketReady()/OnDataReceivedInternal
	};

	OCPPClient();
	~OCPPClient();

	// Connect to OCPP server with WebSocket upgrade
	bool ConnectOCPP(const std::string& host, int port, const std::string& chargeBoxIdentity);

	// Disconnect and reset WebSocket state
	void Disconnect() override;

	// Send OCPP messages
	bool SendBootNotification(const std::string& vendor, const std::string& model);
	bool SendStatusNotification(int connectorId, const std::string& status, const std::string& errorCode = "NoError");

	// Send StatusNotification for a connector using its currently stored state,
	// and the same for every known connector.
	bool SendConnectorStatus(int connectorId);
	bool SendAllConnectorStatus();
	bool SendHeartbeat();
	bool SendAuthorize(const std::string& idTag);
	bool SendMeterValues(int connectorId, double powerWatts, double socPercent);
	bool SendStartTransaction(int connectorId, const std::string& idTag, int meterStart);
	bool SendStopTransaction(int transactionId, int meterStop, const std::string& reason = "Local");
	bool SendDataTransfer(const std::string& vendorId, const std::string& messageId, const std::string& data);

	// Fermata/Heliox specific. Both are no-ops unless chargerType is FermataV2G.
	bool SendCustomMeterValues();                                  // the "nu.ame"/customMeterValues DataTransfer
	bool SendV2GMeterValues(int connectorId, double socPercent);   // MeterValues with the FE20's measurand set

	// Build the exact `data` string that SendCustomMeterValues() puts on the
	// wire, so the UI can show it without sending anything.
	std::string BuildCustomMeterValuesData() const;

	// Ramp output_power toward the commanded p_baseline. Call once per frame
	// from the application logic tick; ignored for a GenericAC client.
	void TickV2G(double dtSeconds);

	// Apply this charger's identity (vendor/model/serials/firmware) to m_info.
	void SetChargerType(ChargerType type);

	// Get client info
	OCPPClientInfo& GetInfo() { return m_info; }
	const OCPPClientInfo& GetInfo() const { return m_info; }

	// Set callback for OCPP responses
	void SetOnOCPPResponse(std::function<void(const std::string& messageType, const json& response)> callback);

	// Check if WebSocket handshake is complete
	bool IsWebSocketReady() const { return m_info.websocketHandshakeComplete; }

	// True once the server has answered the upgrade with something other than
	// 101. The error text survives a Disconnect() so the UI can still show why;
	// it is cleared by the next ConnectOCPP().
	bool HasHandshakeFailed() const { return m_info.handshakeFailed; }
	const std::string& GetHandshakeError() const { return m_info.handshakeError; }

	// Fail the client if the upgrade has gone unanswered past the timeout. Call
	// once per frame from the application tick; returns true on the frame it
	// trips. Sets the same failure state a rejection does, so the existing
	// teardown and UI paths handle it identically.
	bool CheckHandshakeTimeout();

private:
	OCPPClientInfo m_info;
	std::string m_receiveBuffer;
	int m_messageIdCounter = 0;
	std::function<void(const std::string& messageType, const json& response)> m_onOCPPResponse;

	// Transaction
	OCPPTransaction m_current_transaction;

	// WebSocket handshake
	HandshakeResult PerformWebSocketHandshake(const std::string& chargeBoxIdentity);
	std::string GenerateWebSocketKey();

	// WebSocket frame handling
	bool SendWebSocketFrame(const std::string& message);
	void HandleWebSocketFrame(const char* data, int length);
	std::string DecodeWebSocketFrame(const char* data, int length, int& bytesConsumed);

	// OCPP message handling
	bool SendOCPPMessage(int messageType, const std::string& messageId, const std::string& action, const json& payload);
	void HandleOCPPMessage(const std::string& message);
	void HandleSetChargingProfile(const std::string& messageId, const json& payload);
	void HandleChangeConfiguration(const std::string& messageId, const json& payload);
	void HandleChangeAvailability(const std::string& messageId, const json& payload);

	// Generate unique message ID
	std::string GenerateMessageId();

	// Get current UTC time as ISO 8601 string
	std::string CurrentTimestamp();

	// Internal data received handler
	void OnDataReceivedInternal(const char* data, int length);
};
