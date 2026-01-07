#include "HTTPServer.h"
#include "Debug.h"
#include <sstream>
#include <iomanip>
#include <wincrypt.h>
#include "File.h"
#include <thread>
#include <ctime>

//See https://github.com/gennadiygnezdilov/ocpp-1.6J-example-request-response/tree/main
//For examples

static Debugger* http_debug = new Debugger("HTTPServer", DEBUG_TRACE);

HTTPServer::HTTPServer(int port)
	: TCPServer(port)
	, m_fileWatcher(nullptr)
{
	InitializeCriticalSection(&m_wsLock);

	// Try to load HTML from disk first (common locations). If not found, fall back to built-in default.
	if (LoadHTMLFromFile("data/index.html")){
		http_debug->Info("Using HTML loaded from file\n");

		// Start watching the HTML file for changes
		m_fileWatcher = new FileWatcher();
		m_fileWatcher->WatchFile(m_htmlFilePath, [this](const std::string& filePath) {
			http_debug->Info("HTML file changed, reloading: %s\n", filePath.c_str());
			LoadHTMLFromFile(filePath);

			// Notify WebSocket clients about the file change
			SetVariable("fileChanged", "true");
			SetVariable("fileChangedPath", filePath);

			// Get current timestamp
			time_t now = time(nullptr);
			char timeStr[64];
			strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", localtime(&now));
			SetVariable("fileChangedTime", timeStr);
		});
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
	// Stop file watcher
	if (m_fileWatcher)
	{
		delete m_fileWatcher;
		m_fileWatcher = nullptr;
	}

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
	uint8_t* data = LoadFile(filename.c_str(), &sz,true);
	if (!data || sz == 0) {
		http_debug->Trace("LoadHTMLFromFile: not found %s\n", filename.c_str());
		return false;
	}

	// Safe copy into std::string
	m_htmlContent.assign((char*)data, sz);
	m_htmlFilePath = filename;
	http_debug->Info("Loaded HTML from file: %s (%zu bytes)\n", filename.c_str(), sz);
	return true;
}

bool HTTPServer::Start()
{
	// Set callback to handle incoming connections
	SetOnClientConnect([this](SOCKET clientSocket) {
		HandleHTTPConnection(clientSocket);
	});


	// Call parent Start()
	return TCPServer::Start();
}

OCPPClientData* HTTPServer::GetOCPPClientData(SOCKET clientSocket)
{
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	OCPPClientData* result = (it != m_ocppClientData.end()) ? &it->second : nullptr;
	LeaveCriticalSection(&m_wsLock);
	return result;
}

void HTTPServer::HandleHTTPConnection(SOCKET clientSocket){
	http_debug->Info("Spawning thread for client %i\n",clientSocket);
	//We spawn a thread to wait for data from the client.
	u_long blockingMode = 0;
	ioctlsocket(clientSocket, FIONBIO, &blockingMode);
	std::thread th(&HTTPServer::HandleHTTPClient, this, clientSocket);
	th.detach();
}

void HTTPServer::HandleHTTPClient(SOCKET clientSocket){
	char buffer[4096];
	int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

	if (bytesReceived <= 0){
		http_debug->Info("Nothing reveived.\n ");
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
	if (path == "/status"){
		// Build JSON from m_variables
		std::ostringstream jsonstream;
		jsonstream << "{";
		bool first = true;
		for (const auto &p : m_variables)
		{
			if (!first) jsonstream << ",";
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

			jsonstream << '"' << escape(key) << '"' << ":" << '"' << escape(val) << '"';
		}
		jsonstream << "}";

		std::string body = jsonstream.str();
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
	}else if (path == "/set_mode"){	// Support setting the operation mode via /set_mode?mode=<id>
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
	}else if (path == "/set_mode_enabled"){ // Support toggling whether a mode is available
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
	}else{ // Handle websocket upgrade at any other path
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
			//Probablty just a normal HTTP request
			goto handle_get;
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
		if (!chosenProtocol.empty()) {
			m_ocppClients.push_back(clientSocket);

			// Initialize OCPP client data
			OCPPClientData& clientData = m_ocppClientData[clientSocket];
			clientData.socket = clientSocket;
			clientData.path = path;
			clientData.protocol = chosenProtocol;

			// Build ISO8601 UTC timestamp for connection time
			time_t now = time(nullptr);
			struct tm gm;
			gmtime_s(&gm, &now);
			char buf[64];
			strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
			clientData.connectTimestamp = std::string(buf);

			http_debug->Info("OCPP client connected (protocol=%s)\n", chosenProtocol.c_str());
		} else {
			m_wsClients.push_back(clientSocket);
			http_debug->Info("WebSocket client connected\n");
		}
		LeaveCriticalSection(&m_wsLock);

		// Start a reader thread to parse incoming websocket frames from this client
		// Set client socket to blocking mode for the dedicated reader thread (so recv blocks)
		u_long blockingMode = 0;
		ioctlsocket(clientSocket, FIONBIO, &blockingMode);
		std::thread th(&HTTPServer::HandleWebSocketClient, this, clientSocket, path, chosenProtocol);
		th.detach();

		// Send initial variables snapshot
		BroadcastVariables();

		// Do not close the socket here; keep it open for websocket frames
		return;
	}

handle_get:

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

// Helper: read exactly n bytes or return false on error/close
static bool recvAll(SOCKET s, void *buf, size_t len)
{
	char *p = (char*)buf;
	size_t got = 0;
	while (got < len) {
		int r = recv(s, p + got, (int)(len - got), 0);
		if (r > 0) { got += r; continue; }
		if (r == 0) return false; // peer closed
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK || err == WSAEINTR) {
			// no data yet, wait a bit and try again
			Sleep(10);
			continue;
		}
		return false;
	}
	return true;
}

// Minimal websocket frame reader: supports single-frame text messages, masked client frames, ping/pong/close handling
void HTTPServer::HandleWebSocketClient(SOCKET clientSocket, const std::string &path, const std::string &protocol)
{
	http_debug->Info("WebSocket reader started for path=%s protocol=%s\n", path.c_str(), protocol.c_str());
	while (true) {
		unsigned char hdr[2];
		if (!recvAll(clientSocket, hdr, 2)) {
			http_debug->Info("WebSocket client %s disconnected (recv header failed)\n", path.c_str());
			break;
		}
		unsigned char b0 = hdr[0];
		unsigned char b1 = hdr[1];
		bool fin = (b0 & 0x80) != 0;
		unsigned char opcode = b0 & 0x0F;
		bool masked = (b1 & 0x80) != 0;
		uint64_t payloadLen = b1 & 0x7F;

		if (payloadLen == 126) {
			unsigned char ext[2];
			if (!recvAll(clientSocket, ext, 2)) break;
			payloadLen = ((uint64_t)ext[0] << 8) | ext[1];
		} else if (payloadLen == 127) {
			unsigned char ext[8];
			if (!recvAll(clientSocket, ext, 8)) break;
			payloadLen = 0;
			for (int i = 0; i < 8; ++i) payloadLen = (payloadLen << 8) | ext[i];
		}

		unsigned char maskKey[4] = {0,0,0,0};
		if (masked) {
			if (!recvAll(clientSocket, maskKey, 4)) break;
		}

		std::vector<char> payload;
		if (payloadLen > 0) {
			//Had a try-catch here but we've disabled exceptions project-wide
			if (payloadLen > SIZE_MAX) {
				http_debug->Warn("WebSocket payload too large from %s: %llu bytes\n", path.c_str(), payloadLen);
				break;
			}
			payload.resize((size_t)payloadLen);

			if (!recvAll(clientSocket, payload.data(), (size_t)payloadLen)) break;
			if (masked) {
				for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= maskKey[i % 4];
			}
		}

		// Handle opcodes
		if (opcode == 0x1) { // text
			std::string msg(payload.begin(), payload.end());
			http_debug->Info("WS text from %s: %s\n", path.c_str(), msg.c_str());
			// For debug/visibility also set a variable that will be visible via /status
			SetVariable(std::string("ocpp_last_msg_") + path, msg);
			// Try to parse and handle as an OCPP message (e.g., BootNotification)

			if (HandleOCPPMessage(clientSocket, path, msg)){
				http_debug->Info("Handled OCPP message from %s\n", path.c_str());
			}else{
				http_debug->Warn("OCPP message handling failed\n");
			}
		} else if (opcode == 0x8) { // close
			http_debug->Info("WS close received from %s\n", path.c_str());
			break;
		} else if (opcode == 0x9) { // ping - reply pong
			std::vector<unsigned char> frame;
			frame.push_back(0x8A); // FIN=1, pong opcode=0xA
			size_t len = payload.size();
			if (len <= 125) frame.push_back((unsigned char)len);
			else if (len <= 65535) { frame.push_back(126); frame.push_back((len>>8)&0xFF); frame.push_back(len&0xFF); }
			else { frame.push_back(127); for (int i = 7; i >= 0; --i) frame.push_back((len >> (i*8)) & 0xFF); }
			frame.insert(frame.end(), payload.begin(), payload.end());
			send(clientSocket, (const char*)frame.data(), (int)frame.size(), 0);
		} else {
			http_debug->Trace("Unhandled WS opcode %d from %s (len=%llu)\n", (int)opcode, path.c_str(), payloadLen);
		}
	}

	// Cleanup on disconnect
	EnterCriticalSection(&m_wsLock);
	// remove from generic ws list
	for (size_t i = 0; i < m_wsClients.size(); ++i) {
		if (m_wsClients[i] == clientSocket) { m_wsClients.erase(m_wsClients.begin() + i); break; }
	}
	// remove from ocpp list
	for (size_t j = 0; j < m_ocppClients.size(); ++j) {
		if (m_ocppClients[j] == clientSocket) { m_ocppClients.erase(m_ocppClients.begin() + j); break; }
	}
	// remove from ocpp client data map
	m_ocppClientData.erase(clientSocket);
	LeaveCriticalSection(&m_wsLock);

	closesocket(clientSocket);
	DisconnectClient(clientSocket);
	http_debug->Info("WebSocket reader stopped for %s\n", path.c_str());
}

// Very small/simple parser to detect OCPP CALL BootNotification messages and reply with CALLRESULT
bool HTTPServer::HandleOCPPMessage(SOCKET clientSocket, const std::string &path, const std::string &msg)
{
	http_debug->Ok("OCPP Message: %s\n",msg.c_str());
	auto j = json::parse(msg);
	if (!j.is_array() || j.size() < 3) return false;
	int msgType = j[0].get<int>();
	if (msgType != 2) return false; // not a CALL
	std::string msgId = j[1].is_string() ? j[1].get<std::string>() : j[1].dump();
	std::string action = j[2].get<std::string>();
	if (action == "BootNotification") {
		return HandleOCPPBootNotification(clientSocket, path, msgId, j);
	}
	if (action == "StatusNotification") {
		return HandleOCPPStatusNotification(clientSocket, path, msgId, j);
	}
	if (action == "Heartbeat") {
		return HandleOCPPHeartbeat(clientSocket, path, msgId, j);
	}
	if (action == "Authorize") {
		return HandleOCPPAuthorize(clientSocket, path, msgId, j);
	}
	if (action == "MeterValues") {
		return HandleOCPPMeterValues(clientSocket, path, msgId, j);
	}
	if (action == "StartTransaction") {
		return HandleOCPPStartTransaction(clientSocket, path, msgId, j);
	}
	if (action == "StopTransaction") {
		return HandleOCPPStopTransaction(clientSocket, path, msgId, j);
	}
	if (action == "DataTransfer") {
		return HandleOCPPDataTransfer(clientSocket, path, msgId, j);
	}

	http_debug->Warn("Unhandled OCPP action: %s\n", action.c_str());
	return false;
}

//Message from Heliox/Fermata Energy
//[2,"4928f01b-6dcb-4549-b467-34f3061e9b26","BootNotification",{"chargePointVendor":"Heliox","chargePointModel":"FE20","chargeBoxSerialNumber":"620722003700_2241001115","chargePointSerialNumber":"243401022","firmwareVersion":"1.0.0","iccid":"","imsi":""}]
//Simulator
//[2,"b16dd18e-a33a-41dc-9f0f-e1a1c0b30279","BootNotification",{"chargeBoxSerialNumber":"123456","chargePointModel":"Model","chargePointSerialNumber":"123456","chargePointVendor":"Vendor","firmwareVersion":"1.0","iccid":"","imsi":"","meterSerialNumber":"123456","meterType":""}]
//Authorise
//[2,"f0922aea-da99-4c71-9084-98d43af98a64","Authorize",{"idTag":"6BABE9D2"}]
//
//From Fermata Heliox/Fermata Energy
//[2,"2bf90fe7-298e-488c-818f-2c109e3ac002","DataTransfer",{"vendorId":"nu.ame","messageId":"customMeterValues","data":"{\"local_mode\":false,\"p_baseline\":7000,\"q_baseline\":0,\"p_max\":18326,\"p_min\":-19998,\"min_soc\":0,\"max_soc\":100,\"ev_min_soc\":10,\"ev_energy_capacity\":33,\"session_active\":true,\"output_power\":6678}"}]
//[2,"d4187d9e-3fd3-4c49-84b5-bcb6b4053d44","MeterValues",{"connectorId":1,"transactionId":0,"meterValue":[{"timestamp":"2025-12-29T14:37:27.444Z","sampledValue":[{"value":"68","measurand":"SoC","unit":"Percent"},{"value":"6807.0","measurand":"Power.Active.Import","unit":"W"},{"value":"0","measurand":"Power.Active.Export","unit":"W"},{"value":"72.0","measurand":"Power.Reactive.Import","unit":"var"},{"value":"0","measurand":"Power.Reactive.Export","unit":"var"},{"value":"28.300001","measurand":"Temperature","unit":"Celsius"},{"value":"49.965004","measurand":"Frequency","unit":"W"}]}]}]

//From Eaton
//[2, "c0c9197c-62e4-465d-947b-552f5014294b", "DataTransfer", {"vendorId": "GreenMotion", "messageId": "PlugType", "data": "PlugType.Mode3_Type2_Socket"}]

bool HTTPServer::HandleOCPPBootNotification(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	std::string identity;
	if (j.size() >= 4 && j[3].is_object()){
		if (j[3].contains("chargeBoxIdentity")) identity = j[3]["chargeBoxIdentity"].get<std::string>();
		else if (j[3].contains("chargePointIdentity")) identity = j[3]["chargePointIdentity"].get<std::string>();
	}

	std::string expectedId = path;
	if (!expectedId.empty() && expectedId[0] == '/') expectedId = expectedId.substr(1);

	bool accept = false;
	if (identity.empty() || identity == expectedId){
		accept = true;
	}

	accept = true;

	// Build ISO8601 UTC timestamp
	time_t now = time(nullptr);
	struct tm gm;
	gmtime_s(&gm, &now);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
	std::string ts(buf);

	// Update OCPP client data
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		OCPPClientData& clientData = it->second;
		const json& payload = j[3];
		if (payload.contains("chargePointVendor")) clientData.chargePointVendor = payload["chargePointVendor"].get<std::string>();
		if (payload.contains("chargePointModel")) clientData.chargePointModel = payload["chargePointModel"].get<std::string>();
		if (payload.contains("chargeBoxSerialNumber")) clientData.chargeBoxSerialNumber = payload["chargeBoxSerialNumber"].get<std::string>();
		if (payload.contains("chargePointSerialNumber")) clientData.chargePointSerialNumber = payload["chargePointSerialNumber"].get<std::string>();
		if (payload.contains("firmwareVersion")) clientData.firmwareVersion = payload["firmwareVersion"].get<std::string>();
		if (payload.contains("iccid")) clientData.iccid = payload["iccid"].get<std::string>();
		if (payload.contains("imsi")) clientData.imsi = payload["imsi"].get<std::string>();
		if (payload.contains("meterSerialNumber")) clientData.meterSerialNumber = payload["meterSerialNumber"].get<std::string>();
		if (payload.contains("meterType")) clientData.meterType = payload["meterType"].get<std::string>();
		clientData.chargeBoxIdentity = identity;
		clientData.bootAccepted = accept;
		clientData.bootTimestamp = ts;
	}
	LeaveCriticalSection(&m_wsLock);

	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["status"] = accept ? "Accepted" : "Rejected";
	result["interval"] = accept ? 15 : 0;
	result["currentTime"] = ts;
	resp.push_back(result);

	SendWebSocketMessage(clientSocket, resp.dump());
	http_debug->Ok("Sending Back: %s\n",resp.dump().c_str());
	SetVariable(std::string("ocpp_last_boot_") + path, accept ? "Accepted" : "Rejected");
	http_debug->Info("BootNotification %s for %s (id=%s)\n", accept ? "Accepted" : "Rejected", path.c_str(), identity.c_str());
	return true;
}

bool HTTPServer::HandleOCPPStatusNotification(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		http_debug->Warn("StatusNotification missing payload from %s\n", path.c_str());
		return false;
	}
	const json &p = j[3];
	std::string status;
	std::string errorCode;
	std::string timestamp;
	std::string vendorId;
	std::string vendorErrorCode;
	std::string connectorIdStr;

	if (p.contains("status")) status = p["status"].get<std::string>();
	if (p.contains("errorCode")) errorCode = p["errorCode"].get<std::string>();
	if (p.contains("timestamp")) timestamp = p["timestamp"].get<std::string>();
	if (p.contains("vendorId")) vendorId = p["vendorId"].get<std::string>();
	if (p.contains("vendorErrorCode")) vendorErrorCode = p["vendorErrorCode"].get<std::string>();
	int connectorId = -1;
	if (p.contains("connectorId")) {
		if (p["connectorId"].is_number()) {
			connectorId = p["connectorId"].get<int>();
			connectorIdStr = std::to_string(connectorId);
		} else {
			connectorIdStr = p["connectorId"].get<std::string>();
		}
	}

	// Update OCPP client data
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		OCPPClientData& clientData = it->second;
		if (!status.empty()) clientData.connectorStatus = status;
		if (!errorCode.empty()) clientData.errorCode = errorCode;
		if (!timestamp.empty()) clientData.statusTimestamp = timestamp;
		if (!vendorId.empty()) clientData.vendorId = vendorId;
		if (!vendorErrorCode.empty()) clientData.vendorErrorCode = vendorErrorCode;
		if (connectorId != -1) clientData.connectorId = connectorId;
	}
	LeaveCriticalSection(&m_wsLock);

	// Set variables for visibility via /status
	std::string base = std::string("ocpp_status_") + path;
	if (!status.empty()) SetVariable(base, status);
	if (!errorCode.empty()) SetVariable(base + std::string("_error"), errorCode);
	if (!timestamp.empty()) SetVariable(base + std::string("_ts"), timestamp);
	if (!vendorId.empty()) SetVariable(base + std::string("_vendor"), vendorId);
	if (!vendorErrorCode.empty()) SetVariable(base + std::string("_vendor_err"), vendorErrorCode);
	if (!connectorIdStr.empty()) SetVariable(base + std::string("_connector"), connectorIdStr);

	// Store full JSON payload for debugging
	SetVariable(std::string("ocpp_last_status_") + path, p.dump());

	http_debug->Info("Received OCPP StatusNotification from %s: connector=%s status=%s error=%s\n", path.c_str(), connectorIdStr.c_str(), status.c_str(), errorCode.c_str());

	// Reply with CALLRESULT (empty object) per OCPP convention
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	resp.push_back(json::object());
	SendWebSocketMessage(clientSocket, resp.dump());
	return true;
}

