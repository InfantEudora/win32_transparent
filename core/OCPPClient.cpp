#include "OCPPClient.h"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <wincrypt.h>

static Debugger *debug = new Debugger("OCPPClient", DEBUG_ALL);

OCPPClient::OCPPClient()
    : TCPClient(), m_messageIdCounter(0) {
    // Set up internal data received handler
    SetOnDataReceived([this](const char *data, int length) {
        OnDataReceivedInternal(data, length);
    });
    SetOnConnected([this]() {
        m_info.connected = true;
        debug->Info("TCP connection established, now performing WebSocket handshake\n");
        if (PerformWebSocketHandshake(m_info.chargeBoxIdentity) == HandshakeResult::Failed) {
            debug->Err("WebSocket handshake failed\n");
            m_info.connected = false;
            Disconnect();
        }
        // else: request sent, actual success/failure arrives asynchronously
        // in OnDataReceivedInternal once the server's HTTP response comes in.
    });
}

OCPPClient::~OCPPClient() {
	
}

void OCPPClient::Disconnect() {
    m_info.websocketHandshakeComplete = false;
    TCPClient::Disconnect();
}

bool OCPPClient::ConnectOCPP(const std::string &host, int port, const std::string &chargeBoxIdentity) {
    m_info.chargeBoxIdentity = chargeBoxIdentity;
    m_info.serverUrl = host + ":" + std::to_string(port);
    m_info.websocketHandshakeComplete = false;

    // First establish TCP connection
    if (!Connect(host, port)) {
        debug->Err("Failed to connect to OCPP server\n");
        return false;
    }    
    return true;
}

std::string OCPPClient::GenerateWebSocketKey() {
    // Generate 16 random bytes
    unsigned char randomBytes[16];
    HCRYPTPROV hProvider = 0;

    if (!CryptAcquireContext(&hProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        debug->Err("CryptAcquireContext failed\n");
        return "";
    }

    if (!CryptGenRandom(hProvider, 16, randomBytes)) {
        debug->Err("CryptGenRandom failed\n");
        CryptReleaseContext(hProvider, 0);
        return "";
    }

    CryptReleaseContext(hProvider, 0);

    // Base64 encode
    DWORD base64Len = 0;
    CryptBinaryToStringA(randomBytes, 16, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &base64Len);

    std::string base64Key(base64Len, '\0');
    CryptBinaryToStringA(randomBytes, 16, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &base64Key[0], &base64Len);

    // Remove null terminator if present
    if (!base64Key.empty() && base64Key.back() == '\0')
        base64Key.pop_back();

    return base64Key;
}

OCPPClient::HandshakeResult OCPPClient::PerformWebSocketHandshake(const std::string &chargeBoxIdentity) {
    std::string wsKey = GenerateWebSocketKey();
    if (wsKey.empty()) {
        debug->Err("Failed to generate WebSocket key\n");
        return HandshakeResult::Failed;
    }

    // Build WebSocket upgrade request
    std::ostringstream request;
    request << "GET /" << chargeBoxIdentity << " HTTP/1.1\r\n";
    request << "Host: " << m_info.serverUrl << "\r\n";
    request << "Upgrade: websocket\r\n";
    request << "Connection: Upgrade\r\n";
    request << "Sec-WebSocket-Key: " << wsKey << "\r\n";
    request << "Sec-WebSocket-Version: 13\r\n";
    request << "Sec-WebSocket-Protocol: ocpp1.6\r\n";
    request << "\r\n";

    std::string handshake = request.str();
    debug->Info("Sending WebSocket handshake:\n%s", handshake.c_str());

    if (!Send(handshake)) {
        debug->Err("Failed to send WebSocket handshake\n");
        return HandshakeResult::Failed;
    }

    // The handshake response is read on the nonblocking data-received path
    // (see OnDataReceivedInternal), so completion cannot be observed here.
    debug->Info("WebSocket handshake request sent, awaiting response\n");
    return HandshakeResult::Pending;
}

std::string OCPPClient::GenerateMessageId() {
    return std::to_string(++m_messageIdCounter);
}

std::string OCPPClient::CurrentTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm utc;
    gmtime_s(&utc, &now);
    std::ostringstream ts;
    ts << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S.000Z");
    return ts.str();
}

