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

#define BIT_OUTPUT_SHORT         0
#define BIT_HARDWARE_FAILURE     1
#define BIT_INNER_COMM_INTERRUPT 2
#define BIT_PFC_SIDE_ABNORMAL    3
#define BIT_DISCHARGE_ABNORMAL   5
#define BIT_AIRDUCT_OBSTRUCTION  6

#define BIT_MDL_OFF              0
#define BIT_MDL_FAULT            1
#define BIT_MDL_PROTECT          2
#define BIT_FAN_FAULT            3
#define BIT_OVER_TEMPERATURE     4
#define BIT_OVER_VOLTAGE         5
#define BIT_WALKIN               6
#define BIT_COMM_INTERRUPT       7

#define FAN_MODE_POWER          0xA0
#define FAN_MODE_DENOISE        0xA1
#define FAN_MODE_QUIET          0xA2

class InfyPower{



public:
    int bus_id;

    bool UpdateDevice(can_frame_t& frame); //Returns true when a frame needs to be sent

    bool HandleCANMessage(can_frame_t* frame); //Decodes can message traffic for information

    static void BroadcastSetDialMode(can_frame_t& frame);
    static void QueryDevice(can_frame_t& frame, uint8_t command_no);
    static void ReadNrSystemModules(can_frame_t& frame);
    void SetOutput(can_frame_t& frame, uint32_t voltage_mv, uint32_t current_ma);
    void EnableOutput(can_frame_t& frame, bool enabled);
    void SetFanMode(can_frame_t& frame, uint8_t mode);

    //For testing purposes:
    void GenerateTestMessages(); //This fills the CAN Queue with some predefined test  messages.
    void CreateCANHeader(InfyPower_CanMessageHeader& header, uint8_t source, uint8_t destination,uint8_t command_no);
    static float ParseFloat(uint8_t* data);
    static uint32_t ParseU32(uint8_t* data);
    static uint16_t ParseU16(uint8_t* data);
    static void WriteU32(uint8_t* data,uint32_t value);

    //Device state

    float input_voltage = -1;
    float output_voltage = -1;
    float external_voltage = -1;    // Voltage after the output diode...
    float output_current = 0;
    float available_current = 0;
    float ambient_temperature = -1;

    bool output_enabled = false;
    bool output_enabled_read = false;

    uint32_t vout_setmv = 0;
    uint32_t vout_setmv_read = 0;

    uint32_t iout_setma = 0;
    uint32_t iout_setma_read = 0;

    uint8_t module_state_0 = 0xFF;
    uint8_t module_state_1 = 0xFF;
    uint8_t module_state_2 = 0xFF;

    std::string name;

    int message_state = 0;
};

#endif