bool HTTPServer::HandleOCPPHeartbeat(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Build ISO8601 UTC timestamp
	time_t now = time(nullptr);
	struct tm gm;
	gmtime_s(&gm, &now);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
	std::string ts(buf);

	// Update OCPP client data
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		it->second.lastHeartbeatTimestamp = ts;
	}
	LeaveCriticalSection(&m_wsLock);

	// Expose via /status and log
	SetVariable(std::string("ocpp_last_heartbeat_") + path, ts);
	http_debug->Info("Heartbeat from %s at %s\n", path.c_str(), ts.c_str());

	// Send CALLRESULT [3, msgId, { currentTime: ts }]
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["currentTime"] = ts;
	resp.push_back(result);
	SendWebSocketMessage(clientSocket, resp.dump());

	return true;
}

bool HTTPServer::HandleOCPPAuthorize(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Extract idTag from payload
	std::string idTag;
	if (j.size() >= 4 && j[3].is_object()){
		if (j[3].contains("idTag")) {
			idTag = j[3]["idTag"].get<std::string>();
		}
	}

	// Build ISO8601 UTC timestamp
	time_t now = time(nullptr);
	struct tm gm;
	gmtime_s(&gm, &now);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
	std::string ts(buf);

	// Update OCPP client data
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		it->second.lastAuthorizedIdTag = idTag;
		it->second.lastAuthorizeTimestamp = ts;
	}
	LeaveCriticalSection(&m_wsLock);

	// Expose via /status and log
	SetVariable(std::string("ocpp_last_authorize_") + path, idTag);
	http_debug->Info("Authorize request from %s for idTag: %s\n", path.c_str(), idTag.c_str());

	// Accept all ID tags - send CALLRESULT [3, msgId, { idTagInfo: { status: "Accepted" } }]
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	json idTagInfo;
	idTagInfo["status"] = "Accepted";
	result["idTagInfo"] = idTagInfo;
	resp.push_back(result);

	SendWebSocketMessage(clientSocket, resp.dump());
	http_debug->Ok("Authorize Accepted for idTag: %s\n", idTag.c_str());

	return true;
}