bool OCPPClient::SendWebSocketFrame(const std::string &message) {
    if (!IsConnected()) {
        debug->Err("Not connected to server\n");
        return false;
    }

    std::vector<unsigned char> frame;

    // FIN bit + text opcode (0x81)
    frame.push_back(0x81);

    // Mask bit + payload length
    size_t payloadLen = message.length();
    if (payloadLen < 126) {
        frame.push_back(0x80 | static_cast<unsigned char>(payloadLen));
    } else if (payloadLen < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back((payloadLen >> 8) & 0xFF);
        frame.push_back(payloadLen & 0xFF);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((payloadLen >> (i * 8)) & 0xFF);
        }
    }

    // Masking key (4 bytes)
    unsigned char maskingKey[4];
    HCRYPTPROV hProvider = 0;
    if (CryptAcquireContext(&hProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProvider, 4, maskingKey);
        CryptReleaseContext(hProvider, 0);
    } else {
        // Fallback to simple random
        for (int i = 0; i < 4; i++)
            maskingKey[i] = rand() & 0xFF;
    }

    frame.insert(frame.end(), maskingKey, maskingKey + 4);

    // Masked payload
    for (size_t i = 0; i < payloadLen; i++) {
        frame.push_back(message[i] ^ maskingKey[i % 4]);
    }

    return Send(reinterpret_cast<const char *>(frame.data()), static_cast<int>(frame.size()));
}

bool OCPPClient::SendOCPPMessage(int messageType, const std::string &messageId, const std::string &action, const json &payload) {
    json ocppMessage = json::array();
    ocppMessage.push_back(messageType);
    ocppMessage.push_back(messageId);

    if (messageType == 2) // CALL
    {
        ocppMessage.push_back(action);
        ocppMessage.push_back(payload);
    } else if (messageType == 3) // CALLRESULT
    {
        ocppMessage.push_back(payload);
    } else if (messageType == 4) // CALLERROR
    {
        ocppMessage.push_back(action); // error code
        ocppMessage.push_back(payload.value("errorDescription", ""));
        ocppMessage.push_back(payload.value("errorDetails", json::object()));
    }

    std::string message = ocppMessage.dump();
    debug->Info("Sending OCPP: %s\n", message.c_str());

    return SendWebSocketFrame(message);
}

bool OCPPClient::SendBootNotification(const std::string &vendor, const std::string &model) {
    m_info.chargePointVendor = vendor;
    m_info.chargePointModel = model;

    json payload = {
        {"chargePointVendor", vendor},
        {"chargePointModel", model}};

    if (!m_info.chargeBoxSerialNumber.empty())
        payload["chargeBoxSerialNumber"] = m_info.chargeBoxSerialNumber;
    if (!m_info.chargePointSerialNumber.empty())
        payload["chargePointSerialNumber"] = m_info.chargePointSerialNumber;
    if (!m_info.firmwareVersion.empty())
        payload["firmwareVersion"] = m_info.firmwareVersion;

    return SendOCPPMessage(2, GenerateMessageId(), "BootNotification", payload);
}

bool OCPPClient::SendStatusNotification(int connectorId, const std::string &status, const std::string &errorCode) {
    m_info.connectorId = connectorId;
    m_info.connectorStatus = status;
    m_info.errorCode = errorCode;

    json payload = {
        {"connectorId", connectorId},
        {"status", status},
        {"errorCode", errorCode},
        {"timestamp", "2025-01-01T00:00:00.000Z"} // Simplified timestamp
    };

    return SendOCPPMessage(2, GenerateMessageId(), "StatusNotification", payload);
}

bool OCPPClient::SendHeartbeat() {
    json payload = json::object();
    return SendOCPPMessage(2, GenerateMessageId(), "Heartbeat", payload);
}

bool OCPPClient::SendAuthorize(const std::string &idTag) {
    m_info.lastAuthorizedIdTag = idTag;

    json payload = {
        {"idTag", idTag}};

    return SendOCPPMessage(2, GenerateMessageId(), "Authorize", payload);
}

