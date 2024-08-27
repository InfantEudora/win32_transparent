#include <stdio.h>
#include "DeltaSM15K.h"

#include "Debug.h"
static Debugger *debug = new Debugger("DeltaSM15K", DEBUG_ALL);

int DeltaSM15K::TCPInit(){
    /* Initialise Windows Socket API */
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        debug->Err("WSAStartup() returned error code %d\n", (unsigned int) GetLastError());
        errno = EIO;
        return -1;
    }
    return 0;
}

int DeltaSM15K::Connect(){
    TCPInit();
    int iResult;

    SOCKET ConnectSocket = INVALID_SOCKET;

    struct addrinfo *result = NULL;
    struct addrinfo *ptr = NULL;
    struct addrinfo hints;

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string ipstring = "192.168.0.104";
    std::string portstring = "8462";

    // Resolve the server address and port
    iResult = getaddrinfo(ipstring.c_str(), portstring.c_str(), &hints, &result);
    if ( iResult != 0 ) {
        debug->Err("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    debug->Info("Connecting to %s:%s\n",ipstring.c_str(),portstring.c_str());

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype,
            ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            debug->Err("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        debug->Err("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }

    debug->Ok("Connected to Delta\n");




    return 0;
}