bool HTTPServer::HandleOCPPMeterValues(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		http_debug->Warn("MeterValues missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract meterValue array
	if (!payload.contains("meterValue") || !payload["meterValue"].is_array()) {
		http_debug->Warn("MeterValues missing meterValue array from %s\n", path.c_str());
		return false;
	}

	double powerActiveImport = 0.0;
	double voltage = 0.0;
	double soc = 0.0;
	bool foundPower = false;
	bool foundVoltage = false;
	bool foundSoC = false;
	std::string timestamp;

	// Iterate through meterValue array (usually contains one entry with timestamp and sampledValues)
	const json &meterValueArray = payload["meterValue"];
	for (const auto &meterValue : meterValueArray) {
		if (!meterValue.is_object()) continue;

		// Extract timestamp if available
		if (meterValue.contains("timestamp") && timestamp.empty()) {
			timestamp = meterValue["timestamp"].get<std::string>();
		}

		// Extract sampledValue array
		if (!meterValue.contains("sampledValue") || !meterValue["sampledValue"].is_array()) continue;

		const json &sampledValueArray = meterValue["sampledValue"];
		for (const auto &sample : sampledValueArray) {
			if (!sample.is_object()) continue;

			std::string measurand;
			if (sample.contains("measurand")) {
				measurand = sample["measurand"].get<std::string>();
			}

			// Look for Power.Active.Import
			if (measurand == "Power.Active.Import" && sample.contains("value")) {
				if (sample["value"].is_number()) {
					powerActiveImport = sample["value"].get<double>();
				} else if (sample["value"].is_string()) {
					powerActiveImport = std::stod(sample["value"].get<std::string>());
				}
				foundPower = true;
			}
			// Look for Voltage
			if (measurand == "Voltage" && sample.contains("value")) {
				if (sample["value"].is_number()) {
					voltage = sample["value"].get<double>();
				} else if (sample["value"].is_string()) {
					voltage = std::stod(sample["value"].get<std::string>());
				}
				foundVoltage = true;
			}

			// Look for SoC
			if (measurand == "SoC" && sample.contains("value")) {
				if (sample["value"].is_number()) {
					soc = sample["value"].get<double>();
				} else if (sample["value"].is_string()) {
					soc = std::stod(sample["value"].get<std::string>());
				}
				foundSoC = true;
			}
		}
	}

	// Update OCPP client data
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		if (foundPower) it->second.powerActiveImport = powerActiveImport;
		if (foundVoltage) it->second.ACVoltage = voltage;
		if (foundSoC) it->second.soc = soc;
		if (!timestamp.empty()) it->second.meterValuesTimestamp = timestamp;
	}
	LeaveCriticalSection(&m_wsLock);

	// Log the values
	if (foundPower || foundSoC) {
		http_debug->Info("MeterValues from %s: Power=%.1fW, Voltage:%.1fV SoC=%.1f%%\n",
			path.c_str(), powerActiveImport, voltage, soc);
	}

	// Set variables for visibility via /status
	if (foundPower) {
		SetVariable(std::string("ocpp_power_") + path, std::to_string(powerActiveImport));
	}
	if (foundVoltage) {
		SetVariable(std::string("ocpp_voltage_") + path, std::to_string(voltage));
	}
	if (foundSoC) {
		SetVariable(std::string("ocpp_soc_") + path, std::to_string(soc));
	}

	// Reply with CALLRESULT (empty object) per OCPP convention
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	resp.push_back(json::object());
	SendWebSocketMessage(clientSocket, resp.dump());

	return true;
}

