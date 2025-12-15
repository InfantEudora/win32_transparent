#include "HTTPServer.h"
#include "Debug.h"
#include <sstream>
#include <iomanip>
#include <wincrypt.h>
#include "File.h"

static Debugger* http_debug = new Debugger("HTTPServer", DEBUG_INFO);

HTTPServer::HTTPServer(int port)
	: TCPServer(port)
{
	InitializeCriticalSection(&m_wsLock);

	// Try to load HTML from disk first (common locations). If not found, fall back to built-in default.
	if (LoadHTMLFromFile("data/index.html")){
		http_debug->Info("Using HTML loaded from file\n");
	} else{
        // Default HTML content
        m_htmlContent =
            "<!DOCTYPE html>\n"
            "<html>\n"
            "<head><title>HTTP Server</title></head>\n"
            "<body>\n"
            "<h1>Welcome to the HTTP Server</h1>\n"
            "<p>This is the default HTML page served by the HTTP server.</p>\n"
            "<p>Use <a href=\"/status\">/status</a> to get server status in JSON format.</p>\n"
            "</body>\n"
            "</html>\n";
        http_debug->Info("Using built-in default HTML content\n");
	}
}

HTTPServer::~HTTPServer()
{
	DeleteCriticalSection(&m_wsLock);
}


void HTTPServer::SetVariable(const std::string& name, const std::string& value)
{
	m_variables[name] = value;
	// Broadcast updated variables to websocket clients
	BroadcastVariables();
}

void HTTPServer::SetHTMLContent(const std::string& html)
{
	m_htmlContent = html;
	http_debug->Info("HTML content updated\n");
}

bool HTTPServer::LoadHTMLFromFile(const std::string& filename)
{
	size_t sz = 0;
	uint8_t* data = LoadFile(filename.c_str(), &sz);
	if (!data || sz == 0) {
		http_debug->Trace("LoadHTMLFromFile: not found %s\n", filename.c_str());
		return false;
	}

	// Safe copy into std::string
	m_htmlContent.assign((char*)data, sz);
	http_debug->Info("Loaded HTML from file: %s (%zu bytes)\n", filename.c_str(), sz);
	return true;
}

bool HTTPServer::Start()
{
	// Set callback to handle incoming connections
	SetOnClientConnect([this](SOCKET clientSocket) {
		HandleHTTPRequest(clientSocket);
	});

	// Call parent Start()
	return TCPServer::Start();
}

