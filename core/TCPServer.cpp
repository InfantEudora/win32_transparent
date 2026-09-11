#include "TCPServer.h"
#include <algorithm>

static Debugger* debug = new Debugger("TCPServer", DEBUG_ALL);
static bool g_winsockInitialized = false;


TCPServer::TCPServer(int port): m_port(port)
	, m_serverSocket(INVALID_SOCKET)
	, m_running(false)
	, m_acceptThread(nullptr)
	, m_receiveThread(nullptr)
{
	InitializeCriticalSection(&m_clientsLock);
}

TCPServer::~TCPServer(){
	Stop();
	if (m_acceptThread)	{
		WaitForSingleObject(m_acceptThread, INFINITE);
		CloseHandle(m_acceptThread);
	}
	if (m_receiveThread)	{
		WaitForSingleObject(m_receiveThread, INFINITE);
		CloseHandle(m_receiveThread);
	}
	DeleteCriticalSection(&m_clientsLock);
}

bool TCPServer::InitializeWinsock(){
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

bool TCPServer::Start()
{
	if (m_running)
		return false;



    debug->Info("Initializing Winsock\n");
	if (!InitializeWinsock())
		return false;

	// Create server socket.
	//
	// AF_INET - IPv4 only, and deliberately so: everything that talks to this server is on the
	// same machine, and a dual-stack listener would be extra surface for no gain. The cost is that
	// clients must be pointed at 127.0.0.1 and NOT at "localhost": where localhost resolves to ::1
	// first (the Windows default), every connection pays a failed IPv6 attempt before falling back
	// to IPv4 - about 2 seconds per request, while appearing to work perfectly. That is documented
	// where people will actually hit it, in docs/mcp_server.md.
	m_serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_serverSocket == INVALID_SOCKET)
	{
		debug->Err("socket() failed: %d", WSAGetLastError());
		return false;
	}

	// Claim the port EXCLUSIVELY.
	//
	// This used to set SO_REUSEADDR, which on Windows does not mean what the same name means on
	// Unix. Here it lets a socket bind a port another socket is ALREADY listening on, and which of
	// the two a given connection reaches is indeterminate. The effect was that a second copy of
	// this application started cleanly, logged "TCP Server started on port 8765" exactly like the
	// first, and silently shared the endpoint - so MCP tool calls went to whichever process had
	// bound first. That is worse than a failure: a scripted test then measures a stale build and
	// passes for the wrong reason, which is precisely what happened on 2026-09-11.
	//
	// SO_EXCLUSIVEADDRUSE makes the second bind fail with WSAEADDRINUSE instead, which the caller
	// reports and acts on. Note it does not reintroduce the problem SO_REUSEADDR is usually there
	// to solve: that is about a LISTENING socket being blocked by connections left in TIME_WAIT,
	// and Windows does not block a fresh exclusive bind on those once the owning process is gone.
	int optval = 1;
	if (setsockopt(m_serverSocket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&optval, sizeof(optval)) < 0)
	{
		debug->Err("setsockopt() failed: %d", WSAGetLastError());
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
		return false;
	}

	// Bind socket. Loopback only if the owner asked for it - see SetLoopbackOnly.
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_addr.s_addr = htonl(f_loopback_only ? INADDR_LOOPBACK : INADDR_ANY);
	serverAddr.sin_port = htons(m_port);

	if (bind(m_serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0)
	{
		int bind_error = WSAGetLastError();
		if (bind_error == WSAEADDRINUSE)
		{
			//Worth spelling out rather than printing an error number: with the old SO_REUSEADDR
			//this case did not fail at all, so anyone who hits it now is meeting it for the first
			//time and the cause is almost always the obvious one.
			debug->Err("Port %i is already in use - another instance of this application is "
				"most likely still running. This server will not be available.\n", m_port);
		}
		else
		{
			debug->Err("bind() failed: %d", bind_error);
		}
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
		return false;
	}

	// Listen for connections
	if (listen(m_serverSocket, SOMAXCONN) < 0)
	{
		debug->Err("listen() failed: %d", WSAGetLastError());
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
		return false;
	}

	// Set socket to non-blocking mode
	u_long mode = 1;
	if (ioctlsocket(m_serverSocket, FIONBIO, &mode) != 0)
	{
		debug->Err("ioctlsocket() failed: %d", WSAGetLastError());
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
		return false;
	}

	m_running = true;

	// Start accept thread
    debug->Info("Starting AcceptConnections thread\n");
	m_acceptThread = CreateThread(
		NULL,
		0,
		AcceptConnectionsThread,
		(LPVOID)this,
		0,
		NULL
	);

	if (!m_acceptThread)
	{
		debug->Err("CreateThread failed: %d", GetLastError());
		m_running = false;
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
		return false;
	}

	// Start receive thread
	debug->Info("Starting ReceiveClientData thread\n");
	m_receiveThread = CreateThread(
		NULL,
		0,
		ReceiveClientDataThread,
		(LPVOID)this,
		0,
		NULL
	);

	if (!m_receiveThread)
	{
		debug->Err("CreateThread failed for receive: %d", GetLastError());
		m_running = false;
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
		return false;
	}

	debug->Info("TCP Server started on port %d\n", m_port);

	return true;
}

void TCPServer::Stop()
{
	if (!m_running)
		return;

	m_running = false;

	// Close server socket
	if (m_serverSocket != INVALID_SOCKET)
	{
		closesocket(m_serverSocket);
		m_serverSocket = INVALID_SOCKET;
	}

	// Wait for accept thread to finish
	if (m_acceptThread)
	{
		WaitForSingleObject(m_acceptThread, INFINITE);
		CloseHandle(m_acceptThread);
		m_acceptThread = nullptr;
	}

	// Wait for receive thread to finish
	if (m_receiveThread)
	{
		WaitForSingleObject(m_receiveThread, INFINITE);
		CloseHandle(m_receiveThread);
		m_receiveThread = nullptr;
	}

	// Close all connected clients
	EnterCriticalSection(&m_clientsLock);
	for (SOCKET client : m_connectedClients)
	{
		closesocket(client);
	}
	m_connectedClients.clear();
	LeaveCriticalSection(&m_clientsLock);

	debug->Info("TCP Server stopped");
}

bool TCPServer::IsRunning() const
{
	return m_running;
}

void TCPServer::SetOnClientConnect(std::function<void(SOCKET)> callback)
{
	m_onClientConnect = callback;
}

DWORD WINAPI TCPServer::AcceptConnectionsThread(LPVOID param)
{
	TCPServer* pThis = (TCPServer*)param;
	pThis->AcceptConnections();
	return 0;
}

DWORD WINAPI TCPServer::ReceiveClientDataThread(LPVOID param)
{
	TCPServer* pThis = (TCPServer*)param;
	pThis->ReceiveClientData();
	return 0;
}

void TCPServer::AcceptConnections(){
    DWORD thread_id = GetCurrentThreadId();
    debug->Info("AcceptConnections Thread ID: %lu\n",thread_id);
    debug->Info("m_running = %d\n", (int)m_running);
    debug->Info("m_serverSocket = %lld\n", (long long)m_serverSocket);


	while (m_running){
		sockaddr_in clientAddr;
		int clientAddrLen = sizeof(clientAddr);

		SOCKET clientSocket = accept(m_serverSocket, (sockaddr*)&clientAddr, &clientAddrLen);

		if (clientSocket == INVALID_SOCKET){
			// No connection available (expected in non-blocking mode)
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK){
				// No pending connections, sleep briefly to avoid busy-waiting
				Sleep(10);
				continue;
			}else{
				// Socket was closed or real error occurred
				continue;
			}
		}

		// Get client IP address
		char clientIP[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

		debug->Trace("Client connected from %s:%d\n", clientIP, ntohs(clientAddr.sin_port));


		// Call callback if set
		if (m_onClientConnect){
            debug->Trace("Calling client connect callback\n");
			m_onClientConnect(clientSocket);
		} else {
			// No callback set, keep the socket connected
            debug->Info("keeping client socket connected\n");
			EnterCriticalSection(&m_clientsLock);
			m_connectedClients.push_back(clientSocket);
			LeaveCriticalSection(&m_clientsLock);
		}
	}
}

void TCPServer::ReceiveClientData(){
	while (m_running){
		EnterCriticalSection(&m_clientsLock);
		std::vector<SOCKET> clients = m_connectedClients;
		LeaveCriticalSection(&m_clientsLock);
		//debug->Info("Receiving Client data for %i clients\n",clients.size());

		for (size_t i = 0; i < clients.size(); ++i)
		{
			SOCKET clientSocket = clients[i];
			char buffer[1024];
			int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);

			if (bytesReceived > 0)
			{
				// Null-terminate the received data
				buffer[bytesReceived] = '\0';

				// Get client IP for logging
				sockaddr_in clientAddr;
				int clientAddrLen = sizeof(clientAddr);
				getpeername(clientSocket, (sockaddr*)&clientAddr, &clientAddrLen);
				char clientIP[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

				debug->Info("Data from %s:%d: %s\n", clientIP, ntohs(clientAddr.sin_port), buffer);
			}
			else if (bytesReceived == 0)
			{
				// Connection closed by client
				sockaddr_in clientAddr;
				int clientAddrLen = sizeof(clientAddr);
				getpeername(clientSocket, (sockaddr*)&clientAddr, &clientAddrLen);
				char clientIP[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);

				debug->Info("Client %s:%d disconnected\n", clientIP, ntohs(clientAddr.sin_port));
				DisconnectClient(clientSocket);
			}
			else
			{
				// Error or WSAEWOULDBLOCK (no data available)
				int err = WSAGetLastError();
				if (err != WSAEWOULDBLOCK && err != 0)
				{
					debug->Warn("recv() error: %d\n", err);
					DisconnectClient(clientSocket);
				}
			}
		}

		Sleep(10);
	}
}

std::vector<SOCKET> TCPServer::GetConnectedClients() const
{
	std::vector<SOCKET> result;
	EnterCriticalSection((CRITICAL_SECTION*)&m_clientsLock);
	result = m_connectedClients;
	LeaveCriticalSection((CRITICAL_SECTION*)&m_clientsLock);
	return result;
}

void TCPServer::DisconnectClient(SOCKET clientSocket)
{
	EnterCriticalSection(&m_clientsLock);
	auto it = std::find(m_connectedClients.begin(), m_connectedClients.end(), clientSocket);
	if (it != m_connectedClients.end())
	{
		closesocket(*it);
		m_connectedClients.erase(it);
	}
	LeaveCriticalSection(&m_clientsLock);
}