bool OCPPClient::SendMeterValues(int connectorId, double powerWatts, double socPercent) {    
	m_info.m_meterdata.powerActiveImport = powerWatts;
    m_info.m_meterdata.soc = socPercent;

    json sampledValue = json::array();

    // Power value
    sampledValue.push_back({{"value", std::to_string(powerWatts)},
                            {"context", "Sample.Periodic"},
                            {"measurand", "Power.Active.Import"},
                            {"unit", "W"}});

    // SoC value
    sampledValue.push_back({{"value", std::to_string(socPercent)},
                            {"context", "Sample.Periodic"},
                            {"measurand", "SoC"},
                            {"unit", "Percent"}});

    json meterValue = {
        {"timestamp", "2025-01-01T00:00:00.000Z"},
        {"sampledValue", sampledValue}};

    json payload = {
        {"connectorId", connectorId},
        {"meterValue", json::array({meterValue})}};

    return SendOCPPMessage(2, GenerateMessageId(), "MeterValues", payload);
}

bool OCPPClient::SendStartTransaction(int connectorId, const std::string &idTag, int meterStart) {
    json payload = {
        {"connectorId", connectorId},
        {"idTag", idTag},
        {"meterStart", meterStart},
        {"timestamp", CurrentTimestamp()}};

    return SendOCPPMessage(2, GenerateMessageId(), "StartTransaction", payload);
}

bool OCPPClient::SendStopTransaction(int transactionId, int meterStop, const std::string &reason) {
    json payload = {
        {"transactionId", transactionId},
        {"meterStop", meterStop},
        {"timestamp", CurrentTimestamp()}};

    if (!reason.empty())
        payload["reason"] = reason;

    return SendOCPPMessage(2, GenerateMessageId(), "StopTransaction", payload);
}

void OCPPClient::SetOnOCPPResponse(std::function<void(const std::string &messageType, const json &response)> callback) {
    m_onOCPPResponse = callback;
}

void OCPPClient::OnDataReceivedInternal(const char *data, int length) {
    // Add received data to buffer
    m_receiveBuffer.append(data, length);

    // Check if this is an HTTP response (WebSocket handshake)
    if (!m_info.websocketHandshakeComplete && m_receiveBuffer.find("HTTP/1.1") != std::string::npos) {
        if (m_receiveBuffer.find("\r\n\r\n") != std::string::npos) {
            debug->Info("WebSocket handshake response received:\n%s\n", m_receiveBuffer.c_str());
            debug->Info("Buffer length: %zu\n", m_receiveBuffer.length());
            debug->Info("Buffer content:\n%s\n", m_receiveBuffer.c_str());

            if (m_receiveBuffer.find("101 Switching Protocols") != std::string::npos) {
                debug->Info("WebSocket handshake successful\n");
                m_info.websocketHandshakeComplete = true;
            } else {
                // Note: deliberately not calling Disconnect() here — this runs
                // on the receive thread itself, and Disconnect() joins that
                // same thread (deadlock). Callers must notice via
                // IsWebSocketReady() staying false and disconnect from another thread.
                debug->Err("WebSocket handshake failed\n");
            }

            m_receiveBuffer.clear();
        }
        return;
    }

    // Handle WebSocket frames
    if (m_info.websocketHandshakeComplete) {
        HandleWebSocketFrame(m_receiveBuffer.c_str(), static_cast<int>(m_receiveBuffer.length()));
    }
}

std::string OCPPClient::DecodeWebSocketFrame(const char *data, int length, int &bytesConsumed) {
    bytesConsumed = 0;

    if (length < 2)
        return "";

    unsigned char firstByte = data[0];
    unsigned char secondByte = data[1];

    bool fin = (firstByte & 0x80) != 0;
    int opcode = firstByte & 0x0F;
    bool masked = (secondByte & 0x80) != 0;
    uint64_t payloadLen = secondByte & 0x7F;

    int offset = 2;

    if (payloadLen == 126) {
        if (length < 4)
            return "";
        payloadLen = (static_cast<uint64_t>((unsigned char)data[2]) << 8)
                   |  static_cast<uint64_t>((unsigned char)data[3]);
        offset = 4;
    } else if (payloadLen == 127) {
        if (length < 10)
            return "";
        payloadLen = 0;
        for (int i = 0; i < 8; i++) {
            payloadLen = (payloadLen << 8) | static_cast<uint64_t>((unsigned char)data[2 + i]);
        }
        offset = 10;
    }

    if (masked) {
        offset += 4; // Skip masking key (server shouldn't send masked frames)
    }

    if (payloadLen > static_cast<uint64_t>(length) || static_cast<uint64_t>(length) - payloadLen < static_cast<uint64_t>(offset))
        return ""; // Not enough data yet

    std::string payload(data + offset, payloadLen);
    bytesConsumed = offset + static_cast<int>(payloadLen);

    return payload;
}

