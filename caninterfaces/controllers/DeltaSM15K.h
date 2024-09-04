#ifndef _CONTROLLER_DELTASM15K_H_
#define _CONTROLLER_DELTASM15K_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>

#include "Socket.h"
#include "Window.h"

class DeltaSM15K;

class DeltaSM15K{
public:

    DeltaSM15K();


    int ipv4[4];
    std::string name;

    bool  output_enable = false;
    float output_voltage = 0;
    float output_current = 0;
    float output_voltage_set = 0;
    float output_current_set = 0;

    int query_state = 0;

    Socket socket;


    void Connect();
    bool IsConnected();
    void SendDeltaMessage();
    void ReadDeltaMessage();
    void Flush();
    void SetOutputVoltage(float voltage);
    void SetOutputCurrent(float current);
    void EnableOutput(bool enable);
    void SetRemoteState();

    void UpdateDevice();

};

#endif