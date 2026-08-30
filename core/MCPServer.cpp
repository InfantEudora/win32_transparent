#include "MCPServer.h"
#include "Debug.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <thread>

static Debugger *debug = new Debugger("MCPServer", DEBUG_ALL);

MCPServer::MCPServer() {
    // Always available, even if nothing else in this binary registers a
    // tool - lets any MCP client confirm the process is alive and query it.
    RegisterTool("status", "Report that this process is running.",
        json{ {"type", "object"}, {"properties", json::object()} },
        [](const json & /*arguments*/) -> json {
            return json{ {"status", "running"} };
        });
}

MCPServer *MCPServer::Get() {
    static MCPServer instance;
    return &instance;
}

void MCPServer::RegisterTool(const std::string &name, const std::string &description,
                              const json &inputSchema, MCPToolHandler handler) {
    std::lock_guard<std::mutex> lock(m_toolsLock);
    m_tools.push_back(MCPTool{ name, description, inputSchema, handler });
}

void MCPServer::Start() {
    if (m_running) {
        return;
    }
    m_running = true;
    m_thread = CreateThread(NULL, 0, ReaderThreadFunc, this, 0, NULL);
    if (!m_thread) {
        debug->Err("CreateThread failed for MCP reader: %d\n", (int)GetLastError());
        m_running = false;
        return;
    }
    debug->Info("MCP server listening on stdio\n");
}

DWORD WINAPI MCPServer::ReaderThreadFunc(LPVOID param) {
    ((MCPServer *)param)->ReaderLoop();
    return 0;
}

void MCPServer::ReaderLoop() {
    std::string line;
    char chunk[4096];

    while (m_running) {
        if (!fgets(chunk, sizeof(chunk), stdin)) {
            break; // stdin closed or error
        }
        line += chunk;
        if (line.empty() || line.back() != '\n') {
            continue; // fgets stopped because its buffer filled, not at a line end
        }
        line.pop_back();
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            HandleLine(line);
        }
        line.clear();
    }

    debug->Info("MCP stdin closed, reader thread exiting\n");
}

void MCPServer::HandleLine(const std::string &line) {
    // allow_exceptions=false: with -fno-exceptions/JSON_NOEXCEPTION a throwing
    // parse would std::abort() the whole process on malformed input, which a
    // stdio server can't afford to risk on untrusted/partial input.
    json msg = json::parse(line, nullptr, false);
    if (msg.is_discarded()) {
        debug->Warn("MCP: dropping unparsable line: %s\n", line.c_str());
        return;
    }
    json response = Dispatch(msg);
    if (!response.is_null()) {
        SendRaw(response);
    }
}

json MCPServer::Dispatch(const json &msg) {
    std::string method = msg.value("method", "");
    bool isRequest = msg.contains("id");
    json id = isRequest ? msg["id"] : json();
    json params = msg.value("params", json::object());

    if (method == "initialize") {
        return BuildInitializeResult(id);
    } else if (method == "notifications/initialized") {
        return json(); // no response expected
    } else if (method == "ping") {
        return BuildResult(id, json::object());
    } else if (method == "tools/list") {
        return BuildToolsList(id);
    } else if (method == "tools/call") {
        return BuildToolsCall(id, params);
    } else if (isRequest) {
        return BuildError(id, -32601, "Method not found: " + method);
    }
    return json(); // unknown notification - nothing to reply with
}

json MCPServer::BuildInitializeResult(const json &id) {
    json result = {
        { "protocolVersion", "2024-11-05" },
        { "capabilities", { { "tools", json::object() } } },
        { "serverInfo", { { "name", "win32_transparent" }, { "version", "0.1.0" } } }
    };
    return BuildResult(id, result);
}

json MCPServer::BuildToolsList(const json &id) {
    json tools = json::array();
    {
        std::lock_guard<std::mutex> lock(m_toolsLock);
        for (const MCPTool &tool : m_tools) {
            tools.push_back({
                { "name", tool.name },
                { "description", tool.description },
                { "inputSchema", tool.inputSchema }
            });
        }
    }
    return BuildResult(id, { { "tools", tools } });
}

json MCPServer::BuildToolsCall(const json &id, const json &params) {
    std::string name = params.value("name", "");
    json arguments = params.value("arguments", json::object());

    MCPToolHandler handler = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_toolsLock);
        for (const MCPTool &tool : m_tools) {
            if (tool.name == name) {
                handler = tool.handler;
                break;
            }
        }
    }

    if (!handler) {
        return BuildError(id, -32602, "Unknown tool: " + name);
    }

    json output = handler(arguments);
    std::string text = output.is_string() ? output.get<std::string>() : output.dump();
    json content = json::array({ { { "type", "text" }, { "text", text } } });
    return BuildResult(id, { { "content", content }, { "isError", false } });
}

