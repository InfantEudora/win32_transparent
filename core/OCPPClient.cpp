#include "OCPPClient.h"
#include <ctime>
#include <cmath>
#include <cstdlib>
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

bool OCPPClient::CheckHandshakeTimeout() {
    // Nothing to do once the outcome is known either way, or before the
    // request has actually gone out.
    if (m_info.websocketHandshakeComplete || m_info.handshakeFailed)
        return false;
    if (!IsConnected() || m_info.handshakeSentTickMs == 0 || m_info.handshakeTimeoutMs == 0)
        return false;

    // Unsigned subtraction, so this stays correct across the ~49.7 day
    // GetTickCount() wrap.
    DWORD elapsed = GetTickCount() - m_info.handshakeSentTickMs;
    if (elapsed < m_info.handshakeTimeoutMs)
        return false;

    m_info.handshakeFailed = true;
    m_info.handshakeStatusCode = 0;
    m_info.handshakeError = "no response within " +
        std::to_string(m_info.handshakeTimeoutMs / 1000) + "s";

    debug->Err("WebSocket handshake timed out after %lu ms\n", (unsigned long)elapsed);
    return true;
}

void OCPPClient::Disconnect() {
    m_info.websocketHandshakeComplete = false;
    // Drop any partial frame/response left over, otherwise the next connect
    // parses this session's leftovers as part of its handshake response.
    m_receiveBuffer.clear();
    TCPClient::Disconnect();
}

