#ifndef _CONTROLLER_INFYPOWER_H_
#define _CONTROLLER_INFYPOWER_H_

#include <stddef.h>
#include <stdint.h>
#include <vector>
#include <string>
#include "../canframe.h"

class InfyPower;

struct InfyPower_CanMessageHeader{
    uint8_t source;
    uint8_t destination;
    uint8_t command_no : 6;
    uint8_t device_no : 4;
    uint8_t error_code: 3;
};




class InfyPower{



public:
    int bus_id;
    bool HandleCANMessage(can_frame_t* frame); //Decodes can message traffic for information

    static void BroadcastSetDialMode(can_frame_t& frame);

    //For testing purposes:
    void GenerateTestMessages(); //This fills the CAN Queue with some predefined test  messages.
    void CreateCANHeader(InfyPower_CanMessageHeader& header, uint8_t source, uint8_t destination,uint8_t command_no);
    static float ParseFloat(uint8_t* data);
    static uint16_t ParseU16(uint8_t* data);

    //Device state
    bool enabled = false;
    float input_voltage = -1;
    float output_voltage = -1;
    float external_voltage = -1;    // Voltage after the output diode...
    float output_current = 0;
    float available_current = 0;
    float ambient_temperature = -1;

    uint8_t module_state_0 = 0xFF;
    uint8_t module_state_1 = 0xFF;
    uint8_t module_state_2 = 0xFF;


    std::string name;
};

#endif