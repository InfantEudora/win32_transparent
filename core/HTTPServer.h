#pragma once

#include "TCPServer.h"
#include <string>
#include <map>
#include <vector>
#include <winsock2.h>
#include <windows.h>
#include <wincrypt.h>

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

private:
	std::string m_htmlContent;
	std::map<std::string, std::string> m_variables;

	// WebSocket clients
	std::vector<SOCKET> m_wsClients;
	// OCPP-specific websocket clients (subset of m_wsClients) - sockets speaking ocpp1.6/ocpp2.0
	std::vector<SOCKET> m_ocppClients;
	CRITICAL_SECTION m_wsLock;

	// Send raw websocket text message to a client
	bool SendWebSocketMessage(SOCKET client, const std::string &message);

	// Broadcast JSON to all websocket clients
	void BroadcastVariables();

	// Handle HTTP requests from clients
	void HandleHTTPRequest(SOCKET clientSocket);

	// Parse HTTP request
	std::string ParseHTTPRequest(const std::string& request);

	// Replace all variables in HTML with their values
	std::string ReplaceVariables(const std::string& html);
};