bool HTTPServer::HandleOCPPStartTransaction(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		http_debug->Warn("StartTransaction missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract required fields from StartTransaction request
	// Required: connectorId, idTag, meterStart, timestamp
	int connectorId = -1;
	std::string idTag;
	int meterStart = 0;
	std::string timestamp;
	std::string reservationId;

	if (payload.contains("connectorId")) {
		if (payload["connectorId"].is_number()) {
			connectorId = payload["connectorId"].get<int>();
		}
	}

	if (payload.contains("idTag") && payload["idTag"].is_string()) {
		idTag = payload["idTag"].get<std::string>();
	}

	if (payload.contains("meterStart")) {
		if (payload["meterStart"].is_number()) {
			meterStart = payload["meterStart"].get<int>();
		}
	}

	if (payload.contains("timestamp") && payload["timestamp"].is_string()) {
		timestamp = payload["timestamp"].get<std::string>();
	}

	if (payload.contains("reservationId")) {
		if (payload["reservationId"].is_number()) {
			reservationId = std::to_string(payload["reservationId"].get<int>());
		} else if (payload["reservationId"].is_string()) {
			reservationId = payload["reservationId"].get<std::string>();
		}
	}

	// Generate a unique transaction ID (simple incrementing counter)
	static int nextTransactionId = 1;
	int transactionId = nextTransactionId++;

	// Build ISO8601 UTC timestamp for current time
	time_t now = time(nullptr);
	struct tm gm;
	gmtime_s(&gm, &now);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
	std::string currentTime(buf);

	// Update OCPP client data
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		it->second.transactionId = transactionId;
		it->second.transactionIdTag = idTag;
		it->second.transactionMeterStart = meterStart;
		it->second.transactionTimestamp = timestamp;
		it->second.transactionReservationId = reservationId;
		it->second.connectorId = connectorId;
	}
	LeaveCriticalSection(&m_wsLock);

	// Set variables for visibility via /status
	SetVariable(std::string("ocpp_transaction_id_") + path, std::to_string(transactionId));
	SetVariable(std::string("ocpp_transaction_idtag_") + path, idTag);
	SetVariable(std::string("ocpp_transaction_meter_start_") + path, std::to_string(meterStart));

	http_debug->Info("StartTransaction from %s: connectorId=%d, idTag=%s, meterStart=%d, transactionId=%d\n",
		path.c_str(), connectorId, idTag.c_str(), meterStart, transactionId);

	// Send CALLRESULT [3, msgId, { transactionId: <id>, idTagInfo: { status: "Accepted" } }]
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["transactionId"] = transactionId;
	json idTagInfo;
	idTagInfo["status"] = "Accepted";
	result["idTagInfo"] = idTagInfo;
	resp.push_back(result);

	SendWebSocketMessage(clientSocket, resp.dump());
	http_debug->Ok("StartTransaction Accepted: transactionId=%d for idTag=%s\n", transactionId, idTag.c_str());

	return true;
}