void HTTPServer::HandleHTTPRequest(SOCKET clientSocket)
{
	char buffer[4096];
	int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

	if (bytesReceived <= 0)
	{
		// nothing received or error
		closesocket(clientSocket);
		DisconnectClient(clientSocket);
		return;
	}

	buffer[bytesReceived] = '\0';
	std::string request(buffer);

	http_debug->Info("HTTP Request received:\n%s\n", buffer);

	// Determine requested path (keep query string separate)
	std::string fullPath = ParseHTTPRequest(request);
	size_t qpos = fullPath.find('?');
	std::string path = fullPath;
	std::string query;
	if (qpos != std::string::npos) {
		path = fullPath.substr(0, qpos);
		query = fullPath.substr(qpos + 1);
	}
	// Remove trailing slash (treat "/" as root)
	if (path.size() > 1 && path.back() == '/') path.pop_back();


	// If client asked for /status, return JSON of variables
	if (path == "/status")
	{
		// Build JSON from m_variables
		std::ostringstream json;
		json << "{";
		bool first = true;
		for (const auto &p : m_variables)
		{
			if (!first) json << ",";
			first = false;
			// simple JSON string escaping for quotes and backslashes
			std::string key = p.first;
			std::string val = p.second;
			auto escape = [](const std::string &s){
				std::ostringstream o;
				for (auto c : s) {
					switch (c) {
						case '"': o << "\\\""; break;
						case '\\': o << "\\\\"; break;
						case '\b': o << "\\b"; break;
						case '\f': o << "\\f"; break;
						case '\n': o << "\\n"; break;
						case '\r': o << "\\r"; break;
						case '\t': o << "\\t"; break;
						default:
							if ((unsigned char)c < 0x20) {
								o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
							} else {
								o << c;
							}
					}
				}
				return o.str();
			};

			json << '"' << escape(key) << '"' << ":" << '"' << escape(val) << '"';
		}
		json << "}";

		std::string body = json.str();
		std::ostringstream response;
		response << "HTTP/1.1 200 OK\r\n";
		response << "Content-Type: application/json; charset=UTF-8\r\n";
		response << "Content-Length: " << body.length() << "\r\n";
		response << "Connection: close\r\n";
		response << "\r\n";
		response << body;

		std::string responseStr = response.str();
		int bytesSent = send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
		if (bytesSent > 0)
			http_debug->Info("/status response sent (%d bytes)\n", bytesSent);
		else
			http_debug->Err("Failed to send /status response: %d\n", WSAGetLastError());

		closesocket(clientSocket);
		DisconnectClient(clientSocket);
		return;
	}

	// Support setting the operation mode via /set_mode?mode=<id>
	if (path == "/set_mode")
	{
		std::string mode;
		if (!query.empty()){
			// simple parse mode=...
			size_t pos = query.find("mode=");
			if (pos != std::string::npos){
				pos += 5;
				size_t end = query.find('&', pos);
				mode = query.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
			}
		}
		if (!mode.empty()){
			// URL decode simple + and %20 handling (minimal)
			std::string dec;
			for (size_t i=0;i<mode.size();++i){
				char c = mode[i];
				if (c == '+') dec.push_back(' ');
				else if (c == '%' && i + 2 < mode.size()){
					char hi = mode[i+1]; char lo = mode[i+2];
					int v = 0;
					if (hi >= '0' && hi <= '9') v = (hi - '0') << 4; else if (hi >= 'A' && hi <= 'F') v = (hi - 'A' + 10) << 4; else if (hi >= 'a' && hi <= 'f') v = (hi - 'a' + 10) << 4;
					if (lo >= '0' && lo <= '9') v |= (lo - '0'); else if (lo >= 'A' && lo <= 'F') v |= (lo - 'A' + 10); else if (lo >= 'a' && lo <= 'f') v |= (lo - 'a' + 10);
					dec.push_back((char)v);
					i += 2;
				}else dec.push_back(c);
			}
			mode = dec;
			// set variable and broadcast
			SetVariable("operationMode", mode);
			std::string body = "{\"result\":\"ok\",\"mode\":\"" + mode + "\"}";
			std::ostringstream response;
			response << "HTTP/1.1 200 OK\r\n";
			response << "Content-Type: application/json; charset=UTF-8\r\n";
			response << "Content-Length: " << body.length() << "\r\n";
			response << "Connection: close\r\n";
			response << "\r\n";
			response << body;
			std::string responseStr = response.str();
			send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		// bad request
		std::string body = "{\"result\":\"error\",\"reason\":\"missing mode\"}";
		std::ostringstream response;
		response << "HTTP/1.1 400 Bad Request\r\n";
		response << "Content-Type: application/json; charset=UTF-8\r\n";
		response << "Content-Length: " << body.length() << "\r\n";
		response << "Connection: close\r\n";
		response << "\r\n";
		response << body;
		std::string responseStr = response.str();
		send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
		closesocket(clientSocket);
		DisconnectClient(clientSocket);
		return;
	}

	// Support toggling whether a mode is available
	if (path == "/set_mode_enabled"){
		std::string mode;
		std::string enabled;
		if (!query.empty()){
			size_t pos = query.find("mode=");
			if (pos != std::string::npos){ pos += 5; size_t end = query.find('&', pos); mode = query.substr(pos, end==std::string::npos?std::string::npos:end-pos); }
			pos = query.find("enabled=");
			if (pos != std::string::npos){ pos += 8; size_t end = query.find('&', pos); enabled = query.substr(pos, end==std::string::npos?std::string::npos:end-pos); }
		}
		if (!mode.empty() && !enabled.empty()){
			std::string dec;
			for (size_t i=0;i<mode.size();++i){ char c = mode[i]; if (c == '+') dec.push_back(' '); else if (c == '%' && i+2 < mode.size()){ char hi = mode[i+1]; char lo = mode[i+2]; int v = 0; if (hi >= '0' && hi <= '9') v = (hi - '0') << 4; else if (hi >= 'A' && hi <= 'F') v = (hi - 'A' + 10) << 4; else if (hi >= 'a' && hi <= 'f') v = (hi - 'a' + 10) << 4; if (lo >= '0' && lo <= '9') v |= (lo - '0'); else if (lo >= 'A' && lo <= 'F') v |= (lo - 'A' + 10); else if (lo >= 'a' && lo <= 'f') v |= (lo - 'a' + 10); dec.push_back((char)v); i += 2; } else dec.push_back(c); }
			mode = dec;
			std::string val = (enabled == "1" || enabled == "true") ? "1" : "0";
			SetVariable(std::string("mode_") + mode + std::string("_enabled"), val);
			std::string body = "{\"result\":\"ok\",\"mode\":\"" + mode + "\",\"enabled\":\"" + val + "\"}";
			std::ostringstream response;
			response << "HTTP/1.1 200 OK\r\n";
			response << "Content-Type: application/json; charset=UTF-8\r\n";
			response << "Content-Length: " << body.length() << "\r\n";
			response << "Connection: close\r\n";
			response << "\r\n";
			response << body;
			std::string responseStr = response.str();
			send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		std::string body = "{\"result\":\"error\",\"reason\":\"missing params\"}";
		std::ostringstream response;
		response << "HTTP/1.1 400 Bad Request\r\n";
		response << "Content-Type: application/json; charset=UTF-8\r\n";
		response << "Content-Length: " << body.length() << "\r\n";
		response << "Connection: close\r\n";
		response << "\r\n";
		response << body;
		std::string responseStr = response.str();
		send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
		closesocket(clientSocket);
		DisconnectClient(clientSocket);
		return;
	}

	// Handle websocket upgrade at /ws
	if ((path == "/ws") || (path == "/test-cp"))
	{
		// Look for Sec-WebSocket-Key header
		auto findHeader = [&](const std::string &name)->std::string{
			size_t i = request.find(name);
			if (i == std::string::npos) return "";
			i = request.find(':', i);
			if (i == std::string::npos) return "";
			i++;
			// skip whitespace
			while (i < request.size() && (request[i] == ' ' || request[i] == '\t')) i++;
			size_t j = request.find('\r', i);
			if (j == std::string::npos) j = request.find('\n', i);
			if (j == std::string::npos) j = request.size();
			return request.substr(i, j - i);
		};

		std::string key = findHeader("Sec-WebSocket-Key");
		if (key.empty()) {
			http_debug->Warn("WebSocket upgrade request missing Sec-WebSocket-Key\n");
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}

		// See if the client requested an OCPP subprotocol and pick the first we support
		std::string requestedProtocols = findHeader("Sec-WebSocket-Protocol");
		std::string chosenProtocol;
		if (!requestedProtocols.empty()){
			// split by comma
			size_t pos = 0;
			while (pos < requestedProtocols.size()){
				size_t comma = requestedProtocols.find(',', pos);
				std::string token = requestedProtocols.substr(pos, (comma==std::string::npos?requestedProtocols.size():comma)-pos);
				// trim whitespace
				auto l = token.find_first_not_of(" \t\r\n");
				auto r = token.find_last_not_of(" \t\r\n");
				if (l!=std::string::npos && r!=std::string::npos) token = token.substr(l, r-l+1);
				// accept ocpp1.6 or ocpp2.0
				if (token == "ocpp1.6" || token == "ocpp2.0") { chosenProtocol = token; break; }
				if (comma==std::string::npos) break;
				pos = comma + 1;
			}
		}

		// Compute accept
		const char* GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
		std::string combined = key + GUID;

		HCRYPTPROV hProv = 0;
		HCRYPTHASH hHash = 0;
		BYTE hash[20];
		DWORD hashLen = sizeof(hash);

		if (!CryptAcquireContextA(&hProv, NULL, NULL, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
			http_debug->Err("CryptAcquireContext failed: %d\n", GetLastError());
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		if (!CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash)) {
			http_debug->Err("CryptCreateHash failed: %d\n", GetLastError());
			CryptReleaseContext(hProv,0);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		if (!CryptHashData(hHash, (BYTE*)combined.data(), (DWORD)combined.size(), 0)) {
			http_debug->Err("CryptHashData failed: %d\n", GetLastError());
			CryptDestroyHash(hHash); CryptReleaseContext(hProv,0);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashLen, 0)) {
			http_debug->Err("CryptGetHashParam failed: %d\n", GetLastError());
			CryptDestroyHash(hHash); CryptReleaseContext(hProv,0);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		CryptDestroyHash(hHash); CryptReleaseContext(hProv,0);

		// Base64 encode
		DWORD outLen = 0;
		if (!CryptBinaryToStringA(hash, hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &outLen)) {
			http_debug->Err("CryptBinaryToStringA size failed: %d\n", GetLastError());
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		std::string accept;
		accept.resize(outLen);
		if (!CryptBinaryToStringA(hash, hashLen, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, &accept[0], &outLen)) {
			http_debug->Err("CryptBinaryToStringA failed: %d\n", GetLastError());
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		// ensure no trailing nulls
		if (!accept.empty() && accept.back() == '\0') accept.pop_back();

		// Send upgrade response (include Sec-WebSocket-Protocol if we chose one)
		std::ostringstream resp;
		resp << "HTTP/1.1 101 Switching Protocols\r\n";
		resp << "Upgrade: websocket\r\n";
		resp << "Connection: Upgrade\r\n";
		resp << "Sec-WebSocket-Accept: " << accept << "\r\n";
		if (!chosenProtocol.empty()) resp << "Sec-WebSocket-Protocol: " << chosenProtocol << "\r\n";
		resp << "\r\n";

		std::string respStr = resp.str();
		send(clientSocket, respStr.c_str(), (int)respStr.length(), 0);

		// Add to websocket clients list
		EnterCriticalSection(&m_wsLock);
		m_wsClients.push_back(clientSocket);
		if (!chosenProtocol.empty()) {
			m_ocppClients.push_back(clientSocket);
			http_debug->Info("OCPP client connected (protocol=%s)\n", chosenProtocol.c_str());
		} else {
			http_debug->Info("WebSocket client connected\n");
		}
		LeaveCriticalSection(&m_wsLock);

		// Send initial variables snapshot
		BroadcastVariables();

		// Do not close the socket here; keep it open for websocket frames
		return;
	}

	// Parse the request to check if it's a valid HTTP request
	if (request.find("GET") != std::string::npos || request.find("POST") != std::string::npos)
	{
		// Serve a few static files (like /modes.json)
		if (path == "/modes.json") {
			size_t sz = 0;
			uint8_t* data = LoadFile("data/modes.json", &sz);
			if (data && sz > 0) {
				std::string body((char*)data, sz);
				std::ostringstream response;
				response << "HTTP/1.1 200 OK\r\n";
				response << "Content-Type: application/json; charset=UTF-8\r\n";
				response << "Content-Length: " << body.length() << "\r\n";
				response << "Connection: close\r\n";
				response << "\r\n";
				response << body;
				std::string responseStr = response.str();
				send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
				closesocket(clientSocket);
				DisconnectClient(clientSocket);
				return;
			} else {
				std::string body = "{\"error\":\"not found\"}";
				std::ostringstream response;
				response << "HTTP/1.1 404 Not Found\r\n";
				response << "Content-Type: application/json; charset=UTF-8\r\n";
				response << "Content-Length: " << body.length() << "\r\n";
				response << "Connection: close\r\n";
				response << "\r\n";
				response << body;
				std::string responseStr = response.str();
				send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
				closesocket(clientSocket);
				DisconnectClient(clientSocket);
				return;
			}
		}
		// Build HTTP response with replaced variables
		std::string htmlContent = ReplaceVariables(m_htmlContent);

		std::ostringstream response;
		response << "HTTP/1.1 200 OK\r\n";
		response << "Content-Type: text/html; charset=UTF-8\r\n";
		response << "Content-Length: " << htmlContent.length() << "\r\n";
		response << "Connection: close\r\n";
		response << "\r\n";
		response << htmlContent;

		std::string responseStr = response.str();

		// Send response
		int bytesSent = send(clientSocket, responseStr.c_str(), (int)responseStr.length(), 0);
		if (bytesSent > 0)
		{
			http_debug->Info("HTTP Response sent: %d bytes\n", bytesSent);
		}
		else
		{
			http_debug->Err("Failed to send HTTP response: %d\n", WSAGetLastError());
		}
	}
	else
	{
		http_debug->Warn("Invalid HTTP request received\n");
	}

	// Close the client connection after sending response
	closesocket(clientSocket);
	DisconnectClient(clientSocket);
}

std::string HTTPServer::ParseHTTPRequest(const std::string& request)
{
	// Simple parser - extract the request method and path
	std::istringstream iss(request);
	std::string method, path, version;
	iss >> method >> path >> version;

	http_debug->Info("HTTP Method: %s, Path: %s\n", method.c_str(), path.c_str());

	return path;
}

std::string HTTPServer::ReplaceVariables(const std::string& html)
{
	std::string result = html;

	// Replace all variables with their values
	for (const auto& pair : m_variables)
	{
		std::string placeholder = "{{" + pair.first + "}}";
		size_t pos = 0;

		while ((pos = result.find(placeholder, pos)) != std::string::npos)
		{
			result.replace(pos, placeholder.length(), pair.second);
			pos += pair.second.length();
		}
	}

	return result;
}

bool HTTPServer::SendWebSocketMessage(SOCKET client, const std::string &message)
{
	// Build a single-frame unmasked text message (server -> client must NOT mask)
	std::vector<unsigned char> frame;
	frame.push_back(0x81); // FIN=1, opcode=1 (text)

	size_t len = message.size();
	if (len <= 125) {
		frame.push_back((unsigned char)len);
	} else if (len <= 65535) {
		frame.push_back(126);
		frame.push_back((len >> 8) & 0xFF);
		frame.push_back(len & 0xFF);
	} else {
		frame.push_back(127);
		// 8 bytes length, network order
		for (int i = 7; i >= 0; --i) frame.push_back((len >> (i*8)) & 0xFF);
	}

	// append payload
	frame.insert(frame.end(), message.begin(), message.end());

	int res = send(client, (const char*)frame.data(), (int)frame.size(), 0);
	if (res == SOCKET_ERROR) {
		http_debug->Warn("WebSocket send failed: %d\n", WSAGetLastError());
		return false;
	}
	return true;
}

void HTTPServer::BroadcastVariables()
{
	// Build JSON as in /status
	std::ostringstream json;
	json << "{";
	bool first = true;
	for (const auto &p : m_variables)
	{
		if (!first) json << ",";
		first = false;
		// escape
		auto escape = [](const std::string &s){ std::ostringstream o; for (auto c: s) { switch(c){ case '"': o << "\\\""; break; case '\\': o<<"\\\\"; break; case '\b': o<<"\\b"; break; case '\f': o<<"\\f"; break; case '\n': o<<"\\n"; break; case '\r': o<<"\\r"; break; case '\t': o<<"\\t"; break; default: if ((unsigned char)c < 0x20) { o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c; } else o<<c; } } return o.str(); };
		json << '"' << escape(p.first) << '"' << ':' << '"' << escape(p.second) << '"';
	}
	json << "}";
	std::string body = json.str();

	EnterCriticalSection(&m_wsLock);
	for (size_t i = 0; i < m_wsClients.size(); ) {
		SOCKET s = m_wsClients[i];
		bool ok = SendWebSocketMessage(s, body);
		if (!ok) {
			closesocket(s);
			// remove from generic ws list
			m_wsClients.erase(m_wsClients.begin() + i);
			// also remove from ocpp clients list if present
			for (size_t j = 0; j < m_ocppClients.size(); ++j) {
				if (m_ocppClients[j] == s) { m_ocppClients.erase(m_ocppClients.begin() + j); break; }
			}
			continue;
		}
		++i;
	}
	LeaveCriticalSection(&m_wsLock);
}
