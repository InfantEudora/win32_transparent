#include "TCPClient.h"

static Debugger* debug = new Debugger("TCPClient", DEBUG_INFO);
static bool g_winsockInitialized = false;

TCPClient::TCPClient()
	: m_clientSocket(INVALID_SOCKET)
	, m_connected(false)
	, m_receiveThread(nullptr)
{
}

TCPClient::~TCPClient()
{
	Disconnect();
	if (m_receiveThread)
	{
		WaitForSingleObject(m_receiveThread, INFINITE);
		CloseHandle(m_receiveThread);
	}
}

bool TCPClient::InitializeWinsock()
{
	if (g_winsockInitialized)
		return true;

	WSADATA wsaData;
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		debug->Err("WSAStartup failed: %d", result);
		return false;
	}

	g_winsockInitialized = true;
	return true;
}

bool TCPClient::Connect(const std::string& host, int port)
{
	if (m_connected)
	{
		debug->Warn("Already connected");
		return false;
	}

	debug->Info("Initializing Winsock\n");
	if (!InitializeWinsock())
		return false;

	// Create client socket
	m_clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_clientSocket == INVALID_SOCKET)
	{
		debug->Err("socket() failed: %d", WSAGetLastError());
		return false;
	}

	// Resolve server address
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);

	// Try to convert IP address string first
	if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1)
	{
		// Not a valid IP, try to resolve hostname
		struct addrinfo hints, *result = nullptr;
		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0)
		{
			debug->Err("getaddrinfo() failed: %d", WSAGetLastError());
			closesocket(m_clientSocket);
			m_clientSocket = INVALID_SOCKET;
			return false;
		}

		serverAddr.sin_addr = ((sockaddr_in*)result->ai_addr)->sin_addr;
		freeaddrinfo(result);
	}

	// Connect to server
	debug->Info("Connecting to %s:%d\n", host.c_str(), port);
	if (connect(m_clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
	{
		debug->Err("connect() failed: %d", WSAGetLastError());
		closesocket(m_clientSocket);
		m_clientSocket = INVALID_SOCKET;
		return false;
	}

	// Set socket to non-blocking mode
	u_long mode = 1;
	if (ioctlsocket(m_clientSocket, FIONBIO, &mode) != 0)
	{
		debug->Err("ioctlsocket() failed: %d", WSAGetLastError());
		closesocket(m_clientSocket);
		m_clientSocket = INVALID_SOCKET;
		return false;
	}

	m_connected = true;

	// Start receive thread
	debug->Info("Starting ReceiveData thread\n");
	m_receiveThread = CreateThread(
		NULL,
		0,
		ReceiveDataThread,
		(LPVOID)this,
		0,
		NULL
	);

	if (!m_receiveThread)
	{
		debug->Err("CreateThread failed: %d", GetLastError());
		m_connected = false;
		closesocket(m_clientSocket);
		m_clientSocket = INVALID_SOCKET;
		return false;
	}

	debug->Info("Connected to %s:%d\n", host.c_str(), port);
	return true;
}

void TCPClient::Disconnect()
{
	if (!m_connected)
		return;

	m_connected = false;

	// Close client socket
	if (m_clientSocket != INVALID_SOCKET)
	{
		closesocket(m_clientSocket);
		m_clientSocket = INVALID_SOCKET;
	}

	// Wait for receive thread to finish
	if (m_receiveThread)
	{
		WaitForSingleObject(m_receiveThread, INFINITE);
		CloseHandle(m_receiveThread);
		m_receiveThread = nullptr;
	}

	debug->Info("Disconnected from server");
}

bool TCPClient::IsConnected() const
{
	return m_connected;
}

bool TCPClient::Send(const char* data, int length)
{
	if (!m_connected)
	{
		debug->Warn("Not connected to server");
		return false;
	}

	int bytesSent = send(m_clientSocket, data, length, 0);
	if (bytesSent == SOCKET_ERROR)
	{
		int err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
		{
			debug->Warn("Send would block, data not sent");
			return false;
		}

		debug->Err("send() failed: %d", err);
		return false;
	}

	if (bytesSent < length)
	{
		debug->Warn("Only sent %d of %d bytes", bytesSent, length);
		return false;
	}

	return true;
}

bool TCPClient::Send(const std::string& data)
{
	return Send(data.c_str(), static_cast<int>(data.length()));
}

void TCPClient::SetOnDataReceived(std::function<void(const char*, int)> callback)
{
	m_onDataReceived = callback;
}

void TCPClient::SetOnDisconnected(std::function<void()> callback)
{
	m_onDisconnected = callback;
}

DWORD WINAPI TCPClient::ReceiveDataThread(LPVOID param)
{
	TCPClient* pThis = (TCPClient*)param;
	pThis->ReceiveData();
	return 0;
}

void TCPClient::ReceiveData()
{
	DWORD thread_id = GetCurrentThreadId();
	debug->Info("ReceiveData Thread ID: %lu\n", thread_id);

	char buffer[1024];

	while (m_connected)
	{
		int bytesReceived = recv(m_clientSocket, buffer, sizeof(buffer) - 1, 0);

		if (bytesReceived > 0)
		{
			// Null-terminate the received data
			buffer[bytesReceived] = '\0';

			debug->Trace("Received %d bytes: %s\n", bytesReceived, buffer);

			// Call callback if set
			if (m_onDataReceived)
			{
				m_onDataReceived(buffer, bytesReceived);
			}
		}
		else if (bytesReceived == 0)
		{
			// Connection closed by server
			debug->Info("Server closed the connection\n");
			m_connected = false;

			// Call disconnect callback
			if (m_onDisconnected)
			{
				m_onDisconnected();
			}
			break;
		}
		else
		{
			// Error or WSAEWOULDBLOCK (no data available)
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK)
			{
				// No data available, sleep briefly to avoid busy-waiting
				Sleep(10);
				continue;
			}

			debug->Warn("recv() error: %d\n", err);
			m_connected = false;

			// Call disconnect callback
			if (m_onDisconnected)
			{
				m_onDisconnected();
			}
			break;
		}
	}

	debug->Info("ReceiveData thread exiting\n");
}
