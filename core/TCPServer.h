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

	// Get connected clients
	std::vector<SOCKET> GetConnectedClients() const;

	// Disconnect a specific client
	void DisconnectClient(SOCKET clientSocket);

private:
	int m_port;
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