void OCPPClient::HandleWebSocketFrame(const char *data, int length) {
    int bytesConsumed = 0;
    std::string message = DecodeWebSocketFrame(data, length, bytesConsumed);

    if (bytesConsumed > 0) {
        // Remove processed data from buffer
        m_receiveBuffer.erase(0, bytesConsumed);

        if (!message.empty()) {
            debug->Info("Received WebSocket message: %s\n", message.c_str());
            HandleOCPPMessage(message);
        }
    }
}

void OCPPClient::HandleSetChargingProfile(const std::string &messageId, const json &payload) {
    m_info.chargingSchedulePeriods.clear();

    if (!payload.contains("csChargingProfiles")) {
        debug->Err("SetChargingProfile: missing csChargingProfiles\n");
        json response = {{"status", "Rejected"}};
        SendOCPPMessage(3, messageId, "", response);
        return;
    }

    const json &profile = payload["csChargingProfiles"];

    if (!profile.contains("chargingSchedule")) {
        debug->Err("SetChargingProfile: missing chargingSchedule\n");
        json response = {{"status", "Rejected"}};
        SendOCPPMessage(3, messageId, "", response);
        return;
    }

    const json &schedule = profile["chargingSchedule"];

    m_info.chargingRateUnit = schedule.value("chargingRateUnit", "W");

    const json &periods = schedule["chargingSchedulePeriod"];
    debug->Info("SetChargingProfile: %zu period(s), unit=%s\n", periods.size(), m_info.chargingRateUnit.c_str());

    for (const auto &p : periods) {
        ChargingSchedulePeriod period;
        period.startPeriod = p.value("startPeriod", 0);
        period.limit = p.value("limit", 0.0);
        period.numberPhases = p.value("numberPhases", -1);

        debug->Info("  startPeriod=%d  limit=%.2f  numberPhases=%d\n",
            period.startPeriod, period.limit,
            period.numberPhases == -1 ? 3 : period.numberPhases);

        m_info.chargingSchedulePeriods.push_back(period);
    }

    json response = {{"status", "Accepted"}};
    SendOCPPMessage(3, messageId, "", response);
}

void OCPPClient::HandleOCPPMessage(const std::string &message) {
	debug->Info("Attempting to parse [%s]\n",message.c_str());
    json j = json::parse(message, nullptr, false);
    if (j.is_discarded()) {
        debug->Err("Failed to parse OCPP message as JSON\n");
        return;
    }
	debug->Ok("Parsed\n");

    if (!j.is_array() || j.size() < 3) {
        debug->Err("Invalid OCPP message format\n");
        return;
    }

    int messageType = j[0].get<int>();
    std::string messageId = j[1].get<std::string>();

    if (messageType == 3) { // CALLRESULT
        json payload = j[2];
        debug->Info("Received CALLRESULT: %s\n", payload.dump().c_str());

        // Call callback if set
        if (m_onOCPPResponse) {
            m_onOCPPResponse("CALLRESULT", payload);
        }

        // Handle specific responses
        if (payload.contains("status")) {
            std::string status = payload["status"].get<std::string>();
            if (status == "Accepted") {
                m_info.bootAccepted = true;
                debug->Info("Boot notification accepted\n");
            }
        }
    } else if (messageType == 4) { // CALLERROR
        std::string errorCode = j[2].get<std::string>();
        std::string errorDescription = j[3].get<std::string>();
        debug->Err("Received CALLERROR: %s - %s\n", errorCode.c_str(), errorDescription.c_str());

        if (m_onOCPPResponse) {
            json errorPayload = {
                {"errorCode", errorCode},
                {"errorDescription", errorDescription}};
            m_onOCPPResponse("CALLERROR", errorPayload);
        }
    } else if (messageType == 2) { // CALL (server request)
        std::string action = j[2].get<std::string>();
        json payload = j[3];
        debug->Info("Received CALL from server: %s\n", action.c_str());

        if (action == "SetChargingProfile") {
            HandleSetChargingProfile(messageId, payload);
        } else {
            json response = {{"status", "Accepted"}};
            SendOCPPMessage(3, messageId, "", response);
        }
    }
}