bool HTTPServer::HandleOCPPStopTransaction(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		http_debug->Warn("StopTransaction missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract required fields from StopTransaction request
	// Required: transactionId, timestamp, meterStop
	// Optional: idTag, reason, transactionData
	int transactionId = -1;
	std::string timestamp;
	int meterStop = 0;
	std::string idTag;
	std::string reason;

	if (payload.contains("transactionId")) {
		if (payload["transactionId"].is_number()) {
			transactionId = payload["transactionId"].get<int>();
		}
	}

	if (payload.contains("timestamp") && payload["timestamp"].is_string()) {
		timestamp = payload["timestamp"].get<std::string>();
	}

	if (payload.contains("meterStop")) {
		if (payload["meterStop"].is_number()) {
			meterStop = payload["meterStop"].get<int>();
		}
	}

	if (payload.contains("idTag") && payload["idTag"].is_string()) {
		idTag = payload["idTag"].get<std::string>();
	}

	if (payload.contains("reason") && payload["reason"].is_string()) {
		reason = payload["reason"].get<std::string>();
	}

	// Build ISO8601 UTC timestamp for current time
	time_t now = time(nullptr);
	struct tm gm;
	gmtime_s(&gm, &now);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
	std::string currentTime(buf);

	// Update OCPP client data (clear transaction info)
	EnterCriticalSection(&m_wsLock);
	auto it = m_ocppClientData.find(clientSocket);
	if (it != m_ocppClientData.end()) {
		// Verify transaction ID matches (optional check)
		if (it->second.transactionId != transactionId && transactionId != -1) {
			http_debug->Warn("StopTransaction transactionId mismatch: expected=%d, received=%d\n",
				it->second.transactionId, transactionId);
		}
		// Clear transaction data
		it->second.transactionId = -1;
		it->second.transactionIdTag.clear();
		it->second.transactionMeterStart = 0;
		it->second.transactionTimestamp.clear();
		it->second.transactionReservationId.clear();
	}
	LeaveCriticalSection(&m_wsLock);

	// Set variables for visibility via /status
	SetVariable(std::string("ocpp_transaction_id_") + path, "-1");
	SetVariable(std::string("ocpp_last_stop_meter_") + path, std::to_string(meterStop));
	SetVariable(std::string("ocpp_last_stop_reason_") + path, reason.empty() ? "None" : reason);
	SetVariable(std::string("ocpp_last_stop_timestamp_") + path, timestamp);

	http_debug->Info("StopTransaction from %s: transactionId=%d, meterStop=%d, reason=%s\n",
		path.c_str(), transactionId, meterStop, reason.empty() ? "None" : reason.c_str());

	// Send CALLRESULT [3, msgId, { idTagInfo: { status: "Accepted" } }]
	// Note: idTagInfo is optional in StopTransaction response, but commonly included
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	if (!idTag.empty()) {
		json idTagInfo;
		idTagInfo["status"] = "Accepted";
		result["idTagInfo"] = idTagInfo;
	}
	resp.push_back(result);

	SendWebSocketMessage(clientSocket, resp.dump());
	http_debug->Ok("StopTransaction Accepted: transactionId=%d, meterStop=%d\n", transactionId, meterStop);

	return true;
}

bool HTTPServer::HandleOCPPDataTransfer(SOCKET clientSocket, const std::string &path, const std::string &msgId, const nlohmann::json &j)
{
	// Expect payload in j[3]
	if (j.size() < 4 || !j[3].is_object()) {
		http_debug->Warn("DataTransfer missing payload from %s\n", path.c_str());
		return false;
	}

	const json &payload = j[3];

	// Extract DataTransfer fields
	// Required: vendorId
	// Optional: messageId, data
	std::string vendorId;
	std::string messageId;
	std::string data;

	if (payload.contains("vendorId") && payload["vendorId"].is_string()) {
		vendorId = payload["vendorId"].get<std::string>();
	}

	if (payload.contains("messageId")) {
		if (payload["messageId"].is_string()) {
			messageId = payload["messageId"].get<std::string>();
		}
	}

	if (payload.contains("data")) {
		if (payload["data"].is_string()) {
			data = payload["data"].get<std::string>();
		} else {
			// If data is not a string, dump it as JSON
			data = payload["data"].dump();
		}
	}

	// Log the DataTransfer message
	http_debug->Info("DataTransfer from %s: vendorId=%s, messageId=%s\n",
		path.c_str(), vendorId.c_str(), messageId.c_str());

	if (!data.empty()) {
		http_debug->Trace("DataTransfer data: %s\n", data.c_str());
	}

	// Set variables for visibility via /status
	SetVariable(std::string("ocpp_datatransfer_vendor_") + path, vendorId);
	SetVariable(std::string("ocpp_datatransfer_msgid_") + path, messageId);
	SetVariable(std::string("ocpp_datatransfer_data_") + path, data);

	// Parse custom data if it's JSON (like the customMeterValues example)
	if (!data.empty() && data[0] == '{') {
		json customData = json::parse(data);
		// Store interesting fields if they exist
		if (customData.contains("session_active")) {
			bool sessionActive = customData["session_active"].get<bool>();
			SetVariable(std::string("ocpp_session_active_") + path, sessionActive ? "true" : "false");
		}
		if (customData.contains("output_power")) {
			int outputPower = customData["output_power"].get<int>();
			SetVariable(std::string("ocpp_custom_power_") + path, std::to_string(outputPower));
		}
	}

	// Send CALLRESULT [3, msgId, { status: "Accepted" }]
	// Optional: can also return data back to the charge point
	json resp = json::array();
	resp.push_back(3);
	resp.push_back(msgId);
	json result;
	result["status"] = "Accepted";
	resp.push_back(result);

	SendWebSocketMessage(clientSocket, resp.dump());
	http_debug->Ok("DataTransfer Accepted from vendor=%s\n", vendorId.c_str());

	return true;
}

bool HTTPServer::SendSetChargingProfile(SOCKET clientSocket, int connectorId, float currentLimit)
{
	// Generate a unique message ID for this request
	static int nextMsgId = 1000;
	std::string msgId = std::to_string(nextMsgId++);

	// Build ISO8601 UTC timestamp for profile start time
	time_t now = time(nullptr);
	struct tm gm;
	gmtime_s(&gm, &now);
	char buf[64];
	strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &gm);
	std::string currentTime(buf);

	// Create SetChargingProfile CALL message [2, msgId, "SetChargingProfile", payload]
	json call = json::array();
	call.push_back(2); // Message type: CALL
	call.push_back(msgId);
	call.push_back("SetChargingProfile");

	// Build the payload
	json payload;
	payload["connectorId"] = connectorId;

	// Create charging profile
	json chargingProfile;
	chargingProfile["chargingProfileId"] = nextMsgId;
	chargingProfile["stackLevel"] = 0;
	chargingProfile["chargingProfilePurpose"] = "TxProfile";
	chargingProfile["chargingProfileKind"] = "Absolute";

	// Create charging schedule
	json chargingSchedule;
	chargingSchedule["chargingRateUnit"] = "A"; // Amperes
	chargingSchedule["startSchedule"] = currentTime;

	// Create schedule period (single period with the current limit)
	json period;
	period["startPeriod"] = 0;
	period["limit"] = currentLimit;

	json periods = json::array();
	periods.push_back(period);
	chargingSchedule["chargingSchedulePeriod"] = periods;

	chargingProfile["chargingSchedule"] = chargingSchedule;
	payload["csChargingProfiles"] = chargingProfile;

	call.push_back(payload);

	// Send the message
	std::string message = call.dump();
	http_debug->Info("Sending SetChargingProfile to connector %d: %.1fA\n", connectorId, currentLimit);
	http_debug->Trace("SetChargingProfile message: %s\n", message.c_str());

	bool result = SendWebSocketMessage(clientSocket, message);
	if (result) {
		http_debug->Ok("SetChargingProfile sent successfully\n");
	} else {
		http_debug->Err("Failed to send SetChargingProfile\n");
	}

	return result;
}