json MCPServer::BuildResult(const json &id, const json &result) {
    return json{ { "jsonrpc", "2.0" }, { "id", id }, { "result", result } };
}

json MCPServer::BuildError(const json &id, int code, const std::string &message) {
    return json{ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", message } } } };
}

void MCPServer::SendRaw(const json &message) {
    // stdout carries only MCP frames - never write debug/log text here.
    std::string out = message.dump();
    fputs(out.c_str(), stdout);
    fputc('\n', stdout);
    fflush(stdout);
}

// ---------------------------------------------------------------------------
// HTTP transport
//
// Deliberately minimal: one JSON-RPC message per POST body, one JSON-RPC
// response (or a bare 202 for notifications) per HTTP response, connection
// closed afterwards. No sessions, no SSE, no keep-alive - MCP's streamable-
// HTTP transport allows a stateless server exactly like this, and every tool
// call here is a synchronous request/response with no server-initiated
// messages to deliver, so there's nothing a persistent stream would buy.
// ---------------------------------------------------------------------------

void MCPServer::StartHttp(int port) {
    if (m_httpServer) {
        return;
    }
    m_httpServer = new TCPServer(port);
    m_httpServer->SetOnClientConnect([this](SOCKET clientSocket) {
        std::thread th(&MCPServer::HandleHttpConnection, this, clientSocket);
        th.detach();
    });
    if (!m_httpServer->Start()) {
        debug->Err("MCP HTTP transport failed to bind port %d - is another instance already running?\n", port);
        delete m_httpServer;
        m_httpServer = nullptr;
        return;
    }
    debug->Info("MCP server listening on http://localhost:%d/mcp\n", port);
}

// Case-insensitive substring search - HTTP header names are case-insensitive
// and MCP clients aren't guaranteed to send "Content-Length" with that exact
// casing.
static size_t FindHeaderCI(const std::string &headers, const char *name) {
    std::string lower = headers;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
    return lower.find(lowerName); // same length/positions as the original
}

// Blocking-mode read helper - mirrors HTTPServer.cpp's recvAll.
static bool RecvAll(SOCKET s, char *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        int r = recv(s, buf + got, (int)(len - got), 0);
        if (r > 0) { got += r; continue; }
        return false; // peer closed or real error
    }
    return true;
}

void MCPServer::HandleHttpConnection(SOCKET clientSocket) {
    // TCPServer hands off a non-blocking socket; switch to blocking so the
    // reads below can just wait for data instead of spin-polling.
    u_long blockingMode = 0;
    ioctlsocket(clientSocket, FIONBIO, &blockingMode);

    // Read until the blank line ending the headers. MCP request bodies are
    // tiny (a handful of JSON fields), so a generous size cap is simpler
    // than a growable protocol-aware reader and can't be hit by legitimate
    // traffic.
    std::string request;
    char chunk[4096];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int r = recv(clientSocket, chunk, sizeof(chunk), 0);
        if (r <= 0) {
            closesocket(clientSocket);
            return;
        }
        request.append(chunk, r);
        headerEnd = request.find("\r\n\r\n");
        if (request.size() > 65536) {
            debug->Warn("MCP HTTP: request too large without end of headers, dropping connection\n");
            closesocket(clientSocket);
            return;
        }
    }

    std::string headers = request.substr(0, headerEnd);
    std::string body = request.substr(headerEnd + 4);

    size_t contentLength = 0;
    size_t pos = FindHeaderCI(headers, "content-length:");
    if (pos != std::string::npos) {
        contentLength = (size_t)strtoul(headers.c_str() + pos + strlen("content-length:"), nullptr, 10);
    }

    if (body.size() < contentLength) {
        size_t remaining = contentLength - body.size();
        std::vector<char> extra(remaining);
        if (!RecvAll(clientSocket, extra.data(), remaining)) {
            closesocket(clientSocket);
            return;
        }
        body.append(extra.data(), extra.size());
    }

    json msg = json::parse(body, nullptr, false);

    int statusCode;
    const char *statusText;
    std::string responseBody;

    if (msg.is_discarded()) {
        debug->Warn("MCP HTTP: dropping unparsable body: %s\n", body.c_str());
        statusCode = 400;
        statusText = "Bad Request";
    } else {
        json response = Dispatch(msg);
        if (response.is_null()) {
            // Notification - no JSON-RPC response, but the HTTP request
            // still needs an HTTP response to complete.
            statusCode = 202;
            statusText = "Accepted";
        } else {
            statusCode = 200;
            statusText = "OK";
            responseBody = response.dump();
        }
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    out << "Content-Type: application/json\r\n";
    out << "Content-Length: " << responseBody.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << responseBody;

    std::string outStr = out.str();
    send(clientSocket, outStr.c_str(), (int)outStr.size(), 0);
    closesocket(clientSocket);
}
