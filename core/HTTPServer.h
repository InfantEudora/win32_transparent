#pragma once

#include "TCPServer.h"
#include "FileWatcher.h"
#include <string>
#include <map>
#include <vector>
#include <functional>
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>
#include "tinygltf/json.hpp"
using json = nlohmann::json;

/*
    THE APPLICATION SPEAKING OVER THE WEBSOCKET, if there is one.

    WebSockets are a transport and belong here; what is said over them does not. Until
    2026-09-14 this server had an OCPPServerHandler as a MEMBER - a charge-point protocol
    compiled into the generic HTTP server - and the subprotocol negotiation below named
    "ocpp1.6" and "ocpp2.0" literally. That made OCPP a dependency of anything with an HTTP
    server, and since core/MCPServer stands on this, of every app in the tree: all fourteen
    linked a charge-point client and its 992-line server handler whether or not they had ever
    heard of one.

    So the protocol moved out to its caller (apps/ocpp) and left these four hooks behind. All
    of them are optional; with none set this is a plain WebSocket server that accepts no
    subprotocol and drops what it receives, which is what thirteen of the fourteen apps want.

    THE HANDLER IS CALLED FROM THE CLIENT'S OWN READER THREAD, one per connection - see
    HandleWebSocketClient. Anything it touches that the main thread also touches needs its own
    protection; the server holds no lock across these calls and must not, because a handler
    that sends a reply would deadlock against the one guarding the client list.
*/
struct WebSocketApp{
	//Offered each subprotocol the client listed, in the client's order, until one returns true.
	//The first accepted is the one echoed back in Sec-WebSocket-Protocol. Unset means none is
	//acceptable, and the connection proceeds with no subprotocol at all rather than failing -
	//a browser opening a plain socket to the status page is the common case.
	std::function<bool(const std::string& proposed)> accepts_protocol;

	//A client has completed its upgrade. `protocol` is what accepts_protocol chose, or empty.
	std::function<void(SOCKET client, const std::string& path, const std::string& protocol)> on_open;

	//One complete text frame. Returning false means "not mine" and is logged, not an error -
	//a websocket carrying an application protocol will still see pings and stray frames.
	std::function<bool(SOCKET client, const std::string& path, const std::string& message)> on_message;

	//The connection is going away. Called exactly once per on_open, including on a hard drop.
	std::function<void(SOCKET client)> on_close;
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

	// Install the application that speaks over the websocket - see WebSocketApp above.
	// Set it before Start(): it is read from the client reader threads, and installing one
	// while connections are live would be a data race for no useful purpose.
	void SetWebSocketApp(const WebSocketApp& app);

	// Send a text frame to one client. PUBLIC because the application needs it to reply -
	// it is the other half of on_message, and the whole reason the hooks above are useful.
	bool SendWebSocketMessage(SOCKET client, const std::string &message);

private:
	// What is spoken over the websocket, if anything. Empty by default.
	WebSocketApp m_ws_app;

	std::string m_htmlContent;
	std::string m_htmlFilePath;
	std::map<std::string, std::string> m_variables;

	// WebSocket clients
	std::vector<SOCKET> m_wsClients;

	CRITICAL_SECTION m_wsLock;

	// File watcher for HTML file changes
	FileWatcher* m_fileWatcher;

	// Broadcast JSON to all websocket clients
	void BroadcastVariables();

	// Write one complete HTTP response - status line, content type, length, body - and send it.
	void SendHTTPResponse(SOCKET clientSocket, int statusCode, const char *statusText,
	                      const char *contentType, const std::string &body);

	// Handle HTTP requests from clients
	void HandleHTTPConnection(SOCKET clientSocket);

	// Per client reader that waits for HTTP trafic.
	void HandleHTTPClient(SOCKET clientSocket);

	// Per-WebSocket client reader that decodes client frames and handles simple opcodes
	void HandleWebSocketClient(SOCKET clientSocket, const std::string &path, const std::string &protocol);

	// Parse HTTP request
	std::string ParseHTTPRequest(const std::string& request);

	// Replace all variables in HTML with their values
	std::string ReplaceVariables(const std::string& html);
};
