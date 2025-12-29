#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")

#include <functional>
#include <atomic>
#include <string>
#include "Debug.h"

class TCPClient
{
public:
	TCPClient();
	~TCPClient();

	// Connect to a server
	bool Connect(const std::string& host, int port);

	// Disconnect from server
	void Disconnect();

	// Check if connected
	bool IsConnected() const;

	// Send data to server
	bool Send(const char* data, int length);
	bool Send(const std::string& data);

	// Set callback for when data is received (receives data buffer and length)
	void SetOnDataReceived(std::function<void(const char*, int)> callback);

	// Set callback for when disconnected
	void SetOnDisconnected(std::function<void()> callback);

private:
	SOCKET m_clientSocket;
	std::atomic<bool> m_connected = false;
	HANDLE m_receiveThread;
	std::function<void(const char*, int)> m_onDataReceived;
	std::function<void()> m_onDisconnected;

	// Receive data from server (static for WinAPI threading)
	static DWORD WINAPI ReceiveDataThread(LPVOID param);
	void ReceiveData();

	// Initialize Winsock (Windows only)
	bool InitializeWinsock();
};
