#ifndef _MCPSERVER_H_
#define _MCPSERVER_H_

#include "TCPServer.h" // must be included before <windows.h> - winsock2.h vs winsock.h conflict, see HTTPServer.h
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include "tinygltf/json.hpp"
using json = nlohmann::json;

// A tool handler receives the "arguments" object from a tools/call request
// and returns either a json string (sent back as-is) or any other json value
// (sent back as its dump()). Exceptions must not be used - the project builds
// with -fno-exceptions.
using MCPToolHandler = std::function<json(const json &arguments)>;

struct MCPTool {
    std::string name;
    std::string description;
    json inputSchema;
    MCPToolHandler handler;
};

// Process-wide MCP (Model Context Protocol) server. Two transports, either or
// both of which can be running at once, share the same tool registry and
// JSON-RPC dispatch (Dispatch()):
//
//  - stdio (Start()): a client spawns this process and owns its stdin/stdout
//    for the life of the connection - closing the pipe (or the client
//    killing the process) ends the app along with the MCP session. Fine for
//    short-lived tools, but it means a long-running graphical app's lifetime
//    is at the mercy of whatever MCP client happens to be attached: there is
//    no way to reconnect a dropped client without relaunching the app.
//
//  - streamable HTTP (StartHttp()): a small stateless JSON-RPC-over-HTTP
//    server built directly on TCPServer (not HTTPServer - this endpoint has
//    no need for HTTPServer's HTML/websocket/OCPP baggage). The app binds a
//    port once at startup and keeps listening independently of any one
//    client; an MCP client (`claude mcp add --transport http ...`) connects
//    whenever it wants, and disconnecting/reconnecting it never touches the
//    app process itself. This is the transport meant for a long-running
//    graphical app like ApplicationTank that a user is also looking at.
//
// Application's constructor call to Start() (kept for other apps) and the
// generic call site in Application.cpp that also calls StartHttp() mean
// every compiled binary in this project gets both transports for free, with
// whatever tools its linked-in modules have registered.
//
// Protocol notes:
//  - stdout carries ONLY newline-delimited JSON-RPC messages. Nothing else
//    may ever be written there (see Debug_win32.cpp, which logs to stderr).
//  - stdin is read on its own thread, since it blocks and every app here
//    already runs its own render/physics threads independently of it.
class MCPServer {
public:
    static MCPServer *Get();

    void RegisterTool(const std::string &name, const std::string &description,
                       const json &inputSchema, MCPToolHandler handler);

    // For a tool handler that wants to hand back an image alongside/instead of its normal
    // JSON result (e.g. a screenshot): wrap your result with this and return it as-is.
    // BuildToolsCall recognizes the reserved key this adds and splits it into a separate
    // MCP "image" content block (base64-encoded) instead of dumping it as text - every
    // other tool's return value is untouched by this.
    static json AttachImagePNG(json result, const std::vector<uint8_t> &png_bytes);

    // Spawns the stdin-reading thread. Safe to call more than once (no-op
    // after the first call).
    void Start();

    // Starts listening for MCP-over-HTTP connections on the given port. Safe
    // to call more than once (no-op after the first call). If the port is
    // already in use - e.g. another instance of this app is already running
    // - logs an error and leaves HTTP transport off rather than failing the
    // whole process; stdio (if started) is unaffected.
    void StartHttp(int port);

private:
    MCPServer();

    static DWORD WINAPI ReaderThreadFunc(LPVOID param);
    void ReaderLoop();

    void HandleLine(const std::string &line);

    // Parses one JSON-RPC message and returns the JSON-RPC response to send
    // back, or a null json() if the message was a notification/needs no
    // response. Shared by both transports.
    json Dispatch(const json &msg);

    json BuildInitializeResult(const json &id);
    json BuildToolsList(const json &id);
    json BuildToolsCall(const json &id, const json &params);
    json BuildResult(const json &id, const json &result);
    json BuildError(const json &id, int code, const std::string &message);

    void SendRaw(const json &message); // stdio transport only

    // HTTP transport: one detached thread per connection, reads a single
    // JSON-RPC request and writes back a single HTTP response.
    void HandleHttpConnection(SOCKET clientSocket);

    std::mutex m_toolsLock;
    std::vector<MCPTool> m_tools;

    HANDLE m_thread = NULL;
    bool m_running = false;

    TCPServer *m_httpServer = nullptr;
};

#endif
