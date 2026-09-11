#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <functional>
#include <atomic>
#include <vector>
#include "Debug.h"

class TCPServer
{
public:
	TCPServer(int port = 8080);
	~TCPServer();

	// Start listening for connections
	virtual bool Start();

	// Stop the server
	void Stop();

	// Check if server is running
	bool IsRunning() const;

	// Set callback for when a client connects (receives socket descriptor)
	void SetOnClientConnect(std::function<void(SOCKET)> callback);

	/*
	    Bind 127.0.0.1 instead of every interface. Call BEFORE Start().

	    Off by default, because two of the three servers in this repo are meant to be reached from
	    the network: ApplicationOCPP serves real chargers and ApplicationTileset serves a browser
	    that may not be on this machine. The MCP server is the opposite - it is a local control
	    channel, its own source comment says "everything that talks to this server is on the same
	    machine", and it was nevertheless listening on 0.0.0.0.
	*/
	void SetLoopbackOnly(bool loopback_only){ f_loopback_only = loopback_only; }

	// Get connected clients
	std::vector<SOCKET> GetConnectedClients() const;

	// Disconnect a specific client
	void DisconnectClient(SOCKET clientSocket);

private:
	int m_port;
	bool f_loopback_only = false;
	SOCKET m_serverSocket;
	std::atomic<bool> m_running = false;
	HANDLE m_acceptThread;
	HANDLE m_receiveThread;
	std::vector<SOCKET> m_connectedClients;
	CRITICAL_SECTION m_clientsLock;
	std::function<void(SOCKET)> m_onClientConnect;

	// Accept incoming connections (static for WinAPI threading)
	static DWORD WINAPI AcceptConnectionsThread(LPVOID param);
	void AcceptConnections();

	// Receive data from clients (static for WinAPI threading)
	static DWORD WINAPI ReceiveClientDataThread(LPVOID param);
	void ReceiveClientData();

	// Initialize Winsock (Windows only)
	bool InitializeWinsock();
};
