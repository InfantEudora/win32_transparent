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
#include "OCPPClient.h"
#include "OCPPServerHandler.h"

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

	// OCPP protocol handling for websocket clients that negotiate an OCPP subprotocol.
	OCPPServerHandler ocpp;

private:
	std::string m_htmlContent;
	std::string m_htmlFilePath;
	std::map<std::string, std::string> m_variables;

	// WebSocket clients
	std::vector<SOCKET> m_wsClients;

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

	// Parse HTTP request
	std::string ParseHTTPRequest(const std::string& request);

	// Replace all variables in HTML with their values
	std::string ReplaceVariables(const std::string& html);
};
