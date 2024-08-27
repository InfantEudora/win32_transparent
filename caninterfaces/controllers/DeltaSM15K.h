#ifndef _CONTROLLER_DELTASM15K_H_
#define _CONTROLLER_DELTASM15K_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

#include <winsock2.h>
#include <ws2tcpip.h>

class DeltaSM15K;

class DeltaSM15K{
public:


    int ipv4[4];
    std::string name;

    int TCPInit();
    int Connect();
};

#endif