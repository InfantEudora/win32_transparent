#include <stdio.h>
#include "Socket.h"

#include "Debug.h"
static Debugger *debug = new Debugger("Socket", DEBUG_ALL);

bool Socket::wsainit_done = false;

void Socket::WSAInit(){
    /* Initialise Windows Socket API */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        debug->Err("WSAStartup() returned error code %d\n", (unsigned int) GetLastError());
        errno = EIO;
    }
    wsainit_done = true;
}

bool Socket::IsConnected(){
    return (state == SOCKET_CONNECTED);
}

void Socket::Connect(std::string& hoststr, std::string& portstr){
    if (!wsainit_done){
        WSAInit();
    }

    int iResult;

    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    iResult = getaddrinfo(hoststr.c_str(), portstr.c_str(), &hints, &result);
    if ( iResult != 0 ) {
        debug->Err("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return ;
    }

    debug->Info("Connecting to %s:%s\n",hoststr.c_str(),portstr.c_str());

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {
        // Create a SOCKET for connecting to server
        sock = socket(ptr->ai_family, ptr->ai_socktype,
            ptr->ai_protocol);
        if (sock == INVALID_SOCKET) {
            debug->Err("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return;
        }

        debug->Info("socket was created\n");

        // This to set socket to nonblocking
        u_long loption = 1;
        ioctlsocket(sock, FIONBIO, &loption);

        // Connect to server.
        iResult = connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen);

        int wsaError = 0;
        if (iResult == SOCKET_ERROR) {
            wsaError = WSAGetLastError();
            if (wsaError == WSAEWOULDBLOCK || wsaError == WSAEINPROGRESS) {
                debug->Trace("WSAEWOULDBLOCK\n");
                state = SOCKET_CONNECTING;
            }else{
                closesocket(sock);
                sock = INVALID_SOCKET;
                state = SOCKET_INVALID;
                continue;
            }
        }
        break;
    }

    freeaddrinfo(result);

    if (sock == INVALID_SOCKET) {
        debug->Err("Unable to connect to server!\n");
        state = SOCKET_INVALID;
        WSACleanup();
        return;
    }

    debug->Info("Connecting to %s:%s in progress...\n",hoststr.c_str(),portstr.c_str());
}

//Does things depending on socket state, like wait for connection or cleanup.
void Socket::Update(){
    if (state == SOCKET_CONNECTING){
        fd_set setW, setE;

        FD_ZERO(&setW);
        FD_SET(sock, &setW);
        FD_ZERO(&setE);
        FD_SET(sock, &setE);

        timeval time_out = {0};
        time_out.tv_sec = 0;
        time_out.tv_usec = 1000;

        int ret = select(0, NULL, &setW, &setE, &time_out);
        if (ret <= 0){
            // select() failed or connection timed out
            closesocket(sock);
            if (ret == 0){
                WSASetLastError(WSAETIMEDOUT);
                state = SOCKET_DISCONNECTED;
                debug->Err("Connection failed\n");
            }
            return;
        }else{
            debug->Ok("Connected\n");
            state = SOCKET_CONNECTED;
            return;
        }

        if (FD_ISSET(sock, &setE)){
            debug->Err("Connection failed\n");
            state = SOCKET_DISCONNECTED;
            // connection failed
            int err = 0;
            //getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, sizeof(err));
            closesocket(sock);
            //WSASetLastError(err);
            return;
        }
    }
}

int Socket::SendString(std::string& string){
    if (state == SOCKET_CONNECTED){
        return send(sock,string.c_str(),string.length(),0);
    }else if (state == SOCKET_CONNECTING){
        debug->Info("Socket is still connecting...\n");
        Update();
    }else{
        debug->Info("Socket is not connected\n");
    }
    return -1;
}

int Socket::Receive(char* buffer, size_t buffer_sz){
    if (state == SOCKET_CONNECTED){
        int res = recv(sock, (char *) buffer, buffer_sz, 0);
        return res;
    }
    return -1;
}