bool OCPPClient::ConnectOCPP(const std::string &host, int port, const std::string &chargeBoxIdentity) {
    m_info.chargeBoxIdentity = chargeBoxIdentity;
    m_info.serverUrl = host + ":" + std::to_string(port);
    m_info.websocketHandshakeComplete = false;

    // Clear the previous attempt's outcome - this is the only place it resets,
    // so a failure stays on screen after the socket is gone.
    m_info.handshakeFailed = false;
    m_info.handshakeStatusCode = 0;
    m_info.handshakeError.clear();
    m_info.handshakeBody.clear();
    m_info.handshakeSentTickMs = 0;
    m_receiveBuffer.clear();

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
    // Stamp the send so CheckHandshakeTimeout() has something to measure from.
    m_info.handshakeSentTickMs = GetTickCount();

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

    // IsConnected() is TCP-level and goes true before the WebSocket upgrade has
    // been negotiated. Writing a masked frame in that window puts binary noise
    // in front of an HTTP server that is still waiting for the GET, which it
    // logs as an unparseable request and the connection is dead from there.
    // The handshake itself goes out via Send(), not this path, so it is safe
    // to hard-gate every frame on the 101 having arrived.
    if (!m_info.websocketHandshakeComplete) {
        debug->Err("Refusing to send frame before the WebSocket handshake completed\n");
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
    // Record what was sent so the stored per-connector state and the wire never
    // disagree - the UI reads this map back.
    ClientConnectorState &conn = m_info.connectors[connectorId];
    conn.status = status;
    conn.errorCode = errorCode;

    json payload = {
        {"connectorId", connectorId},
        {"status", status},
        {"errorCode", errorCode},
        {"timestamp", CurrentTimestamp()}
    };

    // info is optional in StatusNotification.req; the real charger uses it to
    // carry its raw stop-reason/error codes.
    if (!conn.info.empty())
        payload["info"] = conn.info;

    return SendOCPPMessage(2, GenerateMessageId(), "StatusNotification", payload);
}

bool OCPPClient::SendConnectorStatus(int connectorId) {
    const ClientConnectorState &conn = m_info.connectors[connectorId];
    return SendStatusNotification(connectorId, conn.status, conn.errorCode);
}

bool OCPPClient::SendAllConnectorStatus() {
    bool allOk = true;
    for (const auto &kv : m_info.connectors) {
        if (!SendConnectorStatus(kv.first))
            allOk = false;
    }
    return allOk;
}

void OCPPClient::HandleChangeAvailability(const std::string &messageId, const json &payload) {
    int connectorId = payload.value("connectorId", 0);
    std::string type = payload.value("type", "");
    bool inoperative = (type == "Inoperative");
    std::string newStatus = inoperative ? "Unavailable" : "Available";

    // connectorId 0 addresses the whole charge point. Beyond that, the real
    // FE20 was measured changing *both* connectors when only connector 1 was
    // addressed - it is a single-socket unit, so connector 0 mirrors it. The
    // Fermata emulation reproduces that; a generic AC charger changes only the
    // connector it was told to. See docs/ocpp_fault_diagnostics.md section 4.
    bool applyToAll = (connectorId == 0) || (m_info.chargerType == ChargerType::FermataV2G);

    if (applyToAll) {
        for (auto &kv : m_info.connectors)
            kv.second.status = newStatus;
    } else {
        m_info.connectors[connectorId].status = newStatus;
    }

    debug->Info("ChangeAvailability connectorId=%d type=%s -> %s (%s)\n",
        connectorId, type.c_str(), newStatus.c_str(),
        applyToAll ? "all connectors" : "this connector only");

    json response = {{"status", "Accepted"}};
    SendOCPPMessage(3, messageId, "", response);

    // A real charge point reports the new state straight after accepting.
    if (applyToAll) {
        SendAllConnectorStatus();
    } else {
        SendConnectorStatus(connectorId);
    }
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

const char* ChargerTypeName(ChargerType type) {
    switch (type) {
        case ChargerType::FermataV2G: return "Fermata / Heliox FE20 (V2G)";
        case ChargerType::GenericAC:
        default:                      return "Generic AC charger";
    }
}

void OCPPClient::SetChargerType(ChargerType type) {
    m_info.chargerType = type;

    // Identity as the real units report it in BootNotification. The Fermata
    // values are taken from the captured message quoted in OCPPServerHandler.cpp.
    if (type == ChargerType::FermataV2G) {
        m_info.chargePointVendor       = "Heliox";
        m_info.chargePointModel        = "FE20";
        m_info.chargeBoxSerialNumber   = "620722003700_2241001115";
        m_info.chargePointSerialNumber = "243401022";
        m_info.firmwareVersion         = "1.0.0";
    } else {
        m_info.chargePointVendor       = "MyVendor";
        m_info.chargePointModel        = "ChargePoint-v1";
        m_info.chargeBoxSerialNumber   = "123456";
        m_info.chargePointSerialNumber = "123456";
        m_info.firmwareVersion         = "1.0";
    }

    debug->Info("Charger type set to %s (%s %s)\n", ChargerTypeName(type),
        m_info.chargePointVendor.c_str(), m_info.chargePointModel.c_str());
}

bool OCPPClient::SendDataTransfer(const std::string &vendorId, const std::string &messageId, const std::string &data) {
    json payload = {{"vendorId", vendorId}};

    // messageId and data are both optional per OCPP 1.6. `data` is a free-form
    // string; the Fermata dialect puts a serialised JSON object in it, which is
    // why it is passed through as a string rather than as a nested object.
    if (!messageId.empty())
        payload["messageId"] = messageId;
    if (!data.empty())
        payload["data"] = data;

    return SendOCPPMessage(2, GenerateMessageId(), "DataTransfer", payload);
}

std::string OCPPClient::BuildCustomMeterValuesData() const {
    const V2GState &v = m_info.v2g;

    // ordered_json, not json: the default json is backed by std::map and would
    // emit the keys alphabetically, so the payload would no longer line up with
    // a capture from the real charger. Insertion order below is the FE20's.
    // The unit also emits these as integers, so round rather than letting
    // nlohmann print 7000.0 where the charger sends 7000.
    nlohmann::ordered_json d;
    d["local_mode"]         = v.local_mode;
    d["p_baseline"]         = (long long)llround(v.p_baseline);
    d["q_baseline"]         = (long long)llround(v.q_baseline);
    d["p_max"]              = (long long)llround(v.p_max);
    d["p_min"]              = (long long)llround(v.p_min);
    d["min_soc"]            = (long long)llround(v.min_soc);
    d["max_soc"]            = (long long)llround(v.max_soc);
    d["ev_min_soc"]         = (long long)llround(v.ev_min_soc);
    d["ev_energy_capacity"] = (long long)llround(v.ev_energy_capacity);
    d["session_active"]     = v.session_active;
    d["output_power"]       = (long long)llround(v.output_power);

    return d.dump();
}

bool OCPPClient::SendCustomMeterValues() {
    if (m_info.chargerType != ChargerType::FermataV2G) {
        debug->Warn("SendCustomMeterValues ignored: client is not emulating a Fermata charger\n");
        return false;
    }
    return SendDataTransfer("nu.ame", "customMeterValues", BuildCustomMeterValuesData());
}

bool OCPPClient::SendV2GMeterValues(int connectorId, double socPercent) {
    if (m_info.chargerType != ChargerType::FermataV2G)
        return SendMeterValues(connectorId, m_info.v2g.output_power, socPercent);

    const V2GState &v = m_info.v2g;
    double power = v.output_power;

    m_info.m_meterdata.powerActiveImport = power;
    m_info.m_meterdata.soc = socPercent;

    // Measurand set as sampled from the FE20. Import and export are reported as
    // separate measurands, so a negative (discharging) output goes out as
    // Power.Active.Export with Import pinned at 0.
    json sampledValue = json::array();
    sampledValue.push_back({{"value", std::to_string((int)llround(socPercent))}, {"measurand", "SoC"},                 {"unit", "Percent"}});
    sampledValue.push_back({{"value", std::to_string(power > 0 ? power : 0.0)},  {"measurand", "Power.Active.Import"}, {"unit", "W"}});
    sampledValue.push_back({{"value", std::to_string(power < 0 ? -power : 0.0)}, {"measurand", "Power.Active.Export"}, {"unit", "W"}});
    sampledValue.push_back({{"value", std::to_string(v.q_baseline > 0 ?  v.q_baseline : 0.0)}, {"measurand", "Power.Reactive.Import"}, {"unit", "var"}});
    sampledValue.push_back({{"value", std::to_string(v.q_baseline < 0 ? -v.q_baseline : 0.0)}, {"measurand", "Power.Reactive.Export"}, {"unit", "var"}});
    sampledValue.push_back({{"value", "28.300001"}, {"measurand", "Temperature"}, {"unit", "Celsius"}});
    // Yes, "W" - the FE20 really does tag Frequency with unit W. Mirrored here
    // so the emulation reproduces the quirk a backend has to cope with.
    sampledValue.push_back({{"value", "49.965004"}, {"measurand", "Frequency"},   {"unit", "W"}});

    json meterValue = {
        {"timestamp", CurrentTimestamp()},
        {"sampledValue", sampledValue}};

    json payload = {
        {"connectorId", connectorId},
        {"transactionId", 0},
        {"meterValue", json::array({meterValue})}};

    return SendOCPPMessage(2, GenerateMessageId(), "MeterValues", payload);
}

void OCPPClient::TickV2G(double dtSeconds) {
    if (m_info.chargerType != ChargerType::FermataV2G || dtSeconds <= 0.0)
        return;

    V2GState &v = m_info.v2g;

    // Outside a session the charger settles back to zero regardless of setpoint.
    double target = 0.0;
    if (v.session_active) {
        target = v.p_baseline;
        double cap = fabs(v.power_limit);
        if (target >  cap) target =  cap;
        if (target < -cap) target = -cap;
        if (target > v.p_max) target = v.p_max;
        if (target < v.p_min) target = v.p_min;
    }

    // First-order ramp so the UI shows the setpoint being tracked rather than
    // jumped to. This models the ramp only - the real unit also sat slightly
    // under its setpoint in steady state (6678 W against a commanded 7000 W).
    const double tau = 2.0;
    double alpha = dtSeconds / (tau + dtSeconds);
    v.output_power += (target - v.output_power) * alpha;
    if (fabs(target - v.output_power) < 1.0)
        v.output_power = target;
}

void OCPPClient::HandleChangeConfiguration(const std::string &messageId, const json &payload) {
    std::string key   = payload.value("key", "");
    std::string value = payload.value("value", "");

    // A generic AC charger has no V2G settings to change. Reporting
    // NotSupported rather than Rejected matches OCPP 1.6, which distinguishes
    // "unknown key" from "known key, bad value".
    if (m_info.chargerType != ChargerType::FermataV2G) {
        debug->Warn("ChangeConfiguration %s=%s not supported by a generic AC charger\n", key.c_str(), value.c_str());
        json response = {{"status", "NotSupported"}};
        SendOCPPMessage(3, messageId, "", response);
        return;
    }

    // strtod, not std::stod: with -fno-exceptions a bad value would abort
    // instead of throwing std::invalid_argument.
    const char *begin = value.c_str();
    char *end = nullptr;
    double numeric = strtod(begin, &end);
    bool isNumeric = (end != begin && *end == 0);

    const char *status = "NotSupported";
    if (key == "PBaseline") {
        if (isNumeric) { m_info.v2g.p_baseline = numeric; status = "Accepted"; }
        else           { status = "Rejected"; }
    } else if (key == "QBaseline") {
        if (isNumeric) { m_info.v2g.q_baseline = numeric; status = "Accepted"; }
        else           { status = "Rejected"; }
    } else if (key == "PowerLimit") {
        if (isNumeric) { m_info.v2g.power_limit = numeric; status = "Accepted"; }
        else           { status = "Rejected"; }
    } else if (key == "LocalMode") {
        m_info.v2g.local_mode = (value == "true" || value == "1");
        status = "Accepted";
    }

    debug->Info("ChangeConfiguration %s=%s -> %s\n", key.c_str(), value.c_str(), status);

    json response = {{"status", status}};
    SendOCPPMessage(3, messageId, "", response);
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

            // Parse the status line ("HTTP/1.1 <code> <reason>") rather than
            // grepping for "101 Switching Protocols" - the reason phrase is not
            // fixed by the RFC, and a real status code is what we want to report
            // when the answer is something like 404.
            size_t lineEnd = m_receiveBuffer.find("\r\n");
            std::string statusLine = (lineEnd == std::string::npos)
                ? m_receiveBuffer : m_receiveBuffer.substr(0, lineEnd);

            int    code = 0;
            std::string reason;
            size_t sp = statusLine.find(' ');
            if (sp != std::string::npos) {
                // strtol, not std::stoi: -fno-exceptions turns a throw into abort().
                const char *begin = statusLine.c_str() + sp + 1;
                char *end = nullptr;
                long parsed = strtol(begin, &end, 10);
                if (end != begin) {
                    code = (int)parsed;
                    while (*end == ' ') end++;
                    reason = end;
                }
            }

            if (code == 101) {
                debug->Info("WebSocket handshake successful\n");
                m_info.websocketHandshakeComplete = true;
            } else {
                // Note: deliberately not calling Disconnect() here — this runs
                // on the receive thread itself, and Disconnect() joins that
                // same thread (deadlock). The flag is what lets the UI thread
                // notice and tear the socket down.
                m_info.handshakeFailed = true;
                m_info.handshakeStatusCode = code;
                if (code != 0) {
                    m_info.handshakeError = std::to_string(code);
                    if (!reason.empty())
                        m_info.handshakeError += " " + reason;
                } else {
                    m_info.handshakeError = "server did not answer with an HTTP status line";
                }

                // Keep the body - a backend that rejects the path usually says so.
                size_t bodyStart = m_receiveBuffer.find("\r\n\r\n");
                if (bodyStart != std::string::npos)
                    m_info.handshakeBody = m_receiveBuffer.substr(bodyStart + 4);

                debug->Err("WebSocket handshake rejected: %s%s%s\n",
                    m_info.handshakeError.c_str(),
                    m_info.handshakeBody.empty() ? "" : " - ",
                    m_info.handshakeBody.c_str());
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
        } else if (action == "ChangeConfiguration") {
            HandleChangeConfiguration(messageId, payload);
        } else if (action == "ChangeAvailability") {
            HandleChangeAvailability(messageId, payload);
        } else {
            json response = {{"status", "Accepted"}};
            SendOCPPMessage(3, messageId, "", response);
        }
    }
}
