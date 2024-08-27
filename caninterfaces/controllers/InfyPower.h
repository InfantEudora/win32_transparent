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

    //For testing purposes:
    void GenerateTestMessages(); //This fills the CAN Queue with some predefined test  messages.
    void CreateCANHeader(InfyPower_CanMessageHeader& header, uint8_t source, uint8_t destination,uint8_t command_no);
    static float ParseFloat(uint8_t* data);

    //Device state
    bool enabled = false;
    float output_voltage = -1;
    float output_current = 0;

    std::string name;
};

#endif