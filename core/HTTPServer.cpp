#include "HTTPServer.h"
#include "Debug.h"
#include <wincrypt.h>
#include "File.h"
#include <thread>
#include <ctime>

//See https://github.com/gennadiygnezdilov/ocpp-1.6J-example-request-response/tree/main
//For examples

static Debugger* http_debug = new Debugger("HTTPServer", DEBUG_TRACE);

HTTPServer::HTTPServer(int port)
	: TCPServer(port)
	, ocpp(
		  [this](SOCKET s, const std::string& msg) { return SendWebSocketMessage(s, msg); },
		  [this](const std::string& name, const std::string& value) { SetVariable(name, value); })
	, m_fileWatcher(nullptr)
{
	InitializeCriticalSection(&m_wsLock);

	// Try to load HTML from disk first (common locations). If not found, fall back to built-in default.
	if (LoadHTMLFromFile("www/index.html")){
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
	// ReadFileToString rather than LoadFile, and both halves of that matter here. The page is
	// edited while the server is running, so it must NOT be served out of a cache that has no
	// way of being told it is stale; and it is allowed not to exist, which LoadFile answers by
	// calling Fatal and ending the process - the "not found" path below could never run.
	std::string contents;
	if (!ReadFileToString(filename.c_str(), contents) || contents.empty()) {
		http_debug->Trace("LoadHTMLFromFile: not found %s\n", filename.c_str());
		return false;
	}

	m_htmlContent = contents;

	/*
	    The RESOLVED path is what gets remembered, not the name we were asked for, because
	    m_htmlFilePath is handed straight to FileWatcher - and a watcher needs a real directory
	    and file on disk, not an asset name like "www/index.html" that only means something to
	    the search path. Storing the name would leave the watcher pointed at a path that does not
	    exist, so the page would load correctly and then silently never hot-reload again.

	    ResolveAssetPath is exposed for exactly this: the caller that has to do something with the
	    file itself rather than just read its bytes. See core/File.h.
	*/
	std::string resolved;
	if (!ResolveAssetPath(filename.c_str(), resolved)) {
		resolved = filename;        //it was readable a moment ago; keep what we were given
	}
	m_htmlFilePath = resolved;

	http_debug->Info("Loaded HTML from file: %s (%zu bytes)\n", resolved.c_str(), m_htmlContent.size());
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

/*
    nlohmann validates UTF-8 as it serialises, and under -fno-exceptions/JSON_NOEXCEPTION a
    failed validation calls std::abort() instead of throwing - the same trap MCPServer.cpp
    documents on the parse side, where it answers it with allow_exceptions=false.

    It matters here because these strings are not ours. /set_mode percent-decodes whatever
    is in the query string straight into a variable, so any byte sequence a remote client
    cares to send ends up in m_variables and then in /status. error_handler_t::replace
    substitutes U+FFFD for an invalid sequence; strict would end the process.
*/
static std::string DumpJSON(const json &j)
{
	return j.dump(-1, ' ', false, json::error_handler_t::replace);
}

/*
    Every endpoint in this file answers the same shape: a status line, a content type, the
    body's length and the body. That was written out longhand ten times, and the copies had
    already started to drift - /style.css was the only one that dropped the send-failure
    logging, and each copy was its own chance to get Content-Length wrong.

    Not an ostringstream. std::string concatenation and std::to_string do the same job here
    without linking the locale and streambuf machinery that <sstream> pulls in; the same
    reasoning that took the stringstreams out of tinygltf. See 3rdparty/makefile.

    The websocket upgrade in HandleHTTPClient deliberately does NOT use this - an upgrade
    carries no body and must not announce a Content-Length.
*/
void HTTPServer::SendHTTPResponse(SOCKET clientSocket, int statusCode, const char *statusText,
                                  const char *contentType, const std::string &body)
{
	std::string response = "HTTP/1.1 ";
	response += std::to_string(statusCode);
	response += " ";
	response += statusText;
	response += "\r\nContent-Type: ";
	response += contentType;
	response += "\r\nContent-Length: ";
	response += std::to_string(body.length());
	response += "\r\nConnection: close\r\n\r\n";
	response += body;

	int bytesSent = send(clientSocket, response.c_str(), (int)response.length(), 0);
	if (bytesSent > 0)
		http_debug->Info("HTTP %d response sent (%d bytes)\n", statusCode, bytesSent);
	else
		http_debug->Err("Failed to send HTTP %d response: %d\n", statusCode, WSAGetLastError());
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
	/*
	    Read until the blank line that ends the headers, rather than taking whatever one recv
	    happened to return.

	    This used to be a single recv of up to 4095 bytes, treated as the whole request. One recv
	    returns one TCP segment's worth of whatever has arrived so far, so a request carrying more
	    header than that - or merely one split across segments, which is legal at any size and is
	    what happens over a real network rather than over loopback - was silently truncated and then
	    misparsed into a 404 or a failed WebSocket upgrade. Intermittent, and easy to blame on the
	    client.

	    Headers only: nothing below this consumes a body, it just parses the request line and looks
	    up header values. MCPServer::HandleHttpConnection runs the same loop and then reads
	    Content-Length bytes as well, because that one does have a body to parse.
	*/
	const size_t max_header_bytes = 65536;
	std::string request;
	char buffer[4096];
	size_t header_end = std::string::npos;

	while (header_end == std::string::npos){
		int bytesReceived = recv(clientSocket, buffer, sizeof(buffer), 0);
		if (bytesReceived <= 0){
			http_debug->Info("Nothing reveived.\n ");
			return;
		}
		request.append(buffer, bytesReceived);
		header_end = request.find("\r\n\r\n");
		if ((header_end == std::string::npos) && (request.size() > max_header_bytes)){
			http_debug->Warn("HTTP request headers passed %llu bytes with no end of headers, dropping connection\n",
				(unsigned long long)max_header_bytes);
			return;
		}
	}

	http_debug->Info("HTTP Request received:\n%.*s\n", (int)header_end, request.c_str());

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
		//m_variables is a map<string,string>, which nlohmann converts to a JSON object
		//directly - forty lines of hand-written escaping replaced by the conversion the
		//library already has, and shared with BroadcastVariables() rather than copied.
		std::string body = DumpJSON(json(m_variables));
		SendHTTPResponse(clientSocket, 200, "OK", "application/json; charset=UTF-8", body);

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
			std::string body = DumpJSON(json{ {"result", "ok"}, {"mode", mode} });
			SendHTTPResponse(clientSocket, 200, "OK", "application/json; charset=UTF-8", body);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		// bad request
		std::string body = DumpJSON(json{ {"result", "error"}, {"reason", "missing mode"} });
		SendHTTPResponse(clientSocket, 400, "Bad Request", "application/json; charset=UTF-8", body);
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
			std::string body = DumpJSON(json{ {"result", "ok"}, {"mode", mode}, {"enabled", val} });
			SendHTTPResponse(clientSocket, 200, "OK", "application/json; charset=UTF-8", body);
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		std::string body = DumpJSON(json{ {"result", "error"}, {"reason", "missing params"} });
		SendHTTPResponse(clientSocket, 400, "Bad Request", "application/json; charset=UTF-8", body);
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
		//Not SendHTTPResponse: an upgrade carries no body and must not send Content-Length.
		std::string respStr =
			"HTTP/1.1 101 Switching Protocols\r\n"
			"Upgrade: websocket\r\n"
			"Connection: Upgrade\r\n"
			"Sec-WebSocket-Accept: " + accept + "\r\n";
		if (!chosenProtocol.empty()) respStr += "Sec-WebSocket-Protocol: " + chosenProtocol + "\r\n";
		respStr += "\r\n";
		send(clientSocket, respStr.c_str(), (int)respStr.length(), 0);

		// Add to websocket clients list
		if (!chosenProtocol.empty()) {
			ocpp.RegisterClient(clientSocket, path, chosenProtocol);
		} else {
			EnterCriticalSection(&m_wsLock);
			m_wsClients.push_back(clientSocket);
			LeaveCriticalSection(&m_wsLock);
			http_debug->Info("WebSocket client connected\n");
		}

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
			// Read fresh and non-fatally, like the page itself: a file served to a remote client
			// must be allowed to be missing (LoadFile answers that by exiting the process, so the
			// 404 below was unreachable) and is edited while the server runs.
			std::string file_body;
			if (ReadFileToString("modes.json", file_body) && !file_body.empty()) {
				std::string body = file_body;
				SendHTTPResponse(clientSocket, 200, "OK", "application/json; charset=UTF-8", body);
				closesocket(clientSocket);
				DisconnectClient(clientSocket);
				return;
			} else {
				std::string body = DumpJSON(json{ {"error", "not found"} });
				SendHTTPResponse(clientSocket, 404, "Not Found", "application/json; charset=UTF-8", body);
				closesocket(clientSocket);
				DisconnectClient(clientSocket);
				return;
			}
		}
		// The main page (www/index.html) references style.css as a sibling file.
		if (path == "/style.css") {
			// Same reasoning as /modes.json above.
			std::string file_body;
			if (ReadFileToString("www/style.css", file_body) && !file_body.empty()) {
				std::string body = file_body;
				SendHTTPResponse(clientSocket, 200, "OK", "text/css; charset=UTF-8", body);
			} else {
				std::string body = "Not found";
				SendHTTPResponse(clientSocket, 404, "Not Found", "text/plain; charset=UTF-8", body);
			}
			closesocket(clientSocket);
			DisconnectClient(clientSocket);
			return;
		}
		// Build HTTP response with replaced variables
		std::string htmlContent = ReplaceVariables(m_htmlContent);

		SendHTTPResponse(clientSocket, 200, "OK", "text/html; charset=UTF-8", htmlContent);
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
	// Simple parser - extract the request method and path from the request line.
	// The whitespace split is done by hand rather than with an istringstream: pulling in
	// <sstream> for it costs the binary the whole locale/streambuf machinery, which is
	// what the rest of this file was cleaned up to avoid. See 3rdparty/makefile.
	//Bounded to the first line. The istringstream this replaced was not: >> skips any
	//whitespace including newlines, so a request line missing its version would quietly
	//take the next header's first word as the path.
	size_t line_end = request.find("\r\n");
	if (line_end == std::string::npos) line_end = request.size();
	size_t method_end = request.find(' ');
	size_t path_end = (method_end == std::string::npos) ? std::string::npos : request.find(' ', method_end + 1);
	if (method_end == std::string::npos || path_end == std::string::npos || path_end > line_end) {
		http_debug->Warn("Malformed HTTP request line\n");
		return "";
	}
	std::string method = request.substr(0, method_end);
	std::string path = request.substr(method_end + 1, path_end - method_end - 1);

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
	//Same object /status serves, over the websocket instead of a GET.
	//
	//This used to be a commented-out copy of /status's hand-rolled builder with a Fatal()
	//standing in for it, which was not the harmless placeholder it looks like: Fatal()
	//calls exit(1), and SetVariable() calls this on every change, so the OCPP app ended
	//its own process the first time anything set a variable or a websocket client
	//connected. Sharing the one-line conversion removes both the duplication and the bug.
	std::string body = DumpJSON(json(m_variables));

	EnterCriticalSection(&m_wsLock);
	for (size_t i = 0; i < m_wsClients.size(); ) {
		SOCKET s = m_wsClients[i];
		bool ok = SendWebSocketMessage(s, body);
		if (!ok) {
			closesocket(s);
			// remove from generic ws list
			m_wsClients.erase(m_wsClients.begin() + i);
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

			if (ocpp.HandleMessage(clientSocket, path, msg)){
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
	LeaveCriticalSection(&m_wsLock);
	ocpp.UnregisterClient(clientSocket);

	closesocket(clientSocket);
	DisconnectClient(clientSocket);
	http_debug->Info("WebSocket reader stopped for %s\n", path.c_str());
}
