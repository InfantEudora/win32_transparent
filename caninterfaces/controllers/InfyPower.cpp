#include <stdio.h>
#include "InfyPower.h"

#include "Debug.h"
static Debugger *debug = new Debugger("InfyPower", DEBUG_ALL);


//The ID is used to encode some values:
//Error 3 bits, dev. no 4 bits, command no 6 bits, dest 8,  source8

bool InfyPower::HandleCANMessage(can_frame_t* frame){
    InfyPower_CanMessageHeader header;
    header.error_code = (frame->can_id & (0b111 << 26)) >> 26;
    header.device_no = (frame->can_id & (0b1111 << 22)) >> 22;
    header.command_no = (frame->can_id & (0b111111 << 16)) >> 16;
    header.destination = (frame->can_id & (0xFF<<8)) >> 8;
    header.source = (frame->can_id & 0xFF);
    debug->Info("Frame: ER: %02X DEVNO: %02X COMND: %02X SRC: %02X DST: %02X DATA: %02X %02X %02X %02X\n",header.error_code,header.device_no,header.command_no,header.source,header.destination,
    frame->data[0],frame->data[1],frame->data[2],frame->data[3]);


    if (header.command_no == 0x03){
        output_voltage = ParseFloat(&frame->data[0]);
        output_current = ParseFloat(&frame->data[4]);
    }else if (header.command_no == 0x04){
        char t = frame->data[4];
        ambient_temperature = t;
        module_state_0 = frame->data[7];
        module_state_1 = frame->data[6];
        module_state_2 = frame->data[5];
    }else if (header.command_no == 0x06){
        //For DC/DC, only the first 16 bits are used.
        uint16_t v = ParseU16(&frame->data[0]);
        input_voltage = (float)v * 0.1f;

    }else if (header.command_no == 0x0C){
        uint16_t v = ParseU16(&frame->data[0]);
        external_voltage = (float)v * 0.1f;
        v = ParseU16(&frame->data[2]);
        available_current = (float)v * 0.1f;
    }

    return false;
};


//Fill the Queue with some predetermined test messages
void InfyPower::GenerateTestMessages(){
    //Test frames from the manual
    //02 83 F0 00 43 FA 00 00 40 60 00 00
    can_frame_t frame_03 = {
        .can_id =0x0283F000,
        .can_dlc = 8,
        .data = {0x43, 0xFA, 0x00, 0x00, 0x40, 0x60, 0x00, 0x00}
    };
    can_frame_t frame_04 = {
        .can_id =0x0284F001,
        .can_dlc = 8,
        .data = {0x00, 0x00, 0x02, 0x00, 0x1B, 0x00, 0x40, 0x00}
    };
    can_frame_t frame_06 = {
        .can_id =0x0286F001,
        .can_dlc = 8,
        .data = {0x0F, 0xB4, 0x0F, 0xA5, 0x0F, 0xA7, 0x00, 0x00}
    };
    can_frame_t frame_0C = {
        .can_id =0x028CF000,
        .can_dlc = 8,
        .data = {0x13, 0x58, 0x01, 0x66, 0x00, 0x00, 0x00, 0x00}
    };
    HandleCANMessage(&frame_03);
    HandleCANMessage(&frame_04);
    HandleCANMessage(&frame_06);
    HandleCANMessage(&frame_0C);
}

//Parse float data from a CAN frame
float InfyPower::ParseFloat(uint8_t* data){
    float a = 0;
    uint8_t* ap = (uint8_t*)&a;
    ap[0] = data[3];
    ap[1] = data[2];
    ap[2] = data[1];
    ap[3] = data[0];
    return a;
}

uint16_t InfyPower::ParseU16(uint8_t* data){
    uint16_t a = 0;
    uint8_t* ap = (uint8_t*)&a;
    ap[0] = data[1];
    ap[1] = data[0];
    return a;
}

void InfyPower::BroadcastSetDialMode(can_frame_t& frame){
    frame.can_dlc = 8;
    frame.can_id = 0x029F3FF0;
    frame.data[0] = 1;
    frame.data[1] = 0;
    frame.data[2] = 0;
    frame.data[3] = 0;
    frame.data[4] = 0;
    frame.data[5] = 0;
    frame.data[6] = 0;
    frame.data[7] = 0;
}