#ifndef _TCP_SOCKET_
#define _TCP_SOCKET_

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>

#include <stddef.h>
#include <stdint.h>

//A wrapper around socket

class Socket;

typedef enum {
    SOCKET_INVALID,
    SOCKET_DISCONNECTED,
    SOCKET_CONNECTING,
    SOCKET_CONNECTED
}ConnectionState;

class Socket{
public:
    SOCKET sock = INVALID_SOCKET;
    ConnectionState state = SOCKET_INVALID;

    static bool wsainit_done;
    static void WSAInit();

    bool IsConnected();


    void Connect(std::string& hoststr, std::string& portstr);
    void Update();
    int SendString(std::string& string);
    int Receive(char* buffer, size_t buffer_sz);
};

#endif